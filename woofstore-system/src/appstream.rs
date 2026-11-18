//! AppStream catalogues: the descriptions, icons and screenshots of apps, published by Arch (`archlinux-appstream-data`),
//! by every Flatpak remote, and by WoofStore itself for apps nobody else describes.
//!
//! The files are read as a stream: Flathub's catalogue is about 50 MB of XML, and a tree of it would need several
//! hundred megabytes.

use flate2::read::GzDecoder;
use quick_xml::events::{BytesStart, Event};
use quick_xml::Reader;
use std::fs;
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Icon {
    /// "cached", "stock" or "remote"
    pub kind: String,
    pub width: u32,
    /// File name for cached icons, theme name for stock icons, address for remote icons.
    pub value: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Image {
    /// "source" or "thumbnail"
    pub kind: String,
    pub width: u32,
    pub height: u32,
    pub url: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Release {
    pub version: String,
    /// Seconds since 1970; 0 when the release has no date.
    pub timestamp: i64,
    /// AppStream markup (`<p>`, `<ul>`, `<li>`, `<em>`, `<code>`).
    pub description: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Component {
    pub id: String,
    /// The catalogue it comes from: "archlinux-arch-extra", a Flatpak remote such as "flathub", or "woofstore".
    pub origin: String,
    pub name: String,
    pub summary: String,
    /// AppStream markup.
    pub description: String,
    pub pkgnames: Vec<String>,
    /// "app/org.kde.krita/x86_64/stable"
    pub flatpak_bundle: Option<String>,
    /// (kind, address): homepage, bugtracker, help, donation, translate, vcs-browser, contribute, faq
    pub urls: Vec<(String, String)>,
    pub categories: Vec<String>,
    pub keywords: Vec<String>,
    pub desktop_id: Option<String>,
    /// Newest first.
    pub releases: Vec<Release>,
    pub icons: Vec<Icon>,
    /// One list of images per screenshot, the default screenshot first.
    pub screenshots: Vec<Vec<Image>>,
    /// The folder holding this catalogue's cached icons by size ("64x64/name.png").
    pub icon_root: Option<PathBuf>,
}

impl Component {
    /// The cached icon file, the 64-pixel one or larger when there is one.
    pub fn cached_icon(&self) -> Option<PathBuf> {
        let root = self.icon_root.as_ref()?;
        let mut found = None;
        for icon in self.icons.iter().filter(|icon| icon.kind == "cached" && !icon.value.contains('/')) {
            let path = root.join(format!("{0}x{0}", icon.width)).join(&icon.value);
            if path.exists() {
                found = Some(path);
                if icon.width >= 64 {
                    break;
                }
            }
        }
        found
    }

    /// Up to `limit` https screenshot addresses, each the image closest to 900 × 500.
    pub fn screenshot_urls(&self, limit: usize) -> Vec<String> {
        self.screenshots
            .iter()
            .filter_map(|images| {
                images.iter().filter(|image| image.url.starts_with("https://")).min_by_key(|image| {
                    if image.width == 0 {
                        u32::MAX - 1
                    } else {
                        image.width.abs_diff(900) + image.height.abs_diff(500)
                    }
                })
            })
            .take(limit)
            .map(|image| image.url.clone())
            .collect()
    }

    /// Splits the Flatpak bundle "app/org.kde.krita/x86_64/stable" into ID and branch.
    pub fn flatpak_ref(&self) -> Option<(String, String)> {
        let parts: Vec<&str> = self.flatpak_bundle.as_deref()?.split('/').collect();
        let (id, branch) = if parts.first() == Some(&"app") { (parts.get(1), parts.get(3)) } else { (parts.first(), parts.get(2)) };
        Some((id?.to_string(), branch.copied().unwrap_or("").to_string())).filter(|(id, _)| !id.is_empty())
    }
}

/// How well an `xml:lang` value fits the user's language: "it_IT" for the locale "it_IT.UTF-8" beats "it", which
/// beats no language at all; other languages are skipped.
#[derive(Debug, Clone)]
pub struct Language {
    full: String,
    short: String,
}

impl Language {
    pub fn new(locale: &str) -> Self {
        let full = locale.split(['.', '@']).next().unwrap_or("").to_string();
        let short = full.split('_').next().unwrap_or("").to_string();
        Self { full, short }
    }

    pub fn from_environment() -> Self {
        let locale = ["LC_ALL", "LC_MESSAGES", "LANG"].iter().find_map(|v| std::env::var(v).ok().filter(|s| !s.is_empty())).unwrap_or_default();
        Self::new(&locale)
    }

    /// "it_IT", the form desktop files use for translated names.
    pub fn code(&self) -> &str {
        &self.full
    }

    /// 0 = not this language; otherwise higher is better.
    fn score(&self, lang: Option<&str>) -> u8 {
        match lang {
            None | Some("") => 1,
            Some(l) if !self.full.is_empty() && l == self.full => 3,
            Some(l) if !self.short.is_empty() && l == self.short => 2,
            _ => 0,
        }
    }
}

fn attribute(element: &BytesStart, name: &[u8]) -> Option<String> {
    element.attributes().flatten().find(|a| a.key.as_ref() == name).and_then(|a| a.unescape_value().ok()).map(|v| v.into_owned())
}

/// A localised value: the best-scoring candidate wins, the first one on a tie.
#[derive(Default)]
struct Localised {
    value: String,
    score: u8,
}

impl Localised {
    fn offer(&mut self, score: u8, value: String) {
        if score > self.score {
            self.score = score;
            self.value = value;
        }
    }
}

/// Reads a catalogue file, gzipped or not. `icon_root` is where its cached icons are.
pub fn read_catalogue(path: &Path, icon_root: Option<PathBuf>, language: &Language) -> Result<Vec<Component>, String> {
    let file = fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let reader: Box<dyn Read> = if path.extension().is_some_and(|e| e == "gz") { Box::new(GzDecoder::new(file)) } else { Box::new(file) };
    parse_catalogue(BufReader::new(reader), icon_root, language).map_err(|e| format!("{}: {e}", path.display()))
}

/// Writes the XML of an element's children back as text: the markup of descriptions.
fn inner_markup<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>, language: &Language) -> Result<String, quick_xml::Error> {
    let mut markup = String::new();
    let mut depth = 0usize;
    let mut skip_depth: Option<usize> = None; // Inside a paragraph in another language
    loop {
        buf.clear();
        match reader.read_event_into(buf)? {
            Event::Start(e) => {
                depth += 1;
                if skip_depth.is_none() && language.score(attribute(&e, b"xml:lang").as_deref()) == 0 {
                    skip_depth = Some(depth);
                }
                if skip_depth.is_none() {
                    markup.push('<');
                    markup.push_str(&String::from_utf8_lossy(e.name().as_ref()));
                    markup.push('>');
                }
            }
            Event::Empty(e) => {
                if skip_depth.is_none() {
                    markup.push_str(&format!("<{}/>", String::from_utf8_lossy(e.name().as_ref())));
                }
            }
            Event::End(e) => {
                if depth == 0 {
                    return Ok(markup.trim().to_string());
                }
                if skip_depth.is_none() {
                    markup.push_str(&format!("</{}>", String::from_utf8_lossy(e.name().as_ref())));
                }
                if skip_depth == Some(depth) {
                    skip_depth = None;
                }
                depth -= 1;
            }
            Event::Text(t) => {
                if skip_depth.is_none() {
                    // Kept escaped: the result is markup
                    let raw = String::from_utf8_lossy(t.as_ref()).into_owned();
                    markup.push_str(&raw.split_whitespace().collect::<Vec<_>>().join(" "));
                    if raw.ends_with(char::is_whitespace) && !raw.trim().is_empty() {
                        markup.push(' ');
                    }
                }
            }
            Event::CData(t) => {
                if skip_depth.is_none() {
                    markup.push_str(&quick_xml::escape::escape(String::from_utf8_lossy(t.as_ref()).as_ref()));
                }
            }
            Event::Eof => return Ok(markup.trim().to_string()),
            _ => {}
        }
    }
}

fn text_of<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>) -> Result<String, quick_xml::Error> {
    let mut text = String::new();
    let mut depth = 0usize;
    loop {
        buf.clear();
        match reader.read_event_into(buf)? {
            Event::Start(_) => depth += 1,
            Event::End(_) if depth == 0 => return Ok(text.trim().to_string()),
            Event::End(_) => depth -= 1,
            Event::Text(t) => text.push_str(&t.unescape().map(|t| t.into_owned()).unwrap_or_default()),
            Event::CData(t) => text.push_str(&String::from_utf8_lossy(t.as_ref())),
            Event::Eof => return Ok(text.trim().to_string()),
            _ => {}
        }
    }
}

/// Skips the rest of the element whose start was just read.
fn skip<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>) -> Result<(), quick_xml::Error> {
    let mut depth = 0usize;
    loop {
        buf.clear();
        match reader.read_event_into(buf)? {
            Event::Start(_) => depth += 1,
            Event::End(_) if depth == 0 => return Ok(()),
            Event::End(_) => depth -= 1,
            Event::Eof => return Ok(()),
            _ => {}
        }
    }
}

