//! The AUR as the store asks it: package facts and search through the RPC, the build files downloaded for review (never run
//! here), and the pacman questions an AUR install needs: which dependencies the repositories lack, which packages are orphans.

use crate::update_check::Sources;
use serde_json::Value;
use std::collections::BTreeMap;
use std::fs;
use std::path::Path;
use std::sync::Arc;
use woofstore_core::catalog::App;
use woofstore_core::packages::{parse_orphans, valid_package_name, Orphan};
use woofstore_core::review::{parse_aur_info, sha256, AurInfo, ReviewSnapshot};

/// Percent-encodes everything but the unreserved characters, as QUrl::toPercentEncoding does. A "+" left alone in a query would
/// reach the AUR as a space.
pub fn encode(text: &str) -> String {
    text.bytes().map(|b| if b.is_ascii_alphanumeric() || b"-._~".contains(&b) { char::from(b).to_string() } else { format!("%{b:02X}") }).collect()
}

/// The AUR's facts about one package, or a message for the user.
pub fn aur_info(sources: &dyn Sources, name: &str) -> Result<AurInfo, String> {
    if !valid_package_name(name) {
        return Err("Invalid AUR package name.".into());
    }
    let json = sources.get(&format!("https://aur.archlinux.org/rpc/v5/info?arg[]={}", encode(name)), 0).map_err(|e| format!("Cannot reach the AUR: {e}"))?;
    // The base becomes a git address later: an answer with a strange one is treated as no answer.
    parse_aur_info(&json).filter(|info| valid_package_name(&info.base)).ok_or_else(|| format!("The AUR does not list a package named {name}."))
}

/// AUR packages whose name or description matches `query`. Fewer than two characters search nothing. Results with a name or base
/// pacman would not accept are dropped. `icon` is the package name; the interface falls back to a generic package icon.
pub fn search_aur(sources: &dyn Sources, query: &str) -> Result<Vec<App>, String> {
    let query = query.trim();
    if query.chars().count() < 2 {
        return Ok(Vec::new());
    }
    let body =
        sources.get(&format!("https://aur.archlinux.org/rpc/v5/search/{}?by=name-desc", encode(query)), 0).map_err(|e| format!("AUR search failed: {e}"))?;
    let root: Value = serde_json::from_slice(&body).unwrap_or(Value::Null);
    let Some(results) = root["results"].as_array() else { return Err("The AUR returned an invalid response.".into()) };
    if root["type"] == "error" {
        return Err(root["error"].as_str().unwrap_or("").to_string());
    }
    let text = |item: &Value, key: &str| item[key].as_str().unwrap_or("").to_string();
    Ok(results
        .iter()
        .map(|item| App {
            id: text(item, "Name"),
            name: text(item, "Name"),
            base: text(item, "PackageBase"),
            summary: text(item, "Description"),
            version: text(item, "Version"),
            source: "AUR".into(),
            homepage: text(item, "URL"),
            icon: text(item, "Name"),
            ..Default::default()
        })
        .filter(|app| valid_package_name(&app.id) && valid_package_name(&app.base))
        .collect())
}

/// Build files downloaded for review: the text the user reads, the AUR commit it came from, and the snapshot that keeps the files
/// and their hashes until the build. Check [`woofstore_core::review::review_unchanged`] right before building.
#[derive(Debug, Clone)]
pub struct PreparedReview {
    /// Each file after a "──── name ────" line
    pub recipe: String,
    pub commit: String,
    pub snapshot: ReviewSnapshot,
}

const MAX_FILE: u64 = 256 * 1024;
const MAX_RECIPE: usize = 1024 * 1024;

