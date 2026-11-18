import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What System Update does, shown before it starts. `accepted` gives the two choices.
Window {
    id: dialog
    property string summary
    property int aurCount: 0
    property bool hasParu: false
    signal accepted(bool aur, bool devel)

    title: "System Update"
    flags: Qt.Dialog
    modality: Qt.WindowModal
    color: palette.window
    width: 480
    height: layout.implicitHeight + 34
    minimumWidth: 480
    minimumHeight: height

    function ask(text, aurPackages, paru, aur, devel) {
        summary = text
        aurCount = aurPackages
        hasParu = paru
        aurBox.checked = aur
        develBox.checked = devel
        show(); raise(); requestActivate()
    }
    function accept() { close(); accepted(aurBox.visible && aurBox.checked, develBox.visible && develBox.checked) }

    component Note: Label {
        color: palette.placeholderText
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        leftPadding: 26
        Layout.fillWidth: true
    }

    ColumnLayout {
        id: layout
        x: 20
        y: 18
        width: dialog.width - 40
        spacing: 10
        Label { text: dialog.title; font.pointSize: 15; font.bold: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true }
        Label { text: dialog.summary; wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true }
        CheckBox {
            id: aurBox
            visible: dialog.hasParu && dialog.aurCount > 0
            text: "Include the " + dialog.aurCount + " AUR package" + (dialog.aurCount === 1 ? "" : "s")
            Layout.topMargin: 4
        }
        Note {
            visible: aurBox.visible
            text: "AUR packages are built on this computer from recipes anyone can publish. Updates are built without the review step the store asks for on a first install."
        }
        CheckBox {
            id: develBox
            visible: dialog.hasParu
            text: "Also check development (git) packages"
            Layout.topMargin: 4
        }
        Note {
            visible: develBox.visible
            text: "Packages whose name ends in -git, -svn or -hg are built from the newest source code instead of a release. Their version number rarely changes, so the list cannot show when they have news. "
                  + "With this option paru asks each project whether it has new commits and rebuilds the ones that do. It takes longer, and a rebuild can compile for several minutes."
        }
        RowLayout {  // QDialogButtonBox in the KDE order: the action, then Cancel
            Layout.topMargin: 6
            Item { Layout.fillWidth: true }
            Button { text: dialog.title; focus: true; onClicked: dialog.accept(); Keys.onReturnPressed: dialog.accept() }
            Button { text: "Cancel"; icon.name: "dialog-cancel"; onClicked: dialog.close() }
        }
    }
    Shortcut { sequence: "Escape"; onActivated: dialog.close() }
}
