import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

// Installed: search, a source filter and Refresh; cards that add AppImages and
// Flatpak files; every installed app A–Z with Reinstall, Remove and where it came from.
Item {
    id: page
    required property var window
    property Flickable flickable: flick
    readonly property var store: window.store
    readonly property var updates: window.updates

    readonly property var apps: JSON.parse(store.installed)
    property bool loaded: false  // "Loading installed software…" until the first list arrives
    Connections {
        target: page.store
        function onInstalledLoadingChanged() { if (!page.store.installedLoading) page.loaded = true }
    }
    readonly property var storeUpdate: JSON.parse(updates.entries).find(e => e.source === "WoofStore") ?? null
    readonly property var shown: {
        const query = search.text.trim().toLowerCase(), source = filter.currentText
        const key = a => a.name.toLowerCase()
        return apps.filter(a => (source === "All sources" || a.source === source)
                                && (a.name.toLowerCase().includes(query) || a.id.toLowerCase().includes(query)))
                   .sort((a, b) => key(a) < key(b) ? -1 : key(a) > key(b) ? 1 : 0)
    }
    readonly property var entries: shown.map(app => {
        const managed = app.source !== "Other"  // Only apps with a package manager or one of our files can be removed here
        // One quiet line: what the app is, or its version when its publisher says nothing.
        let line = app.summary !== "" ? app.summary : app.version !== "" ? "Version " + app.version : ""
        if (app.id === "woofstore" && storeUpdate) line = "Version " + storeUpdate.to + " is available on GitHub. Update it in Updates."
        return {
            name: app.name, summary: line, icon: app.icon, installed: true, badge: app.source,
            metadata: [app.source, app.remote, app.scope, app.version].filter(t => t !== "").join(" · "),
            reinstall: managed && app.source !== "AppImage", removable: managed, app: app
        }
    })

    // What a row's Reinstall shows while its operation waits or runs
    readonly property var operations: ({
        job: name => page.window.activity.find(a => (a.state === "running" || a.state === "waiting") && (a.title === "Reinstall " + name || a.title === "Remove " + name)),
        busy: entry => page.operations.job(entry.name) !== undefined,
        label: entry => { const job = page.operations.job(entry.name); return job && job.title.startsWith("Remove") ? "Removing…" : "Reinstalling…" }
    })

    // The app's details: its catalogue page, or what the system knows about an app the catalogue does not list
    function openApp(app) {
        const key = window.catalog.installedKey(app.id, app.source)
        if (key !== "") window.openDetails(key, app.source === "Flatpak" ? "Flatpak · " + app.remote : "")
        else window.openDetails("installed:" + app.id, app.source === "Other" ? "" : app.source)
    }
    function reinstall(app) {
        const op = JSON.parse(updates.reinstallJob(JSON.stringify(app)))
        if (op.error) window.dialogs.warning("Cannot reinstall", op.error)
        else if (op.aur !== undefined) window.reviewAur(op.aur, app.name, true)
        else window.queueJob(op.lane, op)
    }
    property var removing: null
    function remove(app) {
        if (app.source === "AppImage") {  // A file, not a package
            if (!window.dialogs.question("Remove " + app.name, "This deletes the AppImage file:\n" + app.path)) return
            const problem = updates.removeAppImage(JSON.stringify(app))
            if (problem !== "") window.dialogs.warning("Cannot remove " + app.name, problem)
            store.loadInstalled()
            return
        }
        const option = updates.removalOption(JSON.stringify(app))
        if (option === "") return
        removing = app
        removeWindow.ask(app.name, [{ text: option, checked: true }], JSON.parse(updates.leftovers(JSON.stringify(app))))
    }
    RemoveWindow {
        id: removeWindow
        transientParent: page.Window.window
        onAccepted: (options, paths) => {
            const app = page.removing
            const op = JSON.parse(page.updates.removalJob(JSON.stringify(app), options[0]))
            if (op.error) { page.window.dialogs.warning("Cannot remove " + app.name, op.error); return }
            page.updates.rememberLeftovers(page.window.queueJob(op.lane, op), app.name, JSON.stringify(paths))
        }
    }

    // Files: local, with the extension of their kind
    function accepts(kind, url) {
        const file = String(url)
        return file.startsWith("file://") && (kind === "AppImage" ? /\.appimage$/i.test(file) : /\.flatpak(ref)?$/i.test(file))
    }
    // The same progress window as every other install; AppImages are copied by a helper run of this program.
    function installFiles(kind, urls) {
        const files = urls.filter(u => accepts(kind, u)).map(u => decodeURIComponent(String(u).slice(7)))
        if (files.length === 0) {
            window.dialogs.information("Add " + kind, kind === "AppImage" ? "Choose a .AppImage file." : "Choose a .flatpak or .flatpakref file.")
            return
        }
        if (kind === "AppImage") {
            window.queueJob("appimage", JSON.parse(store.importAppImagesJob(JSON.stringify(files))))
            return
        }
        // Installing a file needs an administrator (polkit allows it only after a password), so one pkexec session installs all of them.
        const name = files.length === 1 ? files[0].split("/").pop().replace(/\.[^.]*$/, "") : files.length + " files"
        window.queueJob("flatpak", {
            title: "Install " + name,
            program: "bash",
            args: ["-c", "pkexec \"$WOOFSTORE_ROOT\" flatpak-files \"$@\"", "woof-store-flatpak-files"].concat(files)
        })
    }
    FileDialog {
        id: chooser
        property string kind: "AppImage"
        title: "Add " + kind
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        nameFilters: kind === "AppImage" ? ["AppImages (*.AppImage *.appimage)"] : ["Flatpak files (*.flatpak *.flatpakref)"]
        onAccepted: page.installFiles(kind, selectedFiles.map(String))
    }

    Flickable {
        id: flick
        PageWheel { target: flick }  // Mouse wheel as in Plasma; trackpad with inertia
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.implicitHeight + 16 + 24
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { id: scrollBar }

        ColumnLayout {
            id: body
            x: 20
            y: 16
            width: flick.width - 40 - (scrollBar.visible ? scrollBar.width : 0)
            spacing: 16

            Item { id: slot; Layout.fillWidth: true; Layout.preferredHeight: sticky.slotHeight }
            Label {
                text: page.loaded ? "Installed apps · A–Z · " + page.shown.length : "Loading installed software…"
                textFormat: Text.PlainText
                font.bold: true
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            AppList {
                Layout.fillWidth: true
                model: page.entries
                operations: page.operations
                Accessible.name: "Installed apps, alphabetically sorted"
                onActivated: entry => page.openApp(entry.app)
                onReinstall: entry => page.reinstall(entry.app)
                onRemove: entry => page.remove(entry.app)
            }
            ColumnLayout {  // Nothing matches the search or the source
                visible: page.loaded && page.shown.length === 0
                Layout.fillWidth: true
                Layout.minimumHeight: 300
                spacing: 10
                Item { Layout.fillHeight: true; Layout.fillWidth: true }
                Lucide { name: "search-x"; Layout.preferredWidth: 72; Layout.preferredHeight: 72; weight: 1.4; color: palette.placeholderText; Layout.alignment: Qt.AlignHCenter }
                Label { text: "No installed app matches"; font.pointSize: 18; font.bold: true; textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter }
                Label {
                    text: "Nothing here by that name. Maybe it never made it onto your system."
                    textFormat: Text.PlainText
                    color: palette.placeholderText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.minimumWidth: 480
                    Layout.alignment: Qt.AlignHCenter
                }
                Item { Layout.fillHeight: true; Layout.fillWidth: true }
            }
        }
    }

    InstalledStickyBar {
        id: sticky
        flickable: flick
        slot: slot
        scrollBarWidth: scrollBar.visible ? scrollBar.width : 0
        Label { text: "Installed"; font.pointSize: 23; font.bold: true; textFormat: Text.PlainText }
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            TextField {
                id: search
                placeholderText: "Search installed software"
                Accessible.name: "Search installed software"
                Layout.fillWidth: true
                Layout.minimumHeight: 38
                leftPadding: 35
                rightPadding: clear.visible ? 30 : 8
                AppIcon { icon: "edit-find"; x: 6; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16 }
                AbstractButton {
                    id: clear
                    visible: search.text !== ""
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    Accessible.name: "Clear"
                    contentItem: AppIcon { icon: "edit-clear-locationbar-rtl"; width: 16; height: 16 }
                    onClicked: search.clear()
                }
            }
            ComboBox {
                id: filter
                model: ["All sources", "Flatpak", "Arch", "AUR / local", "AppImage", "Other"]
                Accessible.name: "Installed software source"
                Layout.minimumHeight: 38
                Layout.preferredWidth: 102  // The width Breeze gives the QComboBox for these items
            }
            Button {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                Accessible.name: "Refresh installed software"
                ToolTip.visible: hovered
                ToolTip.text: "Refresh the list"
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                // QPushButton draws its 20 px Lucide icon at the default 16 px icon size
                contentItem: Item { Lucide { anchors.centerIn: parent; width: 16; height: 16; name: "refresh"; color: palette.buttonText } }
                onClicked: page.store.loadInstalled()
            }
        }
        RowLayout {
            spacing: 16
            Layout.fillWidth: true
            Repeater {
                model: ["AppImage", "Flatpak"]
                InstalledDropCard {
                    required property string modelData
                    kind: modelData
                    text: "Add " + modelData
                    caption: modelData === "AppImage" ? "Drop a file here or click to choose" : "Drop a .flatpak or .flatpakref file here"
                    accepts: url => page.accepts(modelData, url)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onDropped: urls => page.installFiles(modelData, urls)
                    onClicked: { chooser.kind = modelData; chooser.open() }
                }
            }
        }
    }
}
