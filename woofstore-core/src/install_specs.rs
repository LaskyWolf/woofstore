//! Install and remove operations as the store queues them, and the facts the app page shows about an app: the source
//! chooser, the description without packaging disclaimers, and Flatpak permissions.

use crate::catalog::{CatalogApp, Package, Source};
use crate::jobs::{root_args, JobSpec};
use crate::news::plain_description;
use crate::packages::valid_package_name;

/// The source chooser's text: "Flatpak · flathub", "Repo · extra", "AUR · name". The chosen source comes back as this text.
pub fn package_label(package: &Package) -> String {
    match package.source {
        Source::Flatpak => format!("Flatpak · {}", package.repo),
        Source::Aur => format!("AUR · {}", package.name),
        Source::Repo => format!("Repo · {}", package.repo),
    }
}

/// A Flatpak needs a full application ID (org.kde.krita); a bare word would make flatpak ask which of many refs is meant.
fn flatpak_app_id(name: &str) -> bool {
    let parts: Vec<&str> = name.split('.').collect();
    parts.len() > 1 && parts.iter().all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-'))
}

fn usable(package: &Package) -> bool {
    match package.source {
        Source::Flatpak => flatpak_app_id(&package.name),
        Source::Repo | Source::Aur => valid_package_name(&package.name),
    }
}

/// The package an operation uses: the one whose label was chosen, else the first of Repo, Flatpak, AUR.
pub fn choose_package<'a>(packages: &'a [Package], label: &str) -> Option<&'a Package> {
    packages
        .iter()
        .find(|p| usable(p) && package_label(p) == label)
        .or_else(|| [Source::Repo, Source::Flatpak, Source::Aur].iter().find_map(|&s| packages.iter().find(|p| p.source == s && usable(p))))
}

/// Lane and job that install (or reinstall) `package`. AUR packages are built after a review of their build files, so they
/// have no job here. `system_remotes`: the Flatpak remotes of the system installation; an origin that is not one of them (some
/// catalogues name the origin "flatpak") installs from flathub.
pub fn install_job(name: &str, package: &Package, again: bool, system_remotes: &[String]) -> Result<(&'static str, JobSpec), String> {
    if !usable(package) {
        return Err(format!("{name} has no package that can be installed here."));
    }
    let title = format!("{} {name}", if again { "Reinstall" } else { "Install" });
    let args = |list: &[&str]| list.iter().filter(|a| !a.is_empty()).map(|a| a.to_string()).collect();
    match package.source {
        Source::Flatpak => {
            let remote = if system_remotes.contains(&package.repo) { package.repo.as_str() } else { "flathub" };
            let reinstall = if again { "--reinstall" } else { "" };
            let args = args(&["install", "--system", "-y", "--noninteractive", reinstall, remote, &package.name]);
            Ok(("flatpak", JobSpec { title, program: "flatpak".into(), args, ..Default::default() }))
        }
        Source::Repo => {
            let reinstall = if again { "--reinstall" } else { "" };
            Ok(("pacman", JobSpec { title, program: "pkexec".into(), args: root_args(&["install", reinstall, &package.name]), ..Default::default() }))
        }
        Source::Aur => Err(format!("{name} is built from the AUR after a review of its build files.")),
    }
}

/// Lane and job that remove `package`. `remove_unused`: also the Flatpak runtimes, or the dependencies, nothing else needs.
pub fn remove_job(name: &str, package: &Package, remove_unused: bool) -> Result<(&'static str, JobSpec), String> {
    if !usable(package) {
        return Err(format!("{name} has no package that can be removed here."));
    }
    let title = format!("Remove {name}");
    if package.source == Source::Flatpak {
        let script = if remove_unused {
            "flatpak uninstall -y --noninteractive \"$1\" && flatpak uninstall -y --noninteractive --unused"
        } else {
            "flatpak uninstall -y --noninteractive \"$1\""
        };
        let args = ["-c", script, "woof-store-remove", &package.name].map(String::from).to_vec();
        return Ok(("flatpak", JobSpec { title, program: "bash".into(), args, ..Default::default() }));
    }
    let args = root_args(&["remove", if remove_unused { "--unused" } else { "" }, &package.name]);
    Ok(("pacman", JobSpec { title, program: "pkexec".into(), args, ..Default::default() }))
}

