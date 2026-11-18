import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

// Asks what to remove besides the package itself. Settings and data stay unless the person ticks them.
// ask(name, choices: [{text, checked}], leftovers: [{kind, path, bytes, checkedByDefault}]); `accepted` gives the ticked
// choices (one bool each) and the chosen leftover paths.
Window {
    id: window
    Shortcut { sequences: [StandardKey.Cancel]; onActivated: window.close() }  // Escape, as a QDialog
    property string appName: ""
    property var choices: []
    property var leftovers: []
    signal accepted(var options, var paths)

    title: "Remove " + appName
    flags: Qt.Dialog
    modality: Qt.WindowModal
    color: palette.window
    width: Math.max(520, layout.implicitWidth + 20)
    height: layout.implicitHeight + 20
    minimumWidth: 520
    minimumHeight: layout.implicitHeight + 20

    function ask(name, offered, found) {
        appName = name
        choices = offered
        leftovers = found
        show(); raise(); requestActivate()
    }
    function size(bytes) {
        const units = ["B", "KiB", "MiB", "GiB"]
        let value = bytes, unit = 0
        while (value >= 1024 && unit < units.length - 1) { value /= 1024; ++unit }
        return (unit === 0 ? value : value.toFixed(1)) + " " + units[unit]
    }
    readonly property string home: StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", "")

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 10
        Label { text: "Remove " + window.appName + " from this computer?"; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Repeater {
            id: options
            model: window.choices
            CheckBox { required property var modelData; text: modelData.text; checked: modelData.checked }
        }
        Label { visible: window.leftovers.length > 0; text: "Files this app left in your home folder"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label {
            visible: window.leftovers.length > 0
            text: "Caches are rebuilt on demand and are selected. Settings and data may hold your documents or accounts, so they stay unless you select them."
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.maximumWidth: 600
        }
        Repeater {
            id: files
            model: window.leftovers
            CheckBox {
                required property var modelData
                text: modelData.kind + " · " + (modelData.path.startsWith(window.home) ? "~" + modelData.path.slice(window.home.length) : modelData.path) + " · " + window.size(modelData.bytes)
                checked: modelData.checkedByDefault
            }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: window.close() }
            Button {
                text: "Remove"
                onClicked: {
                    const ticked = [], paths = []
                    for (let i = 0; i < options.count; ++i) ticked.push(options.itemAt(i).checked)
                    for (let i = 0; i < files.count; ++i) if (files.itemAt(i).checked) paths.push(window.leftovers[i].path)
                    window.close()
                    window.accepted(ticked, paths)
                }
            }
        }
    }
}
