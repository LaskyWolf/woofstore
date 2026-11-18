//! What is installed: Flatpaks, imported AppImages and one row per application in the menu, named after its package
//! when a pacman package owns the launcher.

use crate::update_check::Sources;
use regex::Regex;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::LazyLock;
use woofstore_core::appimage_import::imported_app_images;
use woofstore_core::catalog::App;
use woofstore_core::launchers::{launcher_program, scan_launchers, Launcher};

#[derive(Debug, Default)]
pub struct Installed {
    /// Sorted by name
    pub apps: Vec<App>,
    /// Packages from the repositories (pacman -Qqn), so the store knows which catalogue entries are installed.
    pub native: HashSet<String>,
    pub problems: Vec<String>,
}

/// Where launchers live: the XDG application folders plus both Flatpak export folders.
pub fn launcher_dirs() -> Vec<PathBuf> {
    let home = crate::paths::home();
    let mut dirs = vec![crate::paths::data_home().join("applications")];
    let system = std::env::var("XDG_DATA_DIRS").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| "/usr/local/share:/usr/share".into());
    dirs.extend(system.split(':').map(|d| Path::new(d).join("applications")));
    for flatpak in [PathBuf::from("/var/lib/flatpak/exports/share/applications"), home.join(".local/share/flatpak/exports/share/applications")] {
        if !dirs.contains(&flatpak) {
            dirs.push(flatpak);
        }
    }
    dirs
}

fn icon_or(launcher: &Launcher, fallback: &str) -> String {
    if launcher.icon.is_empty() {
        fallback.into()
    } else {
        launcher.icon.clone()
    }
}

static OWNED: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(.+) is owned by (\S+) (\S+)$").unwrap());

pub fn load_installed(sources: &dyn Sources, launchers: &[Launcher], data_root: &Path, home: &Path) -> Installed {
    let mut result = Installed::default();
    let mut apps = imported_app_images(data_root);
    let flatpaks = sources.run("flatpak", &["list", "--app", "--columns=application,name,version,origin,installation,branch"]);
    if flatpaks.ok() {
        for line in flatpaks.stdout.lines() {
            let f: Vec<&str> = line.split('\t').collect();
            if f.len() < 6 {
                continue;
            }
            apps.push(App {
                id: f[0].into(),
                name: f[1].into(),
                version: f[2].into(),
                remote: f[3].into(),
                scope: f[4].into(),
                branch: f[5].into(),
                source: "Flatpak".into(),
                icon: f[0].into(),
                ..Default::default()
            });
        }
    } else {
        result.problems.push(format!("Cannot list installed Flatpaks: {}", flatpaks.stderr.trim()));
    }
    let native = sources.run("pacman", &["-Qqn"]);
    if native.ok() {
        result.native = native.stdout.lines().map(str::to_string).collect();
    }
    let foreign_out = sources.run("pacman", &["-Qqm"]);
    if !foreign_out.ok() && !foreign_out.stderr.trim().is_empty() {
        result.problems.push(format!("Cannot list foreign Arch packages: {}", foreign_out.stderr.trim()));
    }
    let foreign: HashSet<&str> = foreign_out.stdout.lines().map(str::trim).collect();
    // Which package owns each system launcher; files no package owns make pacman print an error, which is fine.
    let home_text = home.to_string_lossy();
    let system: Vec<&str> = launchers.iter().filter(|l| !l.flatpak && !l.path.starts_with(&*home_text)).map(|l| l.path.as_str()).collect();
    let mut owners: HashMap<String, (String, String)> = HashMap::new();
    if !system.is_empty() {
        let args: Vec<&str> = std::iter::once("-Qo").chain(system.iter().copied()).collect();
        for line in sources.run("pacman", &args).stdout.lines() {
            if let Some(c) = OWNED.captures(line.trim()) {
                owners.insert(c[1].into(), (c[2].into(), c[3].into()));
            }
        }
    }
    // Like the menu: one row per application. A package with several launchers gets one row under its own name.
    let mut by_package: BTreeMap<&str, Vec<&Launcher>> = BTreeMap::new();
    for launcher in launchers {
        if launcher.flatpak {
            for app in apps.iter_mut().filter(|a| a.source == "Flatpak" && a.id == launcher.id) {
                app.summary = launcher.comment.clone();
                app.launcher = launcher.path.clone();
                app.icon = icon_or(launcher, &app.id);
            }
            continue;
        }
        if launcher.id.starts_with("woof-store-") {
            continue; // Our own AppImage launchers; the imported list covers them
        }
        if let Some((package, _)) = owners.get(&launcher.path) {
            by_package.entry(package.as_str()).or_default().push(launcher);
            continue;
        }
        let appimage = launcher.exec.to_lowercase().contains(".appimage");
        apps.push(App {
            id: launcher.id.clone(),
            name: launcher.name.clone(),
            summary: launcher.comment.clone(),
            scope: if launcher.path.starts_with(&*home_text) { "user" } else { "system" }.into(),
            launcher: launcher.path.clone(),
            icon: icon_or(launcher, "application-x-executable"),
            source: if appimage { "AppImage" } else { "Other" }.into(),
            path: if appimage { launcher_program(&launcher.exec) } else { String::new() },
            ..Default::default()
        });
    }
    for (package, group) in by_package {
        let first = group[0];
        let single = group.len() == 1;
        apps.push(App {
            id: package.into(),
            version: owners[&first.path].1.clone(),
            scope: "system".into(),
            source: if foreign.contains(package) { "AUR / local" } else { "Arch" }.into(),
            name: if single { first.name.clone() } else { package.into() },
            summary: if single { first.comment.clone() } else { String::new() },
            icon: icon_or(first, "package-x-generic"),
            launcher: first.path.clone(),
            ..Default::default()
        });
    }
    apps.sort_by_cached_key(|a| a.name.to_lowercase());
    result.apps = apps;
    result
}

