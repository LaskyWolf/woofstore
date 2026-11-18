//! One catalogue entry per app across Flathub, the pacman repos and the AUR: entries that are the same app are folded
//! together and every package stays listed separately.

use crate::catalog::{json_str, letter_or_number, normalize_component_id, CatalogApp, MetadataOrigin, Source};
use regex::Regex;
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::path::Path;
use std::sync::LazyLock;

/// Hand-written corrections from metadata/merge-overrides.json.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct MergeOverrides {
    /// package name -> AppStream ID of the entry it belongs to
    pub merge: HashMap<String, String>,
    /// package names that are never merged automatically
    pub separate: HashSet<String>,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct MergeStats {
    pub by_id: usize,
    pub by_name: usize,
    pub by_homepage: usize,
    pub by_override: usize,
    /// entries offered by two or more sources
    pub multi_source: usize,
    /// same name as an app but a different project: review by hand
    pub near_misses: Vec<String>,
    /// "rule: package -> entry", for reviewing merges by hand
    pub merges: Vec<String>,
}

/// "krita-git" -> "krita"
pub fn strip_variant_suffix(name: &str) -> String {
    static SUFFIX: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"-(git|bin|appimage|nightly|beta|dev)$").unwrap());
    SUFFIX.replace(name, "").into_owned()
}

/// "Visual Studio Code" -> "visualstudiocode"
pub fn name_key(text: &str) -> String {
    text.to_lowercase().chars().filter(|&c| letter_or_number(c)).collect()
}

/// The host (lower case, IPv6 without brackets) and decoded path of an address, as QUrl gives them; `None` when it has
/// no host.
fn host_and_path(address: &str) -> Option<(String, String)> {
    let url = url::Url::parse(address.trim()).ok()?;
    let host = url.host_str()?.trim_start_matches('[').trim_end_matches(']').to_lowercase();
    let path = percent_encoding::percent_decode_str(url.path()).decode_utf8_lossy().into_owned();
    Some((host, path))
}

/// A project's identity: the host, or host/owner/repo on code forges.
pub fn project_key(homepage: &str) -> String {
    let Some((host, path)) = host_and_path(homepage) else { return String::new() };
    let host = host.strip_prefix("www.").map(str::to_string).unwrap_or(host);
    if host.is_empty() {
        return String::new();
    }
    // On shared code hosts the host alone says nothing; the owner/repo path identifies the project.
    const FORGES: [&str; 9] = [
        "github.com",
        "gitlab.com",
        "codeberg.org",
        "sourceforge.net",
        "gitlab.gnome.org",
        "invent.kde.org",
        "bitbucket.org",
        "git.sr.ht",
        "gitlab.freedesktop.org",
    ];
    if !FORGES.contains(&host.as_str()) {
        return host;
    }
    let path = path.to_lowercase();
    let parts: Vec<&str> = path.split('/').filter(|p| !p.is_empty()).collect();
    if parts.len() < 2 {
        return String::new();
    }
    let repo = parts[1].strip_suffix(".git").unwrap_or(parts[1]);
    format!("{host}/{}/{repo}", parts[0])
}

/// The project key plus "name:<publisher>" tokens: short names that identify who publishes a project. The site name
/// (appflowy.com and appflowy.io -> "appflowy"), the subdomain on hosting services (shmmodbus.github.io -> "shmmodbus"),
/// and the owner on code forges.
pub fn identity_tokens(url: &str) -> HashSet<String> {
    let project = project_key(url);
    let mut tokens = HashSet::from([project.clone()]);
    let host = host_and_path(url).map(|(host, _)| host).unwrap_or_default();
    let mut labels: Vec<&str> = host.split('.').filter(|l| !l.is_empty()).collect();
    if labels.first() == Some(&"www") {
        labels.remove(0);
    }
    if labels.len() >= 2 {
        const HOSTING_SUFFIXES: [&str; 10] = [
            "github.io",
            "gitlab.io",
            "sourceforge.net",
            "sourceforge.io",
            "itch.io",
            "codeberg.page",
            "netlify.app",
            "vercel.app",
            "pages.dev",
            "readthedocs.io",
        ];
        const TWO_PART_ENDINGS: [&str; 6] = ["co.uk", "org.uk", "com.br", "com.au", "co.jp", "com.cn"];
        let ending = labels[labels.len() - 2..].join(".");
        let mut sections = project.split('/');
        let forge = sections.next().unwrap_or("");
        if !forge.is_empty() && project.contains('/') {
            tokens.insert(format!("name:{}", sections.next().unwrap_or(""))); // forge owner
        } else if HOSTING_SUFFIXES.contains(&ending.as_str()) || TWO_PART_ENDINGS.contains(&ending.as_str()) {
            if labels.len() >= 3 {
                tokens.insert(format!("name:{}", labels[labels.len() - 3]));
            }
        } else {
            tokens.insert(format!("name:{}", labels[labels.len() - 2]));
        }
    }
    tokens.remove("");
    tokens
}

