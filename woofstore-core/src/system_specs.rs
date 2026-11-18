//! What the Installed and Updates pages show and queue: update rows, the System Update summary, and the operations for
//! updates, removals, orphan packages and service restarts. Operations are JSON objects {"lane", "title", "program",
//! "args", "steps"} in the form the store's queue takes.

use crate::jobs::root_args;
use crate::launchers::version_diff;
use crate::leftovers::{find_leftovers, flatpak_data, format_size};
use crate::news::NewsItem;
use crate::packages::valid_package_name;
use crate::updates::{update_script, UpdateEntry, UpdatePlan};
use chrono::NaiveDate;
use regex::Regex;
use serde::Deserialize;
use serde_json::{json, Value};
use std::path::Path;
use std::sync::LazyLock;

/// A row of the Installed list as the store hands it to QML.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct InstalledApp {
    pub id: String,
    pub name: String,
    pub source: String,
    pub icon: String,
    pub version: String,
    pub launcher: String,
    pub path: String,
}

fn job(lane: &str, title: &str, program: &str, args: Vec<String>, steps: Vec<String>) -> Value {
    json!({ "lane": lane, "title": title, "program": program, "args": args, "steps": steps })
}

/// Rows of the Updates page. Apps: Flatpaks, AppImages, the store, and Arch packages that have a menu entry; everything
/// else is a system package. `action`: "update" (a button of its own), "system" (only System Update updates it, the
/// button is shown disabled) or "none".
pub fn update_rows(entries: &[UpdateEntry], installed: &[InstalledApp]) -> Vec<Value> {
    let known = |id: &str| installed.iter().find(|a| a.id == id && (a.source == "Arch" || a.source == "AUR / local"));
    let mut rows: Vec<Value> = entries
        .iter()
        .map(|entry| {
            let own = matches!(entry.source.as_str(), "Flatpak" | "AppImage" | "WoofStore");
            let package = if entry.source == "Flatpak" { None } else { known(&entry.id) };
            let installed_icon = installed.iter().find(|a| a.id == entry.id && !a.icon.is_empty()).map(|a| a.icon.clone());
            let icon = match entry.source.as_str() {
                "WoofStore" => "io.github.laskywolf.WoofStore".to_string(),
                "Flatpak" | "AppImage" => {
                    installed_icon.unwrap_or_else(|| if entry.source == "Flatpak" { entry.id.clone() } else { "application-x-executable".into() })
                }
                _ => package.map(|p| p.icon.clone()).filter(|i| !i.is_empty()).unwrap_or_else(|| "package-x-generic".into()),
            };
            let (from, to) = if entry.from.is_empty() { (String::new(), String::new()) } else { version_diff(&entry.from, &entry.to) };
            json!({
                "id": entry.id, "source": entry.source,
                "group": if own || package.is_some() { "Apps" } else { "System packages" },
                "name": package.map_or(entry.name.as_str(), |p| p.name.as_str()),
                "icon": icon,
                "version": if entry.from.is_empty() { format!("Version {}", entry.to) } else { format!("{from}&nbsp;&nbsp;→&nbsp;&nbsp;{to}") },
                "action": if own { "update" } else if package.is_some() { "system" } else { "none" },
            })
        })
        .collect();
    rows.sort_by_key(|row| row["group"] != "Apps"); // Stable: each group keeps the order of the check
    rows
}

/// Important news (manual interventions) published since the last System Update, or in the last 14 days when there was
/// none: at most three "• Source: Title" lines.
pub fn news_since(news: &[NewsItem], last_update: Option<NaiveDate>, today: NaiveDate) -> Vec<String> {
    let from = last_update.unwrap_or(today - chrono::Days::new(14));
    news.iter()
        .filter(|item| item.important && item.date.is_some_and(|d| d.date_naive() >= from))
        .take(3)
        .map(|item| format!("• {}: {}", item.source, item.title))
        .collect()
}

