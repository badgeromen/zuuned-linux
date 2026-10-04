import QtQuick

// Slim inline volume control — the music surfaces' answer to the video
// player's orbital volume arc. Drawn monochrome speaker (same family
// as OrbitalControls' mute chip; the emoji clashed), pink fill bar.
// Click the speaker to mute, click/drag the bar to set, wheel to nudge.
Item {
    id: root

    property var player: PlayerService
    property real barWidth: 96
    readonly property real vol: player ? player.volume : 100
    readonly property bool muted: player ? player.muted : false

    implicitWidth: 18 + 6 + barWidth
    implicitHeight: 18

    // ── Speaker (mute toggle) ──
    Canvas {
        id: spk
        width: 18
        height: 18
        anchors.verticalCenter: parent.verticalCenter
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2 - 1, cy = height / 2
            const s = height / 26
            const col = root.muted
                ? Qt.rgba(1, 1, 1, 0.4)
                : (spkArea.containsMouse ? Theme.textPrimary
                                         : Theme.textSecondary)
            ctx.fillStyle = col
            ctx.beginPath()
            ctx.moveTo(cx - 8 * s, cy - 3 * s)
            ctx.lineTo(cx - 4 * s, cy - 3 * s)
            ctx.lineTo(cx + 1 * s, cy - 7.5 * s)
            ctx.lineTo(cx + 1 * s, cy + 7.5 * s)
            ctx.lineTo(cx - 4 * s, cy + 3 * s)
            ctx.lineTo(cx - 8 * s, cy + 3 * s)
            ctx.closePath()
            ctx.fill()
            ctx.strokeStyle = col
            ctx.lineWidth = 1.4
            ctx.lineCap = "round"
            if (root.muted) {
                ctx.beginPath()
                ctx.moveTo(cx - 9 * s, cy + 9 * s)
                ctx.lineTo(cx + 9 * s, cy - 9 * s)
                ctx.stroke()
            } else {
                ctx.beginPath()
                ctx.arc(cx + 1 * s, cy, 4.5 * s, -Math.PI / 3, Math.PI / 3)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx + 1 * s, cy, 7.5 * s, -Math.PI / 3, Math.PI / 3)
                ctx.stroke()
            }
        }
        Connections {
            target: root.player
            function onVolumeChanged() { spk.requestPaint() }
        }
        MouseArea {
            id: spkArea
            anchors.fill: parent
            anchors.margins: -3
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.player.toggleMute()
            onContainsMouseChanged: spk.requestPaint()
        }
    }

    // ── The bar ──
    Item {
        id: bar
        anchors.left: spk.right
        anchors.leftMargin: 6
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 3
            radius: 1.5
            color: Qt.rgba(1, 1, 1, 0.14)
        }
        Rectangle {
            id: fill
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * root.vol / 100
            height: 3
            radius: 1.5
            color: root.muted ? Qt.rgba(1, 1, 1, 0.25) : Theme.pink
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        }
        // Handle dot — appears on hover/drag
        Rectangle {
            visible: barArea.containsMouse || barArea.pressed
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(0, Math.min(parent.width - width,
                                    fill.width - width / 2))
            width: 9
            height: 9
            radius: 4.5
            color: root.muted ? Theme.textSecondary : Theme.activePink
        }
        MouseArea {
            id: barArea
            anchors.fill: parent
            anchors.topMargin: -4
            anchors.bottomMargin: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            function setFrom(mx) {
                root.player.volume =
                    Math.max(0, Math.min(100, mx / bar.width * 100))
            }
            onPressed: mouse => setFrom(mouse.x)
            onPositionChanged: mouse => { if (pressed) setFrom(mouse.x) }
        }
    }

    // Wheel anywhere on the control nudges ±5
    WheelHandler {
        target: null
        onWheel: event => {
            const step = event.angleDelta.y > 0 ? 5 : -5
            root.player.volume =
                Math.max(0, Math.min(100, root.vol + step))
        }
    }
}
