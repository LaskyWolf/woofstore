//! Where an AppImage's newer versions are, and whether the installed copy is the newest.

use crate::packages::valid_package_name;
use regex::Regex;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::sync::LazyLock;

/// The update information the publisher embedded in the AppImage's `.upd_info` ELF section; `None` when it has none.
pub fn update_info_text(path: &Path) -> Option<String> {
    let mut file = File::open(path).ok()?;
    let mut head = [0u8; 64];
    file.read_exact(&mut head).ok()?;
    if &head[..4] != b"\x7fELF" || head[4] != 2 || head[5] != 1 {
        return None; // 64-bit, little endian only
    }
    let number = |bytes: &[u8], at: usize, size: usize| bytes[at..at + size].iter().rev().fold(0u64, |v, &b| (v << 8) | u64::from(b));
    let (table_at, entry, count, names_index) = (number(&head, 0x28, 8), number(&head, 0x3A, 2), number(&head, 0x3C, 2), number(&head, 0x3E, 2));
    if table_at == 0 || entry < 64 || count == 0 || count > 256 || names_index >= count {
        return None;
    }
    let mut table = vec![0u8; (entry * count) as usize];
    file.seek(SeekFrom::Start(table_at)).ok()?;
    file.read_exact(&mut table).ok()?;
    let mut section = |index: u64, limit: u64| -> Option<Vec<u8>> {
        let base = (index * entry) as usize;
        let (at, size) = (number(&table, base + 0x18, 8), number(&table, base + 0x20, 8));
        if size > limit {
            return None;
        }
        let mut bytes = vec![0u8; size as usize];
        file.seek(SeekFrom::Start(at)).ok()?;
        file.read_exact(&mut bytes).ok()?;
        Some(bytes)
    };
    let names = section(names_index, 1 << 20)?;
    let until_nul = |bytes: &[u8]| bytes.iter().position(|&b| b == 0).map_or(bytes, |end| &bytes[..end]).to_vec();
    for i in 0..count {
        let name_at = number(&table, (i * entry) as usize, 4) as usize;
        if name_at >= names.len() || until_nul(&names[name_at..]) != b".upd_info" {
            continue;
        }
        let info = String::from_utf8_lossy(&until_nul(&section(i, 4096)?)).trim().to_string();
        return (!info.is_empty()).then_some(info);
    }
    None
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UpdateInfo {
    /// `zsync|<https address of the .zsync file>`
    Zsync { url: String },
    /// `gh-releases-zsync|owner|repo|tag|pattern of the .zsync file name`
    GitHub { owner: String, repo: String, tag: String, pattern: String },
}

pub fn parse_update_info(info: &str) -> Option<UpdateInfo> {
    let parts: Vec<&str> = info.split('|').collect();
    match parts.as_slice() {
        ["zsync", url, ..] if url.starts_with("https://") => Some(UpdateInfo::Zsync { url: (*url).into() }),
        ["gh-releases-zsync", owner, repo, tag, pattern, ..]
            if valid_package_name(owner) && valid_package_name(repo) && !tag.is_empty() && !pattern.is_empty() =>
        {
            Some(UpdateInfo::GitHub { owner: (*owner).into(), repo: (*repo).into(), tag: (*tag).into(), pattern: (*pattern).into() })
        }
        _ => None,
    }
}

fn header_line(header: &[u8], key: &str) -> Option<String> {
    let text = String::from_utf8_lossy(&header[..header.len().min(4096)]).into_owned();
    let line = Regex::new(&format!(r"(?m)^{key}:[ \t]*(\S+)[ \t]*\r?$")).unwrap();
    line.captures(&text).map(|c| c[1].to_string())
}

/// The "SHA-1:" line of a .zsync file is the checksum of the newest file; equal to the installed file's checksum means
/// up to date.
pub fn zsync_sha1(header: &[u8]) -> Option<String> {
    header_line(header, "SHA-1").filter(|sum| sum.len() == 40 && sum.chars().all(|c| c.is_ascii_hexdigit())).map(|sum| sum.to_lowercase())
}

/// The file a .zsync file says to download (its URL: line, relative to the .zsync address). Nightly builds change their
/// file name, so the address cannot be guessed from the .zsync name. `None` when there is no line or it is not http(s).
pub fn zsync_download_url(header: &[u8], zsync_address: &str) -> Option<String> {
    let target = url::Url::parse(zsync_address).ok()?.join(&header_line(header, "URL")?).ok()?;
    matches!(target.scheme(), "https" | "http").then(|| target.to_string())
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ReleaseAsset {
    pub url: String,
    pub zsync_url: String,
    pub version: String,
}

/// A shell-style pattern (`*`, `?`) matched against the whole name.
fn wildcard(pattern: &str) -> Regex {
    let mut expression = String::from("^");
    for c in pattern.chars() {
        match c {
            '*' => expression.push_str("[^/]*"),
            '?' => expression.push_str("[^/]"),
            _ => expression.push_str(&regex::escape(&c.to_string())),
        }
    }
    expression.push('$');
    Regex::new(&expression).expect("escaped")
}

/// The release asset matching the publisher's pattern, with the address of its .zsync file. The pattern names the
/// .zsync file; the AppImage itself has the same name without ".zsync".
pub fn pick_release_asset(release_json: &str, pattern: &str) -> Option<ReleaseAsset> {
    static EMPTY: LazyLock<Vec<serde_json::Value>> = LazyLock::new(Vec::new);
    let release: serde_json::Value = serde_json::from_str(release_json).ok()?;
    let urls: BTreeMap<&str, &str> = release["assets"]
        .as_array()
        .unwrap_or(&EMPTY)
        .iter()
        .filter_map(|asset| Some((asset["name"].as_str()?, asset["browser_download_url"].as_str()?)))
        .collect();
    let wanted = wildcard(pattern);
    urls.iter().filter(|(name, _)| name.ends_with(".zsync") && wanted.is_match(name)).find_map(|(name, zsync)| {
        let image = urls.get(&name[..name.len() - ".zsync".len()])?;
        Some(ReleaseAsset { url: (*image).into(), zsync_url: (*zsync).into(), version: release["tag_name"].as_str().unwrap_or("").into() })
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A minimal 64-bit ELF file with a `.upd_info` section.
    fn elf_with_update_info(info: &str) -> Vec<u8> {
        let mut elf = vec![0u8; 64];
        elf[..4].copy_from_slice(b"\x7fELF");
        elf[4] = 2;
        elf[5] = 1;
        let mut upd = info.as_bytes().to_vec();
        upd.push(0);
        upd.extend([0xff; 20]);
        let names = b"\0.shstrtab\0.upd_info\0".to_vec();
        let put = |bytes: &mut [u8], at: usize, value: u64, size: usize| {
            for i in 0..size {
                bytes[at + i] = (value >> (8 * i)) as u8;
            }
        };
        let (info_at, names_at) = (64, 64 + upd.len());
        let table_at = names_at + names.len();
        let mut table = vec![0u8; 64 * 3]; // null section, .shstrtab, .upd_info
        put(&mut table, 64, 1, 4);
        put(&mut table, 64 + 0x18, names_at as u64, 8);
        put(&mut table, 64 + 0x20, names.len() as u64, 8);
        put(&mut table, 128, 11, 4);
        put(&mut table, 128 + 0x18, info_at as u64, 8);
        put(&mut table, 128 + 0x20, upd.len() as u64, 8);
        put(&mut elf, 0x28, table_at as u64, 8);
        put(&mut elf, 0x3A, 64, 2);
        put(&mut elf, 0x3C, 3, 2);
        put(&mut elf, 0x3E, 1, 2);
        [elf, upd, names, table].concat()
    }

    #[test]
    fn update_info_from_the_elf_section() {
        let dir = tempfile::tempdir().unwrap();
        let image = dir.path().join("Foo.AppImage");
        std::fs::write(&image, elf_with_update_info("gh-releases-zsync|owner|repo|latest|Foo-*-x86_64.AppImage.zsync")).unwrap();
        let text = update_info_text(&image).unwrap();
        assert_eq!(text, "gh-releases-zsync|owner|repo|latest|Foo-*-x86_64.AppImage.zsync");
        assert_eq!(update_info_text(Path::new("/nonexistent")), None);
        let Some(UpdateInfo::GitHub { owner, tag, pattern, .. }) = parse_update_info(&text) else { panic!("not GitHub") };
        assert_eq!((owner.as_str(), tag.as_str()), ("owner", "latest"));
        assert_eq!(parse_update_info("gh-releases-direct|a|b|latest|x"), None);
        assert!(matches!(parse_update_info("zsync|https://example.org/Foo.AppImage.zsync"), Some(UpdateInfo::Zsync { url }) if url.ends_with(".zsync")));
        assert_eq!(parse_update_info("zsync|file:///etc/passwd"), None);

        let release = r#"{"tag_name":"v2.0","assets":[{"name":"Foo-2.0-x86_64.AppImage","browser_download_url":"https://x/Foo-2.0-x86_64.AppImage"},{"name":"Foo-2.0-x86_64.AppImage.zsync","browser_download_url":"https://x/Foo-2.0-x86_64.AppImage.zsync"},{"name":"Other.AppImage","browser_download_url":"https://x/o"}]}"#;
        let asset = pick_release_asset(release, &pattern).unwrap();
        assert_eq!(asset.url, "https://x/Foo-2.0-x86_64.AppImage");
        assert!(asset.zsync_url.ends_with(".zsync"));
        assert_eq!(asset.version, "v2.0");
        assert_eq!(pick_release_asset("{}", "*"), None);
    }

    #[test]
    fn zsync_header() {
        assert_eq!(
            zsync_sha1(b"zsync: 0.6.2\nFilename: Foo\nSHA-1: 0123456789ABCDEF0123456789abcdef01234567\n\n").as_deref(),
            Some("0123456789abcdef0123456789abcdef01234567")
        );
        assert_eq!(zsync_sha1(b"nothing"), None);
        assert_eq!(
            zsync_download_url(b"zsync: 0.6.2\nFilename: Eden-1.AppImage\nURL: Eden-1.AppImage\nSHA-1: x\n", "https://n.example/latest/Eden.AppImage.zsync")
                .as_deref(),
            Some("https://n.example/latest/Eden-1.AppImage")
        );
        assert_eq!(
            zsync_download_url(b"URL: https://cdn.example/a.AppImage\n", "https://n.example/x.zsync").as_deref(),
            Some("https://cdn.example/a.AppImage")
        );
        assert_eq!(zsync_download_url(b"URL: file:///etc/passwd\n", "https://n.example/x.zsync"), None);
        assert_eq!(zsync_download_url(b"nothing", "https://n.example/x.zsync"), None);
    }
}
