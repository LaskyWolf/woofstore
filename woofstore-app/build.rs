use cxx_qt_build::{CxxQtBuilder, QmlModule};

fn main() {
    let qml: Vec<String> = std::fs::read_dir("qml")
        .expect("qml folder")
        .map(|entry| format!("qml/{}", entry.expect("qml entry").file_name().to_string_lossy()))
        .filter(|name| name.ends_with(".qml"))
        .collect();
    for file in &qml {
        println!("cargo:rerun-if-changed={file}");
    }
    CxxQtBuilder::new_qml_module(QmlModule::new("org.woofstore.app").qml_files(qml))
        .qt_module("Widgets")
        .qt_module("Quick")
        .qt_module("DBus")
        .files(["src/store.rs", "src/catalog.rs", "src/updates.rs", "src/news.rs", "src/aur.rs", "src/shell.rs", "src/dialogs.rs"])
        .include_dir("cpp")
        .cpp_file("cpp/shell.cpp")
        .build();
}