pub fn parse_catalogue<R: BufRead>(input: R, icon_root: Option<PathBuf>, language: &Language) -> Result<Vec<Component>, quick_xml::Error> {
    let mut reader = Reader::from_reader(input);
    let mut buf = Vec::new();
    let mut origin = String::new();
    let mut components = Vec::new();
    loop {
        buf.clear();
        let event = reader.read_event_into(&mut buf)?;
        match event {
            Event::Start(e) if e.name().as_ref() == b"components" => origin = attribute(&e, b"origin").unwrap_or_default(),
            Event::Start(e) if e.name().as_ref() == b"component" => {
                let kind = attribute(&e, b"type").unwrap_or_default();
                let mut scratch = Vec::new();
                let component = parse_component(&mut reader, &mut scratch, language)?;
                if kind == "desktop-application" || kind == "desktop" {
                    components.push(Component { origin: origin.clone(), icon_root: icon_root.clone(), ..component });
                }
            }
            Event::Eof => break,
            _ => {}
        }
    }
    Ok(components)
}

fn parse_component<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>, language: &Language) -> Result<Component, quick_xml::Error> {
    let mut c = Component::default();
    let (mut name, mut summary, mut description) = (Localised::default(), Localised::default(), Localised::default());
    let mut keywords: Vec<(u8, String)> = Vec::new();
    loop {
        buf.clear();
        let event = reader.read_event_into(buf)?.into_owned();
        let e = match event {
            Event::Start(e) => e,
            Event::End(_) | Event::Eof => break,
            _ => continue,
        };
        let score = language.score(attribute(&e, b"xml:lang").as_deref());
        match e.name().as_ref() {
            b"id" => c.id = text_of(reader, buf)?,
            b"name" => {
                let value = text_of(reader, buf)?;
                name.offer(score, value);
            }
            b"summary" => {
                let value = text_of(reader, buf)?;
                summary.offer(score, value);
            }
            b"description" => {
                let value = inner_markup(reader, buf, language)?;
                description.offer(score, value);
            }
            b"pkgname" => c.pkgnames.push(text_of(reader, buf)?),
            b"bundle" => {
                let kind = attribute(&e, b"type");
                let value = text_of(reader, buf)?;
                if kind.as_deref() == Some("flatpak") {
                    c.flatpak_bundle = Some(value);
                }
            }
            b"url" => {
                let kind = attribute(&e, b"type").unwrap_or_default();
                c.urls.push((kind, text_of(reader, buf)?));
            }
            b"launchable" => {
                let kind = attribute(&e, b"type");
                let value = text_of(reader, buf)?;
                if kind.as_deref() == Some("desktop-id") && c.desktop_id.is_none() {
                    c.desktop_id = Some(value);
                }
            }
            b"icon" => {
                let kind = attribute(&e, b"type").unwrap_or_default();
                let width = attribute(&e, b"width").and_then(|w| w.parse().ok()).unwrap_or(0);
                c.icons.push(Icon { kind, width, value: text_of(reader, buf)? });
            }
            b"categories" => c.categories = list(reader, buf, b"category", language)?.into_iter().map(|(_, v)| v).collect(),
            b"keywords" => keywords.extend(list(reader, buf, b"keyword", language)?),
            b"releases" => c.releases = releases(reader, buf, language)?,
            b"screenshots" => c.screenshots = screenshots(reader, buf)?,
            _ => skip(reader, buf)?,
        }
    }
    c.name = name.value;
    c.summary = summary.value;
    c.description = description.value;
    // Keywords in the user's language when there are any, otherwise the untranslated ones
    let best = keywords.iter().map(|(score, _)| *score).max().unwrap_or(0);
    c.keywords = keywords.into_iter().filter(|(score, _)| *score == best && best > 0).map(|(_, k)| k).collect();
    c.releases.sort_by_key(|r| std::cmp::Reverse(r.timestamp));
    Ok(c)
}