/// AppStream markup as plain paragraphs, without Flathub's packaging disclaimers. The summary when nothing is left.
pub fn readable_description(app: &CatalogApp) -> String {
    let text = plain_description(&app.description);
    let paragraphs: Vec<&str> = text
        .split('\n')
        .map(str::trim)
        .filter(|p| !p.is_empty())
        .filter(|p| {
            let lower = p.to_lowercase();
            let disclaimer = lower.contains("flatpak")
                && ["community maintained", "unofficial", "not officially", "not affiliated", "not verified"].iter().any(|w| lower.contains(w));
            !disclaimer
        })
        .collect();
    if paragraphs.is_empty() {
        app.summary.clone()
    } else {
        paragraphs.join("\n\n")
    }
}

/// One permission as the app page shows it: theme icon, name, what it allows.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Permission {
    pub icon: &'static str,
    pub name: &'static str,
    pub value: String,
}

/// The permissions in the `[Context]` group of `flatpak info -M` output.
pub fn flatpak_permissions(manifest: &str) -> Vec<Permission> {
    let mut context: Vec<(&str, Vec<&str>)> = Vec::new();
    let mut in_context = false;
    for line in manifest.lines() {
        if line.starts_with('[') {
            in_context = line.trim() == "[Context]";
            continue;
        }
        if let Some((key, value)) = line.split_once('=').filter(|(key, _)| in_context && !key.is_empty()) {
            context.push((key.trim(), value.split(';').filter(|v| !v.is_empty()).collect()));
        }
    }
    let get = |key: &str| context.iter().rev().find(|(k, _)| *k == key).map(|(_, v)| v.clone()).unwrap_or_default();
    let (shared, sockets, devices, files) = (get("shared"), get("sockets"), get("devices"), get("filesystems"));
    let mut rows = Vec::new();
    let mut add = |icon, name, value: String| rows.push(Permission { icon, name, value });
    if shared.contains(&"network") {
        add("network-wired", "Network", "Can use the internet".into());
    }
    if files.iter().any(|f| ["host", "host-os", "home"].contains(f)) {
        let home_only = files.contains(&"home") && !files.contains(&"host");
        add("folder-red", "Files", if home_only { "Can read and change your home folder" } else { "Can read and change all your files" }.into());
    } else if !files.is_empty() {
        add("folder", "Files", format!("Can access: {}", files.join(", ")));
    }
    if devices.contains(&"all") {
        add("preferences-desktop-peripherals", "Devices", "Can use all devices, such as webcams and game controllers".into());
    } else if !devices.is_empty() {
        add("preferences-desktop-peripherals", "Devices", format!("Can use: {}", devices.join(", ")));
    }
    if sockets.contains(&"x11") || sockets.contains(&"fallback-x11") {
        add("preferences-system-windows", "Legacy display system", "Uses X11, which lets apps see other windows".into());
    }
    if sockets.contains(&"pulseaudio") {
        add("audio-volume-high", "Sound", "Can play and record audio".into());
    }
    rows
}

#[cfg(test)]
mod tests {
    use super::*;

    fn packages() -> Vec<Package> {
        vec![
            Package::new(Source::Aur, "krita-git", "aur", "1"),
            Package::new(Source::Flatpak, "org.kde.krita", "flathub", "5.2"),
            Package::new(Source::Repo, "krita", "extra", "5.2-1"),
        ]
    }

