//! Pending updates of every source, checked side by side: Arch repositories, the AUR, Flatpak, imported AppImages and
//! WoofStore itself.

use crate::command::{self, Output};
use crate::http;
use sha1::{Digest, Sha1};
use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::path::PathBuf;
use std::thread;
use std::time::Duration;
use woofstore_core::appimage::{self, UpdateInfo};
use woofstore_core::updates::{parse_arch_updates, parse_flatpak_updates, parse_self_update, UpdateEntry};

const SLOW: Duration = Duration::from_secs(120);

/// An imported AppImage to compare with its publisher's newest file.
#[derive(Debug, Clone)]
pub struct InstalledImage {
    pub id: String,
    pub name: String,
    pub path: PathBuf,
}

/// The updates that were found, and the sources that could not be checked.
#[derive(Debug, Default)]
pub struct Found {
    pub entries: Vec<UpdateEntry>,
    pub problems: Vec<String>,
}

/// What the checks read from the outside world; tests replace it.
pub trait Sources: Sync {
    fn run(&self, program: &str, args: &[&str]) -> Output;
    fn get(&self, url: &str, first_bytes: u64) -> Result<Vec<u8>, String>;
}

pub struct System;

impl Sources for System {
    fn run(&self, program: &str, args: &[&str]) -> Output {
        command::run(program, args, SLOW)
    }
    fn get(&self, url: &str, first_bytes: u64) -> Result<Vec<u8>, String> {
        http::get(url, first_bytes)
    }
}

/// `flatpak_versions`: installed Flatpak app id -> version, shown as "from". `current`: the running store's version.
pub fn check_all(sources: &dyn Sources, flatpak_versions: &HashMap<String, String>, images: &[InstalledImage], current: &str) -> Found {
    thread::scope(|scope| {
        let store = scope.spawn(|| {
            // Offline or rate limited: no entry, nothing to report
            let json = sources.get("https://api.github.com/repos/LaskyWolf/woofstore/releases/latest", 0).unwrap_or_default();
            parse_self_update(&String::from_utf8_lossy(&json), current).into_iter().collect::<Vec<_>>()
        });
        // Exit codes: checkupdates 2 and paru 1 mean "nothing to update"; a missing tool (-1) is skipped.
        let arch = scope.spawn(|| {
            let out = sources.run("checkupdates", &[]);
            match out.code {
                0 => Ok(parse_arch_updates(&out.stdout, "Arch")),
                2 | -1 => Ok(Vec::new()),
                _ => Err(format!("Arch packages: {}", out.stderr.trim())),
            }
        });
        let aur = scope.spawn(|| {
            let out = sources.run("paru", &["-Qua"]);
            if out.ok() {
                parse_arch_updates(&out.stdout, "AUR")
            } else {
                Vec::new()
            }
        });
        let flatpak = scope.spawn(|| {
            let out = sources.run("flatpak", &["remote-ls", "--updates", "--columns=application,name,version,branch"]);
            match out.code {
                0 => Ok(parse_flatpak_updates(&out.stdout, flatpak_versions)),
                -1 => Ok(Vec::new()),
                _ => Err(format!("Flatpak: {}", out.stderr.trim())),
            }
        });
        let images = scope.spawn(|| check_app_images(sources, images));
        let mut found = Found::default();
        found.entries.extend(store.join().unwrap_or_default());
        for result in [arch.join(), flatpak.join()] {
            match result.unwrap_or_else(|_| Err("A check stopped unexpectedly".into())) {
                Ok(entries) => found.entries.extend(entries),
                Err(problem) => found.problems.push(problem),
            }
        }
        found.entries.extend(aur.join().unwrap_or_default());
        let images = images.join().unwrap_or_default();
        found.entries.extend(images.entries);
        found.problems.extend(images.problems);
        found
    })
}

fn sha1_of(path: &std::path::Path) -> io::Result<String> {
    let mut file = File::open(path)?;
    let mut hasher = Sha1::new();
    io::copy(&mut file, &mut hasher)?;
    Ok(hex::encode(hasher.finalize()))
}