/// The children named `item` of a list element, with their language score.
fn list<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>, item: &[u8], language: &Language) -> Result<Vec<(u8, String)>, quick_xml::Error> {
    let mut values = Vec::new();
    loop {
        buf.clear();
        match reader.read_event_into(buf)?.into_owned() {
            Event::Start(e) if e.name().as_ref() == item => {
                let score = language.score(attribute(&e, b"xml:lang").as_deref());
                let value = text_of(reader, buf)?;
                if score > 0 {
                    values.push((score, value));
                }
            }
            Event::Start(_) => skip(reader, buf)?,
            Event::End(_) | Event::Eof => return Ok(values),
            _ => {}
        }
    }
}

fn releases<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>, language: &Language) -> Result<Vec<Release>, quick_xml::Error> {
    let mut found = Vec::new();
    loop {
        buf.clear();
        match reader.read_event_into(buf)?.into_owned() {
            Event::Start(e) if e.name().as_ref() == b"release" => {
                let mut release = Release {
                    version: attribute(&e, b"version").unwrap_or_default(),
                    timestamp: attribute(&e, b"timestamp")
                        .and_then(|t| t.parse().ok())
                        .or_else(|| attribute(&e, b"date").and_then(|d| date_seconds(&d)))
                        .unwrap_or(0),
                    description: String::new(),
                };
                let mut notes = Localised::default();
                loop {
                    buf.clear();
                    match reader.read_event_into(buf)?.into_owned() {
                        Event::Start(d) if d.name().as_ref() == b"description" => {
                            let score = language.score(attribute(&d, b"xml:lang").as_deref());
                            let value = inner_markup(reader, buf, language)?;
                            notes.offer(score, value);
                        }
                        Event::Start(_) => skip(reader, buf)?,
                        Event::End(_) | Event::Eof => break,
                        _ => {}
                    }
                }
                release.description = notes.value;
                found.push(release);
            }
            Event::Empty(e) if e.name().as_ref() == b"release" => found.push(Release {
                version: attribute(&e, b"version").unwrap_or_default(),
                timestamp: attribute(&e, b"timestamp")
                    .and_then(|t| t.parse().ok())
                    .or_else(|| attribute(&e, b"date").and_then(|d| date_seconds(&d)))
                    .unwrap_or(0),
                description: String::new(),
            }),
            Event::Start(_) => skip(reader, buf)?,
            Event::End(_) | Event::Eof => return Ok(found),
            _ => {}
        }
    }
}