/// The text of the System Update confirmation.
pub fn system_update_text(entries: &[UpdateEntry], headlines: &[String]) -> String {
    let count = |source: &str| entries.iter().filter(|e| e.source == source).count();
    let plural = |n: usize, one: &str, many: &str| format!("{n} {}", if n == 1 { one } else { many });
    let (arch, aur, flatpaks, images) = (count("Arch"), count("AUR"), count("Flatpak"), count("AppImage"));
    let mut lines = vec![if arch > 0 {
        format!("• {}: a full system upgrade (pacman -Syu)", plural(arch, "repository package", "repository packages"))
    } else {
        "• The system: syncs the package databases and upgrades anything new (pacman -Syu)".into()
    }];
    if aur > 0 {
        lines.push(format!("• {}", plural(aur, "AUR package", "AUR packages")));
    }
    if flatpaks > 0 {
        lines.push(format!("• {}", plural(flatpaks, "Flatpak app", "Flatpak apps")));
    }
    if images > 0 {
        lines.push(format!("• {}", plural(images, "AppImage", "AppImages")));
    }
    if let Some(store) = entries.iter().find(|e| e.source == "WoofStore") {
        lines.push(format!("• WoofStore {} from GitHub, last: it is built and installed after everything else, then the store restarts", store.to));
    }
    lines.push("• Afterwards: old cached packages are removed (three versions of each stay), services running old files are offered for a restart, and orphan packages are listed, never removed without asking".into());
    let news = if headlines.is_empty() {
        String::new()
    } else {
        format!("\n\nNews since your last update:\n{}\nOpen the News page to read it before you continue.", headlines.join("\n"))
    };
    format!(
        "This updates:\n{}{news}\n\nThe system comes first. You will be asked for your password, and if the system upgrade fails nothing else is updated. The update keeps running if you close WoofStore.",
        lines.join("\n")
    )
}

/// The AppImage arguments of the update script: "id<TAB>url<TAB>name" each.
fn image_items(entries: &[&UpdateEntry]) -> Vec<String> {
    entries.iter().map(|e| format!("{}\t{}\t{}", e.id, e.url, e.name)).collect()
}

fn script_job(lane: &str, title: &str, plan: &UpdatePlan, exe: &str, images: &[&UpdateEntry]) -> Value {
    let (script, steps) = update_script(plan);
    let mut args = vec!["-c".to_string(), script, "woof-store-update".into(), exe.into()];
    args.extend(image_items(images));
    job(lane, title, "bash", args, steps)
}

/// One run for everything: the system first (a full pacman -Syu), then the AUR, Flatpak, AppImages and the store itself.
/// `aur`: include the AUR packages; `devel`: also rebuild development packages that have new commits. `exe`: the store's
/// program, which imports the new AppImages.
pub fn system_update_job(entries: &[UpdateEntry], aur: bool, devel: bool, exe: &str) -> Value {
    let images: Vec<&UpdateEntry> = entries.iter().filter(|e| e.source == "AppImage").collect();
    let plan = UpdatePlan {
        system: true,
        aur: (aur && entries.iter().any(|e| e.source == "AUR")) || devel,
        devel,
        flatpak: entries.iter().any(|e| e.source == "Flatpak"),
        app_images: !images.is_empty(),
        self_url: entries.iter().find(|e| e.source == "WoofStore").map(|e| e.url.clone()),
    };
    script_job("exclusive", "System Update", &plan, exe, &images)
}

/// The update of one app: a Flatpak, an AppImage or the store itself. `None` for packages, which only System Update updates.
pub fn update_one_job(entry: &UpdateEntry, exe: &str) -> Option<Value> {
    match entry.source.as_str() {
        "WoofStore" => Some(script_job("exclusive", "Update WoofStore", &UpdatePlan { self_url: Some(entry.url.clone()), ..Default::default() }, exe, &[])),
        "Flatpak" => Some(job(
            "flatpak",
            &format!("Update {}", entry.name),
            "flatpak",
            ["update", "-y", "--noninteractive", &entry.id].map(String::from).to_vec(),
            Vec::new(),
        )),
        "AppImage" => Some(script_job("appimage", &format!("Update {}", entry.name), &UpdatePlan { app_images: true, ..Default::default() }, exe, &[entry])),
        _ => None,
    }
}

/// A Flatpak needs a full application ID (org.kde.krita); a bare word would make flatpak ask which of many refs is meant.
fn flatpak_id(id: &str) -> bool {
    static ID: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$").unwrap());
    ID.is_match(id)
}

/// The question the removal dialog asks besides the leftovers, or `None` when the app has no package to remove.
pub fn removal_option(app: &InstalledApp) -> Option<&'static str> {
    match app.source.as_str() {
        "Flatpak" => Some("Also remove runtimes no other app uses"),
        "Arch" | "AUR / local" => Some("Also remove dependencies nothing else needs"),
        _ => None,
    }
}

