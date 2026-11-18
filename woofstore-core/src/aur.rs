//! Building AUR packages in a clean chroot: the chroot's pacman.conf, the bash script of a build, the order of the builds,
//! and the AUR package list the store searches.

use regex::Regex;
use std::collections::HashMap;
use std::sync::LazyLock;

/// The pacman.conf for the AUR build chroot, made from the host's: same repositories and signature levels (so multilib and CachyOS
/// resolve), host-only paths dropped, and every "Include" replaced by that file's Server lines (the chroot has no mirrorlist files of
/// its own). `read_file` returns the text of a file, or an empty string when it cannot be read. Repositories left without a server
/// are dropped.
pub fn chroot_pacman_conf(host_conf: &str, read_file: impl Fn(&str) -> String) -> String {
    const KEPT_OPTIONS: [&str; 4] = ["Architecture", "SigLevel", "LocalFileSigLevel", "RemoteFileSigLevel"];
    // A mirrorlist can hold hundreds of servers; pacman only ever tries the first few.
    const MAX_SERVERS: usize = 10;
    let mut options = String::from("[options]\n");
    let (mut repos, mut section, mut body) = (String::new(), String::new(), String::new());
    let mut servers = 0;
    let flush = |repos: &mut String, section: &str, body: &mut String, servers: &mut usize| {
        if !section.is_empty() && *servers > 0 {
            repos.push_str(&format!("\n[{section}]\n{body}"));
        }
        body.clear();
        *servers = 0;
    };
    for line in host_conf.split('\n').map(str::trim) {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            flush(&mut repos, &section, &mut body, &mut servers);
            section = line[1..line.len() - 1].trim().to_string();
            continue;
        }
        let Some((key, value)) = line.split_once('=') else { continue };
        let (key, value) = (key.trim(), value.trim());
        if section == "options" {
            if KEPT_OPTIONS.contains(&key) {
                options += &format!("{key} = {value}\n");
            }
            continue;
        }
        if section.is_empty() {
            continue;
        }
        if key == "SigLevel" || key == "Usage" {
            body += &format!("{key} = {value}\n");
        } else if key == "Server" && servers < MAX_SERVERS {
            body += &format!("Server = {value}\n");
            servers += 1;
        } else if key == "Include" {
            for item in read_file(value).split('\n').map(str::trim) {
                let Some((name, server)) = item.split_once('=') else { continue };
                if item.starts_with('#') || name.trim() != "Server" || servers >= MAX_SERVERS {
                    continue;
                }
                body += &format!("Server = {}\n", server.trim());
                servers += 1;
            }
        }
    }
    flush(&mut repos, &section, &mut body, &mut servers);
    options + &repos
}

/// The bash script of an AUR build: $1 package name, $2 "--needed" or "", $3 chroot directory holding pacman.conf (see
/// [`chroot_pacman_conf`]), then one "name<TAB>folder" per package to build, dependencies first and $1 last. Creates the chroot the
/// first time, otherwise updates it, builds each folder (each build sees the packages built before it) and installs the exact built
/// files: dependencies as dependencies, then $1.
pub fn aur_build_script() -> &'static str {
    // makepkg.conf comes from devtools (the same file pkgctl uses). mkarchroot, arch-nspawn and makechrootpkg ask for root with sudo themselves.
    r##"target=$1; flag=$2; root=$3; shift 3
[ -n "$root" ] && [ -f "$root/pacman.conf" ] || { echo "No build environment folder"; exit 1; }
if [ -f "$root/root/.arch-chroot" ]; then
  echo "==> Updating the build environment"
  arch-nspawn -C "$root/pacman.conf" "$root/root" pacman -Syu --noconfirm || exit
else
  echo "==> Preparing the build environment (first time only)"
  [ -e "$root/root" ] && sudo rm -rf --one-file-system -- "$root/root"
  mkarchroot -C "$root/pacman.conf" -M /usr/share/devtools/makepkg.conf.d/x86_64.conf "$root/root" base-devel || exit