/// "2024-05-17" (or a longer ISO date) to seconds since 1970, midnight UTC.
fn date_seconds(date: &str) -> Option<i64> {
    let mut parts = date.get(..10)?.split('-').map(|p| p.parse::<i64>().ok());
    let (year, month, day) = (parts.next()??, parts.next()??, parts.next()??);
    let y = if month <= 2 { year - 1 } else { year };
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let doy = (153 * (if month > 2 { month - 3 } else { month + 9 }) + 2) / 5 + day - 1;
    Some((era * 146_097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719_468) * 86_400)
}

fn screenshots<R: BufRead>(reader: &mut Reader<R>, buf: &mut Vec<u8>) -> Result<Vec<Vec<Image>>, quick_xml::Error> {
    let mut shots: Vec<(bool, Vec<Image>)> = Vec::new();
    loop {
        buf.clear();
        match reader.read_event_into(buf)?.into_owned() {
            Event::Start(e) if e.name().as_ref() == b"screenshot" => {
                let default = attribute(&e, b"type").as_deref() == Some("default");
                let mut images = Vec::new();
                loop {
                    buf.clear();
                    match reader.read_event_into(buf)?.into_owned() {
                        Event::Start(i) if i.name().as_ref() == b"image" => {
                            let kind = attribute(&i, b"type").unwrap_or_else(|| "source".into());
                            let width = attribute(&i, b"width").and_then(|w| w.parse().ok()).unwrap_or(0);
                            let height = attribute(&i, b"height").and_then(|w| w.parse().ok()).unwrap_or(0);
                            images.push(Image { kind, width, height, url: text_of(reader, buf)? });
                        }
                        Event::Start(_) => skip(reader, buf)?,
                        Event::End(_) | Event::Eof => break,
                        _ => {}
                    }
                }
                shots.push((default, images));
            }
            Event::Start(_) => skip(reader, buf)?,
            Event::End(_) | Event::Eof => {
                shots.sort_by_key(|(default, _)| !default); // Stable: the default first, the rest in their order
                return Ok(shots.into_iter().map(|(_, images)| images).collect());
            }
            _ => {}
        }
    }
}

