import QtQuick
import QtQuick.Controls

// A list of checkable lines for the orphans and services windows. `model`: [{text, checked}];
// `checked` follows the boxes, one bool each.
Rectangle {
    id: list
    property var model: []
    property var checked: []
    onModelChanged: checked = model.map(m => m.checked)
    function setAll(on) { checked = model.map(() => on) }

    color: palette.base
    radius: 3

    ListView {
        id: wheelList
        PageWheel { target: wheelList }  // Mouse wheel as in Plasma; trackpad with inertia
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        model: list.model
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: CheckBox {
            required property var modelData
            required property int index
            width: ListView.view.width
            height: 24
            text: modelData.text
            checked: list.checked[index] === true
            onToggled: { const next = list.checked.slice(); next[index] = checked; list.checked = next }
        }
    }
}
