//! The catalogue model: what one app on screen is made of, the package lists it is built from, and the popularity
//! and ratings that order it.

use crate::packages::valid_package_name;
use regex::Regex;
use serde_json::{Map, Value};
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufReader, BufWriter, Read, Write};
use std::path::Path;
use std::sync::LazyLock;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Hash)]
pub enum Source {
    Flatpak,
    #[default]
    Repo,
    Aur,
}

/// One installable thing from one source.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Package {
    pub source: Source,
    /// Flatpak app ID, repo package name, or AUR package name
    pub name: String,
    /// flathub, extra, cachyos-v3, aur
    pub repo: String,
    pub version: String,
    /// Source-native score; compare only within one source.
    pub popularity: f64,
}

impl Package {
    pub fn new(source: Source, name: &str, repo: &str, version: &str) -> Self {
        Package { source, name: name.into(), repo: repo.into(), version: version.into(), popularity: 0.0 }
    }
}

/// AppStream URL kind ("homepage", "vcs-browser", ...) and address.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Link {
    pub kind: String,
    pub url: String,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum MetadataOrigin {
    #[default]
    None,
    AppStream,
    Overlay,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Release {
    pub version: String,
    /// ISO date ("2026-07-01"), empty when unknown
    pub date: String,
    /// Markup
    pub notes: String,
}

/// One entry on screen. `packages` lists every source that provides it.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct CatalogApp {
    /// Normalized AppStream ID, or "pkg:<name>" / "aur:<name>" without metadata
    pub key: String,
    pub name: String,
    pub summary: String,
    pub description: String,
    pub homepage: String,
    pub icon_path: String,
    pub categories: Vec<String>,
    pub screenshots: Vec<String>,
    /// Homepage first when present
    pub links: Vec<Link>,
    pub keywords: Vec<String>,
    pub packages: Vec<Package>,
    /// Original AppStream ID, for add-on lookups
    pub component_id: String,
    /// Launcher file name, to open an installed app
    pub desktop_id: String,
    pub release_version: String,
    pub release_notes: String,
    /// ISO date, empty when unknown
    pub release_date: String,
    /// After the latest, newest first
    pub older_releases: Vec<Release>,
    pub origin: MetadataOrigin,
    /// Theme icon name or file, set for installed apps outside the catalogue, which have no icon file to point to
    pub icon: String,
}

/// An installed or found app as the Installed and AUR search lists show it.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct App {
    pub id: String,
    pub name: String,
    pub summary: String,
    pub version: String,
    pub source: String,
    pub remote: String,
    pub scope: String,
    pub branch: String,
    pub base: String,
    pub path: String,
    pub homepage: String,
    pub launcher: String,
    /// Theme icon name or icon file
    pub icon: String,
    pub screenshot: String,
}

/// Popularity signals from Flathub and pkgstats; empty when never downloaded.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Rankings {
    /// Flathub app IDs, best first
    pub popular: Vec<String>,
    pub trending: Vec<String>,
    /// Flathub app IDs, newest first
    pub added: Vec<String>,
    pub updated: Vec<String>,
    /// Flathub app ID -> installs last month
    pub flathub_installs: HashMap<String, f64>,
    /// pkgname -> % of pkgstats submitters
    pub arch_popularity: HashMap<String, f64>,
    /// Shown from the saved copy
    pub offline: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct RepoPackage {
    pub name: String,
    pub repo: String,
    pub version: String,
    pub description: String,
    pub url: String,
}

/// Packages with zero votes keep only name and version: exact-name search still finds them.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct AurEntry {
    pub name: String,
    pub version: String,
    pub description: String,
    pub url: String,
    pub votes: i32,
    pub popularity: f64,
}

impl AurEntry {
    pub fn detailed(&self) -> bool {
        self.votes > 0
    }
}