/// Removes a Flatpak or an Arch package. `extra`: the dialog's option (unused runtimes, or dependencies nothing needs).
pub fn removal_job(app: &InstalledApp, extra: bool) -> Result<Value, String> {
    let title = format!("Remove {}", app.name);
    match app.source.as_str() {
        "Flatpak" if flatpak_id(&app.id) => {
            let script = if extra {
                "flatpak uninstall -y --noninteractive \"$1\" && flatpak uninstall -y --noninteractive --unused"
            } else {
                "flatpak uninstall -y --noninteractive \"$1\""
            };
            Ok(job("flatpak", &title, "bash", ["-c", script, "woof-store-remove", &app.id].map(String::from).to_vec(), Vec::new()))
        }
        "Arch" | "AUR / local" if valid_package_name(&app.id) => {
            Ok(job("pacman", &title, "pkexec", root_args(&["remove", if extra { "--unused" } else { "" }, &app.id]), Vec::new()))
        }
        _ => Err(format!("{} cannot be removed here", app.name)),
    }
}

/// Files the app left in the home folder, each {"path", "label", "checked"}; a Flatpak's own data folder comes first.
pub fn leftover_rows(home: &Path, app: &InstalledApp) -> Vec<Value> {
    let mut found = find_leftovers(home, &[&app.id, &app.name]);
    if app.source == "Flatpak" {
        found.splice(0..0, flatpak_data(home, &app.id));
    }
    let home_text = home.to_string_lossy();
    found
        .iter()
        .map(|item| {
            let path = item.path.to_string_lossy();
            let shown = path.strip_prefix(&*home_text).map_or_else(|| path.to_string(), |rest| format!("~{rest}"));
            json!({ "path": path, "label": format!("{} · {} · {}", item.kind, shown, format_size(item.bytes)), "checked": item.checked_by_default })
        })
        .collect()
}

/// Removes the chosen orphan packages and their settings files in /etc. `None` when no valid name is left.
pub fn orphan_removal_job(names: &[String]) -> Option<Value> {
    let names: Vec<String> = names.iter().filter(|n| valid_package_name(n)).cloned().collect();
    if names.is_empty() {
        return None;
    }
    let args = root_args(&["remove", "--unused"]).into_iter().chain(names).collect();
    Some(job("pacman", "Remove orphan packages", "pkexec", args, Vec::new()))
}

/// Services whose restart can end the session or the network: listed, but not selected.
pub fn risky_service(name: &str) -> bool {
    ["dbus-broker.service", "dbus.service", "systemd-logind.service", "polkit.service"].contains(&name) || name.starts_with("user@")
}

