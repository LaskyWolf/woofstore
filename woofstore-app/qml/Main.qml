import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import org.woofstore.app

// The window: a collapsible sidebar (Explore, Installed, Updates, News, Activity at the bottom) beside one page at a
// time. App details are a page of their own; Back, Alt+Left and the mouse's back button return to where they were opened.
ApplicationWindow {
    id: root
    title: "WoofStore"
    width: 1180
    height: 850
    minimumWidth: 720
    minimumHeight: 580
    visible: true
    color: palette.window

    // ── Objects the pages use through `window` ──
    readonly property alias store: store
    readonly property alias catalog: catalog
    readonly property alias updates: updates
    readonly property alias news: newsFeed
    readonly property alias aurReview: aurReview
    readonly property alias dialogs: dialogs
    readonly property alias updatesFlow: updatesFlow
    readonly property alias settings: settings

    Store {
        id: store
        // A program the operation needs is missing; other refusals (the same operation already queued) stay silent, as before
        onNotice: text => { if (text.startsWith("Install ")) dialogs.warning("Required program missing", text) }
    }
    Catalog {
        id: catalog
        onReadyChanged: root.handleRequest()
    }
    Updates { id: updates }
    News { id: newsFeed }
    NativeDialogs { id: dialogs }
    AurReview {
        id: aurReview
        onFailed: (title, message) => dialogs.warning(title, message)
        onBuildReady: spec => root.queueJob("pacman", JSON.parse(spec))
    }
    AurReviewDialog { review: aurReview }
    // What follows an operation (leftovers, re-check, service restarts, the store's own restart), whatever page is open
    UpdatesFlow { id: updatesFlow; store: store; updates: updates; window: root }

    // The settings file of every version (~/.config/WoofStore/WoofStore.conf), same keys as 1.x
    Settings {
        id: settings
        property bool compactSidebar: false
        // The date of the newest Arch or CachyOS article the person has seen (ISO 8601)
        property string newsSeen: ""
    }

    // One check at start, after the first installed list, so the count on Updates is right without opening it
    Connections {
        target: store
        function onInstalledLoadingChanged() {
            if (!store.installedLoading && !updates.checked && !updates.loading) updates.check(store.installed)
        }
        function onInstalledChanged() { catalog.setInstalled(store.installed) }
    }

    // ── News: unread important articles put a count on the News entry; the first start has nothing unread ──
    readonly property var newsItems: JSON.parse(newsFeed.items)
    readonly property string newestImportant: newsItems.filter(i => i.important && i.date).map(i => i.date).sort().pop() ?? ""
    readonly property int unreadNews: settings.newsSeen === "" ? 0 : newsItems.filter(i => i.important && i.date && new Date(i.date) > new Date(settings.newsSeen)).length
    onNewestImportantChanged: if (settings.newsSeen === "" && newestImportant !== "") settings.newsSeen = newestImportant
    // Articles newer than this carry the "New" pill while the page is open
    property string newsPillSince: ""
    function openedNews() {
        newsPillSince = settings.newsSeen
        if (newestImportant !== "") settings.newsSeen = newestImportant
    }

    // ── Operations ──
    readonly property var activity: JSON.parse(store.activity)
    readonly property bool operationsActive: activity.some(a => a.state === "running" || a.state === "waiting")
    property int lastResult: 0      // Last operation's result until Activity is opened: 0 none, 1 succeeded, 2 failed
    property bool jobFailed: false  // A failure nobody has looked at

    /// Queues an operation (see Store.queueJob); `spec` is an object. Returns its id, 0 when refused.
    function queueJob(lane, spec) {
        return store.queueJob(lane, JSON.stringify(spec))
    }
    /// Installs AUR package `name` after the person reviewed its build files and those of the AUR packages it needs.
    function reviewAur(name, title, reinstall) {
        aurReview.start(name, title || name, reinstall === true)
    }

    // Jobs write their output to files; read them a few times a second while anything is queued or running
    Timer { interval: 300; repeat: true; running: store.busy; onTriggered: store.tick() }

    // One details window per operation. Only an operation that starts while no other runs opens its window by itself;
    // the others are followed in Activity.
    property var jobWindows: ({})
    property var seenRunning: ({})
    Component { id: jobWindowComponent; JobWindow { transientParent: root } }
    function jobWindow(id) {
        if (!jobWindows[id]) {
            const job = activity.find(a => a.id === id)
            if (!job) return null
            jobWindows[id] = jobWindowComponent.createObject(root, { store: store, jobId: id, job: job })
        }
        return jobWindows[id]
    }
    function showJob(id) {
        const job = activity.find(a => a.id === id)
        if (!job) return
        if (job.state === "waiting") {
            if (dialogs.question("Remove from the queue", "Remove " + job.title + " from the queue? It has not started.")) store.cancel(id)
            return
        }
        const window = jobWindow(id)
        if (!window) return
        window.show(); window.raise(); window.requestActivate()
    }
    onActivityChanged: {
        const running = activity.filter(a => a.state === "running")
        for (const job of activity) {
            if (jobWindows[job.id]) jobWindows[job.id].job = job
            if (job.state === "running" && !seenRunning[job.id]) {
                seenRunning[job.id] = true
                if (running.length === 1) showJob(job.id)
            }
        }
    }
    Connections {
        target: store
        function onJobEnded(id, code) {
            root.lastResult = code === 0 ? 1 : 2
            if (code !== 0) root.jobFailed = true
            store.loadInstalled()
            const job = root.activity.find(a => a.id === id)
            if (code === 0 && job && job.title === "System Update") Qt.callLater(store.checkReboot)
            catalog.jobEnded(id, code)  // Removals delete the leftover files chosen for them once they end well
        }
        function onRebootAsked() { if (dialogs.question("Restart now?", "Save your work first. The computer restarts right away.")) store.reboot() }
        function onRebootNeeded() { dialogs.information("Restart to finish the update", "A new kernel was installed. Restart when you are ready.") }
    }
    Timer { interval: 3000; running: true; onTriggered: store.checkReboot() }

    ActivityWindow {
        id: activityWindow
        transientParent: root
        store: store
        records: root.activity
        onOpenJob: id => root.showJob(id)
    }
    function showActivity() {
        jobFailed = false
        lastResult = 0
        activityWindow.show(); activityWindow.raise(); activityWindow.requestActivate()
    }

    onClosing: close => {
        if (operationsActive && !dialogs.question("Operation running", "Running package operations keep going without the store, and a shutdown waits for them. Operations still waiting in the queue are dropped. Close WoofStore anyway?\n\nOpen it again to see how they ended."))
            close.accepted = false
    }

    // ── Requests from the desktop or a second start: an appstream: link, a .flatpakref or .flatpak file, or an app name ──
    property string pendingRequest: ""  // Kept until the catalogue can answer it
    function openRequest(text) {
        root.raise()
        root.requestActivate()
        if (root.visibility === Window.Minimized) root.showNormal()
        pendingRequest = String(text).trim()
        handleRequest()
    }
    function handleRequest() {
        if (pendingRequest === "") return
        const answer = JSON.parse(catalog.openRequest(pendingRequest))
        if (answer.wait) { selectPage(0); return }  // Again when the catalogue is ready
        pendingRequest = ""
        if (answer.key) openDetails(answer.key)
        else if (answer.search) { selectPage(0); explorePage.showSearch(answer.search) }
        else if (answer.file && dialogs.question("Install " + answer.name, answer.name + " is not in the catalogue. Install it from this file with Flatpak?\n\n" + answer.file)) {
            // Installing a file needs an administrator, so one pkexec session installs it
            queueJob("flatpak", {
                title: "Install " + answer.name,
                program: "bash",
                args: ["-c", "pkexec \"$WOOFSTORE_ROOT\" flatpak-files \"$1\"", "woof-store-flatpak-files", answer.file]
            })
        }
    }

    // ── Navigation with history: visits to the four pages and to app details ──
    // Entry: {page: 0..4, key, platform, section: the sidebar entry, scroll}
    property var history: [{ page: 0, section: 0, scroll: 0 }]
    property int historyIndex: 0
    readonly property var pageItems: [explorePage, installedPage, updatesPage, newsPage, detailsPage]
    function saveHistory() {
        const entry = history[historyIndex]
        const page = pageItems[entry.page]
        if (page.flickable) entry.scroll = page.flickable.contentY
        if (entry.page === 4) entry.platform = detailsPage.platform
    }
    function showHistory(fresh) {
        const entry = history[historyIndex]
        if (entry.page === 4) {
            if (fresh || detailsPage.appKey !== entry.key || detailsPage.platform !== entry.platform) detailsPage.showApp(entry.key, entry.platform ?? "")
            detailsPage.backLabel = entry.section === 1 ? "Installed" : "Explore"
        }
        navigation.section = entry.section
        pages.currentIndex = entry.page
        const page = pageItems[entry.page]
        if (page.flickable) page.flickable.contentY = fresh && entry.page === 4 ? 0 : entry.scroll
        page.forceActiveFocus()
    }
    function pushHistory(entry) {
        saveHistory()
        history = history.slice(0, historyIndex + 1).concat([entry])
        historyIndex = history.length - 1
        showHistory(true)
    }
    function moveHistory(direction) {
        const next = historyIndex + direction
        if (next < 0 || next >= history.length) return
        saveHistory()
        historyIndex = next
        showHistory(false)
    }
    function selectPage(page) {
        if (page === 3) openedNews()
        const enteringInstalled = page === 1 && pages.currentIndex !== 1
        if (pages.currentIndex !== page) pushHistory({ page: page, section: page, scroll: 0 })
        if (enteringInstalled) store.loadInstalled()
    }
    /// Shows app `key` of the catalogue; `platform` picks one of its sources.
    function openDetails(key, platform) {
        pushHistory({ page: 4, key: key, platform: platform ?? "", section: navigation.section, scroll: 0 })
    }
    // A window that opens with nothing focused hands the keyboard to the page, so typing works before the first click
    onActiveChanged: if (active && !contentItem.activeFocus) pageItems[pages.currentIndex].forceActiveFocus()
    Shortcut { sequences: ["Alt+Left", StandardKey.Back]; onActivated: root.moveHistory(-1) }
    Shortcut { sequences: ["Alt+Right", StandardKey.Forward]; onActivated: root.moveHistory(1) }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        // Letters typed while the sidebar has focus start a search on Explore too
        Keys.onPressed: event => { if (pages.currentIndex === 0) explorePage.typeToSearch(event) }

        // ── Sidebar ──
        Item {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: width
            width: settings.compactSidebar ? 56 : 156
            Behavior on width { enabled: root.active; NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

            ColumnLayout {
                id: navigation
                property int section: 0
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 16
                anchors.bottomMargin: 8
                spacing: 16

                AbstractButton {
                    id: sidebarToggle
                    implicitWidth: 40
                    implicitHeight: 40
                    checkable: true
                    checked: settings.compactSidebar
                    onToggled: settings.compactSidebar = checked
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: ToolTip.text
                    ToolTip.visible: hovered
                    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
                    ToolTip.text: checked ? "Expand sidebar" : "Collapse sidebar"
                    background: Rectangle {
                        radius: 8
                        color: sidebarToggle.checked ? sidebarToggle.palette.midlight : sidebarToggle.hovered ? sidebarToggle.palette.button : "transparent"
                        border.width: sidebarToggle.activeFocus ? 1 : 0
                        border.color: sidebarToggle.palette.highlight
                    }
                    contentItem: Item {
                        Lucide { anchors.centerIn: parent; width: 22; height: 22; name: sidebarToggle.checked ? "panel-open" : "panel-close" }
                    }
                }

                ColumnLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    Repeater {
                        model: [["Explore", "compass"], ["Installed", "list"], ["Updates", "refresh"], ["News", "newspaper"]]
                        NavButton {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            text: modelData[0]
                            glyph: modelData[1]
                            checked: navigation.section === index
                            // Every activation switches the page, also on the entry already selected
                            onClicked: { checked = Qt.binding(() => navigation.section === index); root.selectPage(index) }
                            badge: index === 2 ? updates.count : index === 3 ? root.unreadNews : 0
                            badgeColor: index === 2 && updates.systemCount > 0 ? "#da4453" : root.palette.highlight
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                ActivityButton {
                    Layout.fillWidth: true
                    compact: settings.compactSidebar
                    busy: store.busy
                    indeterminate: store.busy
                    progress: 0  // Package operations report no percentage; the line stays empty, the arc and the 30 % mark show work
                    result: root.lastResult
                    attention: root.jobFailed
                    onClicked: root.showActivity()
                }
            }
        }
        Rectangle {  // The divider shows while the sidebar is open
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            visible: !settings.compactSidebar
            color: palette.mid
        }

        // ── Pages ──
        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            ExplorePage { id: explorePage; window: root }
            InstalledPage { id: installedPage; window: root }
            UpdatesPage { id: updatesPage; window: root }
            NewsPage { id: newsPage; window: root }
            AppPage { id: detailsPage; window: root; onBack: root.moveHistory(-1) }
        }
    }

    // The mouse's back and forward buttons move through the history
    TapHandler {
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onTapped: (point, button) => root.moveHistory(button === Qt.BackButton ? -1 : 1)
    }

    // ── Screenshots for checks: WOOFSTORE_PREVIEW lists a page and states, e.g. "installed,expanded" or "explore,details:org.kde.krita" ──
    property string preview: ""
    property var shotTarget: null  // The window the screenshot saves, when not this one
    property string previewDetails: ""
    onPreviewChanged: {
        const parts = preview.split(",")
        const page = ["explore", "installed", "updates", "news"].indexOf(parts[0])
        if (page > 0) selectPage(page)
        if (parts.includes("expanded")) settings.compactSidebar = false
        if (parts.includes("collapsed")) settings.compactSidebar = true
        if (parts.includes("activity")) { showActivity(); shotTarget = activityWindow }
        const details = parts.find(p => p.startsWith("details:"))
        if (details) openRequest("appstream:" + details.slice(8))
    }

    Component.onCompleted: {
        // After the store restarted itself for an upgrade it comes back on Updates
        const afterRestart = String(settings.value("pageAfterRestart", "")) === "2"
        if (afterRestart) { settings.setValue("pageAfterRestart", ""); selectPage(2) }
        store.loadInstalled()
        catalog.load()
        newsFeed.load()
        updatesFlow.resumeAfterRestart()
    }
}