/// What people say about an app, from sources that publish it. `None` / 0 means "not available": the row is hidden.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Ratings {
    /// 1-5, only from ODRS reviews
    pub stars: f64,
    pub reviews: i32,
    /// Reviews with 1..5 stars
    pub histogram: [i32; 5],
    /// Flathub
    pub installs: Option<i64>,
    pub installs_month: Option<i64>,
    /// Flathub installs per day, oldest first
    pub daily: Vec<(String, i32)>,
    pub aur_votes: Option<i32>,
    pub aur_popularity: f64,
    /// pkgstats: share of reporting systems
    pub repo_percent: Option<f64>,
}

impl Ratings {
    pub fn any(&self) -> bool {
        self.reviews > 0 || self.installs.is_some() || self.aur_votes.is_some() || self.repo_percent.is_some()
    }
}

// JSON read the way Qt's QJsonValue reads it: a missing or wrong-typed value is 0 or "".
pub(crate) fn json_object(json: &[u8]) -> Map<String, Value> {
    match serde_json::from_slice(json) {
        Ok(Value::Object(object)) => object,
        _ => Map::new(),
    }
}

pub(crate) fn json_str(value: Option<&Value>) -> String {
    value.and_then(Value::as_str).unwrap_or("").to_string()
}

pub(crate) fn json_f64(value: Option<&Value>) -> f64 {
    value.and_then(Value::as_f64).unwrap_or(0.0)
}

/// Like QJsonValue::toInt: only whole numbers in range count.
pub(crate) fn json_i32(value: Option<&Value>) -> i32 {
    match value.and_then(Value::as_f64) {
        Some(n) if n.fract() == 0.0 && n >= f64::from(i32::MIN) && n <= f64::from(i32::MAX) => n as i32,
        _ => 0,
    }
}

fn json_array(value: Option<&Value>) -> &[Value] {
    value.and_then(Value::as_array).map_or(&[], Vec::as_slice)
}

/// Qt's QChar::isLetterOrNumber.
pub(crate) fn letter_or_number(c: char) -> bool {
    c.is_alphanumeric()
}

/// Simple one-to-one case folding, as Qt::CaseInsensitive compares.
pub(crate) fn fold(c: char) -> char {
    let mut lower = c.to_lowercase();
    match (lower.next(), lower.next()) {
        (Some(l), None) => l,
        _ => c,
    }
}

fn folded(text: &str) -> Vec<char> {
    text.chars().map(fold).collect()
}

/// Every position where needle starts in text, ignoring case.
fn match_positions(text: &[char], needle: &[char]) -> Vec<usize> {
    if text.len() < needle.len() {
        return Vec::new();
    }
    (0..=text.len() - needle.len()).filter(|&at| text[at..at + needle.len()] == *needle).collect()
}

pub fn normalize_component_id(id: &str) -> String {
    let id = id.to_lowercase();
    id.strip_suffix(".desktop").map(str::to_string).unwrap_or(id)
}

pub fn is_library_name(name: &str) -> bool {
    static PREFIX: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(
            r"^(python2?|perl|ruby|nodejs|r|haskell|lib32|lua[0-9]*|go|rust|php|ocaml|texlive|ttf|otf|fonts?|mingw-w64|android|java|vim|emacs|kodi-addon)-",
        )
        .unwrap()
    });
    PREFIX.is_match(name)
}

/// expac lines "repo\tname\tversion\tdescription[\turl]"; the first repo in pacman.conf order wins, as in pacman.
pub fn parse_sync_packages(expac_output: &str, repo_order: &[&str]) -> Vec<RepoPackage> {
    // Repos missing from the order list rank last.
    let rank = |repo: &str| repo_order.iter().position(|r| *r == repo).unwrap_or(repo_order.len());
    let mut best: Vec<RepoPackage> = Vec::new();
    let mut index: HashMap<String, usize> = HashMap::new();
    for line in expac_output.split('\n').filter(|l| !l.is_empty()) {
        let f: Vec<&str> = line.split('\t').collect();
        if f.len() < 4 || !valid_package_name(f[1]) {
            continue;
        }
        let package =
            RepoPackage { name: f[1].into(), repo: f[0].into(), version: f[2].into(), description: f[3].into(), url: f.get(4).unwrap_or(&"").to_string() };
        match index.get(&package.name) {
            Some(&at) if rank(&package.repo) < rank(&best[at].repo) => best[at] = package,
            Some(_) => {}
            None => {
                index.insert(package.name.clone(), best.len());
                best.push(package);
            }
        }
    }
    best
}

