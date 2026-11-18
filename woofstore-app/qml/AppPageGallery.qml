import QtQuick

// The screenshot strip of the app page: images keep their aspect ratio inside 76 % of the width and
// 96 % of the height, sit one 12 px gap apart and peek in from the sides. It wraps around; drag, click a side or use Left/Right.
// The wheel scrolls the page.
Item {
    id: gallery
    property var urls: []
    property int duration: 300
    property int current: 0
    property real position: 0
    // Downloaded screenshots in publisher order, however downloads finish: {url, w, h}
    property var images: []
    implicitHeight: Math.max(220, Math.min(420, Math.floor(width * .5)))
    activeFocusOnTab: true
    clip: true
    Accessible.name: "App screenshots; use Left and Right to browse"

    function reset() { slide.stop(); current = 0; position = 0 }
    // Another app's addresses: drop the old pictures at once, an app without screenshots loads nothing that would replace them.
    // Compared as text, since the page parses a new list each time the catalogue changes.
    readonly property string urlKey: JSON.stringify(urls)
    onUrlKeyChanged: { images = []; reset(); Qt.callLater(refresh) }
    function refresh() {
        const ready = []
        for (let i = 0; i < loaders.count; ++i) {
            const image = loaders.itemAt(i)
            if (image && image.status === Image.Ready) ready.push({ url: image.source, w: image.implicitWidth, h: image.implicitHeight })
        }
        const selected = current
        images = ready
        if (ready.length > 0) select(Math.min(selected, ready.length - 1), false)
    }
    function select(index, animate) {
        const count = images.length
        if (!count) return
        index = (index % count + count) % count
        slide.stop()
        current = index
        let target = index
        while (target - position > count / 2) target -= count
        while (position - target > count / 2) target += count
        if (animate && duration > 0) { slide.from = position; slide.to = target; slide.duration = duration; slide.start() }
        else position = index
    }
    NumberAnimation { id: slide; target: gallery; property: "position"; easing.type: Easing.OutCubic; onFinished: gallery.position = gallery.current }

    Repeater {
        id: loaders
        model: gallery.urls
        Image {
            required property string modelData
            visible: false
            source: modelData
            asynchronous: true
            onStatusChanged: gallery.refresh()
        }
    }

    // Up to five images around the current one, nearest drawn last
    readonly property var placed: {
        const count = images.length
        if (!count || width <= 0) return []
        const bounds = { w: width * .76, h: height * .96 }
        const wrapped = i => (i % count + count) % count
        const sizeOf = i => { const im = images[wrapped(i)]; const s = Math.min(bounds.w / im.w, bounds.h / im.h); return { w: im.w * s, h: im.h * s } }
        // Neighbours sit one gap away from the image's real edge, so small or wide images still show a peek of the next one.
        const step = i => (sizeOf(i).w + sizeOf(i + 1).w) / 2 + 12
        const base = Math.floor(position)
        const centers = {}
        centers[base] = width / 2 - (position - base) * step(base)
        for (let i = base + 1; i <= base + 2; ++i) centers[i] = centers[i - 1] + step(i - 1)
        for (let i = base - 1; i >= base - 2; --i) centers[i] = centers[i + 1] - step(i)
        const order = count === 1 ? [0] : [base - 2, base - 1, base, base + 1, base + 2]
        const result = []
        for (const i of order) {
            const size = sizeOf(i)
            const x = (count === 1 ? width / 2 : centers[i]) - size.w / 2
            if (x + size.w < 0 || x > width) continue
            result.push({ url: images[wrapped(i)].url, x: Math.round(x), y: Math.round(height / 2 - size.h / 2), w: Math.round(size.w), h: Math.round(size.h), z: -Math.abs(i - position) })
        }
        return result
    }
    Repeater {
        model: 5
        Image {
            required property int index
            readonly property var spot: gallery.placed[index] ?? null
            visible: spot !== null
            source: spot ? spot.url : ""
            x: spot ? spot.x : 0
            y: spot ? spot.y : 0
            width: spot ? spot.w : 0
            height: spot ? spot.h : 0
            z: spot ? spot.z : 0
            smooth: true
            mipmap: true
        }
    }

    // Keyboard focus draws a Highlight underline; mouse focus draws nothing
    Rectangle {
        visible: gallery.activeFocus && (gallery.focusReason === Qt.TabFocusReason || gallery.focusReason === Qt.BacktabFocusReason)
        x: 4; y: gallery.height - 3
        width: gallery.width - 8; height: 2
        color: palette.highlight
        z: 10
    }
    Keys.onLeftPressed: select(current - 1, true)
    Keys.onRightPressed: select(current + 1, true)
    MouseArea {
        property real start
        anchors.fill: parent
        onPressed: mouse => { start = mouse.x; gallery.forceActiveFocus(Qt.MouseFocusReason) }
        onReleased: mouse => {
            const drag = mouse.x - start
            if (Math.abs(drag) > 30) gallery.select(gallery.current + (drag < 0 ? 1 : -1), true)
            else if (mouse.x < gallery.width * .2) gallery.select(gallery.current - 1, true)
            else if (mouse.x > gallery.width * .8) gallery.select(gallery.current + 1, true)
        }
    }
}
