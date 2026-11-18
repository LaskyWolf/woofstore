import QtQuick
import QtQuick.Controls

// A pagination bar: a rounded mark, longer and in Highlight when checked. No button panel;
// keyboard focus draws a ring. `duration` 0 switches without the spring.
AbstractButton {
    id: dot
    property int duration: 0
    property real length: checked ? 80 : 24
    Behavior on length {
        enabled: dot.duration > 0 && dot.visible
        NumberAnimation { duration: dot.duration; easing.type: Easing.OutBack; easing.overshoot: 1 }
    }
    // Checked follows the page's state; a click only asks for the change
    implicitWidth: Math.ceil(length) + 4
    implicitHeight: 36
    focusPolicy: Qt.StrongFocus
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 700
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    background: null
    contentItem: null
    Rectangle {
        x: (dot.width - dot.length) / 2
        y: (dot.height - 7) / 2
        width: dot.length
        height: 7
        radius: 3.5
        color: dot.checked ? dot.palette.highlight : Qt.rgba(dot.palette.windowText.r, dot.palette.windowText.g, dot.palette.windowText.b, 130 / 255)
    }
    Rectangle {
        visible: dot.visualFocus
        anchors.centerIn: parent
        width: 24; height: 24; radius: 12
        color: "transparent"
        border.width: 1
        border.color: dot.palette.highlight
    }
}
