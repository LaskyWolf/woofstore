//! AppImages the user imports: a private executable copy named after its SHA-256 and a menu launcher for it, both
//! under the user's data folder.

use crate::catalog::App;
use crate::launchers::{complete_base_name, desktop_quote};
use regex::Regex;
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::io::{Read, Write};
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

/// An I/O error as Qt words it: without Rust's " (os error N)" suffix.
fn message(error: std::io::Error) -> String {
    let text = error.to_string();
    text.split(" (os error").next().unwrap_or(&text).to_string()
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// The SHA-256 of a file; `None` when it cannot be read.
pub fn digest_file(path: &Path) -> Option<[u8; 32]> {
    let mut file = File::open(path).ok()?;
    let mut hash = Sha256::new();
    std::io::copy(&mut file, &mut hash).ok()?;
    Some(hash.finalize().into())
}

fn app_dir(data_root: &Path) -> PathBuf {
    data_root.join("WoofStore/appimages")
}

fn launcher_path(data_root: &Path, id: &str) -> PathBuf {
    data_root.join("applications").join(format!("woof-store-{id}.desktop"))
}

/// Writes through a temporary file next to path, so path is either complete or untouched (as QSaveFile does).
/// `fill` returns `Err` with a message to abandon the file.
fn save(path: &Path, fill: impl FnOnce(&mut File) -> Result<(), String>) -> Result<(), String> {
    let temporary = path.with_file_name(format!(".{}.part", path.file_name().unwrap_or_default().to_string_lossy()));
    let result = (|| {
        let mut file = File::create(&temporary).map_err(message)?;
        fill(&mut file)?;
        file.sync_all().map_err(message)?;
        fs::rename(&temporary, path).map_err(message)
    })();
    if result.is_err() {
        let _ = fs::remove_file(&temporary);
    }
    result
}

/// Copies a type 1 or type 2 AppImage to `<data_root>/WoofStore/appimages/<sha256>.AppImage` (owner-only, executable)
/// and adds `<data_root>/applications/woof-store-<sha256>.desktop`. The source file is left in place. `data_root` is
/// the user's data folder, usually ~/.local/share. Returns the copy's path.
pub fn import_app_image(file: &Path, data_root: &Path) -> Result<PathBuf, String> {
    let mut source = File::open(file).map_err(message)?;
    let mut header = Vec::with_capacity(16);
    (&mut source).take(16).read_to_end(&mut header).map_err(message)?;
    if header.len() < 16 || &header[..4] != b"\x7fELF" || &header[8..10] != b"AI" || (header[10] != 1 && header[10] != 2) {
        return Err("This file is not a type 1 or type 2 AppImage. Select an AppImage from its publisher.".into());
    }
    let id = hex(&digest_file(file).ok_or("Could not read the whole AppImage.")?);
    static CONTROL: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"[\x00-\x1f\x7f]").unwrap());
    let title = CONTROL.replace_all(&complete_base_name(&file.to_string_lossy()), " ").replace('\\', "_");
    let (folder, desktop_dir) = (app_dir(data_root), data_root.join("applications"));
    if data_root.to_string_lossy().contains(['\n', '\r']) {
        return Err("The application data path contains unsupported characters.".into());
    }
    if fs::create_dir_all(&folder).is_err() || fs::create_dir_all(&desktop_dir).is_err() {
        return Err("Cannot create the app or launcher folder.".into());
    }
    let dest = folder.join(format!("{id}.AppImage"));
    let launcher = launcher_path(data_root, &id);
    if dest.exists() || launcher.exists() {
        return Err("This AppImage is already imported, or its destination already exists.".into());
    }
    // The copy is checked against the hash the id was made from: a file that changed while it was read is not imported.
    let mut source = File::open(file).map_err(message)?;
    save(&dest, |copy| {
        let mut hash = Sha256::new();
        let mut chunk = vec![0u8; 1024 * 1024];
        loop {
            let read = source.read(&mut chunk).map_err(message)?;
            if read == 0 {
                break;
            }
            hash.update(&chunk[..read]);
            copy.write_all(&chunk[..read]).map_err(message)?;
        }
        if hex(&hash.finalize()) != id {
            return Err("The source file changed during import. Try again.".into());
        }
        Ok(())
    })?;
    if fs::set_permissions(&dest, fs::Permissions::from_mode(0o700)).is_err() {
        let _ = fs::remove_file(&dest);
        return Err("Cannot make the imported AppImage executable.".into());
    }
    let entry = format!(
        "[Desktop Entry]\nType=Application\nName={title}\nExec={}\nIcon=application-x-executable\nTerminal=false\nCategories=Utility;\nX-WoofStore-AppImage={id}\n",
        desktop_quote(&dest.to_string_lossy())
    );
    if save(&launcher, |desktop| desktop.write_all(entry.as_bytes()).map_err(message)).is_err() {
        let _ = fs::remove_file(&dest);
        return Err("Cannot save the launcher. The imported copy was removed.".into());
    }
    Ok(dest)
}

