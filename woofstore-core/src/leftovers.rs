//! Files an uninstalled app commonly leaves in the user's home folder, and removing them. Deleting is the dangerous part: every
//! path is checked to stay inside home first.

use std::collections::HashSet;
use std::fs;
use std::path::{Component, Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Leftover {
    pub path: PathBuf,
    /// "Cache", "Settings", "Data" or "State"
    pub kind: &'static str,
    pub bytes: u64,
    /// Only caches: they are rebuilt on demand; settings and data may hold the user's work.
    pub checked_by_default: bool,
}

/// Every file below `root` (hidden ones too) and symlinks to files. Symlinks to folders are not followed, so a link cannot pull
/// files from outside into the list; broken links and special files are skipped.
pub(crate) fn files_under(root: &Path) -> Vec<PathBuf> {
    let mut files = Vec::new();
    for entry in fs::read_dir(root).into_iter().flatten().flatten() {
        let path = entry.path();
        if entry.file_type().is_ok_and(|t| t.is_dir()) {
            files.extend(files_under(&path));
        } else if fs::metadata(&path).is_ok_and(|m| m.is_file()) {
            files.push(path);
        }
    }
    files
}

/// Bytes of a file (the target's, for a symlink) or of every file inside a folder.
fn size_of(path: &Path) -> u64 {
    let link = fs::symlink_metadata(path).is_ok_and(|m| m.file_type().is_symlink());
    if link || fs::metadata(path).is_ok_and(|m| m.is_file()) {
        return fs::metadata(path).map_or(0, |m| m.len());
    }
    files_under(path).iter().map(|file| fs::metadata(file).map_or(0, |m| m.len())).sum()
}

/// Entries of ~/.config, ~/.local/share, ~/.local/state and ~/.cache whose names equal one of `names` (case-insensitive,
/// "<name>rc" too). Exact matches only; names shorter than three characters are ignored.
pub fn find_leftovers(home: &Path, names: &[&str]) -> Vec<Leftover> {
    // Folders every desktop shares: a package called "icons" must never select them.
    const SHARED: [&str; 18] = [
        "applications",
        "icons",
        "themes",
        "fonts",
        "share",
        "bin",
        "systemd",
        "autostart",
        "dconf",
        "pulse",
        "mime",
        "locale",
        "kde",
        "gtk-3.0",
        "gtk-4.0",
        "trash",
        "flatpak",
        "recently-used.xbel",
    ];
    let mut wanted = HashSet::new();
    for name in names.iter().map(|n| n.to_lowercase()) {
        if name.chars().count() < 3 || SHARED.contains(&name.as_str()) {
            continue;
        }
        wanted.insert(format!("{name}rc"));
        wanted.insert(name);
    }
    let home = std::path::absolute(home).unwrap_or_else(|_| home.to_path_buf());
    let mut found = Vec::new();
    for (place, kind) in [(".config", "Settings"), (".local/share", "Data"), (".local/state", "State"), (".cache", "Cache")] {
        let mut entries: Vec<_> = fs::read_dir(home.join(place))
            .into_iter()
            .flatten()
            .flatten()
            // Like QDir: files and folders, links followed; broken links and special files left out.
            .filter(|e| fs::metadata(e.path()).is_ok_and(|m| m.is_file() || m.is_dir()))
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .filter(|name| wanted.contains(&name.to_lowercase()))
            .collect();
        entries.sort_by_key(|name| name.to_lowercase());
        for name in entries {
            let path = home.join(place).join(name);
            found.push(Leftover { bytes: size_of(&path), path, kind, checked_by_default: kind == "Cache" });
        }
    }
    found
}

/// ~/.var/app/<id>: where Flatpak keeps an app's data. `None` when absent.
pub fn flatpak_data(home: &Path, app_id: &str) -> Option<Leftover> {
    let path = home.join(".var/app").join(app_id);
    // "." and ".." would name ~/.var/app or ~/.var itself, not one app's data
    if app_id.is_empty() || app_id.contains('/') || app_id == "." || app_id == ".." || !path.is_dir() {
        return None;
    }
    Some(Leftover { bytes: size_of(&path), path, kind: "Data", checked_by_default: false })
}

/// The absolute form of a path with "." and ".." resolved by text only, so "home/.cache/.." is home itself and not something
/// inside it.
fn clean(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for part in std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf()).components() {
        match part {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other),
        }
    }
    out
}

