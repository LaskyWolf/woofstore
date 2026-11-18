import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Progress of a package operation without a terminal: steps, one status line, and the raw output on request.
// While the operation runs, closing the window only hides it.
Window {
    id: window
    Shortcut { sequences: [StandardKey.Cancel]; onActivated: window.close() }  // Escape, as a QDialog
    property var store
    property int jobId: 0
    property var job: ({})          // The operation's entry of store.activity
    readonly property bool done: job.state === "done" || job.state === "failed"
    readonly property bool ok: job.state === "done"
    property string log: ""

    title: job.title ?? ""
    flags: Qt.Dialog
    color: palette.window
    width: Math.max(560, layout.implicitWidth + 20)
    height: layout.implicitHeight + 20
    minimumWidth: layout.implicitWidth + 20
    minimumHeight: layout.implicitHeight + 20

    function refresh() {
        if (!store) return  // createObject can set job before store
        log = store.jobLog(jobId)
        const lines = log.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "").split(/[\n\r]/).map(l => l.trim()).filter(l => l !== "")
        let fraction = 0
        for (const line of lines) {
            const percent = line.match(/(\d{1,3})%/)
            if (percent && Number(percent[1]) <= 100) fraction = Number(percent[1]) / 100
        }
        track.fraction = fraction
        const last = lines.length ? lines[lines.length - 1] : ""
        phase.text = done ? (ok ? "Done" : "It did not finish. Open the details to see why.")
            : last === "" ? "Starting…" : last.startsWith("==>") ? last.slice(3).trim() : last.startsWith("->") ? last.slice(2).trim() : last
        if (done && !ok) details.checked = true
    }
    onJobChanged: refresh()
    Component.onCompleted: refresh()

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12
        Label {
            text: window.title
            font.pointSize: Application.font.pointSize + 4
            font.bold: true
            Layout.fillWidth: true
            Layout.preferredWidth: 520  // Long text wraps inside the step track's width instead of widening the window
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        StepTrack {
            id: track
            Layout.fillWidth: true
            Layout.minimumWidth: implicitWidth  // Room for every step name
            names: window.job.steps && window.job.steps.length ? window.job.steps : ["Prepare", "Install"]
            step: window.done && window.ok ? names.length : (window.job.step ?? 0)
            failed: window.done && !window.ok
            finished: window.done
        }
        Label {
            id: phase
            Layout.fillWidth: true
            Layout.preferredWidth: 520
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: palette.placeholderText
            text: "Starting…"
        }
        ScrollView {
            visible: details.checked
            Layout.fillWidth: true
            Layout.fillHeight: true
            // A fixed size: the window follows the steps, not the length or width of the output
            Layout.preferredWidth: 520
            Layout.preferredHeight: 220
            Layout.minimumHeight: 220
            TextArea {
                readOnly: true
                text: window.log.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "")
                font.family: "monospace"
                wrapMode: TextEdit.NoWrap
                onTextChanged: cursorPosition = length
            }
        }
        RowLayout {
            ToolButton {
                id: details
                checkable: true
                text: checked ? "Hide details" : "Show details"
                icon.name: checked ? "arrow-down" : "arrow-right"
                display: AbstractButton.TextBesideIcon
            }
            Item { Layout.fillWidth: true }
            Button {
                text: window.done ? "Close" : "Hide"
                ToolTip.visible: hovered && !window.done
                ToolTip.text: "The operation keeps running; progress stays in Activity"
                onClicked: window.close()
            }
        }
    }
}
