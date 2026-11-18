mod aur;
mod catalog;
mod dialogs;
mod news;
mod shell;
mod store;
mod updates;

use std::os::unix::fs::MetadataExt;
use std::path::Path;
use woofstore_core::appimage_import::{import_app_image, replace_app_image};

const USAGE: &str = "Usage: woof-store [LINK]
Software store and system updater for CachyOS and Arch.

  LINK                          an appstream: link, a .flatpakref or .flatpak file, or an app name to open
  --import-appimage PATH        copy an AppImage into the app menu and exit (used by the install window); repeatable
  --replaces ID                 with --import-appimage: the id of the imported AppImage this file updates
  -h, --help                    show this help";

/// The helper run of an AppImage install: copies each file and prints the step lines the install window follows.
fn import(files: &[String], replaces: Option<&str>) -> i32 {
    let data_root = woofstore_system::paths::data_home();
    let mut failures = 0;
    for file in files {
        println!("==> Copying {}", Path::new(file).file_name().map(|n| n.to_string_lossy()).unwrap_or_default());
        let result = match replaces {
            Some(id) => replace_app_image(Path::new(file), id, &data_root),
            None => import_app_image(Path::new(file), &data_root),
        };
        match result {
            Ok(_) => println!("==> Adding to the app menu"),
            Err(error) => {
                println!("error: {error}");
                failures += 1;
            }
        }
    }
    i32::from(failures > 0)
}

fn main() {
    // The owner of /proc/self is the effective user
    if std::fs::metadata("/proc/self").is_ok_and(|m| m.uid() == 0) {
        eprintln!("Run WoofStore as your normal user. Package managers request elevation when needed.");
        std::process::exit(1);
    }
    let args: Vec<String> = std::env::args().skip(1).collect();
    let (mut files, mut replaces, mut link) = (Vec::new(), None, String::new());
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-h" | "--help" => {
                println!("{USAGE}");
                return;
            }
            "--import-appimage" | "--replaces" if i + 1 >= args.len() => {
                eprintln!("{} needs a value\n\n{USAGE}", args[i]);
                std::process::exit(2);
            }
            "--import-appimage" => {
                files.push(args[i + 1].clone());
                i += 1;
            }
            "--replaces" => {
                replaces = Some(args[i + 1].clone());
                i += 1;
            }
            other if other.starts_with("--") => {
                eprintln!("Unknown option {other}\n\n{USAGE}");
                std::process::exit(2);
            }
            other if link.is_empty() => link = other.to_string(),
            _ => {}
        }
        i += 1;
    }
    if !files.is_empty() {
        std::process::exit(import(&files, replaces.as_deref()));
    }
    std::process::exit(shell::ffi::run_shell("qrc:/qt/qml/org/woofstore/app/qml/Main.qml", &link));
}
