//! Pending updates of every source, System Update and what follows it, and the operations of the Installed page
//! (open, remove with leftovers, orphan packages). Operations come back as JSON {"lane", "title", "program", "args",
//! "steps"}; QML queues them with `store.queueJob(op.lane, JSON.stringify(op))`.

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
        /// An update check is running
        #[qproperty(bool, loading)]
        /// At least one check ended
        #[qproperty(bool, checked)]
        /// "HH:mm" of the last check
        #[qproperty(QString, checked_at)]
        /// JSON array of the updates found: {"id", "name", "from", "to", "source", "url"}
        #[qproperty(QString, entries)]
        #[qproperty(i32, count)]
        /// Updates of Arch and AUR packages: the system itself is behind
        #[qproperty(i32, system_count)]
        /// Sources the last check could not read, one per line
        #[qproperty(QString, problems)]
        /// JSON array of orphan packages {"name", "size"}
        #[qproperty(QString, orphans)]
        #[qproperty(bool, orphans_loading)]
        /// The service restart that follows a System Update is queued; the store restarts after it when it must
        #[qproperty(bool, restart_after_services)]
        type Updates = super::UpdatesRust;

        /// Checks every source on a worker thread. `installed`: the store's installed list (JSON), for Flatpak versions
        /// and the AppImages to compare.
        #[qinvokable]
        fn check(self: Pin<&mut Updates>, installed: &QString);

        /// The rows of the Updates page (JSON), named and grouped with the help of the installed list.
        #[qinvokable]
        fn rows(self: &Updates, installed: &QString) -> QString;

        /// The System Update confirmation text. `last_update`: ISO date of the last System Update, or empty.
        #[qinvokable]
        fn system_update_text(self: &Updates, last_update: &QString) -> QString;

        #[qinvokable]
        fn system_update_job(self: &Updates, aur: bool, devel: bool) -> QString;

        /// The update of one Flatpak, AppImage or the store; empty when it only updates with System Update.
        #[qinvokable]
        fn update_one_job(self: &Updates, id: &QString, source: &QString) -> QString;

        /// The option the removal dialog offers for an installed app (JSON row); empty when it has no package.
        #[qinvokable]
        fn removal_option(self: &Updates, app: &QString) -> QString;

        /// {"error"} or the removal operation.
        #[qinvokable]
        fn removal_job(self: &Updates, app: &QString, extra: bool) -> QString;

        /// Files the app left in the home folder, as RemoveWindow takes them: JSON [{"kind", "path", "bytes", "checkedByDefault"}].
        #[qinvokable]
        fn leftovers(self: &Updates, app: &QString) -> QString;

        /// Reinstalls an installed app (JSON row) from where it came from: the operation, {"aur": package} for an AUR
        /// build, or {"error"}.
        #[qinvokable]
        fn reinstall_job(self: &Updates, app: &QString) -> QString;

        /// Sample results for screenshots: "idle", "checking", "failed", "available" or "apps".
        #[qinvokable]
        fn preview(self: Pin<&mut Updates>, state: &QString);

        /// Leftovers to delete once the removal `job_id` ends well. `paths`: JSON array.
        #[qinvokable]
        fn remember_leftovers(self: Pin<&mut Updates>, job_id: i32, name: &QString, paths: &QString);

        /// What follows an ended operation, once per id (empty the second time): JSON {"next": "services" | "afterUpgrade"
        /// | "recheck" | "none", "services": [{"name", "risky"}], "systemDone", "message"}.
        #[qinvokable]
        fn job_ended(self: Pin<&mut Updates>, id: i32, code: i32, title: &QString, log: &QString) -> QString;

        /// Deletes an AppImage (a file elsewhere goes to the trash) and its launcher. Empty, or why it failed.
        #[qinvokable]
        fn remove_app_image(self: &Updates, app: &QString) -> QString;

        /// Starts an installed app. Empty, or why it could not.
        #[qinvokable]
        fn open_app(self: &Updates, app: &QString) -> QString;

        #[qinvokable]
        fn find_orphans(self: Pin<&mut Updates>);

        #[qinvokable]
        fn orphan_removal_job(self: &Updates, names: &QString) -> QString;

        #[qinvokable]
        fn restart_services_job(self: &Updates, services: &QString) -> QString;

        /// This process runs libraries or its own program from files an upgrade replaced.
        #[qinvokable]
        fn must_restart(self: &Updates) -> bool;

        /// Starts a new copy of the store in a moment; QML quits right after.
        #[qinvokable]
        fn restart_store(self: &Updates) -> bool;

        #[qinvokable]
        fn has_program(self: &Updates, name: &QString) -> bool;

        #[qsignal]
        fn orphans_found(self: Pin<&mut Updates>);
    }

    impl cxx_qt::Threading for Updates {}
}

