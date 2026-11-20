import QtQuick
import QtQuick.Layouts

// The header of Installed and Updates: it scrolls with the page until it reaches the top, then stays there
// over the content, spanning the whole page, on the window colour with a Mid line under it. `slot` keeps its room in the page:
// give it `height: bar.slotHeight`.
Item {
    id: bar
    required property Flickable flickable
    required property Item slot
    default property alias content: column.data
    readonly property real slotHeight: column.implicitHeight
    // Where the slot is in the page's viewport
    readonly property real home: slot.parent.y + slot.y - flickable.contentY
    readonly property bool stuck: home < 0

    z: 1
    y: stuck ? 0 : home
    property real scrollBarWidth: 0  // The page's visible scroll bar: the bar spans the viewport beside it
    width: flickable.width - scrollBarWidth
    height: column.implicitHeight + (stuck ? 24 : 0)  // Stuck: 8 px above the content, 16 px between it and the list

    Rectangle {
        anchors.fill: parent
        visible: bar.stuck
        color: bar.palette.window
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: bar.palette.mid }
    }
    ColumnLayout {
        id: column
        x: bar.slot.x + bar.slot.parent.x
        y: bar.stuck ? 8 : 0
        width: bar.slot.width
        spacing: 12
    }
}
