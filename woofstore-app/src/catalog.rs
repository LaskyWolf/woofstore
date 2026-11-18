//! The catalogue for Explore and the app pages. Searching, filtering and sorting run here; QML gets only the page of
//! results it shows, as JSON text. Loading, ratings and permissions run on threads.

#[cxx_qt::bridge]
pub mod qobject {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    #[auto_cxx_name]
    extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(bool, loading)]
        /// The catalogue has been read once
        #[qproperty(bool, ready)]
        /// Problems of the last load, one per line
        #[qproperty(QString, problems)]
        /// archlinux-appstream-data is missing: repository apps have no icons or descriptions
        #[qproperty(bool, metadata_missing)]
        /// The rankings come from the saved copy
        #[qproperty(bool, offline)]
        /// Grows when the catalogue, the rankings or the installed apps change; QML asks again then.
        #[qproperty(i32, revision)]
        /// JSON {"apps", "appsTotal", "others", "othersTotal"}: the answer to the last `search`
        #[qproperty(QString, results)]
        #[qproperty(bool, searching)]
        /// JSON {"key", "ratings", "permissions"} for the app `load_extras` was last called for
        #[qproperty(QString, extras)]
        type Catalog = super::CatalogRust;

        /// Reads the catalogue and the rankings.
        #[qinvokable]
        fn load(self: Pin<&mut Catalog>);

        /// Forgets saved web answers and reads the rankings again.
        #[qinvokable]
        fn refresh(self: Pin<&mut Catalog>);

        /// `Store.installed`, to know which apps are installed.
        #[qinvokable]
        fn set_installed(self: Pin<&mut Catalog>, json: &QString);

        /// Searches on a thread, into `results`. `source`: "All sources", "Repositories", "Flatpak" or "AUR"; `sort`: 0
        /// recommended, 1 name A–Z, 2 name Z–A. Each list holds at most its limit.
        #[qinvokable]
        fn search(self: Pin<&mut Catalog>, query: &QString, source: &QString, category: &QString, sort: i32, apps_limit: i32, others_limit: i32);

        /// JSON {"carousel": [app], "sections": [{"name", "apps"}]}: Trending, Popular, New and Updated on Flathub.
        #[qinvokable]
        fn featured(self: &Catalog) -> QString;

        /// JSON array of at most 8 app names that contain `text` (any case), most popular first: the search field's completer.
        #[qinvokable]
        fn suggest(self: &Catalog, text: &QString) -> QString;

        /// Everything the app page shows except ratings and permissions; "{}" for an unknown key.
        #[qinvokable]
        fn details(self: &Catalog, key: &QString) -> QString;

        /// Ratings and Flatpak permissions of one app, into `extras`.
        #[qinvokable]
        fn load_extras(self: Pin<&mut Catalog>, key: &QString);

        /// The operation for `Store.queueJob`: {"lane", "spec"}, or {"aur": package} for an AUR build, or {"error"}.
        /// `action`: "Install", "Reinstall" or "Remove"; `label`: the chosen source, "" for the default one.
        #[qinvokable]
        fn operation(self: &Catalog, key: &QString, action: &QString, label: &QString, remove_unused: bool) -> QString;

        /// What the remove dialog asks: {"name", "option", "leftovers": [{"path", "text", "checked"}]}, or {"error"}.
        #[qinvokable]
        fn remove_plan(self: &Catalog, key: &QString) -> QString;

        /// Files to delete once the removal `job` ends well (JSON array of paths).
        #[qinvokable]
        fn remember_leftovers(self: Pin<&mut Catalog>, job: i32, paths: &QString);

        /// Deletes the files remembered for `job` when it ended well. Returns how many, or -1 when there was nothing to do.
        #[qinvokable]
        fn job_ended(self: Pin<&mut Catalog>, job: i32, code: i32) -> i32;

        /// Starts an installed app. Returns a message when it cannot.
        /// What a desktop request asks for (an appstream: link, a .flatpakref or .flatpak file, or a name): {"key"} for a
        /// catalogue app, {"file", "name"} for a Flatpak file the catalogue does not have, {"search"} otherwise, {"wait": true}
        /// while the catalogue loads, {} for nothing.
        #[qinvokable]
        fn open_request(self: &Catalog, text: &QString) -> QString;

        /// The catalogue key of an installed app (`Store.installed` id and source), or "" when the catalogue does not list it.
        #[qinvokable]
        fn installed_key(self: &Catalog, id: &QString, source: &QString) -> QString;

        #[qinvokable]
        fn launch(self: &Catalog, key: &QString) -> QString;

        /// KDE's animation speed (kdeglobals [KDE] AnimationDurationFactor, 0 = no animations): page motions scale by it.
        #[qinvokable]
        fn animation_factor(self: &Catalog) -> f64;
    }

    impl cxx_qt::Threading for Catalog {}
}

