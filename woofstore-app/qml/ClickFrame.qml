import QtQuick
import QtQuick.Controls

// A card that opens something when clicked, with the look of the list panels.
AbstractButton {
    id: frame
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    background: Rectangle {
        radius: 10
        color: frame.hovered || frame.activeFocus ? frame.palette.midlight : frame.palette.alternateBase
        border.width: frame.activeFocus ? 1 : 0
        border.color: frame.palette.highlight
    }
}
