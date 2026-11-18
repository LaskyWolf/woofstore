import QtQuick

// One "On Flathub" list: bands of cards, each fully partitioned into one image card and paired
// small cards, some with a short wide card. Each list keeps its own arrangement for the session, so resizing never reshuffles it.
Item {
    id: masonry
    required property var page
    property var apps: []
    property int fill: 0        // Minimum height; the bands grow to reach it so every list is equally tall
    property int bandCount: 0
    readonly property int count: tiles.count
    readonly property int seed: Math.floor(Math.random() * 4294967296)
    implicitHeight: 1

    function tile(i) { return tiles.itemAt(i) }
    function show(count) { for (let i = 0; i < tiles.count; ++i) tiles.itemAt(i).shown = i < count }
    function roll(band, salt) {
        return ((Math.imul((seed ^ Math.imul(band, 2654435761)) >>> 0, 40503) + salt * 97) >>> 0) >>> 7
    }
    function place(item, x, y, w, h) { item.x = x; item.y = y; item.width = w; item.height = h }

    function arrange() {
        const natural = layout(0, 0)
        if (fill > natural && bandCount > 0) layout(Math.floor((fill - natural) / bandCount), (fill - natural) % bandCount)  // Spread the missing height over all bands
    }
    // Places the cards and returns the height used. Every band gets `extra` pixels more, the last one also `remainder`.
    function layout(extra, remainder) {
        const width = Math.floor(masonry.width)
        const visible = []
        for (let i = 0; i < tiles.count; ++i) if (tiles.itemAt(i).shown) visible.push(tiles.itemAt(i))
        if (visible.length === 0) { implicitHeight = 1; bandCount = 0; return 1 }
        const count = Math.min(visible.length, Math.max(2, Math.min(5, Math.floor(width / 185))))
        const gap = 12
        for (const t of visible) t.spotlight = false
        let offset = 0, y = 0, band = 0
        // ponytail: sections hold a handful of cards; virtualize if a section ever shows hundreds.
        while (offset < visible.length) {
            if (visible.length - offset === 1 && band > 0) { visible[offset].shown = false; break }  // A lone card would stretch across every column
            // Some bands hold a short wide card (two or three columns) next to single cards.
            const bannerSpan = count === 5 && roll(band, 4) % 2 ? 3 : 2
            const lanes = count - bannerSpan + 1
            if (count >= 4 && band > 0 && roll(band, 3) % 3 === 0 && visible.length - offset >= lanes + 1) {
                let banner = -1
                for (let i = 0; i < lanes && banner < 0; ++i) if (visible[offset + i].hasArtwork) banner = i
                if (banner >= 0) {
                    const columnWidth = Math.floor((width - gap * (count - 1)) / count), bannerLane = roll(band, 5) % lanes
                    const row = visible.slice(offset, offset + lanes)
                    row.splice(bannerLane, 0, row.splice(banner, 1)[0])
                    let bandHeight = 0
                    for (let lane = 0; lane < lanes; ++lane)
                        bandHeight = Math.max(bandHeight, row[lane].heightForWidth(lane === bannerLane ? columnWidth * bannerSpan + gap * (bannerSpan - 1) : columnWidth))
                    bandHeight += extra + (offset + lanes >= visible.length ? remainder : 0)
                    let column = 0
                    for (let lane = 0; lane < lanes; ++lane) {
                        const laneSpan = lane === bannerLane ? bannerSpan : 1, x = column * (columnWidth + gap), right = column + laneSpan
                        place(row[lane], x, y, right === count ? width - x : columnWidth * laneSpan + gap * (laneSpan - 1), bandHeight)
                        column = right
                    }
                    offset += lanes; y += bandHeight + gap; ++band
                    continue
                }
            }
            const span = count >= 4 && (band + (seed & 1)) % 2 === 0 ? 2 : 1
            let size = Math.min(visible.length - offset, 2 * (count - span) + 1)
            if (size > 1 && size % 2 === 0) --size
            let chosen = -1
            const candidates = []
            for (let i = 0; i < size; ++i) if (visible[offset + i].hasArtwork) candidates.push(i)
            if (candidates.length) chosen = candidates[roll(band, 1) % candidates.length]
            if (size < 3) chosen = -1  // An image card needs a pair of cards beside it, never a band of its own
            if (chosen < 0) size = Math.min(count, visible.length - offset)
            const group = visible.slice(offset, offset + size)
            const large = chosen < 0 ? null : group.splice(chosen, 1)[0]
            const pairs = large ? Math.floor(group.length / 2) : group.length
            const columns = pairs + (large ? span : 0)
            const columnWidth = Math.floor((width - gap * (columns - 1)) / columns)
            let bandHeight = 0
            if (large) {
                large.spotlight = true
                bandHeight = large.heightForWidth(columnWidth * span + gap * (span - 1))
            }
            for (let i = 0; i < group.length; i += large ? 2 : 1) {
                let h = group[i].heightForWidth(columnWidth)
                if (large) h += gap + group[i + 1].heightForWidth(columnWidth)
                bandHeight = Math.max(bandHeight, h)
            }
            bandHeight += extra + (offset + size >= visible.length ? remainder : 0)
            const largeLane = pairs ? roll(band, 2) % (pairs + 1) : 0
            let column = 0, next = 0
            for (let lane = 0; lane < pairs + (large ? 1 : 0); ++lane) {
                const laneSpan = large && lane === largeLane ? span : 1
                const x = column * (columnWidth + gap)
                const right = column + laneSpan
                const cardWidth = right === columns ? width - x : columnWidth * laneSpan + gap * (laneSpan - 1)
                if (large && lane === largeLane) place(large, x, y, cardWidth, bandHeight)
                else if (large) {
                    const upper = group[next++], lower = group[next++]
                    const upperMinimum = upper.heightForWidth(cardWidth), lowerMinimum = lower.heightForWidth(cardWidth)
                    const upperHeight = upperMinimum + Math.trunc((bandHeight - gap - upperMinimum - lowerMinimum) / 2)
                    place(upper, x, y, cardWidth, upperHeight)
                    place(lower, x, y + upperHeight + gap, cardWidth, bandHeight - upperHeight - gap)
                } else place(group[next++], x, y, cardWidth, bandHeight)
                column = right
            }
            offset += size; y += bandHeight + gap; ++band
        }
        bandCount = band
        implicitHeight = Math.max(1, y - gap)
        return implicitHeight
    }

    Repeater {
        id: tiles
        model: masonry.apps
        ExploreTile {
            required property var modelData
            app: modelData
            page: masonry.page
            // Shown in this list, whichever list is on screen; hidden cards download nothing
            property bool shown: true
            visible: shown
            wantArtwork: shown
            onClicked: masonry.page.openApp(app)
            onHoveredChanged: masonry.page.tileHovered(this, hovered)
            onActiveFocusChanged: masonry.page.tileHovered(this, activeFocus)
        }
    }
}