use core::pin::Pin;
use cxx_qt::{CxxQtType, Threading};
use cxx_qt_lib::QString;
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::sync::Arc;
use woofstore_core::catalog::{app_score, category_group, rank_search, read_aur_cache, CatalogApp, MetadataOrigin, Package, Rankings, Ratings, Source};
use woofstore_core::install_specs::{choose_package, flatpak_permissions, install_job, package_label, readable_description, remove_job};
use woofstore_core::jobs::JobSpec;
use woofstore_core::leftovers::{delete_leftover, find_leftovers, flatpak_data, format_size};
use woofstore_core::news::plain_description;
use woofstore_system::appstream::Language;
use woofstore_system::catalogue::{self, flatpak_remotes};
use woofstore_system::rankings::{load_rankings, ratings};
use woofstore_system::update_check::{Sources, System};
use woofstore_system::{http, installed, paths};

/// The catalogue and what is derived from the rankings. Replaced as a whole when either changes.
#[derive(Default)]
struct Data {
    apps: Arc<Vec<CatalogApp>>,
    /// AUR packages without votes, left out of the catalogue: name -> version. An exact name still finds them.
    zero_vote: Arc<HashMap<String, String>>,
    /// The system installation's Flatpak remotes
    remotes: Arc<Vec<String>>,
    rankings: Rankings,
    scores: Vec<f64>,
    groups: Vec<&'static str>,
    /// Apps with metadata, most popular first
    by_score: Vec<usize>,
    index: HashMap<String, usize>,
}

impl Data {
    fn new(apps: Arc<Vec<CatalogApp>>, zero_vote: Arc<HashMap<String, String>>, remotes: Arc<Vec<String>>, rankings: Rankings) -> Self {
        let scores: Vec<f64> = apps.iter().map(|a| app_score(a, &rankings)).collect();
        let groups = apps.iter().map(|a| category_group(&a.categories.iter().map(String::as_str).collect::<Vec<_>>())).collect();
        let mut by_score: Vec<usize> = (0..apps.len()).filter(|&i| apps[i].origin != MetadataOrigin::None).collect();
        by_score.sort_by(|&a, &b| scores[b].total_cmp(&scores[a]));
        let index = apps.iter().enumerate().map(|(i, a)| (a.key.clone(), i)).collect();
        Data { apps, zero_vote, remotes, rankings, scores, groups, by_score, index }
    }
}

/// What is installed, as far as the catalogue's buttons need it.
#[derive(Default)]
struct Installed {
    /// pacman -Qqn
    native: HashSet<String>,
    /// (id, source, launcher) of the Installed page's rows
    rows: Vec<(String, String, String)>,
    /// The rows themselves, for the page of an app the catalogue does not list
    apps: Vec<Value>,
}

impl Installed {
    fn row(&self, package: &Package) -> Option<&(String, String, String)> {
        let kind = if package.source == Source::Flatpak { "Flatpak" } else { "AUR / local" };
        self.rows.iter().find(|(id, source, _)| *source == kind && *id == package.name)
    }

    fn has(&self, package: &Package) -> bool {
        match package.source {
            Source::Repo => self.native.contains(&package.name),
            _ => self.row(package).is_some(),
        }
    }
}

/// The catalogue and what is installed: what every answer is built from. Cheap to clone for a thread.
#[derive(Default, Clone)]
struct Snapshot {
    data: Arc<Data>,
    installed: Arc<Installed>,
}

#[derive(Default)]
pub struct CatalogRust {
    loading: bool,
    ready: bool,
    problems: QString,
    metadata_missing: bool,
    offline: bool,
    revision: i32,
    results: QString,
    searching: bool,
    extras: QString,
    now: Snapshot,
    installed_text: String,
    search_serial: u64,
    extras_key: String,
    /// "installed:<id>" -> looked-up (summary, website); an empty summary when nothing was found
    about: HashMap<String, (String, String)>,
    leftovers: HashMap<i32, Vec<PathBuf>>,
}

/// A theme icon name or a file address for Kirigami.Icon.
fn icon_source(app: &CatalogApp) -> String {
    if !app.icon_path.is_empty() {
        format!("file://{}", app.icon_path)
    } else if !app.icon.is_empty() {
        if app.icon.starts_with('/') {
            format!("file://{}", app.icon)
        } else {
            app.icon.clone()
        }
    } else if app.origin == MetadataOrigin::None {
        "package-x-generic".into()
    } else {
        "applications-other".into()
    }
}

/// "Repo", "Flatpak", "AUR": the kinds of source that provide the app, in the order of its packages.
fn source_kinds(app: &CatalogApp) -> Vec<&'static str> {
    let mut kinds = Vec::new();
    for package in &app.packages {
        let kind = match package.source {
            Source::Repo => "Repo",
            Source::Flatpak => "Flatpak",
            Source::Aur => "AUR",
        };
        if !kinds.contains(&kind) {
            kinds.push(kind);
        }
    }
    kinds
}

