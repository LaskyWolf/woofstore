import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The shared application list of Explore's directory and Installed: one rounded AlternateBase panel with a
// 14 px inset; rows with a 40 px icon, the name and summary, where the app comes from and its actions.
//
// Each entry: {name, summary, icon, fallback?, metadata ("Repo · Flatpak · AUR" or free text), badge (a source for SourceBadge,
// installed rows), installed (bool), reinstall (bool, Reinstall available), removable (bool)} plus anything the page needs back.
Rectangle {
    id: list
    property var model: []
    // Optional: {label(entry) -> "Install" / "Open" / "Installing…", busy(entry) -> bool}
    property var operations: null
    signal activated(var entry)
    signal install(var entry)
    signal reinstall(var entry)
    signal remove(var entry)

    visible: model.length > 0
    radius: 10
    color: palette.alternateBase
    implicitHeight: rows.implicitHeight + 28

    Button { id: reinstallSize; visible: false; text: "Reinstall"; icon.name: "view-refresh" }  // Measures Reinstall for Install

    Column {
        id: rows
        x: 14
        y: 14
        width: list.width - 28
        Repeater {
            model: list.model
            AbstractButton {
                id: row
                required property var modelData
                readonly property var entry: modelData
                readonly property var kinds: (entry.metadata ?? "").split(" · ").filter(k => k !== "")
                readonly property bool onlySources: kinds.length > 0 && kinds.every(k => k === "Repo" || k === "Flatpak" || k === "AUR")
                readonly property bool busy: list.operations ? list.operations.busy(entry) : false
                width: rows.width
                implicitHeight: Math.max(56, content.implicitHeight)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: entry.name + ". " + (entry.summary ?? "") + ". " + (entry.metadata ?? "")
                // Rows add only 8 px horizontal margins inside hover and selection backgrounds
                leftPadding: 8
                rightPadding: 8
                topPadding: 0
                bottomPadding: 0
                onClicked: list.activated(entry)
                Keys.onReturnPressed: list.activated(entry)
                Keys.onEnterPressed: list.activated(entry)
                background: Rectangle {
                    radius: 8
                    color: row.activeFocus ? row.palette.midlight : row.hovered ? row.palette.button : "transparent"
                }
                contentItem: RowLayout {
                    id: content
                    spacing: 12
                    AppIcon {
                        icon: row.entry.icon ?? ""
                        fallback: row.entry.fallback ?? "application-x-executable"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        Layout.alignment: Qt.AlignVCenter
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 5
                        Label { text: row.entry.name; font.bold: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true }
                        Label {
                            visible: text !== ""
                            text: row.entry.summary ?? ""
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            ToolTip.visible: truncated && summaryHover.hovered
                            ToolTip.text: text
                            HoverHandler { id: summaryHover }
                        }
                    }
                    // Where the app can be installed from, as the same marks the Installed page uses
                    Repeater {
                        model: !row.entry.badge && row.onlySources ? row.kinds : []
                        SourceBadge {
                            required property string modelData
                            source: modelData === "Repo" ? "Arch" : modelData
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            ToolTip.visible: markHover.hovered
                            ToolTip.text: modelData === "Repo" ? "Arch repositories" : modelData
                            HoverHandler { id: markHover }
                        }
                    }
                    Label {
                        visible: !row.entry.badge && !row.onlySources && text !== ""
                        text: row.entry.metadata ?? ""
                        font.pointSize: 10
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignRight
                        Layout.maximumWidth: 180
                        Layout.alignment: Qt.AlignVCenter
                    }
                    // Installed: Reinstall, Remove (icon only), then where the app came from. Unavailable actions stay, greyed out.
                    Button {
                        visible: row.entry.installed === true
                        text: row.busy && list.operations ? list.operations.label(row.entry) : "Reinstall"
                        icon.name: "view-refresh"
                        enabled: row.entry.reinstall === true && !row.busy
                        opacity: row.entry.reinstall === true ? 1 : 0.4
                        Layout.minimumHeight: 32
                        ToolTip.visible: hovered && row.entry.reinstall !== true
                        ToolTip.text: "Reinstall isn't available for this app."
                        onClicked: list.reinstall(row.entry)
                    }
                    Button {
                        visible: row.entry.installed === true
                        icon.name: "edit-delete"
                        enabled: row.entry.removable === true && !row.busy
                        opacity: row.entry.removable === true ? 1 : 0.4
                        Layout.preferredWidth: 36
                        Layout.minimumHeight: 32
                        Accessible.name: "Remove " + row.entry.name
                        ToolTip.visible: hovered
                        ToolTip.text: row.entry.removable === true ? "Remove" : "This app has no package to remove here."
                        onClicked: list.remove(row.entry)
                    }
                    SourceBadge {
                        visible: !!row.entry.badge
                        source: row.entry.badge ?? ""
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        Layout.alignment: Qt.AlignVCenter
                        ToolTip.visible: badgeHover.hovered
                        ToolTip.text: row.entry.metadata ?? ""
                        HoverHandler { id: badgeHover }
                    }
                    Button {  // Not installed: Install, or Open once it is
                        id: installButton
                        visible: row.entry.installed !== true && list.operations !== null
                        readonly property string label: list.operations ? list.operations.label(row.entry) : "Install"
                        enabled: !row.busy
                        Layout.minimumHeight: 32
                        Layout.preferredWidth: Math.max(implicitWidth, reinstallSize.implicitWidth)  // As wide as Installed's Reinstall
                        Accessible.name: label + " " + row.entry.name
                        onClicked: list.install(row.entry)
                        contentItem: Item {  // Icon and label centred, as in Reinstall
                            implicitWidth: installContent.implicitWidth
                            implicitHeight: installContent.implicitHeight
                            RowLayout {
                                id: installContent
                                anchors.centerIn: parent
                                spacing: 6
                                Lucide {
                                    visible: installButton.label === "Open"
                                    name: "external-link"
                                    color: installButton.palette.buttonText
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                }
                                AppIcon {
                                    visible: installButton.label !== "Open"
                                    icon: "system-software-install"
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                }
                                Label { text: installButton.label; color: installButton.palette.buttonText }
                            }
                        }
                    }
                }
            }
        }
    }
}