/// Reads the real system.
pub fn load(sources: &dyn Sources, language: &str) -> Installed {
    let dirs = launcher_dirs();
    let refs: Vec<&Path> = dirs.iter().map(PathBuf::as_path).collect();
    load_installed(sources, &scan_launchers(&refs, language), &crate::paths::data_home(), &crate::paths::home())
}

/// A summary and a website for an app installed outside the catalogue: the package
/// database for Arch and AUR packages, else DuckDuckGo's abstract when it is about this name. `None` when nothing is found.
pub fn about_installed(sources: &dyn Sources, id: &str, name: &str, source: &str) -> Option<(String, String)> {
    if source == "Arch" || source == "AUR / local" {
        let out = sources.run("pacman", &["-Qi", id]);
        let field =
            |key: &str| out.stdout.lines().find(|l| l.starts_with(key)).and_then(|l| l.split_once(':')).map_or(String::new(), |(_, v)| v.trim().to_string());
        let description = field("Description");
        if out.code == 0 && !description.is_empty() && description != "None" {
            return Some((description, field("URL")));
        }
    }
    let url = format!("https://api.duckduckgo.com/?q={}&format=json&no_html=1&skip_disambig=1", crate::aur_remote::encode(name));
    let answer: serde_json::Value = serde_json::from_slice(&sources.get(&url, 0).ok()?).ok()?;
    let (heading, text) = (answer["Heading"].as_str().unwrap_or(""), answer["AbstractText"].as_str().unwrap_or(""));
    let (heading_low, name_low) = (heading.to_lowercase(), name.to_lowercase());
    // Only an answer about this name
    if text.is_empty() || !(heading_low.contains(&name_low) || name_low.contains(&heading_low)) {
        return None;
    }
    let summary = if text.chars().count() > 260 {
        let cut: String = text.chars().take(260).collect();
        format!("{}…", cut.rsplit_once(' ').map_or(cut.as_str(), |(head, _)| head))
    } else {
        text.to_string()
    };
    Some((summary, answer["AbstractURL"].as_str().unwrap_or("").to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::command::Output;

    struct Fake;

    impl Sources for Fake {
        fn run(&self, program: &str, args: &[&str]) -> Output {
            let stdout = match (program, args.first().copied()) {
                ("flatpak", _) => "org.kde.krita\tKrita\t5.2\tflathub\tsystem\tstable\n",
                ("pacman", Some("-Qqn")) => "dolphin\nkcalc\n",
                ("pacman", Some("-Qqm")) => "yay\n",
                ("pacman", Some("-Qo")) => {
                    "/usr/share/applications/a.desktop is owned by kdegames 1-1\n/usr/share/applications/b.desktop is owned by kdegames 1-1\n/usr/share/applications/yay.desktop is owned by yay 12-1\n"
                }
                _ => "",
            };
            Output { code: 0, stdout: stdout.into(), stderr: String::new() }
        }
        fn get(&self, _: &str, _: u64) -> Result<Vec<u8>, String> {
            Err("offline".into())
        }
    }

    struct About(&'static str, &'static str);

    impl Sources for About {
        fn run(&self, _: &str, args: &[&str]) -> Output {
            assert_eq!(args, ["-Qi", "foo"]);
            Output { code: if self.0.is_empty() { 1 } else { 0 }, stdout: self.0.into(), stderr: String::new() }
        }
        fn get(&self, url: &str, _: u64) -> Result<Vec<u8>, String> {
            assert!(url.contains("q=Foo%20Bar&"), "{url}");
            Ok(self.1.as_bytes().to_vec())
        }
    }

    #[test]
    fn about_installed_asks_pacman_then_the_web() {
        let pacman = "Name            : foo\nDescription     : A foo tool\nURL             : https://foo.org\n";
        assert_eq!(about_installed(&About(pacman, ""), "foo", "Foo Bar", "Arch"), Some(("A foo tool".into(), "https://foo.org".into())));
        let web = r#"{"Heading":"Foo Bar","AbstractText":"Foo Bar is a game.","AbstractURL":"https://en.wikipedia.org/wiki/Foo"}"#;
        assert_eq!(about_installed(&About("", web), "foo", "Foo Bar", "Arch"), Some(("Foo Bar is a game.".into(), "https://en.wikipedia.org/wiki/Foo".into())));
        let other = r#"{"Heading":"Baz","AbstractText":"Baz is a city."}"#;
        assert_eq!(about_installed(&About("", other), "foo", "Foo Bar", "Other"), None);
        let long = format!(r#"{{"Heading":"Foo Bar","AbstractText":"{}"}}"#, "word ".repeat(80));
        let (summary, _) = about_installed(&About("", Box::leak(long.into_boxed_str())), "foo", "Foo Bar", "Other").unwrap();
        assert!(summary.ends_with("word…") && summary.chars().count() <= 261, "{summary}");
    }

    fn launcher(id: &str, path: &str, exec: &str, flatpak: bool) -> Launcher {
        Launcher { id: id.into(), name: id.to_uppercase(), comment: format!("{id} app"), icon: String::new(), exec: exec.into(), path: path.into(), flatpak }
    }

    #[test]
    fn one_row_per_application() {
        let data = tempfile::tempdir().unwrap();
        let launchers = [
            launcher("org.kde.krita", "/var/lib/flatpak/exports/share/applications/org.kde.krita.desktop", "krita", true),
            launcher("a", "/usr/share/applications/a.desktop", "a", false),
            launcher("b", "/usr/share/applications/b.desktop", "b", false),
            launcher("yay", "/usr/share/applications/yay.desktop", "yay", false),
            launcher("tool", "/home/me/.local/share/applications/tool.desktop", "/home/me/Tool.AppImage --x", false),
            launcher("woof-store-x", "/home/me/.local/share/applications/woof-store-x.desktop", "x", false),
        ];
        let installed = load_installed(&Fake, &launchers, data.path(), Path::new("/home/me"));
        let rows: Vec<(&str, &str)> = installed.apps.iter().map(|a| (a.id.as_str(), a.source.as_str())).collect();
        assert_eq!(rows, [("kdegames", "Arch"), ("org.kde.krita", "Flatpak"), ("tool", "AppImage"), ("yay", "AUR / local")]);
        let krita = &installed.apps[1];
        assert_eq!((krita.summary.as_str(), krita.icon.as_str()), ("org.kde.krita app", "org.kde.krita"));
        assert_eq!((installed.apps[0].name.as_str(), installed.apps[0].version.as_str()), ("kdegames", "1-1")); // Two launchers: the package name
        assert_eq!((installed.apps[2].path.as_str(), installed.apps[2].scope.as_str()), ("/home/me/Tool.AppImage", "user"));
        assert!(installed.native.contains("kcalc") && installed.problems.is_empty());
    }
}
