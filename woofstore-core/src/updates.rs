//! Pending updates, the store's own new releases, and the bash script of an update run.

use regex::Regex;
use std::collections::{HashMap, HashSet};
use std::sync::LazyLock;

/// `source`: Flatpak, Arch, AUR, AppImage or WoofStore. `url`: where an AppImage update or a new store release downloads from.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct UpdateEntry {
    pub id: String,
    pub name: String,
    pub from: String,
    pub to: String,
    pub source: String,
    pub url: String,
}

/// `checkupdates` and `paru -Qua` lines: "name old -> new".
pub fn parse_arch_updates(text: &str, source: &str) -> Vec<UpdateEntry> {
    static LINE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(\S+)\s+(\S+)\s+->\s+(\S+)$").unwrap());
    text.lines()
        .filter_map(|row| LINE.captures(row.trim()))
        .map(|c| UpdateEntry { id: c[1].into(), name: c[1].into(), from: c[2].into(), to: c[3].into(), source: source.into(), url: String::new() })
        .collect()
}

/// `flatpak remote-ls --updates --columns=application,name,version,branch` lines, each application once.
pub fn parse_flatpak_updates(text: &str, installed_versions: &HashMap<String, String>) -> Vec<UpdateEntry> {
    let mut seen = HashSet::new();
    text.lines()
        .filter_map(|row| {
            let f: Vec<&str> = row.split('\t').collect();
            if f.len() < 3 || f[0].is_empty() || !seen.insert(f[0]) {
                return None;
            }
            // Runtimes (GNOME Platform, KDE Platform…) have no version of their own: the branch tells which one.
            let to = if f[2].is_empty() { f.get(3).copied().unwrap_or("") } else { f[2] };
            Some(UpdateEntry {
                id: f[0].into(),
                name: if f[1].is_empty() { f[0] } else { f[1] }.into(),
                from: installed_versions.get(f[0]).cloned().unwrap_or_default(),
                to: if to.is_empty() { "newer" } else { to }.into(),
                source: "Flatpak".into(),
                url: String::new(),
            })
        })
        .collect()
}

/// Numeric dotted versions compared part by part; a missing part counts as 0.
fn version_newer(candidate: &str, current: &str) -> bool {
    let parts = |v: &str| v.split('.').map(|p| p.parse::<u64>().unwrap_or(0)).collect::<Vec<_>>();
    let (a, b) = (parts(candidate), parts(current));
    for i in 0..a.len().max(b.len()) {
        let (x, y) = (a.get(i).copied().unwrap_or(0), b.get(i).copied().unwrap_or(0));
        if x != y {
            return x > y;
        }
    }
    false
}

pub const RELEASE_ARCHIVE_PREFIX: &str = "https://github.com/LaskyWolf/woofstore/archive/refs/tags/";
/// The package .github/workflows/release.yml builds and attaches to each release, with RELEASE_PACKAGE.sha256 beside it
const RELEASE_DOWNLOAD_PREFIX: &str = "https://github.com/LaskyWolf/woofstore/releases/download/";
const RELEASE_PACKAGE: &str = "woofstore-x86_64.pkg.tar.zst";

/// GitHub's latest-release answer against the running version: the update entry (url = source archive), or `None`
/// when it is not newer.
pub fn parse_self_update(release_json: &str, current: &str) -> Option<UpdateEntry> {
    static TAG: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^v[0-9]+(\.[0-9]+)*$").unwrap());
    let release: serde_json::Value = serde_json::from_str(release_json).ok()?;
    let tag = release["tag_name"].as_str().filter(|tag| TAG.is_match(tag))?;
    let version = &tag[1..];
    version_newer(version, current).then(|| UpdateEntry {
        id: "woofstore".into(),
        name: "WoofStore".into(),
        from: current.into(),
        to: version.into(),
        source: "WoofStore".into(),
        url: format!("{RELEASE_ARCHIVE_PREFIX}{tag}.tar.gz"),
    })
}

/// Services listed by the update's last step as "needs-restart: name.service" (they run files the upgrade replaced).
pub fn parse_services_to_restart(log: &str) -> Vec<String> {
    static LINE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^needs-restart: ([A-Za-z0-9:_.@\-]+\.service)$").unwrap());
    let mut services: Vec<String> = Vec::new();
    for row in log.lines() {
        if let Some(c) = LINE.captures(row.trim()) {
            if !services.iter().any(|s| s == &c[1]) {
                services.push(c[1].into());
            }
        }
    }
    services
}

