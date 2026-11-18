import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// One article: an optional cover, the source with its colour, the "New" pill, the date, the title and the summary.
// A click opens the article in the browser.
ClickFrame {
    id: card
    required property var item
    property bool fresh: false  // Carries the "New" pill

    readonly property var sourceColors: ({ "Arch Linux": "#1793d1", "CachyOS": "#1abc9c", "Linux kernel": "#f1c40f", "KDE": "#3daee9", "LWN.net": "#8e6bbf", "9to5Linux": "#e74c3c", "Phoronix": "#7f8c8d", "GamingOnLinux": "#27ae60", "OMG! Ubuntu": "#e95420", "Fedora Magazine": "#3c6eb4", "Linuxiac": "#16a085", "It's FOSS": "#e67e22", "Linux Mint": "#87cf3e", "Blender": "#ea7600", "Krita": "#3babff", "Godot": "#478cbf", "LibreOffice": "#18a303", "Flathub": "#4a86cf" })

    padding: 0
    implicitHeight: column.implicitHeight
    Accessible.name: item.title + ", " + item.source
    ToolTip.text: item.url
    ToolTip.visible: hovered
    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
    onClicked: Qt.openUrlExternally(item.url)

    contentItem: ColumnLayout {
        id: column
        spacing: 0

        // The cover: hidden until it has loaded, 16:9 of the card width less 16 px, cropped to fill, rounded on top
        Item {
            id: cover
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round((width - 16) * 9 / 16)
            visible: card.item.image !== "" && picture.status === Image.Ready
            Image {
                id: picture
                anchors.fill: parent
                visible: false
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 640
                source: card.item.image === "" ? "" : card.item.image.startsWith("/") ? "file://" + card.item.image : card.item.image
            }
            Item {
                id: mask
                anchors.fill: parent
                visible: false
                layer.enabled: true
                Rectangle { width: parent.width; height: parent.height + 12; radius: 10 }
            }
            MultiEffect {
                anchors.fill: parent
                source: picture
                maskEnabled: true
                maskSource: mask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 16
            spacing: 8

            RowLayout {
                spacing: 8
                Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 3; color: card.sourceColors[card.item.source] ?? "#6c757d" }
                Label { text: card.item.source; textFormat: Text.PlainText; font.bold: true }
                Rectangle {
                    visible: card.fresh
                    // A styled QLabel indents its text by half an "x" and counts a whole one in its width
                    readonly property real indent: freshMetrics.advanceWidth("x") / 2
                    implicitWidth: fresh.implicitWidth + 16 + 2 * indent
                    implicitHeight: fresh.implicitHeight + 2
                    radius: 8
                    color: palette.active.highlight  // palette.active: the highlight ignores the inactive colours
                    FontMetrics { id: freshMetrics; font: fresh.font }
                    Label { id: fresh; x: 8 + parent.indent; anchors.verticalCenter: parent.verticalCenter; text: "New"; font.weight: Font.DemiBold; color: palette.active.highlightedText }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: card.item.date ? new Date(card.item.date).toLocaleDateString(Qt.locale(), Locale.ShortFormat) : ""
                    color: palette.placeholderText
                }
            }
            Label {
                Layout.fillWidth: true
                text: card.item.title
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                visible: card.item.summary !== ""
                text: card.item.summary
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: palette.placeholderText
            }
        }
    }
}
