//! Menu entries (.desktop files) the user can start, the quoting their Exec lines need, and how a version change is
//! shown.

use crate::catalog::letter_or_number;
use std::collections::HashSet;
use std::path::Path;

/// A menu entry (.desktop file) the user can start.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Launcher {
    pub id: String,
    pub name: String,
    pub comment: String,
    pub icon: String,
    pub exec: String,
    pub path: String,
    pub flatpak: bool,
}

/// A file name without its last extension, like QFileInfo::completeBaseName: "org.a.B.desktop" -> "org.a.B".
pub(crate) fn complete_base_name(path: &str) -> String {
    let name = path.rsplit('/').next().unwrap_or(path);
    name.rsplit_once('.').map_or(name, |(base, _)| base).to_string()
}

/// `None` when the entry is hidden, is not an application, is for another desktop, or only starts a Steam game.
/// `language` is a locale name like "it_IT".
pub fn parse_launcher(path: &str, text: &[u8], language: &str) -> Option<Launcher> {
    let mut launcher = Launcher { path: path.into(), ..Default::default() };
    let (mut name, mut comment, mut local_name, mut local_comment, mut kind, mut only_show_in) =
        (String::new(), String::new(), String::new(), String::new(), String::new(), String::new());
    let (mut hidden, mut in_entry) = (false, false);
    let short_language = language.split('_').next().unwrap_or("");
    let (local_key, short_key) = (format!("[{language}]"), format!("[{short_language}]"));
    for raw in text.split(|&b| b == b'\n') {
        let line = String::from_utf8_lossy(raw);
        let line = line.trim();
        if line.starts_with('[') {
            in_entry = line == "[Desktop Entry]";
            continue;
        }
        if !in_entry || line.starts_with('#') {
            continue;
        }
        let Some(equals) = line.find('=').filter(|&at| at >= 1) else { continue };
        let (key, value) = (line[..equals].trim(), line[equals + 1..].trim().to_string());
        let localized = |field: &str, current: &str| key.strip_prefix(field).is_some_and(|tag| tag == local_key || (tag == short_key && current.is_empty()));
        match key {
            "Name" => name = value,
            _ if localized("Name", &local_name) => local_name = value,
            "Comment" => comment = value,
            _ if localized("Comment", &local_comment) => local_comment = value,
            "Icon" => launcher.icon = value,
            "Exec" => launcher.exec = value,
            "Type" => kind = value,
            "OnlyShowIn" => only_show_in = value,
            "NoDisplay" | "Hidden" => hidden |= value.eq_ignore_ascii_case("true"),
            _ => {}
        }
    }
    if hidden || kind != "Application" || launcher.exec.is_empty() {
        return None;
    }
    if !only_show_in.is_empty() && !only_show_in.split(';').any(|desktop| desktop == "KDE") {
        return None;
    }
    if launcher.exec.contains("steam://rungameid") {
        return None; // Games are managed by Steam
    }
    launcher.name = if local_name.is_empty() { name } else { local_name };
    launcher.comment = if local_comment.is_empty() { comment } else { local_comment };
    if launcher.name.is_empty() {
        return None;
    }
    launcher.id = complete_base_name(path);
    launcher.flatpak = path.contains("/flatpak/exports/");
    Some(launcher)
}