fn spec_json(lane: &str, spec: &JobSpec) -> Value {
    json!({ "lane": lane, "spec": { "title": spec.title, "program": spec.program.to_string_lossy(), "args": spec.args, "steps": spec.steps } })
}

/// The installs number as the ratings row shows it: "1.2 M", "34 K", "980".
fn compact(n: i64) -> String {
    if n >= 1_000_000 {
        format!("{:.1} M", n as f64 / 1e6)
    } else if n >= 10_000 {
        format!("{} K", n / 1000)
    } else {
        n.to_string()
    }
}

fn ratings_json(r: &Ratings) -> Value {
    let mut tiles = Vec::new();
    if let Some(installs) = r.installs {
        tiles.push(json!({ "value": compact(installs), "caption": "installs on Flathub" }));
        tiles.push(json!({ "value": compact(r.installs_month.unwrap_or(0)), "caption": "in the last month" }));
    }
    if let Some(percent) = r.repo_percent {
        tiles.push(json!({ "value": format!("{percent:.1} %"), "caption": "of Arch systems (pkgstats)" }));
    }
    if let Some(votes) = r.aur_votes {
        tiles.push(json!({ "value": votes.to_string(), "caption": format!("AUR votes · popularity {:.2}", r.aur_popularity) }));
    }
    if tiles.is_empty() && r.reviews == 0 {
        return Value::Null;
    }
    json!({ "stars": r.stars, "reviews": r.reviews, "histogram": r.histogram, "tiles": tiles, "daily": r.daily.iter().map(|(date, n)| json!({ "date": date, "n": n })).collect::<Vec<_>>() })
}

/// The links an app page lists, with their names and icons. Only web addresses with a host.
fn links_json(app: &CatalogApp) -> Vec<Value> {
    let mut links: Vec<(&str, &str)> = app.links.iter().map(|l| (l.kind.as_str(), l.url.as_str())).collect();
    if links.is_empty() && !app.homepage.is_empty() {
        links.push(("homepage", &app.homepage)); // AUR packages carry only a URL
    }
    links
        .into_iter()
        .filter(|(_, url)| url.strip_prefix("https://").or_else(|| url.strip_prefix("http://")).is_some_and(|rest| !rest.is_empty() && !rest.starts_with('/')))
        .map(|(kind, url)| {
            let (title, icon) = match kind {
                "homepage" => ("Project website", "internet-web-browser"),
                "help" => ("Help", "help-browser"),
                "translate" => ("Contribute translations", "preferences-desktop-locale"),
                "bugtracker" => ("Report an issue", "tools-report-bug"),
                "vcs-browser" => ("Browse the source code", "code-context"),
                "contribute" => ("Contribute to the app", "applications-development"),
                "donation" => ("Donate", "help-donate"),
                "faq" => ("Frequently asked questions", "help-contents"),
                "manifest" => ("Manifest", "code-context"),
                _ => ("Project link", "internet-web-browser"),
            };
            json!({ "title": title, "icon": icon, "url": url })
        })
        .collect()
}

/// The first paragraph of a description, at most 200 characters: the hover preview's extra line.
fn first_paragraph(text: &str) -> String {
    let line = text.lines().next().unwrap_or("").trim();
    if line.chars().count() > 200 {
        line.chars().take(197).collect::<String>() + "…"
    } else {
        line.to_string()
    }
}

fn error(text: String) -> QString {
    QString::from(&json!({ "error": text }).to_string())
}

impl Snapshot {
    /// The catalogue entry, or a bare AUR package without votes ("aur:<name>").
    fn app(&self, key: &str) -> Option<CatalogApp> {
        if let Some(&i) = self.data.index.get(key) {
            return Some(self.data.apps[i].clone());
        }
        let name = key.strip_prefix("aur:")?;
        let version = self.data.zero_vote.get(name)?;
        Some(CatalogApp {
            key: key.into(),
            name: name.into(),
            summary: "AUR package without votes or description".into(),
            packages: vec![Package::new(Source::Aur, name, "aur", version)],
            ..Default::default()
        })
    }

    fn summary(&self, app: &CatalogApp) -> Value {
        json!({
            "key": app.key, "name": app.name, "summary": app.summary, "icon": icon_source(app), "sources": source_kinds(app),
            "installed": app.packages.iter().any(|p| self.installed.has(p)), "screenshot": app.screenshots.first().map_or("", String::as_str),
            "group": category_group(&app.categories.iter().map(String::as_str).collect::<Vec<_>>()),
        })
    }

