//! Loads the real catalogues of this computer and prints what was found: `cargo run --release --example catalogue_stats`.
use woofstore_system::{appstream, paths};

fn main() {
    let start = std::time::Instant::now();
    let files = appstream::catalogue_files(&paths::home(), &[]);
    let (components, problems) = appstream::load_all(&files, &appstream::Language::from_environment());
    let icons = components.iter().filter(|c| c.cached_icon().is_some()).count();
    let flatpaks = components.iter().filter(|c| c.flatpak_bundle.is_some()).count();
    println!("{} files, {} apps ({} Flatpak, {} with icons) in {:?}", files.len(), components.len(), flatpaks, icons, start.elapsed());
    for problem in problems {
        println!("problem: {problem}");
    }
}
