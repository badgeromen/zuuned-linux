import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Zuuned

// Device playlists — Phase 9. List → detail drill-down (house style),
// right-click delete. Playlists come from MTP enumeration at breach
// (the ZMDB path is blind). Creation is library-first (UX-2): the
// + button opens the builder tray; syncing to the zune uses the 0x9808
// forge that survives re-index.
Item {
    id: root

    property var openPlaylist: null
    // Re-rolls which cover each tape's label wears, per visit (same
    // ritual as the library's mixtape wall).
    property int tapeShuffleTick: 0
    function memberTrack(itemId) {
        const info = DeviceService.trackInfo(itemId)
        return Object.assign({}, info, {
            itemId: itemId, libraryId: -1, filepath: "",
            title: info.title || ("track " + itemId)
        })
    }
    function saveMember(itemId) {
        if (!DeviceService.connected) return
        const track = memberTrack(itemId)
        DeviceService.saveTrackToLibrary(itemId, track.title, track.artist || "", track.album || "")
        toastHost.show("saving " + track.title + "…")
    }

    Connections {
        target: DeviceService
        function onStateChanged() {
            if (root.openPlaylist)
                root.openPlaylist = DeviceService.playlistsList.find(
                    p => p.itemId === root.openPlaylist.itemId) || null
        }
        function onPlaylistCreated(name, ok) {
            toastHost.show(ok ? "playlist '" + name + "' created on the zune"
                              : "playlist creation failed")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header band — fixed height, on the app-wide spine, wearing
        // the same tab language as every other section (2026-09-06)
        PageHeaderBand {
            Layout.leftMargin: Gallery.spine(root.width)
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceMd

            // The band NEVER changes — drills add a breadcrumb below
            PivotBar {
                Layout.preferredWidth: 104
                Layout.alignment: Qt.AlignVCenter
                tabs: ["Playlists"]
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.openPlaylist === null
                    ? DeviceService.playlistsList.length + " playlists"
                    : (root.openPlaylist.trackIds ?? []).length + " tracks"
                font.pixelSize: 12
                color: Theme.textDim
            }
            Text {
                visible: root.openPlaylist === null
                // UX-2: playlists are born in the LIBRARY (the builder
                // tray) and sync over — no more create-on-zune sheet.
                text: "+ new playlist (in your library)"
                font.pixelSize: 13
                color: newPlArea.containsMouse ? Theme.activePink : Theme.pink
                MouseArea {
                    id: newPlArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: TrayState.openNew()
                }
            }
        }

        // Breadcrumb under the band (music's drill pattern)
        MusicBreadcrumbBar {
            Layout.fillWidth: true
            visible: root.openPlaylist !== null
            inset: Gallery.spine(root.width)
            crumbs: ["Playlists",
                     root.openPlaylist ? root.openPlaylist.name : ""]
            onBackClicked: root.openPlaylist = null
            onCrumbClicked: root.openPlaylist = null
        }

        // ── Playlist wall — the device wears the library's mixtape
        // style (P4): cassettes in a drawer, art-slice labels from the
        // zune's own album art. Click opens the tape's track list
        // (device tracks don't play locally); right-click deletes. ──
        Flickable {
            visible: root.openPlaylist === null
            onVisibleChanged: if (visible) root.tapeShuffleTick++
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            clip: true
            contentHeight: devTapeFlow.height + 28 + Theme.spaceXxl
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {
                parent: root
                visible: root.openPlaylist === null && size < 1.0
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }

            // padded so the tilted corners never shear at the viewport;
            // centered shelf, capped tapes-per-row (grid study 2026-09-06)
            Flow {
                id: devTapeFlow
                readonly property int gap: 48
                // Left-anchored on the app-wide spine; tapes wear the
                // canonical cell width (176 classic … 240 cap)
                readonly property real x0: Math.max(14,
                    Gallery.spine(root.width) - Theme.spaceLg)
                readonly property int tapeW: Math.min(240, Math.max(176,
                    Gallery.canonicalPlan(root.width).cell))
                readonly property int cols: Math.max(1, Math.min(6,
                    Math.floor((parent.width - x0 - 14 + gap)
                               / (tapeW + gap))))
                width: cols * tapeW + (cols - 1) * gap
                x: x0
                y: 14
                spacing: gap

                Repeater {
                    model: DeviceService.playlistsList
                    delegate: Item {
                        id: devTape
                        required property var modelData
                        width: devTapeFlow.tapeW
                        height: Math.round(width * 112 / 176) + 22

                        // Same drawer physics as the library: stable
                        // per-tape tilt hashed from the name.
                        readonly property real tilt: {
                            let h = 0
                            const n = modelData.name || ""
                            for (let i = 0; i < n.length; i++)
                                h = ((h << 5) - h + n.charCodeAt(i)) | 0
                            return ((Math.abs(h) % 50) / 10) - 2.5
                        }

                        // Fetch art for up to 8 distinct member albums
                        // (device art is representative-sample per id).
                        function requestArts() {
                            const ids = devTape.modelData.trackIds ?? []
                            const seen = ({})
                            let n = 0
                            for (let i = 0; i < ids.length && n < 8; i++) {
                                const info = DeviceService.trackInfo(ids[i])
                                const alb = info.album ?? ""
                                if (alb === "" || seen[alb])
                                    continue
                                seen[alb] = true
                                n++
                                DeviceService.requestArt(ids[i])
                            }
                        }
                        Component.onCompleted: requestArts()
                        onModelDataChanged: requestArts()

                        readonly property var artUrls: {
                            void DeviceService.artPaths
                            const ids = devTape.modelData.trackIds ?? []
                            const seen = ({})
                            const urls = []
                            for (let i = 0; i < ids.length
                                     && urls.length < 8; i++) {
                                const info = DeviceService.trackInfo(ids[i])
                                const alb = info.album ?? ""
                                if (alb === "" || seen[alb])
                                    continue
                                seen[alb] = true
                                const u = DeviceService.artPaths[
                                    String(ids[i])] ?? ""
                                if (u !== "")
                                    urls.push(u)
                            }
                            return urls
                        }
                        readonly property var accents: {
                            if (devTape.artUrls.length === 0)
                                return []
                            const cols = devTape.artUrls.map(
                                u => LibraryService.dominantColor(u))
                            cols.sort((a, b) => a.hslHue - b.hslHue)
                            return [cols[Math.floor(cols.length / 2)],
                                    cols[Math.floor(cols.length / 4)]]
                        }
                        readonly property url labelArt: {
                            void root.tapeShuffleTick
                            return devTape.artUrls.length === 0 ? ""
                                : devTape.artUrls[Math.floor(
                                    Math.random() * devTape.artUrls.length)]
                        }

                        CassetteTile {
                            id: devCassette
                            width: devTape.width
                            name: devTape.modelData.name
                            accents: devTape.accents
                            labelArt: devTape.labelArt
                            rotation: devTape.tilt
                            scale: devTapeArea.containsMouse ? 1.04 : 1
                            Behavior on scale {
                                NumberAnimation { duration: Theme.motionFast }
                            }
                        }
                        MouseArea {
                            id: devTapeArea
                            anchors.fill: devCassette
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: mouseEvent => {
                                if (mouseEvent.button === Qt.RightButton) {
                                    plMenu.target = devTape.modelData
                                    plMenu.popup()
                                } else {
                                    root.openPlaylist = devTape.modelData
                                }
                            }
                            // Drag device playlist → library sidebar
                            // (recreate it here; owned tracks join, the
                            // rest are pulled + auto-joined).
                            readonly property bool dragArmed: DeviceService.connected
                            drag.target: dragArmed ? devTapeGhost : null
                            preventStealing: dragArmed
                            onPressed: mouse => devTapeGhost.place(devTapeArea, mouse)
                            onReleased: devTapeGhost.drop()
                        }
                        DragGhost {
                            id: devTapeGhost
                            area: devTapeArea
                            dragKind: "device-playlist"
                            dragName: devTape.modelData.name
                            dragTracks: () =>
                                (devTape.modelData.trackIds ?? []).map(id => {
                                    const info = DeviceService.trackInfo(id)
                                    return { itemId: id,
                                             title: info.title ?? "",
                                             artist: info.artist ?? "",
                                             album: info.album ?? "" }
                                })
                            Rectangle {
                                width: 72; height: 46; radius: 4
                                color: Theme.artworkPlaceholder
                                border.width: 1
                                border.color: Qt.alpha(Theme.orange, 0.75)
                                clip: true
                                ArtworkImage {
                                    anchors.fill: parent; anchors.margins: 1
                                    source: devTape.labelArt
                                    fillMode: Image.PreserveAspectCrop
                                    visible: String(devTape.labelArt) !== ""
                                }
                            }
                        }
                        Text {
                            anchors.left: devCassette.left
                            anchors.top: devCassette.bottom
                            anchors.topMargin: 4
                            text: devTape.modelData.count + " tracks"
                            font.pixelSize: 10
                            color: Theme.textDim
                        }
                    }
                }
            }

            Text {
                visible: DeviceService.playlistsList.length === 0
                anchors.centerIn: parent
                text: DeviceService.connected
                    ? "no playlists on this zune yet — make one!"
                    : "connect a zune to browse its playlists"
                font.pixelSize: 14
                color: Theme.textDim
            }
        }

        // ── Detail: resolved track rows ──
        ListView {
            visible: root.openPlaylist !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            clip: true
            spacing: 1
            model: root.openPlaylist ? (root.openPlaylist.trackIds ?? []) : []
            delegate: TrackListRow {
                id: dtRow
                required property var modelData
                required property int index
                objectName: "devicePlaylistTrack" + modelData
                width: ListView.view.width
                track: root.memberTrack(modelData)
                displayTrackNumber: index + 1
                showNumber: true
                showArt: true
                localArt: false
                saveable: true
                deletable: true
                onSaveRequested: root.saveMember(modelData)
                onDeleteRequested: {
                    if (DeviceService.connected) DeviceService.purgeItems([modelData])
                }
            }
        }
    }

    // ── Context menu ──
    ZuneMenu {
        id: plMenu
        property var target: null
        ZuneMenuItem {
            text: "Delete from Zune"
            enabled: DeviceService.connected
            onTriggered: {
                if (!DeviceService.connected) return
                DeviceService.purgeItems([plMenu.target.itemId])
                toastHost.show("deleting '" + plMenu.target.name + "'…")
            }
        }
    }

}