/// What one update run includes. `self_url`: the source archive of a newer WoofStore release, installed as the very last
/// step, so a failure there loses nothing else. The package built for that release is used when its checksum matches;
/// otherwise the archive is built here. Only the project's own release archives are accepted.
#[derive(Debug, Clone, Default)]
pub struct UpdatePlan {
    pub system: bool,
    pub aur: bool,
    pub devel: bool,
    pub flatpak: bool,
    pub app_images: bool,
    pub self_url: Option<String>,
}

/// The bash script of an update run and the names of its steps. `$1` is the store's program (it imports the AppImages),
/// the other arguments are "id<TAB>url<TAB>name" per AppImage. Each step prints "==> <name>" first. A failed system
/// upgrade stops everything; a failed AUR, Flatpak, AppImage or WoofStore step is reported and the script goes on,
/// ending with status 1.
pub fn update_script(plan: &UpdatePlan) -> (String, Vec<String>) {
    static ARCHIVE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^https://github\.com/LaskyWolf/woofstore/archive/refs/tags/v[0-9.]+\.tar\.gz$").unwrap());
    let mut names = Vec::new();
    let mut script = String::from("fail=0\nexe=\"$1\"; shift\n");
    let mut section = |name: &str, script: &mut String| {
        names.push(name.to_string());
        script.push_str(&format!("echo '==> {name}'\n"));
    };
    if plan.system {
        // One root session for the upgrade and what follows it, so the password is asked once. The helper prints the
        // "Cache and services" step itself; if it does not get through the upgrade the whole run stops.
        section("System packages", &mut script);
        section("Cache and services", &mut String::new());
        script.push_str("pkexec \"$WOOFSTORE_ROOT\" upgrade || { echo '==> The system upgrade failed, so nothing else was updated.'; exit 1; }\n");
    }
    if plan.aur {
        section("AUR packages", &mut script);
        let devel = if plan.devel { " --devel" } else { "" };
        script.push_str(&format!(
            "if command -v paru >/dev/null; then paru -Sua --noconfirm --skipreview{devel} || {{ echo '==> The AUR update failed.'; fail=1; }}; else echo 'paru is not installed.'; fail=1; fi\n"
        ));
    }
    if plan.flatpak {
        section("Flatpak apps", &mut script);
        script.push_str("flatpak update -y --noninteractive || { echo '==> The Flatpak update failed.'; fail=1; }\n");
    }
    if plan.app_images {
        section("AppImages", &mut script);
        // Each AppImage downloads under its old name, is imported as the new copy, and the old copy goes away.
        script.push_str(
            "tmp=$(mktemp -d); trap 'rm -rf \"$tmp\"' EXIT\n\
             for item in \"$@\"; do\n\
             \x20 IFS=$'\\t' read -r id url name <<<\"$item\"\n\
             \x20 echo \"Downloading $name\"; mkdir -p \"$tmp/$id\"; file=\"$tmp/$id/${name//\\//_}.AppImage\"\n\
             \x20 if curl -fsSL -o \"$file\" \"$url\" && \"$exe\" --import-appimage \"$file\" --replaces \"$id\"; then echo \"Updated $name\"; else echo \"Could not update $name\"; fail=1; fi\n\
             done\n",
        );
    }
    if plan.system {
        section("Orphan packages", &mut script); // Only reported: removing them is the user's call
        script.push_str("orphans=$(pacman -Qdtq 2>/dev/null); if [ -n \"$orphans\" ]; then echo 'Orphan packages (nothing needs them any more):'; echo \"$orphans\"; echo 'Remove them with: sudo pacman -Rns $(pacman -Qdtq)'; else echo 'No orphan packages.'; fi\n");
    }
    // The address goes into the script, so only the project's own release archives are accepted
    if let Some(url) = plan.self_url.as_deref().filter(|url| ARCHIVE.is_match(url)) {
        section("WoofStore", &mut script);
        let tag = url.rsplit('/').next().unwrap_or_default().trim_end_matches(".tar.gz");
        script.push_str(&format!(
            "dir=$(mktemp -d); pkg=\"$dir/{RELEASE_PACKAGE}\"; built='{RELEASE_DOWNLOAD_PREFIX}{tag}/{RELEASE_PACKAGE}'\n\
             if curl -fsSL -o \"$pkg\" \"$built\" && curl -fsSL -o \"$pkg.sha256\" \"$built.sha256\" && (cd \"$dir\" && sha256sum -c --status {RELEASE_PACKAGE}.sha256); then\n\
             \x20 echo 'Downloaded the package built for this release.'; files=(\"$pkg\")\n\
             else\n\
             \x20 echo 'No package built for this release; building it here.'; rm -f \"$pkg\"\n\
             \x20 curl -fsSL '{url}' | tar -xz -C \"$dir\" --strip-components=1 && (cd \"$dir\" && makepkg -sf --noconfirm) && files=(\"$dir\"/woofstore-[0-9]*.pkg.tar.*) || files=()\n\
             fi\n\
             if [ ${{#files[@]}} -gt 0 ] && pkexec \"$WOOFSTORE_ROOT\" install-files \"${{files[0]}}\"; then echo 'WoofStore updated.'; else echo '==> The WoofStore update failed.'; fail=1; fi\n\
             rm -rf \"$dir\"\n"
        ));
    }
    script.push_str("exit $fail\n");
    (script, names)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::os::unix::fs::PermissionsExt;
    use std::path::Path;
    use std::process::Command;

    #[test]
    fn arch_and_flatpak_updates() {
        let updates = parse_arch_updates("systemd  262-1 -> 262.1-1\nnoise line\nlinux  7.2.9-2  ->  7.2.11-1\n", "Arch");
        assert_eq!(updates.len(), 2);
        assert_eq!((updates[0].name.as_str(), updates[1].to.as_str(), updates[1].source.as_str()), ("systemd", "7.2.11-1", "Arch"));
        let installed = HashMap::from([("org.a.B".to_string(), "1.0".to_string())]);
        let flatpaks = parse_flatpak_updates("org.a.B\tB\t2.0\t\norg.a.B\tB\t2.0\t\norg.c.D\tD\t1.1\t\n", &installed);
        assert_eq!(flatpaks.len(), 2);
        assert_eq!((flatpaks[0].from.as_str(), flatpaks[1].from.as_str()), ("1.0", ""));
    }

    #[test]
    fn self_update_from_github() {
        let release = |tag: &str| format!(r#"{{"tag_name":"{tag}"}}"#);
        let newer = parse_self_update(&release("v1.2.0"), "1.1.0").unwrap();
        assert_eq!((newer.to.as_str(), newer.from.as_str(), newer.source.as_str()), ("1.2.0", "1.1.0", "WoofStore"));
        assert_eq!(newer.url, "https://github.com/LaskyWolf/woofstore/archive/refs/tags/v1.2.0.tar.gz");
        assert!(parse_self_update(&release("v1.1.0"), "1.1.0").is_none() && parse_self_update(&release("v1.0.9"), "1.1.0").is_none());
        assert!(parse_self_update(&release("v1.10.0"), "1.9.0").is_some());
        assert!(
            parse_self_update(&release("v1.2.0; rm"), "1.1.0").is_none()
                && parse_self_update("{}", "1.1.0").is_none()
                && parse_self_update("not json", "1.1.0").is_none()
        );
    }

    #[test]
    fn services_are_parsed_once_and_only_when_safe() {
        let log = "x\nneeds-restart: a.service\n  needs-restart: b@1.service  \nneeds-restart: a.service\nneeds-restart: c;rm -rf x.service\nneeds-restart: ../d.service\nneeds-restart: e\n";
        assert_eq!(parse_services_to_restart(log), ["a.service", "b@1.service"]);
    }

    /// Stand-ins for pkexec, pacman, paru, flatpak, curl and the store, each writing its arguments to $LOG.
    struct Stubs {
        bin: tempfile::TempDir,
    }

    impl Stubs {
        fn new() -> Self {
            let stubs = Self { bin: tempfile::tempdir().unwrap() };
            stubs.stub("pkexec", "p=$1; shift; echo \"pkexec ${p##*/} $1\" >> \"$LOG\"; exec \"$p\" \"$@\"\n"); // Runs the real helper as the test user
            stubs.stub("pacman", "echo \"pacman $*\" >> \"$LOG\"; [ \"$1\" = -Syu ] && exit ${FAIL_PACMAN:-0}; exit 0\n");
            stubs.stub("paccache", "echo \"paccache $*\" >> \"$LOG\"; echo '==> finished: 2 packages removed'\n");
            stubs.stub("checkservices", "echo \"checkservices $*\" >> \"$LOG\"; echo \"Service 'dbus-broker.service' needs a restart\"; echo \"Service 'bluetooth.service' needs a restart\"; echo \"Service 'bluetooth.service' again\"\n");
            stubs.stub("paru", "echo \"paru $*\" >> \"$LOG\"; exit ${FAIL_PARU:-0}\n");
            stubs.stub("flatpak", "echo \"flatpak $*\" >> \"$LOG\"\n");
            stubs.stub("curl", "echo \"curl $*\" >> \"$LOG\"; for last; do :; done; : > \"$3\"\n");
            stubs.stub("woof", "echo \"woof $*\" >> \"$LOG\"\n");
            stubs
        }

        fn stub(&self, name: &str, body: &str) {
            let path = self.bin.path().join(name);
            fs::write(&path, format!("#!/bin/sh\n{body}")).unwrap();
            fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).unwrap();
        }

        /// Exit code, stdout, and the calls the stand-ins recorded.
        fn run(&self, script: &str, args: &[&str], env: &[(&str, &str)]) -> (i32, String, String) {
            let log = self.bin.path().join("log");
            let _ = fs::remove_file(&log);
            let path = format!("{}:{}", self.bin.path().display(), std::env::var("PATH").unwrap_or_default());
            let woof = self.bin.path().join("woof");
            let output = Command::new("bash")
                .args(["-c", script, "x"])
                .arg(&woof)
                .args(args)
                .env("PATH", path)
                .env("LOG", &log)
                .env("WOOFSTORE_ROOT", crate::jobs::root_helper())
                .envs(env.iter().copied())
                .current_dir(self.bin.path())
                .output()
                .unwrap();
            let calls = fs::read_to_string(Path::new(&log)).unwrap_or_default();
            (output.status.code().unwrap_or(-1), String::from_utf8_lossy(&output.stdout).into_owned(), calls)
        }
    }

    fn at(text: &str, needle: &str) -> usize {
        text.find(needle).unwrap_or_else(|| panic!("{needle:?} missing in {text:?}"))
    }

    #[test]
    fn full_run_order_and_failures() {
        let stubs = Stubs::new();
        let plan = UpdatePlan { system: true, aur: true, devel: true, flatpak: true, app_images: true, self_url: None };
        let (full, names) = update_script(&plan);
        assert_eq!(names, ["System packages", "Cache and services", "AUR packages", "Flatpak apps", "AppImages", "Orphan packages"]);
        let image = "abc\thttps://x/Foo.AppImage\tFoo/Bar";
        let (code, _, calls) = stubs.run(&full, &[image], &[]);
        assert_eq!(code, 0);
        assert!(at(&calls, "pacman -Syu --noconfirm") < at(&calls, "paru -Sua --noconfirm --skipreview --devel"));
        assert!(at(&calls, "paru") < at(&calls, "flatpak update -y --noninteractive"));
        assert!(
            calls.contains("curl -fsSL -o")
                && calls.contains("woof --import-appimage")
                && calls.contains("--replaces abc")
                && calls.contains("Foo_Bar.AppImage")
        );
        // Nothing after a failed system upgrade: no cleanup, no AUR
        let (code, _, calls) = stubs.run(&full, &[image], &[("FAIL_PACMAN", "1")]);
        assert_eq!((code, calls.as_str()), (1, "pkexec woofstore-root upgrade\npacman -Syu --noconfirm\n"));
        // A failed AUR step: the rest still runs, the result reports the failure
        let (code, _, calls) = stubs.run(&full, &[image], &[("FAIL_PARU", "1")]);
        assert!(code == 1 && calls.contains("flatpak update") && calls.contains("woof --import-appimage"));
        let (only_apps, names) = update_script(&UpdatePlan { flatpak: true, ..Default::default() });
        assert_eq!(names, ["Flatpak apps"]);
        assert!(!only_apps.contains("pacman") && !only_apps.contains("paru"));
        assert_eq!(stubs.run(&only_apps, &[], &[]).2, "flatpak update -y --noninteractive\n");
        // The password prompt was cancelled: nothing ran, so nothing else is updated
        stubs.stub("pkexec", "echo \"pkexec ${1##*/} $2\" >> \"$LOG\"; exit 126\n");
        let (code, _, calls) = stubs.run(&full, &[image], &[]);
        assert_eq!((code, calls.as_str()), (1, "pkexec woofstore-root upgrade\n"));
    }

    #[test]
    fn root_step_asks_once_trims_cache_and_lists_services() {
        let stubs = Stubs::new();
        let (script, _) = update_script(&UpdatePlan { system: true, ..Default::default() });
        let (code, out, calls) = stubs.run(&script, &[], &[]);
        assert!(code == 0 && out.contains("==> Cache and services") && out.contains("2 packages removed"));
        assert_eq!(calls.matches("pkexec").count(), 1); // The password is asked once
        assert!(
            at(&calls, "pacman -Syu") < at(&calls, "paccache -rk3\n")
                && calls.contains("paccache -ruk0\n")
                && at(&calls, "paccache -rk3") < at(&calls, "checkservices")
        );
        // Listing only, display managers ignored
        assert!(calls.contains("checkservices -F -P -R -i gdm.service") && calls.contains("-i sddm.service") && !calls.contains("systemctl"));
        assert_eq!(parse_services_to_restart(&out), ["dbus-broker.service", "bluetooth.service"]);
    }

    #[test]
    fn store_updates_itself_last_from_its_own_archives_only() {
        let stubs = Stubs::new();
        let url = "https://github.com/LaskyWolf/woofstore/archive/refs/tags/v9.9.9.tar.gz";
        let (with_self, names) =
            update_script(&UpdatePlan { system: true, aur: true, devel: true, flatpak: true, app_images: true, self_url: Some(url.into()) });
        assert_eq!(&names[names.len() - 2..], ["Orphan packages", "WoofStore"]);
        assert!(at(&with_self, "==> WoofStore") > at(&with_self, "==> Orphan packages"));
        let (_, names) =
            update_script(&UpdatePlan { flatpak: true, self_url: Some("https://evil.example/x'; rm -rf ~; '.tar.gz".into()), ..Default::default() });
        assert_eq!(names, ["Flatpak apps"]); // Any other address is ignored
        stubs.stub("tar", "echo \"tar $*\" >> \"$LOG\"; : > \"$3/PKGBUILD\"\n");
        stubs.stub("makepkg", "echo \"makepkg $*\" >> \"$LOG\"; : > woofstore-9.9.9-1-x86_64.pkg.tar.zst\n");
        let only_self = update_script(&UpdatePlan { self_url: Some(url.into()), ..Default::default() }).0;
        let (code, _, calls) = stubs.run(&only_self, &[], &[]);
        assert!(
            code == 0
                && calls.contains(&format!("curl -fsSL {url}"))
                && calls.contains("makepkg -sf --noconfirm")
                && calls.contains("pkexec woofstore-root install-files")
        );
        assert!(at(&calls, "makepkg") < at(&calls, "pacman -U --noconfirm") && calls.contains("woofstore-9.9.9-1-x86_64.pkg.tar.zst"));
        // The release's built package, when its checksum matches: no build
        stubs.stub(
            "curl",
            "echo \"curl $*\" >> \"$LOG\"; case $4 in *.sha256) cd \"${3%/*}\" && sha256sum woofstore-x86_64.pkg.tar.zst > \"$3\";; *) echo built > \"$3\";; esac\n",
        );
        let (code, _, calls) = stubs.run(&only_self, &[], &[]);
        assert!(code == 0 && !calls.contains("makepkg") && calls.contains("/releases/download/v9.9.9/woofstore-x86_64.pkg.tar.zst"));
        assert!(calls.contains("pacman -U --noconfirm -- ") && calls.contains("/woofstore-x86_64.pkg.tar.zst\n"));
        // A wrong checksum falls back to building
        stubs.stub("curl", "echo \"curl $*\" >> \"$LOG\"; case $4 in *.sha256) echo '0000  woofstore-x86_64.pkg.tar.zst' > \"$3\";; *) echo built > \"$3\";; esac; [ -n \"$3\" ]\n");
        stubs.stub("tar", "echo \"tar $*\" >> \"$LOG\"; : > \"$3/PKGBUILD\"\n");
        let (code, _, calls) = stubs.run(&only_self, &[], &[]);
        assert!(code == 0 && calls.contains("makepkg -sf --noconfirm"));
        // A failed build installs nothing
        stubs.stub("makepkg", "echo \"makepkg $*\" >> \"$LOG\"; exit 1\n");
        let (code, _, calls) = stubs.run(&only_self, &[], &[]);
        assert!(code == 1 && !calls.contains("pacman -U"));
    }
}