    /// The installed package of `app`, the last one when several are.
    fn installed_package<'a>(&self, app: &'a CatalogApp) -> Option<&'a Package> {
        app.packages.iter().rev().find(|p| self.installed.has(p))
    }

    fn search(&self, query: &str, source: &str, category: &str, sort: i32, apps_limit: i32, others_limit: i32) -> String {
        let d = &self.data;
        let query = query.trim();
        let wanted = match source {
            "Repositories" => Some(Source::Repo),
            "Flatpak" => Some(Source::Flatpak),
            "AUR" => Some(Source::Aur),
            _ => None,
        };
        let mut candidates = if query.is_empty() { d.by_score.clone() } else { rank_search(&d.apps, query, &d.scores) };
        if sort > 0 {
            candidates.sort_by_cached_key(|&i| d.apps[i].name.to_lowercase());
            if sort == 2 {
                candidates.reverse();
            }
        }
        let (mut apps, mut others) = (Vec::new(), Vec::new());
        for i in candidates {
            let app = &d.apps[i];
            if wanted.is_some_and(|s| !app.packages.iter().any(|p| p.source == s)) || (!category.is_empty() && d.groups[i] != category) {
                continue;
            }
            if app.origin == MetadataOrigin::None { &mut others } else { &mut apps }.push(app);
        }
        let mut extra = None;
        if !query.is_empty() && category.is_empty() && matches!(wanted, None | Some(Source::Aur)) {
            let listed = apps.iter().chain(&others).any(|a| a.packages.iter().any(|p| p.name == query));
            if !listed {
                extra = self.app(&format!("aur:{query}"));
            }
        }
        let mut other_rows: Vec<Value> = extra.iter().map(|a| self.summary(a)).collect();
        other_rows.extend(others.iter().take(others_limit.max(0) as usize).map(|a| self.summary(a)));
        other_rows.truncate(others_limit.max(0) as usize);
        let app_rows: Vec<Value> = apps.iter().take(apps_limit.max(0) as usize).map(|a| self.summary(a)).collect();
        let result = json!({ "apps": app_rows, "appsTotal": apps.len(), "others": other_rows, "othersTotal": others.len() + usize::from(extra.is_some()) });
        result.to_string()
    }
}

impl qobject::Catalog {
    fn bump(mut self: Pin<&mut Self>) {
        let next = self.revision() + 1;
        self.as_mut().set_revision(next);
    }

