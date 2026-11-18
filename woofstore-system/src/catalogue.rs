//! The whole catalogue: AppStream descriptions joined with what the pacman repositories, the Flatpak remotes and the
//! AUR actually provide, one entry per app across every source.

use crate::appstream::{self, Component, Language};
use crate::http::{self, Conditional};
use crate::update_check::Sources;
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};
use woofstore_core::catalog::{
    gunzip, normalize_component_id, parse_aur_dump, parse_sync_packages, read_aur_cache, write_aur_cache, AurEntry, CatalogApp, Link, MetadataOrigin, Package,
    Release, RepoPackage, Source,
};
use woofstore_core::merge::{load_merge_overrides, merge_catalog, MergeOverrides, MergeStats};

/// WoofStore's own metadata (the installed copy, then the source tree during development), plus folders listed in
/// WOOFSTORE_EXTRA_METADATA for testing with unpacked catalogues.
pub fn metadata_dirs() -> Vec<PathBuf> {
    let mut data_dirs = vec![crate::paths::data_home()];
    let system = std::env::var("XDG_DATA_DIRS").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| "/usr/local/share:/usr/share".into());
    data_dirs.extend(system.split(':').map(PathBuf::from));
    let mut dirs: Vec<PathBuf> = data_dirs.into_iter().map(|d| d.join("woofstore/metadata")).filter(|d| d.is_dir()).collect();
    let source = Path::new(env!("CARGO_MANIFEST_DIR")).join("../metadata");
    if source.is_dir() {
        dirs.push(source);
    }
    if let Ok(extra) = std::env::var("WOOFSTORE_EXTRA_METADATA") {
        dirs.extend(extra.split(':').filter(|s| !s.is_empty()).map(PathBuf::from));
    }
    dirs
}

/// Packages of every enabled repository, in pacman's repository order.
pub fn load_repos(sources: &dyn Sources) -> Result<Vec<RepoPackage>, String> {
    let order = sources.run("pacman-conf", &["--repo-list"]);
    if !order.ok() {
        return Err(format!("Cannot read the pacman repository list: {}", order.stderr.trim()));
    }
    let repos: Vec<&str> = order.stdout.lines().filter(|l| !l.is_empty()).collect();
    let packages = sources.run("expac", &["-S", "%r\t%n\t%v\t%d\t%u"]);
    match packages.code {
        0 => Ok(parse_sync_packages(&packages.stdout, &repos)),
        -1 => Err("Repository apps need expac. Install it with: sudo pacman -S --needed expac".into()),
        _ => Err(format!("Cannot list repository packages: {}", packages.stderr.trim())),
    }
}

/// "id/branch" -> (remote, version) of every app the Flatpak remotes offer, from their cached lists.
pub fn load_flatpak_offers(sources: &dyn Sources) -> Result<HashMap<String, (String, String)>, String> {
    let out = sources.run("flatpak", &["remote-ls", "--cached", "--app", "--columns=application,branch,origin,version"]);
    if !out.ok() {
        return Err(format!("Cannot identify cached Flatpak sources: {}", out.stderr.trim()));
    }
    Ok(out
        .stdout
        .lines()
        .filter_map(|line| {
            let f: Vec<&str> = line.split('\t').collect();
            (f.len() >= 4).then(|| (format!("{}/{}", f[0], f[1]), (f[2].to_string(), f[3].to_string())))
        })
        .collect())
}

/// The enabled Flatpak remotes: (per-user, system-wide).
pub fn flatpak_remotes(sources: &dyn Sources) -> Result<(Vec<String>, Vec<String>), String> {
    let out = sources.run("flatpak", &["remotes", "--columns=name,options"]);
    if !out.ok() {
        return Err(format!("Cannot read Flatpak sources: {}", out.stderr.trim()));
    }
    let (mut user, mut system) = (Vec::new(), Vec::new());
    for line in out.stdout.lines() {
        let f: Vec<&str> = line.split('\t').collect();
        if f.len() < 2 || f[1].contains("disabled") {
            continue;
        }
        if f[1].contains("user") { &mut user } else { &mut system }.push(f[0].to_string());
    }
    Ok((user, system))
}

pub const AUR_DUMP_URL: &str = "https://aur.archlinux.org/packages-meta-v1.json.gz";

fn modified(path: &Path) -> Option<SystemTime> {
    fs::metadata(path).and_then(|m| m.modified()).ok()
}