/// Every visible launcher in these folders and their subfolders. Like the menu, the first folder that has an ID wins,
/// so a user entry hides the system one. Hidden files and folders and linked folders are skipped, as Qt's directory
/// iterator does.
pub fn scan_launchers(dirs: &[&Path], language: &str) -> Vec<Launcher> {
    fn walk(dir: &Path, found: &mut Vec<std::path::PathBuf>) {
        let Ok(entries) = std::fs::read_dir(dir) else { return };
        let mut entries: Vec<_> = entries.flatten().filter(|e| !e.file_name().to_string_lossy().starts_with('.')).collect();
        entries.sort_by_key(|e| e.file_name());
        for entry in entries {
            let path = entry.path();
            let Ok(kind) = entry.file_type() else { continue };
            if kind.is_dir() {
                walk(&path, found);
            } else if path.is_file() && entry.file_name().to_string_lossy().to_lowercase().ends_with(".desktop") {
                found.push(path);
            }
        }
    }
    let mut result = Vec::new();
    let mut seen = HashSet::new();
    for dir in dirs {
        let mut files = Vec::new();
        walk(dir, &mut files);
        for path in files {
            let id = path.strip_prefix(dir).unwrap_or(&path).to_string_lossy().replace('/', "-");
            if !seen.insert(id) {
                continue;
            }
            let Ok(text) = std::fs::read(&path) else { continue };
            if let Some(launcher) = parse_launcher(&path.to_string_lossy(), &text, language) {
                result.push(launcher);
            }
        }
    }
    result
}

/// The program an AppImage launcher starts, without quotes and arguments.
pub fn launcher_program(exec: &str) -> String {
    let trimmed = exec.trim();
    match trimmed.strip_prefix('"') {
        Some(rest) => rest.split('"').next().unwrap_or("").to_string(),
        None => trimmed.split(' ').next().unwrap_or("").to_string(),
    }
}

/// A path as one quoted argument of a desktop file's Exec line. Exec first unescapes the value, then parses quoted
/// arguments, so backslashes and the characters special inside quotes are escaped twice.
pub fn desktop_quote(path: &str) -> String {
    let mut quoted = path.replace('\\', r"\\\\");
    for c in ['"', '`', '$'] {
        quoted = quoted.replace(c, &format!(r"\\{c}"));
    }
    format!("\"{}\"", quoted.replace('%', "%%"))
}