/// The first gzip member of data; `None` when it is not complete gzip.
pub fn gunzip(data: &[u8]) -> Option<Vec<u8>> {
    if data.is_empty() {
        return None;
    }
    let mut out = Vec::new();
    flate2::read::GzDecoder::new(data).read_to_end(&mut out).ok()?;
    Some(out)
}

/// The AUR's packages-meta-v1 dump; `None` when it is not a JSON array.
pub fn parse_aur_dump(json: &[u8]) -> Option<Vec<AurEntry>> {
    let Ok(Value::Array(items)) = serde_json::from_slice::<Value>(json) else { return None };
    let mut entries = Vec::with_capacity(items.len());
    for item in &items {
        let item = item.as_object();
        let get = |key: &str| item.and_then(|o| o.get(key));
        let name = json_str(get("Name"));
        if !valid_package_name(&name) {
            continue;
        }
        let mut entry = AurEntry { name, version: json_str(get("Version")), votes: json_i32(get("NumVotes")), ..Default::default() };
        if entry.detailed() {
            entry.description = json_str(get("Description"));
            entry.url = json_str(get("URL"));
            entry.popularity = json_f64(get("Popularity"));
        }
        entries.push(entry);
    }
    Some(entries)
}

const CACHE_MAGIC: &[u8] = b"WOOFAUR";
const CACHE_VERSION: i32 = 1;

// The cache uses Qt's QDataStream layout (big endian, strings as UTF-16), so 1.x and 2.x read each other's file.
fn put_bytes(out: &mut impl Write, bytes: &[u8]) -> std::io::Result<()> {
    out.write_all(&(bytes.len() as u32).to_be_bytes())?;
    out.write_all(bytes)
}

fn put_string(out: &mut impl Write, text: &str) -> std::io::Result<()> {
    let utf16: Vec<u8> = text.encode_utf16().flat_map(u16::to_be_bytes).collect();
    put_bytes(out, &utf16)
}

pub fn write_aur_cache(path: &Path, entries: &[AurEntry]) -> bool {
    let temporary = path.with_file_name(format!(".{}.part", path.file_name().map(|n| n.to_string_lossy()).unwrap_or_default()));
    let written = (|| -> std::io::Result<()> {
        let mut out = BufWriter::new(File::create(&temporary)?);
        put_bytes(&mut out, CACHE_MAGIC)?;
        out.write_all(&CACHE_VERSION.to_be_bytes())?;
        out.write_all(&(entries.len() as i64).to_be_bytes())?;
        for e in entries {
            for text in [&e.name, &e.version, &e.description, &e.url] {
                put_string(&mut out, text)?;
            }
            out.write_all(&e.votes.to_be_bytes())?;
            out.write_all(&e.popularity.to_be_bytes())?;
        }
        out.into_inner().map_err(|e| e.into_error())?.sync_all()?;
        std::fs::rename(&temporary, path)
    })();
    if written.is_err() {
        let _ = std::fs::remove_file(&temporary);
    }
    written.is_ok()
}