use chrono::{Local, NaiveDate};
use core::pin::Pin;
use cxx_qt::{CxxQtType, Threading};
use cxx_qt_lib::QString;
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::process::Command;
use woofstore_core::catalog::{Package, Source};
use woofstore_core::install_specs::install_job;
use woofstore_core::jobs::runs_replaced_files;
use woofstore_core::leftovers::{delete_leftover, find_leftovers, flatpak_data};
use woofstore_core::news::{parse_cachy_blog, parse_feed, NewsItem, NEWS_SOURCES};
use woofstore_core::system_specs::{self as specs, InstalledApp};
use woofstore_core::updates::{parse_services_to_restart, UpdateEntry};
use woofstore_system::activity::find_executable;
use woofstore_system::catalogue::flatpak_remotes;
use woofstore_system::update_check::{check_all, InstalledImage, Sources, System};
use woofstore_system::{aur_remote, paths};

pub struct UpdatesRust {
    loading: bool,
    checked: bool,
    checked_at: QString,
    entries: QString,
    count: i32,
    system_count: i32,
    problems: QString,
    orphans: QString,
    orphans_loading: bool,
    restart_after_services: bool,
    found: Vec<UpdateEntry>,
    /// Important news, newest first, for the System Update confirmation
    news: Vec<NewsItem>,
    /// Removal job id -> app name and the leftovers to delete after it
    leftovers: HashMap<i32, (String, Vec<String>)>,
    handled: HashSet<i32>,
}

impl Default for UpdatesRust {
    fn default() -> Self {
        Self {
            loading: false,
            checked: false,
            checked_at: QString::default(),
            entries: QString::from("[]"),
            count: 0,
            system_count: 0,
            problems: QString::default(),
            orphans: QString::from("[]"),
            orphans_loading: false,
            restart_after_services: false,
            found: Vec::new(),
            news: Vec::new(),
            leftovers: HashMap::new(),
            handled: HashSet::new(),
        }
    }
}

fn installed_list(text: &QString) -> Vec<InstalledApp> {
    serde_json::from_str(&text.to_string()).unwrap_or_default()
}

fn installed_app(text: &QString) -> InstalledApp {
    serde_json::from_str(&text.to_string()).unwrap_or_default()
}

fn strings(text: &QString) -> Vec<String> {
    serde_json::from_str(&text.to_string()).unwrap_or_default()
}

fn text(value: &Value) -> QString {
    QString::from(&value.to_string())
}

/// The store's program, which imports AppImages. pacman may have replaced it, and Linux then adds " (deleted)" to its path.
fn store_program() -> String {
    let path = std::env::current_exe().map(|p| p.to_string_lossy().replace(" (deleted)", "")).unwrap_or_default();
    if Path::new(&path).is_file() {
        return path;
    }
    find_executable("woof-store").map(|p| p.to_string_lossy().into_owned()).unwrap_or(path)
}

/// Important news only (Arch Linux, CachyOS), newest first.
fn important_news(sources: &dyn Sources) -> Vec<NewsItem> {
    let mut items: Vec<NewsItem> = NEWS_SOURCES
        .iter()
        .filter(|source| source.important)
        .flat_map(|source| {
            let data = sources.get(source.url, 0).unwrap_or_default();
            if source.html {
                parse_cachy_blog(&String::from_utf8_lossy(&data))
            } else {
                parse_feed(&data, source.name, source.important)
            }
        })
        .collect();
    items.sort_by_key(|item| std::cmp::Reverse(item.date));
    items
}