    fn load(mut self: Pin<&mut Self>) {
        if *self.loading() {
            return;
        }
        self.as_mut().set_loading(true);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let cache = paths::cache();
            let (rankings, (found, problems), remotes) = std::thread::scope(|scope| {
                let rankings = scope.spawn(|| load_rankings(&System, &cache));
                let remotes = scope.spawn(|| flatpak_remotes(&System).map(|(_, system)| system).unwrap_or_default());
                let found = catalogue::load(&System, &cache, &Language::from_environment());
                (rankings.join().unwrap_or_default(), found, remotes.join().unwrap_or_default())
            });
            // The AUR cache is written by the load above
            let zero_vote: HashMap<String, String> =
                read_aur_cache(&cache.join("aur.bin")).unwrap_or_default().into_iter().filter(|e| !e.detailed()).map(|e| (e.name, e.version)).collect();
            let missing = found.os_metadata_missing;
            let data = Data::new(Arc::new(found.apps), Arc::new(zero_vote), Arc::new(remotes), rankings);
            let problems = problems.join("\n");
            let _ = thread.queue(move |mut catalog| {
                let offline = data.rankings.offline;
                catalog.as_mut().rust_mut().now.data = Arc::new(data);
                catalog.as_mut().set_problems(QString::from(&problems));
                catalog.as_mut().set_metadata_missing(missing);
                catalog.as_mut().set_offline(offline);
                catalog.as_mut().set_ready(true);
                catalog.as_mut().set_loading(false);
                catalog.bump();
            });
        });
    }

    fn refresh(mut self: Pin<&mut Self>) {
        if !*self.ready() {
            self.load();
            return;
        }
        let d = &self.rust().now.data;
        let (apps, zero_vote, remotes) = (d.apps.clone(), d.zero_vote.clone(), d.remotes.clone());
        self.as_mut().set_loading(true);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let cache = paths::cache();
            http::forget(&cache);
            let data = Data::new(apps, zero_vote, remotes, load_rankings(&System, &cache));
            let _ = thread.queue(move |mut catalog| {
                let offline = data.rankings.offline;
                catalog.as_mut().rust_mut().now.data = Arc::new(data);
                catalog.as_mut().set_offline(offline);
                catalog.as_mut().set_loading(false);
                catalog.bump();
            });
        });
    }

    fn set_installed(mut self: Pin<&mut Self>, json: &QString) {
        let text = json.to_string();
        if text == self.rust().installed_text {
            return;
        }
        let parsed: Value = serde_json::from_str(&text).unwrap_or(Value::Null);
        let field = |row: &Value, key: &str| row[key].as_str().unwrap_or("").to_string();
        let rows: Vec<_> = parsed.as_array().into_iter().flatten().map(|r| (field(r, "id"), field(r, "source"), field(r, "launcher"))).collect();
        let apps = parsed.as_array().cloned().unwrap_or_default();
        self.as_mut().rust_mut().installed_text = text;
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let out = System.run("pacman", &["-Qqn"]);
            let native = out.stdout.lines().map(str::to_string).collect();
            let _ = thread.queue(move |mut catalog| {
                catalog.as_mut().rust_mut().now.installed = Arc::new(Installed { native, rows, apps });
                catalog.bump();
            });
        });
    }

    fn search(mut self: Pin<&mut Self>, query: &QString, source: &QString, category: &QString, sort: i32, apps_limit: i32, others_limit: i32) {
        // ponytail: every search runs to the end even when a newer one replaced it; cancel stale ones if typing feels slow.
        let serial = self.rust().search_serial + 1;
        self.as_mut().rust_mut().search_serial = serial;
        self.as_mut().set_searching(true);
        let now = self.rust().now.clone();
        let (query, source, category) = (query.to_string(), source.to_string(), category.to_string());
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let text = now.search(&query, &source, &category, sort, apps_limit, others_limit);
            let _ = thread.queue(move |mut catalog| {
                if catalog.rust().search_serial == serial {
                    catalog.as_mut().set_results(QString::from(&text));
                    catalog.set_searching(false);
                }
            });
        });
    }

    fn featured(&self) -> QString {
        let this = &self.rust().now;
        let d = &this.data;
        let mut by_flatpak: HashMap<&str, usize> = HashMap::new();
        for (i, app) in d.apps.iter().enumerate().filter(|(_, a)| a.origin != MetadataOrigin::None) {
            for package in app.packages.iter().filter(|p| p.source == Source::Flatpak) {
                by_flatpak.insert(&package.name, i);
            }
        }
        let ranked = |ids: &[String], limit: usize, needs_screenshot: bool, used: &mut Option<HashSet<usize>>| {
            let mut picks = Vec::new();
            for id in ids {
                let Some(&i) = by_flatpak.get(id.as_str()) else { continue };
                if used.as_ref().is_some_and(|u| u.contains(&i)) || (needs_screenshot && d.apps[i].screenshots.is_empty()) {
                    continue;
                }
                picks.push(i);
                if let Some(u) = used.as_mut() {
                    u.insert(i);
                }
                if picks.len() == limit {
                    break;
                }
            }
            picks
        };
        let mut carousel = ranked(&d.rankings.popular, 5, true, &mut None);
        if carousel.is_empty() {
            // Never downloaded rankings: the most popular apps with screenshots
            carousel = d.by_score.iter().copied().filter(|&i| !d.apps[i].screenshots.is_empty()).take(5).collect();
        }
        // Discovery tiles also carry the description's first paragraph for their hover preview
        let tile = |app: &CatalogApp| {
            let mut value = this.summary(app);
            value["detail"] = json!(first_paragraph(&plain_description(&app.description)));
            value
        };
        let mut used = Some(HashSet::new());
        let r = &d.rankings;
        let sections: Vec<Value> = [("Trending", &r.trending), ("Popular", &r.popular), ("New", &r.added), ("Updated", &r.updated)]
            .into_iter()
            .map(|(name, ids)| (name, ranked(ids, 20, false, &mut used)))
            .map(|(name, picks)| json!({ "name": name, "apps": picks.iter().map(|&i| tile(&d.apps[i])).collect::<Vec<_>>() }))
            .collect();
        let carousel: Vec<Value> = carousel.iter().map(|&i| this.summary(&d.apps[i])).collect();
        QString::from(&json!({ "carousel": carousel, "sections": sections }).to_string())
    }

    fn suggest(&self, text: &QString) -> QString {
        let d = &self.rust().now.data;
        let wanted = text.to_string().trim().to_lowercase();
        let mut names: Vec<&str> = Vec::new();
        if !wanted.is_empty() {
            for &i in &d.by_score {
                let name = d.apps[i].name.as_str();
                if name.to_lowercase().contains(&wanted) && !names.contains(&name) {
                    names.push(name);
                    if names.len() == 8 {
                        break;
                    }
                }
            }
        }
        QString::from(&json!(names).to_string())
    }

    fn animation_factor(&self) -> f64 {
        let file = paths::config().parent().map(|dir| dir.join("kdeglobals")).unwrap_or_default();
        animation_factor(&std::fs::read_to_string(file).unwrap_or_default())
    }

    fn installed_key(&self, id: &QString, source: &QString) -> QString {
        QString::from(installed_key(&self.rust().now.data.apps, &id.to_string(), &source.to_string()).unwrap_or_default())
    }

    fn open_request(&self, text: &QString) -> QString {
        QString::from(&open_request(&self.rust().now.data.apps, *self.ready(), &text.to_string()).to_string())
    }

    fn details(&self, key: &QString) -> QString {
        let this = &self.rust().now;
        let Some(app) = this.app(&key.to_string()).or_else(|| self.local_app(&key.to_string())) else { return QString::from("{}") };
        let mut labels: Vec<String> = Vec::new();
        let mut packages = Vec::new();
        for package in &app.packages {
            let label = package_label(package);
            if labels.contains(&label) {
                continue;
            }
            labels.push(label.clone());
            let text = if package.version.is_empty() { label.clone() } else { format!("{label} · {}", package.version) };
            packages.push(json!({ "label": label, "text": text, "aur": package.source == Source::Aur }));
        }
        let older: Vec<Value> = app
            .older_releases
            .iter()
            .map(|r| (r, plain_description(&r.notes).trim().to_string()))
            .filter(|(_, notes)| !notes.is_empty())
            .map(|(r, notes)| json!({ "version": r.version, "date": r.date, "notes": notes }))
            .collect();
        let mut result = this.summary(&app);
        let extra = json!({
            "description": readable_description(&app),
            "screenshots": app.screenshots,
            "releaseVersion": app.release_version,
            "releaseDate": app.release_date,
            "notes": plain_description(&app.release_notes).trim(),
            "older": older,
            "links": links_json(&app),
            "packages": packages,
            "chosen": choose_package(&app.packages, "").map(package_label).unwrap_or_default(),
        });
        if let (Some(target), Value::Object(more)) = (result.as_object_mut(), extra) {
            target.extend(more);
        }
        QString::from(&result.to_string())
    }

    /// An installed app the catalogue does not list ("installed:<id>"): what the system knows, then what `load_extras` found.
    fn local_app(&self, key: &str) -> Option<CatalogApp> {
        let id = key.strip_prefix("installed:")?;
        let row = self.rust().now.installed.apps.iter().find(|r| r["id"].as_str() == Some(id))?;
        let text = |name: &str| row[name].as_str().unwrap_or("").to_string();
        let (summary, homepage) = match self.rust().about.get(key) {
            Some((found, site)) if !found.is_empty() => (found.clone(), site.clone()),
            Some(_) if text("summary").is_empty() => ("No description found for this app.".into(), String::new()),
            None if text("summary").is_empty() => ("Looking for a description…".into(), String::new()),
            _ => (text("summary"), String::new()),
        };
        Some(CatalogApp { key: key.into(), name: text("name"), summary, icon: text("icon"), homepage, ..Default::default() })
    }

    fn load_extras(mut self: Pin<&mut Self>, key: &QString) {
        let key = key.to_string();
        if key.starts_with("installed:") {
            self.as_mut().rust_mut().extras_key = key.clone();
            self.as_mut().set_extras(QString::from("{}"));
            let found = self.rust().now.installed.apps.iter().find(|r| Some(r["id"].as_str().unwrap_or("")) == key.strip_prefix("installed:")).cloned();
            let Some(row) = found.filter(|_| !self.rust().about.contains_key(&key)) else { return };
            let text = |name: &str| row[name].as_str().unwrap_or("").to_string();
            let (id, name, source, summary) = (text("id"), text("name"), text("source"), text("summary"));
            // Apps whose launcher describes them only ask pacman
            let packaged = source == "Arch" || source == "AUR / local";
            if !packaged && !summary.is_empty() {
                return;
            }
            let thread = self.qt_thread();
            std::thread::spawn(move || {
                let found = installed::about_installed(&System, &id, &name, &source).unwrap_or_default();
                let _ = thread.queue(move |mut catalog| {
                    catalog.as_mut().rust_mut().about.insert(key, found);
                    catalog.bump();
                });
            });
            return;
        }
        let Some(app) = self.rust().now.app(&key) else { return };
        // Flatpak permissions come from the installed manifest; other sources have no sandbox to describe.
        let flatpak = app.packages.iter().find(|p| p.source == Source::Flatpak && self.rust().now.installed.has(p)).map(|p| p.name.clone());
        self.as_mut().rust_mut().extras_key = key.clone();
        self.as_mut().set_extras(QString::from("{}"));
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let found = ratings(&System, &paths::cache(), &app);
            let permissions = flatpak.map(|id| System.run("flatpak", &["info", "-M", &id])).filter(|out| out.ok()).map(|out| {
                let rows: Vec<Value> = flatpak_permissions(&out.stdout).iter().map(|p| json!({ "icon": p.icon, "name": p.name, "value": p.value })).collect();
                let note = if rows.is_empty() {
                    "This app asks for no special permissions."
                } else {
                    "From the installed Flatpak. Permissions may differ in other versions."
                };
                json!({ "note": note, "rows": rows })
            });
            let text = json!({ "key": key, "ratings": ratings_json(&found), "permissions": permissions }).to_string();
            let _ = thread.queue(move |mut catalog| {
                if catalog.rust().extras_key == key {
                    catalog.as_mut().set_extras(QString::from(&text));
                }
            });
        });
    }

    fn operation(&self, key: &QString, action: &QString, label: &QString, remove_unused: bool) -> QString {
        let this = &self.rust().now;
        let Some(app) = this.app(&key.to_string()) else { return error("This app is not in the catalogue.".into()) };
        let action = action.to_string();
        if action == "Remove" {
            let Some(package) = this.installed_package(&app) else { return error(format!("{} is not installed", app.name)) };
            return match remove_job(&app.name, package, remove_unused) {
                Ok((lane, spec)) => QString::from(&spec_json(lane, &spec).to_string()),
                Err(e) => error(e),
            };
        }
        let Some(package) = choose_package(&app.packages, &label.to_string()) else {
            return error(format!("{} has no package that can be installed here.", app.name));
        };
        if package.source == Source::Aur {
            return QString::from(&json!({ "aur": package.name }).to_string());
        }
        match install_job(&app.name, package, action == "Reinstall", &this.data.remotes) {
            Ok((lane, spec)) => QString::from(&spec_json(lane, &spec).to_string()),
            Err(e) => error(e),
        }
    }

    fn remove_plan(&self, key: &QString) -> QString {
        let this = &self.rust().now;
        let Some(app) = this.app(&key.to_string()) else { return error("This app is not in the catalogue.".into()) };
        let Some(package) = this.installed_package(&app) else { return error(format!("{} is not installed", app.name)) };
        let home = paths::home();
        let flatpak = package.source == Source::Flatpak;
        let mut found: Vec<_> = if flatpak { flatpak_data(&home, &package.name).into_iter().collect() } else { Vec::new() };
        found.extend(find_leftovers(&home, &[package.name.as_str(), app.name.as_str()]));
        let leftovers: Vec<Value> = found
            .iter()
            .map(|item| {
                let path = item.path.to_string_lossy();
                let shown = path.strip_prefix(&*home.to_string_lossy()).map_or(path.to_string(), |rest| format!("~{rest}"));
                json!({
                    "path": path, "text": format!("{} · {shown} · {}", item.kind, format_size(item.bytes)), "checked": item.checked_by_default,
                    // The fields RemoveWindow reads
                    "kind": item.kind, "bytes": item.bytes, "checkedByDefault": item.checked_by_default,
                })
            })
            .collect();
        let option = if flatpak { "Also remove runtimes no other app uses" } else { "Also remove dependencies nothing else needs" };
        QString::from(&json!({ "name": app.name, "option": option, "leftovers": leftovers }).to_string())
    }

    fn remember_leftovers(mut self: Pin<&mut Self>, job: i32, paths: &QString) {
        let list: Vec<PathBuf> = serde_json::from_str::<Vec<String>>(&paths.to_string()).unwrap_or_default().into_iter().map(PathBuf::from).collect();
        if !list.is_empty() {
            self.as_mut().rust_mut().leftovers.insert(job, list);
        }
    }

    fn job_ended(mut self: Pin<&mut Self>, job: i32, code: i32) -> i32 {
        let Some(list) = self.as_mut().rust_mut().leftovers.remove(&job) else { return -1 };
        if code != 0 {
            return -1;
        }
        let home = paths::home();
        list.iter().filter(|path| delete_leftover(&home, path)).count() as i32
    }

    fn launch(&self, key: &QString) -> QString {
        let this = &self.rust().now;
        let Some(app) = this.app(&key.to_string()) else { return QString::from("This app is not in the catalogue.") };
        let Some(package) = this.installed_package(&app) else { return QString::from(&format!("{} is not installed", app.name)) };
        let row_launcher = this.installed.rows.iter().find(|(id, _, launcher)| *id == package.name && !launcher.is_empty()).map(|r| PathBuf::from(&r.2));
        let (program, args): (&str, Vec<String>) = if package.source == Source::Flatpak {
            ("flatpak", vec!["run".into(), package.name.clone()])
        } else {
            let desktop =
                (!app.desktop_id.is_empty()).then(|| installed::launcher_dirs().into_iter().map(|d| d.join(&app.desktop_id)).find(|p| p.is_file())).flatten();
            match row_launcher.or(desktop) {
                Some(path) => ("gio", vec!["launch".into(), path.to_string_lossy().into_owned()]),
                None => return QString::from(&format!("{} · no launcher found", app.name)),
            }
        };
        match std::process::Command::new(program).args(&args).spawn() {
            Ok(mut child) => {
                std::thread::spawn(move || child.wait()); // Reaps it when it exits
                QString::default()
            }
            Err(e) => QString::from(&format!("Cannot start {}: {e}", app.name)),
        }
    }
}