/// Downloads the build files of AUR package base `base` into a new folder under `cache_dir/aur-build` and reads them, without
/// running anything they contain: git hooks are off and local-file clones are refused. Refuses a recipe the user cannot review
/// in full as text: symlinks, files over 256 KiB, control characters in names, binary files, over 1 MiB in total, no PKGBUILD.
/// Not /tmp: it is often a small RAM disk, and the same folder later holds the build, which can need gigabytes.
pub fn prepare_aur(sources: &dyn Sources, cache_dir: &Path, base: &str) -> Result<PreparedReview, String> {
    if !valid_package_name(base) {
        return Err("Invalid AUR package base.".into());
    }
    let build_root = cache_dir.join("aur-build");
    let dir = fs::create_dir_all(&build_root)
        .and_then(|()| tempfile::Builder::new().prefix("review-").tempdir_in(&build_root))
        .map_err(|_| "Cannot create a review folder.".to_string())?;
    let snapshot = ReviewSnapshot { dir: Some(Arc::new(dir)), hashes: BTreeMap::new() };
    let checkout = snapshot.path().expect("the snapshot has a folder");
    let checkout_arg = checkout.to_string_lossy();
    let url = format!("https://aur.archlinux.org/{base}.git");
    let git = |args: &[&str]| {
        let out = sources.run("git", args);
        if out.ok() {
            Ok(out.stdout)
        } else {
            Err(out.stderr)
        }
    };
    git(&["-c", "core.hooksPath=/dev/null", "-c", "protocol.file.allow=never", "clone", "--depth=1", "--", &url, &checkout_arg])
        .map_err(|e| format!("Cannot download AUR build files: {e}"))?;
    let commit = git(&["-C", &checkout_arg, "rev-parse", "HEAD"])?;
    let listed = git(&["-C", &checkout_arg, "ls-files", "-z"])?;

    let mut recipe = String::new();
    let mut hashes = BTreeMap::new();
    for name in listed.split('\0').filter(|n| !n.is_empty()) {
        let path = checkout.join(name);
        let meta = fs::symlink_metadata(&path);
        let reviewable = meta.as_ref().is_ok_and(|m| m.file_type().is_file() && m.len() <= MAX_FILE) && !name.chars().any(|c| c < '\x20');
        if !reviewable {
            return Err("This recipe contains a symlink, oversized file, or unsupported filename. Review it manually before installing.".into());
        }
        let bytes = fs::read(&path).map_err(|e| e.to_string())?;
        if bytes.contains(&0) {
            return Err("This recipe includes a binary file. The store cannot provide a complete text review.".into());
        }
        if recipe.len() + bytes.len() > MAX_RECIPE {
            return Err("The review exceeds 1 MiB. Review this package manually.".into());
        }
        hashes.insert(name.to_string(), sha256(&bytes));
        recipe += &format!("\n──── {name} ────\n{}\n", String::from_utf8_lossy(&bytes));
    }
    if !hashes.contains_key("PKGBUILD") {
        return Err("No PKGBUILD was found.".into());
    }
    Ok(PreparedReview { recipe, commit: commit.trim().to_string(), snapshot: ReviewSnapshot { hashes, ..snapshot } })
}

/// The dependencies no repository provides, by name, provided name or version: those must come from the AUR.
pub fn missing_from_repos(sources: &dyn Sources, dependencies: &[&str]) -> Vec<String> {
    if dependencies.is_empty() {
        return Vec::new();
    }
    // pacman -Sp prints the download address when a repository can install the dependency. The names go in as arguments, never
    // into the script text.
    let mut args = vec!["-c", "for d; do pacman -Sp -- \"$d\" >/dev/null 2>&1 || echo \"$d\"; done", "woof-store-deps"];
    args.extend_from_slice(dependencies);
    sources.run("bash", &args).stdout.lines().filter(|l| !l.is_empty()).map(str::to_string).collect()
}

