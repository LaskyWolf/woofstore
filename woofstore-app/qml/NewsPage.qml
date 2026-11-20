import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// News: cards in columns, the newest first. Unread news from Arch and CachyOS put the accent badge on the sidebar (Main.qml).
// A clean page shows two articles per source and about fifteen in all until "Show more"; a search or a topic looks at everything.
Item {
    id: page
    required property var window
    property Flickable flickable: scroll

    readonly property var feed: window.news
    readonly property var items: window.newsItems
    readonly property var problems: JSON.parse(feed.problems)
    // The sample articles keep their own pill date; real ones use the date of the last visit
    readonly property bool sample: window.preview.split(",").includes("sample-news")
    readonly property string pillSince: sample ? "2026-10-16T12:00:00" : window.newsPillSince
    property string topic: ""
    property int extra: 0
    readonly property string needle: search.text.trim().toLowerCase()
    // The viewport keeps room for the scroll bar, shown or not
    readonly property real viewportWidth: scroll.width - bar.implicitWidth
    readonly property int columnCount: viewportWidth >= 900 ? 3 : 2

    readonly property var topicColors: ({ Security: "#a53a3a", Gaming: "#a24465", Desktop: "#315c91", Hardware: "#8d5134", Apps: "#674296", Linux: "#356b58" })

    // The articles to show, each dealt to the shortest column, and how many the limits hide
    readonly property var dealt: {
        const perSource = 2 + 2 * extra, total = 15 + 12 * extra
        const columns = [], weight = []
        for (let i = 0; i < columnCount; ++i) { columns.push([]); weight.push(0) }
        const counts = {}
        let shown = 0, hidden = 0
        for (const item of items) {
            if (topic && !item.topics.includes(topic)) continue
            if (needle && !(item.title + " " + item.summary + " " + item.source).toLowerCase().includes(needle)) continue
            if (!needle && !topic && (shown >= total || (counts[item.source] ?? 0) >= perSource)) { ++hidden; continue }
            counts[item.source] = (counts[item.source] ?? 0) + 1
            const side = weight.indexOf(Math.min(...weight))
            weight[side] += 70 + (item.image ? 160 : 0) + item.title.length * 2 + item.summary.length
            columns[side].push(item); ++shown
        }
        return { columns: columns, shown: shown, hidden: hidden }
    }
    readonly property string message: feed.loading ? (dealt.shown ? "Reloading the news…" : "")
        : !dealt.shown ? (items.length === 0 ? "Couldn't load the news. " + problems.join("; ") : "No news matches your search or topic.")
        : problems.length ? "Not reachable now: " + problems.join("; ") : ""

    Connections {
        target: page.window
        function onPreviewChanged() { if (page.window.preview.split(",").includes("news-filters")) filters.checked = true }
    }

    Flickable {
        id: scroll
        PageWheel { target: scroll }  // Mouse wheel as in Plasma; trackpad with inertia
        anchors.fill: parent
        contentWidth: page.viewportWidth
        contentHeight: body.implicitHeight + 16 + 24
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { id: bar }

        ColumnLayout {
            id: body
            x: 20
            y: 16
            width: page.viewportWidth - 40
            spacing: 16

            Item { Layout.fillWidth: true; Layout.preferredHeight: header.implicitHeight }  // Where the header sits while not stuck

            Label {
                Layout.fillWidth: true
                visible: page.message !== ""
                text: page.message
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: palette.placeholderText
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: 260
                visible: page.feed.loading && page.dealt.shown === 0
                spacing: 14
                Item { Layout.fillHeight: true }
                Spinner { Layout.alignment: Qt.AlignHCenter; implicitWidth: 44; implicitHeight: 44 }
                Label { Layout.fillWidth: true; text: "Loading the news…"; horizontalAlignment: Text.AlignHCenter }
                Item { Layout.fillHeight: true }
            }

            Row {
                id: cards
                Layout.fillWidth: true
                spacing: 16
                readonly property real columnWidth: (width - spacing * (page.columnCount - 1)) / page.columnCount
                Repeater {
                    model: page.dealt.columns
                    Column {
                        required property var modelData
                        width: cards.columnWidth
                        spacing: 16
                        Repeater {
                            model: parent.modelData
                            NewsCard {
                                required property var modelData
                                width: cards.columnWidth
                                item: modelData
                                fresh: modelData.important && modelData.date !== "" && page.pillSince !== "" && new Date(modelData.date) > new Date(page.pillSince)
                            }
                        }
                    }
                }
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                visible: page.dealt.hidden > 0
                text: "Show more (" + page.dealt.hidden + ")"
                onClicked: page.extra++
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }

        // The header stays at the top while the page scrolls. Stuck, it spans the viewport with the window colour and a line under it.
        Item {
            id: sticky
            readonly property bool stuck: scroll.contentY > 16
            z: 1
            x: stuck ? 0 : 20
            y: stuck ? scroll.contentY : 16
            width: stuck ? page.viewportWidth : page.viewportWidth - 40
            height: header.implicitHeight + (stuck ? 16 : 0)
            Rectangle {
                anchors.fill: parent
                visible: sticky.stuck
                color: palette.window
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: palette.mid }
            }

            ColumnLayout {
                id: header
                x: sticky.stuck ? 20 : 0
                y: sticky.stuck ? 8 : 0
                width: page.viewportWidth - 40
                spacing: 12

                Label { text: "News"; font.pointSize: 23; font.bold: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: search
                        Layout.fillWidth: true
                        implicitHeight: Math.max(38, implicitBackgroundHeight)
                        leftPadding: 35
                        rightPadding: clear.visible ? 30 : 8
                        placeholderText: "Search news"
                        Accessible.name: "Search news"
                        onTextChanged: page.extra = 0
                        SearchIcon {}
                        Image {
                            id: clear
                            x: parent.width - width - 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            visible: search.text !== ""
                            sourceSize: Qt.size(16, 16)
                            source: "image://icon/edit-clear-locationbar-rtl|edit-clear"
                            TapHandler { onTapped: search.clear() }
                        }
                    }
                    ToolButton {
                        id: filters
                        implicitHeight: Math.max(38, implicitBackgroundHeight)
                        flat: false
                        checkable: true
                        display: AbstractButton.TextBesideIcon
                        icon.name: "view-filter"
                        text: "Filters"
                        Accessible.name: "Show or hide the topics"
                    }
                    Button {
                        implicitWidth: 38
                        implicitHeight: 38
                        Accessible.name: "Reload the news"
                        ToolTip.text: "Reload the news"
                        ToolTip.visible: hovered
                        ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
                        onClicked: page.feed.load()
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        // QPushButton draws its icon at the default 16 px
                        contentItem: Item { Lucide { anchors.centerIn: parent; width: 16; height: 16; name: "refresh" } }
                    }
                }

                // Same filter button and colour grid as Explore: the grid slides open under the search row, and with no topic chosen every article shows
                Item {
                    id: panel
                    property real open: filters.checked ? 1 : 0
                    Behavior on open { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(topics.implicitHeight * open)
                    visible: open > 0
                    clip: true
                    GridLayout {
                        id: topics
                        width: parent.width
                        columns: 3
                        rowSpacing: 10
                        columnSpacing: 10
                        Repeater {
                            model: [["Security", "shield"], ["Gaming", "gamepad"], ["Desktop", "monitor"], ["Hardware", "cpu"], ["Apps", "grid"], ["Linux", "terminal"]]
                            AbstractButton {
                                id: pill
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1  // Equal columns
                                implicitHeight: Math.max(58, implicitContentHeight + 2 * padding)
                                padding: 8
                                checked: page.topic === modelData[0]
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                text: modelData[0]
                                Accessible.name: text
                                font.bold: true
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                onClicked: { page.topic = page.topic === modelData[0] ? "" : modelData[0]; page.extra = 0 }
                                background: Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1  // The pills paint 1 px inside their cell
                                    radius: 9
                                    color: page.topicColors[pill.modelData[0]]
                                    border.width: 2
                                    border.color: pill.hovered || pill.activeFocus || pill.checked ? "white" : "transparent"
                                }
                                contentItem: Item {
                                    implicitHeight: row.implicitHeight
                                    Row {
                                        id: row
                                        anchors.centerIn: parent
                                        spacing: 2
                                        Lucide { anchors.verticalCenter: parent.verticalCenter; width: 22; height: 22; name: pill.modelData[1]; color: "white" }
                                        Label { anchors.verticalCenter: parent.verticalCenter; text: pill.text; font: pill.font; color: "white" }
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