/// Restarts the chosen services. `None` when no valid unit name is left.
pub fn restart_services_job(services: &[String]) -> Option<Value> {
    static UNIT: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[A-Za-z0-9:_.@\-]+\.service$").unwrap());
    let services: Vec<String> = services.iter().filter(|s| UNIT.is_match(s) && !s.starts_with('-')).cloned().collect();
    if services.is_empty() {
        return None;
    }
    let args = root_args(&["restart"]).into_iter().chain(services).collect();
    Some(job("exclusive", "Restart services", "pkexec", args, Vec::new()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;
    use std::process::Command;

    fn entry(id: &str, source: &str, from: &str, to: &str, url: &str) -> UpdateEntry {
        UpdateEntry { id: id.into(), name: id.into(), from: from.into(), to: to.into(), source: source.into(), url: url.into() }
    }

    fn app(id: &str, name: &str, source: &str) -> InstalledApp {
        InstalledApp { id: id.into(), name: name.into(), source: source.into(), icon: format!("{id}-icon"), ..Default::default() }
    }

    fn strings(value: &Value) -> Vec<String> {
        value.as_array().unwrap().iter().map(|v| v.as_str().unwrap().to_string()).collect()
    }

    const SELF_URL: &str = "https://github.com/LaskyWolf/woofstore/archive/refs/tags/v9.9.9.tar.gz";

    fn sample() -> Vec<UpdateEntry> {
        vec![
            entry("linux", "Arch", "7.2.9-2", "7.2.11-1", ""),
            entry("dolphin", "Arch", "25.08.1-1", "25.08.2-1", ""),
            entry("org.kde.krita", "Flatpak", "", "5.3", ""),
            entry("paru", "AUR", "2.0-1", "2.1-1", ""),
            entry("abc", "AppImage", "", "newer", "https://x/Eden.AppImage"),
            entry("woofstore", "WoofStore", "1.3.0", "9.9.9", SELF_URL),
        ]
    }

    #[test]
    fn rows_put_apps_first_and_only_some_update_alone() {
        let installed = [app("dolphin", "Dolphin", "Arch"), app("org.kde.krita", "Krita", "Flatpak")];
        let rows = update_rows(&sample(), &installed);
        let shown: Vec<(&str, &str, &str)> =
            rows.iter().map(|r| (r["name"].as_str().unwrap(), r["group"].as_str().unwrap(), r["action"].as_str().unwrap())).collect();
        assert_eq!(
            shown,
            [
                ("Dolphin", "Apps", "system"),
                ("org.kde.krita", "Apps", "update"),
                ("abc", "Apps", "update"),
                ("woofstore", "Apps", "update"),
                ("linux", "System packages", "none"),
                ("paru", "System packages", "none"),
            ]
        );
        assert_eq!((rows[0]["icon"].as_str().unwrap(), rows[1]["icon"].as_str().unwrap()), ("dolphin-icon", "org.kde.krita-icon"));
        assert_eq!((rows[3]["icon"].as_str().unwrap(), rows[4]["icon"].as_str().unwrap()), ("io.github.laskywolf.WoofStore", "package-x-generic"));
        assert_eq!(rows[1]["version"], "Version 5.3");
        assert!(rows[4]["version"].as_str().unwrap().starts_with("7.2.<span style=\"color:#da4453"));
    }

    #[test]
    fn summary_counts_and_recent_important_news() {
        let text = system_update_text(&sample(), &[]);
        assert!(text.contains("• 2 repository packages: a full system upgrade") && text.contains("• 1 AUR package\n") && text.contains("• 1 Flatpak app\n"));
        assert!(text.contains("• WoofStore 9.9.9 from GitHub") && !text.contains("News since"));
        assert!(system_update_text(&[], &[]).contains("• The system: syncs the package databases"));
        let day = |d: u32| NaiveDate::from_ymd_opt(2026, 11, d).unwrap();
        let item = |title: &str, d: u32, important: bool| NewsItem {
            source: "Arch Linux".into(),
            title: title.into(),
            important,
            date: Some(day(d).and_hms_opt(12, 0, 0).unwrap().and_utc().fixed_offset()),
            ..Default::default()
        };
        let news = [item("new", 10, true), item("minor", 9, false), item("old", 4, true)];
        assert_eq!(news_since(&news, Some(day(5)), day(17)), ["• Arch Linux: new"]);
        assert_eq!(news_since(&news, None, day(17)).len(), 2); // Two weeks back without a last update
        assert!(
            system_update_text(&[], &news_since(&news, Some(day(5)), day(17))).contains("News since your last update:\n• Arch Linux: new\nOpen the News page")
        );
    }

    #[test]
    fn system_update_runs_every_part_in_one_script() {
        let job = system_update_job(&sample(), false, false, "/usr/bin/woofstore");
        assert_eq!((job["lane"].as_str().unwrap(), job["title"].as_str().unwrap(), job["program"].as_str().unwrap()), ("exclusive", "System Update", "bash"));
        assert_eq!(strings(&job["steps"]), ["System packages", "Cache and services", "Flatpak apps", "AppImages", "Orphan packages", "WoofStore"]);
        let args = strings(&job["args"]);
        assert_eq!(&args[2..], ["woof-store-update", "/usr/bin/woofstore", "abc\thttps://x/Eden.AppImage\tabc"]);
        // Every step's name is printed, so the progress can follow it; the root helper prints "Cache and services"
        for step in strings(&job["steps"]).into_iter().filter(|s| s != "Cache and services") {
            assert!(args[1].contains(&format!("==> {step}")), "{step}");
        }
        assert!(strings(&system_update_job(&sample(), true, false, "w")["steps"]).contains(&"AUR packages".to_string()));
        let devel_only = system_update_job(&[], false, true, "w");
        assert_eq!(strings(&devel_only["steps"]), ["System packages", "Cache and services", "AUR packages", "Orphan packages"]);
        assert!(strings(&devel_only["args"])[1].contains("--devel"));
    }

    #[test]
    fn single_updates() {
        let entries = sample();
        let flatpak = update_one_job(&entries[2], "w").unwrap();
        assert_eq!((flatpak["lane"].as_str().unwrap(), flatpak["program"].as_str().unwrap()), ("flatpak", "flatpak"));
        assert_eq!(strings(&flatpak["args"]), ["update", "-y", "--noninteractive", "org.kde.krita"]);
        let image = update_one_job(&entries[4], "w").unwrap();
        assert_eq!((image["lane"].as_str().unwrap(), strings(&image["steps"])), ("appimage", vec!["AppImages".to_string()]));
        let store = update_one_job(&entries[5], "w").unwrap();
        assert_eq!((store["title"].as_str().unwrap(), store["lane"].as_str().unwrap()), ("Update WoofStore", "exclusive"));
        assert!(update_one_job(&entries[0], "w").is_none());
    }

    /// Runs a job with stand-ins for pkexec, pacman, flatpak and systemctl that record their arguments.
    fn run_with_stubs(job: &Value) -> String {
        let bin = tempfile::tempdir().unwrap();
        for (name, body) in [
            ("pkexec", "p=$1; shift; echo \"pkexec ${p##*/} $*\" >> \"$LOG\"; exec \"$p\" \"$@\""),
            ("pacman", "echo \"pacman $*\" >> \"$LOG\""),
            ("flatpak", "echo \"flatpak $*\" >> \"$LOG\""),
            ("systemctl", "echo \"systemctl $*\" >> \"$LOG\""),
        ] {
            let path = bin.path().join(name);
            std::fs::write(&path, format!("#!/bin/sh\n{body}\n")).unwrap();
            std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
        }
        let log = bin.path().join("log");
        let status = Command::new(job["program"].as_str().unwrap())
            .args(strings(&job["args"]))
            .env("PATH", format!("{}:{}", bin.path().display(), std::env::var("PATH").unwrap_or_default()))
            .env("LOG", &log)
            .status()
            .unwrap();
        assert!(status.success());
        std::fs::read_to_string(log).unwrap_or_default()
    }

    #[test]
    fn removals() {
        let krita = app("org.kde.krita", "Krita", "Flatpak");
        assert_eq!(
            run_with_stubs(&removal_job(&krita, true).unwrap()),
            "flatpak uninstall -y --noninteractive org.kde.krita\nflatpak uninstall -y --noninteractive --unused\n"
        );
        assert_eq!(run_with_stubs(&removal_job(&krita, false).unwrap()), "flatpak uninstall -y --noninteractive org.kde.krita\n");
        let yay = removal_job(&app("yay", "Yay", "AUR / local"), false).unwrap();
        assert_eq!((yay["lane"].as_str().unwrap(), strings(&yay["args"])[1..].to_vec()), ("pacman", vec!["remove".to_string(), "yay".into()]));
        assert_eq!(strings(&removal_job(&app("yay", "Yay", "Arch"), true).unwrap()["args"])[2], "--unused");
        assert!(removal_job(&app("krita", "Krita", "Flatpak"), false).is_err()); // Not a full application ID
        assert!(removal_job(&app("--all", "x", "Arch"), false).is_err() && removal_job(&app("x", "x", "Other"), false).is_err());
        assert_eq!(removal_option(&krita), Some("Also remove runtimes no other app uses"));
        assert_eq!(removal_option(&app("t", "T", "AppImage")), None);
    }

    #[test]
    fn leftovers_flatpak_data_first() {
        let home = tempfile::tempdir().unwrap();
        for dir in [".var/app/org.kde.krita", ".cache/krita", ".config/krita"] {
            std::fs::create_dir_all(home.path().join(dir)).unwrap();
        }
        let rows = leftover_rows(home.path(), &app("org.kde.krita", "Krita", "Flatpak"));
        let labels: Vec<&str> = rows.iter().map(|r| r["label"].as_str().unwrap()).collect();
        assert_eq!(labels, ["Data · ~/.var/app/org.kde.krita · 0 bytes", "Settings · ~/.config/krita · 0 bytes", "Cache · ~/.cache/krita · 0 bytes"]);
        assert_eq!(rows.iter().map(|r| r["checked"].as_bool().unwrap()).collect::<Vec<_>>(), [false, false, true]);
    }

    #[test]
    fn orphans_and_services() {
        let job = orphan_removal_job(&["libfoo".into(), "$(rm -rf ~)".into(), "bar".into()]).unwrap();
        assert_eq!(strings(&job["args"])[1..], ["remove", "--unused", "libfoo", "bar"]);
        assert_eq!(run_with_stubs(&job), "pkexec woofstore-root remove --unused libfoo bar\npacman -Rns --noconfirm -- libfoo bar\n");
        assert!(orphan_removal_job(&["--x".into()]).is_none());
        let restart = restart_services_job(&["bluetooth.service".into(), "x;rm.service".into(), "--now.service".into()]).unwrap();
        assert_eq!(restart["title"].as_str().unwrap(), "Restart services");
        assert_eq!(run_with_stubs(&restart), "pkexec woofstore-root restart bluetooth.service\nsystemctl restart -- bluetooth.service\n");
        assert!(restart_services_job(&[]).is_none());
        assert!(risky_service("dbus-broker.service") && risky_service("user@1000.service") && !risky_service("bluetooth.service"));
    }
}