fn app_identity_tokens(app: &CatalogApp) -> HashSet<String> {
    let mut tokens = identity_tokens(&app.homepage);
    // A bug tracker like github.com/owner/repo/issues still names the repository.
    for link in app.links.iter().filter(|l| l.kind != "donation" && l.kind != "translate") {
        tokens.extend(identity_tokens(&link.url));
    }
    tokens
}

pub fn load_merge_overrides(dirs: &[&Path]) -> MergeOverrides {
    let mut overrides = MergeOverrides::default();
    for dir in dirs {
        let Ok(text) = std::fs::read(dir.join("merge-overrides.json")) else { continue };
        let root = crate::catalog::json_object(&text);
        if let Some(merge) = root.get("merge").and_then(Value::as_object) {
            for (name, id) in merge {
                overrides.merge.insert(name.clone(), normalize_component_id(&json_str(Some(id))));
            }
        }
        for name in root.get("separate").and_then(Value::as_array).into_iter().flatten() {
            overrides.separate.insert(json_str(Some(name)));
        }
    }
    overrides
}

fn source_rank(source: Source) -> u8 {
    match source {
        Source::Repo => 0,
        Source::Flatpak => 1,
        Source::Aur => 2,
    }
}

fn provides(app: &CatalogApp, source: Source) -> bool {
    app.packages.iter().any(|p| p.source == source)
}

