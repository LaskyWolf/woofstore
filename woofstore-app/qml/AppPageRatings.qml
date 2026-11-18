import QtQuick
import QtQuick.Controls

// The average and how many reviews gave each star count. Hover a row to read its numbers; bars grow in once.
Item {
    id: bars
    property var ratings: null
    property int duration: 0
    property int hoveredRow: -1  // 0 = five stars
    property real grow: 0
    implicitHeight: 5 * 26 + 8
    implicitWidth: 360
    Component.onCompleted: if (duration > 0) growth.start(); else grow = 1
    onRatingsChanged: if (duration > 0) { growth.restart() } else grow = 1
    NumberAnimation { id: growth; target: bars; property: "grow"; from: 0; to: 1; duration: bars.duration; easing.type: Easing.OutCubic }

    readonly property var histogram: ratings ? ratings.histogram : [0, 0, 0, 0, 0]
    readonly property int most: Math.max(1, ...histogram)
    readonly property int whole: ratings ? Math.round(ratings.stars) : 0
    Accessible.name: ratings ? ratings.stars.toFixed(1) + " out of 5 stars from " + ratings.reviews + " reviews" : ""

    Label { x: 0; y: 0; width: 110; height: 56; verticalAlignment: Text.AlignVCenter; text: bars.ratings ? bars.ratings.stars.toFixed(1) : ""; font.pointSize: 30; font.bold: true }
    Label { x: 0; y: 56; width: 110; height: 24; verticalAlignment: Text.AlignVCenter; color: palette.highlight; text: "★".repeat(bars.whole) + "☆".repeat(5 - bars.whole) }
    Label { x: 0; y: 80; width: 130; height: 24; verticalAlignment: Text.AlignVCenter; text: (bars.ratings ? Number(bars.ratings.reviews).toLocaleString(Qt.locale(), "f", 0) : "") + " reviews" }

    Repeater {
        model: 5
        Item {
            id: row
            required property int index
            readonly property int stars: 5 - index
            readonly property int count: bars.histogram[stars - 1] ?? 0
            x: 150; y: 4 + index * 26
            width: bars.width - 150; height: 22
            Rectangle {
                visible: bars.hoveredRow === row.index
                x: -6; y: -2; width: row.width + 12; height: row.height + 4; radius: 6
                color: Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 36 / 255)
            }
            Label { width: 30; height: row.height; verticalAlignment: Text.AlignVCenter; text: row.stars + " ★" }
            Rectangle {
                id: track
                x: 36; y: 6; width: row.width - 36 - 64; height: row.height - 12
                radius: height / 2
                color: Qt.rgba(palette.mid.r, palette.mid.g, palette.mid.b, 90 / 255)
                Rectangle {
                    readonly property int filled: Math.floor(track.width * row.count / bars.most * bars.grow)
                    visible: filled > 0
                    width: Math.max(filled, track.height); height: track.height
                    radius: height / 2
                    color: bars.hoveredRow === row.index ? Qt.lighter(palette.highlight, 1.25) : palette.highlight
                }
            }
            Label {
                x: track.x + track.width + 10; width: 54; height: row.height
                horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                text: Number(row.count).toLocaleString(Qt.locale(), "f", 0)
            }
        }
    }
    HoverHandler {
        id: hover
        onPointChanged: {
            const p = point.position
            let row = -1
            for (let i = 0; i < 5; ++i) if (p.x >= 144 && p.y >= 2 + i * 26 && p.y < 4 + i * 26 + 24) row = i
            bars.hoveredRow = row
        }
        onHoveredChanged: if (!hovered) bars.hoveredRow = -1
    }
    // Its own tool tip: the shared attached one can open with the text of before the row changed
    ToolTip {
        visible: bars.hoveredRow >= 0
        delay: 700
        text: bars.tipText
    }
    readonly property string tipText: {
        if (hoveredRow < 0 || !ratings) return ""
        const count = histogram[4 - hoveredRow]
        return count.toLocaleString(Qt.locale(), "f", 0) + " reviews gave " + (5 - hoveredRow) + " stars · " + (ratings.reviews ? (100 * count / ratings.reviews).toFixed(0) : "0") + " %"
    }
}
