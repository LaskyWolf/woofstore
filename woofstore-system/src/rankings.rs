//! Popularity and ratings: Flathub collections and pkgstats order the catalogue, and ODRS reviews, Flathub installs and AUR votes
//! fill an app's ratings row. Every answer is kept for a day, and the last saved copy is shown when the computer is offline.

use crate::aur_remote::aur_info;
use crate::http;
use crate::update_check::Sources;
use std::fs;
use std::path::Path;
use std::thread;
use std::time::{Duration, SystemTime};
use woofstore_core::catalog::{parse_flathub_stats, parse_odrs, parse_rankings, CatalogApp, Rankings, Ratings, Source};

const ONE_DAY: Duration = Duration::from_secs(24 * 3600);

/// The answer saved at `path` while it is less than a day old, otherwise a new one from `url`, saved when it is a JSON object.
/// A failed or invalid answer falls back to the saved copy (empty when there is none) and reports `true`: shown offline.
pub(crate) fn saved_or_fetched(sources: &dyn Sources, path: &Path, url: &str) -> (Vec<u8>, bool) {
    let fresh = fs::metadata(path).and_then(|m| m.modified()).is_ok_and(|time| SystemTime::now().duration_since(time).unwrap_or_default() < ONE_DAY);
    if fresh {
        if let Ok(data) = fs::read(path) {
            return (data, false);
        }
    }
    match sources.get(url, 0) {
        Ok(body) if serde_json::from_slice::<serde_json::Value>(&body).is_ok_and(|v| v.is_object()) => {
            let _ = http::save(path, &body);
            (body, false)
        }
        _ => (fs::read(path).unwrap_or_default(), true),
    }
}

/// Flathub's popular, trending, recently added and recently updated apps, and the pkgstats share of every Arch package. The
/// files keep 1.x's names in `cache_dir`, so either version reuses the other's copy.
pub fn load_rankings(sources: &dyn Sources, cache_dir: &Path) -> Rankings {
    const SOURCES: [(&str, &str); 5] = [
        ("flathub-popular.json", "https://flathub.org/api/v2/collection/popular?page=1&per_page=250"),
        ("flathub-trending.json", "https://flathub.org/api/v2/collection/trending?page=1&per_page=50"),
        ("pkgstats.json", "https://pkgstats.archlinux.de/api/packages?limit=3000&offset=0"),
        ("flathub-added.json", "https://flathub.org/api/v2/collection/recently-added?page=1&per_page=60"),
        ("flathub-updated.json", "https://flathub.org/api/v2/collection/recently-updated?page=1&per_page=60"),
    ];
    let answers: Vec<(Vec<u8>, bool)> = thread::scope(|scope| {
        let handles: Vec<_> = SOURCES.iter().map(|(file, url)| scope.spawn(move || saved_or_fetched(sources, &cache_dir.join(file), url))).collect();
        handles.into_iter().map(|h| h.join().unwrap_or_default()).collect()
    });
    let [popular, trending, pkgstats, added, updated] = &answers[..] else { unreachable!("one answer per source") };
    let mut rankings = parse_rankings(&popular.0, &trending.0, &pkgstats.0, &added.0, &updated.0);
    rankings.offline = answers.iter().any(|(_, offline)| *offline);
    rankings
}