/// `AnimationDurationFactor` of the [KDE] group in kdeglobals text, between 0 and 10; 1 when absent.
fn animation_factor(kdeglobals: &str) -> f64 {
    let mut in_kde = false;
    for line in kdeglobals.lines().map(str::trim) {
        if line.starts_with('[') {
            in_kde = line == "[KDE]";
        } else if let Some(value) = line.strip_prefix("AnimationDurationFactor=").filter(|_| in_kde) {
            return value.trim().parse::<f64>().map_or(1.0, |f| f.clamp(0.0, 10.0));
        }
    }
    1.0
}

/// See `Catalog::installed_key`: Flatpaks by app ID, Arch and AUR rows by package name, other launchers by desktop ID.
fn installed_key(apps: &[CatalogApp], id: &str, source: &str) -> Option<String> {
    let found = match source {
        "Flatpak" => apps.iter().find(|a| a.packages.iter().any(|p| p.source == Source::Flatpak && p.name == id)),
        "Arch" | "AUR / local" => apps.iter().find(|a| a.packages.iter().any(|p| p.source != Source::Flatpak && p.name == id)),
        _ => apps.iter().find(|a| !a.desktop_id.is_empty() && a.desktop_id.trim_end_matches(".desktop") == id),
    };
    found.map(|a| a.key.clone())
}