/// A catalogue file and where its cached icons are.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CatalogueFile {
    pub path: PathBuf,
    /// For OS-style folders the icons are below `icons/<origin>/`, so the origin is added once it is known.
    pub icons: IconLayout,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IconLayout {
    /// `<root>/icons/<origin>/<size>/<file>`: the system catalogue and WoofStore's metadata folders.
    ByOrigin(PathBuf),
    /// `<folder>/icons/<size>/<file>`: a Flatpak remote's catalogue.
    Direct(PathBuf),
}

/// The catalogue files libappstream would load for the OS catalogue and the Flatpak remotes, plus `extra` folders laid
/// out like the OS catalogue (`xml/*.xml`, `icons/<origin>/<size>/`).
pub fn catalogue_files(home: &Path, extra: &[PathBuf]) -> Vec<CatalogueFile> {
    let mut found = Vec::new();
    let os_roots = [
        PathBuf::from("/usr/share/swcatalog"),
        PathBuf::from("/var/cache/swcatalog"),
        PathBuf::from("/usr/share/app-info"),
        PathBuf::from("/var/cache/app-info"),
    ];
    for root in os_roots.iter().chain(extra) {
        for folder in ["xml", "xmls"] {
            let Ok(entries) = fs::read_dir(root.join(folder)) else { continue };
            let mut paths: Vec<PathBuf> = entries.flatten().map(|e| e.path()).filter(|p| is_catalogue(p)).collect();
            paths.sort();
            found.extend(paths.into_iter().map(|path| CatalogueFile { path, icons: IconLayout::ByOrigin(root.join("icons")) }));
        }
    }
    let arch = std::env::consts::ARCH;
    for installation in [PathBuf::from("/var/lib/flatpak"), home.join(".local/share/flatpak")] {
        let Ok(remotes) = fs::read_dir(installation.join("appstream")) else { continue };
        let mut remotes: Vec<PathBuf> = remotes.flatten().map(|e| e.path()).collect();
        remotes.sort();
        for remote in remotes {
            let active = remote.join(arch).join("active");
            // The uncompressed file when both exist: no need to unpack it
            if let Some(path) = ["appstream.xml", "appstream.xml.gz"].iter().map(|n| active.join(n)).find(|p| p.exists()) {
                found.push(CatalogueFile { path, icons: IconLayout::Direct(active.join("icons")) });
            }
        }
    }
    found
}

fn is_catalogue(path: &Path) -> bool {
    let name = path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    name.ends_with(".xml") || name.ends_with(".xml.gz")
}

