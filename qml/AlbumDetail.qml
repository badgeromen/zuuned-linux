import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// UX-3 "vinyl shelf" (A3, approved): the sleeve sits tilted with the
// record sliding out behind it — further on hover, SPINNING while this
// album is on the turntable. Identity to the right, full-width track
// list, actions bottom-right (house rule).
Item {
    id: page

    property string albumName
    property string albumArtist: ""
    // Snapshot rows for THIS album, pre-sorted by the shell.
    property var tracks: []
    property var fullTracks: tracks
    // Local library: art keys by (artist, album, filepath), not itemId
    property bool localArt: false

    signal artistSelected(string artist)
    signal trackEditRequested(var track)   // UX-2 Edit Info bubble-up
    signal contextRequested()

    // Playback (local library only — mac parity for device rows)
    function _queueItems() {
        return page.tracks.map(t => ({
            filepath: t.filepath || "", title: t.title || "",
            artist: t.artist || "", album: t.album || "",
            discNumber: t.discNumber || 0, year: t.year || 0,
            libraryId: t.libraryId !== undefined ? t.libraryId : -1
        }))
    }
    function playTracks(index) {
        if (!page.localArt || page.tracks.length === 0)
            return
        PlayerService.setQueue(_queueItems(), Math.max(0, index))
    }
    function queueTracks() {
        if (!page.localArt || page.tracks.length === 0)
            return
        PlayerService.appendToQueue(_queueItems())
    }

    // Sync-queue entries (SyncEngine contract shape)
    function _syncItemOf(t) {
        return { filepath: t.filepath || "", title: t.title || "",
                 artist: t.artist || "",
                 albumartist: (t.albumartist && t.albumartist.length > 0)
                              ? t.albumartist : (t.artist || ""),
                 album: t.album || "", genre: t.genre || "",
                 trackNumber: t.trackNumber || 0,
                 discNumber: t.discNumber || 0, year: t.year || 0,
                 durationMs: t.durationMs || 0,
                 libraryId: t.libraryId !== undefined ? t.libraryId : -1 }
    }
    function syncTracks() {
        if (!page.localArt || !DeviceService.connected || page.fullTracks.length === 0)
            return
        SyncEngine.addTracks(page.fullTracks.map(t => page._syncItemOf(t)))
    }

    readonly property string primaryArtist:
        fullTracks.length > 0 ? fullTracks[0].artist : ""
    readonly property string primaryAlbumArtist:
        albumArtist || (fullTracks.length > 0
            ? (fullTracks[0].albumartist || fullTracks[0].artist) : "")

    readonly property string totalDuration: {
        let secs = 0
        for (const t of tracks)
            secs += Math.floor((t.durationMs || 0) / 1000)
        const h = Math.floor(secs / 3600)
        const m = Math.floor((secs % 3600) / 60)
        return h > 0 ? h + "h " + m + "m" : m + " min"
    }

    // Rim accent — sampled from the album cover (review call). The
    // void read keeps this reactive as art caches fill in.
    readonly property color rimAccent: {
        void LibraryService.artPaths
        void DeviceService.artPaths
        let u = ""
        if (page.localArt) {
            u = LibraryService.artPaths[LibraryService.artKey(
                    page.primaryAlbumArtist, page.albumName)] ?? ""
        } else if (page.tracks.length > 0) {
            u = DeviceService.artPaths[String(page.tracks[0].itemId)] ?? ""
        }
        return u !== "" ? LibraryService.dominantColor(u) : Theme.pink
    }

    // Per-album grime: the mask seed hashes from the album name, so
    // every record wears its own pattern.
    readonly property int discSeed: {
        let h = 0
        for (let i = 0; i < page.albumName.length; i++)
            h = ((h << 5) - h + page.albumName.charCodeAt(i)) | 0
        return (Math.abs(h) % 99990) + 7
    }

    // Is THIS album on the turntable right now? (drives the spin)
    readonly property bool albumPlaying: {
        const q = PlayerService.queue
        const i = PlayerService.queueIndex
        return PlayerService.playing && i >= 0 && i < q.length
               && page.fullTracks.some(t => !!t.filepath && t.filepath === q[i].filepath)
    }

    ColumnLayout {
        anchors.fill: parent
        // The page's standard frame (2026-09-06)
        anchors.leftMargin: Gallery.spine(page.width)
        anchors.rightMargin: Theme.spaceXl
        anchors.topMargin: Theme.spaceLg
        anchors.bottomMargin: Theme.spaceLg
        spacing: 0

        // ═══ The shelf pose (A3): sleeve + vinyl sliding out ═══
        // Stacks vertically when the center column is starved (both
        // islands open at the 1100px window floor).
        GridLayout {
            Layout.fillWidth: true
            columns: page.width < 640 ? 1 : 2
            columnSpacing: Theme.spaceXl
            rowSpacing: Theme.spaceMd

            Item {
                id: shelf
                Layout.preferredWidth: 300
                Layout.preferredHeight: 210
                readonly property bool shelfHover: shelfArea.containsMouse

                // ── The record — peeks from behind the sleeve, slides
                // further on hover, SPINS while this album plays ──
                Item {
                    id: disc
                    // Honors Prefs.playerStyle: vinyl = grooves + small label
                    // (this shelf's original look); disc = no grooves, album
                    // art fills the face like a CD (mirrors SpinningDiscView).
                    // This is the bespoke shelf record — NOT the shared
                    // VinylRecordView/SpinningDiscView, which stay untouched.
                    readonly property bool asVinyl: Prefs.playerStyle === "vinyl"
                    width: 180
                    height: 180
                    y: 12
                    x: shelf.shelfHover ? 116 : 92
                    Behavior on x {
                        NumberAnimation { duration: Theme.motionBase; easing.type: Easing.OutQuad }
                    }

                    Item {
                        anchors.fill: parent
                        rotation: disc.spin
                        // The REAL grunge recipe (GrungeMaskProvider),
                        // bent into a circle: exact-size masks with
                        // cr = radius (the 32px bucketing stretch is
                        // what broke the earlier circular attempt).
                        // Dark substrate → chipped glass (whole-surface
                        // grain + eaten edge) → spray tint through the
                        // edge-only mask, accent sampled from the cover.
                        // Layer 1, island order: dark body at FULL
                        // radius, UNMASKED — the zone between the true
                        // edge and the chipped glass boundary is the
                        // black grime that then becomes the color.
                        // (Safe now the mask is a true circle; in the
                        // squircle era this poked out asymmetrically.)
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: Theme.bg
                            opacity: 0.72
                        }
                        Item {
                            id: discGlassSrc
                            anchors.fill: parent
                            visible: false
                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
                                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.03) }
                                }
                            }
                            // grooves live IN the glass so the chip
                            // mask eats them with the surface
                            Repeater {
                                model: disc.asVinyl ? [0.90, 0.78, 0.66, 0.54] : []
                                delegate: Rectangle {
                                    required property real modelData
                                    anchors.centerIn: parent
                                    width: disc.width * modelData
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Qt.rgba(1, 1, 1, 0.05)
                                }
                            }
                        }
                        Image {
                            id: discMaskImg
                            anchors.fill: parent
                            visible: false
                            source: disc.width > 4
                                ? "image://grungemask/" + Math.round(disc.width)
                                  + "x" + Math.round(disc.width)
                                  + "?g=" + (DeviceService.connected ? 1 : 0)
                                  + "&r=20&s=60&circle=1&seed=" + page.discSeed
                                : ""
                        }
                        OpacityMask {
                            anchors.fill: parent
                            source: discGlassSrc
                            maskSource: discMaskImg
                            cached: false
                        }
                        // Spray rim — the islands' colored band, tinted
                        // by the album cover instead of brand pink
                        Image {
                            id: discEdgeMask
                            anchors.fill: parent
                            visible: false
                            source: disc.width > 4 && DeviceService.connected
                                ? "image://grungemask/" + Math.round(disc.width)
                                  + "x" + Math.round(disc.width)
                                  + "?g=1&edge=1&r=20&s=60&circle=1&seed=" + page.discSeed
                                : ""
                        }
                        Rectangle {
                            id: discTintSrc
                            anchors.fill: parent
                            visible: false
                            gradient: Gradient {
                                GradientStop {
                                    position: 0.0
                                    color: Qt.rgba(page.rimAccent.r, page.rimAccent.g,
                                                   page.rimAccent.b, 0.9)
                                }
                                GradientStop {
                                    position: 0.55
                                    color: Qt.rgba(page.rimAccent.r * 0.7 + 0.3,
                                                   page.rimAccent.g * 0.7 + 0.16,
                                                   page.rimAccent.b * 0.7, 0.8)
                                }
                                GradientStop { position: 1.0; color: Qt.rgba(1.0, 0.55, 0.0, 0.75) }
                            }
                        }
                        OpacityMask {
                            anchors.fill: parent
                            visible: DeviceService.connected
                            opacity: 0.45
                            source: discTintSrc
                            maskSource: discEdgeMask
                            cached: false
                        }
                        // Boundary stroke — the islands' borderCanvas,
                        // circular: the edge the grunge draws inward
                        // from (offline-verified composite).
                        Canvas {
                            id: discBorder
                            anchors.fill: parent
                            readonly property color accent: page.rimAccent
                            onAccentChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                const grad = ctx.createLinearGradient(0, 0, 0, height)
                                grad.addColorStop(0, Qt.rgba(accent.r, accent.g,
                                                             accent.b, 0.45))
                                grad.addColorStop(1, Qt.rgba(1.0, 0.55, 0.0, 0.25))
                                ctx.strokeStyle = grad
                                ctx.lineWidth = 1.5
                                ctx.beginPath()
                                ctx.arc(width / 2, height / 2,
                                        width / 2 - 0.75, 0, Math.PI * 2)
                                ctx.stroke()
                            }
                            Component.onCompleted: requestPaint()
                            onWidthChanged: requestPaint()
                        }
                        // label = the album art, circular. Small label for
                        // vinyl; fills the face for disc (CD-style).
                        Item {
                            anchors.centerIn: parent
                            width: disc.asVinyl ? 72 : 150
                            height: width
                            layer.enabled: true
                            layer.effect: OpacityMask { maskSource: labelMask }
                            AlbumArt {
                                anchors.fill: parent
                                itemId: page.tracks.length > 0 ? page.tracks[0].itemId : 0
                                local: page.localArt
                                artist: page.primaryAlbumArtist
                                album: page.albumName
                                filepath: page.tracks.length > 0
                                          ? (page.tracks[0].filepath || "") : ""
                            }
                        }
                        Rectangle {
                            id: labelMask
                            anchors.centerIn: parent
                            width: disc.asVinyl ? 72 : 150
                            height: width
                            radius: width / 2
                            visible: false
                        }
                        // spindle hole
                        Rectangle {
                            anchors.centerIn: parent
                            width: 9
                            height: 9
                            radius: 4.5
                            color: "#0a0a0a"
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.2)
                        }
                    }

                    // ~33rpm reads frantic at this size — a lazy turn
                    // sells "playing" without stealing the eye.
                    property real spin: 0
                    NumberAnimation on spin {
                        running: page.albumPlaying
                        loops: Animation.Infinite
                        from: 0; to: 360
                        duration: 5400
                    }
                }

                // ── The sleeve ──
                Item {
                    id: sleeve
                    width: 190
                    height: 190
                    y: 8
                    rotation: -3

                    AlbumArt {
                        id: sleeveArt
                        anchors.fill: parent
                        itemId: page.tracks.length > 0 ? page.tracks[0].itemId : 0
                        local: page.localArt
                        artist: page.primaryAlbumArtist
                        album: page.albumName
                        filepath: page.tracks.length > 0
                                  ? (page.tracks[0].filepath || "") : ""
                        cornerRadius: Theme.radiusMd
                    }
                    // sleeve lip highlight — reads as cardboard edge
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusMd
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.14)
                    }
                }
                DropShadow {
                    anchors.fill: sleeve
                    source: sleeveArt
                    rotation: sleeve.rotation
                    x: sleeve.x
                    y: sleeve.y
                    radius: 22
                    samples: 25
                    horizontalOffset: 7
                    verticalOffset: 9
                    color: Qt.rgba(0, 0, 0, 0.65)
                    z: -1
                }

                // Click the record to PLAY it — the object is the
                // button (review: bottom-of-screen actions were dead
                // real estate).
                MouseArea {
                    id: shelfArea
                    objectName: "albumHeroArea"
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: page.localArt ? Qt.PointingHandCursor
                                               : Qt.ArrowCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) page.contextRequested()
                        else page.playTracks(0)
                    }
                    drag.target: DeviceService.connected ? sleeveGhost : null
                    preventStealing: DeviceService.connected
                    onPressed: mouse => sleeveGhost.place(shelfArea, mouse)
                    onReleased: sleeveGhost.drop()
                }
                DragGhost {
                    id: sleeveGhost
                    area: shelfArea
                    dragKind: page.localArt ? "tracks" : "device-tracks"
                    dragTracks: () => page.localArt
                        ? page.fullTracks.map(t => page._syncItemOf(t)) : page.fullTracks
                    AlbumArt {
                        width: Theme.spaceHuge * 2
                        height: width
                        local: page.localArt
                        artist: page.primaryAlbumArtist
                        album: page.albumName
                        filepath: page.fullTracks.length ? page.fullTracks[0].filepath || "" : ""
                        itemId: page.fullTracks.length ? page.fullTracks[0].itemId || 0 : 0
                        cornerRadius: Theme.radiusMd
                    }
                }
            }

            // ── Identity ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spaceXs

                Text {
                    text: "ALBUM"
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 3
                    color: Theme.textDim
                }
                // NOT fillWidth: GradientText stretches its text
                // texture to the item width — fillWidth smeared the
                // letters across the page.
                GradientText {
                    Layout.maximumWidth: parent.width
                    text: page.albumName.toLowerCase()
                    font.family: Theme.displayFamily
                    font.pixelSize: 32
                    tracking: 1
                    rotation: -1.5
                    transformOrigin: Item.Left
                }
                Text {
                    text: page.primaryAlbumArtist
                    font.pixelSize: 14
                    font.weight: Font.Light
                    color: Theme.pink
                    font.underline: artistArea.containsMouse
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    MouseArea {
                        id: artistArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.artistSelected(page.primaryAlbumArtist)
                    }
                }
                Text {
                    text: page.tracks.length + " tracks  ·  " + page.totalDuration
                          + (page.albumPlaying ? "  ·  spinning" : "")
                    font.pixelSize: 11
                    font.weight: Font.Light
                    color: page.albumPlaying ? Theme.activePink : Theme.textSecondary
                }

                // Inline actions, right where the eye already is —
                // the vinyl itself is the play button
                RowLayout {
                    Layout.topMargin: Theme.spaceSm
                    spacing: Theme.spaceLg
                    visible: page.localArt

                    Text {
                        text: "▶ add to now playing"
                        font.pixelSize: 12
                        color: npArea.containsMouse ? Theme.activePink : Theme.textSecondary
                        MouseArea {
                            id: npArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                page.queueTracks()
                                toastHost.show("added " + page.tracks.length
                                    + " to now playing")
                            }
                        }
                    }
                    SplitAddButton {
                        enabled: TrayState.building || DeviceService.connected
                        showPick: false
                        armed: TrayState.building
                        onAddAll: {
                            if (TrayState.building) {
                                TrayState.addTracks(page.fullTracks.map(t => page._syncItemOf(t)))
                                return
                            }
                            if (!DeviceService.connected) return
                            const res = SyncEngine.addTracks(
                                page.fullTracks.map(t => page._syncItemOf(t)))
                            toastHost.show(res.error || (res.added > 0
                                ? "queued " + res.added + " tracks"
                                : (res.onDevice ?? 0) > 0
                                  ? "already on the zune"
                                  : "already queued"))
                        }
                    }
                }
                Text {
                    visible: !page.localArt
                    Layout.topMargin: Theme.spaceSm
                    text: "delete from device"
                    font.pixelSize: 12
                    color: delArea.containsMouse ? Theme.error : Theme.textDim
                    MouseArea {
                        id: delArea
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!DeviceService.connected) return
                            const ids = page.fullTracks
                                .map(t => t.itemId).filter(id => id > 0)
                            if (ids.length > 0)
                                DeviceService.purgeItems(ids)
                        }
                    }
                }
            }
        }

        // ═══ Tracks — full width under the shelf ═══
        ListView {
            id: trackPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spaceLg
            // Ultrawide guard: track rows stop stretching into absurdity
            Layout.maximumWidth: Gallery.frameW(page.width)
            Layout.alignment: Qt.AlignLeft
            clip: true
            model: page.tracks
            boundsBehavior: Flickable.StopAtBounds
            // Page-edge bar — one scrollbar x everywhere
            ScrollBar.vertical: ZuneScrollBar {
                parent: page
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }

            delegate: TrackListRow {
                required property var modelData
                required property int index
                track: modelData
                localArt: page.localArt
                showNumber: true
                showArtist: true
                showAlbum: false
                artistLink: true
                syncable: page.localArt
                // Device tracks (device mode = !localArt): rule-of-3
                // delete + save, same as the Songs tab (was missing here,
                // so right-click delete only worked in the Songs list).
                deletable: !page.localArt
                saveable: !page.localArt
                onPlayed: page.playTracks(index)
                onSyncRequested: TrayState.building
                    ? TrayState.addTracks([page._syncItemOf(modelData)])
                    : SyncEngine.addTracks([page._syncItemOf(modelData)])
                onDeleteRequested:
                    DeviceService.purgeItems([modelData.itemId])
                onSaveRequested: DeviceService.saveTrackToLibrary(
                    modelData.itemId, modelData.title,
                    modelData.artist, modelData.album)
                onEditRequested: page.trackEditRequested(modelData)
                onArtistTapped: page.artistSelected(modelData.artist)
            }
        }

    }
}
