import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The modeless Activity window of the sidebar control. Closing it hides it; operations keep running.
Window {
    id: window
    Shortcut { sequences: [StandardKey.Cancel]; onActivated: window.close() }  // Escape, as a QDialog
    property var store
    property var records: []    // store.activity, newest first
    signal openJob(int id)

    title: "Activity"
    flags: Qt.Dialog
    color: palette.window
    width: 800
    height: 600

    readonly property bool canClear: records.some(r => r.state !== "running" && r.state !== "waiting")
    function stateText(r) {
        return r.state === "waiting" ? "Waiting in the queue" : r.state === "running" ? "Running · progress unavailable"
            : r.state === "done" ? "Done" : r.state === "cancelled" ? "Cancelled" : "Failed"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 16
        anchors.bottomMargin: 24
        spacing: 16
        Label { text: "Activity"; font.pointSize: 23; font.bold: true; textFormat: Text.PlainText }

        ListView {  // Stays, empty, under the message
            id: wheelList
            PageWheel { target: wheelList }  // Mouse wheel as in Plasma; trackpad with inertia
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            spacing: 8
            model: window.records
            Accessible.name: "Activity history"
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 76
                hoverEnabled: true
                // Waiting ones can leave the queue; the others show their details window
                ToolTip.visible: hovered
                ToolTip.text: modelData.state === "waiting" ? "Remove from the queue" : "Show the details of this operation"
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                onClicked: window.openJob(modelData.id)
                background: Rectangle { radius: 8; color: row.palette.alternateBase }
                leftPadding: 14
                rightPadding: 14
                topPadding: 10
                bottomPadding: 10
                contentItem: ColumnLayout {
                    spacing: 5
                    Label { text: row.modelData.title + " · " + window.stateText(row.modelData); font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: "Package operation"; Layout.fillWidth: true }
                    ProgressBar {
                        visible: row.modelData.state === "running"
                        indeterminate: true
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                    }
                }
            }
        }

        RowLayout {
            spacing: 16
            Button { text: "Close"; icon.name: "window-close"; Layout.minimumHeight: 32; onClicked: window.close() }
            Button {
                text: "Clear"
                icon.name: "edit-clear"
                Layout.minimumHeight: 32
                enabled: window.canClear
                opacity: enabled ? 1 : 0.4
                ToolTip.visible: hovered
                ToolTip.text: "Clear completed activity"
                onClicked: window.store.clearEnded()
            }
            Item { Layout.fillWidth: true }
        }
    }
    // Empty: a plain message in the middle of the window instead of the list
    Column {
        visible: window.records.length === 0
        anchors.centerIn: parent
        spacing: 10
        Lucide { name: "trophy"; color: "#f5c211"; weight: 1.4; width: 72; height: 72; anchors.horizontalCenter: parent.horizontalCenter }
        Label { text: "No activity yet"; font.pointSize: 18; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
        Label {
            text: "Nothing installed, nothing broken. Suspiciously peaceful."
            color: palette.placeholderText
            horizontalAlignment: Text.AlignHCenter
            width: Math.max(380, implicitWidth)
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