fn touch(path: &Path, time: SystemTime) {
    if let Ok(file) = fs::File::options().write(true).open(path) {
        let _ = file.set_modified(time);
    }
}

/// Every AUR package. The 50 MB dump downloads at most once a day and only when it changed; it is parsed once and the
/// compact cache is read on every other start. `download` gets the time of the saved dump. Returns the entries and a
/// notice for the user (empty when all went well).
pub fn load_aur(cache_dir: &Path, download: impl Fn(Option<SystemTime>) -> Conditional) -> (Vec<AurEntry>, String) {
    let _ = fs::create_dir_all(cache_dir);
    let (gz, bin) = (cache_dir.join("aur.json.gz"), cache_dir.join("aur.bin"));
    let fresh = modified(&bin).is_some_and(|time| SystemTime::now().duration_since(time).unwrap_or_default() < Duration::from_secs(24 * 3600));
    let build = |downloaded: Option<Vec<u8>>, notice: String| -> (Vec<AurEntry>, String) {
        if downloaded.is_none() {
            if let Some(entries) = read_aur_cache(&bin) {
                return (entries, notice);
            }
        }
        let Some(compressed) = downloaded.or_else(|| fs::read(&gz).ok()).filter(|d| !d.is_empty()) else {
            return (Vec::new(), "The AUR package list is not downloaded yet. Check your connection and restart WoofStore.".into());
        };
        let Some(json) = gunzip(&compressed) else {
            return (Vec::new(), "The downloaded AUR package list is damaged. It will download again on the next start.".into());
        };
        let Some(entries) = parse_aur_dump(&json) else { return (Vec::new(), "The AUR package list has an unexpected format.".into()) };
        if !write_aur_cache(&bin, &entries) {
            return (entries, "Cannot save the AUR cache; the list will be parsed again next start.".into());
        }
        (entries, notice)
    };
    if fresh {
        return build(None, String::new());
    }
    let since = if bin.exists() { modified(&gz) } else { None };
    match download(since) {
        Conditional::Changed { body, modified } => {
            if http::save(&gz, &body).is_ok() {
                // Server time, so the next If-Modified-Since matches the server's clock
                if let Some(time) = modified {
                    touch(&gz, time);
                }
            }
            build(Some(body), String::new())
        }
        Conditional::Unchanged => {
            touch(&bin, SystemTime::now()); // Unchanged upstream: the cache is fresh for another day
            build(None, String::new())
        }
        Conditional::Failed(error) => {
            let notice = if bin.exists() { format!("Cannot refresh the AUR package list ({error}). Showing the saved list.") } else { String::new() };
            build(None, notice)
        }
    }
}

/// Everything the catalogue is made of.
#[derive(Debug, Default)]
pub struct Parts {
    pub components: Vec<Component>,
    pub repos: Vec<RepoPackage>,
    pub aur: Vec<AurEntry>,
    /// "id/branch" -> (remote, version)
    pub flatpak_offers: HashMap<String, (String, String)>,
    pub overrides: MergeOverrides,
}

#[derive(Debug, Default)]
pub struct Catalogue {
    pub apps: Vec<CatalogApp>,
    pub stats: MergeStats,
    /// archlinux-appstream-data is not installed: repo apps show without descriptions or icons.
    pub os_metadata_missing: bool,
}