/// Keeps the richer metadata of the two and lists both entries' packages.
fn absorb(target: &mut CatalogApp, other: &CatalogApp) {
    target.packages.extend(other.packages.iter().cloned());
    // Lengths in UTF-16 units, as Qt counts them.
    if other.origin != MetadataOrigin::None && other.description.encode_utf16().count() > target.description.encode_utf16().count() {
        target.description = other.description.clone();
        if !other.screenshots.is_empty() {
            target.screenshots = other.screenshots.clone();
        }
    }
    if target.summary.is_empty() {
        target.summary = other.summary.clone();
    }
    if target.icon_path.is_empty() {
        target.icon_path = other.icon_path.clone();
    }
    if target.homepage.is_empty() {
        target.homepage = other.homepage.clone();
    }
    for link in &other.links {
        if !target.links.contains(link) {
            target.links.push(link.clone());
        }
    }
    for category in &other.categories {
        if !target.categories.contains(category) {
            target.categories.push(category.clone());
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum Rule {
    Override,
    Name,
    Homepage,
}

/// Folds entries that are the same app into one entry; every package stays listed separately.
pub fn merge_catalog(apps: Vec<CatalogApp>, overrides: &MergeOverrides) -> (Vec<CatalogApp>, MergeStats) {
    let mut count = MergeStats::default();

    // Rule 1: entries with metadata and the same AppStream ID are one app.
    let (mut merged, mut loose) = (Vec::<CatalogApp>::new(), Vec::<CatalogApp>::new());
    let mut by_key: HashMap<String, usize> = HashMap::new();
    for app in apps {
        if app.origin == MetadataOrigin::None {
            loose.push(app);
        } else if let Some(&found) = by_key.get(&app.key) {
            absorb(&mut merged[found], &app);
            count.by_id += 1;
        } else {
            by_key.insert(app.key.clone(), merged.len());
            merged.push(app);
        }
    }

    // Rule 3 between two metadata entries: Arch names many apps after their desktop file (obsidian.desktop) where
    // Flathub uses reverse DNS (md.obsidian.Obsidian). Same name, same project and complementary sources make one app.
    let mut flatpak_only_by_name: HashMap<String, Vec<usize>> = HashMap::new();
    for (i, app) in merged.iter().enumerate() {
        if provides(app, Source::Flatpak) && !provides(app, Source::Repo) {
            flatpak_only_by_name.entry(name_key(&app.name)).or_default().push(i);
        }
    }
    let mut absorbed: HashSet<usize> = HashSet::new();
    for i in 0..merged.len() {
        if provides(&merged[i], Source::Flatpak) || !provides(&merged[i], Source::Repo) {
            continue;
        }
        let project = app_identity_tokens(&merged[i]);
        for &candidate in flatpak_only_by_name.get(&name_key(&merged[i].name)).into_iter().flatten() {
            if absorbed.contains(&candidate) || provides(&merged[candidate], Source::Repo) || app_identity_tokens(&merged[candidate]).is_disjoint(&project) {
                continue;
            }
            count.merges.push(format!("project: {} -> {}", merged[i].key, merged[candidate].name));
            let other = merged[i].clone();
            absorb(&mut merged[candidate], &other);
            absorbed.insert(i);
            count.by_homepage += 1;
            break;
        }
    }
    if !absorbed.is_empty() {
        merged = merged.into_iter().enumerate().filter(|(i, _)| !absorbed.contains(i)).map(|(_, app)| app).collect();
        by_key = merged.iter().enumerate().map(|(i, app)| (app.key.clone(), i)).collect();
    }

    let mut by_repo_package: HashMap<String, usize> = HashMap::new();
    let (mut by_flatpak_name, mut by_display_name): (HashMap<String, Vec<usize>>, HashMap<String, Vec<usize>>) = Default::default();
    // Flatpak ID endings that name a role, not an app: com.spotify.Client is not the "client" package.
    const GENERIC_ENDINGS: [&str; 13] =
        ["client", "app", "desktop", "browser", "editor", "player", "studio", "launcher", "manager", "viewer", "tool", "tools", "gui"];
    for (i, app) in merged.iter().enumerate() {
        for package in &app.packages {
            if package.source == Source::Repo {
                by_repo_package.insert(package.name.clone(), i);
            }
            let ending = package.name.rsplit('.').next().unwrap_or("").to_lowercase();
            if package.source == Source::Flatpak && ending.encode_utf16().count() >= 3 && !GENERIC_ENDINGS.contains(&ending.as_str()) {
                by_flatpak_name.entry(ending).or_default().push(i);
            }
        }
        by_display_name.entry(name_key(&app.name)).or_default().push(i);
    }

    for app in loose {
        let name = app.packages.first().map(|p| p.name.clone()).unwrap_or_default();
        if overrides.separate.contains(&name) {
            merged.push(app);
            continue;
        }
        let mut target: Option<(usize, Rule)> = None;
        if let Some(id) = overrides.merge.get(&name) {
            target = by_key.get(id).map(|&t| (t, Rule::Override));
        }
        // Rule 2: package-name rules (krita-git -> the app that ships the krita package).
        let base = strip_variant_suffix(&name);
        if target.is_none() && base != name {
            if let Some(&t) = by_repo_package.get(&base) {
                target = Some((t, Rule::Name));
            }
        }
        // Rule 3: same name (display name or Flatpak ID ending) AND the same project (homepage, source code or
        // bug tracker). Short names like "buzz" belong to many projects, so a name alone is not enough.
        let mut same_name = by_display_name.get(&name_key(&base)).cloned().unwrap_or_default();
        for &candidate in by_flatpak_name.get(&base).into_iter().flatten() {
            if !same_name.contains(&candidate) {
                same_name.push(candidate);
            }
        }
        if target.is_none() {
            let project = identity_tokens(&app.homepage);
            target = same_name.iter().find(|&&c| !app_identity_tokens(&merged[c]).is_disjoint(&project)).map(|&c| (c, Rule::Homepage));
            if let (None, Some(&first), Some(Source::Aur)) = (target, same_name.first(), app.packages.first().map(|p| p.source)) {
                count.near_misses.push(format!("{name} ~ {} ({} vs {})", merged[first].name, project_key(&app.homepage), project_key(&merged[first].homepage)));
            }
        }
        if let Some((t, rule)) = target {
            let label = match rule {
                Rule::Name => "name",
                Rule::Homepage => "project",
                Rule::Override => "override",
            };
            count.merges.push(format!("{label}: {name} -> {}", merged[t].name));
            absorb(&mut merged[t], &app);
            *match rule {
                Rule::Name => &mut count.by_name,
                Rule::Homepage => &mut count.by_homepage,
                Rule::Override => &mut count.by_override,
            } += 1;
            continue;
        }
        merged.push(app);
    }

    for app in &mut merged {
        app.packages.sort_by_key(|p| source_rank(p.source)); // stable
        let sources: HashSet<Source> = app.packages.iter().map(|p| p.source).collect();
        if sources.len() > 1 {
            count.multi_source += 1;
        }
    }
    (merged, count)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::Package;

    fn app_stream(id: &str, name: &str, homepage: &str, package: Package) -> CatalogApp {
        CatalogApp {
            key: id.into(),
            name: name.into(),
            homepage: homepage.into(),
            packages: vec![package],
            origin: MetadataOrigin::AppStream,
            ..Default::default()
        }
    }

    fn aur_app(name: &str, homepage: &str) -> CatalogApp {
        CatalogApp {
            key: format!("aur:{name}"),
            name: name.into(),
            homepage: homepage.into(),
            packages: vec![Package::new(Source::Aur, name, "aur", "1")],
            ..Default::default()
        }
    }

    fn meet(a: &str, b: &str) -> bool {
        !identity_tokens(a).is_disjoint(&identity_tokens(b))
    }

    #[test]
    fn keys() {
        assert_eq!(strip_variant_suffix("krita-git"), "krita");
        assert_eq!(strip_variant_suffix("android-studio"), "android-studio");
        assert_eq!(project_key("https://www.GitHub.com/Owner/Repo.git/issues"), "github.com/owner/repo");
        assert_eq!(project_key("https://www.krita.org/en/"), "krita.org");
        assert_eq!(project_key("not a url"), "");
        assert!(meet("https://appflowy.io", "https://appflowy.com"));
        assert!(meet("https://shmmodbus.github.io", "https://github.com/shmmodbus/shm_modbus"));
        assert!(!meet("https://github.com/block/buzz", "https://github.com/chidiwilliams/buzz"));
        assert_eq!(name_key("Visual Studio Code"), "visualstudiocode");
    }

    #[test]
    fn merge_rules() {
        let (merged, stats) = merge_catalog(
            vec![
                app_stream("org.kde.krita", "Krita", "https://krita.org", Package::new(Source::Flatpak, "org.kde.krita", "flathub", "6")),
                app_stream("org.kde.krita", "Krita", "https://krita.org", Package::new(Source::Repo, "krita", "extra", "6")),
                aur_app("krita-git", "https://krita.org"), // rule 2: repo package name
                app_stream("io.notes.one", "Notes", "https://notes-one.example", Package::new(Source::Flatpak, "io.notes.one", "flathub", "1")),
                aur_app("notes", "https://github.com/someone-else/notes"), // same name, other project: stays apart
                app_stream(
                    "com.example.Buzz",
                    "Buzz",
                    "https://github.com/chidiwilliams/buzz",
                    Package::new(Source::Flatpak, "com.example.Buzz", "flathub", "1"),
                ),
                aur_app("buzz-bin", "https://chidiwilliams.github.io/buzz"), // rule 3: same name and publisher
                aur_app("buzz-appimage", "https://github.com/block/buzz"),   // same name, other publisher: stays apart
            ],
            &MergeOverrides::default(),
        );
        assert_eq!(merged.len(), 5); // Krita, Notes, Buzz, plus the unrelated AUR "notes" and "buzz-appimage"
        let krita = &merged[0];
        assert!(krita.packages.len() == 3 && krita.packages[0].source == Source::Repo && krita.packages[2].name == "krita-git");
        assert!(stats.by_id == 1 && stats.by_name == 1 && stats.by_homepage == 1 && stats.near_misses.len() == 2 && stats.multi_source == 2);

        let keep_apart = MergeOverrides { separate: HashSet::from(["krita-git".to_string()]), ..Default::default() };
        let kept = merge_catalog(
            vec![
                app_stream("org.kde.krita", "Krita", "https://krita.org", Package::new(Source::Repo, "krita", "extra", "6")),
                aur_app("krita-git", "https://krita.org"),
            ],
            &keep_apart,
        );
        assert_eq!(kept.0.len(), 2);

        let (two_ids, _) = merge_catalog(
            vec![
                app_stream("md.obsidian.obsidian", "Obsidian", "https://obsidian.md", Package::new(Source::Flatpak, "md.obsidian.Obsidian", "flathub", "1")),
                app_stream("obsidian", "Obsidian", "https://obsidian.md/", Package::new(Source::Repo, "obsidian", "extra", "1")),
                app_stream("notes", "Notes", "https://a.example", Package::new(Source::Repo, "notes", "extra", "1")),
                app_stream("org.b.notes", "Notes", "https://b.example", Package::new(Source::Flatpak, "org.b.Notes", "flathub", "1")),
            ],
            &MergeOverrides::default(),
        );
        // other projects stay apart
        assert!(two_ids.len() == 3 && two_ids[0].packages.len() == 2 && two_ids[0].packages[0].source == Source::Repo);
    }

    #[test]
    fn overrides_from_the_metadata_folder() {
        let metadata = Path::new(env!("CARGO_MANIFEST_DIR")).join("../metadata");
        let overrides = load_merge_overrides(&[metadata.as_path(), Path::new("/nonexistent")]);
        assert_eq!(overrides.merge["seafile-client"], "com.seafile.client"); // normalized like AppStream IDs
        assert!(overrides.separate.is_empty());

        let (merged, stats) = merge_catalog(
            vec![
                app_stream("com.seafile.client", "Seafile", "https://www.seafile.com", Package::new(Source::Flatpak, "com.seafile.Client", "flathub", "9")),
                aur_app("seafile-client", "https://example.org/other"),
            ],
            &overrides,
        );
        assert!(merged.len() == 1 && stats.by_override == 1 && stats.merges == ["override: seafile-client -> Seafile"]);
    }
}
