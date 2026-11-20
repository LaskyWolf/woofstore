import QtQuick

// Wheel and trackpad scrolling for a Flickable: declare `PageWheel { target: flick }` inside it.
// A mouse wheel moves 20 px per scroll line (the desktop's lines per notch, 3 by default) with a short glide.
// A trackpad moves the content with the fingers; when they lift, it glides on for a moment and slows down.
// It sits under the content, so items that take the wheel themselves (the Explore carousel) still get it first.
MouseArea {
    id: area
    required property Flickable target
    readonly property real motion: typeof catalog === "undefined" ? 1 : catalog.animationFactor()  // KDE's animation speed (Main's catalog); 0 turns the glides off
    property var samples: []  // The trackpad's last movements: {t: ms, dy: px}

    parent: target
    anchors.fill: parent
    z: -1
    acceptedButtons: Qt.NoButton  // Wheel only: clicks and the scroll bar work as before
    // The Flickable would also move on the same events and fight this handler; not interactive, it leaves them here
    Component.onCompleted: target.interactive = false

    onWheel: wheel => scroll(wheel)

    // Also for items over the page that must not keep the wheel (Explore's tab bar)
    function scroll(wheel) {
        wheel.accepted = true  // Every wheel event ends here, even one that moves nothing
        const p = wheel.pixelDelta, a = wheel.angleDelta
        // A trackpad gesture has phases (fingers down, moving, lifted); a mouse notch has none. Without phases (X11), pixels
        // that differ from the angles still come from a trackpad: they move the content, with no glide at the end.
        const phased = wheel.phase !== Qt.NoScrollPhase
        const fingers = phased ? wheel.phase !== Qt.ScrollEnd : (p.x !== 0 || p.y !== 0) && (a.x !== p.x || a.y !== p.y)
        const lifted = wheel.phase === Qt.ScrollEnd
        if (fingers) {
            glide.stop()
            const now = Date.now()
            target.contentY = clamp(target.contentY - p.y)
            samples = samples.filter(s => now - s.t < 80).concat([{ t: now, dy: -p.y }])
        } else if (lifted) {
            const now = Date.now()
            const recent = samples.filter(s => now - s.t < 80)
            samples = []
            if (recent.length < 2) return
            const speed = recent.reduce((sum, s) => sum + s.dy, 0) / Math.max(16, now - recent[0].t) * 1000  // px/s
            const braking = 5000  // px/s²: a 2000 px/s swipe glides 400 px in 0.4 s
            if (Math.abs(speed) < 100) return
            glideTo(target.contentY + speed * Math.abs(speed) / (2 * braking), Math.abs(speed) / braking * 1000, Easing.OutQuad, false)
        } else if (a.y !== 0) {
            samples = []
            const from = glide.running ? glide.to : target.contentY  // Notches in a row add up
            glideTo(from - a.y / 120 * 20 * Qt.styleHints.wheelScrollLines, 150, Easing.OutCubic, true)
        }
    }

    function clamp(y) {
        return Math.max(target.originY, Math.min(y, target.originY + Math.max(0, target.contentHeight - target.height)))
    }

    // `scaled`: the wheel glide follows KDE's animation speed; the trackpad glide keeps its physics, only 0 turns it off
    function glideTo(y, duration, easing, scaled) {
        glide.stop()
        y = clamp(y)
        if (scaled) duration *= motion
        if (motion === 0 || duration < 16) { target.contentY = y; return }
        glide.to = y
        glide.duration = duration
        glide.easing.type = easing
        glide.start()
    }

    NumberAnimation {
        id: glide
        target: area.target
        property: "contentY"
    }
}