/// Imports a newer file of an imported AppImage and removes the old copy and its launcher; the old one stays when the
/// import fails.
pub fn replace_app_image(file: &Path, old_id: &str, data_root: &Path) -> Result<PathBuf, String> {
    static ID: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^[0-9a-f]{64}$").unwrap());
    if !ID.is_match(old_id) {
        return Err("This is not an imported AppImage.".into());
    }
    let dest = import_app_image(file, data_root)?;
    let _ = fs::remove_file(app_dir(data_root).join(format!("{old_id}.AppImage")));
    let _ = fs::remove_file(launcher_path(data_root, old_id));
    Ok(dest)
}

/// Every imported AppImage, sorted by file name; the name comes from its launcher.
pub fn imported_app_images(data_root: &Path) -> Vec<App> {
    let folder = app_dir(data_root);
    let Ok(entries) = fs::read_dir(&folder) else { return Vec::new() };
    // Like QDir's listing: visible files only, the pattern and the order ignore case.
    let mut files: Vec<String> = entries
        .flatten()
        .filter(|e| e.path().is_file())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|name| !name.starts_with('.') && name.to_lowercase().ends_with(".appimage"))
        .collect();
    files.sort_by_key(|name| name.to_lowercase());
    files
        .into_iter()
        .map(|file| {
            let id = complete_base_name(&file);
            let launcher = launcher_path(data_root, &id);
            let mut name = id.clone();
            if let Ok(text) = fs::read(&launcher) {
                for line in text.split(|&b| b == b'\n') {
                    if let Some(value) = line.strip_prefix(b"Name=") {
                        name = String::from_utf8_lossy(value).into_owned();
                    }
                }
            }
            App {
                path: folder.join(&file).to_string_lossy().into_owned(),
                name,
                source: "AppImage".into(),
                launcher: launcher.to_string_lossy().into_owned(),
                scope: "user".into(),
                summary: "Imported portable app".into(),
                icon: "application-x-executable".into(),
                id,
                ..Default::default()
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fake_app_image(path: &Path) -> Vec<u8> {
        let mut bytes = vec![0u8; 64];
        bytes[..4].copy_from_slice(b"\x7fELF");
        bytes[8..11].copy_from_slice(b"AI\x02");
        fs::write(path, &bytes).unwrap();
        bytes
    }

    #[test]
    fn import_copies_and_adds_a_launcher() {
        let temp = tempfile::tempdir().unwrap();
        let source = temp.path().join("My App%25.AppImage");
        let bytes = fake_app_image(&source);
        let root = temp.path().join("user data");
        let imported = import_app_image(&source, &root).unwrap();
        assert!(fs::metadata(&imported).unwrap().permissions().mode() & 0o777 == 0o700 && source.exists());
        assert_eq!(fs::read(&imported).unwrap(), bytes);
        let apps = imported_app_images(&root);
        assert!(apps.len() == 1 && apps[0].name == "My App%25" && Path::new(&apps[0].path) == imported);
        let launcher = fs::read_to_string(root.join(format!("applications/woof-store-{}.desktop", apps[0].id))).unwrap();
        assert!(launcher.contains(&format!("Exec={}", desktop_quote(&imported.to_string_lossy()))));
        assert_eq!(apps[0].id, hex(&digest_file(&source).unwrap()));
        assert!(import_app_image(&source, &root).unwrap_err().contains("already"));

        let invalid = temp.path().join("fake.AppImage");
        fs::write(&invalid, "#!/bin/sh\necho not an AppImage\n").unwrap();
        assert!(import_app_image(&invalid, &temp.path().join("invalid-data")).is_err());
        assert!(!temp.path().join("invalid-data").exists());
        assert!(import_app_image(&temp.path().join("missing"), &root).unwrap_err().contains("No such file"));
    }

    #[test]
    fn replace_removes_the_old_copy() {
        let temp = tempfile::tempdir().unwrap();
        let root = temp.path().join("data");
        let old = temp.path().join("Foo.AppImage");
        fake_app_image(&old);
        let first = import_app_image(&old, &root).unwrap();
        let old_id = complete_base_name(&first.to_string_lossy());
        let mut newer = fs::read(&old).unwrap();
        newer.push(1);
        fs::write(&old, &newer).unwrap();
        assert!(replace_app_image(&old, "../x", &root).is_err());
        let second = replace_app_image(&old, &old_id, &root).unwrap();
        assert!(!first.exists() && second.exists() && !launcher_path(&root, &old_id).exists());
        assert_eq!(imported_app_images(&root).len(), 1);
    }
}