/// What people say about `app`: ODRS reviews and Flathub installs for its Flatpak, votes for its AUR package, and the pkgstats
/// share its repository package already carries. Web answers are kept for a day in `cache_dir/http`; offline, the saved ones count.
pub fn ratings(sources: &dyn Sources, cache_dir: &Path, app: &CatalogApp) -> Ratings {
    let first = |source: Source| app.packages.iter().find(|p| p.source == source);
    let mut result =
        Ratings { repo_percent: app.packages.iter().find(|p| p.source == Source::Repo && p.popularity > 0.0).map(|p| p.popularity), ..Default::default() };
    // The id goes into an address: anything outside Flathub's id characters is not asked for.
    let flatpak =
        first(Source::Flatpak).map(|p| p.name.as_str()).filter(|id| !id.is_empty() && id.bytes().all(|b| b.is_ascii_alphanumeric() || b"._-".contains(&b)));
    let aur = first(Source::Aur).map(|p| p.name.as_str());
    let cached = |url: String| saved_or_fetched(sources, &http::cache_path(cache_dir, &url), &url).0;
    thread::scope(|scope| {
        let odrs = flatpak.map(|id| scope.spawn(move || cached(format!("https://odrs.gnome.org/1.0/reviews/api/ratings/{id}"))));
        let stats = flatpak.map(|id| scope.spawn(move || cached(format!("https://flathub.org/api/v2/stats/{id}"))));
        let votes = aur.map(|name| scope.spawn(move || aur_info(sources, name)));
        if let Some(json) = odrs.and_then(|h| h.join().ok()) {
            parse_odrs(&json, &mut result, 5);
        }
        if let Some(json) = stats.and_then(|h| h.join().ok()) {
            parse_flathub_stats(&json, &mut result);
        }
        // Unreachable AUR or unknown package: no votes row
        if let Some(Ok(info)) = votes.and_then(|h| h.join().ok()) {
            result.aur_votes = Some(info.votes);
            result.aur_popularity = info.popularity;
        }
    });
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::aur_remote::tests::Fake;
    use woofstore_core::catalog::Package;

    #[test]
    fn rankings_are_saved_and_used_offline() {
        let cache = tempfile::tempdir().unwrap();
        let mut fake = Fake::default();
        fake.pages.insert(
            "https://flathub.org/api/v2/collection/popular?page=1&per_page=250".into(),
            br#"{"hits":[{"app_id":"org.kde.krita","installs_last_month":1000}]}"#.to_vec(),
        );
        fake.pages.insert(
            "https://pkgstats.archlinux.de/api/packages?limit=3000&offset=0".into(),
            br#"{"packagePopularities":[{"name":"firefox","popularity":60}]}"#.to_vec(),
        );
        fake.pages.insert("https://flathub.org/api/v2/collection/trending?page=1&per_page=50".into(), b"not json".to_vec());
        let rankings = load_rankings(&fake, cache.path());
        assert_eq!(rankings.popular, ["org.kde.krita"]);
        assert_eq!(rankings.arch_popularity["firefox"], 60.0);
        assert!(rankings.offline); // trending, added and updated did not answer
        assert!(cache.path().join("flathub-popular.json").exists() && !cache.path().join("flathub-trending.json").exists());

        // Fresh copies are not asked for again; old ones are, and a failure falls back to them
        let asked = fake.asked.lock().unwrap().len();
        fake.pages.clear();
        assert_eq!(load_rankings(&fake, cache.path()).popular, ["org.kde.krita"]);
        assert_eq!(fake.asked.lock().unwrap().len(), asked + 3);
        let old = SystemTime::now() - 2 * ONE_DAY;
        fs::File::options().write(true).open(cache.path().join("flathub-popular.json")).unwrap().set_modified(old).unwrap();
        let offline = load_rankings(&fake, cache.path());
        assert!(offline.offline && offline.popular == ["org.kde.krita"]);
    }

    #[test]
    fn every_kind_of_rating() {
        let cache = tempfile::tempdir().unwrap();
        let mut fake = Fake::default();
        fake.pages.insert("https://odrs.gnome.org/1.0/reviews/api/ratings/org.kde.krita".into(), br#"{"star4":5,"star5":5,"total":10}"#.to_vec());
        fake.pages.insert("https://flathub.org/api/v2/stats/org.kde.krita".into(), br#"{"installs_total":1781622,"installs_last_month":35610}"#.to_vec());
        fake.pages.insert(
            "https://aur.archlinux.org/rpc/v5/info?arg[]=krita-git".into(),
            br#"{"results":[{"Name":"krita-git","PackageBase":"krita-git","NumVotes":12,"Popularity":0.25}]}"#.to_vec(),
        );
        let mut repo = Package::new(Source::Repo, "krita", "extra", "1");
        repo.popularity = 4.5;
        let app = CatalogApp {
            packages: vec![Package::new(Source::Flatpak, "org.kde.krita", "flathub", "1"), repo, Package::new(Source::Aur, "krita-git", "aur", "1")],
            ..Default::default()
        };
        let rated = ratings(&fake, cache.path(), &app);
        assert!(rated.reviews == 10 && (rated.stars - 4.5).abs() < 0.001);
        assert_eq!((rated.installs, rated.aur_votes, rated.aur_popularity, rated.repo_percent), (Some(1781622), Some(12), 0.25, Some(4.5)));

        // Offline: the saved ODRS and Flathub answers still count, the AUR row goes away
        let old = SystemTime::now() - 2 * ONE_DAY;
        for path in fs::read_dir(cache.path().join("http")).unwrap() {
            fs::File::options().write(true).open(path.unwrap().path()).unwrap().set_modified(old).unwrap();
        }
        fake.pages.clear();
        let offline = ratings(&fake, cache.path(), &app);
        assert!(offline.reviews == 10 && offline.installs == Some(1781622) && offline.aur_votes.is_none());

        // An id that does not look like a Flatpak id is never put into an address
        let odd = CatalogApp { packages: vec![Package::new(Source::Flatpak, "../x?y", "flathub", "1")], ..Default::default() };
        let before = fake.asked.lock().unwrap().len();
        assert!(!ratings(&fake, cache.path(), &odd).any());
        assert_eq!(fake.asked.lock().unwrap().len(), before);
    }
}
