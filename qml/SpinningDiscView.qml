import QtQuick
import Qt5Compat.GraphicalEffects

// Port of SpinningDiscView (QueueRibbonBar.swift) — simplified vinyl
// disc for the queue ribbon: circular album art spinning while playing,
// a pink→orange gradient progress ring riding the glass border, and an
// orange seek dot on the circumference. No grooves, no chapters.
//
// Timing discipline (session playback inventory):
// - 15 Hz interpolated display progress anchored on the 4 Hz real
//   position; the anchor snaps on every real update AND on seek.
// - Rotation via a 10 Hz accumulator (1 rpm) that advances only while
//   playing — the art freezes in place when paused, no wall-clock
//   catch-up jump on resume.
Item {
    id: root

    property var player: PlayerService
    property url artSource: ""
    property real diameter: 90
    // Opt-in (ribbon only): drag the head around the rim to seek, and the
    // centre becomes a larger play/pause target. Off everywhere else so
    // the now-playing island / video disc keep tap-to-play/pause.
    property bool dragSeekEnabled: false
    // Ribbon bumps the play/pause glyph so its bigger tap zone reads.
    property real playGlyphScale: 1.0

    readonly property real _dotRadius: 5
    readonly property real _ringWidth: 4

    // Progress (0..1) for a point relative to centre; 0 at 12 o'clock.
    function _progAt(x, y) {
        let a = Math.atan2(y - height / 2, x - width / 2) + Math.PI / 2
        if (a < 0) a += 2 * Math.PI
        return a / (2 * Math.PI)
    }
    function _seekTo(p) {
        if (!player || player.durationMs <= 0)
            return
        const f = Math.min(Math.max(p, 0), 1)
        player.seekMs(f * player.durationMs)
        _anchorMs = f * player.durationMs      // snap the dot immediately
        _anchorClock = Date.now()
        _updateDisplay()
    }

    implicitWidth: diameter + _dotRadius * 2
    implicitHeight: diameter + _dotRadius * 2
    // Keep the ring/seek-dot glow inside our bounds so the disc never
    // bleeds over headers above it (base fix every host inherits).
    clip: false

    // ── Interpolated display progress ──
    property real _anchorMs: 0
    property real _anchorClock: 0
    property real displayProgress: 0

    function _snapAnchor() {
        _anchorMs = player ? player.positionMs : 0
        _anchorClock = Date.now()
        _updateDisplay()
    }
    function _updateDisplay() {
        const dur = player ? player.durationMs : 0
        if (dur <= 0) { displayProgress = 0; return }
        let ms = _anchorMs
        if (player.playing)
            ms += Date.now() - _anchorClock
        displayProgress = Math.min(Math.max(ms / dur, 0), 1)
    }

    Connections {
        target: root.player
        function onPositionMsChanged() { root._snapAnchor() }
        function onDurationMsChanged() { root._updateDisplay() }
        function onPlayingChanged() { root._snapAnchor() }
    }
    Timer {
        interval: 66   // ~15 Hz projection between the 4 Hz real updates
        repeat: true
        running: root.player && root.player.playing && root.visible
        onTriggered: root._updateDisplay()
    }

    // ── Rotation accumulator — 10 Hz, one revolution per minute ──
    property real _rotationDeg: 0
    Timer {
        interval: 100
        repeat: true
        running: root.player && root.player.playing && root.visible
        onTriggered: root._rotationDeg += (0.1 / 60.0) * 360.0   // 0.6°/tick
    }

    readonly property bool _isPlaying: player ? player.playing : false
    onDisplayProgressChanged: ring.requestPaint()

    // ── Glass disc (ultraThinMaterial stand-in) ──
    Rectangle {
        anchors.centerIn: parent
        width: root.diameter
        height: root.diameter
        radius: width / 2
        color: Theme.glassBg
        border.width: 1
        border.color: Theme.glassHighlight
    }

    // ── Progress ring + seek dot (Canvas; segments approximate the
    //    mac's AngularGradient pink→orange) ──
    Canvas {
        id: ring
        anchors.fill: parent
        opacity: 1.0

        property real glow: root._isPlaying ? 1.0 : 0.45
        Behavior on glow { NumberAnimation { duration: 500; easing.type: Easing.InOutQuad } }
        onGlowChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2, cy = height / 2
            const r = (root.diameter - 12) / 2
            const p = root.displayProgress
            if (p <= 0.001)
                return

            const startA = -Math.PI / 2
            const segments = 48
            const played = Math.max(1, Math.round(segments * p))
            for (let s = 0; s < played; s++) {
                const f = played > 1 ? s / (played - 1) : 1.0
                // pink (0.83,0.21,0.48) → orange (1.0,0.55,0.0)
                const cr = 0.83 + f * 0.17
                const cg = 0.21 + f * 0.34
                const cb = 0.48 - f * 0.48
                const a0 = startA + (s / segments) * 2 * Math.PI * p * (segments / played)
                const a1 = startA + ((s + 1) / segments) * 2 * Math.PI * p * (segments / played)
                ctx.strokeStyle = Qt.rgba(cr, cg, cb, 0.9)
                ctx.lineWidth = root._ringWidth
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.arc(cx, cy, r, a0, a1, false)
                ctx.stroke()
            }

            // Seek dot with glow (scaled by play-state pulse)
            const dotA = startA + 2 * Math.PI * p
            const dx = cx + r * Math.cos(dotA)
            const dy = cy + r * Math.sin(dotA)
            const g = ring.glow
            ctx.fillStyle = Qt.rgba(1.0, 0.55, 0.0, 0.35 * g)
            ctx.beginPath(); ctx.arc(dx, dy, 10, 0, 2 * Math.PI); ctx.fill()
            ctx.fillStyle = Qt.rgba(1.0, 0.55, 0.0, 0.6 * g)
            ctx.beginPath(); ctx.arc(dx, dy, 6.5, 0, 2 * Math.PI); ctx.fill()
            ctx.fillStyle = Theme.orange
            ctx.beginPath(); ctx.arc(dx, dy, root._dotRadius, 0, 2 * Math.PI); ctx.fill()
        }
    }

    // ── Spinning circular art (fallback: dark disc) ──
    Item {
        anchors.centerIn: parent
        width: root.diameter - 24
        height: root.diameter - 24

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Qt.rgba(0.1, 0.1, 0.1, 1)
        }

        ArtworkImage {
            id: art
            anchors.fill: parent
            source: root.artSource
            visible: source !== "" && status === Image.Ready
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            rotation: root._rotationDeg
            layer.enabled: visible
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: root.diameter - 24
                    height: root.diameter - 24
                    radius: (root.diameter - 24) / 2
                }
            }
        }

        // Play/pause indicator: loud when paused, subtle while playing
        Item {
            anchors.centerIn: parent
            width: parent.width * 0.3 * root.playGlyphScale
            height: width
            opacity: root._isPlaying ? 0.35 : 0.85
            Behavior on opacity { NumberAnimation { duration: 200 } }

            // Pause bars
            Row {
                anchors.centerIn: parent
                spacing: parent.width * 0.24
                visible: root._isPlaying
                Rectangle { width: parent.parent.width * 0.26; height: parent.parent.height; radius: 1.5; color: "#ffffff" }
                Rectangle { width: parent.parent.width * 0.26; height: parent.parent.height; radius: 1.5; color: "#ffffff" }
            }
            // Play triangle
            Canvas {
                anchors.fill: parent
                visible: !root._isPlaying
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

    // ── Center hole ──
    Rectangle {
        anchors.centerIn: parent
        width: root.diameter * 0.06
        height: width
        radius: width / 2
        color: Qt.rgba(0.12, 0.12, 0.12, 1)
    }

    // Interaction:
    //  • default — tap anywhere in the disc = play/pause.
    //  • dragSeekEnabled — the outer ring grabs & drags the seek head,
    //    the centre stays a (large) play/pause target.
    MouseArea {
        id: hit
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        property bool seeking: false
        readonly property real playR: root.diameter * 0.38   // centre play zone
        readonly property real outerR: root.diameter / 2 + root._dotRadius

        function _dist(mx, my) {
            const dx = mx - width / 2, dy = my - height / 2
            return Math.sqrt(dx * dx + dy * dy)
        }

        onPressed: function(mouse) {
            if (!root.dragSeekEnabled)
                return
            const d = _dist(mouse.x, mouse.y)
            if (d > hit.playR && d <= hit.outerR) {
                hit.seeking = true
                root._seekTo(root._progAt(mouse.x, mouse.y))
            }
        }
        onPositionChanged: function(mouse) {
            if (hit.seeking)
                root._seekTo(root._progAt(mouse.x, mouse.y))
        }
        onReleased: hit.seeking = false
        onClicked: function(mouse) {
            if (hit.seeking)
                return
            const d = _dist(mouse.x, mouse.y)
            const playR = root.dragSeekEnabled ? hit.playR : root.diameter / 2
            if (d <= playR && root.player)
                root.player.playPause()
        }
    }
}
