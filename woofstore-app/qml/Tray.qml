import QtQuick

// Quickpeek's Tray treatment: desktop palette, translucent fill, faint edge.
Rectangle {
    radius: 7
    color: Qt.alpha(palette.button, 150 / 255)
    border.width: 1
    border.color: Qt.alpha(palette.mid, 90 / 255)
}
