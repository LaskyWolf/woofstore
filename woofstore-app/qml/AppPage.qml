import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The app page: Back, Install/Open, Remove and the source menu on top; then the header (96 px icon,
// name, summary), the description with Show more, the screenshot strip and the Latest changelog, Ratings, Permissions and
// Links panels.
Item {
    id: details
    required property var window
    property Flickable flickable: flick
    property string appKey: ""
    property string platform: ""
    property string backLabel: "Explore"
    signal back()

    readonly property var catalog: window.catalog
    readonly property var app: { catalog.revision; return appKey === "" ? ({}) : JSON.parse(catalog.details(appKey)) }
    readonly property bool known: app.key !== undefined
    readonly property var extras: { const value = JSON.parse(catalog.extras || "{}"); return value.key === appKey ? value : ({}) }
    readonly property var labels: (app.packages ?? []).map(p => p.label)
    // The source button's text; history restores the choice by this text
    readonly property string chosen: labels.includes(platform) ? platform : labels.length > 0 ? (app.chosen || labels[0]) : (platform || "Unknown source")
    readonly property real motion: catalog.animationFactor()
    property bool expanded: false
    property bool animateDescription: false

    function showApp(key, source) {
        appKey = key
        platform = source ?? ""
        expanded = false
        animateDescription = false
        gallery.reset()
        flick.contentY = 0
        if (known) catalog.loadExtras(appKey)
    }
    // Ratings once the catalogue knows the app; permissions again when it gets installed or removed
    readonly property string extrasInputs: known ? appKey + String(app.installed) : ""
    onExtrasInputsChanged: if (known) catalog.loadExtras(appKey)

    // Screenshot state "panels": scrolled to the panels below the screenshots
    Timer {
        running: String(details.window.preview ?? "").split(",").includes("panels") && details.known
        interval: 4000
        onTriggered: flick.contentY = Math.max(0, flick.contentHeight - flick.height)
    }
    function shortDate(text) {
        const parts = String(text).split("-").map(Number)
        if (parts.length < 3 || parts.some(isNaN)) return ""
        return new Date(parts[0], parts[1] - 1, parts[2]).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    function number(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
    function remove() {
        const plan = JSON.parse(catalog.removePlan(appKey))
        if (plan.error) return
        removeWindow.ask(plan.name, [{ text: plan.option, checked: true }], plan.leftovers)
    }

    AppOperations { id: ops; store: details.window.store; catalog: details.window.catalog; window: details.window }
    RemoveWindow {
        id: removeWindow
        transientParent: details.Window.window
        onAccepted: (options, paths) => {
            const id = ops.run(details.appKey, "Remove", "", options[0])
            if (id > 0 && paths.length > 0) details.catalog.rememberLeftovers(id, JSON.stringify(paths))
        }
    }
    FontMetrics { id: metrics }

    // A neutral surface: WindowText at alpha 14, 12 px radius, 24 px padding
    component Panel: Rectangle {
        default property alias content: panelColumn.data
        property alias spacing: panelColumn.spacing
        Layout.fillWidth: true
        implicitHeight: panelColumn.implicitHeight + 48
        radius: 12
        color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 14 / 255)
        ColumnLayout { id: panelColumn; x: 24; y: 24; width: parent.width - 48; spacing: 12 }
    }
    component Heading: Label { textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pointSize: 16; font.bold: true; Layout.fillWidth: true }
    component Copy: TextEdit {
        readOnly: true
        selectByMouse: true
        wrapMode: Text.Wrap
        textFormat: TextEdit.PlainText
        color: palette.windowText
        selectionColor: palette.highlight
        selectedTextColor: palette.highlightedText
        Layout.fillWidth: true
    }

    Flickable {
        id: flick
        PageWheel { target: flick }  // Mouse wheel as in Plasma; trackpad with inertia
        anchors.fill: parent
        contentWidth: width
        contentHeight: outer.implicitHeight + 16 + 32
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton  // Wheel and scroll bar only, as a QScrollArea
        clip: true
        ScrollBar.vertical: ScrollBar { id: bar }

        ColumnLayout {
            id: outer
            x: 28
            y: 16
            width: flick.width - 56 - bar.width
            spacing: 24

            // ── Toolbar ──
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                ToolButton {
                    flat: false
                    icon.name: "go-previous"
                    text: details.backLabel
                    display: AbstractButton.TextBesideIcon
                    Layout.minimumHeight: 36
                    Accessible.name: "Back to " + details.backLabel
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: "Back to " + details.backLabel + " (Alt+Left)"
                    onClicked: details.back()
                }
                Item { Layout.fillWidth: true }
                Button {
                    id: install
                    readonly property string label: details.known ? ops.label(details.app) : "Install"
                    visible: !details.appKey.startsWith("installed:")  // Installed outside the catalogue: nothing to install or switch
                    enabled: details.known && !ops.busy(details.app)
                    Layout.preferredHeight: 36
                    Layout.minimumWidth: 112
                    Accessible.name: label
                    onClicked: ops.primary(details.app, details.chosen)
                    contentItem: Item {
                        implicitWidth: installRow.implicitWidth
                        implicitHeight: installRow.implicitHeight
                        Row {
                            id: installRow
                            anchors.centerIn: parent
                            spacing: 6
                            Lucide {
                                visible: install.label === "Open"  // Opening is not installing
                                name: "external-link"
                                width: 20; height: 20
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            AppIcon {
                                visible: install.label !== "Open"
                                icon: "system-software-install"
                                width: 16; height: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Label { text: install.label; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
                Button {
                    visible: details.known && details.app.installed === true
                    text: details.known ? ops.removeLabel(details.app) : "Remove"
                    icon.name: "edit-delete"
                    enabled: details.known && !ops.busy(details.app)
                    Layout.preferredHeight: 36
                    Layout.minimumWidth: 112
                    onClicked: details.remove()
                }
                // The source menu: clicking anywhere on it opens the menu; with one source there is nothing to choose
                ToolButton {
                    id: sourceButton
                    flat: false
                    icon.name: "package-x-generic"
                    text: details.chosen
                    display: AbstractButton.TextBesideIcon
                    enabled: details.labels.length > 1
                    // Room for the drop-down arrow, drawn here since a Qt Quick ToolButton has no menu-button mode
                    Layout.preferredWidth: implicitWidth + (details.labels.length > 1 ? 30 : 0)
                    Layout.preferredHeight: 36
                    Accessible.name: "Choose source"
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: "Choose software source"
                    onClicked: sourceMenu.popup(sourceButton, 0, sourceButton.height)
                    AppIcon {
                        visible: details.labels.length > 1
                        icon: "arrow-down"
                        width: 16; height: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Menu {
                        id: sourceMenu
                        popupType: Popup.Native
                        Instantiator {
                            model: details.app.packages ?? []
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.text
                                checkable: true
                                checked: modelData.label === details.chosen
                                onTriggered: details.platform = modelData.label
                            }
                            onObjectAdded: (index, item) => sourceMenu.insertItem(index, item)
                            onObjectRemoved: (index, item) => sourceMenu.removeItem(item)
                        }
                    }
                }
            }

            ColumnLayout {
                id: content
                spacing: 24
                Layout.fillWidth: true

                // ── Header ──
                RowLayout {
                    spacing: 22
                    Layout.fillWidth: true
                    AppIcon {
                        icon: details.app.icon ?? ""
                        fallback: "applications-other"
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 96
                        Layout.alignment: Qt.AlignTop
                    }
                    ColumnLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Label {
                            text: details.known ? details.app.name : details.catalog.ready ? "This app is not in the catalogue" : "Loading apps…"
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.pointSize: 28
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            text: details.app.summary ?? ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.pointSize: 12
                            Layout.fillWidth: true
                        }
                    }
                }

                // ── Description: six lines, then Show more ──
                ColumnLayout {
                    visible: details.known && !details.appKey.startsWith("installed:")
                    spacing: 8
                    Layout.fillWidth: true
                    Item {
                        readonly property int collapsed: metrics.lineSpacing * 6
                        readonly property int full: Math.ceil(description.implicitHeight)
                        clip: true
                        Layout.fillWidth: true
                        Layout.preferredHeight: details.expanded ? full : Math.min(full, collapsed)
                        Behavior on Layout.preferredHeight {
                            enabled: details.animateDescription && details.motion > 0
                            NumberAnimation { duration: Math.round(240 * details.motion); easing.type: Easing.OutCubic }
                        }
                        Copy { id: description; width: parent.width; text: details.app.description ?? "" }
                    }
                    ToolButton {
                        visible: description.implicitHeight > metrics.lineSpacing * 6
                        flat: false
                        icon.name: details.expanded ? "arrow-up" : "arrow-down"
                        text: details.expanded ? "Show less" : "Show more"
                        display: AbstractButton.TextBesideIcon
                        Layout.minimumHeight: 36
                        Accessible.name: details.expanded ? "Collapse app description" : "Expand app description"
                        onClicked: { details.animateDescription = true; details.expanded = !details.expanded }
                    }
                }

                // ── Screenshots ──
                AppPageGallery {
                    id: gallery
                    urls: details.app.screenshots ?? []
                    duration: Math.round(300 * details.motion)
                    visible: images.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                }
                Item {
                    visible: gallery.visible && gallery.images.length > 1
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 4
                        Repeater {
                            model: gallery.images.length
                            ExploreDot {
                                required property int index
                                checked: index === gallery.current
                                duration: Math.round(280 * details.motion)
                                Accessible.name: "Screenshot " + (index + 1) + " of " + gallery.images.length
                                ToolTip.text: Accessible.name
                                onClicked: gallery.select(index, true)
                            }
                        }
                    }
                }

                // ── Latest changelog; older versions stay folded until asked for ──
                Panel {
                    visible: (details.app.notes ?? "") !== ""
                    Heading { text: "Latest changelog" }
                    Label {
                        readonly property string date: details.shortDate(details.app.releaseDate ?? "")
                        text: [details.app.releaseVersion ?? "", date].filter(t => t !== "").join(" · ")
                        visible: text !== ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Copy { text: details.app.notes ?? "" }
                    ColumnLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        Repeater {
                            model: details.app.older ?? []
                            ColumnLayout {
                                id: release
                                required property var modelData
                                readonly property string label: [modelData.version, details.shortDate(modelData.date ?? "")].filter(t => t !== "").join(" · ")
                                spacing: 4
                                Layout.fillWidth: true
                                AbstractButton {
                                    id: header
                                    checkable: true
                                    hoverEnabled: true
                                    leftPadding: 6
                                    rightPadding: 6
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: 34
                                    Accessible.name: "Release " + release.label + " notes"
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    background: Rectangle { radius: 6; color: header.hovered ? header.palette.button : "transparent" }
                                    contentItem: Row {
                                        spacing: 4
                                        AppIcon { icon: header.checked ? "arrow-down" : "arrow-right"; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter }
                                        Label { text: release.label; textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                }
                                Copy {
                                    visible: header.checked
                                    text: release.modelData.notes
                                    Layout.leftMargin: 24
                                    Layout.bottomMargin: 8
                                }
                            }
                        }
                    }
                }

                // ── Ratings or popularity: each number names its source; stars come only from ODRS reviews ──
                Panel {
                    id: ratingsPanel
                    readonly property var ratings: details.extras.ratings ?? null
                    readonly property bool hasBars: !!ratings && ratings.reviews > 0
                    visible: !!ratings
                    spacing: 8
                    Heading { text: ratingsPanel.hasBars ? "Ratings" : "Popularity" }
                    AppPageRatings {
                        visible: ratingsPanel.hasBars
                        ratings: ratingsPanel.ratings
                        duration: Math.round(450 * details.motion)
                        Layout.fillWidth: true
                    }
                    Label { visible: ratingsPanel.hasBars; text: "Reviews from GNOME ODRS"; font.pointSize: 9 }
                    RowLayout {
                        visible: !!ratingsPanel.ratings && ratingsPanel.ratings.tiles.length > 0
                        spacing: 32
                        Layout.fillWidth: true
                        Layout.topMargin: ratingsPanel.hasBars ? 12 : 0
                        Repeater {
                            model: ratingsPanel.ratings ? ratingsPanel.ratings.tiles : []
                            ColumnLayout {
                                required property var modelData
                                spacing: 0
                                Layout.alignment: Qt.AlignTop
                                Label { text: modelData.value; textFormat: Text.PlainText; font.pointSize: 24; font.bold: true }
                                Label { text: modelData.caption; textFormat: Text.PlainText; font.pointSize: 9 }
                            }
                        }
                        Item { Layout.fillWidth: !chart.visible }
                        ColumnLayout {
                            id: chart
                            visible: !!ratingsPanel.ratings && ratingsPanel.ratings.daily.length > 1
                            spacing: 2
                            Layout.fillWidth: true
                            AppPageSparkline {
                                daily: ratingsPanel.ratings ? ratingsPanel.ratings.daily : []
                                duration: Math.round(600 * details.motion)
                                Layout.fillWidth: true
                                Layout.minimumWidth: 240
                                Layout.preferredHeight: 96
                            }
                            Label { text: "Installs per day, last " + (ratingsPanel.ratings ? ratingsPanel.ratings.daily.length : 0) + " days"; font.pointSize: 9 }
                        }
                    }
                }

                // ── Permissions: from the installed Flatpak manifest; other sources have no sandbox to describe ──
                Panel {
                    id: permissionsPanel
                    readonly property var permissions: details.extras.permissions ?? null
                    visible: !!permissions
                    spacing: 18
                    Heading { text: "Permissions" }
                    Label { text: permissionsPanel.permissions ? permissionsPanel.permissions.note : ""; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Repeater {
                        model: permissionsPanel.permissions ? permissionsPanel.permissions.rows : []
                        RowLayout {
                            required property var modelData
                            spacing: 14
                            Layout.fillWidth: true
                            Item {
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 24
                                Layout.alignment: Qt.AlignTop
                                AppIcon { icon: modelData.icon; width: 24; height: 24 }
                            }
                            ColumnLayout {
                                spacing: 5
                                Layout.fillWidth: true
                                Label { text: modelData.name; textFormat: Text.PlainText; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                Label { text: modelData.value; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                        }
                    }
                }

                // ── Links ──
                Panel {
                    visible: (details.app.links ?? []).length > 0
                    Heading { text: "Links" }
                    Repeater {
                        model: details.app.links ?? []
                        RowLayout {
                            required property var modelData
                            spacing: 14
                            Layout.fillWidth: true
                            // A QCommandLinkButton: bold title, the address in 9 pt below
                            Button {
                                id: link
                                flat: true
                                Layout.fillWidth: true
                                Layout.minimumHeight: 56
                                Accessible.name: modelData.title + ": " + modelData.url
                                ToolTip.visible: hovered
                                ToolTip.delay: 700
                                ToolTip.text: modelData.url
                                onClicked: Qt.openUrlExternally(modelData.url)
                                // The icon sits level with the title, and the block is centred in the button's height
                                contentItem: Item {
                                    implicitWidth: linkRow.implicitWidth
                                    implicitHeight: linkRow.implicitHeight
                                    RowLayout {
                                        id: linkRow
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8
                                        AppIcon { icon: link.modelData.icon; Layout.preferredWidth: 20; Layout.preferredHeight: 20; Layout.alignment: Qt.AlignTop; Layout.topMargin: Math.max(0, (linkTitle.implicitHeight - 20) / 2) }
                                        ColumnLayout {
                                            spacing: 2
                                            Layout.fillWidth: true
                                            Layout.alignment: Qt.AlignTop
                                            Label { id: linkTitle; text: link.modelData.title; textFormat: Text.PlainText; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Label { text: link.modelData.url; textFormat: Text.PlainText; font.pointSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                                        }
                                    }
                                }
                                readonly property var modelData: parent.modelData
                            }
                            ToolButton {
                                icon.name: "window-new"
                                Accessible.name: "Open " + modelData.title
                                ToolTip.visible: hovered
                                ToolTip.delay: 700
                                ToolTip.text: "Open in browser"
                                onClicked: Qt.openUrlExternally(modelData.url)
                            }
                        }
                    }
                }
            }
        }
    }
}
