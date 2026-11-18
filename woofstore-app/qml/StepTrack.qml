import QtQuick

// Steps as nodes on one line. The line fills as the job moves from the first step to the last.
Canvas {
    id: track
    property var names: []
    property int step: 0
    property real fraction: 0
    property bool failed: false
    property bool finished: false
    property real shown: 0
    property real glow: 0
    readonly property bool filled: step >= names.length

    implicitHeight: 68
    // At least 520 px; wider when the step names need it, so neighbouring labels never touch.
    // The nodes sit 56 px in from each end, and the active name is bold.
    implicitWidth: Math.max(520, 2 * 56 + (names.length - 1) * (Math.max(0, ...names.map(n => boldMetrics.advanceWidth(n))) + 16))
    FontMetrics { id: boldMetrics; font.family: metrics.font.family; font.pointSize: metrics.font.pointSize; font.bold: true }

    // Steps already done count 1 each; the active step adds its own progress, or a little while its progress is unknown.
    // Only a finished job reaches the last node.
    readonly property real position: {
        const last = names.length - 1
        return step >= names.length ? last : Math.min(last - 0.08, step + (fraction > 0 ? fraction * 0.9 : 0.35))
    }
    onPositionChanged: if (!finished && visible) { fill.stop(); fill.from = shown; fill.to = position; fill.start() } else shown = position
    Component.onCompleted: shown = position
    NumberAnimation { id: fill; target: track; property: "shown"; duration: 450; easing.type: Easing.OutCubic }
    NumberAnimation on glow { from: 0; to: 1; duration: 1300; loops: Animation.Infinite; running: !track.finished }
    onFinishedChanged: if (finished) { glow = 0; fill.stop(); shown = position }
    onShownChanged: requestPaint()
    onGlowChanged: requestPaint()
    onWidthChanged: requestPaint()
    onNamesChanged: requestPaint()

    FontMetrics { id: metrics }

    onPaint: {
        const p = getContext("2d")
        p.reset()
        const n = names.length
        if (n < 2) return
        const margin = 56, y = 22, radius = 11, span = (width - 2 * margin) / (n - 1)
        const accent = palette.highlight, idle = palette.mid, danger = "#da4453"
        const x = i => margin + i * span
        const rounded = (left, top, w, h, r) => { p.beginPath(); p.roundedRect(left, top, w, h, r, r); p.fill() }
        p.fillStyle = palette.base
        rounded(x(0), y - 2.5, span * (n - 1), 5, 2.5)
        p.fillStyle = failed ? danger : accent
        rounded(x(0), y - 2.5, span * Math.max(0, Math.min(shown, n - 1)), 5, 2.5)
        for (let i = 0; i < n; ++i) {
            const done = i < step, active = i === step && !filled, cx = x(i)
            if (active && !failed) {
                p.fillStyle = Qt.alpha(accent, 0.35 * (1 - glow))
                p.beginPath(); p.arc(cx, y, radius + 10 * glow, 0, 2 * Math.PI); p.fill()
            }
            p.beginPath(); p.arc(cx, y, radius, 0, 2 * Math.PI)
            p.fillStyle = done || filled ? accent : active ? (failed ? danger : accent) : palette.window
            p.fill()
            if (!(done || active || filled)) { p.lineWidth = 2; p.strokeStyle = idle; p.stroke() }
            p.strokeStyle = "white"; p.lineWidth = 2.4; p.lineCap = "round"; p.lineJoin = "round"
            if (done || filled) {
                p.beginPath(); p.moveTo(cx - 4.5, y); p.lineTo(cx - 1, y + 3.5); p.lineTo(cx + 5, y - 3.5); p.stroke()
            } else if (active && failed) {
                p.beginPath(); p.moveTo(cx - 3.5, y - 3.5); p.lineTo(cx + 3.5, y + 3.5); p.moveTo(cx - 3.5, y + 3.5); p.lineTo(cx + 3.5, y - 3.5); p.stroke()
            } else if (active) {
                p.fillStyle = "white"; p.beginPath(); p.arc(cx, y, 3.5, 0, 2 * Math.PI); p.fill()
            }
            // Canvas reads a CSS font string ("bold 10pt 'Noto Sans'"), not a font object
            const f = metrics.font
            p.font = (active ? "bold " : "") + (f.pointSize > 0 ? f.pointSize + "pt" : f.pixelSize + "px") + " \"" + f.family + "\""
            p.fillStyle = active || done || filled ? palette.text : palette.placeholderText
            p.textAlign = "center"; p.textBaseline = "top"
            p.fillText(names[i], cx, y + radius + 8)
        }
    }
}
