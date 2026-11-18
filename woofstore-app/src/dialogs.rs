//! The desktop's own message boxes for QML: QMessageBox windows, separate from the main window.

#[cxx_qt::bridge]
pub mod qobject {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
        include!("shell.h");
        /// `kind`: 0 information, 1 warning, 2 question (Yes / No). True for OK or Yes.
        fn message_box(kind: i32, title: &QString, text: &QString) -> bool;
    }

    #[auto_cxx_name]
    extern "RustQt" {
        #[qobject]
        #[qml_element]
        type NativeDialogs = super::NativeDialogsRust;

        #[qinvokable]
        fn information(self: &NativeDialogs, title: &QString, text: &QString);
        #[qinvokable]
        fn warning(self: &NativeDialogs, title: &QString, text: &QString);
        /// Waits for the answer; true for Yes.
        #[qinvokable]
        fn question(self: &NativeDialogs, title: &QString, text: &QString) -> bool;
    }
}

use cxx_qt_lib::QString;

#[derive(Default)]
pub struct NativeDialogsRust;

impl qobject::NativeDialogs {
    fn information(&self, title: &QString, text: &QString) {
        qobject::message_box(0, title, text);
    }
    fn warning(&self, title: &QString, text: &QString) {
        qobject::message_box(1, title, text);
    }
    fn question(&self, title: &QString, text: &QString) -> bool {
        qobject::message_box(2, title, text)
    }
}
