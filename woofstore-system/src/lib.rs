//! WoofStore's access to the system and the network. Every function here blocks; the user interface calls them on
//! worker threads and hands the results back to its own thread.

pub mod activity;
pub mod appstream;
pub mod aur_remote;
pub mod catalogue;
pub mod command;
pub mod http;
pub mod installed;
pub mod news;
pub mod paths;
pub mod rankings;
pub mod update_check;
