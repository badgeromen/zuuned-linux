import QtQuick
import QtQuick.Layouts
import Zuuned

// Fullscreen video overlay — port of VideoPlayerView.swift.
//
// Two disc styles (unified app-wide via Prefs.playerStyle):
//   "vinyl" — full 400px multi-groove disc, grooves = chapters.
//   "disc"  — smaller 300px single-ring seeker: one revolution = the
//             whole file, the audio-disc look.
// Everything else (orbital controls, curved title, chrome row) is
// shared and scales with the disc.
//
// Binge queues also get the episode ribbon (top-center, adapted from
// the music QueueRibbonBar): poster cards for previous/upcoming
// episodes around a "now playing" slot — click a card to jump without
// leaving the player.
Rectangle {
    id: playerRoot

    property bool controlsShown: true
    readonly property bool active: VideoPlayerService.active
    readonly property bool vinylStyle: Prefs.playerStyle !== "disc"
    readonly property real discSize: vinylStyle ? 400 : 300

    visible: active
    color: "black"
    focus: active
    onActiveChanged: {
        if (active) {
            // Remember the app window's visibility from BEFORE the video
            // took over, so leaving/closing restores it (maximized stays
            // maximized, fullscreen stays fullscreen) rather than dropping
            // to windowed.
            const w = playerRoot.Window.window
            if (w) playerRoot._savedVisibility = w.visibility
            showControls()
            playerRoot.forceActiveFocus()
        } else {
            subPicker.shown = false
            audioPicker.shown = false
        }
    }

    function showControls() {
        controlsShown = true
        hideTimer.restart()
    }
    function fmtTime(s) {
        if (!isFinite(s) || s < 0) return "0:00"
        const t = Math.floor(s)
        const h = Math.floor(t / 3600)
        const m = Math.floor((t % 3600) / 60)
        const sec = t % 60
        const mm = (h > 0 && m < 10 ? "0" : "") + m
        const ss = (sec < 10 ? "0" : "") + sec
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }
    // Remember the app window's visibility from BEFORE the video took over,
    // so leaving/closing restores it (maximized stays maximized, fullscreen
    // stays fullscreen) instead of always dropping to windowed.
    property int _savedVisibility: Window.Windowed

    function toggleFullscreen() {
        const w = playerRoot.Window.window
        if (!w) return
        w.visibility = w.visibility === Window.FullScreen
            ? playerRoot._savedVisibility : Window.FullScreen
    }
    function close() {
        const w = playerRoot.Window.window
        if (w)
            w.visibility = playerRoot._savedVisibility
        VideoPlayerService.stop()
    }

    // ── Video surface ──
    MpvVideoItem {
        anchors.fill: parent
        player: VideoPlayerService
    }

    // ── Mouse tracking ──
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: playerRoot.controlsShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: playerRoot.showControls()
        onClicked: { VideoPlayerService.togglePause(); playerRoot.showControls() }
        onDoubleClicked: playerRoot.toggleFullscreen()
    }
    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: if (VideoPlayerService.playing) playerRoot.controlsShown = false
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Space:
            VideoPlayerService.togglePause(); break
        case Qt.Key_Left:
            VideoPlayerService.seekRelative(event.modifiers & Qt.ShiftModifier ? -60 : -10); break
        case Qt.Key_Right:
            VideoPlayerService.seekRelative(event.modifiers & Qt.ShiftModifier ? 60 : 10); break
        case Qt.Key_Up:
            VideoPlayerService.volume = Math.min(100, VideoPlayerService.volume + 5); break
        case Qt.Key_Down:
            VideoPlayerService.volume = Math.max(0, VideoPlayerService.volume - 5); break
        case Qt.Key_M:
            VideoPlayerService.toggleMute(); break
        case Qt.Key_F:
            playerRoot.toggleFullscreen(); break
        case Qt.Key_Escape:
            playerRoot.close(); break
        default:
            return
        }
        playerRoot.showControls()
        event.accepted = true
    }

    // ── Scrims (control-tied) ──
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 100
        opacity: playerRoot.controlsShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.7) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    Canvas {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 640
        height: 640
        opacity: playerRoot.controlsShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const g = ctx.createRadialGradient(width, height, 20,
                                               width, height, 560)
            g.addColorStop(0, Qt.rgba(0, 0, 0, 0.65))
            g.addColorStop(1, Qt.rgba(0, 0, 0, 0))
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
        }
    }

    // ── Episode ribbon (binge queues) — top-center, music-ribbon
    //    adaptation: previous cards | now-playing | upcoming cards ──
    Item {
        id: episodeRibbon
        visible: VideoPlayerService.queueCount > 1
        opacity: playerRoot.controlsShown ? 1 : 0
        enabled: playerRoot.controlsShown && visible
        Behavior on opacity { NumberAnimation { duration: 250 } }
        anchors.top: parent.top
        anchors.topMargin: 28
        anchors.horizontalCenter: parent.horizontalCenter
        width: ribbonRow.implicitWidth + 32
        height: 96

        readonly property int cardW: 44
        readonly property int cardH: 66
        // 4 cards each side of the current episode
        readonly property var slots: {
            const q = VideoPlayerService.queue
            const cur = VideoPlayerService.queueIndex
            const out = []
            for (let i = cur - 4; i <= cur + 4; i++)
                out.push(i >= 0 && i < q.length
                         ? { idx: i, item: q[i] } : null)
            return out
        }

        Rectangle {
            anchors.fill: parent
            radius: 14
            color: Qt.rgba(0, 0, 0, 0.55)
            border.color: Theme.glassBorder
        }

        Row {
            id: ribbonRow
            anchors.centerIn: parent
            spacing: 8

            Repeater {
                model: episodeRibbon.slots
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: index === 4
                    readonly property bool filled: modelData !== null
                    width: episodeRibbon.cardW * (isCurrent ? 1.35 : 1)
                    height: episodeRibbon.cardH * (isCurrent ? 1.35 : 1)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: filled

                    Rectangle {
                        anchors.fill: parent
                        radius: 5
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.color: slot.isCurrent
                            ? Theme.activePink : Theme.glassBorder
                        border.width: slot.isCurrent ? 2 : 1
                        clip: true
                        ArtworkImage {
                            id: slotImg
                            anchors.fill: parent
                            anchors.margins: 1
                            source: slot.filled
                                ? (slot.modelData.item.poster ?? "") : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        Text {
                            visible: slotImg.status !== Image.Ready
                            anchors.centerIn: parent
                            text: slot.filled
                                ? "E" + (slot.modelData.idx + 1) : ""
                            font.pixelSize: 11
                            color: Theme.textSecondary
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: slot.filled && !slot.isCurrent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: {
                            VideoPlayerService.jumpTo(slot.modelData.idx)
                            playerRoot.showControls()
                        }
                    }
                }
            }
        }

        // Current episode title under the strip
        Text {
            anchors.top: ribbonRow.bottom
            anchors.topMargin: 2
            anchors.horizontalCenter: parent.horizontalCenter
            text: VideoPlayerService.currentTitle
            font.pixelSize: 11
            color: Theme.textSecondary
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, episodeRibbon.width - 24)
        }
    }

    // ── Bottom-right: disc + curved title + orbital controls ──
    Column {
        id: assembly
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 24
        anchors.bottomMargin: 24
        spacing: 8
        opacity: playerRoot.controlsShown ? 1 : 0
        enabled: playerRoot.controlsShown
        Behavior on opacity { NumberAnimation { duration: 250 } }

        Item {
            width: playerRoot.discSize + 140
            height: playerRoot.discSize + 140

            VinylRecordView {
                id: disc
                anchors.centerIn: parent
                player: VideoPlayerService
                posterSource: VideoPlayerService.currentPoster
                size: playerRoot.discSize
                isVisible: playerRoot.controlsShown && playerRoot.active
                // Vinyl: grooves are chapters (real mpv chapters when
                // present, else ~5-minute rings). Disc: ONE ring — one
                // revolution seeks the whole file.
                chapterCount: {
                    if (!playerRoot.vinylStyle)
                        return 1
                    const ch = VideoPlayerService.chapterCount
                    if (ch > 1 && ch <= 24)
                        return ch
                    const dur = VideoPlayerService.durationMs
                    return dur > 0
                        ? Math.min(10, Math.max(4, Math.round(dur / 300000)))
                        : 6
                }
            }

            // Vinyl: curved TITLE. Disc (zuuned-web look): curved TIME
            // readout hugging the rim; the title moves to the flat row.
            CurvedText {
                anchors.centerIn: parent
                text: playerRoot.vinylStyle
                    ? VideoPlayerService.currentTitle
                    : playerRoot.fmtTime(VideoPlayerService.position)
                      + "  /  " + playerRoot.fmtTime(VideoPlayerService.duration)
                radius: playerRoot.discSize / 2 + (playerRoot.vinylStyle ? 10 : 16)
                arcDegrees: playerRoot.vinylStyle ? 80 : 55
                placement: "bottom"
                font: playerRoot.vinylStyle
                    ? Qt.font({ pixelSize: 13, weight: Font.Medium })
                    : Qt.font({ family: "monospace", pixelSize: 11,
                                weight: Font.DemiBold })
                color: playerRoot.vinylStyle
                    ? Theme.textPrimary : Qt.rgba(1, 1, 1, 0.5)
            }

            OrbitalControls {
                anchors.centerIn: parent
                // Web Tg geometry for the disc style: chips at 1.35r,
                // volume arc hugging the rim at r+16.
                orbitRadius: playerRoot.vinylStyle
                    ? playerRoot.discSize / 2 + 40
                    : playerRoot.discSize / 2 * 1.35
                volumeRadius: playerRoot.vinylStyle
                    ? playerRoot.discSize / 2 + 40
                    : playerRoot.discSize / 2 + 16
                volume: VideoPlayerService.volume
                muted: VideoPlayerService.muted
                onPrevChapter: { VideoPlayerService.skipChapter(-1); playerRoot.showControls() }
                onSkipBack: { VideoPlayerService.seekRelative(-10); playerRoot.showControls() }
                onStopRequested: playerRoot.close()
                onSkipForward: { VideoPlayerService.seekRelative(10); playerRoot.showControls() }
                onNextChapter: { VideoPlayerService.skipChapter(1); playerRoot.showControls() }
                onToggleMute: { VideoPlayerService.toggleMute(); playerRoot.showControls() }
                onVolumeDragged: v => { VideoPlayerService.volume = v; playerRoot.showControls() }
            }
        }

        // Chrome row — subtitles, audio, fullscreen, close. The track
        // pickers pop up ANCHORED ABOVE their own chip.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            ChromeChip {
                glyph: "cc"
                onActivated: {
                    subPicker.tracks = VideoPlayerService.subtitleTracks()
                    subPicker.shown = !subPicker.shown
                    audioPicker.shown = false
                    playerRoot.showControls()
                }
                TrackPicker {
                    id: subPicker
                    title: "Subtitles"
                    isSubtitles: true
                }
            }
            ChromeChip {
                glyph: "♫"
                onActivated: {
                    audioPicker.tracks = VideoPlayerService.audioTracks()
                    audioPicker.shown = !audioPicker.shown
                    subPicker.shown = false
                    playerRoot.showControls()
                }
                TrackPicker {
                    id: audioPicker
                    title: "Audio"
                }
            }
            ChromeChip {
                glyph: "⛶"
                onActivated: {
                    playerRoot.toggleFullscreen()
                    playerRoot.showControls()
                }
            }
            ChromeChip {
                glyph: "✕"
                onActivated: playerRoot.close()
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            // Disc style curves the time onto the rim; this row carries
            // the title there instead.
            text: playerRoot.vinylStyle
                ? playerRoot.fmtTime(VideoPlayerService.position)
                  + " / " + playerRoot.fmtTime(VideoPlayerService.duration)
                : VideoPlayerService.currentTitle
            font.pixelSize: 11
            color: Theme.textSecondary
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, playerRoot.discSize)
        }
    }

    // ── Components ──
    component ChromeChip: Rectangle {
        id: chip
        property string glyph: ""
        signal activated()
        width: 32
        height: 32
        radius: 16
        color: chipArea.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Theme.glassBg
        border.color: Theme.glassBorder
        Text {
            anchors.centerIn: parent
            text: chip.glyph
            font.pixelSize: 13
            font.weight: Font.Medium
            color: Theme.textPrimary
        }
        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.activated()
        }
    }

    // Anchored above its parent chip (not floating over the disc).
    component TrackPicker: Rectangle {
        id: picker
        property bool shown: false
        property var tracks: []
        property string title: ""
        property bool isSubtitles: false

        visible: shown && playerRoot.controlsShown
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - 10
        z: 50
        width: 240
        height: pickerCol.implicitHeight + 16
        radius: 8
        color: Qt.rgba(0.08, 0.08, 0.08, 0.96)
        border.color: Theme.glassBorder

        Column {
            id: pickerCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 2

            Text {
                text: picker.title.toUpperCase()
                font.pixelSize: 9
                font.bold: true
                font.letterSpacing: 1
                color: Theme.textDim
                leftPadding: 6
                bottomPadding: 4
            }
            Repeater {
                model: [{ id: -1, label: "Off",
                          selected: !picker.tracks.some(t => t.selected) }]
                       .concat(picker.tracks.map(t => ({
                           id: t.id,
                           label: t.title !== "" ? t.title
                                : t.lang !== "" ? t.lang : "Track " + t.id,
                           selected: t.selected })))
                delegate: Rectangle {
                    required property var modelData
                    width: pickerCol.width
                    height: 26
                    radius: 4
                    color: rowArea.containsMouse
                        ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.label
                        font.pixelSize: 12
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        width: parent.width - 30
                    }
                    Text {
                        visible: parent.modelData.selected
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✔"
                        font.pixelSize: 11
                        color: Theme.pink
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (picker.isSubtitles)
                                VideoPlayerService.setSubtitleTrack(parent.modelData.id)
                            else
                                VideoPlayerService.setAudioTrack(parent.modelData.id)
                            picker.shown = false
                            playerRoot.showControls()
                        }
                    }
                }
            }
        }
    }
}