/// Packages installed as dependencies that nothing needs or optionally needs any more, with their installed size when expac is there.
pub fn orphans(sources: &dyn Sources) -> Vec<Orphan> {
    let script = "pacman -Qdtq | while read -r n; do printf '%s\\t%s\\n' \"$n\" \"$(expac -H M '%m' \"$n\" 2>/dev/null)\"; done";
    parse_orphans(&sources.run("bash", &["-c", script]).stdout)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::command::Output;
    use std::collections::HashMap;
    use std::sync::Mutex;
    use woofstore_core::review::review_unchanged;

    type Runner = Box<dyn Fn(&str, &[&str]) -> Output + Sync>;

    /// Web pages by address, and commands answered by `runner`; every address asked for is recorded.
    #[derive(Default)]
    pub(crate) struct Fake {
        pub pages: HashMap<String, Vec<u8>>,
        pub asked: Mutex<Vec<String>>,
        pub runner: Option<Runner>,
        pub ran: Mutex<Vec<Vec<String>>>,
    }

    impl Sources for Fake {
        fn run(&self, program: &str, args: &[&str]) -> Output {
            self.ran.lock().unwrap().push(std::iter::once(program).chain(args.iter().copied()).map(str::to_string).collect());
            match &self.runner {
                Some(runner) => runner(program, args),
                None => Output { code: -1, stdout: String::new(), stderr: "missing".into() },
            }
        }
        fn get(&self, url: &str, _: u64) -> Result<Vec<u8>, String> {
            self.asked.lock().unwrap().push(url.into());
            self.pages.get(url).cloned().ok_or_else(|| "offline".into())
        }
    }

    fn ok(stdout: &str) -> Output {
        Output { code: 0, stdout: stdout.into(), stderr: String::new() }
    }

    #[test]
    fn info_from_the_rpc() {
        let mut fake = Fake::default();
        fake.pages.insert(
            "https://aur.archlinux.org/rpc/v5/info?arg[]=libfoo%2B%2B".into(),
            br#"{"results":[{"Name":"libfoo++","PackageBase":"libfoo","NumVotes":3,"Popularity":0.5}]}"#.to_vec(),
        );
        fake.pages.insert("https://aur.archlinux.org/rpc/v5/info?arg[]=bad-base".into(), br#"{"results":[{"Name":"bad-base","PackageBase":"../x"}]}"#.to_vec());
        let info = aur_info(&fake, "libfoo++").unwrap();
        assert!(info.base == "libfoo" && info.votes == 3);
        assert_eq!(aur_info(&fake, "bad-base").unwrap_err(), "The AUR does not list a package named bad-base.");
        assert_eq!(aur_info(&fake, "gone").unwrap_err(), "Cannot reach the AUR: offline");
        assert_eq!(aur_info(&fake, "--help").unwrap_err(), "Invalid AUR package name.");
        assert_eq!(fake.asked.lock().unwrap().len(), 3); // the invalid name was never sent
    }

    #[test]
    fn search() {
        let mut fake = Fake::default();
        fake.pages.insert(
            "https://aur.archlinux.org/rpc/v5/search/pac%20seek?by=name-desc".into(),
            br#"{"type":"search","results":[{"Name":"pacseek","PackageBase":"pacseek","Version":"1.8-1","Description":"TUI","URL":"https://p.example"},{"Name":"$(id)","PackageBase":"x"}]}"#.to_vec(),
        );
        fake.pages.insert(
            "https://aur.archlinux.org/rpc/v5/search/to?by=name-desc".into(),
            br#"{"type":"error","results":[],"error":"Too many package results."}"#.to_vec(),
        );
        fake.pages.insert("https://aur.archlinux.org/rpc/v5/search/xx?by=name-desc".into(), b"<html>".to_vec());
        let found = search_aur(&fake, " pac seek ").unwrap();
        assert_eq!(found.len(), 1);
        let app = &found[0];
        assert_eq!(
            (app.id.as_str(), app.base.as_str(), app.version.as_str(), app.source.as_str(), app.homepage.as_str()),
            ("pacseek", "pacseek", "1.8-1", "AUR", "https://p.example")
        );
        assert_eq!(search_aur(&fake, "to").unwrap_err(), "Too many package results.");
        assert_eq!(search_aur(&fake, "xx").unwrap_err(), "The AUR returned an invalid response.");
        assert_eq!(search_aur(&fake, "zz").unwrap_err(), "AUR search failed: offline");
        assert_eq!(search_aur(&fake, " a ").unwrap(), []);
    }

    /// A fake git whose clone writes `files` (name, content) into the checkout, or a symlink for content "->target".
    fn git_with(files: &[(&str, &str)]) -> Fake {
        let files: Vec<(String, String)> = files.iter().map(|(n, c)| (n.to_string(), c.to_string())).collect();
        let runner: Runner = Box::new(move |program, args| {
            assert_eq!(program, "git");
            if args.contains(&"clone") {
                assert_eq!(&args[..4], ["-c", "core.hooksPath=/dev/null", "-c", "protocol.file.allow=never"]);
                let checkout = Path::new(args.last().unwrap());
                for (name, content) in &files {
                    let path = checkout.join(name);
                    fs::create_dir_all(path.parent().unwrap()).unwrap();
                    match content.strip_prefix("->") {
                        Some(target) => std::os::unix::fs::symlink(target, path).unwrap(),
                        None => fs::write(path, content).unwrap(),
                    }
                }
                ok("")
            } else if args.contains(&"rev-parse") {
                ok("0123456789abcdef0123456789abcdef01234567\n")
            } else {
                ok(&files.iter().map(|(name, _)| format!("{name}\0")).collect::<String>())
            }
        });
        Fake { runner: Some(runner), ..Default::default() }
    }

    #[test]
    fn prepared_review_and_its_snapshot() {
        let cache = tempfile::tempdir().unwrap();
        let fake = git_with(&[("PKGBUILD", "pkgname=foo\n"), ("sub/fix.patch", "x")]);
        let review = prepare_aur(&fake, cache.path(), "foo").unwrap();
        assert_eq!(review.recipe, "\n──── PKGBUILD ────\npkgname=foo\n\n\n──── sub/fix.patch ────\nx\n");
        assert_eq!(review.commit.len(), 40);
        let root = review.snapshot.path().unwrap();
        assert!(root.starts_with(cache.path().join("aur-build")));
        assert!(fake.ran.lock().unwrap()[0].contains(&"https://aur.archlinux.org/foo.git".to_string()));
        assert_eq!(review_unchanged(&review.snapshot), Ok(()));
        fs::write(root.join("PKGBUILD"), "pkgname=foo\n# integration-test change\n").unwrap();
        assert!(review_unchanged(&review.snapshot).unwrap_err().contains("changed"));
        // The folder lives as long as the snapshot
        drop(review);
        assert!(!root.exists());
    }

    #[test]
    fn unreviewable_recipes_are_refused() {
        let cache = tempfile::tempdir().unwrap();
        assert_eq!(prepare_aur(&Fake::default(), cache.path(), "../anything").unwrap_err(), "Invalid AUR package base.");
        let refused = |fake: Fake| prepare_aur(&fake, cache.path(), "foo").unwrap_err();
        assert!(refused(git_with(&[("PKGBUILD", ""), ("link", "->/etc/passwd")])).contains("symlink"));
        assert!(refused(git_with(&[("PKGBUILD", ""), ("bad\nname", "")])).contains("unsupported filename"));
        assert!(refused(git_with(&[("PKGBUILD", ""), ("blob", "a\0b")])).contains("binary file"));
        assert_eq!(refused(git_with(&[("README", "")])), "No PKGBUILD was found.");
        let big = "x".repeat(200 * 1024);
        assert!(refused(git_with(&["PKGBUILD", "a", "b", "c", "d", "e"].map(|name| (name, big.as_str())))).contains("exceeds 1 MiB"));
        assert!(refused(git_with(&[("PKGBUILD", &"x".repeat(300 * 1024))])).contains("oversized"));
        let offline = Fake { runner: Some(Box::new(|_, _| Output { code: 128, stdout: String::new(), stderr: "no network".into() })), ..Default::default() };
        assert_eq!(refused(offline), "Cannot download AUR build files: no network");
    }

    #[test]
    fn pacman_questions() {
        let fake = Fake {
            runner: Some(Box::new(|program, args| {
                assert_eq!(program, "bash");
                if args.len() > 2 {
                    assert_eq!(args[2..], ["woof-store-deps", "cmake", "lito>=0.8.5"]); // names stay arguments
                    ok("lito>=0.8.5\n")
                } else {
                    ok("libfoo\t1.2 MiB\nbar-git\n\n$(rm -rf x)\t3 MiB\n")
                }
            })),
            ..Default::default()
        };
        assert_eq!(missing_from_repos(&fake, &["cmake", "lito>=0.8.5"]), ["lito>=0.8.5"]);
        assert!(missing_from_repos(&fake, &[]).is_empty());
        let found = orphans(&fake);
        assert_eq!(found, [Orphan { name: "libfoo".into(), size: "1.2 MiB".into() }, Orphan { name: "bar-git".into(), size: String::new() }]);
        assert_eq!(fake.ran.lock().unwrap().len(), 2); // no dependencies: pacman is not asked
    }
}