/// Lets a started program run on its own; a thread waits for it so it leaves no zombie behind.
fn start_detached(program: &str, args: &[&str]) -> bool {
    match Command::new(program).args(args).spawn() {
        Ok(mut child) => {
            std::thread::spawn(move || child.wait());
            true
        }
        Err(_) => false,
    }
}

impl qobject::Updates {
    fn check(mut self: Pin<&mut Self>, installed: &QString) {
        if self.rust().loading {
            return;
        }
        self.as_mut().set_loading(true);
        let apps = installed_list(installed);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let versions: HashMap<String, String> = apps.iter().filter(|a| a.source == "Flatpak").map(|a| (a.id.clone(), a.version.clone())).collect();
            let images: Vec<InstalledImage> = apps
                .iter()
                .filter(|a| a.source == "AppImage" && !a.path.is_empty())
                .map(|a| InstalledImage { id: a.id.clone(), name: a.name.clone(), path: PathBuf::from(&a.path) })
                .collect();
            let (found, news) = std::thread::scope(|scope| {
                let news = scope.spawn(|| important_news(&System));
                let found = check_all(&System, &versions, &images, env!("CARGO_PKG_VERSION"));
                (found, news.join().unwrap_or_default())
            });
            let time = Local::now().format("%H:%M").to_string();
            let _ = thread.queue(move |mut updates| {
                let entries: Vec<Value> =
                    found.entries.iter().map(|e| json!({ "id": e.id, "name": e.name, "from": e.from, "to": e.to, "source": e.source, "url": e.url })).collect();
                let system = found.entries.iter().filter(|e| e.source == "Arch" || e.source == "AUR").count();
                updates.as_mut().set_count(found.entries.len() as i32);
                updates.as_mut().set_system_count(system as i32);
                updates.as_mut().set_problems(QString::from(&found.problems.join("\n")));
                updates.as_mut().set_checked_at(QString::from(&time));
                {
                    let mut rust = updates.as_mut().rust_mut();
                    rust.found = found.entries;
                    rust.news = news;
                }
                updates.as_mut().set_entries(text(&Value::from(entries)));
                updates.as_mut().set_checked(true);
                updates.set_loading(false);
            });
        });
    }

    fn rows(&self, installed: &QString) -> QString {
        text(&Value::from(specs::update_rows(&self.rust().found, &installed_list(installed))))
    }

    fn system_update_text(&self, last_update: &QString) -> QString {
        let since = NaiveDate::parse_from_str(&last_update.to_string(), "%Y-%m-%d").ok();
        let headlines = specs::news_since(&self.rust().news, since, Local::now().date_naive());
        QString::from(&specs::system_update_text(&self.rust().found, &headlines))
    }

    fn system_update_job(&self, aur: bool, devel: bool) -> QString {
        text(&specs::system_update_job(&self.rust().found, aur, devel, &store_program()))
    }

    fn update_one_job(&self, id: &QString, source: &QString) -> QString {
        let (id, source) = (id.to_string(), source.to_string());
        self.rust()
            .found
            .iter()
            .find(|e| e.id == id && e.source == source)
            .and_then(|e| specs::update_one_job(e, &store_program()))
            .map_or_else(QString::default, |job| text(&job))
    }

    fn removal_option(&self, app: &QString) -> QString {
        QString::from(specs::removal_option(&installed_app(app)).unwrap_or(""))
    }

    fn removal_job(&self, app: &QString, extra: bool) -> QString {
        text(&specs::removal_job(&installed_app(app), extra).unwrap_or_else(|error| json!({ "error": error })))
    }

    fn leftovers(&self, app: &QString) -> QString {
        let app = installed_app(app);
        let home = paths::home();
        let mut found = find_leftovers(&home, &[&app.id, &app.name]);
        if app.source == "Flatpak" {
            found.splice(0..0, flatpak_data(&home, &app.id)); // The app's own data folder first
        }
        let rows: Vec<Value> = found
            .iter()
            .map(|item| json!({ "kind": item.kind, "path": item.path.to_string_lossy(), "bytes": item.bytes, "checkedByDefault": item.checked_by_default }))
            .collect();
        text(&Value::from(rows))
    }

    fn reinstall_job(&self, app: &QString) -> QString {
        let row: Value = serde_json::from_str(&app.to_string()).unwrap_or_default();
        let field = |key: &str| row[key].as_str().unwrap_or("").to_string();
        let (id, name, source) = (field("id"), field("name"), field("source"));
        let package = match source.as_str() {
            "Flatpak" => Package::new(Source::Flatpak, &id, &field("remote"), ""),
            "Arch" => Package::new(Source::Repo, &id, "arch", ""),
            "AUR / local" => return text(&json!({ "aur": id })),
            _ => return text(&json!({ "error": format!("{name} cannot be reinstalled here") })),
        };
        let remotes = if source == "Flatpak" { flatpak_remotes(&System).map(|(_, system)| system).unwrap_or_default() } else { Vec::new() };
        match install_job(&name, &package, true, &remotes) {
            Ok((lane, spec)) => text(&json!({ "lane": lane, "title": spec.title, "program": spec.program, "args": spec.args, "steps": spec.steps })),
            Err(error) => text(&json!({ "error": error })),
        }
    }

    fn preview(mut self: Pin<&mut Self>, state: &QString) {
        let state = state.to_string();
        let entry = |id: &str, name: &str, from: &str, to: &str, source: &str, url: &str| UpdateEntry {
            id: id.into(),
            name: name.into(),
            from: from.into(),
            to: to.into(),
            source: source.into(),
            url: url.into(),
        };
        let mut found = Vec::new();
        if state == "available" || state == "apps" {
            found = vec![
                entry("org.kde.krita", "Krita", "5.2.9", "5.3.0", "Flatpak", ""),
                entry("org.gimp.GIMP", "GIMP", "3.0.4", "3.0.6", "Flatpak", ""),
                entry("abc123", "Lotion", "", "v1.7.0", "AppImage", "https://example.invalid/Lotion.AppImage"),
                entry("systemd", "systemd", "262-1", "262.1-1", "Arch", ""),
                entry("linux-cachyos", "linux-cachyos", "7.2.9-2", "7.2.11-1", "Arch", ""),
                entry("paru", "paru", "2.0.4-1", "2.1.0-1", "AUR", ""),
            ];
            if state == "apps" {
                found.truncate(3); // Only apps: the system button stays grey
            }
        }
        let entries: Vec<Value> =
            found.iter().map(|e| json!({ "id": e.id, "name": e.name, "from": e.from, "to": e.to, "source": e.source, "url": e.url })).collect();
        let system = found.iter().filter(|e| e.source == "Arch" || e.source == "AUR").count();
        self.as_mut().set_count(found.len() as i32);
        self.as_mut().set_system_count(system as i32);
        self.as_mut().set_problems(QString::from(if state == "failed" { "Flatpak: the remote is not reachable." } else { "" }));
        self.as_mut().set_checked_at(QString::from("09:41"));
        self.as_mut().rust_mut().found = found;
        self.as_mut().set_entries(text(&Value::from(entries)));
        self.as_mut().set_checked(state != "checking");
        self.set_loading(state == "checking");
    }

    fn remember_leftovers(mut self: Pin<&mut Self>, job_id: i32, name: &QString, paths: &QString) {
        let chosen = strings(paths);
        if job_id > 0 && !chosen.is_empty() {
            self.as_mut().rust_mut().leftovers.insert(job_id, (name.to_string(), chosen));
        }
    }

    fn job_ended(mut self: Pin<&mut Self>, id: i32, code: i32, title: &QString, log: &QString) -> QString {
        let (title, log) = (title.to_string(), log.to_string());
        let mut rust = self.as_mut().rust_mut();
        if !rust.handled.insert(id) {
            return QString::default();
        }
        let ok = code == 0;
        let mut message = String::new();
        if let Some((name, chosen)) = rust.leftovers.remove(&id).filter(|_| ok) {
            let home = paths::home();
            let removed = chosen.iter().filter(|path| delete_leftover(&home, Path::new(path))).count();
            message = format!("{name} removed · {removed} leftover item(s) deleted");
        }
        // The upgrade itself finished, whatever the later steps did
        let system_done = title == "System Update" && log.contains("==> Cache and services");
        let mut services = Vec::new();
        let next = if system_done {
            services = parse_services_to_restart(&log).into_iter().map(|name| json!({ "name": name, "risky": specs::risky_service(&name) })).collect();
            "services"
        } else if ok && title == "Update WoofStore" {
            "afterUpgrade" // The new version starts instead of the old one
        } else if title == "Restart services" {
            if ok && rust.restart_after_services {
                "afterUpgrade"
            } else {
                "none"
            }
        } else if title.starts_with("Update") {
            "recheck"
        } else {
            "none"
        };
        if title == "Restart services" {
            self.as_mut().set_restart_after_services(false);
        }
        text(&json!({ "next": next, "services": services, "systemDone": system_done, "message": message }))
    }

    fn remove_app_image(&self, app: &QString) -> QString {
        let app = installed_app(app);
        let data = paths::data_home().join("WoofStore/appimages");
        let path = Path::new(&app.path);
        let ok = if app.path.is_empty() {
            false
        } else if path.starts_with(&data) {
            std::fs::remove_file(path).is_ok()
        } else {
            // A file the user keeps elsewhere goes to the trash, not away for good
            Command::new("gio").args(["trash", "--", &app.path]).status().is_ok_and(|s| s.success())
        };
        if !ok {
            return QString::from(&format!("The file could not be deleted: {}", app.path));
        }
        if !app.launcher.is_empty() && Path::new(&app.launcher).starts_with(paths::home()) {
            let _ = std::fs::remove_file(&app.launcher);
        }
        QString::default()
    }

    fn open_app(&self, app: &QString) -> QString {
        let app = installed_app(app);
        if !app.launcher.is_empty() && find_executable("gio").is_some() && start_detached("gio", &["launch", &app.launcher]) {
            return QString::default();
        }
        if app.source == "Flatpak" && start_detached("flatpak", &["run", &app.id]) {
            return QString::default();
        }
        QString::from(&format!("{} · no launcher found", app.name))
    }

    fn find_orphans(mut self: Pin<&mut Self>) {
        if self.rust().orphans_loading {
            return;
        }
        self.as_mut().set_orphans_loading(true);
        let thread = self.qt_thread();
        std::thread::spawn(move || {
            let found: Vec<Value> = aur_remote::orphans(&System).iter().map(|o| json!({ "name": o.name, "size": o.size })).collect();
            let _ = thread.queue(move |mut updates| {
                updates.as_mut().set_orphans(text(&Value::from(found)));
                updates.as_mut().set_orphans_loading(false);
                updates.orphans_found();
            });
        });
    }

    fn orphan_removal_job(&self, names: &QString) -> QString {
        specs::orphan_removal_job(&strings(names)).map_or_else(QString::default, |job| text(&job))
    }

    fn restart_services_job(&self, services: &QString) -> QString {
        specs::restart_services_job(&strings(services)).map_or_else(QString::default, |job| text(&job))
    }

    fn must_restart(&self) -> bool {
        runs_replaced_files(&std::fs::read_to_string("/proc/self/maps").unwrap_or_default())
    }

    fn restart_store(&self) -> bool {
        start_detached("sh", &["-c", "sleep 1.5; exec \"$0\"", &store_program()])
    }

    fn has_program(&self, name: &QString) -> bool {
        find_executable(&name.to_string()).is_some()
    }
}
