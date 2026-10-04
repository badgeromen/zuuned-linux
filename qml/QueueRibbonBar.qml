import QtQuick
import QtQuick.Layouts

// Port of QueueRibbonBar.swift — the floating queue ribbon: played
// cards (left, trailing-aligned so the most recent sits beside the
// disc) | 90px SpinningDiscView | upcoming cards (right). Both sides
// get an identical fixed width (4 cards) so the disc is always dead
// center. Card strips depend only on queue/index/art — position ticks
// never touch them (the disc alone consumes progress updates).
//
// Source-agnostic: art comes via the injected artUrlFor(item) function
// so the ribbon serves local and device libraries alike.
Item {
    id: root

    property var player: PlayerService
    // Parent-supplied: item ({filepath,title,artist,album,...}) → art url
    property var artUrlFor: function(item) { return "" }

    signal saveQueueRequested()
    signal miniPlayerRequested()

    readonly property real discSize: 90
    readonly property real cardSize: 50
    // 4 cards per side, mac math: 4 × (cardSize + ZuneSpace.sm)
    readonly property real sideWidth: 4 * (cardSize + Theme.spaceSm)

    // Exact mac visibility: state != .stopped || queue non-empty
    visible: player ? (!player.stopped || player.queue.length > 0) : false

    implicitWidth: ribbonRow.implicitWidth
    implicitHeight: ribbonRow.implicitHeight

    readonly property var queueItems: player ? player.queue : []
    readonly property int queueIndex: player ? player.queueIndex : -1
    readonly property var currentItem:
        queueIndex >= 0 && queueIndex < queueItems.length
        ? queueItems[queueIndex] : null

    // Played: NOT reversed on the mac (trailing-aligned HStack); our
    // RightToLeft ListView needs the reverse so the last-played card
    // renders nearest the disc.
    readonly property var playedItems:
        queueIndex > 0 ? queueItems.slice(0, queueIndex).reverse() : []
    readonly property var upcomingItems:
        queueIndex >= 0 && queueIndex + 1 < queueItems.length
        ? queueItems.slice(queueIndex + 1) : []

    RowLayout {
        id: ribbonRow
        spacing: 0

        // ── Left chevron ──
        ChevronButton {
            glyph: "‹"
            shown: root.playedItems.length > 0
            onActivated: playedStrip.scrollByOne()
        }

        // ── Played strip (right-aligned) ──
        QueueStrip {
            id: playedStrip
            Layout.preferredWidth: root.sideWidth
            Layout.preferredHeight: root.cardSize + 22
            items: root.playedItems
            rightToLeft: true
            // reversed strip: visual idx 0 = queueIndex-1
            indexFor: idx => root.queueIndex - 1 - idx
        }

        // ── Center: disc + title capsule + controls ──
        ColumnLayout {
            spacing: 4
            Layout.leftMargin: Theme.spaceXs
            Layout.rightMargin: Theme.spaceXs

            // Disc or Vinyl per Prefs.playerStyle (unified across the app)
            Loader {
                Layout.alignment: Qt.AlignHCenter
                sourceComponent: Prefs.playerStyle === "vinyl" ? ribbonVinyl : ribbonDisc
            }
            Component {
                id: ribbonDisc
                SpinningDiscView {
                    player: root.player
                    diameter: root.discSize
                    artSource: root.currentItem ? root.artUrlFor(root.currentItem) : ""
                    dragSeekEnabled: true      // ribbon: drag the head to seek
                    playGlyphScale: 1.5        // ribbon: bigger play/pause target
                }
            }
            Component {
                id: ribbonVinyl
                VinylRecordView {
                    player: root.player
                    // A vinyl is physically bigger than a CD — so the record
                    // reads as a record, with grooves showing around a normal
                    // centre label instead of the art swallowing them.
                    size: Math.round(root.discSize * 1.5)
                    posterSource: root.currentItem ? root.artUrlFor(root.currentItem) : ""
                    chapterCount: 4         // a few clean groove bands
                    labelFraction: 0.24     // proportional centre (grooves show)
                    playZoneScale: 1.5      // still a big play/pause target
                }
            }

            // Title / artist capsule
            Rectangle {
                z: 5   // force the header above the vinyl/disc
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: root.discSize + 30
                visible: root.player ? !root.player.stopped : false
                implicitWidth: capsuleCol.implicitWidth + 16
                implicitHeight: capsuleCol.implicitHeight + 6
                radius: 6
                color: Theme.glassBg
                border.width: 1
                border.color: Theme.glassBorderSubtle

                Column {
                    id: capsuleCol
                    anchors.centerIn: parent
                    spacing: 1
                    width: root.discSize + 14

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.player && root.player.currentTitle !== ""
                              ? root.player.currentTitle : "—"
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        color: Theme.textPrimary
                        elide: Text.ElideMiddle
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.player ? root.player.currentArtist : ""
                        font.pixelSize: 9
                        font.weight: Font.Light
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }

            // Controls: shuffle · repeat(.1) · mini player · save queue
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 2
                spacing: 14
                visible: root.player ? !root.player.stopped : false

                RibbonButton {
                    glyph: "⇄"
                    active: root.player ? root.player.shuffled : false
                    onActivated: root.player.toggleShuffle()
                }
                RibbonButton {
                    glyph: "↻"
                    active: root.player ? root.player.repeatMode !== 0 : false
                    badge: root.player && root.player.repeatMode === 2 ? "1" : ""
                    onActivated: root.player.cycleRepeat()
                }
                RibbonButton {
                    glyph: "◉"
                    onActivated: root.miniPlayerRequested()
                }
                RibbonButton {
                    glyph: "+"
                    onActivated: root.saveQueueRequested()
                }
            }

            // Volume on its own row, under the controls
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 2
                visible: root.player ? !root.player.stopped : false
                VolumeSlim {
                    player: root.player
                    barWidth: 100
                }
            }
        }

        // ── Upcoming strip (left-aligned) ──
        QueueStrip {
            id: upcomingStrip
            Layout.preferredWidth: root.sideWidth
            Layout.preferredHeight: root.cardSize + 22
            items: root.upcomingItems
            rightToLeft: false
            indexFor: idx => root.queueIndex + 1 + idx
        }

        // ── Right chevron ──
        ChevronButton {
            glyph: "›"
            shown: root.upcomingItems.length > 0
            onActivated: upcomingStrip.scrollByOne()
        }
    }

    // ═══ Reusable pieces ═══

    component ChevronButton: Item {
        property string glyph
        property bool shown: true
        property bool hovering: false
        signal activated()

        Layout.preferredWidth: shown ? 20 : 0
        Layout.preferredHeight: 40
        visible: shown

        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.pixelSize: 16
            font.weight: Font.Medium
            color: parent.hovering ? Theme.textPrimary : Theme.textDim
            scale: parent.hovering ? 1.1 : 1.0
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: parent.hovering = true
            onExited: parent.hovering = false
            onClicked: parent.activated()
        }
    }

    component RibbonButton: Item {
        property string glyph
        property string badge: ""
        property bool active: false
        property bool hovering: false
        signal activated()

        implicitWidth: 16
        implicitHeight: 14

        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.pixelSize: 11
            color: parent.active ? Theme.pink
                 : parent.hovering ? Theme.textPrimary : Theme.textSecondary
            scale: parent.hovering ? 1.15 : 1.0
            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
        }
        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: -3
            visible: parent.badge !== ""
            text: parent.badge
            font.pixelSize: 7
            font.weight: Font.Bold
            color: Theme.pink
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: parent.hovering = true
            onExited: parent.hovering = false
            onClicked: parent.activated()
        }
    }

    // Card strip — a horizontal ListView whose delegates depend only on
    // the queue slice + art (never on playback position)
    component QueueStrip: Item {
        id: strip
        property var items: []
        property bool rightToLeft: false
        // visual index → queue index
        property var indexFor: function(idx) { return idx }
        property int scrollOffset: 0

        function scrollByOne() {
            scrollOffset = Math.min(items.length - 1, scrollOffset + 1)
            stripList.positionViewAtIndex(scrollOffset, ListView.Beginning)
        }
        onItemsChanged: scrollOffset = 0

        clip: true

        ListView {
            id: stripList
            anchors.fill: parent
            anchors.leftMargin: Theme.spaceMd
            anchors.rightMargin: Theme.spaceMd
            orientation: ListView.Horizontal
            layoutDirection: strip.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight
            spacing: Theme.spaceSm
            model: strip.items
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                required property var modelData
                required property int index

                width: root.cardSize + 8
                height: root.cardSize + 22

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: cardArea.containsMouse ? Theme.cardHover : Theme.glassBg
                    border.width: 1
                    border.color: Theme.glassBorderSubtle
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    spacing: 2

                    Item {
                        width: root.cardSize
                        height: root.cardSize

                        AlbumArtPlaceholder {
                            anchors.fill: parent
                        }
                        ArtworkImage {
                            anchors.fill: parent
                            source: root.artUrlFor(modelData)
                            visible: source !== "" && status === Image.Ready
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }

                    Text {
                        width: root.cardSize
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.title || ""
                        font.pixelSize: 9
                        font.weight: Font.Medium
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: cardArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.player.jumpTo(strip.indexFor(index))
                }
            }
        }
    }
}