/// `None` when the file is missing, damaged or from another format version: the cache is then rebuilt from the dump.
pub fn read_aur_cache(path: &Path) -> Option<Vec<AurEntry>> {
    let mut input = BufReader::new(File::open(path).ok()?);
    let mut take = |size: usize| -> Option<Vec<u8>> {
        let mut bytes = vec![0u8; size];
        input.read_exact(&mut bytes).ok()?;
        Some(bytes)
    };
    fn array<const N: usize>(bytes: Vec<u8>) -> [u8; N] {
        bytes.try_into().expect("read with this size")
    }
    let bytes = |take: &mut dyn FnMut(usize) -> Option<Vec<u8>>| -> Option<Vec<u8>> {
        match u32::from_be_bytes(array(take(4)?)) {
            u32::MAX => Some(Vec::new()), // a null QByteArray or QString
            size if size > 1 << 30 => None,
            size => take(size as usize),
        }
    };
    let magic = bytes(&mut take)?;
    let version = i32::from_be_bytes(array(take(4)?));
    let count = i64::from_be_bytes(array(take(8)?));
    if magic != CACHE_MAGIC || version != CACHE_VERSION || !(0..=10_000_000).contains(&count) {
        return None;
    }
    let mut entries = Vec::with_capacity(count as usize);
    for _ in 0..count {
        let mut string = || -> Option<String> {
            let raw = bytes(&mut take)?;
            if raw.len() % 2 != 0 {
                return None;
            }
            Some(String::from_utf16_lossy(&raw.chunks(2).map(|c| u16::from_be_bytes([c[0], c[1]])).collect::<Vec<_>>()))
        };
        let (name, version, description, url) = (string()?, string()?, string()?, string()?);
        let votes = i32::from_be_bytes(array(take(4)?));
        let popularity = f64::from_be_bytes(array(take(8)?));
        entries.push(AurEntry { name, version, description, url, votes, popularity });
    }
    Some(entries)
}

/// Flathub /collection/popular, /collection/trending, /collection/recently-added and /collection/recently-updated, and
/// pkgstats /api/packages. Bad input yields empty parts.
pub fn parse_rankings(popular: &[u8], trending: &[u8], pkgstats: &[u8], added: &[u8], updated: &[u8]) -> Rankings {
    let mut rankings = Rankings::default();
    let hits = |json: &[u8]| json_array(json_object(json).get("hits")).to_vec();
    let ids = |json: &[u8]| -> Vec<String> { hits(json).iter().map(|hit| json_str(hit.get("app_id"))).filter(|id| !id.is_empty()).collect() };
    rankings.added = ids(added);
    rankings.updated = ids(updated);
    for hit in hits(popular) {
        let id = json_str(hit.get("app_id"));
        if id.is_empty() {
            continue;
        }
        rankings.popular.push(id.clone());
        rankings.flathub_installs.insert(id, json_f64(hit.get("installs_last_month")));
    }
    rankings.trending = ids(trending);
    for package in json_array(json_object(pkgstats).get("packagePopularities")) {
        rankings.arch_popularity.insert(json_str(package.get("name")), json_f64(package.get("popularity")));
    }
    rankings
}

/// One comparable popularity score per entry, about 0..1, from each source's own signal.
pub fn app_score(app: &CatalogApp, rankings: &Rankings) -> f64 {
    // ponytail: hand-tuned scales (1M Flathub installs = 100 % of Arch users = 1); replace with a learned blend if rankings look off.
    let mut score: f64 = 0.0;
    for package in &app.packages {
        let value = |map: &HashMap<String, f64>| map.get(&package.name).copied().unwrap_or(0.0);
        score = score.max(match package.source {
            Source::Flatpak => (1.0 + value(&rankings.flathub_installs)).log10() / 6.0,
            // pkgstats also counts packages pulled in as dependencies; their helper apps ship no screenshots.
            Source::Repo => value(&rankings.arch_popularity) / 100.0 * if app.screenshots.is_empty() { 0.3 } else { 1.0 },
            Source::Aur => package.popularity / (package.popularity + 1.0) / 2.0,
        });
    }
    score + if app.origin == MetadataOrigin::None { 0.0 } else { 0.05 }
}

/// The Explore category group for AppStream categories, or empty.
pub fn category_group(categories: &[&str]) -> &'static str {
    const GROUPS: [(&str, &[&str]); 9] = [
        ("Games", &["Game"]),
        ("Graphics & Photography", &["Graphics"]),
        ("Audio & Video", &["AudioVideo", "Audio", "Video"]),
        ("Developer Tools", &["Development"]),
        ("Education & Science", &["Education", "Science"]),
        ("Networking", &["Network"]),
        ("Productivity", &["Office"]),
        ("System", &["System", "Settings"]),
        ("Utilities", &["Utility"]),
    ];
    GROUPS.iter().find(|(_, ids)| ids.iter().any(|id| categories.contains(id))).map_or("", |(group, _)| group)
}

