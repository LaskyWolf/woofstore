import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Updates: a list only when there is something to update, otherwise a short message.
// System Update stays at the bottom of the window and runs everything in one go.
Item {
    id: page
    required property var window
    property Flickable flickable: flick
    readonly property var store: window.store
    readonly property var updates: window.updates
    readonly property var flow: window.updatesFlow

    readonly property var rows: { updates.entries; store.installed; return JSON.parse(updates.rows(store.installed)) }
    readonly property var groups: ["Apps", "System packages"].map(g => ({ title: g, rows: rows.filter(r => r.group === g) })).filter(g => g.rows.length > 0)
    readonly property var installedIds: new Set(JSON.parse(store.installed).map(a => a.id))
    readonly property bool pending: updates.systemCount > 0

    // Screenshots with sample results: "updates,available" and the like; "updates,confirm" opens
    // the System Update confirmation.
    Connections {
        target: page.window
        function onPreviewChanged() {
            const parts = page.window.preview.split(",")
            const state = ["idle", "checking", "failed", "available", "apps"].find(s => parts.includes(s))
            if (parts[0] === "updates" && state) page.updates.preview(state)
            if (parts.includes("confirm")) { page.confirmUpdate(); page.window.shotTarget = confirm }
        }
    }

    function confirmUpdate() {
        const settings = window.settings
        confirm.ask(updates.systemUpdateText(String(settings.value("lastSystemUpdate", ""))),
                    rows.filter(r => r.source === "AUR").length, updates.hasProgram("paru"),
                    String(settings.value("updateAur", true)) === "true", String(settings.value("updateDevel", false)) === "true")
    }
    UpdatesConfirm {
        id: confirm
        transientParent: page.Window.window
        onAccepted: (aur, devel) => {
            if (aurCount > 0 && hasParu) page.window.settings.setValue("updateAur", aur)
            if (hasParu) page.window.settings.setValue("updateDevel", devel)
            const op = JSON.parse(page.updates.systemUpdateJob(aur, devel))
            page.window.queueJob(op.lane, op)
        }
    }

    Flickable {
        id: flick
        PageWheel { target: flick }  // Mouse wheel as in Plasma; trackpad with inertia
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, body.implicitHeight + 16 + 24)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { id: scrollBar }

        ColumnLayout {
            id: body
            x: 20
            y: 16
            // The page is always a little taller than the window, so its scroll bar always keeps its room
            width: flick.width - 40 - scrollBar.width
            height: flick.contentHeight - 16 - 24
            spacing: 16

            Item { id: slot; Layout.fillWidth: true; Layout.preferredHeight: sticky.slotHeight }

            ColumnLayout {  // Nothing listed: checking, failed, not checked yet, or up to date
                visible: page.groups.length === 0
                readonly property bool failed: page.updates.checked && page.updates.problems !== ""
                readonly property var look: page.updates.loading ? ["refresh", palette.placeholderText, "Checking for updates…", ""]
                    : failed ? ["circle-x", "#da4453", "Couldn't check for updates", page.updates.problems]
                    : !page.updates.checked ? ["refresh", palette.placeholderText, "Updates haven't been checked yet", "Use the refresh button above."]
                    : ["circle-check", "#27ae60", "You're all up to date", "Your apps and system packages are current."]
                id: empty
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 340
                spacing: 10
                Item { Layout.fillHeight: true; Layout.fillWidth: true }
                Lucide { name: empty.look[0]; color: empty.look[1]; Layout.preferredWidth: 72; Layout.preferredHeight: 72; weight: 1.4; Layout.alignment: Qt.AlignHCenter }
                Label { text: empty.look[2]; font.pointSize: 18; font.bold: true; textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter }
                Label {  // Empty while checking, and still keeping its line
                    text: empty.look[3]
                    textFormat: Text.PlainText
                    color: palette.placeholderText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.minimumWidth: 380
                    Layout.alignment: Qt.AlignHCenter
                }
                Item { Layout.fillHeight: true; Layout.fillWidth: true }
            }

            Repeater {
                model: page.groups
                ColumnLayout {
                    id: group
                    required property var modelData
                    spacing: 16
                    Layout.fillWidth: true
                    Label { text: group.modelData.title + " · " + group.modelData.rows.length; font.bold: true; textFormat: Text.PlainText }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: rowsColumn.implicitHeight + 20
                        radius: 10
                        color: palette.alternateBase
                        ColumnLayout {
                            id: rowsColumn
                            x: 14
                            y: 10
                            width: parent.width - 28
                            spacing: 4
                            Repeater {
                                model: group.modelData.rows
                                RowLayout {
                                    id: row
                                    required property var modelData
                                    readonly property bool own: modelData.source === "Flatpak" || modelData.source === "AppImage"
                                    spacing: 12
                                    Layout.fillWidth: true
                                    Layout.topMargin: 6
                                    Layout.bottomMargin: 6
                                    AppIcon {
                                        // An AppImage the installed list does not know shows the package icon
                                        icon: row.modelData.source === "AppImage" && !page.installedIds.has(row.modelData.id) ? "package-x-generic" : row.modelData.icon
                                        fallback: row.own ? "application-x-executable" : "package-x-generic"
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 40
                                    }
                                    ColumnLayout {
                                        spacing: 2
                                        Layout.fillWidth: true
                                        Label { text: row.modelData.name; font.bold: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true }
                                        Label {
                                            readonly property bool plain: row.modelData.version.startsWith("Version ")
                                            text: row.modelData.version
                                            textFormat: plain ? Text.PlainText : Text.RichText
                                            color: palette.placeholderText
                                            wrapMode: Text.Wrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Button {
                                        visible: row.modelData.action !== "none"
                                        enabled: row.modelData.action === "update"
                                        text: "Update"
                                        icon.name: "system-software-update"
                                        Layout.minimumHeight: 34
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        ToolTip.visible: hovered && !enabled
                                        ToolTip.text: "This package updates with System Update, because Arch does not support updating one package alone."
                                        onClicked: {
                                            const op = page.updates.updateOneJob(row.modelData.id, row.modelData.source)
                                            if (op !== "") page.window.queueJob(JSON.parse(op).lane, JSON.parse(op))
                                        }
                                    }
                                    SourceBadge { source: row.modelData.source; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                                }
                            }
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true; visible: page.groups.length > 0 }
            Item { id: footerSlot; Layout.fillWidth: true; Layout.preferredHeight: 44 }
        }
    }

    InstalledStickyBar {
        id: sticky
        flickable: flick
        slot: slot
        scrollBarWidth: scrollBar.visible ? scrollBar.width : 0
        Label { text: "Updates"; font.pointSize: 23; font.bold: true; textFormat: Text.PlainText }
        // Same place as the search row of Installed: what the last check found, and the button to run it again.
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            TextField {  // Looks like the search bar of Installed
                readOnly: true
                focusPolicy: Qt.NoFocus
                leftPadding: 8
                text: page.updates.loading ? "Checking…" : page.updates.checked ? "Checked at " + page.updates.checkedAt : ""
                Layout.fillWidth: true
                Layout.minimumHeight: 38
            }
            Repeater {
                model: [["package", "Find orphan packages"], ["refresh", "Check for updates"]]
                Button {
                    required property var modelData
                    required property int index
                    enabled: index === 0 || !page.updates.loading
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    Accessible.name: modelData[1]
                    ToolTip.visible: hovered
                    ToolTip.text: modelData[1]
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    contentItem: Item { Lucide { anchors.centerIn: parent; width: 16; height: 16; name: modelData[0]; color: palette.buttonText } }
                    onClicked: index === 0 ? page.flow.showOrphans(false) : page.updates.check(page.store.installed)
                }
            }
        }
    }

    // The mirror of the sticky header: System Update stays 24 px above the bottom of the window while
    // the page is longer than it, on the window colour with a Mid line above.
    Item {
        id: footer
        readonly property real home: body.y + footerSlot.y - flick.contentY
        readonly property bool stuck: home + 44 + 24 > page.height
        x: 0
        width: flick.width
        y: stuck ? page.height - 44 - 24 - 8 : home
        height: stuck ? 44 + 8 + 24 : 44
        Rectangle {
            anchors.fill: parent
            visible: footer.stuck
            color: palette.window
            Rectangle { width: parent.width; height: 1; color: palette.mid }
        }
        Button {
            id: systemUpdate
            x: body.x
            y: footer.stuck ? 8 : 0
            width: body.width
            height: 44
            focusPolicy: Qt.StrongFocus
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            ToolTip.visible: hovered
            ToolTip.text: "Update the system, AUR packages, Flatpak apps and AppImages in one run, then list orphan packages"
            onClicked: page.confirmUpdate()
            contentItem: Item {
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    Lucide { width: 16; height: 16; name: "package"; color: page.pending ? palette.active.highlightedText : palette.windowText; anchors.verticalCenter: parent.verticalCenter }
                    Label { text: "System Update"; color: page.pending ? palette.active.highlightedText : palette.buttonText; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            // The accent colour while there is something for it to do, drawn over the style's frame (swapping the background
            // out and back left the button with no frame). Active colours also in an inactive window.
            Rectangle {
                visible: page.pending
                anchors.fill: parent
                z: -0.5  // Above the style's background (-1), below the label
                radius: 6
                color: palette.active.highlight
                border.width: systemUpdate.activeFocus ? 2 : 1
                border.color: systemUpdate.hovered || systemUpdate.activeFocus ? palette.active.highlightedText : palette.active.highlight
            }
        }
    }
}
