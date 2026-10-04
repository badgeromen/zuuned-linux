import QtQuick
import Qt5Compat.GraphicalEffects

// Port of VinylRecordView.swift — spinning vinyl record with chapter
// grooves, gradient progress trail, glowing seek dot, ring-tap chapter
// jump, and cumulative-angle drag seek.
//
// Timing disciplines (session playback inventory, all honored here):
// - 15 Hz interpolated display progress anchored on the 4 Hz real
//   position; anchor snaps on real updates AND on seeks. While the user
//   drags, displayProgress reads the in-flight drag value so the dot
//   tracks the cursor instead of freezing between mpv callbacks.
// - Poster rotation via a 10 Hz accumulator (1 rpm, 0.6°/tick) that
//   only advances while playing && isVisible — the poster freezes where
//   it was and resumes without a wall-clock catch-up jump.
// - Adaptive grooves: one ring per ~45 s, clamped 5–20, with ±1% seeded
//   ring-spacing jitter (stable per count).
// - Drag seek is RELATIVE within the chapter the drag started in; the
//   cumulative-angle accumulator handles the ±180° atan2 seam so
//   crossing it never rewinds.
Item {
    id: root

    property var player: PlayerService
    property url posterSource: ""
    property real size: 400
    // Opt-in (ribbon): enlarge the centre play/pause glyph + its tap zone
    // so it matches the disc's big play target. 1.0 = unchanged everywhere
    // else (mini-player, video), so the OG vinyl is untouched.
    property real playZoneScale: 1.0
    // On-screen flag from the host (chrome hidden ⇒ stop the tickers)
    property bool isVisible: true
    // false in hosts where dragging must move the window instead
    // (music mini-player); ring-tap seek still works either way
    property bool dragSeekEnabled: true
    // Adaptive by duration (one per ~45 s, clamp 5–20); override at will
    property int chapterCount: {
        const dur = player ? player.durationMs : 0
        return dur > 0 ? Math.min(20, Math.max(5, Math.round(dur / 45000))) : 20
    }

    width: size
    height: size
    // Keep all drawing (groove glow, seek-head halos) INSIDE our bounds so
    // the record never bleeds over whatever sits above/around it (headers,
    // titles) — the base fix every host inherits.
    clip: false

    // Single-ring (video "disc") mode: poster at 66% of the disc —
    // slightly tighter than web's 70% so the seek band gets an even
    // gap on both sides (band radius derives from this, see paint).
    // labelFraction is opt-in: the ribbon enlarges the centre; default
    // matches the original everywhere else.
    property real labelFraction: chapterCount === 1 ? 0.33 : 0.20
    readonly property real labelRadius: size * labelFraction

    // ── Interpolated real progress (excludes drag) ──
    property real _anchorMs: 0
    property real _anchorClock: 0
    property real _realProgress: 0

    function _snapAnchor() {
        _anchorMs = player ? player.positionMs : 0
        _anchorClock = Date.now()
        _updateReal()
    }
    function _updateReal() {
        const dur = player ? player.durationMs : 0
        if (dur <= 0) { _realProgress = 0; return }
        let ms = _anchorMs
        if (player.playing)
            ms += Date.now() - _anchorClock
        _realProgress = Math.min(Math.max(ms / dur, 0), 1)
    }

    Connections {
        target: root.player
        function onPositionMsChanged() { root._snapAnchor() }
        function onDurationMsChanged() { root._updateReal() }
        function onPlayingChanged() { root._snapAnchor() }
    }
    Timer {
        interval: 66
        repeat: true
        running: root.player && root.player.playing && root.isVisible
        onTriggered: root._updateReal()
    }

    // ── Drag state (cumulative angular travel) ──
    property bool _dragging: false
    property real _dragLastAngle: 0
    property bool _dragHasAngle: false
    property real _dragAccum: 0
    property int _dragStartChapter: 0

    // Display progress: in-flight drag value while dragging, else real.
    // _dragAccum is UNCLAMPED revolutions — one full turn per chapter,
    // so pulling past a groove's end spirals the dot onto the next
    // ring like a real needle; only the final fraction clamps.
    readonly property real displayProgress: {
        if (_dragging && _dragHasAngle) {
            const chSize = 1.0 / chapterCount
            return Math.min(Math.max(
                _dragStartChapter * chSize + _dragAccum * chSize, 0), 1)
        }
        return _realProgress
    }

    // Throttled scrub while dragging: per-tick exact seeks made video
    // scrubbing laggy (each decodes from a keyframe and they queue).
    // 150ms cadence; players with scrubMs (video) get fast keyframe
    // seeks, others (music) an exact seek — cheap for audio.
    Timer {
        id: scrubTimer
        interval: 150
        repeat: true
        running: root._dragging && root._dragHasAngle
        onTriggered: {
            if (!root.player || root.player.durationMs <= 0)
                return
            const ms = root.displayProgress * root.player.durationMs
            if (root.player.scrubMs !== undefined)
                root.player.scrubMs(ms)
            else
                root.player.seekMs(ms)
        }
    }

    readonly property int currentChapter:
        Math.min(Math.floor(displayProgress * chapterCount), chapterCount - 1)
    readonly property real chapterProgress: {
        const chSize = 1.0 / chapterCount
        return Math.min((displayProgress - currentChapter * chSize) / chSize, 1.0)
    }

    // Seeded groove offsets — port of generateGrooveOffsets (±1% radius)
    readonly property var grooveOffsets: {
        const out = []
        for (let i = 0; i < chapterCount; i++) {
            const seed = Math.sin(i * 12.9898 + 78.233)
            const frac = seed - Math.floor(seed)
            out.push((frac - 0.5) * 0.02)
        }
        return out
    }

    function _seekFraction(frac) {
        if (!player || player.durationMs <= 0)
            return
        const f = Math.min(Math.max(frac, 0), 1)
        player.seekMs(f * player.durationMs)
        // Snap the anchor so the dot lands immediately (mpv echoes at 4 Hz)
        _anchorMs = f * player.durationMs
        _anchorClock = Date.now()
        _updateReal()
    }

    onDisplayProgressChanged: grooves.requestPaint()
    onChapterCountChanged: grooves.requestPaint()

    // ── 1. Glass disc body (ultraThinMaterial stand-in, dark tint) ──
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Qt.rgba(0, 0, 0, 0.45)
        border.width: 1
        border.color: Theme.glassBorder
    }

    // ── 2. Grooves — chapter rings, gradient trail, seek dot ──
    Canvas {
        z: 5
        id: grooves
        // Bigger than the disc so the glow layer has room and isn't clamped
        // into a square at the rim; rings still drawn at the disc radius.
        anchors.centerIn: parent
        width: root.size + 40
        height: root.size + 40
        layer.enabled: true
        layer.effect: Glow {
            radius: 10
            samples: 21
            color: Qt.rgba(0.83, 0.21, 0.48, 0.4)
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2, cy = height / 2
            const r = root.size / 2
            const innerF = 0.45, outerF = 0.95
            const range = outerF - innerF
            const n = root.chapterCount

            // ── Single-ring mode — port of the zuuned-web disc (Tg) ──
            // Rim hairline, then a 12px seek band at 0.83r: dim track,
            // 80-segment pink→orange trail from the top, glowing orange
            // dot with a white core.
            if (n === 1) {
                // Web Tg used absolute px on a 200px disc — scale
                // everything by disc size so the band/dot keep their
                // visual weight at any size.
                const k = root.size / 200
                const bandW = 16 * k
                ctx.beginPath()
                ctx.arc(cx, cy, r - 1, 0, 2 * Math.PI, false)
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08)
                ctx.lineWidth = 1.5
                ctx.stroke()

                // Band CENTERED in the poster→rim annulus (midpoint of
                // the poster edge and the disc edge), not a fixed 0.83r.
                const posterF = root.labelRadius / r
                const v = r * (posterF + 1) / 2
                ctx.beginPath()
                ctx.arc(cx, cy, v, 0, 2 * Math.PI, false)
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08)
                ctx.lineWidth = bandW
                ctx.stroke()

                const startA = -Math.PI / 2
                const prog = root.displayProgress
                const sweep = prog * 2 * Math.PI
                if (prog > 0.001) {
                    // Smooth pink→orange sweep: many thin, slightly
                    // overlapped, fully-opaque segments — translucent
                    // segment ends double-blend at the joints and read
                    // as cuts in the bar.
                    const end = startA + sweep
                    // Rounded start: a half-dot in the start color at
                    // 12 o'clock instead of the stark flat butt edge.
                    ctx.fillStyle = Qt.rgba(0.83, 0.21, 0.48, 1.0)
                    ctx.beginPath()
                    ctx.arc(cx + v * Math.cos(startA),
                            cy + v * Math.sin(startA),
                            bandW / 2, 0, 2 * Math.PI, false)
                    ctx.fill()
                    const segs = 200
                    const half = sweep / segs / 2
                    for (let s = 0; s < segs; s++) {
                        const t = s / segs
                        const a0 = startA + t * sweep
                        const a1 = startA + (s + 1) / segs * sweep + half
                        if (a0 >= end) break
                        ctx.strokeStyle = Qt.rgba(0.83 + t * 0.17,
                                                  0.21 + t * 0.34,
                                                  0.48 - t * 0.48, 1.0)
                        ctx.lineWidth = bandW
                        ctx.beginPath()
                        ctx.arc(cx, cy, v, a0, Math.min(a1, end), false)
                        ctx.stroke()
                    }
                }
                // Head — PINK, larger than the band (volume-handle
                // family: pink fill + white ring), with pink halos.
                const dotA = startA + sweep
                const dx = cx + v * Math.cos(dotA)
                const dy = cy + v * Math.sin(dotA)
                // Just proud of the band (radius 9 vs band 16/2=8 per
                // 200px) — 11 with the big halos read as a lollipop.
                const headR = 9 * k
                for (const halo of [{ r: 16 * k, a: 0.25 }, { r: 11 * k, a: 0.45 }]) {
                    const g2 = ctx.createRadialGradient(dx, dy, 0, dx, dy, halo.r)
                    g2.addColorStop(0, Qt.rgba(0.83, 0.21, 0.48, halo.a))
                    g2.addColorStop(1, Qt.rgba(0.83, 0.21, 0.48, 0))
                    ctx.fillStyle = g2
                    ctx.beginPath(); ctx.arc(dx, dy, halo.r, 0, 2 * Math.PI); ctx.fill()
                }
                ctx.fillStyle = Theme.pink
                ctx.beginPath(); ctx.arc(dx, dy, headR, 0, 2 * Math.PI); ctx.fill()
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.85)
                ctx.lineWidth = 1.5
                ctx.beginPath(); ctx.arc(dx, dy, headR, 0, 2 * Math.PI); ctx.stroke()
                return
            }

            for (let i = 0; i < n; i++) {
                const baseF = innerF + range * (n > 1 ? i / (n - 1) : 1)
                const off = n > 1 && i < root.grooveOffsets.length
                    ? root.grooveOffsets[i] : 0
                const f = Math.min(Math.max(baseF + off, innerF), outerF)
                const gr = r * f

                // Outer ring (high i) = chapter 0, inner = last
                const chapterIndex = n - 1 - i

                if (chapterIndex < root.currentChapter) {
                    // Completed — solid pink, thick
                    ctx.strokeStyle = Qt.rgba(0.83, 0.21, 0.48, 0.6)
                    ctx.lineWidth = 3.0
                    ctx.beginPath()
                    ctx.arc(cx, cy, gr, 0, 2 * Math.PI, false)
                    ctx.stroke()
                } else if (chapterIndex === root.currentChapter) {
                    // CURRENT — 60-segment pink→orange trail from top.
                    // Single-ring mode: the lone band carries the whole
                    // file, so it (and its dot) get real weight.
                    const single = n === 1
                    const trailW = single ? 9.0 : 3.5
                    const restW = single ? 7.0 : 2.0
                    const startA = 270 * Math.PI / 180
                    const cp = root.chapterProgress
                    const segments = 60
                    const played = Math.floor(segments * cp)
                    for (let s = 0; s < played; s++) {
                        const segF = s / Math.max(played - 1, 1)
                        const cr = 0.83 + segF * 0.17
                        const cg = 0.21 + segF * 0.34
                        const cb = 0.48 - segF * 0.48
                        const a0 = startA + (s / segments) * 2 * Math.PI
                        const a1 = startA + ((s + 1) / segments) * 2 * Math.PI
                        ctx.strokeStyle = Qt.rgba(cr, cg, cb, 0.8)
                        ctx.lineWidth = trailW
                        ctx.beginPath()
                        ctx.arc(cx, cy, gr, a0, a1, false)
                        ctx.stroke()
                    }
                    // Unplayed remainder — dim but visible
                    if (cp < 1.0) {
                        ctx.strokeStyle = Qt.rgba(1, 1, 1, single ? 0.12 : 0.08)
                        ctx.lineWidth = restW
                        ctx.beginPath()
                        ctx.arc(cx, cy, gr,
                                startA + cp * 2 * Math.PI,
                                startA + 2 * Math.PI, false)
                        ctx.stroke()
                    }
                    // Triple-layer glowing orange seek dot
                    const dotA = startA + cp * 2 * Math.PI
                    const dx = cx + gr * Math.cos(dotA)
                    const dy = cy + gr * Math.sin(dotA)
                    const dotS = single ? 1.35 : 1.0
                    ctx.fillStyle = Qt.rgba(1.0, 0.55, 0.0, 0.35)
                    ctx.beginPath(); ctx.arc(dx, dy, 12 * dotS, 0, 2 * Math.PI); ctx.fill()
                    ctx.fillStyle = Qt.rgba(1.0, 0.55, 0.0, 0.6)
                    ctx.beginPath(); ctx.arc(dx, dy, 7 * dotS, 0, 2 * Math.PI); ctx.fill()
                    ctx.fillStyle = Theme.orange
                    ctx.beginPath(); ctx.arc(dx, dy, 5 * dotS, 0, 2 * Math.PI); ctx.fill()
                } else {
                    // Future — dim
                    ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.08)
                    ctx.lineWidth = 2.0
                    ctx.beginPath()
                    ctx.arc(cx, cy, gr, 0, 2 * Math.PI, false)
                    ctx.stroke()
                }
            }
        }
    }

    // ── 3. Center label — spinning poster or dark label + glyph ──
    property real _posterRotation: 0
    Timer {
        interval: 100   // 10 Hz accumulator, 60 s/rev ⇒ 0.6°/tick
        repeat: true
        running: root.player && root.player.playing && root.isVisible
        onTriggered: root._posterRotation += 0.6
    }

    Item {
        anchors.centerIn: parent
        width: root.labelRadius * 2
        height: root.labelRadius * 2

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Qt.rgba(0.08, 0.08, 0.08, 1)
        }

        ArtworkImage {
            anchors.fill: parent
            source: root.posterSource
            visible: source !== "" && status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            rotation: root._posterRotation
            layer.enabled: visible
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: root.labelRadius * 2
                    height: root.labelRadius * 2
                    radius: root.labelRadius
                }
            }
        }

        // Play/pause indicator — loud play when paused, subtle pause
        // while playing (tap target stays discoverable)
        Item {
            anchors.centerIn: parent
            width: root.labelRadius * 0.55 * root.playZoneScale
            height: width
            opacity: root.player && root.player.playing ? 0.4 : 0.92
            Behavior on opacity { NumberAnimation { duration: 200 } }

            Row {
                anchors.centerIn: parent
                spacing: parent.width * 0.24
                visible: root.player ? root.player.playing : false
                Rectangle { width: parent.parent.width * 0.26; height: parent.parent.height; radius: 2; color: "#ffffff" }
                Rectangle { width: parent.parent.width * 0.26; height: parent.parent.height; radius: 2; color: "#ffffff" }
            }
            Canvas {
                anchors.fill: parent
                visible: root.player ? !root.player.playing : true
                onVisibleChanged: if (visible) requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#ffffff"
                    ctx.beginPath()
                    ctx.moveTo(width * 0.12, 0)
                    ctx.lineTo(width * 0.12, height)
                    ctx.lineTo(width, height / 2)
                    ctx.closePath()
                    ctx.fill()
                }
                Component.onCompleted: requestPaint()
            }
        }
    }

    // ── 4. Center hole dot ──
    Rectangle {
        anchors.centerIn: parent
        width: root.size * 0.04
        height: width
        radius: width / 2
        color: Qt.rgba(0.15, 0.15, 0.15, 1)
    }

    // ── Interaction: tap (label = play/pause, ring = chapter jump) and
    //    cumulative-angle drag seek within the starting chapter ──
    MouseArea {
        anchors.fill: parent

        property real pressX: 0
        property real pressY: 0

        onPressed: function(mouse) {
            pressX = mouse.x
            pressY = mouse.y
            root._dragging = false
            root._dragHasAngle = false
        }

        onPositionChanged: function(mouse) {
            if (!root.dragSeekEnabled || !pressed)
                return
            if (!root._dragging) {
                const mdx = mouse.x - pressX, mdy = mouse.y - pressY
                if (Math.sqrt(mdx * mdx + mdy * mdy) < 5)
                    return   // below the mac's 5pt drag threshold
                root._dragging = true
            }

            const dx = mouse.x - root.width / 2
            const dy = mouse.y - root.height / 2
            const current = Math.atan2(dy, dx) * 180 / Math.PI

            if (!root._dragHasAngle) {
                // Seed from the REAL progress (not displayProgress — at
                // this instant it would read the zeroed drag state)
                const chSize = 1.0 / root.chapterCount
                const startChapter = Math.min(
                    Math.floor(root._realProgress * root.chapterCount),
                    root.chapterCount - 1)
                const chapterStart = startChapter * chSize
                root._dragStartChapter = startChapter
                root._dragAccum = Math.min(Math.max(
                    (root._realProgress - chapterStart) / chSize, 0), 1)
                root._dragLastAngle = current
                root._dragHasAngle = true
                // Video: pause for the scrub — seeking while PLAYING
                // restarts from the same keyframe every tick (the
                // stuck-repeat bug); paused seeks just update the frame.
                if (root.player && root.player.beginScrub !== undefined)
                    root.player.beginScrub()
                return
            }

            let delta = current - root._dragLastAngle
            if (delta > 180) delta -= 360
            else if (delta < -180) delta += 360
            root._dragLastAngle = current

            // Unclamped — crossing a groove boundary carries the drag
            // onto the neighboring ring. Seeks happen on scrubTimer's
            // 150ms cadence, not per mouse tick.
            root._dragAccum += delta / 360.0
        }

        onReleased: function(mouse) {
            const wasDragging = root._dragging
            const finalFrac = root.displayProgress
            root._dragging = false
            root._dragHasAngle = false
            if (wasDragging) {
                // Land exactly where the needle was dropped; video
                // resumes playback via endScrubMs.
                if (root.player && root.player.endScrubMs !== undefined
                        && root.player.durationMs > 0) {
                    const f = Math.min(Math.max(finalFrac, 0), 1)
                    root.player.endScrubMs(f * root.player.durationMs)
                    root._anchorMs = f * root.player.durationMs
                    root._anchorClock = Date.now()
                    root._updateReal()
                } else {
                    root._seekFraction(finalFrac)
                }
                return
            }

            // TAP — inside the label = play/pause; on a ring = chapter jump
            const dx = mouse.x - root.width / 2
            const dy = mouse.y - root.height / 2
            const dist = Math.sqrt(dx * dx + dy * dy)
            if (dist > root.size / 2)
                return   // outside the disc circle

            if (dist < root.labelRadius * root.playZoneScale) {
                if (root.player)
                    root.player.playPause()
                return
            }

            // Single-ring mode: a tap on the ring seeks by ANGLE from
            // the top (the ring IS the whole file). Multi-groove mode
            // keeps the radial chapter jump.
            if (root.chapterCount === 1) {
                let deg = Math.atan2(dy, dx) * 180 / Math.PI - 270
                while (deg < 0) deg += 360
                root._seekFraction(deg / 360)
                return
            }
            const outerR = root.size / 2 * 0.95
            const innerR = root.size / 2 * 0.45
            const chapterFrac = (outerR - dist) / (outerR - innerR)
            const tapped = Math.min(Math.max(
                Math.floor(chapterFrac * root.chapterCount), 0),
                root.chapterCount - 1)
            root._seekFraction(tapped / root.chapterCount)
        }
    }
}