/// True when a word in text starts with needle: "spo" finds "Spotify client", not "transport".
fn word_start_match(text: &str, needle: &[char]) -> bool {
    let text = folded(text);
    match_positions(&text, needle).into_iter().any(|at| at == 0 || !letter_or_number(text[at - 1]))
}

/// Indexes of entries matching query, best first: exact name, name prefix, name or package, then a word in keywords or
/// summary. Within a tier the higher score comes first.
pub fn rank_search(apps: &[CatalogApp], query: &str, scores: &[f64]) -> Vec<usize> {
    let needle = folded(query.trim());
    if needle.is_empty() {
        return Vec::new();
    }
    let contains = |text: &str| !match_positions(&folded(text), &needle).is_empty();
    let mut hits: Vec<(u8, usize)> = Vec::new();
    for (i, app) in apps.iter().enumerate() {
        let name = folded(&app.name);
        let tier = if name == needle {
            Some(0)
        } else if name.starts_with(&needle) {
            Some(1)
        } else if contains(&app.name) || app.packages.iter().any(|p| contains(&p.name)) {
            Some(2)
        } else if app.keywords.iter().any(|k| word_start_match(k, &needle)) {
            Some(3)
        } else if word_start_match(&app.summary, &needle) {
            Some(4)
        } else {
            None
        };
        if let Some(tier) = tier {
            hits.push((tier, i));
        }
    }
    let score = |i: usize| scores.get(i).copied().unwrap_or(0.0);
    hits.sort_by(|a, b| a.0.cmp(&b.0).then_with(|| score(b.1).partial_cmp(&score(a.1)).unwrap_or(std::cmp::Ordering::Equal)));
    hits.into_iter().map(|(_, i)| i).collect()
}

/// ODRS: {"star1": n, ... "star5": n, "total": n}. Fewer than `min_reviews` reviews say nothing, so they give no stars.
pub fn parse_odrs(json: &[u8], out: &mut Ratings, min_reviews: i32) {
    let o = json_object(json);
    let total = json_i32(o.get("total"));
    if total < min_reviews {
        return;
    }
    for (i, stars) in out.histogram.iter_mut().enumerate() {
        *stars = json_i32(o.get(&format!("star{}", i + 1)));
    }
    let sum: f64 = out.histogram.iter().enumerate().map(|(i, &n)| (i as f64 + 1.0) * f64::from(n)).sum();
    out.stars = sum / f64::from(total);
    out.reviews = total;
}