/// Removes everything it can below a folder, then the folder; a symlink inside is removed, never followed. False when anything stayed.
fn remove_recursively(dir: &Path) -> bool {
    let mut ok = true;
    for entry in fs::read_dir(dir).into_iter().flatten().flatten() {
        let path = entry.path();
        ok &= if entry.file_type().is_ok_and(|t| t.is_dir()) { remove_recursively(&path) } else { fs::remove_file(&path).is_ok() };
    }
    ok && fs::remove_dir(dir).is_ok()
}

/// Removes a path inside home; a symlink is removed without touching its target. Refuses anything outside home.
pub fn delete_leftover(home: &Path, path: &Path) -> bool {
    let path = clean(path);
    // The folder holding the entry, resolved, so a symlinked entry itself can go while a symlinked folder cannot lead outside.
    let parent = path.parent().and_then(|p| fs::canonicalize(p).ok());
    let (Some(parent), Ok(root)) = (parent, fs::canonicalize(home)) else { return false };
    let Ok(meta) = fs::symlink_metadata(&path) else { return false };
    if !parent.starts_with(&root) {
        return false;
    }
    if meta.file_type().is_symlink() || meta.is_file() {
        return fs::remove_file(&path).is_ok();
    }
    // Something that is neither file nor folder (a socket, a pipe): QDir::removeRecursively reported success without removing it.
    !meta.is_dir() || remove_recursively(&path)
}

/// A size as "512 bytes" or "1.5 MB": powers of 1024 with the traditional unit names, one decimal.
pub fn format_size(bytes: u64) -> String {
    const UNITS: [&str; 6] = ["kB", "MB", "GB", "TB", "PB", "EB"];
    let power = if bytes == 0 { 0 } else { bytes.ilog2() / 10 };
    if power == 0 {
        return format!("{bytes} bytes");
    }
    format!("{:.1} {}", bytes as f64 / 1024f64.powi(power as i32), UNITS[power as usize - 1])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn leftovers() {
        let fake = tempfile::tempdir().unwrap();
        let home = fake.path();
        let touch = |relative: &str| {
            let path = home.join(relative);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(path, "12345").unwrap();
        };
        for file in [
            ".config/Krita/a.conf",
            ".config/kritarc",
            ".cache/krita/x",
            ".local/share/krita/y",
            ".local/share/applications/krita.desktop",
            ".config/other/z",
            ".var/app/org.kde.krita/data/d",
            ".config/icons/i",
        ] {
            touch(file);
        }
        let left = find_leftovers(home, &["krita", "icons", "go"]);
        let mut kinds: Vec<String> = left
            .iter()
            .map(|item| format!("{}:{}{}", item.kind, item.path.file_name().unwrap().to_string_lossy(), if item.checked_by_default { "*" } else { "" }))
            .collect();
        kinds.sort();
        assert_eq!(kinds, ["Cache:krita*", "Data:krita", "Settings:Krita", "Settings:kritarc"]); // not applications/, icons/, other/
        assert!(left[0].bytes > 0);
        assert!(flatpak_data(home, "org.kde.krita").is_some() && flatpak_data(home, "org.none").is_none() && flatpak_data(home, "../x").is_none());

        assert!(!delete_leftover(home, Path::new("/etc/hostname")) && !delete_leftover(home, &home.join("../x")));
        assert!(!delete_leftover(home, home) && !delete_leftover(home, &home.join(".config/..")));
        std::os::unix::fs::symlink(home.join(".config/other"), home.join(".config/linked")).unwrap();
        assert!(delete_leftover(home, &home.join(".config/linked")) && home.join(".config/other/z").exists()); // link only
        assert!(delete_leftover(home, &home.join(".cache/krita")) && !home.join(".cache/krita").exists());
        assert!(delete_leftover(home, &home.join(".var/app/org.kde.krita")) && !home.join(".var/app/org.kde.krita").exists());
    }

    #[test]
    fn sizes() {
        assert_eq!(format_size(0), "0 bytes");
        assert_eq!(format_size(1023), "1023 bytes");
        assert_eq!(format_size(1536), "1.5 kB");
        assert_eq!(format_size(5 * 1024 * 1024), "5.0 MB");
    }

    #[test]
    fn flatpak_data_never_names_a_parent_folder() {
        let home = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(home.path().join(".var/app/org.a.B")).unwrap();
        assert!(flatpak_data(home.path(), "org.a.B").is_some());
        for id in ["", ".", "..", "a/b"] {
            assert!(flatpak_data(home.path(), id).is_none(), "{id:?}");
        }
    }
}
