#[cxx::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("shell.h");
        /// Opens the window, or hands `link` to the store that is already open and returns 0.
        fn run_shell(url: &str, link: &str) -> i32;
    }
}
