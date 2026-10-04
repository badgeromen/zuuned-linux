import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import "MusicIdentity.js" as MusicIdentity

// UX-3 "AR3 headliner" (approved 2026-09-01): concert-bill hero —
// centered portrait under a stage-light wash (color pulled from the
// artist photo), the name HUGE beneath like a poster headliner, albums
// and tracks stacked below. Locked rules: the portrait always plays,
// actions stay inline, albums open the vinyl page. Narrow windows
// collapse the hero (smaller portrait, smaller bill).
Item {
    id: page

    property string artistName
    // Snapshot rows {title, artist, albumartist?, album, durationMs,
    // trackNumber, itemId, filepath?}
    property var tracks: []
    property var fullTracks: tracks
    property var albumTracksResolver: null
    // Local library: art keys by (artist, album, filepath), not itemId
    property bool localArt: false

    signal albumSelected(var album)
    // UX-1 tile anatomy — host (MusicPage) supplies the actions
    signal albumPlayRequested(var album)
    signal albumAddRequested(var album)
    signal albumPickRequested(var album, var anchor)
    signal albumContextRequested(var album)
    signal contextRequested()
    signal trackEditRequested(var track)   // UX-2 Edit Info bubble-up
    property bool deviceSource: false

    readonly property bool narrow: width < 700

    // Playback (local library only)
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

    readonly property var albums: MusicIdentity.groupAlbums(tracks)
    function albumDragTracks(album) {
        const members = albumTracksResolver ? albumTracksResolver(album)
            : fullTracks.filter(t => MusicIdentity.matches(t, album))
        return deviceSource ? members : members.map(t => _syncItemOf(t))
    }

    // Stage-light color — pulled from the artist photo when there is
    // one, house purple otherwise. The void read keeps this reactive
    // as photo fetches land.
    readonly property color washAccent: {
        void LibraryService.artistImages
        const u = LibraryService.artistImages[page.artistName.toLowerCase()] ?? ""
        return u !== "" ? LibraryService.dominantColor(u) : Theme.purple
    }

    // ── The stage light — fixed behind everything, doesn't scroll ──
    RadialGradient {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: Math.min(parent.height, 560)
        verticalOffset: -height * 0.42
        horizontalRadius: width * 0.55
        verticalRadius: height * 1.05
        opacity: 0.55
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(page.washAccent.r, page.washAccent.g,
                               page.washAccent.b, 0.65)
            }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: bill.height + Theme.spaceXxl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ZuneScrollBar {}

        ColumnLayout {
            id: bill
            // The page's standard frame (2026-09-06): drills wear the
            // same rectangle as the grids — hero still centers within
            x: Gallery.spine(page.width)
            width: Gallery.frameW(page.width)
            spacing: Theme.spaceMd

            Item { Layout.preferredHeight: page.narrow ? Theme.spaceMd : Theme.spaceXxl }

            // ── The portrait — always the play button ──
            Item {
                readonly property int pSize: page.narrow ? 140 : 200
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: pSize
                Layout.preferredHeight: pSize

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.artworkPlaceholder
                }
                // The Zune exposes representative artwork for device
                // browsing; local portraits use artist photos only.
                Loader {
                    anchors.fill: parent
                    active: !page.localArt
                    sourceComponent: AlbumArt {
                        itemId: page.tracks.length > 0 ? page.tracks[0].itemId : 0
                        cornerRadius: width / 2
                        showPlaceholder: false
                    }
                }
                // An unavailable artist photo leaves a flat circle in
                // the local library instead of borrowing an album cover.
                ArtworkImage {
                    id: artistPhoto
                    anchors.fill: parent
                    source: LibraryService.artistImages[page.artistName.toLowerCase()] ?? ""
                    visible: status === Image.Ready
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    layer.enabled: visible
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: 260; height: 260; radius: 130
                        }
                    }
                    Component.onCompleted:
                        LibraryService.requestArtistImage(page.artistName)
                }
                // Spotlight rim — the stage light catching the subject
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.rgba(0.83, 0.21, 0.48,
                                          portraitArea.containsMouse ? 0.9 : 0.6)
                    Behavior on border.color {
                        ColorAnimation { duration: Theme.motionFast }
                    }
                }
                // Hover play ring — same affordance as the video posters
                Rectangle {
                    anchors.centerIn: parent
                    width: 48; height: 48; radius: 24
                    visible: page.localArt && portraitArea.containsMouse
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.width: 2
                    border.color: Qt.rgba(1, 1, 1, 0.85)
                    Text {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        text: "▶"
                        font.pixelSize: 17
                        color: "white"
                    }
                }
                MouseArea {
                    id: portraitArea
                    objectName: "artistHeroArea"
                    anchors.fill: parent
                    z: 5
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) page.contextRequested()
                        else page.playTracks(0)
                    }
                    drag.target: DeviceService.connected ? portraitGhost : null
                    preventStealing: DeviceService.connected
                    onPressed: mouse => portraitGhost.place(portraitArea, mouse)
                    onReleased: portraitGhost.drop()
                }
                DragGhost {
                    id: portraitGhost
                    area: portraitArea
                    dragKind: page.deviceSource ? "device-tracks" : "tracks"
                    dragTracks: () => page.deviceSource ? page.fullTracks
                        : page.fullTracks.map(t => page._syncItemOf(t))
                    Item {
                        width: Theme.spaceHuge * 2
                        height: width
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: Theme.artworkPlaceholder
                        }
                        ArtworkImage {
                            anchors.fill: parent
                            source: artistPhoto.source
                            fillMode: Image.PreserveAspectCrop
                            layer.enabled: true
                            layer.effect: OpacityMask {
                                maskSource: Rectangle {
                                    width: Theme.spaceHuge * 2
                                    height: width
                                    radius: width / 2
                                }
                            }
                        }
                    }
                }
            }

            // ── The headline — huge, like the top of the bill ──
            // NOT fillWidth — GradientText stretches its text texture
            // to the item width (the smeared-title bug)
            GradientText {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: bill.width
                text: page.artistName.toLowerCase()
                font.family: Theme.displayFamily
                font.pixelSize: page.narrow ? 32 : 46
                tracking: 1
                rotation: -1.5
            }

            // Meta + inline actions, one centered line (board mock)
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spaceLg

                Text {
                    text: page.albums.length
                          + (page.albums.length === 1 ? " album" : " albums")
                          + "  ·  " + page.tracks.length + " tracks"
                    font.pixelSize: 11
                    font.weight: Font.Light
                    color: Theme.textSecondary
                }
                Text {
                    visible: page.localArt
                    text: "▶ add to now playing"
                    font.pixelSize: 12
                    color: npAllArea.containsMouse ? Theme.activePink
                                                   : Theme.textSecondary
                    MouseArea {
                        id: npAllArea
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
                    visible: page.localArt
                    enabled: TrayState.building || DeviceService.connected
                    showPick: false
                    armed: TrayState.building
                    onAddAll: {
                        if (TrayState.building) {
                            TrayState.addTracks(page.fullTracks.map(
                                t => page._syncItemOf(t)))
                            return
                        }
                        if (!DeviceService.connected) return
                        const res = SyncEngine.addTracks(page.fullTracks.map(
                            t => page._syncItemOf(t)))
                        toastHost.show(res.error || (res.added > 0
                            ? "queued " + res.added + " tracks"
                            : (res.onDevice ?? 0) > 0
                              ? "already on the zune"
                              : "already queued"))
                    }
                }
            }

            // ── On the bill: albums (open the vinyl page) ──
            Text {
                visible: page.albums.length > 0
                Layout.topMargin: Theme.spaceLg
                text: "ALBUMS"
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.5
                color: Theme.textDim
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceMd

                Repeater {
                    model: page.albums
                    AlbumCard {
                        required property var modelData
                        name: modelData.name
                        subtitle: modelData.subtitle
                        artItemId: modelData.artItemId
                        localArt: page.localArt
                        artArtist: modelData.artArtist || ""
                        artAlbum: modelData.artAlbum || ""
                        artFilepath: modelData.firstTrackPath || ""
                        cardSize: page.narrow ? 130 : 160
                        onSelected: page.albumSelected(MusicIdentity.fromAlbum(modelData))
                        onContextRequested: page.albumContextRequested(MusicIdentity.fromAlbum(modelData))
                        deviceSide: page.deviceSource
                        addArmed: !page.deviceSource && TrayState.building
                        dragTracks: () => page.albumDragTracks(MusicIdentity.fromAlbum(modelData))
                        onPlayed: page.albumPlayRequested(MusicIdentity.fromAlbum(modelData))
                        onAddAll: page.albumAddRequested(MusicIdentity.fromAlbum(modelData))
                        onPickParts: page.albumPickRequested(MusicIdentity.fromAlbum(modelData), this)
                    }
                }
            }

            Text {
                Layout.topMargin: Theme.spaceSm
                text: "ALL TRACKS"
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.5
                color: Theme.textDim
            }
            Column {
                Layout.fillWidth: true

                Repeater {
                    model: page.tracks
                    TrackListRow {
                        required property var modelData
                        required property int index
                        width: bill.width
                        track: modelData
                        localArt: page.localArt
                        onPlayed: page.playTracks(index)
                        showNumber: true
                        showArtist: false
                        showAlbum: true
                        albumLink: true
                        syncable: page.localArt
                        // Device tracks: rule-of-3 delete + save, same as
                        // the Songs tab (was missing in the drill-down).
                        deletable: page.deviceSource
                        saveable: page.deviceSource
                        onSyncRequested: TrayState.building
                            ? TrayState.addTracks([page._syncItemOf(modelData)])
                            : SyncEngine.addTracks([page._syncItemOf(modelData)])
                        onDeleteRequested:
                            DeviceService.purgeItems([modelData.itemId])
                        onSaveRequested: DeviceService.saveTrackToLibrary(
                            modelData.itemId, modelData.title,
                            modelData.artist, modelData.album)
                        onEditRequested: page.trackEditRequested(modelData)
                        onAlbumTapped: page.albumSelected(MusicIdentity.fromTrack(modelData))
                    }
                }
            }
        }
    }
}
