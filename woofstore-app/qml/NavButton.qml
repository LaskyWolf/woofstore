import QtQuick
import QtQuick.Controls

// A sidebar entry. The icon never moves; widening or narrowing the button only reveals or hides the label.
AbstractButton {
    id: button
    property string glyph
    property int badge: 0                  // Count shown on the icon, e.g. pending updates
    property color badgeColor: "#da4453"   // Red asks for attention; the accent colour only informs
    // 0 while only the icon fits, 1 when the whole label has room
    readonly property real reveal: Math.max(0, Math.min(1, (width - 84) / 48))

    checkable: true
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    ToolTip.text: text
    ToolTip.visible: hovered && reveal === 0
    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: 8
        color: button.checked ? button.palette.midlight : button.hovered ? button.palette.button : "transparent"
        border.width: button.activeFocus ? 1 : 0
        border.color: button.palette.highlight
    }
    contentItem: Item {
        Lucide {
            x: 9
            y: button.height / 2 - 11
            width: 22
            height: 22
            name: button.glyph
            color: button.palette.windowText
        }
        Rectangle {
            visible: button.badge > 0
            x: 32 - 8
            y: button.height / 2 - 9 - 8
            width: 16
            height: 16
            radius: 8
            color: button.badgeColor
            Text {
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: button.badge > 9 ? "9+" : button.badge
                font.pointSize: 7.5
                font.bold: true
                // Black on light badge colours, white on dark ones (Qt's HSL lightness above 170 of 255)
                color: (Math.max(button.badgeColor.r, button.badgeColor.g, button.badgeColor.b) + Math.min(button.badgeColor.r, button.badgeColor.g, button.badgeColor.b)) / 2 > 170 / 255 ? "black" : "white"
            }
        }
        Text {
            visible: button.reveal > 0
            x: 44 - (1 - button.reveal) * 10
            width: button.width - 48
            height: button.height
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: button.text
            color: button.palette.windowText
            opacity: button.reveal
        }
    }
}