/// AppImages that publish update information are compared by checksum with the newest file the publisher lists.
pub fn check_app_images(sources: &dyn Sources, images: &[InstalledImage]) -> Found {
    let results: Vec<Found> = thread::scope(|scope| {
        let handles: Vec<_> = images.iter().map(|image| scope.spawn(move || check_one(sources, image))).collect();
        handles.into_iter().map(|h| h.join().unwrap_or_default()).collect()
    });
    let mut found = Found::default();
    for result in results {
        found.entries.extend(result.entries);
        found.problems.extend(result.problems);
    }
    found
}

fn check_one(sources: &dyn Sources, image: &InstalledImage) -> Found {
    let mut found = Found::default();
    // No update information: nothing to compare
    let Some(info) = appimage::update_info_text(&image.path).and_then(|text| appimage::parse_update_info(&text)) else { return found };
    let (zsync_url, guessed_url, version, read_name_from_header) = match info {
        // name.AppImage.zsync -> name.AppImage, unless the .zsync file names the real file (nightly builds change it)
        UpdateInfo::Zsync { url } => (url.clone(), url.trim_end_matches(".zsync").to_string(), String::new(), true),
        UpdateInfo::GitHub { owner, repo, tag, pattern } => {
            let release = if tag == "latest" { "latest".to_string() } else { format!("tags/{tag}") };
            let answer = sources.get(&format!("https://api.github.com/repos/{owner}/{repo}/releases/{release}"), 0);
            let json = String::from_utf8_lossy(answer.as_deref().unwrap_or_default()).into_owned();
            match appimage::pick_release_asset(&json, &pattern) {
                // GitHub names the exact file
                Some(asset) => (asset.zsync_url, asset.url, asset.version, false),
                None => {
                    found.problems.push(format!("{}: {}", image.name, answer.err().unwrap_or_else(|| "no matching release file".into())));
                    return found;
                }
            }
        }
    };
    let header = sources.get(&zsync_url, 4096);
    let Some(newest) = header.as_deref().ok().and_then(appimage::zsync_sha1) else {
        found.problems.push(format!("{}: {}", image.name, header.err().unwrap_or_else(|| "no checksum in the update file".into())));
        return found;
    };
    let local = sha1_of(&image.path).unwrap_or_default();
    if newest != local {
        let target = if read_name_from_header { header.as_deref().ok().and_then(|h| appimage::zsync_download_url(h, &zsync_url)) } else { None };
        found.entries.push(UpdateEntry {
            id: image.id.clone(),
            name: image.name.clone(),
            from: String::new(),
            to: if version.is_empty() { "newer".into() } else { version },
            source: "AppImage".into(),
            url: target.unwrap_or(guessed_url),
        });
    }
    found
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    #[derive(Default)]
    struct Fake {
        outputs: HashMap<String, Output>,
        pages: HashMap<String, Vec<u8>>,
        asked: Mutex<Vec<String>>,
    }

    impl Sources for Fake {
        fn run(&self, program: &str, _: &[&str]) -> Output {
            self.outputs.get(program).cloned().unwrap_or(Output { code: -1, stdout: String::new(), stderr: "missing".into() })
        }
        fn get(&self, url: &str, _: u64) -> Result<Vec<u8>, String> {
            self.asked.lock().unwrap().push(url.into());
            self.pages.get(url).cloned().ok_or_else(|| "offline".into())
        }
    }

    fn out(code: i32, stdout: &str, stderr: &str) -> Output {
        Output { code, stdout: stdout.into(), stderr: stderr.into() }
    }

    #[test]
    fn every_source_and_its_exit_codes() {
        let mut fake = Fake::default();
        fake.outputs.insert("checkupdates".into(), out(0, "linux 1-1 -> 2-1\n", ""));
        fake.outputs.insert("paru".into(), out(1, "", "")); // Nothing to update
        fake.outputs.insert("flatpak".into(), out(1, "", "remote down\n"));
        fake.pages.insert("https://api.github.com/repos/LaskyWolf/woofstore/releases/latest".into(), br#"{"tag_name":"v9.0.0"}"#.to_vec());
        let found = check_all(&fake, &HashMap::new(), &[], "1.3.0");
        let names: Vec<_> = found.entries.iter().map(|e| (e.name.as_str(), e.source.as_str())).collect();
        assert_eq!(names, [("WoofStore", "WoofStore"), ("linux", "Arch")]);
        assert_eq!(found.problems, ["Flatpak: remote down"]);
        fake.outputs.insert("checkupdates".into(), out(2, "", "")); // Nothing to update
        fake.outputs.remove("flatpak"); // Not installed: skipped
        let found = check_all(&fake, &HashMap::new(), &[], "9.0.0");
        assert!(found.entries.is_empty() && found.problems.is_empty());
    }

    /// An AppImage file whose update information points at `info`.
    fn image_with(dir: &std::path::Path, info: &str) -> InstalledImage {
        // The same minimal ELF the core crate tests use, written here with the update information only
        let mut elf = vec![0u8; 64];
        elf[..4].copy_from_slice(b"\x7fELF");
        elf[4] = 2;
        elf[5] = 1;
        let mut upd = info.as_bytes().to_vec();
        upd.push(0);
        let names = b"\0.shstrtab\0.upd_info\0".to_vec();
        let put = |bytes: &mut [u8], at: usize, value: u64, size: usize| (0..size).for_each(|i| bytes[at + i] = (value >> (8 * i)) as u8);
        let (info_at, names_at) = (64u64, 64 + upd.len() as u64);
        let table_at = names_at + names.len() as u64;
        let mut table = vec![0u8; 192];
        put(&mut table, 64, 1, 4);
        put(&mut table, 64 + 0x18, names_at, 8);
        put(&mut table, 64 + 0x20, names.len() as u64, 8);
        put(&mut table, 128, 11, 4);
        put(&mut table, 128 + 0x18, info_at, 8);
        put(&mut table, 128 + 0x20, upd.len() as u64, 8);
        put(&mut elf, 0x28, table_at, 8);
        put(&mut elf, 0x3A, 64, 2);
        put(&mut elf, 0x3C, 3, 2);
        put(&mut elf, 0x3E, 1, 2);
        let path = dir.join("Eden.AppImage");
        std::fs::write(&path, [elf, upd, names, table].concat()).unwrap();
        InstalledImage { id: "abc".into(), name: "Eden".into(), path }
    }

    #[test]
    fn zsync_appimage_downloads_the_file_the_header_names() {
        let dir = tempfile::tempdir().unwrap();
        let image = image_with(dir.path(), "zsync|https://n.example/latest/Eden.AppImage.zsync");
        let mut fake = Fake::default();
        let header = "zsync: 0.6.2\nURL: Eden-7f1b.AppImage\nSHA-1: 0123456789abcdef0123456789abcdef01234567\n";
        fake.pages.insert("https://n.example/latest/Eden.AppImage.zsync".into(), header.as_bytes().to_vec());
        let found = check_app_images(&fake, std::slice::from_ref(&image));
        assert_eq!(found.entries.len(), 1);
        assert_eq!(found.entries[0].url, "https://n.example/latest/Eden-7f1b.AppImage");
        // The same checksum as the installed file: up to date
        let local = sha1_of(&image.path).unwrap();
        fake.pages.insert("https://n.example/latest/Eden.AppImage.zsync".into(), format!("SHA-1: {local}\n").into_bytes());
        assert!(check_app_images(&fake, &[image]).entries.is_empty());
    }

    #[test]
    fn github_appimage_keeps_the_exact_asset_address() {
        let dir = tempfile::tempdir().unwrap();
        let image = image_with(dir.path(), "gh-releases-zsync|o|r|latest|Eden-*.AppImage.zsync");
        let mut fake = Fake::default();
        fake.pages.insert(
            "https://api.github.com/repos/o/r/releases/latest".into(),
            br#"{"tag_name":"v2","assets":[{"name":"Eden-2.AppImage","browser_download_url":"https://gh/Eden-2.AppImage"},{"name":"Eden-2.AppImage.zsync","browser_download_url":"https://gh/Eden-2.AppImage.zsync"}]}"#.to_vec(),
        );
        fake.pages.insert("https://gh/Eden-2.AppImage.zsync".into(), b"URL: elsewhere.AppImage\nSHA-1: 0123456789abcdef0123456789abcdef01234567\n".to_vec());
        let found = check_app_images(&fake, std::slice::from_ref(&image));
        assert_eq!((found.entries[0].url.as_str(), found.entries[0].to.as_str()), ("https://gh/Eden-2.AppImage", "v2"));
        // No release answer: reported as a problem, not as an update
        fake.pages.clear();
        let found = check_app_images(&fake, &[image]);
        assert!(found.entries.is_empty() && found.problems == ["Eden: offline"]);
    }
}
