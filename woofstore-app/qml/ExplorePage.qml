import QtQuick
import org.kde.kirigami as Kirigami
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Explore: the title and search row (they stay at the top once the page scrolls), the filter panel,
// the featured carousel, the "On Flathub" lists, then every app (the directory) and Other packages.
Item {
    id: explore
    required property var window
    property Flickable flickable: flick

    readonly property var catalog: window.catalog
    readonly property alias ops: operations
    readonly property var categoryColors: ({
        "Graphics & Photography": "#674296", "Games": "#a24465", "Productivity": "#315c91", "Developer Tools": "#356b58",
        "Networking": "#8d5134", "Audio & Video": "#744d79", "Education & Science": "#526733", "System": "#486578", "Utilities": "#5c567e"
    })
    readonly property real motion: catalog.animationFactor()
    function ms(duration) { return Math.round(duration * motion) }

    // ── Search state ──
    property string query: ""
    property string category: ""
    property int appsLimit: 50
    property int othersLimit: 50
    readonly property int pageSize: 50
    readonly property var result: JSON.parse(catalog.results || '{"apps": [], "appsTotal": 0, "others": [], "othersTotal": 0}')
    readonly property bool browsing: catalog.ready && query === "" && category === "" && sourceBox.currentIndex === 0
    readonly property var featured: { catalog.revision; return JSON.parse(catalog.featured()) }
    readonly property bool previewing: preview.visible

    function runSearch() { catalog.search(query, sourceBox.currentText, category, sortBox.currentIndex, appsLimit, othersLimit) }
    function applyFilters() {
        searchDelay.stop()
        hidePreview()
        query = search.text.trim()
        appsLimit = pageSize
        othersLimit = pageSize
        runSearch()
    }
    function showSearch(text) { search.text = text; applyFilters() }
    function openApp(app) { hidePreview(); window.openDetails(app.key) }
    function number(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
    function entries(apps) {
        return apps.map(a => ({ name: a.name, summary: a.summary, icon: a.icon, fallback: a.icon, metadata: a.sources.join(" · "), app: a }))
    }
    Connections { target: explore.catalog; function onRevisionChanged() { explore.runSearch() } }
    Component.onCompleted: runSearch()

    // Screenshot states after WOOFSTORE_PREVIEW's page: "filters", "directory" (scrolled to every app), "search:TEXT", "hover".
    // Previews keep the first slide.
    readonly property var previewParts: String(window.preview ?? "").split(",")
    onPreviewPartsChanged: {
        if (previewParts.includes("filters")) filtersButton.checked = true
        const typed = previewParts.find(p => p.startsWith("search:"))
        if (typed) showSearch(typed.slice(7))
    }
    // "hover": the hover preview of the first mosaic card
    Timer {
        running: explore.previewParts.includes("hover") && explore.catalog.ready
        interval: 3000
        onTriggered: { explore.hoverSource = masonries.itemAt(tabs.currentIndex).tile(0); explore.showPreview() }
    }
    function showDirectory() { flick.contentY = Math.max(0, directoryHeading.y + column.y - 16) }
    Connections {
        target: explore.catalog
        enabled: explore.previewParts.includes("directory")
        function onResultsChanged() { Qt.callLater(explore.showDirectory) }
    }

    AppOperations { id: operations; store: explore.window.store; catalog: explore.window.catalog; window: explore.window }
    readonly property var listOps: ({ label: entry => operations.label(entry.app), busy: entry => operations.busy(entry.app) })

    // Wait for a pause in typing: one search per word instead of one per key.
    Timer { id: searchDelay; interval: 150; onTriggered: explore.applyFilters() }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape && preview.visible) { hidePreview(); event.accepted = true; return }
        typeToSearch(event)
    }
    // Typing anywhere on Explore starts a search, so the first letters are not lost. Main.qml also sends the keys that
    // the sidebar leaves alone.
    function typeToSearch(event) {
        const typed = event.text
        if (typed.length > 0 && typed.charCodeAt(0) > 32 && typed.charCodeAt(0) !== 127 && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            search.forceActiveFocus()
            search.text += typed
            event.accepted = true
        }
    }

    // ── Carousel ──
    function advance(step) {
        const count = slides.count
        if (!count) return
        carousel.select(((carousel.current + step) % count + count) % count)
    }
    function within(item, ancestor) { for (; item; item = item.parent) if (item === ancestor) return true; return false }
    readonly property bool carouselOnScreen: {
        const top = carousel.y + column.y - flick.contentY
        return top < flick.height && top + carousel.height > 0
    }
    readonly property bool carouselKeyboard: {
        const focused = explore.Window.activeFocusItem
        return !!focused && focused.visualFocus === true && (within(focused, carousel) || within(focused, dotRow))
    }
    Timer {
        id: autoplay
        interval: 5000
        repeat: true
        running: explore.window.preview === "" && explore.visible && carousel.visible && slides.count > 1 && explore.carouselOnScreen && !carouselHover.hovered && !dotHover.hovered && !explore.carouselKeyboard
        onTriggered: explore.advance(1)
    }

    // ── On Flathub lists: every list shows enough cards to fill the same height, so switching tabs never moves the page ──
    function equalize() {
        const minimum = 15, cap = 780  // At least `minimum` cards and, beyond that, as many as fit under `cap` pixels
        let tallest = 0
        const lists = []
        for (let i = 0; i < masonries.count; ++i) if (masonries.itemAt(i).count > 0) lists.push(masonries.itemAt(i))
        for (const masonry of lists) {
            masonry.fill = 0
            const total = masonry.count
            let best = Math.min(minimum, total)
            for (let count = best; count <= total; ++count) {
                masonry.show(count)
                masonry.arrange()
                if (count > minimum && masonry.implicitHeight > cap) break
                best = count
            }
            masonry.show(best)
            masonry.arrange()
            tallest = Math.max(tallest, masonry.implicitHeight)
        }
        for (const masonry of lists) { masonry.fill = tallest; masonry.arrange() }  // Lists differ by less than a band; the bands share the difference
    }
    onFeaturedChanged: {
        carousel.current = Math.min(carousel.current, Math.max(0, featured.carousel.length - 1))
        if (!(featured.sections[tabs.currentIndex]?.apps.length > 0)) {
            const first = featured.sections.findIndex(s => s.apps.length > 0)
            if (first >= 0) tabs.currentIndex = first
        }
        Qt.callLater(equalize)
    }

    // ── Hover preview: a bigger card over a mosaic card, with more text and Install ──
    property Item hoverSource: null
    Timer { id: hoverDelay; interval: 350; onTriggered: explore.showPreview() }
    Timer { id: dismissDelay; interval: 150; onTriggered: explore.fadePreview() }
    function tileHovered(tile, on) {
        // The open preview covers its own tile, so the tile always reads "left": the preview's own hover closes it
        if (preview.visible && tile === hoverSource) return
        if (on) { hidePreview(); hoverSource = tile; hoverDelay.restart() }
        else { hoverDelay.stop(); dismissDelay.restart() }
    }
    function hidePreview() {
        hoverDelay.stop(); dismissDelay.stop(); grow.stop(); fade.stop()
        preview.opacity = 1
        preview.visible = false
        hoverSource = null
    }
    function fadePreview() {
        hoverDelay.stop(); dismissDelay.stop()
        if (!preview.visible) return
        if (ms(180) === 0) { hidePreview(); return }
        fade.stop(); fade.duration = ms(180); fade.from = preview.opacity; fade.start()
    }
    function showPreview() {
        const source = hoverSource
        if (!source || !source.visible || !explore.visible) return
        const card = source.mapToItem(flick, 0, 0)
        const viewWidth = flick.width - bar.width, viewHeight = flick.height
        if (card.x + source.width < 0 || card.y + source.height < 0 || card.x > viewWidth || card.y > viewHeight) return
        fade.stop(); preview.opacity = 1
        const app = source.app
        const facts = [app.group, app.sources.join(" · ")].filter(f => f !== "")
        let text = app.summary + "\n\n" + facts.join(" · ")
        if (app.detail && app.detail !== app.summary) text += "\n\n" + app.detail
        const copy = Object.assign({}, app)
        if (!source.artworkReady) copy.screenshot = ""  // No image yet: a text card
        preview.app = copy
        preview.summary = text
        preview.tintOverride = source.tint
        preview.spotlight = source.artworkReady
        preview.artworkBelow = !source.spotlight
        const width = Math.min(Math.max(320, source.width + 48), viewWidth - 24)
        const height = Math.min(Math.max(source.height + 24, preview.heightForWidth(width)), viewHeight - 24)
        const x = Math.max(12, Math.min(card.x + source.width / 2 - width / 2, Math.max(12, viewWidth - width - 12)))
        const y = Math.max(12, Math.min(card.y + source.height / 2 - height / 2, Math.max(12, viewHeight - height - 12)))
        preview.from = Qt.rect(card.x, card.y, source.width, source.height)
        preview.to = Qt.rect(Math.round(x), Math.round(y), width, height)
        preview.progress = 0
        preview.visible = true
        if (ms(220) > 0) { grow.duration = ms(220); grow.start() } else preview.progress = 1
    }

    Flickable {
        id: flick
        Kirigami.WheelHandler { target: flick }  // Plasma's wheel scrolling: a fixed step per notch with its smooth glide
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 16 + 24
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton  // Wheel and scroll bar only, as a QScrollArea
        clip: true
        onContentYChanged: explore.hidePreview()
        ScrollBar.vertical: ScrollBar { id: bar }

        ColumnLayout {
            id: column
            x: 20
            y: 16
            // The scroll bar's room stays empty when it is hidden, so every page has the same width
            width: flick.width - 40 - bar.width
            height: implicitHeight
            spacing: 16

            Item { id: slot; Layout.fillWidth: true; Layout.preferredHeight: zoneColumn.implicitHeight }

            TextEdit {  // Setup problems the person can fix, with the exact command
                visible: explore.catalog.metadataMissing
                text: "Repository apps have no icons or descriptions yet. Install them with: sudo pacman -S --needed archlinux-appstream-data"
                readOnly: true
                selectByMouse: true
                wrapMode: Text.Wrap
                textFormat: TextEdit.PlainText
                color: palette.windowText
                selectionColor: palette.highlight
                selectedTextColor: palette.highlightedText
                Layout.fillWidth: true
            }

            // ── Featured carousel: whole-click slides that spring sideways ──
            Item {
                id: carousel
                property int current: 0
                property int from: -1
                property int direction: 1
                property real shift: 0
                visible: explore.browsing && slides.count > 0
                Layout.fillWidth: true
                Layout.preferredHeight: flick.width - bar.width < 650 ? 220 : 246
                clip: true
                function select(index) {
                    if (index === current) return
                    slide.stop()
                    direction = index > current ? 1 : -1
                    if (!visible || explore.ms(420) === 0) { from = -1; current = index; return }
                    from = current
                    current = index
                    shift = 0
                    slide.to = direction
                    slide.duration = explore.ms(420)
                    slide.start()
                }
                onWidthChanged: slide.stop()
                onVisibleChanged: slide.stop()
                NumberAnimation {
                    id: slide
                    target: carousel
                    property: "shift"
                    from: 0
                    easing.type: Easing.OutBack
                    easing.overshoot: .65
                    onStopped: carousel.from = -1
                }
                // While sliding, the frame is clipped to the card's rounded shape
                layer.enabled: slide.running
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: carouselMask
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }
                Repeater {
                    id: slides
                    model: explore.featured.carousel
                    ExploreTile {
                        required property var modelData
                        required property int index
                        app: modelData
                        page: explore
                        hero: true
                        width: carousel.width
                        height: carousel.height
                        readonly property real offset: carousel.shift * carousel.width
                        visible: index === carousel.current || (slide.running && index === carousel.from)
                        x: !slide.running ? 0 : index === carousel.from ? -offset : carousel.direction * carousel.width - offset
                        onClicked: explore.openApp(app)
                    }
                }
                HoverHandler { id: carouselHover }
                // The wheel turns the slides instead of scrolling the page
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: wheel => explore.turnByWheel(wheel)
                }
            }
            Item {
                id: carouselMask
                visible: false
                layer.enabled: true
                width: carousel.width
                height: carousel.height
                Layout.preferredHeight: 0
                Layout.preferredWidth: 0
                Rectangle { anchors.fill: parent; anchors.margins: 1; radius: 10 }
            }

            Item {
                id: dotRow
                visible: explore.browsing && slides.count > 1
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    Repeater {
                        model: explore.featured.carousel
                        ExploreDot {
                            required property var modelData
                            required property int index
                            checked: index === carousel.current
                            duration: explore.ms(280)
                            Accessible.name: "Show " + modelData.name + " featured slide, " + (index + 1) + " of " + slides.count
                            ToolTip.text: modelData.name
                            onClicked: explore.advance(index - carousel.current)
                        }
                    }
                }
                HoverHandler { id: dotHover }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: wheel => explore.turnByWheel(wheel)
                }
            }

            Label {
                visible: explore.browsing && explore.catalog.offline
                text: "Offline: showing saved suggestions."
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            // ── On Flathub: four lists behind one tab bar ──
            TabBar {
                id: tabs
                visible: explore.browsing && explore.featured.sections.some(s => s.apps.length > 0)
                Layout.alignment: Qt.AlignHCenter
                font.pointSize: Application.font.pointSize + 3
                Accessible.name: "Flathub lists"
                onCurrentIndexChanged: { explore.hidePreview(); Qt.callLater(explore.equalize) }
                Repeater {
                    model: [["Trending", "trending"], ["Popular", "star"], ["New", "sparkle"], ["Updated", "refresh"]]
                    TabButton {
                        id: tab
                        required property var modelData
                        required property int index
                        text: modelData[0]
                        // The style paints the tab; the Lucide glyph and the text are drawn here, so its own text goes
                        implicitWidth: tabRow.implicitWidth + 32
                        width: implicitWidth
                        Component.onCompleted: background.text = Qt.binding(() => "")
                        enabled: (explore.featured.sections[index]?.apps.length ?? 0) > 0
                        Row {
                            id: tabRow
                            anchors.centerIn: parent
                            spacing: 4
                            Lucide {
                                name: tab.modelData[1]
                                width: 16; height: 16
                                color: tab.palette.windowText
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Label { text: tab.text; font: tab.font; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }
            Item {
                id: lists
                visible: tabs.visible
                Layout.fillWidth: true
                Layout.preferredHeight: masonries.itemAt(tabs.currentIndex)?.implicitHeight ?? 1
                onWidthChanged: Qt.callLater(explore.equalize)
                Repeater {
                    id: masonries
                    model: 4
                    ExploreMasonry {
                        required property int index
                        page: explore
                        width: lists.width
                        apps: explore.featured.sections[index]?.apps ?? []
                        visible: index === tabs.currentIndex
                    }
                }
            }

            // ── Directory: every app, then the packages without app metadata ──
            Label {
                id: directoryHeading
                visible: explore.result.appsTotal > 0
                text: (explore.query === "" ? (explore.category === "" ? "All apps" : explore.category) : "Apps") + " · " + explore.number(explore.result.appsTotal) + " · "
                    + (sortBox.currentIndex === 0 ? (explore.query === "" ? "most popular first" : "best match first") : sortBox.currentText)
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pointSize: 12
                font.bold: true
                Layout.topMargin: 20
                Layout.fillWidth: true
            }
            AppList {
                Layout.fillWidth: true
                model: explore.entries(explore.result.apps)
                operations: explore.listOps
                Accessible.name: "Apps"
                onActivated: entry => explore.openApp(entry.app)
                onInstall: entry => operations.primary(entry.app, "")
            }
            Button {
                visible: explore.result.apps.length < explore.result.appsTotal
                text: "Load more (" + explore.number(explore.result.appsTotal - explore.result.apps.length) + " more)"
                Layout.minimumHeight: 32
                Layout.alignment: Qt.AlignHCenter
                onClicked: { explore.appsLimit += explore.pageSize; explore.runSearch() }
            }
            Label {
                visible: explore.result.othersTotal > 0
                text: "Other packages · " + explore.number(explore.result.othersTotal)
                textFormat: Text.PlainText
                font.pointSize: 12
                font.bold: true
                Layout.fillWidth: true
                ToolTip.visible: othersHover.hovered
                ToolTip.delay: 700
                ToolTip.text: "Packages without app metadata: libraries, command-line tools, and most AUR packages."
                HoverHandler { id: othersHover }
            }
            AppList {
                Layout.fillWidth: true
                model: explore.entries(explore.result.others)
                operations: explore.listOps
                Accessible.name: "Other packages"
                onActivated: entry => explore.openApp(entry.app)
                onInstall: entry => operations.primary(entry.app, "")
            }
            Button {
                visible: explore.result.others.length < explore.result.othersTotal
                text: "Show more packages (" + explore.number(explore.result.othersTotal - explore.result.others.length) + " more)"
                Layout.minimumHeight: 32
                Layout.alignment: Qt.AlignHCenter
                onClicked: { explore.othersLimit += explore.pageSize; explore.runSearch() }
            }
            Label {
                visible: explore.result.appsTotal === 0 && explore.result.othersTotal === 0
                text: !explore.catalog.ready ? "Loading apps…"
                    : explore.query === "" ? "No apps in this category from this source. Choose All sources or clear the filters."
                    : "Nothing matches “" + explore.query + "”. Check the spelling, or clear the filters."
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    property int wheelDistance: 0
    property bool wheelPixels: false
    function turnByWheel(wheel) {
        const pixels = wheel.angleDelta.x === 0 && wheel.angleDelta.y === 0
        const delta = pixels ? wheel.pixelDelta : wheel.angleDelta
        const distance = Math.abs(delta.x) > Math.abs(delta.y) ? delta.x : delta.y
        const threshold = pixels ? 60 : 120
        if (wheel.phase === Qt.ScrollBegin || wheelPixels !== pixels) wheelDistance = 0
        // One slide per gesture: input that arrives while a slide moves is dropped, so a swipe or a fast spin turns one slide
        if (slide.running) { wheelDistance = 0; wheel.accepted = true; return }
        wheelPixels = pixels
        wheelDistance += distance
        const steps = Math.trunc(wheelDistance / threshold)
        if (steps) { wheelDistance %= threshold; advance(-steps) }
        if (wheel.phase === Qt.ScrollEnd) wheelDistance = 0
        wheel.accepted = true
    }

    // ── The title, search row and filters: part of the page until it scrolls past them, then stuck at the top ──
    Rectangle {
        id: zone
        readonly property real home: slot.y + column.y - flick.contentY
        readonly property bool stuck: home < 0
        x: stuck ? 0 : column.x
        y: stuck ? 0 : home
        width: stuck ? flick.width - bar.width : column.width
        height: stuck ? zoneColumn.implicitHeight + 16 : zoneColumn.implicitHeight
        color: stuck ? palette.window : "transparent"
        Rectangle { visible: zone.stuck; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: palette.mid }

        ColumnLayout {
            id: zoneColumn
            x: zone.stuck ? column.x : 0
            y: zone.stuck ? 8 : 0
            width: column.width
            spacing: 12

            Label { text: "Explore"; textFormat: Text.PlainText; font.pointSize: 23; font.bold: true }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                TextField {
                    id: search
                    placeholderText: "Search apps and packages"
                    Accessible.name: "Search apps and packages"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 38
                    leftPadding: 33
                    rightPadding: text === "" ? 8 : 30
                    onTextEdited: { searchDelay.restart(); completer.update() }
                    onTextChanged: if (text === "") searchDelay.restart()
                    onAccepted: {
                        if (completer.visible && suggestions.currentIndex >= 0) completer.choose(suggestions.currentIndex)
                        else { completer.close(); explore.applyFilters() }
                    }
                    Keys.onDownPressed: if (completer.visible) suggestions.currentIndex = Math.min(suggestions.count - 1, suggestions.currentIndex + 1)
                    Keys.onUpPressed: if (completer.visible) suggestions.currentIndex = Math.max(-1, suggestions.currentIndex - 1)
                    Keys.onEscapePressed: event => { if (completer.visible) completer.close(); else event.accepted = false }
                    AppIcon { x: 4; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; icon: "edit-find" }
                    ToolButton {
                        visible: search.text !== ""
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24; height: 24
                        padding: 0
                        icon.name: "edit-clear-locationbar-rtl"
                        icon.width: 16; icon.height: 16
                        focusPolicy: Qt.NoFocus
                        Accessible.name: "Clear text"
                        onClicked: { search.clear(); search.forceActiveFocus() }
                    }
                    // Suggested app names, most popular first; the rows line up with the typed text
                    Popup {
                        id: completer
                        y: search.height
                        width: search.width
                        padding: 1
                        focus: false
                        closePolicy: Popup.CloseOnPressOutsideParent | Popup.CloseOnEscape
                        property var names: []
                        function update() {
                            names = search.text.trim() === "" ? [] : JSON.parse(explore.catalog.suggest(search.text))
                            suggestions.currentIndex = -1
                            if (names.length > 0 && search.activeFocus) open(); else close()
                        }
                        function choose(index) {
                            search.text = names[index]
                            close()
                            explore.applyFilters()
                        }
                        contentItem: ListView {
                            id: suggestions
                            implicitHeight: contentHeight
                            clip: true
                            model: completer.names
                            delegate: ItemDelegate {
                                required property string modelData
                                required property int index
                                width: suggestions.width
                                text: modelData
                                leftPadding: 33
                                highlighted: ListView.isCurrentItem
                                onClicked: completer.choose(index)
                            }
                        }
                    }
                }
                ToolButton {
                    id: filtersButton
                    flat: false
                    checkable: true
                    icon.name: "view-filter"
                    text: "Filters"
                    display: AbstractButton.TextBesideIcon
                    Accessible.name: "Show or hide filters and categories"
                    Layout.minimumHeight: 38
                }
                Button {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    Accessible.name: "Refresh the catalogue"
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: "Refresh the catalogue"
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    onClicked: explore.catalog.refresh()
                    contentItem: Item { Lucide { anchors.centerIn: parent; width: 20; height: 20; name: "refresh"; color: palette.buttonText } }
                }
            }

            // Source, sort and the nine categories; slides open and closed
            Item {
                id: filterPanel
                readonly property real target: filtersButton.checked ? filterContent.implicitHeight : 0
                property real shown: target
                Behavior on shown { enabled: explore.visible && explore.ms(240) > 0; NumberAnimation { duration: explore.ms(240); easing.type: Easing.OutCubic } }
                visible: shown > 0.5
                clip: true
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(shown)
                ColumnLayout {
                    id: filterContent
                    width: parent.width
                    spacing: 12
                    RowLayout {
                        spacing: 6
                        Layout.fillWidth: true
                        Label { text: "Source" }
                        ComboBox {
                            id: sourceBox
                            model: ["All sources", "Repositories", "Flatpak", "AUR"]
                            Accessible.name: "Software source"
                            onActivated: explore.applyFilters()
                        }
                        Item { Layout.preferredWidth: 16 }
                        Label { text: "Sort" }
                        ComboBox {
                            id: sortBox
                            model: ["Recommended", "Name A–Z", "Name Z–A"]
                            Accessible.name: "App sort order"
                            onActivated: explore.applyFilters()
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Clear filters"
                            onClicked: {
                                explore.category = ""
                                search.clear()
                                sourceBox.currentIndex = 0
                                sortBox.currentIndex = 0
                                explore.applyFilters()
                            }
                        }
                    }
                    GridLayout {
                        columns: 3
                        rowSpacing: 10
                        columnSpacing: 10
                        Layout.fillWidth: true
                        Repeater {
                            model: [["Audio & Video", "applications-multimedia"], ["Developer Tools", "applications-development"],
                                ["Education & Science", "applications-science"], ["Games", "applications-games"],
                                ["Graphics & Photography", "applications-graphics"], ["Networking", "applications-internet"],
                                ["Productivity", "applications-office"], ["System", "applications-system"], ["Utilities", "applications-utilities"]]
                            AbstractButton {
                                id: categoryButton
                                required property var modelData
                                readonly property string name: modelData[0]
                                checked: explore.category === name
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                padding: 8
                                Accessible.name: name
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumHeight: 58
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                onClicked: { explore.category = explore.category === name ? "" : name; explore.applyFilters() }
                                background: Rectangle {
                                    radius: 9
                                    color: explore.categoryColors[categoryButton.name]
                                    border.width: 2
                                    border.color: categoryButton.hovered || categoryButton.activeFocus || categoryButton.checked ? "white" : "transparent"
                                }
                                contentItem: Item {
                                    implicitWidth: categoryRow.implicitWidth
                                    implicitHeight: categoryRow.implicitHeight
                                    Row {
                                        id: categoryRow
                                        anchors.centerIn: parent
                                        spacing: 4
                                        // Themed category icons, tinted white
                                        Item {
                                            width: 22; height: 22
                                            anchors.verticalCenter: parent.verticalCenter
                                            AppIcon { id: categoryIcon; anchors.fill: parent; icon: categoryButton.modelData[1]; fallback: "applications-other"; visible: false }
                                            MultiEffect { anchors.fill: parent; source: categoryIcon; brightness: 1.0 }
                                        }
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: categoryButton.name === "Graphics & Photography" || categoryButton.name === "Education & Science"
                                                ? categoryButton.name.replace(" & ", " &\n") : categoryButton.name
                                            textFormat: Text.PlainText
                                            color: "white"
                                            font.bold: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ExploreTile {
        id: preview
        property rect from
        property rect to
        property real progress: 0
        visible: false
        app: ({})
        page: explore
        quickInstall: true
        wantArtwork: true
        focusPolicy: Qt.NoFocus
        x: from.x + (to.x - from.x) * progress
        y: from.y + (to.y - from.y) * progress
        width: from.width + (to.width - from.width) * progress
        height: from.height + (to.height - from.height) * progress
        Accessible.name: (app.name ?? "") + "; open app details"
        onHoveredChanged: if (hovered) { dismissDelay.stop(); fade.stop(); opacity = 1 } else dismissDelay.restart()
        onClicked: { const app = explore.hoverSource ? explore.hoverSource.app : null; explore.hidePreview(); if (app) explore.window.openDetails(app.key) }
        NumberAnimation { id: grow; target: preview; property: "progress"; from: 0; to: 1; easing.type: Easing.OutCubic }
        NumberAnimation { id: fade; target: preview; property: "opacity"; to: 0; easing.type: Easing.OutCubic; onFinished: explore.hidePreview() }
    }
}