    #[test]
    fn chooses_the_label_then_repo_flatpak_aur() {
        let list = packages();
        assert_eq!(choose_package(&list, "").unwrap().name, "krita");
        assert_eq!(choose_package(&list, "Flatpak · flathub").unwrap().name, "org.kde.krita");
        assert_eq!(choose_package(&list, "AUR · krita-git").unwrap().name, "krita-git");
        // A bare word is no Flatpak app ID: never chosen
        let bare = vec![Package::new(Source::Flatpak, "krita", "flathub", ""), Package::new(Source::Aur, "krita-bin", "aur", "")];
        assert_eq!(choose_package(&bare, "Flatpak · flathub").unwrap().name, "krita-bin");
        assert!(choose_package(&bare[..1], "").is_none());
    }

    #[test]
    fn install_jobs() {
        let list = packages();
        let remotes = vec!["flathub".to_string()];
        let (lane, job) = install_job("Krita", &list[2], false, &remotes).unwrap();
        assert_eq!((lane, job.title.as_str(), job.program.to_str().unwrap()), ("pacman", "Install Krita", "pkexec"));
        assert_eq!(job.args[1..], ["install", "krita"]);
        assert_eq!(install_job("Krita", &list[2], true, &remotes).unwrap().1.args[1..], ["install", "--reinstall", "krita"]);
        let (lane, job) = install_job("Krita", &list[1], false, &remotes).unwrap();
        assert_eq!((lane, job.program.to_str().unwrap()), ("flatpak", "flatpak"));
        assert_eq!(job.args, ["install", "--system", "-y", "--noninteractive", "flathub", "org.kde.krita"]);
        let odd_origin = Package::new(Source::Flatpak, "org.kde.krita", "flatpak", "");
        let job = install_job("Krita", &odd_origin, true, &remotes).unwrap().1;
        assert_eq!((job.title.as_str(), job.args[4].as_str(), job.args[5].as_str()), ("Reinstall Krita", "--reinstall", "flathub"));
        assert!(install_job("Krita", &list[0], false, &remotes).is_err());
        assert!(install_job("X", &Package::new(Source::Repo, "-rf", "extra", ""), false, &remotes).is_err());
    }

    #[test]
    fn remove_jobs() {
        let list = packages();
        let (lane, job) = remove_job("Krita", &list[2], true).unwrap();
        assert_eq!((lane, job.title.as_str()), ("pacman", "Remove Krita"));
        assert_eq!(job.args[1..], ["remove", "--unused", "krita"]);
        assert_eq!(remove_job("Krita", &list[0], false).unwrap().1.args[1..], ["remove", "krita-git"]);
        let (lane, job) = remove_job("Krita", &list[1], true).unwrap();
        assert_eq!((lane, job.program.to_str().unwrap(), job.args[3].as_str()), ("flatpak", "bash", "org.kde.krita"));
        assert!(job.args[1].ends_with("--unused"));
        assert!(!remove_job("Krita", &list[1], false).unwrap().1.args[1].contains("--unused"));
    }

    #[test]
    fn description_drops_flatpak_disclaimers() {
        let app =
            CatalogApp { summary: "Paint".into(), description: "<p>Draw things.</p><p>This Flatpak is community maintained.</p>".into(), ..Default::default() };
        assert_eq!(readable_description(&app), "Draw things.");
        let only = CatalogApp { description: "<p>Unofficial Flatpak build</p>".into(), ..app };
        assert_eq!(readable_description(&only), "Paint");
    }

    #[test]
    fn permissions_from_the_manifest() {
        let manifest = "[Application]\nname=org.kde.krita\n\n[Context]\nshared=network;ipc;\nsockets=x11;pulseaudio;\ndevices=dri;\nfilesystems=home;\n";
        let rows = flatpak_permissions(manifest);
        let names: Vec<&str> = rows.iter().map(|r| r.name).collect();
        assert_eq!(names, ["Network", "Files", "Devices", "Legacy display system", "Sound"]);
        assert_eq!(rows[1].value, "Can read and change your home folder");
        assert_eq!(rows[2].value, "Can use: dri");
        let files = flatpak_permissions("[Context]\nfilesystems=xdg-download;xdg-music:ro;\n");
        assert_eq!(files[0].value, "Can access: xdg-download, xdg-music:ro");
        assert!(flatpak_permissions("[Application]\nshared=network;\n").is_empty());
    }
}