/// See `Catalog::open_request`.
fn open_request(apps: &[CatalogApp], ready: bool, text: &str) -> serde_json::Value {
    let text = text.trim();
    let path = text.strip_prefix("file://").unwrap_or(text);
    let lower = path.to_lowercase();
    if (lower.ends_with(".flatpakref") || lower.ends_with(".flatpak")) && std::path::Path::new(path).is_file() {
        if !ready {
            return json!({ "wait": true });
        }
        // A .flatpak bundle is binary; only a .flatpakref names its app
        let id = std::fs::read(path).ok().and_then(|bytes| woofstore_core::packages::flatpak_ref_id(&String::from_utf8_lossy(&bytes)));
        if let Some(app) = id.and_then(|id| apps.iter().find(|a| a.component_id.eq_ignore_ascii_case(&id))) {
            return json!({ "key": app.key });
        }
        let name = std::path::Path::new(path).file_stem().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        return json!({ "file": path, "name": name });
    }
    let mut wanted = text;
    if wanted.len() >= 10 && wanted[..10].eq_ignore_ascii_case("appstream:") {
        wanted = &wanted[10..];
    }
    let wanted = wanted.trim_start_matches('/');
    let wanted = wanted.strip_suffix(".desktop").unwrap_or(wanted);
    if wanted.is_empty() {
        return json!({});
    }
    if !ready {
        return json!({ "wait": true });
    }
    let same = |a: &str| !a.is_empty() && a.eq_ignore_ascii_case(wanted);
    match apps.iter().find(|a| same(&a.component_id) || same(&a.key) || same(a.desktop_id.trim_end_matches(".desktop"))) {
        Some(app) => json!({ "key": app.key }),
        None => json!({ "search": wanted }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn installed_rows_find_their_catalogue_entry() {
        let app = CatalogApp {
            key: "org.kde.krita".into(),
            desktop_id: "org.kde.krita.desktop".into(),
            packages: vec![Package::new(Source::Repo, "krita", "extra", "1"), Package::new(Source::Flatpak, "org.kde.krita", "flathub", "1")],
            ..Default::default()
        };
        let apps = [app];
        for (id, source) in [("krita", "Arch"), ("org.kde.krita", "Flatpak"), ("org.kde.krita", "Other")] {
            assert_eq!(installed_key(&apps, id, source).as_deref(), Some("org.kde.krita"), "{id} {source}");
        }
        assert_eq!(installed_key(&apps, "krita", "Flatpak"), None);
    }

    #[test]
    fn kde_animation_factor() {
        assert_eq!(animation_factor("[General]\nAnimationDurationFactor=3\n[KDE]\nAnimationDurationFactor=0.5\n"), 0.5);
        assert_eq!(animation_factor("[KDE]\nAnimationDurationFactor=0\n"), 0.0);
        assert_eq!(animation_factor("[General]\nAnimationDurationFactor=3\n"), 1.0);
    }

    #[test]
    fn desktop_requests() {
        let app = CatalogApp {
            key: "org.kde.krita".into(),
            component_id: "org.kde.krita.desktop".into(),
            desktop_id: "org.kde.krita.desktop".into(),
            ..Default::default()
        };
        let apps = [app];
        assert_eq!(open_request(&apps, true, "appstream://org.kde.krita.desktop")["key"], "org.kde.krita");
        assert_eq!(open_request(&apps, true, "  ORG.KDE.KRITA ")["key"], "org.kde.krita");
        assert_eq!(open_request(&apps, true, "gimp")["search"], "gimp");
        assert_eq!(open_request(&apps, false, "gimp")["wait"], true);
        assert_eq!(open_request(&apps, true, "appstream:"), json!({}));
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("krita.flatpakref");
        std::fs::write(&file, "[Flatpak Ref]\nName=org.kde.krita.desktop\n").unwrap();
        assert_eq!(open_request(&apps, true, &format!("file://{}", file.display()))["key"], "org.kde.krita");
        std::fs::write(&file, "[Flatpak Ref]\nName=org.other.App\n").unwrap();
        assert_eq!(open_request(&apps, true, file.to_str().unwrap())["name"], "krita");
    }
}