/// Every desktop app of every catalogue file. Files that cannot be read are reported, the rest still load.
pub fn load_all(files: &[CatalogueFile], language: &Language) -> (Vec<Component>, Vec<String>) {
    let mut components = Vec::new();
    let mut problems = Vec::new();
    for file in files {
        let direct = match &file.icons {
            IconLayout::Direct(root) => Some(root.clone()),
            IconLayout::ByOrigin(_) => None,
        };
        match read_catalogue(&file.path, direct, language) {
            Ok(mut found) => {
                if let IconLayout::ByOrigin(root) = &file.icons {
                    for component in &mut found {
                        component.icon_root = Some(root.join(&component.origin));
                    }
                }
                components.extend(found);
            }
            Err(e) => problems.push(e),
        }
    }
    (components, problems)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CATALOGUE: &str = r#"<?xml version="1.0" encoding="utf-8"?>
<components version="1.0" origin="flathub">
  <component type="desktop-application">
    <id>org.kde.krita</id>
    <name>Krita</name>
    <name xml:lang="it">Krita IT</name>
    <name xml:lang="fr">Krita FR</name>
    <summary>Digital Painting</summary>
    <description><p>Paint &amp; draw.</p><ul><li>Brushes</li></ul></description>
    <description xml:lang="pl"><p>Maluj.</p></description>
    <pkgname>krita</pkgname>
    <bundle type="flatpak">app/org.kde.krita/x86_64/stable</bundle>
    <url type="homepage">https://krita.org</url>
    <url type="bugtracker">https://bugs.kde.org</url>
    <launchable type="desktop-id">org.kde.krita.desktop</launchable>
    <icon type="stock">krita</icon>
    <icon type="cached" width="64" height="64">org.kde.krita.png</icon>
    <categories><category>Graphics</category></categories>
    <keywords><keyword>paint</keyword><keyword xml:lang="it">dipingere</keyword></keywords>
    <releases>
      <release version="5.1" timestamp="1600000000"/>
      <release version="5.2" date="2024-05-17"><description><p>Faster.</p></description></release>
    </releases>
    <screenshots>
      <screenshot><image type="source">https://x/other.png</image></screenshot>
      <screenshot type="default">
        <image type="thumbnail" width="224" height="126">https://x/small.png</image>
        <image type="thumbnail" width="1248" height="702">https://x/big.png</image>
        <image type="source" width="1920" height="1080">https://x/source.png</image>
      </screenshot>
    </screenshots>
  </component>
  <component type="runtime"><id>org.kde.Platform</id><name>Runtime</name></component>
  <component type="desktop"><id>legacy.desktop</id><name>Legacy</name></component>
</components>"#;

    #[test]
    fn components_with_their_language() {
        let it = parse_catalogue(CATALOGUE.as_bytes(), None, &Language::new("it_IT.UTF-8")).unwrap();
        assert_eq!(it.len(), 2); // The runtime is not an app
        let krita = &it[0];
        assert_eq!((krita.id.as_str(), krita.origin.as_str(), krita.name.as_str()), ("org.kde.krita", "flathub", "Krita IT"));
        assert_eq!(krita.description, "<p>Paint &amp; draw.</p><ul><li>Brushes</li></ul>");
        assert_eq!(krita.keywords, ["dipingere"]);
        let en = parse_catalogue(CATALOGUE.as_bytes(), None, &Language::new("en_US.UTF-8")).unwrap();
        assert_eq!((en[0].name.as_str(), en[0].keywords.as_slice()), ("Krita", ["paint".to_string()].as_slice()));
        assert_eq!(en[1].name, "Legacy");
    }

    #[test]
    fn bundle_links_releases_and_screenshots() {
        let krita = parse_catalogue(CATALOGUE.as_bytes(), None, &Language::new("C")).unwrap().remove(0);
        assert_eq!(krita.flatpak_ref(), Some(("org.kde.krita".into(), "stable".into())));
        assert_eq!(krita.desktop_id.as_deref(), Some("org.kde.krita.desktop"));
        assert_eq!(krita.urls[1], ("bugtracker".into(), "https://bugs.kde.org".into()));
        assert_eq!(krita.categories, ["Graphics"]);
        assert_eq!(krita.releases[0].version, "5.2"); // Newest first, by date
        assert_eq!(krita.releases[0].timestamp, 1_715_904_000);
        assert_eq!(krita.releases[0].description, "<p>Faster.</p>");
        assert_eq!(krita.screenshot_urls(4), ["https://x/big.png", "https://x/other.png"]); // Default first, closest to 900 × 500
        let runtime = Component { flatpak_bundle: Some("runtime/org.kde.Platform/x86_64/6.8".into()), ..Default::default() };
        assert_eq!(runtime.flatpak_ref(), Some(("runtime".into(), "x86_64".into())));
        // Same split as libappstream's callers
    }

    #[test]
    fn cached_icons_and_catalogue_folders() {
        let root = tempfile::tempdir().unwrap();
        let xml = root.path().join("xml");
        fs::create_dir_all(&xml).unwrap();
        fs::write(xml.join("woofstore.xml"), CATALOGUE.replace("origin=\"flathub\"", "origin=\"woofstore\"")).unwrap();
        fs::create_dir_all(root.path().join("icons/woofstore/64x64")).unwrap();
        fs::write(root.path().join("icons/woofstore/64x64/org.kde.krita.png"), b"png").unwrap();
        let files: Vec<CatalogueFile> =
            catalogue_files(Path::new("/nonexistent"), &[root.path().to_path_buf()]).into_iter().filter(|f| f.path.starts_with(root.path())).collect();
        assert_eq!(files.len(), 1);
        let (components, problems) = load_all(&files, &Language::new("C"));
        assert!(problems.is_empty());
        assert_eq!(components[0].cached_icon(), Some(root.path().join("icons/woofstore/64x64/org.kde.krita.png")));
        assert_eq!(components[1].cached_icon(), None);
    }

    #[test]
    fn gzipped_catalogue() {
        use flate2::{write::GzEncoder, Compression};
        use std::io::Write;
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("extra.xml.gz");
        let mut gz = GzEncoder::new(Vec::new(), Compression::fast());
        gz.write_all(CATALOGUE.as_bytes()).unwrap();
        fs::write(&path, gz.finish().unwrap()).unwrap();
        assert_eq!(read_catalogue(&path, None, &Language::new("C")).unwrap().len(), 2);
        assert!(read_catalogue(&dir.path().join("missing.xml"), None, &Language::new("C")).is_err());
    }
}
