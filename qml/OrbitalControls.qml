import QtQuick

// Port of OrbitalControls.swift — transport + volume controls arranged
// as two arcs orbiting the vinyl disc.
//
// Right arc (−40°…+40° about the disc's right horizontal, 20° steps):
// chapter back, −10s, stop, +10s, chapter forward.
// Left arc (140°…220°): curved volume slider with a draggable dot,
// plus a mute chip at 230° (just above the arc's top).
//
// Angle convention (mac): math degrees in a y-down coordinate system —
// 0° = right, 90° = bottom, 180° = left, 270° = top — so cos/sin map
// straight to screen offsets with no sign flips.
Item {
    id: root

    property real orbitRadius: 240
    // Volume arc + handle radius — the zuuned-web disc hugs the rim
    // (discRadius + 16) while the chips orbit farther out.
    property real volumeRadius: orbitRadius
    property real chipSize: 36

    // Player state (bound by caller)
    property real volume: 100        // 0…100
    property bool muted: false

    signal prevChapter()
    signal skipBack()                // −10 s
    signal stopRequested()
    signal skipForward()             // +10 s
    signal nextChapter()
    signal toggleMute()
    // Not "volumeChanged" — that name is taken by the volume property's
    // auto-generated change signal (QML duplicate-signal error).
    signal volumeDragged(real v)

    readonly property real frameSize: (orbitRadius + chipSize / 2) * 2 + 8
    width: frameSize
    height: frameSize

    // Volume arc: 140° (bottom-left, 0%) → 220° (top-left, 100%)
    readonly property real volArcStart: 140
    readonly property real volArcEnd: 220
    readonly property real volFraction: Math.min(Math.max(volume, 0), 100) / 100
    readonly property real handleAngle:
        volArcStart + (volArcEnd - volArcStart) * volFraction

    function polarX(deg) { return frameSize / 2 + orbitRadius * Math.cos(deg * Math.PI / 180) }
    function volX(deg) { return frameSize / 2 + volumeRadius * Math.cos(deg * Math.PI / 180) }
    function volY(deg) { return frameSize / 2 + volumeRadius * Math.sin(deg * Math.PI / 180) }
    function polarY(deg) { return frameSize / 2 + orbitRadius * Math.sin(deg * Math.PI / 180) }

    // ── Volume arc track (dim full arc + pink filled sweep) ──
    Canvas {
        id: volArc
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const c = root.frameSize / 2
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            // Background track
            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.15)
            ctx.beginPath()
            ctx.arc(c, c, root.volumeRadius,
                    root.volArcStart * Math.PI / 180,
                    root.volArcEnd * Math.PI / 180, false)
            ctx.stroke()
            // Filled portion — bottom (0%) sweeping up
            ctx.strokeStyle = root.muted
                ? Qt.rgba(1, 1, 1, 0.25) : Theme.pink
            ctx.beginPath()
            ctx.arc(c, c, root.volumeRadius,
                    root.volArcStart * Math.PI / 180,
                    (root.volArcStart + (root.volArcEnd - root.volArcStart)
                     * root.volFraction) * Math.PI / 180, false)
            ctx.stroke()
        }
        Connections {
            target: root
            function onVolFractionChanged() { volArc.requestPaint() }
            function onMutedChanged() { volArc.requestPaint() }
            function onVolumeRadiusChanged() { volArc.requestPaint() }
        }
    }

    // ── Volume handle (draggable dot) ──
    Rectangle {
        id: handle
        width: 18
        height: 18
        radius: 9
        x: root.volX(root.handleAngle) - 9
        y: root.volY(root.handleAngle) - 9
        color: root.muted ? Qt.rgba(1, 1, 1, 0.5) : Theme.pink
        border.color: Qt.rgba(1, 1, 1, 0.85)
        border.width: 1.2

        MouseArea {
            anchors.fill: parent
            anchors.margins: -8
            cursorShape: Qt.PointingHandCursor
            onPositionChanged: mouseEvent => {
                // Pointer in frame coords → angle → clamp into the arc
                const p = mapToItem(root, mouseEvent.x, mouseEvent.y)
                const dx = p.x - root.frameSize / 2
                const dy = p.y - root.frameSize / 2
                let deg = Math.atan2(dy, dx) * 180 / Math.PI
                if (deg < 0) deg += 360
                const clamped = Math.min(Math.max(deg, root.volArcStart),
                                         root.volArcEnd)
                root.volumeDragged((clamped - root.volArcStart)
                                   / (root.volArcEnd - root.volArcStart) * 100)
            }
        }
    }

    // ── Transport chips (right arc) + mute (above volume arc) ──
    Repeater {
        model: [
            { glyph: "⏮", angle: -40, act: () => root.prevChapter() },
            { glyph: "-10", angle: -20, act: () => root.skipBack() },
            { glyph: "⏹", angle: 0, act: () => root.stopRequested() },
            { glyph: "+10", angle: 20, act: () => root.skipForward() },
            { glyph: "⏭", angle: 40, act: () => root.nextChapter() },
        ]
        delegate: Rectangle {
            required property var modelData
            width: root.chipSize
            height: root.chipSize
            radius: root.chipSize / 2
            x: root.polarX(modelData.angle) - root.chipSize / 2
            y: root.polarY(modelData.angle) - root.chipSize / 2
            color: chipArea.containsMouse
                ? Qt.rgba(1, 1, 1, 0.14) : Theme.glassBg
            border.color: Theme.glassBorder
            Text {
                anchors.centerIn: parent
                text: parent.modelData.glyph
                font.pixelSize: parent.modelData.glyph.length > 1 ? 11 : 14
                font.bold: parent.modelData.glyph.length > 1
                color: Theme.textPrimary
            }
            MouseArea {
                id: chipArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: parent.modelData.act()
            }
        }
    }

    Rectangle {
        width: root.chipSize
        height: root.chipSize
        radius: root.chipSize / 2
        x: root.polarX(230) - root.chipSize / 2
        y: root.polarY(230) - root.chipSize / 2
        color: muteArea.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Theme.glassBg
        border.color: Theme.glassBorder
        // Drawn monochrome speaker (the emoji clashed with the chip
        // family): body + cone, sound waves when live, slash when muted.
        Canvas {
            id: muteGlyph
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2
                const col = root.muted
                    ? Qt.rgba(1, 1, 1, 0.55) : Theme.textPrimary
                // Speaker body + cone
                ctx.fillStyle = col
                ctx.beginPath()
                ctx.moveTo(cx - 8, cy - 3)
                ctx.lineTo(cx - 4, cy - 3)
                ctx.lineTo(cx + 1, cy - 7.5)
                ctx.lineTo(cx + 1, cy + 7.5)
                ctx.lineTo(cx - 4, cy + 3)
                ctx.lineTo(cx - 8, cy + 3)
                ctx.closePath()
                ctx.fill()
                ctx.strokeStyle = col
                ctx.lineWidth = 1.6
                ctx.lineCap = "round"
                if (root.muted) {
                    // Slash across
                    ctx.beginPath()
                    ctx.moveTo(cx - 9, cy + 9)
                    ctx.lineTo(cx + 9, cy - 9)
                    ctx.stroke()
                } else {
                    // Two sound-wave arcs
                    ctx.beginPath()
                    ctx.arc(cx + 1, cy, 4.5, -Math.PI / 3, Math.PI / 3)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.arc(cx + 1, cy, 7.5, -Math.PI / 3, Math.PI / 3)
                    ctx.stroke()
                }
            }
            Connections {
                target: root
                function onMutedChanged() { muteGlyph.requestPaint() }
            }
        }
        MouseArea {
            id: muteArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleMute()
        }
    }
}
