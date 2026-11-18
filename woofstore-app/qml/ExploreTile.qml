import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Shapes

// One Explore card: a featured slide (`hero`), a mosaic card, or the mosaic's hover preview.
// The card is tinted with its icon's colour; publisher screenshots fill the image side.
AbstractButton {
    id: tile
    required property var app          // An entry of Catalog.featured(): key, name, summary, icon, screenshot, group, sources
    required property var page         // ExplorePage: categoryColors, ops
    property bool hero: false
    property bool spotlight: false     // The band's image card: screenshot above (or below) the copy
    property bool artworkBelow: false
    property bool quickInstall: hero   // The Install button in the corner
    property string summary: app.summary ?? ""
    property bool wantArtwork: hero    // Screenshots download once a card is first shown
    property color sampled: "transparent"
    // A card that is handed its colour (the hover preview) samples nothing
    property color tintOverride: "transparent"

    readonly property bool hasArtwork: (app.screenshot ?? "") !== ""
    readonly property bool artworkReady: artwork.status === Image.Ready
    readonly property color tint: tintOverride.a > 0 ? tintOverride : sampled.a > 0 ? sampled : (page.categoryColors[app.group] ?? "#5c567e")
    readonly property color wash: Qt.rgba(tint.r, tint.g, tint.b, (hero ? 70 : 85) / 255)
    readonly property color surface: {
        const a = wash.a, base = palette.button
        return Qt.rgba(base.r * (1 - a) + tint.r * a, base.g * (1 - a) + tint.g * a, base.b * (1 - a) + tint.b * a, 1)
    }
    readonly property bool wide: !hero && !spotlight && hasArtwork && width >= 340
    readonly property int textWidth: wide ? Math.floor(width * .54) : width
    readonly property int split: Math.floor(width * 46 / 100)
    // Height of the copy above or below a spotlight card's image
    readonly property int copyHeight: 110 + (copyMeasure.text === "" ? 0 : Math.ceil(copyMeasure.contentHeight))
    readonly property int copyTop: spotlight && !artworkBelow ? Math.max(0, height - copyHeight) : 0

    function summaryHeight(w) {
        measure.width = Math.max(80, w)
        return measure.text === "" ? 0 : Math.ceil(measure.contentHeight)
    }
    function heightForWidth(w) {
        if (hero) return w < 650 ? 220 : 246
        const text = !spotlight && hasArtwork && w >= 340 ? Math.floor(w * .54) : w
        return (spotlight ? Math.min(Math.floor(w / 2), 280) + 110 : 110) + summaryHeight(text - 28)
    }

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: "Open " + (app.name ?? "")
    ToolTip.visible: hovered && !page.previewing
    ToolTip.delay: 700
    ToolTip.text: (app.name ?? "") + " — " + (app.summary ?? "")
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    background: null
    contentItem: null

    Text { id: measure; visible: false; text: tile.summary; wrapMode: Text.Wrap; textFormat: Text.PlainText }
    Text { id: copyMeasure; visible: false; width: Math.max(80, tile.width - 28); text: tile.summary; wrapMode: Text.Wrap; textFormat: Text.PlainText }

    // The card's colour comes from its icon: saturated pixels count most. Icons without colour use the category colour.
    Canvas {
        width: 32
        height: 32
        opacity: 0
        enabled: false
        visible: tile.tintOverride.a === 0
        readonly property string iconUrl: {
            const icon = tile.app.icon ?? ""
            return icon.startsWith("/") ? "file://" + icon : icon.includes("://") ? icon : icon === "" ? "" : "image://icon/" + icon + "|applications-other"
        }
        onIconUrlChanged: { tile.sampled = "transparent"; if (iconUrl !== "") loadImage(iconUrl) }
        onImageLoaded: requestPaint()
        onPaint: {
            if (iconUrl === "" || !isImageLoaded(iconUrl)) return
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, 32, 32)
            ctx.drawImage(iconUrl, 0, 0, 32, 32)
            const d = ctx.getImageData(0, 0, 32, 32).data
            let r = 0, g = 0, b = 0, total = 0
            for (let i = 0; i < d.length; i += 4) {
                const max = Math.max(d[i], d[i + 1], d[i + 2]), min = Math.min(d[i], d[i + 1], d[i + 2])
                const s = max === 0 ? 0 : (max - min) / max
                const w = d[i + 3] / 255 * s * s * (.3 + max / 255)
                r += d[i] * w; g += d[i + 1] * w; b += d[i + 2] * w; total += w
            }
            // Scaled by the device pixel ratio the canvas holds more pixels than 32 x 32; the threshold is per 32 x 32
            if (total * 4096 / d.length <= 3) { tile.sampled = "transparent"; return }
            const c = Qt.rgba(r / total / 255, g / total / 255, b / total / 255, 1)
            const h = c.hsvHue < 0 ? 0 : c.hsvHue
            tile.sampled = Qt.hsva(h, Math.min(.85, Math.max(.5, c.hsvSaturation)), Math.min(.8, Math.max(.5, c.hsvValue)), 1)  // Never washed out, never glaring
        }
    }

    // Everything painted inside the rounded card is clipped to it
    Item {
        id: body
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
        Rectangle { anchors.fill: parent; color: tile.palette.button }
        Rectangle { anchors.fill: parent; color: tile.wash }

        // The screenshot: right of the copy (hero, wide card) or above/below it (spotlight)
        Item {
            id: picture
            visible: tile.artworkReady && (tile.hero || tile.wide || tile.spotlight)
            x: tile.hero ? tile.split : tile.wide ? tile.textWidth : 0
            y: tile.spotlight && tile.artworkBelow ? tile.copyHeight : 0
            width: tile.width - x
            height: tile.spotlight ? Math.max(0, tile.height - tile.copyHeight) : tile.height
            clip: true
            Image {
                id: artwork
                anchors.fill: parent
                source: tile.wantArtwork && tile.hasArtwork ? tile.app.screenshot : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                mipmap: true
            }
            // Hero: a radial wash from above the image's leading edge, then a join that keeps the seam continuous
            Shape {
                visible: tile.hero
                anchors.fill: parent
                ShapePath {
                    strokeWidth: -1
                    fillGradient: RadialGradient {
                        centerX: 0; centerY: -tile.height * .12
                        focalX: 0; focalY: -tile.height * .12
                        centerRadius: picture.width * 1.15
                        focalRadius: 0
                        GradientStop { position: 0; color: tile.surface }
                        GradientStop { position: .28; color: tile.surface }
                        GradientStop { position: 1; color: Qt.rgba(tile.surface.r, tile.surface.g, tile.surface.b, 0) }
                    }
                    PathRectangle { width: picture.width; height: picture.height }
                }
            }
            Rectangle {
                visible: tile.hero || tile.wide
                width: picture.width * (tile.hero ? .32 : .4)
                height: picture.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: tile.surface }
                    GradientStop { position: 1; color: Qt.rgba(tile.surface.r, tile.surface.g, tile.surface.b, 0) }
                }
            }
        }

        // A soft light follows the pointer
        Shape {
            id: light
            visible: tile.hovered
            anchors.fill: parent
            readonly property color glow: Qt.lighter(tile.tint, 1.5)
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: pointer.point.position.x; centerY: pointer.point.position.y
                    focalX: centerX; focalY: centerY
                    centerRadius: Math.max(tile.width, tile.height) * .6
                    focalRadius: 0
                    GradientStop { position: 0; color: Qt.rgba(light.glow.r, light.glow.g, light.glow.b, 80 / 255) }
                    GradientStop { position: 1; color: Qt.rgba(light.glow.r, light.glow.g, light.glow.b, 0) }
                }
                PathRectangle { width: tile.width; height: tile.height }
            }
        }
    }
    Item {
        id: mask
        anchors.fill: parent
        layer.enabled: true
        visible: false
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: 10 }
    }
    HoverHandler { id: pointer; cursorShape: Qt.PointingHandCursor }

    // ── Copy ──
    AppIcon {
        x: tile.hero ? 24 : 14
        y: tile.hero ? 24 : tile.copyTop + 14
        width: tile.hero ? 56 : 44
        height: width
        icon: tile.app.icon ?? ""
        fallback: "applications-other"
    }
    Text {
        visible: tile.hero
        x: 96; y: 24
        width: tile.split - 114; height: 60
        text: tile.app.name ?? ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        verticalAlignment: Text.AlignVCenter
        font.pointSize: tile.width < 650 ? 18 : 22
        font.bold: true
        color: tile.palette.windowText
        clip: true
    }
    Text {
        visible: tile.hero
        x: 24; y: 106
        width: tile.split - 48; height: Math.max(0, tile.height - 145)
        text: tile.summary
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font.pointSize: 13
        color: tile.palette.windowText
        clip: true
    }
    Text {
        visible: !tile.hero
        x: 14; y: tile.copyTop + 70
        width: tile.textWidth - 28; height: 24
        text: tile.app.name ?? ""
        textFormat: Text.PlainText
        verticalAlignment: Text.AlignVCenter
        font.bold: true
        color: tile.palette.windowText
        clip: true
    }
    Text {
        visible: !tile.hero
        x: 14; y: tile.copyTop + 100
        width: tile.textWidth - 28; height: Math.max(0, tile.height - tile.copyTop - 106)
        text: tile.summary
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: tile.palette.windowText
        clip: true
    }

    // Hover edge picks up the card colour; keyboard focus draws a Highlight ring
    Rectangle {
        visible: tile.hovered
        anchors.fill: parent
        anchors.margins: 1
        radius: 10
        color: "transparent"
        border.width: 1.5
        border.color: { const c = Qt.lighter(tile.tint, 1.6); return Qt.rgba(c.r, c.g, c.b, tile.down ? 1 : 190 / 255) }
    }
    Rectangle {
        visible: tile.visualFocus
        anchors.fill: parent
        anchors.margins: 2
        radius: 10
        color: "transparent"
        border.width: 2
        border.color: tile.palette.highlight
    }

    Button {
        id: install
        visible: tile.quickInstall
        readonly property string label: tile.page.ops.label(tile.app)
        width: 130
        height: 36
        x: Math.max(14, tile.width - width - (tile.hero ? 24 : 14))
        y: tile.hero ? Math.max(14, tile.height - height - 24) : 14
        enabled: !tile.page.ops.busy(tile.app)
        Accessible.name: "Quick install " + (tile.app.name ?? "")
        onClicked: tile.page.ops.primary(tile.app, "")
        contentItem: Item {
            implicitWidth: row.implicitWidth
            implicitHeight: row.implicitHeight
            Row {
                id: row
                anchors.centerIn: parent
                spacing: 6
                Lucide {
                    visible: install.label === "Open"
                    name: "external-link"
                    color: install.palette.buttonText
                    width: 20; height: 20
                    anchors.verticalCenter: parent.verticalCenter
                }
                AppIcon {
                    visible: install.label !== "Open"
                    icon: "system-software-install"
                    width: 16; height: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label { text: install.label; color: install.palette.buttonText; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
