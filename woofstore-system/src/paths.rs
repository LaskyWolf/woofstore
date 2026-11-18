//! The store's folders. They are the ones 1.x used (organisation and application both "WoofStore"), so a
//! job it started, its caches and its imported AppImages are found by the Rust version too.

use std::env;
use std::path::PathBuf;

fn xdg(variable: &str, fallback: &str) -> PathBuf {
    env::var_os(variable)
        .map(PathBuf::from)
        .filter(|path| path.is_absolute())
        .unwrap_or_else(|| PathBuf::from(env::var_os("HOME").unwrap_or_default()).join(fallback))
}

/// `~/.local/share`: imported AppImages and their launchers live below it.
pub fn data_home() -> PathBuf {
    xdg("XDG_DATA_HOME", ".local/share")
}

/// `~/.local/share/WoofStore/WoofStore`: job folders and the askpass helper.
pub fn app_data() -> PathBuf {
    data_home().join("WoofStore/WoofStore")
}

/// `~/.cache/WoofStore/WoofStore`: downloaded lists and web answers.
pub fn cache() -> PathBuf {
    xdg("XDG_CACHE_HOME", ".cache").join("WoofStore/WoofStore")
}

/// `~/.config/WoofStore/WoofStore.conf` is the settings file, shared with 1.x.
pub fn config() -> PathBuf {
    xdg("XDG_CONFIG_HOME", ".config").join("WoofStore")
}

pub fn home() -> PathBuf {
    PathBuf::from(env::var_os("HOME").unwrap_or_default())
}
