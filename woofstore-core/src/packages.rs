//! Names and small outputs of the package managers. Everything here comes from outside the store, so names are checked
//! before they go near a command line.

use regex::Regex;
use std::sync::LazyLock;

/// A name pacman, the AUR or GitHub would accept, and that can never be read as an option or a path.
pub fn valid_package_name(name: &str) -> bool {
    static PATTERN: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[a-zA-Z0-9][a-zA-Z0-9@._+\-]*$").unwrap());
    name.len() <= 200 && PATTERN.is_match(name)
}

/// The application ID a .flatpakref file installs (its `Name=` line in the `[Flatpak Ref]` group); `None` when there is none.
pub fn flatpak_ref_id(text: &str) -> Option<String> {
    static ID: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[A-Za-z0-9_.-]+$").unwrap());
    let mut in_ref = false;
    for line in text.lines().map(str::trim) {
        if line.starts_with('[') {
            in_ref = line.eq_ignore_ascii_case("[Flatpak Ref]");
        } else if let Some(id) = line.strip_prefix("Name=").filter(|_| in_ref) {
            let id = id.trim();
            return ID.is_match(id).then(|| id.to_string());
        }
    }
    None
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Orphan {
    pub name: String,
    pub size: String,
}

/// Lines of "name" or "name<TAB>size"; names pacman would not accept are dropped.
pub fn parse_orphans(text: &str) -> Vec<Orphan> {
    text.lines()
        .filter_map(|line| {
            let mut fields = line.trim().split('\t');
            let name = fields.next()?;
            valid_package_name(name).then(|| Orphan { name: name.into(), size: fields.next().unwrap_or("").trim().into() })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn package_names() {
        assert!(valid_package_name("visual-studio-code-bin") && valid_package_name("libfoo++"));
        for name in ["--help", "../package", "foo;touch /tmp/x", "foo\nbar", "", "$(id)"] {
            assert!(!valid_package_name(name), "{name:?}");
        }
    }

    #[test]
    fn flatpakref_ids() {
        assert_eq!(flatpak_ref_id("[Flatpak Ref]\nTitle=Foo\nName=org.foo.Bar\nBranch=stable\n").as_deref(), Some("org.foo.Bar"));
        assert_eq!(flatpak_ref_id("[Flatpak Repo]\nName=flathub\n"), None);
        assert_eq!(flatpak_ref_id("[Flatpak Ref]\nName=a b; rm\n"), None);
        assert_eq!(flatpak_ref_id(""), None);
    }

    #[test]
    fn orphans() {
        let orphans = parse_orphans("libfoo\t1.2 MiB\nbar-git\n\n$(rm -rf x)\t3 MiB\n");
        assert_eq!(orphans, [Orphan { name: "libfoo".into(), size: "1.2 MiB".into() }, Orphan { name: "bar-git".into(), size: String::new() }]);
    }
}
