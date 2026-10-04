import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import "MusicIdentity.js" as MusicIdentity

// Genre drill — the ArtistDetail concert bill wearing a genre's face
// (Orson 2026-09-06: "similar to the artist view styling"). Centered
// 2×2 album mosaic under the stage light instead of a portrait, the
// genre name HUGE beneath, albums and tracks stacked below. Locked
// rules carry over: the mosaic always plays, actions stay inline,
// albums open the vinyl page.
Item {
    id: page

    property string genreName
    // Snapshot rows for this genre {title, artist, albumartist?, album,
    // durationMs, trackNumber, itemId, filepath?, genre}
    property var tracks: []
    property var fullTracks: tracks
    property var albumTracksResolver: null
    // Up to 4 cover urls for the mosaic (host resolves art keys)
    property var artUrls: []
    property url customArt: ""
    property bool localArt: false
    property bool deviceSource: false

    signal albumSelected(var album)
    signal artistSelected(string artist)
    signal albumPlayRequested(var album)
    signal albumAddRequested(var album)
    signal albumPickRequested(var album, var anchor)
    signal albumContextRequested(var album)
    signal trackEditRequested(var track)
    signal contextRequested()

    readonly property bool narrow: width < 700

    // Playback (local library only) — same contract as ArtistDetail
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

    // Albums grouped from this genre's tracks; each wears ITS OWN
    // artist as the subtitle (a genre spans artists).
    readonly property var albums: MusicIdentity.groupAlbums(tracks)
    function albumDragTracks(album) {
        const members = albumTracksResolver ? albumTracksResolver(album)
            : fullTracks.filter(t => MusicIdentity.matches(t, album))
        return deviceSource ? members : members.map(t => _syncItemOf(t))
    }

    // Stage-light color — pulled from the first cover; house orange
    // stands in (genres wear orange, artists purple).
    readonly property color washAccent: customArt.toString() !== ""
        ? LibraryService.dominantColor(customArt)
        : artUrls.length > 0 ? LibraryService.dominantColor(artUrls[0]) : Theme.orange

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

            // ── The mosaic — always the play button ──
            Item {
                id: mosaicHero
                readonly property int pSize: page.narrow ? 140 : 200
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: pSize
                Layout.preferredHeight: pSize

                Rectangle {
                    id: heroBox
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: Theme.artworkPlaceholder
                    clip: true
                    // Rounded ART needs a mask — a radius on the container
                    // doesn't clip the Image (QML clip is rectangular)
                    layer.enabled: true
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: heroBox.width; height: heroBox.height
                            radius: Theme.radiusMd
                        }
                    }
                    // 2×2 covers; a single cover fills the square
                    Grid {
                        objectName: "genreDetailMosaic"
                        anchors.fill: parent
                        columns: page.artUrls.length > 1 ? 2 : 1
                        visible: page.customArt.toString() === "" && page.artUrls.length > 0
                        Repeater {
                            model: page.artUrls.slice(
                                0, page.artUrls.length > 1 ? 4 : 1)
                            ArtworkImage {
                                required property var modelData
                                width: page.artUrls.length > 1
                                       ? mosaicHero.pSize / 2 : mosaicHero.pSize
                                height: width
                                source: modelData
                                fillMode: Image.PreserveAspectCrop
                                sourceSize.width: 256
                                asynchronous: true
                            }
                        }
                    }
                    ArtworkImage {
                        objectName: "genreDetailCustomArt"
                        anchors.fill: parent
                        source: page.customArt
                        visible: page.customArt.toString() !== "" && status === Image.Ready
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: Theme.customizeArtSize * 2
                        asynchronous: true
                    }
                }
                // Spotlight rim — the stage light catching the subject
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.rgba(0.83, 0.21, 0.48,
                                          mosaicArea.containsMouse ? 0.9 : 0.6)
                    Behavior on border.color {
                        ColorAnimation { duration: Theme.motionFast }
                    }
                }
                // Hover play ring — same affordance as the portrait
                Rectangle {
                    anchors.centerIn: parent
                    width: 48; height: 48; radius: 24
                    visible: page.localArt && mosaicArea.containsMouse
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
                    id: mosaicArea
                    objectName: "genreHeroArea"
                    anchors.fill: parent
                    z: 5
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouse => mouse.button === Qt.RightButton
                        ? page.contextRequested() : page.playTracks(0)
                    drag.target: DeviceService.connected ? genreGhost : null
                    preventStealing: DeviceService.connected
                    onPressed: mouse => genreGhost.place(mosaicArea, mouse)
                    onReleased: genreGhost.drop()
                }
                DragGhost {
                    id: genreGhost
                    area: mosaicArea
                    dragKind: page.deviceSource ? "device-tracks" : "tracks"
                    dragTracks: () => page.deviceSource ? page.fullTracks
                        : page.fullTracks.map(t => page._syncItemOf(t))
                    Rectangle {
                        width: Theme.spaceHuge * 2
                        height: width
                        radius: Theme.radiusMd
                        color: Theme.artworkPlaceholder
                        ArtworkImage {
                            anchors.fill: parent
                            source: page.customArt.toString() !== "" ? page.customArt
                                : page.artUrls.length ? page.artUrls[0] : ""
                            fillMode: Image.PreserveAspectCrop
                        }
                    }
                }
            }

            // ── The headline — huge, like the top of the bill ──
            GradientText {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: bill.width
                text: page.genreName.toLowerCase()
                font.family: Theme.displayFamily
                font.pixelSize: page.narrow ? 32 : 46
                tracking: 1
                rotation: -1.5
            }

            // Meta + inline actions, one centered line
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
                        // A genre spans artists — show them, tappable
                        showNumber: false
                        showArtist: true
                        artistLink: true
                        showAlbum: true
                        albumLink: true
                        syncable: page.localArt
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
                        onArtistTapped: page.artistSelected(modelData.artist)
                        onAlbumTapped: page.albumSelected(MusicIdentity.fromTrack(modelData))
                    }
                }
            }
        }
    }
}
