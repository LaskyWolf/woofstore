import QtQuick
import QtQuick.Controls

// Installs per day as a smooth curve you can hover. `daily`: [{date, n}].
Canvas {
    id: chart
    property var daily: []
    property int duration: 0
    property int hovered: -1
    property real grow: 0
    readonly property color accent: palette.highlight
    implicitHeight: 96
    Accessible.name: "Installs per day over the last " + daily.length + " days"
    onDailyChanged: if (duration > 0) growth.restart(); else { grow = 1; requestPaint() }
    onGrowChanged: requestPaint()
    onHoveredChanged: requestPaint()
    onWidthChanged: requestPaint()
    NumberAnimation { id: growth; target: chart; property: "grow"; from: 0; to: 1; duration: chart.duration; easing.type: Easing.OutCubic }

    function point(i, area, most) {
        return { x: area.x + area.w * i / (daily.length - 1), y: area.y + area.h - area.h * daily[i].n / most * grow }
    }
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (daily.length < 2) return
        const area = { x: 2, y: 8, w: width - 4, h: height - 12 }
        const most = Math.max(1, ...daily.map(d => d.n))
        const a = accent
        ctx.beginPath()
        let p = point(0, area, most)
        ctx.moveTo(p.x, p.y)
        for (let i = 1; i < daily.length; ++i) {
            const from = point(i - 1, area, most), to = point(i, area, most), mid = (from.x + to.x) / 2
            ctx.bezierCurveTo(mid, from.y, mid, to.y, to.x, to.y)
        }
        ctx.save()
        ctx.lineTo(area.x + area.w, area.y + area.h)
        ctx.lineTo(area.x, area.y + area.h)
        ctx.closePath()
        const shade = ctx.createLinearGradient(0, area.y, 0, area.y + area.h)
        shade.addColorStop(0, Qt.rgba(a.r, a.g, a.b, 110 / 255))
        shade.addColorStop(1, Qt.rgba(a.r, a.g, a.b, 8 / 255))
        ctx.fillStyle = shade
        ctx.fill()
        ctx.restore()
        // The line again, without the closing edges
        ctx.beginPath()
        ctx.moveTo(p.x, p.y)
        for (let i = 1; i < daily.length; ++i) {
            const from = point(i - 1, area, most), to = point(i, area, most), mid = (from.x + to.x) / 2
            ctx.bezierCurveTo(mid, from.y, mid, to.y, to.x, to.y)
        }
        ctx.strokeStyle = a
        ctx.lineWidth = 2
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.stroke()
        if (hovered >= 0) {
            const at = point(hovered, area, most), ink = palette.windowText
            ctx.beginPath()
            ctx.strokeStyle = Qt.rgba(ink.r, ink.g, ink.b, 60 / 255)
            ctx.lineWidth = 1
            ctx.moveTo(at.x, area.y); ctx.lineTo(at.x, area.y + area.h)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(at.x, at.y, 5, 0, 2 * Math.PI)
            ctx.fillStyle = a
            ctx.fill()
            ctx.strokeStyle = palette.window
            ctx.lineWidth = 2
            ctx.stroke()
        }
    }
    HoverHandler {
        onPointChanged: {
            if (chart.daily.length < 2) return
            chart.hovered = Math.max(0, Math.min(chart.daily.length - 1, Math.round((point.position.x - 2) / (chart.width - 4) * (chart.daily.length - 1))))
        }
        onHoveredChanged: if (!hovered) chart.hovered = -1
    }
    // Its own tool tip: the shared attached one can open with the text of before the point changed
    ToolTip { visible: chart.hovered >= 0; text: chart.tipText }
    readonly property string tipText: {
        if (hovered < 0 || !daily[hovered]) return ""
        const parts = daily[hovered].date.split("-").map(Number)
        return new Date(parts[0], parts[1] - 1, parts[2]).toLocaleDateString(Qt.locale(), Locale.ShortFormat) + " · " + daily[hovered].n.toLocaleString(Qt.locale(), "f", 0) + " installs"
    }
}
