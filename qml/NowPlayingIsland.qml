import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Zuuned

// N4 · THE MIGRATING RIBBON (Orson's design, locked): the horizontal
// ribbon turned upright, floating above the sidebar as a stacked card
// (builder-tray geometry, PINK identity, clean surface). History
// flows away above — smaller and dimmer with distance — the disc
// holds the center, the future approaches from below. Unlike the
// builder this never locks you in: the sidebar's ◉ is the way back.
//
// Migration spec: while this is open the top ribbon fades out (one
// object traveling, never two); closing fades it back. In narrow
// windows the ribbon retires entirely and this is the only
// now-playing surface (Main.qml owns that logic).
Item {
    id: island
    z: 5
    property var artUrlFor: function(item) { return "" }
    signal closeRequested()

    // Same lift/settle as the builder tray
    property real reveal: visible ? 1 : 0
    property bool shown: false
    visible: shown || reveal > 0.01
    Behavior on reveal {
        NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic }
    }
    onShownChanged: reveal = shown ? 1 : 0
    opacity: reveal
    scale: 0.93 + 0.07 * reveal
    transformOrigin: Item.Left
    transform: Translate { y: (1 - island.reveal) * 22 }

    readonly property var queueItems: PlayerService.queue
    readonly property int queueIndex: PlayerService.queueIndex
    readonly property var currentItem:
        queueIndex >= 0 && queueIndex < queueItems.length
        ? queueItems[queueIndex] : null
    // FULL history/future (scrollable) — reversed played so index 0
    // is the most recent, rendered nearest the disc
    readonly property var playedItems:
        queueIndex > 0 ? queueItems.slice(0, queueIndex).reverse() : []
    readonly property var upcomingItems:
        queueIndex >= 0 && queueIndex + 1 < queueItems.length
        ? queueItems.slice(queueIndex + 1) : []

    // ── The floating card (clean, PINK identity — grunge belongs to
    // the ambient islands, not tools) ──
    Item {
        id: card
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Qt.rgba(0.075, 0.065, 0.07, 0.98)
            border.width: 1
            border.color: Qt.alpha(Theme.pink, 0.32)
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.83, 0.21, 0.48, 0.06) }
                GradientStop { position: 0.25; color: "transparent" }
            }
        }
    }
    DropShadow {
        anchors.fill: card
        source: card
        radius: 40
        samples: 49
        verticalOffset: 18
        horizontalOffset: 6
        color: Qt.rgba(0, 0, 0, 0.45 + 0.3 * island.reveal)
        z: -1
    }
    DropShadow {
        anchors.fill: card
        source: card
        radius: 10
        samples: 17
        verticalOffset: 4
        color: Qt.rgba(0, 0, 0, 0.6)
        z: -1
    }

    // EXACT ribbon card anatomy (review: same glass look) — glass
    // backing, 50px art with a flat fallback, 9px caption, hover tint,
    // click jumps the queue.
    component QueueTile: Item {
        id: tile
        property var item: null
        property int qIndex: -1
        width: 58
        height: 72

        Rectangle {
            anchors.fill: parent
            radius: 6
            color: tileArea.containsMouse ? Theme.cardHover : Theme.glassBg
            border.width: 1
            border.color: Theme.glassBorderSubtle
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 4
            spacing: 2

            Item {
                width: 50
                height: 50

                AlbumArtPlaceholder {
                    anchors.fill: parent
                }
                ArtworkImage {
                    anchors.fill: parent
                    source: tile.item ? island.artUrlFor(tile.item) : ""
                    visible: source !== "" && status === Image.Ready
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }

            Text {
                width: 50
                horizontalAlignment: Text.AlignHCenter
                text: tile.item ? (tile.item.title || "") : ""
                font.pixelSize: 9
                font.weight: Font.Medium
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }

        MouseArea {
            id: tileArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: PlayerService.jumpTo(tile.qIndex)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceSm
        // Short-window safety: nothing ever spills past the card.
        clip: true

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "PLAYED"
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 2
            color: island.playedItems.length > 0
                   ? Theme.activePink : Theme.textGhost
        }

        // History climbs away upward — most recent at the bottom,
        // beside the disc; scroll up for older. FLEXIBLE height: up to
        // 4 cards on a tall window, compressing to 1 on a short one so
        // the disc + transport always fit inside the card.
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: 4 * 72 + 3 * Theme.spaceXs
            Layout.minimumHeight: 72
            clip: true
            spacing: Theme.spaceXs
            verticalLayoutDirection: ListView.BottomToTop
            boundsBehavior: Flickable.StopAtBounds
            model: island.playedItems
            delegate: Item {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 72
                QueueTile {
                    anchors.horizontalCenter: parent.horizontalCenter
                    item: parent.modelData
                    qIndex: island.queueIndex - 1 - parent.index
                }
            }
        }

        // ── The player — Disc or Vinyl, per Prefs.playerStyle (set in
        // onboarding / settings). Same component, new home; shrinks with
        // the island so short windows keep the whole anatomy ──
        Loader {
            Layout.alignment: Qt.AlignHCenter
            sourceComponent: Prefs.playerStyle === "vinyl"
                             ? vinylPlayerComp : discPlayerComp
        }
        Component {
            id: discPlayerComp
            SpinningDiscView {
                player: PlayerService
                diameter: island.height < 720 ? 90 : 120
                artSource: island.currentItem
                           ? island.artUrlFor(island.currentItem) : ""
                dragSeekEnabled: true   // drag the head to seek
            }
        }
        Component {
            id: vinylPlayerComp
            VinylRecordView {
                player: PlayerService
                size: island.height < 720 ? 130 : 170
                posterSource: island.currentItem
                              ? island.artUrlFor(island.currentItem) : ""
            }
        }

        GradientText {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width - Theme.spaceLg * 2
            text: PlayerService.currentTitle !== ""
                  ? PlayerService.currentTitle : "—"
            font.family: Theme.displayFamily
            font.pixelSize: 16
            tracking: 1
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width - Theme.spaceLg * 2
            text: PlayerService.currentArtist
            font.pixelSize: 11
            font.weight: Font.Light
            color: Theme.textSecondary
            elide: Text.ElideRight
        }

        // Transport
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
            spacing: 16

            // Arrow glyphs (shuffle / repeat) render clean already.
            component TBtn: Text {
                property bool active: false
                signal go()
                font.pixelSize: 13
                color: active ? Theme.pink
                     : tArea.containsMouse ? Theme.textPrimary : Theme.textSecondary
                scale: tArea.containsMouse ? 1.15 : 1
                Behavior on scale { NumberAnimation { duration: 100 } }
                MouseArea {
                    id: tArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.go()
                }
            }

            // Media transport — DRAWN shapes (no emoji), so they stay clean
            // and monochrome in the theme colour.
            component MediaBtn: Item {
                property string kind: "play"   // play | pause | prev | next
                property color baseColor: Theme.textSecondary
                signal go()
                readonly property color iconColor:
                    mArea.containsMouse ? Theme.textPrimary : baseColor
                implicitWidth: 22
                implicitHeight: 18
                scale: mArea.containsMouse ? 1.15 : 1
                Behavior on scale { NumberAnimation { duration: 100 } }
                onIconColorChanged: cnv.requestPaint()
                onKindChanged: cnv.requestPaint()
                Canvas {
                    id: cnv
                    anchors.centerIn: parent
                    width: 16; height: 14
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = parent.iconColor
                        const w = width, h = height
                        if (parent.kind === "play") {
                            ctx.beginPath(); ctx.moveTo(1, 0); ctx.lineTo(1, h)
                            ctx.lineTo(w - 1, h / 2); ctx.closePath(); ctx.fill()
                        } else if (parent.kind === "pause") {
                            const bw = w * 0.3
                            ctx.fillRect(w * 0.16, 0, bw, h)
                            ctx.fillRect(w - w * 0.16 - bw, 0, bw, h)
                        } else if (parent.kind === "prev") {
                            ctx.fillRect(0, 0, 2.5, h)
                            ctx.beginPath(); ctx.moveTo(w, 0); ctx.lineTo(w, h)
                            ctx.lineTo(4, h / 2); ctx.closePath(); ctx.fill()
                        } else if (parent.kind === "next") {
                            ctx.fillRect(w - 2.5, 0, 2.5, h)
                            ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(0, h)
                            ctx.lineTo(w - 4, h / 2); ctx.closePath(); ctx.fill()
                        }
                    }
                    Component.onCompleted: requestPaint()
                }
                MouseArea {
                    id: mArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: parent.go()
                }
            }

            TBtn { text: "⇄"; active: PlayerService.shuffled; onGo: PlayerService.toggleShuffle() }
            MediaBtn { kind: "prev"; onGo: PlayerService.previous() }
            MediaBtn {
                kind: PlayerService.playing ? "pause" : "play"
                baseColor: Theme.activePink
                onGo: PlayerService.playPause()
            }
            MediaBtn { kind: "next"; onGo: PlayerService.next() }
            TBtn { text: "↻"; active: PlayerService.repeatMode !== 0; onGo: PlayerService.cycleRepeat() }
        }

        VolumeSlim {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
            barWidth: 110
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceSm
            text: "UP NEXT"
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 2
            color: island.upcomingItems.length > 0
                   ? Theme.textDim : Theme.textGhost
        }

        // The future approaches from below — next up beside the
        // disc; scroll down for later. Same flexible height rule.
        ListView {
            Layout.fillWidth: true
            // Ribbon parity: up to 4 cards visible per side
            Layout.fillHeight: true
            Layout.maximumHeight: 4 * 72 + 3 * Theme.spaceXs
            Layout.minimumHeight: 72
            clip: true
            spacing: Theme.spaceXs
            boundsBehavior: Flickable.StopAtBounds
            model: island.upcomingItems
            delegate: Item {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 72
                QueueTile {
                    anchors.horizontalCenter: parent.horizontalCenter
                    item: parent.modelData
                    qIndex: island.queueIndex + 1 + parent.index
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Footer — action bottom-right (house rule)
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            Text {
                text: "close"
                font.pixelSize: 11
                color: closeArea.containsMouse ? Theme.textPrimary : Theme.textDim
                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: island.closeRequested()
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "save as playlist"
                font.pixelSize: 11
                color: saveQArea.containsMouse ? Theme.orange : Theme.textSecondary
                MouseArea {
                    id: saveQArea
                    anchors.fill: parent
                    anchors.margins: -5
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        const q = PlayerService.queue
                        if (q.length === 0)
                            return
                        if (!TrayState.building)
                            TrayState.openNew()
                        TrayState.addTracks(q.map(it => ({
                            id: it.libraryId !== undefined ? it.libraryId : -1,
                            title: it.title || "", artist: it.artist || "",
                            album: it.album || "",
                            durationMs: it.durationMs || 0,
                            filepath: it.filepath || ""
                        })))
                        island.closeRequested()
                    }
                }
            }
        }
    }
}
