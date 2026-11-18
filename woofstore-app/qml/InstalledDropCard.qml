import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// A card that opens a file chooser on click and takes dropped files of its own kind.
AbstractButton {
    id: card
    property string kind   // "AppImage" or "Flatpak"
    property string caption
    property var accepts: url => false
    signal dropped(var urls)
    property bool dragging: false
    readonly property color accent: kind === "AppImage" ? "#9141ac" : "#3584e4"

    implicitHeight: 84
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    HoverHandler { cursorShape: Qt.PointingHandCursor }

    DropArea {
        anchors.fill: parent
        onEntered: drag => {
            card.dragging = drag.hasUrls && drag.urls.length > 0 && drag.urls.every(u => card.accepts(String(u)))
            drag.accepted = card.dragging
        }
        onExited: card.dragging = false
        onDropped: drop => {
            card.dragging = false
            card.dropped(drop.urls.map(String))
            drop.acceptProposedAction()
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: 12
            color: card.dragging ? Qt.alpha(card.accent, 0.18) : card.hovered || card.activeFocus ? card.palette.button : card.palette.alternateBase
        }
        Shape {
            anchors.fill: parent
            visible: card.dragging
            ShapePath {
                strokeColor: card.accent
                strokeWidth: 2
                strokeStyle: ShapePath.DashLine
                fillColor: "transparent"
                PathRectangle { x: 1; y: 1; width: card.width - 2; height: card.height - 2; radius: 12 }
            }
        }
        Rectangle {
            x: 20
            y: (card.height - 48) / 2
            width: 48
            height: 48
            radius: 12
            color: card.accent
            Lucide { x: 11; y: 11; width: 26; height: 26; color: "white"; name: card.kind === "AppImage" ? "app-window" : "package" }
        }
        Label {
            x: 84
            width: card.width - 84 - 12
            height: card.height / 2 + 2
            verticalAlignment: Text.AlignBottom
            text: card.text
            textFormat: Text.PlainText
            font.bold: true
            font.pointSize: card.font.pointSize * 1.1
            elide: Text.ElideRight
        }
        Label {
            x: 84
            y: card.height / 2 + 6
            width: card.width - 84 - 12
            text: card.dragging ? "Release to install" : card.caption
            textFormat: Text.PlainText
            color: card.palette.placeholderText
            elide: Text.ElideRight
        }
    }
}
