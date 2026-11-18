import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What follows an operation: a new check after an update, and after a System Update the service restarts,
// the store's own restart and the orphan packages. Also the orphan packages window.
Item {
    id: flow
    required property var store
    required property var updates
    required property var window

    property bool orphansWanted: false
    property bool orphansOnlyIfAny: false

    function queue(op) { return window.queueJob(op.lane, op) }

    function showOrphans(onlyIfAny) {
        orphansOnlyIfAny = onlyIfAny
        orphansWanted = true
        updates.findOrphans()
    }

    // Restarts only when this process runs files the upgrade replaced; otherwise it checks again and lists orphans.
    function restartAfterUpgrade() {
        if (!updates.mustRestart()) {
            store.loadInstalled()
            updates.check(store.installed)
            showOrphans(true)
            return
        }
        window.dialogs.information("System Update finished", "WoofStore restarts now to load the updated libraries.")
        window.settings.setValue("orphansAfterRestart", true)
        window.settings.setValue("pageAfterRestart", 2)  // Back to Updates
        if (updates.restartStore()) Qt.quit()
    }

    // The upgrade replaced files that some services still run old copies of. The person picks which to restart, then the store restarts itself.
    function afterSystemUpgrade(services) {
        if (services.length === 0) restartAfterUpgrade()
        else servicesWindow.ask(services)
    }

    // After the store restarted itself: the orphans the upgrade left
    function resumeAfterRestart() {
        if (String(window.settings.value("orphansAfterRestart", false)) !== "true") return
        window.settings.setValue("orphansAfterRestart", false)
        showOrphans(true)
    }

    Connections {
        target: flow.store
        function onJobEnded(id, code) {
            const job = flow.window.activity.find(a => a.id === id)
            const answer = flow.updates.jobEnded(id, code, job ? job.title : "", flow.store.jobLog(id))
            if (answer === "") return
            const result = JSON.parse(answer)
            // ponytail: result.message (leftovers deleted after a removal) is not shown; the window has no status bar.
            if (result.systemDone) flow.window.settings.setValue("lastSystemUpdate", Qt.formatDate(new Date(), "yyyy-MM-dd"))
            switch (result.next) {
            case "services": flow.afterSystemUpgrade(result.services); break
            case "afterUpgrade": flow.restartAfterUpgrade(); break
            case "recheck": flow.updates.check(flow.store.installed); break
            }
        }
    }

    Connections {
        target: flow.updates
        function onOrphansFound() {
            if (!flow.orphansWanted) return
            flow.orphansWanted = false
            const orphans = JSON.parse(flow.updates.orphans)
            if (orphans.length > 0 || !flow.orphansOnlyIfAny) orphansWindow.ask(orphans)
        }
    }

    // Screenshots with sample data: "orphans" and "services" open those windows
    Connections {
        target: flow.window
        function onPreviewChanged() {
            const parts = flow.window.preview.split(",")
            if (parts.includes("orphans")) {
                orphansWindow.ask([{ name: "python-pyparsing", size: "1.1 MiB" }, { name: "libfoo", size: "0.4 MiB" },
                                   { name: "gst-plugins-bad-libs", size: "7.9 MiB" }, { name: "qt5-base", size: "58.3 MiB" }])
                flow.window.shotTarget = orphansWindow
            }
            if (parts.includes("services")) {
                servicesWindow.ask([{ name: "NetworkManager.service", risky: false }, { name: "dbus-broker.service", risky: true },
                                    { name: "sddm.service", risky: false }])
                flow.window.shotTarget = servicesWindow
            }
        }
    }

    // Services whose restart would end the session are listed but not ticked.
    Window {
        id: servicesWindow
        Shortcut { sequences: [StandardKey.Cancel]; onActivated: servicesWindow.close() }  // Escape, as a QDialog
        property var services: []
        property bool restarting: false
        title: "Restart services"
        flags: Qt.Dialog
        modality: Qt.WindowModal
        transientParent: flow.window
        color: palette.window
        width: servicesLayout.implicitWidth + 20
        height: servicesLayout.implicitHeight + 20
        minimumWidth: width
        minimumHeight: height
        function ask(found) {
            services = found
            restarting = false
            show(); raise(); requestActivate()
        }
        onClosing: if (!restarting) flow.restartAfterUpgrade()  // "Not now" and the window's close button
        ColumnLayout {
            id: servicesLayout
            x: 10
            y: 10
            width: servicesWindow.width - 20
            spacing: 12
            Label { text: "Restart services"; font.pointSize: 18; font.bold: true; textFormat: Text.PlainText }
            Label {
                text: "The upgrade replaced files these services are still running. Restart them to use the new versions. Services marked ⚠ can end your session or the network: a reboot is the safer way to apply them."
                color: palette.placeholderText
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                Layout.preferredWidth: 460
                Layout.fillWidth: true
            }
            UpdatesCheckList {
                id: serviceList
                model: servicesWindow.services.map(s => ({ text: (s.risky ? "⚠ " : "") + s.name, checked: !s.risky }))
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 180
            }
            RowLayout {
                Button { text: "Not now"; onClicked: servicesWindow.close() }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Restart selected"
                    onClicked: {
                        const chosen = servicesWindow.services.filter((s, i) => serviceList.checked[i]).map(s => s.name)
                        const op = chosen.length ? flow.updates.restartServicesJob(JSON.stringify(chosen)) : ""
                        if (op !== "" && flow.queue(JSON.parse(op)) > 0) {
                            flow.updates.restartAfterServices = true  // The store restarts after them, when it must
                            servicesWindow.restarting = true
                        }
                        servicesWindow.close()
                    }
                }
            }
        }
    }

    // Orphans: packages installed as dependencies that nothing needs any more. The person ticks the ones to remove.
    Window {
        id: orphansWindow
        Shortcut { sequences: [StandardKey.Cancel]; onActivated: orphansWindow.close() }  // Escape, as a QDialog
        property var orphans: []
        readonly property int chosen: orphanList.checked.filter(c => c).length
        title: "Orphan packages"
        flags: Qt.Dialog
        transientParent: flow.window
        color: palette.window
        width: orphansLayout.implicitWidth + 20
        height: orphansLayout.implicitHeight + 20
        minimumWidth: width
        minimumHeight: height
        function ask(found) {
            orphans = found
            show(); raise(); requestActivate()
        }
        ColumnLayout {
            id: orphansLayout
            x: 10
            y: 10
            width: orphansWindow.width - 20
            spacing: 12
            Label { text: "Orphan packages"; font.pointSize: 18; font.bold: true; textFormat: Text.PlainText }
            Label {
                text: orphansWindow.orphans.length === 0 ? "No orphan packages. Nothing to clean up."
                    : "These were installed to support other software and nothing needs them any more. Tick the ones to remove: their settings files in /etc go too (pacman -Rns)."
                color: palette.placeholderText
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                Layout.preferredWidth: 460
                Layout.fillWidth: true
            }
            UpdatesCheckList {
                id: orphanList
                visible: orphansWindow.orphans.length > 0
                model: orphansWindow.orphans.map(o => ({ text: o.size ? o.name + "  ·  " + o.size : o.name, checked: false }))
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 240
            }
            RowLayout {
                Button { text: "Close"; onClicked: orphansWindow.close() }
                Item { Layout.fillWidth: true }
                Button {
                    visible: orphansWindow.orphans.length > 0
                    text: orphansWindow.chosen === orphansWindow.orphans.length ? "Select none" : "Select all"
                    onClicked: orphanList.setAll(orphansWindow.chosen < orphansWindow.orphans.length)
                }
                Button {
                    visible: orphansWindow.orphans.length > 0
                    enabled: orphansWindow.chosen > 0
                    text: orphansWindow.chosen ? "Remove selected (" + orphansWindow.chosen + ")" : "Remove selected"
                    onClicked: {
                        const names = orphansWindow.orphans.filter((o, i) => orphanList.checked[i]).map(o => o.name)
                        if (!flow.window.dialogs.question("Remove packages", "Remove " + names.length + " package(s)? This cannot be undone.")) return
                        orphansWindow.close()
                        const op = flow.updates.orphanRemovalJob(JSON.stringify(names))
                        if (op !== "") flow.queue(JSON.parse(op))
                    }
                }
            }
        }
    }
}