/// Flathub /api/v2/stats/<id>
pub fn parse_flathub_stats(json: &[u8], out: &mut Ratings) {
    let o = json_object(json);
    if !o.contains_key("installs_total") {
        return;
    }
    if let Some(days) = o.get("installs_per_day").and_then(Value::as_object) {
        let mut daily: Vec<(String, i32)> = days.iter().map(|(day, n)| (day.clone(), json_i32(Some(n)))).collect();
        daily.sort();
        out.daily.extend(daily);
    }
    out.installs = Some(json_f64(o.get("installs_total")) as i64);
    out.installs_month = Some(json_f64(o.get("installs_last_month")) as i64);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn app_stream(id: &str, name: &str, homepage: &str, package: Package) -> CatalogApp {
        CatalogApp {
            key: id.into(),
            name: name.into(),
            homepage: homepage.into(),
            packages: vec![package],
            origin: MetadataOrigin::AppStream,
            ..Default::default()
        }
    }

    fn aur_app(name: &str, homepage: &str) -> CatalogApp {
        CatalogApp {
            key: format!("aur:{name}"),
            name: name.into(),
            homepage: homepage.into(),
            packages: vec![Package::new(Source::Aur, name, "aur", "1")],
            ..Default::default()
        }
    }

    #[test]
    fn sync_packages_follow_pacman_conf_order() {
        let repo = parse_sync_packages(
            "extra\tkrita\t6.0-1\tPaint\ncachyos-extra-v3\tkrita\t6.0-1.1\tPaint\nextra\tbad;name\t1\tx\ncore\tbash\t5-1\tShell\thttps://www.gnu.org/software/bash/\n",
            &["cachyos-extra-v3", "core", "extra"],
        );
        assert_eq!(repo.len(), 2);
        let krita = repo.iter().find(|p| p.name == "krita").unwrap();
        assert_eq!((krita.repo.as_str(), krita.version.as_str(), krita.url.as_str()), ("cachyos-extra-v3", "6.0-1.1", ""));
        assert_eq!(repo.iter().find(|p| p.name == "bash").unwrap().url, "https://www.gnu.org/software/bash/");
        assert_eq!(normalize_component_id("org.KDE.krita.desktop"), "org.kde.krita");
        assert!(is_library_name("python-requests") && is_library_name("ttf-dejavu") && !is_library_name("pythonista") && !is_library_name("krita"));
    }

    #[test]
    fn aur_dump_and_cache() {
        // Tiny gzip of a three-package AUR dump: one voted, one with zero votes, one invalid name.
        let dump: &[u8] = b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\x5d\x8d\x3d\x0b\xc2\x30\x10\x86\xff\xcb\xcd\x49\x3f\x14\x07\x33\x3b\x4a\x11\xc1\x2e\xe2\x10\xcd\x49\x03\x4d\x13\x93\x0b\x55\xa4\xff\xdd\x03\x41\x6c\xb7\xf7\xeb\xee\x39\xbf\xa1\xd1\x0e\x41\xc1\xe8\xfd\x1d\x04\xb4\x18\x93\xf5\x03\x07\xb5\xac\xd9\x37\xd9\xb5\x9e\x30\x81\x5a\x0b\xd8\x61\xba\x45\x1b\xe8\x3b\x30\x5c\x9f\x8e\x7b\x56\x1d\x51\x48\xaa\x2c\xc7\x02\x9f\xda\x85\x1e\xb9\x39\xf8\x90\x7b\x1d\x2d\xbd\x40\x55\xc5\x66\x12\x3f\xd2\x23\x5b\xa4\x19\x6a\xb5\x40\x55\x4b\x54\x67\x8d\xc1\x01\xfe\x9e\x48\x79\xd5\x66\x76\xb4\x9d\x2e\x1f\xb6\x44\x99\xa9\xce\x00\x00\x00";
        assert_eq!(dump.len(), 151);
        let json = gunzip(dump).unwrap();
        assert_eq!(gunzip(b"not gzip"), None);
        let aur = parse_aur_dump(&json).unwrap();
        assert_eq!(aur.len(), 2);
        assert!(aur[0].name == "woof" && aur[0].detailed() && aur[0].url == "https://w.example" && aur[0].popularity == 0.5);
        assert!(aur[1].name == "quiet" && !aur[1].detailed() && aur[1].description.is_empty() && aur[1].version == "2-1");
        assert_eq!(parse_aur_dump(b"{}"), None);

        let dir = tempfile::tempdir().unwrap();
        let cache = dir.path().join("aur.bin");
        assert!(write_aur_cache(&cache, &aur));
        let reloaded = read_aur_cache(&cache).unwrap();
        assert!(reloaded.len() == 2 && reloaded[0].url == aur[0].url && reloaded[1].votes == 0);
        assert_eq!(reloaded, aur);
        std::fs::write(&cache, "old format").unwrap();
        assert_eq!(read_aur_cache(&cache), None);
    }

    #[test]
    fn aur_cache_is_a_qdatastream() {
        // The bytes QDataStream writes for one entry (name "a", null description), as 1.x saves them.
        let mut qt = Vec::new();
        qt.extend(7u32.to_be_bytes());
        qt.extend(b"WOOFAUR");
        qt.extend(1i32.to_be_bytes());
        qt.extend(1i64.to_be_bytes());
        qt.extend(2u32.to_be_bytes());
        qt.extend([0, b'a']);
        qt.extend(2u32.to_be_bytes());
        qt.extend([0, b'1']);
        qt.extend(u32::MAX.to_be_bytes());
        qt.extend(0u32.to_be_bytes());
        qt.extend(3i32.to_be_bytes());
        qt.extend(0.25f64.to_be_bytes());
        let dir = tempfile::tempdir().unwrap();
        let cache = dir.path().join("aur.bin");
        std::fs::write(&cache, &qt).unwrap();
        let entries = read_aur_cache(&cache).unwrap();
        assert_eq!(entries, vec![AurEntry { name: "a".into(), version: "1".into(), votes: 3, popularity: 0.25, ..Default::default() }]);
        std::fs::write(&cache, &qt[..qt.len() - 1]).unwrap();
        assert_eq!(read_aur_cache(&cache), None); // cut short
    }

    #[test]
    fn rankings_and_search() {
        let ranked = parse_rankings(
            br#"{"hits":[{"app_id":"org.kde.krita","installs_last_month":1000}]}"#,
            br#"{"hits":[{"app_id":"org.gimp.GIMP"}]}"#,
            br#"{"packagePopularities":[{"name":"firefox","popularity":60}]}"#,
            b"",
            b"",
        );
        assert_eq!(ranked.popular, ["org.kde.krita"]);
        assert_eq!(ranked.trending, ["org.gimp.GIMP"]);
        assert_eq!(ranked.arch_popularity["firefox"], 60.0);
        assert!(parse_rankings(b"offline", b"", b"", b"", b"").popular.is_empty());
        assert_eq!(category_group(&["Graphics", "Game"]), "Games");
        assert_eq!(category_group(&["Settings"]), "System");
        assert_eq!(category_group(&["X-Unknown"]), "");

        let mut firefox = app_stream("org.mozilla.firefox", "Firefox", "", Package::new(Source::Repo, "firefox", "extra", "1"));
        firefox.summary = "Web browser".into();
        firefox.keywords = vec!["internet".into()];
        let mut fox = aur_app("foxit", "");
        fox.summary = "PDF reader".into();
        let mut browser = aur_app("lynx", "");
        browser.summary = "Text web browser".into();
        let scores = [app_score(&browser, &ranked), app_score(&fox, &ranked), app_score(&firefox, &ranked)];
        let searchable = [browser, fox, firefox];
        assert!(scores[2] > scores[0]); // 60 % of Arch users beats an unvoted AUR package
        assert_eq!(rank_search(&searchable, "fox", &scores), [1, 2]); // name prefix before name contains
        assert!(rank_search(&searchable, "rowser", &scores).is_empty()); // summaries match at word starts only
        assert_eq!(rank_search(&searchable, "browser", &scores), [2, 0]); // summary matches, more popular first
        assert_eq!(rank_search(&searchable, "internet", &scores), [2]);
        assert!(rank_search(&searchable, "  ", &scores).is_empty());
    }

    #[test]
    fn ratings() {
        let mut rated = Ratings::default();
        parse_odrs(br#"{"star0":0,"star1":0,"star2":0,"star3":0,"star4":5,"star5":5,"total":10}"#, &mut rated, 5);
        assert!(rated.reviews == 10 && (rated.stars - 4.5).abs() < 0.001);
        let mut few = Ratings::default();
        parse_odrs(br#"{"star5":2,"total":2}"#, &mut few, 5); // Too few reviews: no stars
        assert!(few.reviews == 0 && !few.any());
        parse_flathub_stats(br#"{"installs_total":1781622,"installs_last_month":35610,"installs_per_day":{"2026-07-02":5,"2026-07-01":3}}"#, &mut few);
        assert!(few.installs == Some(1781622) && few.any() && few.daily.len() == 2 && few.daily[0].0 == "2026-07-01");
        let recent = parse_rankings(b"", b"", b"", br#"{"hits":[{"app_id":"a.b.New"}]}"#, br#"{"hits":[{"app_id":"a.b.Fresh"},{"app_id":"c.d.Fresh"}]}"#);
        assert_eq!(recent.added, ["a.b.New"]);
        assert_eq!(recent.updated.len(), 2);
    }
}