/// One entry per described app with every source that provides it, then the repo and AUR packages nothing describes,
/// then merged across sources.
pub fn assemble(parts: &Parts) -> Catalogue {
    let repo_by_name: HashMap<&str, &RepoPackage> = parts.repos.iter().map(|p| (p.name.as_str(), p)).collect();
    let aur_by_name: HashMap<&str, &AurEntry> = parts.aur.iter().map(|e| (e.name.as_str(), e)).collect();
    let (mut arch_claimed, mut aur_claimed, mut seen_flatpaks) = (HashSet::new(), HashSet::new(), HashSet::new());
    let mut apps = Vec::new();
    for component in &parts.components {
        let mut app = CatalogApp::default();
        let overlay = component.origin == "woofstore";
        if let Some((id, branch)) = component.flatpak_ref() {
            // Flathub added as both a system and a user remote lists every app twice
            if !seen_flatpaks.insert(format!("{id}/{branch}")) {
                continue;
            }
            // The remote comes from `flatpak remote-ls`: newer Flatpak writes origin="flatpak" into every remote's catalogue,
            // and `flatpak install flatpak <id>` finds no such remote.
            let offer = parts.flatpak_offers.get(&format!("{id}/{branch}"));
            let remote = offer.map_or(component.origin.as_str(), |(r, _)| r.as_str());
            app.packages.push(Package::new(Source::Flatpak, &id, remote, offer.map_or("", |(_, v)| v.as_str())));
        } else if component.flatpak_bundle.is_some() {
            continue;
        }
        for name in &component.pkgnames {
            if let Some(package) = repo_by_name.get(name.as_str()) {
                app.packages.push(Package::new(Source::Repo, &package.name, &package.repo, &package.version));
                if !overlay {
                    arch_claimed.insert(name.clone());
                }
            } else if let Some(entry) = aur_by_name.get(name.as_str()) {
                // Our overlay describes AUR packages by name
                app.packages.push(Package { popularity: entry.popularity, ..Package::new(Source::Aur, &entry.name, "aur", &entry.version) });
                aur_claimed.insert(name.clone());
            }
        }
        if app.packages.is_empty() {
            continue; // Metadata for a package that no enabled repo provides
        }
        app.key = normalize_component_id(&component.id);
        app.name = component.name.clone();
        app.summary = component.summary.clone();
        app.description = component.description.clone();
        app.homepage = component.urls.iter().find(|(kind, _)| kind == "homepage").map(|(_, u)| u.clone()).unwrap_or_default();
        // Arch generates many entries from desktop files without a website; the package URL still identifies the project.
        if app.homepage.is_empty() {
            if let Some(package) = app.packages.iter().find(|p| p.source == Source::Repo) {
                app.homepage = repo_by_name[package.name.as_str()].url.clone();
            }
        }
        for kind in ["homepage", "vcs-browser", "bugtracker", "help", "donation", "translate", "contribute", "faq"] {
            if let Some((_, url)) = component.urls.iter().find(|(k, url)| k == kind && (url.starts_with("https://") || url.starts_with("http://"))) {
                app.links.push(Link { kind: kind.into(), url: url.clone() });
            }
        }
        app.categories = component.categories.clone();
        app.keywords = component.keywords.clone();
        app.component_id = component.id.clone();
        app.desktop_id = component.desktop_id.clone().unwrap_or_default();
        if let Some(release) = component.releases.first() {
            app.release_version = release.version.clone();
            app.release_date = if release.timestamp > 0 { http::iso_date(release.timestamp) } else { String::new() };
            app.release_notes = release.description.clone();
            app.older_releases = component.releases[1..]
                .iter()
                .take(7)
                .map(|older| Release {
                    version: older.version.clone(),
                    date: if older.timestamp > 0 { http::iso_date(older.timestamp) } else { String::new() },
                    notes: older.description.chars().take(4000).collect(),
                })
                .collect();
        }
        app.icon_path = component.cached_icon().map(|p| p.to_string_lossy().into_owned()).unwrap_or_default();
        app.screenshots = component.screenshot_urls(4);
        app.origin = if overlay { MetadataOrigin::Overlay } else { MetadataOrigin::AppStream };
        apps.push(app);
    }
    // Stray files in /usr/share/swcatalog can name packages; only Arch's catalogue matches repo packages.
    let os_metadata_missing = arch_claimed.is_empty();
    let claimed: HashSet<String> = apps.iter().flat_map(|a| &a.packages).filter(|p| p.source == Source::Repo).map(|p| p.name.clone()).collect();
    for package in parts.repos.iter().filter(|p| !claimed.contains(&p.name)) {
        apps.push(CatalogApp {
            key: format!("pkg:{}", package.name),
            name: package.name.clone(),
            summary: package.description.clone(),
            packages: vec![Package::new(Source::Repo, &package.name, &package.repo, &package.version)],
            ..Default::default()
        });
    }
    for entry in parts.aur.iter().filter(|e| e.detailed() && !aur_claimed.contains(&e.name)) {
        apps.push(CatalogApp {
            key: format!("aur:{}", entry.name),
            name: entry.name.clone(),
            summary: entry.description.clone(),
            homepage: entry.url.clone(),
            packages: vec![Package { popularity: entry.popularity, ..Package::new(Source::Aur, &entry.name, "aur", &entry.version) }],
            ..Default::default()
        });
    }
    let (apps, stats) = merge_catalog(apps, &parts.overrides);
    Catalogue { apps, stats, os_metadata_missing }
}

