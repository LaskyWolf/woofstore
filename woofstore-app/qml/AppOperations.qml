import QtQuick

// Install, Open and Remove for the Explore and app pages: what a button says and what a click does. An app's operation is
// found in the activity by its title, so a button shows it even when the operation started on another page.
QtObject {
    id: ops
    required property var store
    required property var catalog
    required property var window

    function job(name) {
        const titles = ["Install " + name, "Reinstall " + name, "Remove " + name, "Install " + name + " from the AUR"]
        return window.activity.find(a => (a.state === "running" || a.state === "waiting") && titles.includes(a.title)) ?? null
    }
    function busy(app) { return job(app.name) !== null }
    // Button texts: Install, Installing…, Reinstalling…, Removing…, then Open once installed
    function label(app) {
        const running = job(app.name)
        if (running) return running.title.startsWith("Remove") ? "Removing…" : running.title.startsWith("Reinstall") ? "Reinstalling…" : "Installing…"
        return app.installed ? "Open" : "Install"
    }
    function removeLabel(app) {
        const running = job(app.name)
        return running && running.title.startsWith("Remove") ? "Removing…" : "Remove"
    }

    // Opens an installed app, otherwise installs it from `source` (the source chooser's text, "" for the default one).
    function primary(app, source) {
        if (busy(app)) return
        if (app.installed) {
            const problem = catalog.launch(app.key)
            if (problem) window.dialogs.warning("Cannot open " + app.name, problem)
            return
        }
        run(app.key, "Install", source ?? "", false)
    }

    // Queues the operation; returns its id, or 0 when nothing was queued.
    function run(key, action, source, removeUnused) {
        const operation = JSON.parse(catalog.operation(key, action, source, removeUnused))
        if (operation.error) {
            window.dialogs.warning("Cannot " + action.toLowerCase(), operation.error)
            return 0
        }
        if (operation.aur !== undefined) {
            window.reviewAur(operation.aur, operation.title ?? "", action === "Reinstall")
            return 0
        }
        return window.queueJob(operation.lane, operation.spec)
    }
}
