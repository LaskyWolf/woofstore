//! Loads the catalogue, the installed list and the news from this computer and prints counts.
use woofstore_system::{appstream::Language, catalogue, installed, news, paths, update_check::System};

fn main() {
    let start = std::time::Instant::now();
    let (catalogue, problems) = catalogue::load(&System, &paths::cache(), &Language::from_environment());
    println!("catalogue: {} apps, metadata missing: {}, problems: {problems:?} ({:?})", catalogue.apps.len(), catalogue.os_metadata_missing, start.elapsed());
    let installed = installed::load(&System, "en");
    println!("installed: {} apps, {} native packages, problems: {:?}", installed.apps.len(), installed.native.len(), installed.problems);
    if std::env::args().any(|a| a == "--news") {
        let (items, problems) = news::load_news(|url| woofstore_system::http::get(url, 0));
        println!("news: {} items, problems: {problems:?}", items.len());
    }
}
