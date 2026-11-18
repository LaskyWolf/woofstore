//! WoofStore logic that needs no user interface: package operations run outside the store,
//! the queue that orders them, update checks and the scripts the store runs.

pub mod appimage;
pub mod appimage_import;
pub mod aur;
pub mod catalog;
pub mod install_specs;
pub mod jobs;
pub mod launchers;
pub mod leftovers;
pub mod merge;
pub mod news;
pub mod packages;
pub mod queue;
pub mod review;
pub mod system_specs;
pub mod updates;