/// Reads every source side by side and assembles the catalogue. Problems are reported, and the sources that worked
/// still make a catalogue.
pub fn load(sources: &dyn Sources, cache_dir: &Path, language: &Language) -> (Catalogue, Vec<String>) {
    let dirs = metadata_dirs();
    let mut problems = Vec::new();
    let parts = std::thread::scope(|scope| {
        let components = scope.spawn(|| appstream::load_all(&appstream::catalogue_files(&crate::paths::home(), &dirs), language));
        let repos = scope.spawn(|| load_repos(sources));
        let offers = scope.spawn(|| load_flatpak_offers(sources));
        let aur = scope.spawn(|| load_aur(cache_dir, |since| http::get_if_modified(AUR_DUMP_URL, since, Duration::from_secs(60))));
        let (components, catalogue_problems) = components.join().unwrap_or_default();
        problems.extend(catalogue_problems.into_iter().map(|p| format!("Cannot read a catalogue: {p}")));
        let repos = repos.join().unwrap_or_else(|_| Err("The repository list stopped unexpectedly".into())).unwrap_or_else(|e| {
            problems.push(e);
            Vec::new()
        });
        let flatpak_offers = offers.join().unwrap_or_else(|_| Err("The Flatpak list stopped unexpectedly".into())).unwrap_or_else(|e| {
            problems.push(e);
            HashMap::new()
        });
        let (aur, notice) = aur.join().unwrap_or_default();
        if !notice.is_empty() {
            problems.push(notice);
        }
        let dir_refs: Vec<&Path> = dirs.iter().map(PathBuf::as_path).collect();
        Parts { components, repos, aur, flatpak_offers, overrides: load_merge_overrides(&dir_refs) }
    });
    (assemble(&parts), problems)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::command::Output;

    struct Fake(HashMap<&'static str, Output>);

    impl Sources for Fake {
        fn run(&self, program: &str, _: &[&str]) -> Output {
            self.0.get(program).cloned().unwrap_or(Output { code: -1, stdout: String::new(), stderr: "missing".into() })
        }
        fn get(&self, _: &str, _: u64) -> Result<Vec<u8>, String> {
            Err("offline".into())
        }
    }

    fn ok(stdout: &str) -> Output {
        Output { code: 0, stdout: stdout.into(), stderr: String::new() }
    }

    #[test]
    fn repos_flatpak_offers_and_remotes() {
        let fake = Fake(HashMap::from([
            ("pacman-conf", ok("core\nextra\n")),
            ("expac", ok("extra\tkrita\t5.2-1\tPaint\thttps://krita.org\n")),
            ("flatpak", ok("org.kde.krita\tstable\tflathub\t5.2.9\n")),
        ]));
        assert_eq!(load_repos(&fake).unwrap()[0].name, "krita");
        assert_eq!(load_flatpak_offers(&fake).unwrap()["org.kde.krita/stable"], ("flathub".to_string(), "5.2.9".to_string()));
        let remotes = Fake(HashMap::from([("flatpak", ok("flathub\tsystem\nmine\tuser\nold\tsystem,disabled\n"))]));
        assert_eq!(flatpak_remotes(&remotes).unwrap(), (vec!["mine".to_string()], vec!["flathub".to_string()]));
        let no_expac = Fake(HashMap::from([("pacman-conf", ok("core\n"))]));
        assert!(load_repos(&no_expac).unwrap_err().contains("need expac"));
    }

    fn component(id: &str, origin: &str, pkgnames: &[&str], bundle: Option<&str>) -> Component {
        Component {
            id: id.into(),
            origin: origin.into(),
            name: id.into(),
            pkgnames: pkgnames.iter().map(|s| s.to_string()).collect(),
            flatpak_bundle: bundle.map(Into::into),
            ..Default::default()
        }
    }

    #[test]
    fn sources_join_into_one_entry_and_leftovers_get_their_own() {
        let parts = Parts {
            components: vec![
                component("org.kde.krita", "flatpak", &[], Some("app/org.kde.krita/x86_64/stable")), // Newer Flatpak's origin
                component("org.kde.krita", "flathub-user", &[], Some("app/org.kde.krita/x86_64/stable")), // The same remote twice
                component("org.kde.krita.desktop", "archlinux-arch-extra", &["krita"], None),
                component("org.example.Gone", "archlinux-arch-extra", &["gone"], None), // No repo has it
                component("org.cachyos.hello", "woofstore", &["fancy-aur"], None),
            ],
            repos: vec![
                RepoPackage {
                    name: "krita".into(),
                    repo: "extra".into(),
                    version: "5.2-1".into(),
                    description: "Paint".into(),
                    url: "https://krita.org".into(),
                },
                RepoPackage { name: "zsh".into(), repo: "extra".into(), version: "5.9".into(), description: "Shell".into(), url: String::new() },
            ],
            aur: vec![
                AurEntry { name: "fancy-aur".into(), version: "1".into(), votes: 5, popularity: 1.5, ..Default::default() },
                AurEntry { name: "other-aur".into(), version: "2".into(), votes: 3, description: "Other".into(), ..Default::default() },
                AurEntry { name: "quiet-aur".into(), version: "3".into(), votes: 0, ..Default::default() }, // No votes: search only
            ],
            flatpak_offers: HashMap::from([("org.kde.krita/stable".into(), ("flathub".into(), "5.2.9".into()))]),
            overrides: MergeOverrides::default(),
        };
        let catalogue = assemble(&parts);
        let keys: Vec<&str> = catalogue.apps.iter().map(|a| a.key.as_str()).collect();
        assert!(!catalogue.os_metadata_missing);
        assert!(keys.contains(&"pkg:zsh") && keys.contains(&"aur:other-aur") && !keys.iter().any(|k| k.contains("quiet") || k.contains("Gone")));
        let krita = catalogue.apps.iter().find(|a| a.packages.iter().any(|p| p.name == "krita")).unwrap();
        let sources: Vec<Source> = krita.packages.iter().map(|p| p.source).collect();
        assert!(sources.contains(&Source::Flatpak) && sources.contains(&Source::Repo), "{sources:?}"); // Flathub and Arch merged
        assert_eq!(krita.packages.iter().filter(|p| p.source == Source::Flatpak).count(), 1);
        assert_eq!(krita.packages.iter().find(|p| p.source == Source::Flatpak).unwrap().repo, "flathub"); // The remote, not the origin
        assert_eq!(krita.homepage, "https://krita.org"); // From the package when the metadata has no website
        let hello = catalogue.apps.iter().find(|a| a.key == "org.cachyos.hello").unwrap();
        assert_eq!((hello.origin, hello.packages[0].source, hello.packages[0].popularity), (MetadataOrigin::Overlay, Source::Aur, 1.5));
    }

    #[test]
    fn aur_dump_downloads_once_a_day_and_only_when_changed() {
        use flate2::{write::GzEncoder, Compression};
        use std::cell::Cell;
        use std::io::Write;
        let cache = tempfile::tempdir().unwrap();
        let mut gz = GzEncoder::new(Vec::new(), Compression::fast());
        gz.write_all(br#"[{"Name":"foo","Version":"1","NumVotes":2,"Description":"Foo","URL":"https://foo","Popularity":0.5}]"#).unwrap();
        let body = gz.finish().unwrap();
        let calls = Cell::new(0);
        let (entries, notice) = load_aur(cache.path(), |since| {
            calls.set(calls.get() + 1);
            assert!(since.is_none());
            Conditional::Changed { body: body.clone(), modified: None }
        });
        assert_eq!((entries.len(), notice.as_str()), (1, ""));
        // Within a day the cache is read and nothing is downloaded
        let (entries, _) = load_aur(cache.path(), |_| panic!("downloaded again"));
        assert_eq!(entries[0].name, "foo");
        // A day later the server says nothing changed: the cache is used and marked fresh
        touch(&cache.path().join("aur.bin"), SystemTime::now() - Duration::from_secs(2 * 24 * 3600));
        let (entries, _) = load_aur(cache.path(), |since| {
            assert!(since.is_some());
            Conditional::Unchanged
        });
        assert_eq!(entries.len(), 1);
        assert_eq!(calls.get(), 1);
        // Offline with an old cache: the saved list and a notice
        touch(&cache.path().join("aur.bin"), SystemTime::now() - Duration::from_secs(2 * 24 * 3600));
        let (entries, notice) = load_aur(cache.path(), |_| Conditional::Failed("offline".into()));
        assert!(entries.len() == 1 && notice.contains("offline"));
        // Nothing saved and offline
        let empty = tempfile::tempdir().unwrap();
        let (entries, notice) = load_aur(empty.path(), |_| Conditional::Failed("offline".into()));
        assert!(entries.is_empty() && notice.contains("not downloaded yet"));
    }
}