/// Like QString::toHtmlEscaped.
fn html_escaped(text: &str) -> String {
    text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

/// Both versions as rich text with only the part that changed coloured: the old one red, the new one green.
pub fn version_diff(from: &str, to: &str) -> (String, String) {
    let wrap = |text: &[char], color: &str| {
        if text.is_empty() {
            return String::new();
        }
        format!("<span style=\"color:{color}; font-weight:600\">{}</span>", html_escaped(&text.iter().collect::<String>()))
    };
    let (from, to): (Vec<char>, Vec<char>) = (from.chars().collect(), to.chars().collect());
    let mut prefix = from.iter().zip(&to).take_while(|(a, b)| a == b).count();
    // Do not cut a number in half: step back to the start of the version part that differs.
    let continues = |at: usize| from.get(at).is_some_and(|&c| letter_or_number(c)) || to.get(at).is_some_and(|&c| letter_or_number(c));
    while prefix > 0 && letter_or_number(from[prefix - 1]) && continues(prefix) {
        prefix -= 1;
    }
    // Everything from the first difference to the end is coloured, so "2.5.2-1.1" and "2.5.3-1.1" differ from the "2" on.
    let head = html_escaped(&from[..prefix].iter().collect::<String>());
    (head.clone() + &wrap(&from[prefix..], "#da4453"), head + &wrap(&to[prefix..], "#27ae60"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use regex::Regex;

    #[test]
    fn desktop_quoting() {
        assert_eq!(desktop_quote("/tmp/my app%20.AppImage"), "\"/tmp/my app%%20.AppImage\"");
        assert_eq!(desktop_quote("/tmp/a$b`c\"d\\e"), "\"/tmp/a\\\\$b\\\\`c\\\\\"d\\\\\\\\e\"");
        assert_eq!(launcher_program("\"/opt/My App.AppImage\" %U"), "/opt/My App.AppImage");
        assert_eq!(launcher_program("  /opt/a.AppImage --x"), "/opt/a.AppImage");
    }

    #[test]
    fn launchers() {
        let mpv = parse_launcher(
            "/usr/share/applications/mpv.desktop",
            b"[Desktop Entry]\nType=Application\nName=mpv\nName[it]=Lettore mpv\nComment=Video player\nExec=mpv %U\nIcon=mpv\n[Desktop Action x]\nName=Other\n",
            "it_IT",
        )
        .unwrap();
        assert!(mpv.name == "Lettore mpv" && mpv.comment == "Video player" && mpv.id == "mpv" && !mpv.flatpak);
        assert!(!parse_launcher("/x/org.a.B.desktop", b"[Desktop Entry]\nType=Application\nName=B\nExec=b", "en_US").unwrap().flatpak);
        assert!(
            parse_launcher("/v/flatpak/exports/share/applications/org.a.B.desktop", b"[Desktop Entry]\nType=Application\nName=B\nExec=b", "en_US")
                .unwrap()
                .flatpak
        );
        assert_eq!(parse_launcher("/x/org.a.B.desktop", b"[Desktop Entry]\nType=Application\nName=B\nExec=b", "en_US").unwrap().id, "org.a.B");
        assert_eq!(parse_launcher("/x/h.desktop", b"[Desktop Entry]\nType=Application\nName=H\nExec=h\nNoDisplay=true", "en_US"), None); // hidden
        assert_eq!(parse_launcher("/x/l.desktop", b"[Desktop Entry]\nType=Link\nName=L\nExec=l", "en_US"), None); // not an application
        assert_eq!(parse_launcher("/x/g.desktop", b"[Desktop Entry]\nType=Application\nName=G\nOnlyShowIn=GNOME;", "en_US"), None); // no Exec, other desktop
        assert_eq!(parse_launcher("/x/s.desktop", b"[Desktop Entry]\nType=Application\nName=Game\nExec=steam steam://rungameid/1", "en_US"), None);
        // Steam game
    }

    #[test]
    fn scanning_prefers_the_first_folder() {
        let (user, system) = (tempfile::tempdir().unwrap(), tempfile::tempdir().unwrap());
        let entry = |name: &str| format!("[Desktop Entry]\nType=Application\nName={name}\nExec=x\n");
        std::fs::write(user.path().join("a.desktop"), entry("User A")).unwrap();
        std::fs::create_dir(system.path().join("kde")).unwrap();
        std::fs::write(system.path().join("a.desktop"), entry("System A")).unwrap();
        std::fs::write(system.path().join("kde/b.desktop"), entry("B")).unwrap();
        std::fs::write(system.path().join(".hidden.desktop"), entry("Hidden")).unwrap();
        let found = scan_launchers(&[user.path(), system.path()], "en_US");
        let names: Vec<&str> = found.iter().map(|l| l.name.as_str()).collect();
        assert_eq!(names, ["User A", "B"]);
    }

    #[test]
    fn version_changes() {
        let plain = |html: &str| Regex::new("<[^>]*>").unwrap().replace_all(html, "").into_owned();
        let kernel = version_diff("7.2.9-2", "7.2.11-1");
        assert!(plain(&kernel.0) == "7.2.9-2" && plain(&kernel.1) == "7.2.11-1" && kernel.0.starts_with("7.2.<span") && kernel.1.contains(">11-1<"));
        assert_eq!(version_diff("5.2.9", "5.3.0").1, "5.<span style=\"color:#27ae60; font-weight:600\">3.0</span>");
        let longer = version_diff("1.10", "1.1"); // Whole numbers change, never half a number
        assert!(longer.0.contains(">10<") && longer.1.contains(">1<"));
        assert_eq!(version_diff("262.1", "262.1.1").0, "262.1");
        assert!(version_diff("262.1", "262.1.1").1.contains(">.1<"));
        assert_eq!(plain(&version_diff("2.5.2-1.1", "2.5.3-1.1").1), "2.5.3-1.1");
        assert!(version_diff("2.5.2-1.1", "2.5.3-1.1").1.contains(">3-1.1<"));
        assert_eq!(version_diff("2.0", "2.0").0, "2.0");
    }
}