fi
built=(); main=
for item; do
  name=${item%%$'\t'*}; cd "${item#*$'\t'}" || exit
  extra=(); for p in "${built[@]}"; do extra+=(-I "$p"); done
  makechrootpkg -r "$root" "${extra[@]}" || exit
  found=; for f in ./*.pkg.tar.*; do case "$f" in *.sig) continue;; esac; [ "$(pacman -Qpq "$f")" = "$name" ] && found=$f; done
  [ -n "$found" ] || { echo "No built package named $name"; exit 1; }
  main=$(realpath "$found"); [ "$name" = "$target" ] || built+=("$main")
done
echo "==> Installing package"
pkexec "$WOOFSTORE_ROOT" install-files $flag "$main" "${built[@]}"
"##
}

/// The package name of a dependency: "lito>=0.8.5" -> "lito".
pub fn dependency_name(dependency: &str) -> &str {
    dependency.split(['<', '>', '=']).next().unwrap_or("").trim()
}

/// Dependencies listed in a .SRCINFO (depends, makedepends, checkdepends), with version constraints, without the package's own names
/// (a split package may depend on its sibling).
pub fn srcinfo_dependencies(srcinfo: &str) -> Vec<String> {
    static DEPENDS: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^\s*(?:depends|makedepends|checkdepends)(?:_x86_64)?\s*=\s*(\S.*?)\s*$").unwrap());
    let (mut found, mut own) = (Vec::<String>::new(), Vec::<String>::new());
    for line in srcinfo.split('\n') {
        if let Some(c) = DEPENDS.captures(line) {
            if !found.iter().any(|f| f == &c[1]) {
                found.push(c[1].to_string());
            }
        } else if line.trim().starts_with("pkgname =") {
            own.push(line.split_once('=').map_or("", |(_, name)| name).trim().to_string());
        }
    }
    found.into_iter().filter(|d| !own.iter().any(|o| o == dependency_name(d))).collect()
}

/// The order to build `root` and the AUR packages it needs: every package after the ones it needs. `needs` maps a package to its AUR
/// dependencies; a dependency missing from `needs` is not an AUR build and is left out.
pub fn aur_build_order(root: &str, needs: &HashMap<String, Vec<String>>) -> Vec<String> {
    fn visit(name: &str, needs: &HashMap<String, Vec<String>>, order: &mut Vec<String>, visiting: &mut Vec<String>) {
        // A cycle cannot be built; the build itself reports it.
        if order.iter().chain(visiting.iter()).any(|n| n == name) {
            return;
        }
        visiting.push(name.to_string());
        for need in needs.get(name).into_iter().flatten().filter(|need| needs.contains_key(*need)) {
            visit(need, needs, order, visiting);
        }
        order.push(name.to_string());
    }
    let mut order = Vec::new();
    visit(root, needs, &mut order, &mut Vec::new());
    order
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::parse_aur_dump;
    use std::fs;
    use std::os::unix::fs::PermissionsExt;
    use std::path::Path;
    use std::process::Command;

    const HOST: &str = "[options]\nHoldPkg = pacman\nCacheDir = /host/cache\nArchitecture = auto\nParallelDownloads = 10\nSigLevel = Required DatabaseOptional\nLocalFileSigLevel = Optional\n\
                        [core]\nInclude = /etc/pacman.d/mirrorlist\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n[cachyos]\nSigLevel = Optional TrustAll\nInclude = /missing\n[own]\nServer = https://own.example/$repo\n#[commented]\n";

    fn conf() -> String {
        chroot_pacman_conf(HOST, |path| {
            if path == "/etc/pacman.d/mirrorlist" {
                "# comment\nServer = https://a.example/$repo/os/$arch\nServer = https://b.example/$repo/os/$arch\n".into()
            } else {
                String::new()
            }
        })
    }

    #[test]
    fn chroot_conf_keeps_repositories_and_expands_includes() {
        let conf = conf();
        assert!(conf.starts_with("[options]\nArchitecture = auto\nSigLevel = Required DatabaseOptional\nLocalFileSigLevel = Optional\n"));
        // Host paths, unreadable repos and comments are gone
        for gone in ["CacheDir", "HoldPkg", "Include", "[cachyos]", "commented"] {
            assert!(!conf.contains(gone), "{gone}");
        }
        assert!(conf.contains("[core]\nServer = https://a.example/$repo/os/$arch\nServer = https://b.example/$repo/os/$arch\n"));
        assert!(conf.contains("[multilib]\nServer = https://a.example") && conf.contains("[own]\nServer = https://own.example/$repo\n"));
        // pacman itself must accept it
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("pacman.conf");
        fs::write(&file, &conf).unwrap();
        if let Ok(out) = Command::new("pacman-conf").arg("--config").arg(&file).arg("--repo-list").output() {
            assert!(out.status.success());
            assert_eq!(String::from_utf8_lossy(&out.stdout).split('\n').filter(|l| !l.is_empty()).collect::<Vec<_>>(), ["core", "multilib", "own"]);
        }
    }

    struct Stubs {
        bin: tempfile::TempDir,
        work: tempfile::TempDir,
        chroot: String,
    }

    impl Stubs {
        fn new() -> Self {
            let (bin, work) = (tempfile::tempdir().unwrap(), tempfile::tempdir().unwrap());
            let stub = |name: &str, body: &str| {
                let path = bin.path().join(name);
                fs::write(&path, format!("#!/bin/sh\n{body}")).unwrap();
                fs::set_permissions(&path, fs::Permissions::from_mode(0o755)).unwrap();
            };
            stub("mkarchroot", "echo \"mkarchroot $*\" >> \"$LOG\"; mkdir -p \"$5\"; touch \"$5/.arch-chroot\"\n");
            stub("arch-nspawn", "echo \"arch-nspawn $*\" >> \"$LOG\"\n");
            stub("makechrootpkg", "echo \"makechrootpkg $(basename \"$PWD\") $*\" >> \"$LOG\"; [ -z \"$FAIL_BUILD\" ] && : > \"$(basename \"$PWD\")-1-1-x86_64.pkg.tar.zst\"; exit ${FAIL_BUILD:-0}\n");
            stub("pacman", "[ \"$1\" = -Qpq ] && basename \"$2\" | sed 's/-1-1-.*//'\n");
            stub("pkexec", "echo \"pkexec $*\" >> \"$LOG\"\n");
            stub("sudo", "echo \"sudo $*\" >> \"$LOG\"; \"$@\"\n");
            let chroot = format!("{}/chroot", work.path().display());
            fs::create_dir_all(&chroot).unwrap();
            fs::write(format!("{chroot}/pacman.conf"), conf()).unwrap();
            fs::create_dir_all(work.path().join("foo")).unwrap();
            fs::create_dir_all(work.path().join("dep")).unwrap();
            Stubs { bin, work, chroot }
        }

        fn item(&self, name: &str) -> String {
            format!("{name}\t{}/{name}", self.work.path().display())
        }

        fn build(&self, flag: &str, items: &[String], fail: bool) -> (i32, String) {
            let log = self.bin.path().join("log");
            let _ = fs::remove_file(&log);
            let path = format!("{}:{}", self.bin.path().display(), std::env::var("PATH").unwrap_or_default());
            let mut command = Command::new("bash");
            command
                .args(["-c", aur_build_script(), "x", "foo", flag, &self.chroot])
                .args(items)
                .env("PATH", path)
                .env("LOG", &log)
                .env("WOOFSTORE_ROOT", "woofstore-root");
            if fail {
                command.env("FAIL_BUILD", "1");
            } else {
                command.env_remove("FAIL_BUILD");
            }
            let status = command.output().unwrap().status;
            (status.code().unwrap_or(-1), fs::read_to_string(&log).unwrap_or_default())
        }
    }

    fn at(text: &str, needle: &str) -> usize {
        text.find(needle).unwrap_or_else(|| panic!("{needle:?} missing from {text:?}"))
    }

    #[test]
    fn build_script_with_stand_ins() {
        let s = Stubs::new();
        let (chroot, work) = (&s.chroot, s.work.path().display().to_string());
        let (foo, dep) = (s.item("foo"), s.item("dep"));

        let (code, log) = s.build("--needed", std::slice::from_ref(&foo), false);
        assert_eq!(code, 0);
        assert!(log.contains("base-devel") && !log.contains("arch-nspawn"));
        assert!(at(&log, &format!("mkarchroot -C {chroot}/pacman.conf")) < at(&log, &format!("makechrootpkg foo -r {chroot}")));
        assert!(at(&log, "makechrootpkg") < at(&log, &format!("pkexec woofstore-root install-files --needed {work}/foo/foo-1-1-x86_64.pkg.tar.zst\n")));

        // The chroot exists now: it is updated, not created
        let (code, log) = s.build("", std::slice::from_ref(&foo), false);
        assert!(code == 0 && !log.contains("mkarchroot"));
        assert!(log.contains(&format!("arch-nspawn -C {chroot}/pacman.conf {chroot}/root pacman -Syu --noconfirm")));
        assert!(log.contains(&format!("pkexec woofstore-root install-files {work}/foo/foo-1-1")));

        // An AUR dependency is built first, is seen by the next build, and is installed as a dependency before the package itself
        let (code, log) = s.build("--needed", &[dep.clone(), foo.clone()], false);
        assert!(code == 0 && at(&log, "makechrootpkg dep") < at(&log, "makechrootpkg foo"));
        assert!(log.contains(&format!("makechrootpkg foo -r {chroot} -I {work}/dep/dep-1-1-x86_64.pkg.tar.zst")));
        assert!(!log.contains(&format!("makechrootpkg dep -r {chroot} -I")));
        assert!(
            log.contains(&format!("pkexec woofstore-root install-files --needed {work}/foo/foo-1-1-x86_64.pkg.tar.zst {work}/dep/dep-1-1-x86_64.pkg.tar.zst"))
        );

        // A failed build installs nothing and stops the rest
        let (code, log) = s.build("--needed", &[dep, foo], true);
        assert!(code == 1 && !log.contains("pkexec") && !log.contains("makechrootpkg foo"));
        assert!(Path::new(chroot).join("root/.arch-chroot").exists());
    }

    #[test]
    fn srcinfo_dependencies_and_build_order() {
        let srcinfo = "pkgbase = foo\n\tpkgver = 1\n\tmakedepends = cmake\n\tmakedepends = lito>=0.8.5\n\tdepends = bar\n\tdepends_x86_64 = lib32-baz\n\tcheckdepends = cmake\n\tprovides = notadep\n\toptdepends = opt: not needed\n\npkgname = foo\n\tdepends = foo-libs\n\npkgname = foo-libs\n";
        assert_eq!(srcinfo_dependencies(srcinfo), ["cmake", "lito>=0.8.5", "bar", "lib32-baz"]);
        assert_eq!((dependency_name("lito>=0.8.5"), dependency_name("a<2"), dependency_name("plain")), ("lito", "a", "plain"));

        let needs = |pairs: &[(&str, &[&str])]| -> HashMap<String, Vec<String>> {
            pairs.iter().map(|(k, v)| (k.to_string(), v.iter().map(|s| s.to_string()).collect())).collect()
        };
        assert_eq!(aur_build_order("foo", &needs(&[("foo", &["a", "b"]), ("a", &["b"]), ("b", &[])])), ["b", "a", "foo"]);
        assert_eq!(aur_build_order("x", &needs(&[("x", &["y"]), ("y", &["x"])])), ["y", "x"]);
        assert_eq!(aur_build_order("solo", &HashMap::new()), ["solo"]);
    }

    #[test]
    fn aur_dump() {
        let json = br#"[{"Name":"woof","Version":"1-1","NumVotes":3,"Description":"d","URL":"https://w.example","Popularity":0.5},{"Name":"quiet","Version":"2-1","NumVotes":0,"Description":"hidden"},{"Name":"--bad","NumVotes":9}]"#;
        let aur = parse_aur_dump(json).unwrap();
        assert_eq!(aur.len(), 2);
        assert!(aur[0].name == "woof" && aur[0].detailed() && aur[0].url == "https://w.example" && aur[0].popularity == 0.5);
        assert!(aur[1].name == "quiet" && !aur[1].detailed() && aur[1].description.is_empty() && aur[1].version == "2-1");
        assert_eq!(parse_aur_dump(b"{}"), None);
        assert_eq!(parse_aur_dump(b"not json"), None);
    }
}
