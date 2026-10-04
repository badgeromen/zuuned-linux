import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "MusicIdentity.js" as MusicIdentity

// Music browser — port of CollectionView.swift's shell over the pivot
// views (MusicPivots.swift) with the drill-stack navigation of
// MusicNavState: always-visible PivotBar (tapping a pivot pops to that
// root), per-level search, breadcrumb bar over the drill stack, and
// detail pages (ArtistDetail/AlbumDetail).
//
// SOURCE-AGNOSTIC, mirroring how the mac shares these components
// between CollectionView (local) and DeviceMusicView (device):
//   MusicPage {}                            → device library (default)
//   MusicPage { source: LibraryService }    → local library
// The two services expose the same shape (tracks model + grouped
// lists + artPaths); art requests differ (device: itemId; local:
// artist/album/filepath) — branched on `deviceSource`.
Item {
    id: root

    property var source: DeviceService
    readonly property bool deviceSource: source === DeviceService
    readonly property bool browserAvailable:
        deviceSource ? DeviceService.connected : source.trackCount > 0

    // ── Track snapshot ──
    // ONE C++ call per model change (rowsSnapshot on both track
    // models, identical keys). This replaced a QML Instantiator that
    // materialized a QObject PER ROW: at 40k tracks every model reset
    // froze the UI thread instantiating delegates and the deleteLater
    // backlog ballooned memory until the OOM killer took the box down
    // (E5 scale pass, 2026-09-02).
    property var allTracks: []

    Connections {
        target: root.source.tracks
        function onModelReset() { rebuildTimer.restart() }
        function onRowsInserted() { rebuildTimer.restart() }
        function onRowsRemoved() { rebuildTimer.restart() }
        function onDataChanged() { rebuildTimer.restart() }
    }
    Timer {
        id: rebuildTimer
        interval: 50
        onTriggered: root.rebuildSnapshot()
    }
    function rebuildSnapshot() {
        allTracks = root.source.tracks.rowsSnapshot()
    }
    Component.onCompleted: {
        rebuildSnapshot()
        // Album → Add to Playlist ▸ (library side; submenus can't hide
        // via visible/height, so it's attached conditionally)
        if (!root.deviceSource)
            albumCtxMenu.insertMenu(1, albumPlSubmenu)
    }

    // ── Navigation (MusicNavState) ──
    // Album drill entries carry their owner as well as their title.
    property var navStack: []
    property string searchText: ""
    // A–Z rail filter — "" shows everything; cleared on pivot change
    property string letterFilter: ""
    function letterOf(v) {
        const c = (v || "").charAt(0).toLowerCase()
        return /[a-z]/.test(c) ? c : "#"
    }
    function applyLetter(list, key) {
        if (letterFilter === "")
            return list
        return list.filter(r => root.letterOf(r[key]) === letterFilter)
    }

    readonly property var drill: navStack.length > 0 ? navStack[navStack.length - 1] : null

    // ── ONE spine, ONE plan per page (2026-09-06): every pivot's
    // content left-anchors to the same x and shares the same cell
    // width, so a tab click never moves the tabs, re-centers the
    // block, or resizes the tiles (per-tab centering + per-shape
    // cells made every pivot click a horizontal teleport — jarring).
    // spineFor computes from root width (loop rule); the StackLayout
    // reserves 26px on the right, so the galleries see width − 26. ──
    readonly property real pageSpineX: Gallery.spine(width)
    // Songs fills the SAME rectangle as the grids — every pivot
    // occupies one identical frame; only the content changes
    readonly property real songsTableW: Gallery.frameW(width)
    readonly property real songsX: pageSpineX
    // The band NEVER moves — drills included (drilling used to snap
    // the tabs to a fixed inset: the last mover)
    readonly property real contentSpineX: pageSpineX

    // Which pivot root is live — the sidebar highlight needs it to
    // tell "music" apart from "playlists" (same page, different pivot).
    readonly property string currentPivot: pivots.current

    // Jump straight to a pivot root (sidebar "playlists" lands here)
    function openPivot(name) {
        pivots.current = name
        navStack = []
        searchText = ""
    }

    function pushDrill(d) {
        navStack = navStack.concat([d])
        searchText = ""
    }
    function openAlbum(album) {
        root.pushDrill({ kind: "album", name: album.name, artist: album.artist })
    }
    function popDrill() {
        navStack = navStack.slice(0, -1)
        searchText = ""
    }
    function popTo(depth) {
        navStack = navStack.slice(0, depth)
        searchText = ""
    }

    // ── Groupings (MusicBrowserModel / MusicGroupings) ──
    // Albums retain their owner throughout every action. Device rows
    // use their available track artist; local rows use albumartist.
    function computeArtists(rows) {
        const map = {}
        for (const t of rows) {
            const name = t.artist.length > 0 ? t.artist : "Unknown Artist"
            if (!map[name])
                map[name] = { name: name, count: 0, artItemId: t.itemId }
            map[name].count++
        }
        return Object.values(map).sort((a, b) =>
            a.name.localeCompare(b.name, undefined, { sensitivity: "base" }))
    }

    function computeAlbums(rows) {
        return MusicIdentity.groupAlbums(rows)
    }

    function filterNamed(list, q) {
        if (!q || q.length === 0)
            return list
        const t = q.toLowerCase()
        return list.filter(e => (e.name || "").toLowerCase().includes(t)
                             || (e.subtitle || "").toLowerCase().includes(t))
    }

    readonly property var artistRows: filterNamed(
        deviceSource ? computeArtists(allTracks) : source.artistsList,
        navStack.length === 0 ? searchText : "")
    readonly property var albumRows: filterNamed(
        deviceSource ? computeAlbums(allTracks) : source.albumsList,
        navStack.length === 0 ? searchText : "")
    readonly property var genreRows: filterNamed(source.genresList,
        navStack.length === 0 ? searchText : "")

    // ── Songs pivot: search + sortable columns ──
    property string sortField: "title"
    property bool sortAscending: true

    function computeSongs(rows, q, field, asc) {
        let out = rows
        if (q && q.length > 0) {
            const t = q.toLowerCase()
            out = out.filter(r => r.title.toLowerCase().includes(t)
                              || r.artist.toLowerCase().includes(t)
                              || r.album.toLowerCase().includes(t))
        }
        const dir = asc ? 1 : -1
        out = out.slice().sort((a, b) => {
            if (field === "duration")
                return (a.durationMs - b.durationMs) * dir
            const av = a[field] || "", bv = b[field] || ""
            return av.localeCompare(bv, undefined, { sensitivity: "base" }) * dir
        })
        return out
    }

    readonly property var songsRows: computeSongs(allTracks, navStack.length === 0 ? searchText : "", sortField, sortAscending)
    readonly property var displayedSongs: applyLetter(songsRows,
        sortField === "duration" ? "title" : sortField)

    // ── Detail data ──
    function artistKeyOf(t) {
        return MusicIdentity.owner(t)
    }

    function tracksByGenre(name, q) {
        let out = allTracks.filter(t => (t.genre || "") === name)
        if (q && q.length > 0) {
            const s = q.toLowerCase()
            out = out.filter(r => r.title.toLowerCase().includes(s)
                              || r.album.toLowerCase().includes(s)
                              || r.artist.toLowerCase().includes(s))
        }
        return out.sort((a, b) =>
            (a.album || "").localeCompare(b.album || "", undefined,
                                          { sensitivity: "base" })
            || MusicIdentity.compareAlbumTracks(a, b))
    }
    function tracksByArtist(name, q) {
        let out = allTracks.filter(t =>
            root.artistKeyOf(t) === name || t.artist === name)
        if (q && q.length > 0) {
            const s = q.toLowerCase()
            out = out.filter(r => r.title.toLowerCase().includes(s)
                              || r.album.toLowerCase().includes(s))
        }
        // An artist page displays track numbers, so preserve album sequence:
        // albums alphabetically, then disc/track within each album. Reuse the
        // device plausibility guard because Keel snapshots can carry 2297 in
        // every TrackNumber field.
        let ordered = []
        for (const album of MusicIdentity.groupAlbums(out)) {
            const identity = MusicIdentity.fromAlbum(album)
            let members = out.filter(t => MusicIdentity.matches(t, identity))
            if (!root.deviceSource) {
                members.sort(MusicIdentity.compareAlbumTracks)
            } else {
                const plausible = members.every(t =>
                    t.trackNumber > 0 && t.trackNumber <= 99)
                members.sort((a, b) => plausible
                    ? a.trackNumber - b.trackNumber
                    : a.title.localeCompare(b.title, undefined,
                                            { sensitivity: "base" }))
            }
            ordered = ordered.concat(members)
        }
        return ordered
    }

    function tracksInAlbum(album, q) {
        let out = allTracks.filter(t => MusicIdentity.matches(t, album))
        if (q && q.length > 0) {
            const s = q.toLowerCase()
            out = out.filter(r => r.title.toLowerCase().includes(s)
                              || r.artist.toLowerCase().includes(s))
        }
        // Track numbers can be garbage on the device (see TrackListRow)
        // — sort by them only when plausible, else title.
        if (!root.deviceSource) return out.sort(MusicIdentity.compareAlbumTracks)
        const plausible = out.every(t => t.trackNumber > 0 && t.trackNumber <= 99)
        return out.sort((a, b) => plausible
            ? a.trackNumber - b.trackNumber
            : a.title.localeCompare(b.title, undefined, { sensitivity: "base" }))
    }

    // ── Playback (MusicActions.playFrom) — local library only; device
    // tracks aren't files we can stream (mac parity: copy first) ──
    function toQueueItems(list) {
        return list.map(t => ({
            filepath: t.filepath || "", title: t.title || "",
            artist: t.artist || "", album: t.album || "",
            discNumber: t.discNumber || 0, year: t.year || 0,
            libraryId: t.libraryId !== undefined ? t.libraryId : -1
        }))
    }
    function playFromList(list, index) {
        if (root.deviceSource || list.length === 0)
            return
        PlayerService.setQueue(toQueueItems(list), Math.max(0, index))
    }
    // Every track of the album present on the connected device?
    function albumFullyOnDevice(album) {
        if (root.deviceSource || !DeviceService.connected)
            return false
        void DeviceService.musicIdentityRevision
        void LibraryService.localTrackIdentityRevision
        let found = 0
        for (const t of allTracks) {
            if (!MusicIdentity.matches(t, album))
                continue
            if (!DeviceService.hasDeviceTrack(t, LibraryService.musicIdentityPeers(t)))
                return false
            found++
        }
        return found > 0
    }

    // Up to 4 distinct albums for a genre — the mac's 2x2 mosaic refs
    function genreAlbumRefs(genreName) {
        const seen = {}
        const out = []
        for (const t of allTracks) {
            if ((t.genre || "") !== genreName || !t.album)
                continue
            const aa = (t.albumartist && t.albumartist.length > 0)
                       ? t.albumartist : (t.artist || "")
            const key = (aa + "\n" + t.album).toLowerCase()
            if (seen[key])
                continue
            seen[key] = true
            out.push({ artist: aa, album: t.album,
                       filepath: t.filepath || "", itemId: t.itemId || 0 })
            if (out.length >= 4)
                break
        }
        return out
    }

    // Sync-queue entry in SyncEngine's contract shape
    function syncItemOf(t) {
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

    function appendList(list) {
        if (root.deviceSource || list.length === 0)
            return
        PlayerService.appendToQueue(toQueueItems(list))
    }

    readonly property var breadcrumbTitles: {
        const out = [pivots.current]
        for (const d of navStack)
            out.push(d.name)
        return out
    }

    // Sortable songs-table column header — click active column to flip.
    component SortHeader: Item {
        id: sh
        property string title
        property string field
        readonly property bool active: root.sortField === field

        implicitHeight: shLabel.implicitHeight + Theme.spaceSm * 2
        implicitWidth: shLabel.implicitWidth + 12

        Text {
            id: shLabel
            anchors.verticalCenter: parent.verticalCenter
            text: sh.title + (sh.active ? (root.sortAscending ? "  ▾" : "  ▴") : "")
            font.pixelSize: 12
            font.weight: Font.Light
            font.letterSpacing: 1
            color: sh.active ? Qt.rgba(0.83, 0.21, 0.48, 0.8) : Theme.textDim
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (sh.active)
                    root.sortAscending = !root.sortAscending
                else {
                    root.sortField = sh.field
                    root.sortAscending = true
                }
            }
        }
    }

    // ═══════════ Disconnected / empty state ═══════════
    ColumnLayout {
        anchors.centerIn: parent
        visible: !root.browserAvailable
        spacing: Theme.spaceMd

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "♪"
            font.pixelSize: 40
            font.weight: Font.Light
            color: Theme.textDim
        }
        GradientText {
            Layout.alignment: Qt.AlignHCenter
            text: root.deviceSource
                  ? (DeviceService.busy ? "breaching…" : "no device")
                  : (LibraryService.scanning ? "scanning…" : "your music library")
            font.family: Theme.displayFamily
            font.pixelSize: 26
            tracking: Math.max(26 * 0.08, 2)
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.deviceSource
                  ? (DeviceService.devicePresent
                     ? "zune detected — connecting"
                     : "plug in a zune")
                  : (LibraryService.scanning
                     ? LibraryService.scanStage
                     : "add media folders in settings → media, then rescan")
            font.pixelSize: 14
            font.weight: Font.Light
            color: Theme.textDim
        }
    }

    // ═══════════ Browser ═══════════
    ColumnLayout {
        anchors.fill: parent
        visible: root.browserAvailable
        spacing: 0

        // ── Header band: pivots + count + per-level search — fixed
        // height, pivots on the spine (one chrome band, 2026-09-06) ──
        PageHeaderBand {
            Layout.leftMargin: root.contentSpineX
            Layout.rightMargin: Theme.spaceXxxl
            spacing: 0

            PivotBar {
                id: pivots
                // root-derived, not parent.width — a self-referential
                // width in a layout row rearranges recursively
                // Floored at the tabs' OWN width. PivotBar never
                // clips, so a reservation smaller than the tabs lets
                // them render outside their box and crowd the chip and
                // search beside them; flooring it makes the layout
                // shrink the flexible spacer instead (Orson 2026-09-12).
                Layout.preferredWidth: Math.max(implicitWidth,
                    Math.min(420, root.width * 0.4))
                Layout.minimumWidth: implicitWidth
                Layout.alignment: Qt.AlignVCenter
                // Library side gains the UX-2 Playlists pivot; device
                // playlists live on their own device page.
                tabs: root.deviceSource
                    ? ["Artists", "Albums", "Songs", "Genres"]
                    : ["Artists", "Albums", "Songs", "Genres", "Playlists"]
                // Tapping a pivot from a detail page pops to that root.
                onCurrentChanged: {
                    root.navStack = []
                    root.searchText = ""
                    root.letterFilter = ""
                    artistsGallery.letterFilter = ""
                    albumsGallery.letterFilter = ""
                }
            }

            Item { Layout.fillWidth: true }


            // Right cluster: ＋ all music beside an ELASTIC search
            // (shrinks before it can crowd the pivots), track count on
            // its own row beneath — per design spec.
            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.spaceMd

                // UX-2: the whole library in one move (capacity gate and
                // on-device dedup do the pruning).
                Rectangle {
                    visible: !root.deviceSource && DeviceService.connected
                             && !TrayState.building
                    Layout.leftMargin: Theme.spaceLg
                    implicitWidth: allMusicLabel.implicitWidth + 22
                    implicitHeight: 24
                    radius: Theme.radiusMd
                    color: allMusicArea.containsMouse
                        ? Qt.alpha(Theme.pink, 0.9) : Qt.rgba(0, 0, 0, 0.5)
                    border.width: 1
                    border.color: Qt.alpha(Theme.pink, 0.5)
                    scale: allMusicArea.pressed ? 0.95 : 1
                    Behavior on scale { NumberAnimation { duration: 80 } }
                    Text {
                        id: allMusicLabel
                        anchors.centerIn: parent
                        text: "＋ all music"
                        font.pixelSize: 11
                        color: allMusicArea.containsMouse ? "white" : Theme.activePink
                    }
                    MouseArea {
                        id: allMusicArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!DeviceService.connected) return
                            const res = SyncEngine.addTracks(
                                root.allTracks.map(t => root.syncItemOf(t)))
                            toastHost.show("queued " + res.added + " tracks"
                                + (res.duplicates > 0
                                   ? " · " + res.duplicates + " already queued or on the zune"
                                   : ""))
                        }
                    }
                }

                    ZuneSearchField {
                        id: searchField
                        Layout.fillWidth: true
                        Layout.maximumWidth: 200
                        Layout.minimumWidth: 80
                        onTextEdited: root.searchText = text

                        Connections {
                            target: root
                            function onSearchTextChanged() {
                                if (searchField.text !== root.searchText)
                                    searchField.text = root.searchText
                            }
                        }
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    Layout.rightMargin: Theme.spaceXs
                    text: root.source.trackCount + " tracks"
                    font.pixelSize: 11
                    font.weight: Font.Light
                    color: Theme.textDim
                }
            }

        }

        // ── Breadcrumb (only when drilled) ──
        MusicBreadcrumbBar {
            Layout.fillWidth: true
            visible: root.navStack.length > 0
            inset: root.pageSpineX
            crumbs: root.breadcrumbTitles
            onBackClicked: root.popDrill()
            onCrumbClicked: depth => root.popTo(depth)
        }

        // ── Content ──
        Item {
            id: musicContent
            Layout.fillWidth: true
            Layout.fillHeight: true

            // A–Z jump rail — Songs only now (a table filters; the
            // Artists/Albums galleries carry their own rails per the
            // grid study, 2026-09-06). Rides the table's right shoulder.
            AlphabetRail {
                x: Math.min(root.songsX + root.songsTableW + 14,
                            parent.width - width - 2)
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: Theme.spaceXl
                anchors.bottomMargin: Theme.spaceXl
                z: 5
                visible: root.drill === null && pivots.current === "Songs"
                available: {
                    const key = root.sortField === "duration"
                              ? "title" : root.sortField
                    const out = {}
                    for (const r of root.songsRows) {
                        const v = (r[key] || "")
                        if (v.length > 0) {
                            const c = v.charAt(0).toLowerCase()
                            out[/[a-z]/.test(c) ? c : "#"] = true
                        }
                    }
                    return Object.keys(out)
                }
                activeLetter: root.letterFilter
                onJump: letter => root.letterFilter = letter
            }

            // Pivot roots — full width; the galleries carry their own
            // rail lane, and the spine math must see the SAME width
            // every other section uses (a 26px lane here put music's
            // spine 13px left of everyone else's: the section shift)
            StackLayout {
                id: pivotStack
                anchors.fill: parent
                visible: root.drill === null
                currentIndex: pivots.tabs.indexOf(pivots.current)

                // ── Artists: round-photo gallery (grid study) ──
                GalleryView {
                    id: artistsGallery
                    idealCell: 210
                    maxCols: 7
                    anchorX: root.pageSpineX
                    // circle = cell − 20; card adds its 44px caption
                    cellHeightFor: (w) => (w - 20) + 44
                    nameOf: (r) => r.name ?? ""
                    rows: root.artistRows

                    cardDelegate: ArtistCard {
                        required property var modelData
                        width: artistsGallery.plan.cell
                        diameter: artistsGallery.plan.cell - 20
                        name: modelData.name
                        count: modelData.count
                        artItemId: modelData.artItemId || 0
                        localArt: !root.deviceSource
                        photoUrl: LibraryService.artistImages[modelData.name.toLowerCase()] ?? ""
                        artworkDiscovery: root.deviceSource ? ({})
                            : (LibraryService.artistArtworkStatus || ({}))[modelData.name.toLowerCase()] || ({})
                        Component.onCompleted: LibraryService.requestArtistImage(modelData.name)
                        onModelDataChanged: LibraryService.requestArtistImage(modelData.name)
                        onSelected: root.pushDrill({ kind: "artist", name: modelData.name })
                        onRightClicked: root.openArtistMenu(modelData.name)
                        deviceSide: root.deviceSource
                        dragTracks: root.deviceSource
                            ? () => root.tracksByArtist(modelData.name, "")
                            : () => root.tracksByArtist(modelData.name, "")
                                        .map(t => root.syncItemOf(t))
                    }
                }

                // ── Albums: card gallery (grid study) ──
                GalleryView {
                    id: albumsGallery
                    idealCell: 210
                    maxCols: 7
                    anchorX: root.pageSpineX
                    cellHeightFor: (w) => w + 42
                    nameOf: (r) => r.name ?? ""
                    rows: root.albumRows

                    cardDelegate: AlbumCard {
                        required property var modelData
                        width: albumsGallery.plan.cell
                        cardSize: albumsGallery.plan.cell
                        onDevice: root.albumFullyOnDevice(MusicIdentity.fromAlbum(modelData))
                        name: modelData.name
                        subtitle: modelData.subtitle || ""
                        artItemId: modelData.artItemId || 0
                        localArt: !root.deviceSource
                        artArtist: modelData.artArtist || ""
                        artAlbum: modelData.artAlbum || modelData.name
                        artFilepath: modelData.firstTrackPath || ""
                        artworkDiscovery: root.deviceSource ? ({})
                            : (LibraryService.albumArtworkStatus || ({}))[LibraryService.artKey(artArtist, artAlbum)] || ({})
                        onSelected: root.openAlbum(MusicIdentity.fromAlbum(modelData))
                        onContextRequested: root.openAlbumMenu(MusicIdentity.fromAlbum(modelData))
                        // Contract tile anatomy (UX-1)
                        deviceSide: root.deviceSource
                        addArmed: root.trayFeeds
                        dragTracks: root.deviceSource
                            ? () => root.tracksInAlbum(MusicIdentity.fromAlbum(modelData), "")
                            : () => root.tracksInAlbum(MusicIdentity.fromAlbum(modelData), "")
                                        .map(t => root.syncItemOf(t))
                        onPlayed: root.playFromList(
                            root.tracksInAlbum(MusicIdentity.fromAlbum(modelData), ""), 0)
                        onAddAll: root.deviceSource
                            ? root.saveAlbumToLibrary(MusicIdentity.fromAlbum(modelData))
                            : root.queueAlbum(MusicIdentity.fromAlbum(modelData))
                        onPickParts: root.openAlbumPicker(MusicIdentity.fromAlbum(modelData), this)
                    }
                }

                // ── Songs: sortable table ──
                ColumnLayout {
                    spacing: 0

                    // Sortable column header — click active column to flip.
                    // Proportional, matching TrackListRow's 0.34 /
                    // 0.22 / 0.22 cells — fixed widths overflowed the
                    // center column at the 1100px window floor.
                    RowLayout {
                        id: songsHeader
                        Layout.fillWidth: true
                        Layout.leftMargin: root.songsX
                        Layout.maximumWidth: root.songsTableW
                        Layout.alignment: Qt.AlignLeft
                        spacing: 0

                        SortHeader { title: "TITLE"; field: "title"; Layout.preferredWidth: root.songsTableW * 0.34 }
                        SortHeader { title: "ARTIST"; field: "artist"; Layout.preferredWidth: root.songsTableW * 0.22 }
                        SortHeader { title: "ALBUM"; field: "album"; Layout.preferredWidth: root.songsTableW * 0.22 }
                        Item { Layout.fillWidth: true }
                        SortHeader { title: "TIME"; field: "duration" }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.songsX
                        Layout.maximumWidth: root.songsTableW
                        Layout.alignment: Qt.AlignLeft
                        implicitHeight: 1
                        color: Theme.border
                    }

                    ListView {
                        id: songsList
                        objectName: "songsList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // Rows sit a step wider than the header, as they
                        // always did (spaceXl vs spaceXxxl inset)
                        Layout.leftMargin: root.songsX
                                           - (Theme.spaceXxxl - Theme.spaceXl)
                        Layout.maximumWidth: root.songsTableW
                                             + (Theme.spaceXxxl - Theme.spaceXl) * 2
                        Layout.alignment: Qt.AlignLeft
                        clip: true
                        model: root.displayedSongs
                        boundsBehavior: Flickable.StopAtBounds
                        // Scrollbar at the PAGE edge, same x as every
                        // other section's (a bar that jumps between
                        // pivots reads as the whole page shifting)
                        ScrollBar.vertical: ZuneScrollBar {
                            parent: musicContent
                            visible: songsList.visible && root.drill === null
                                     && size < 1.0
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.right: parent.right
                        }

                        delegate: TrackListRow {
                            required property var modelData
                            required property int index
                            track: modelData
                            showArt: true
                            localArt: !root.deviceSource
                            syncable: !root.deviceSource
                            deletable: root.deviceSource
                            saveable: root.deviceSource
                            onSyncRequested: root.trayFeeds
                                ? TrayState.addTracks([root.syncItemOf(modelData)])
                                : SyncEngine.addTracks([root.syncItemOf(modelData)])
                            onDeleteRequested: DeviceService.purgeItems([modelData.itemId])
                            onSaveRequested: {
                                DeviceService.saveTrackToLibrary(
                                    modelData.itemId, modelData.title,
                                    modelData.artist, modelData.album)
                                toastHost.show("saving " + modelData.title + "…")
                            }
                            artistLink: true
                            albumLink: true
                            onEditRequested: editSheet.openFor(modelData)
                            onPlayed: root.playFromList(root.displayedSongs, index)
                            onArtistTapped: root.pushDrill({ kind: "artist", name: root.artistKeyOf(modelData) })
                            onAlbumTapped: root.openAlbum(MusicIdentity.fromTrack(modelData))
                        }
                    }
                }

                // ── Genres: tile gallery (grid study; few genres →
                // no letter breaks, wider tiles, max 6 across) ──
                GalleryView {
                    id: genresGallery
                    idealCell: 210
                    maxCols: 7
                    lettered: false
                    anchorX: root.pageSpineX
                    cellHeightFor: (w) => w + 42
                    nameOf: (r) => r.name ?? ""
                    rows: root.genreRows

                    cardDelegate: GenreTile {
                        required property var modelData
                        width: genresGallery.plan.cell
                        tileSize: genresGallery.plan.cell
                        name: modelData.name
                        count: modelData.count
                        customArt: {
                            void LibraryService.collectionArtRevision
                            return root.deviceSource ? ""
                                : LibraryService.collectionArt("genre", modelData.name)
                        }
                        onSelected: root.pushDrill(
                            { kind: "genre", name: modelData.name })
                        onContextRequested: root.openGenreMenu(modelData.name)
                        deviceSide: root.deviceSource
                        dragTracks: () => {
                            const members = root.tracksByGenre(modelData.name, "")
                            return root.deviceSource ? members
                                : members.map(t => root.syncItemOf(t))
                        }
                        artUrls: {
                            void LibraryService.artPaths  // re-evaluate on artChanged
                            return root.genreAlbumRefs(modelData.name).map(e =>
                                root.deviceSource
                                    ? (DeviceService.artPaths[String(e.itemId)] ?? "")
                                    : (LibraryService.artPaths[
                                          LibraryService.artKey(e.artist, e.album)] ?? ""))
                                .filter(u => u !== "")
                        }
                        Component.onCompleted: {
                            for (const e of root.genreAlbumRefs(modelData.name)) {
                                if (root.deviceSource)
                                    DeviceService.requestArt(e.itemId)
                                else
                                    LibraryService.requestArt(e.artist, e.album, e.filepath)
                            }
                        }
                    }
                }

                // ── Playlists: P4 MIXTAPES (approved 2026-09-02) — every
                // playlist is a cassette: album-art label slice under
                // the scrawled title, reels that turn while it plays.
                // The TAPE is the play button (house rule); hover verbs
                // edit/sync, right-click menu, drag onto the zune or
                // the tray. ──
                ColumnLayout {
                    spacing: 0
                    // Each visit to the list re-rolls which cover each
                    // label wears (delegates key their random pick off
                    // this tick).
                    onVisibleChanged: if (visible) root.tapeShuffleTick++

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spaceXxxl
                        Layout.rightMargin: Theme.spaceXxxl
                        Layout.topMargin: Theme.spaceLg

                        Item { Layout.fillWidth: true }
                        Rectangle {
                            implicitWidth: newPlLabel.implicitWidth + 28
                            implicitHeight: 28
                            radius: Theme.radiusMd
                            color: newPlArea.containsMouse
                                ? Qt.lighter(Theme.orange, 1.12) : Theme.orange
                            scale: newPlArea.pressed ? 0.95 : 1
                            Behavior on scale { NumberAnimation { duration: 80 } }
                            Text {
                                id: newPlLabel
                                anchors.centerIn: parent
                                text: "＋ new mixtape"
                                font.pixelSize: 12
                                font.bold: true
                                color: "white"
                            }
                            MouseArea {
                                id: newPlArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: TrayState.openNew()
                            }
                        }
                    }

                    Flickable {
                        id: tapeFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: Theme.spaceXxxl
                        Layout.rightMargin: Theme.spaceXxl
                        Layout.topMargin: Theme.spaceLg
                        clip: true
                        contentHeight: tapeFlow.height + 28 + Theme.spaceXxl
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ZuneScrollBar {
                            parent: musicContent
                            visible: tapeFlick.visible && root.drill === null
                                     && size < 1.0
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.right: parent.right
                        }

                        // The drawer's tilt needs air: pad inside the
                        // clipping viewport so turned corners (and the
                        // hover lift) never shear at the edges.
                        // Centered shelf, capped tapes-per-row, real air
                        // (grid study 2026-09-06).
                        Flow {
                            id: tapeFlow
                            readonly property int gap: 48
                            // Left-anchored on the page spine; tapes
                            // wear the page's shared cell width
                            // (176 classic … 240 cap)
                            readonly property real x0: Math.max(14,
                                root.pageSpineX - Theme.spaceXxxl)
                            readonly property int tapeW: Math.min(240,
                                Math.max(176, artistsGallery.plan.cell))
                            readonly property int cols: Math.max(1, Math.min(6,
                                Math.floor((parent.width - x0 - 14 + gap)
                                           / (tapeW + gap))))
                            width: cols * tapeW + (cols - 1) * gap
                            x: x0
                            y: 14
                            spacing: gap

                            Repeater {
                                model: LibraryService.playlists
                                delegate: Item {
                                    id: tapeTile
                                    required property var modelData
                                    width: tapeFlow.tapeW
                                    height: Math.round(width * 112 / 176) + 22

                                    // Stable per-tape tilt, hashed from
                                    // the name — a drawer of real tapes
                                    // never sits perfectly straight.
                                    readonly property real tilt: {
                                        let h = 0
                                        const n = modelData.name || ""
                                        for (let i = 0; i < n.length; i++)
                                            h = ((h << 5) - h + n.charCodeAt(i)) | 0
                                        return ((Math.abs(h) % 50) / 10) - 2.5
                                    }
                                    readonly property bool hovering:
                                        tapeArea.containsMouse
                                        || tapeEditArea.containsMouse
                                        || tapeSyncArea.containsMouse

                                    // Actively fetch this mixtape's album
                                    // art — nothing else on this view
                                    // requests it, so without this the
                                    // tapes only colored up once some
                                    // OTHER view (or pressing play) had
                                    // warmed the cache.
                                    function requestArts() {
                                        const rows = LibraryService
                                            .playlistTracks(tapeTile.modelData.id)
                                        const seen = ({})
                                        let n = 0
                                        for (let i = 0; i < rows.length
                                                 && n < 8; i++) {
                                            const t = rows[i]
                                            if (!t.album || !t.filepath)
                                                continue
                                            const key = LibraryService.artKey(
                                                t.artist, t.album)
                                            if (seen[key])
                                                continue
                                            seen[key] = true
                                            n++
                                            LibraryService.requestArt(
                                                t.artist, t.album, t.filepath)
                                        }
                                    }
                                    Component.onCompleted: requestArts()
                                    onModelDataChanged: requestArts()

                                    // Cover art of the mixtape's distinct
                                    // albums (the void read keeps this
                                    // live as art caches fill in).
                                    readonly property var artUrls: {
                                        void LibraryService.artPaths
                                        const rows = LibraryService
                                            .playlistTracks(tapeTile.modelData.id)
                                        const seen = ({})
                                        const urls = []
                                        for (let i = 0; i < rows.length
                                                 && urls.length < 8; i++) {
                                            const t = rows[i]
                                            const key = LibraryService.artKey(
                                                t.artist, t.album)
                                            if (seen[key])
                                                continue
                                            seen[key] = true
                                            const u = LibraryService.artPaths[key] ?? ""
                                            if (u !== "")
                                                urls.push(u)
                                        }
                                        return urls
                                    }
                                    // Theme colors: dominant colors of
                                    // those covers, sorted by hue — the
                                    // median leads the gradient, a
                                    // quarter-offset partner closes it.
                                    readonly property var accents: {
                                        if (tapeTile.artUrls.length === 0)
                                            return []
                                        const cols = tapeTile.artUrls.map(
                                            u => LibraryService.dominantColor(u))
                                        cols.sort((a, b) => a.hslHue - b.hslHue)
                                        return [cols[Math.floor(cols.length / 2)],
                                                cols[Math.floor(cols.length / 4)]]
                                    }
                                    // The label wears one of the covers,
                                    // re-rolled every visit to the list.
                                    readonly property url labelArt: {
                                        void LibraryService.collectionArtRevision
                                        const custom = String(LibraryService.collectionArt("mixtape", String(modelData.id)))
                                        if (custom !== "") return custom
                                        void root.tapeShuffleTick
                                        return tapeTile.artUrls.length === 0 ? ""
                                            : tapeTile.artUrls[Math.floor(
                                                Math.random()
                                                * tapeTile.artUrls.length)]
                                    }

                                    CassetteTile {
                                        id: cassette
                                        objectName: "mixtapeCassette" + tapeTile.modelData.id
                                        width: tapeTile.width
                                        name: tapeTile.modelData.name
                                        accents: tapeTile.accents
                                        labelArt: tapeTile.labelArt
                                        playing: root.playingPlaylistId
                                                     === tapeTile.modelData.id
                                                 && PlayerService.playing
                                        rotation: tapeTile.tilt
                                        scale: tapeTile.hovering ? 1.04 : 1
                                        Behavior on scale {
                                            NumberAnimation { duration: Theme.motionFast }
                                        }
                                    }

                                    // The tape plays itself; right-click
                                    // for the menu; drag to the zune or
                                    // the tray.
                                    MouseArea {
                                        id: tapeArea
                                        objectName: "mixtapeArea" + tapeTile.modelData.id
                                        anchors.fill: cassette
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: mouse => {
                                            if (mouse.button === Qt.RightButton) {
                                                libPlMenu.target = tapeTile.modelData
                                                libPlMenu.popup()
                                            } else {
                                                root.playLibraryPlaylist(
                                                    tapeTile.modelData.id)
                                            }
                                        }
                                        readonly property bool dragArmed:
                                            DeviceService.connected
                                        drag.target: dragArmed ? tapeDragGhost : null
                                        preventStealing: dragArmed
                                        onPressed: mouse =>
                                            tapeDragGhost.place(tapeArea, mouse)
                                        onReleased: tapeDragGhost.drop()
                                    }

                                    // Meta + hover verbs under the shell
                                    // (＋ sync rightmost, house rule)
                                    RowLayout {
                                        anchors.left: cassette.left
                                        anchors.right: cassette.right
                                        anchors.top: cassette.bottom
                                        anchors.topMargin: 4
                                        spacing: Theme.spaceSm

                                        Text {
                                            text: tapeTile.modelData.count + " tracks"
                                            font.pixelSize: 10
                                            color: cassette.playing
                                                ? Theme.activePink : Theme.textDim
                                        }
                                        Item { Layout.fillWidth: true }
                                        Text {
                                            visible: tapeTile.hovering
                                            text: "✎"
                                            font.pixelSize: 12
                                            color: tapeEditArea.containsMouse
                                                ? Theme.textPrimary : Theme.textSecondary
                                            MouseArea {
                                                id: tapeEditArea
                                                anchors.fill: parent
                                                anchors.margins: -5
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: TrayState.openExisting(
                                                    tapeTile.modelData.id,
                                                    tapeTile.modelData.name)
                                            }
                                        }
                                        Text {
                                            visible: tapeTile.hovering
                                                     && DeviceService.connected
                                            text: "＋"
                                            font.pixelSize: 13
                                            color: tapeSyncArea.containsMouse
                                                ? Theme.activePink : Theme.pink
                                            MouseArea {
                                                id: tapeSyncArea
                                                anchors.fill: parent
                                                anchors.margins: -5
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.syncPlaylistToDevice(
                                                    tapeTile.modelData.id,
                                                    tapeTile.modelData.name)
                                            }
                                        }
                                    }

                                    DragGhost {
                                        id: tapeDragGhost
                                        area: tapeArea
                                        dragKind: "playlist"
                                        dragName: tapeTile.modelData.name
                                        dragTracks: () => LibraryService
                                            .playlistTracks(tapeTile.modelData.id)
                                            .map(t => ({
                                                filepath: t.filepath, title: t.title,
                                                artist: t.artist, albumartist: t.albumartist || t.artist,
                                                album: t.album, genre: "",
                                                trackNumber: t.trackNumber || 0,
                                                discNumber: t.discNumber || 0, year: t.year || 0,
                                                durationMs: t.durationMs || 0,
                                                libraryId: t.id
                                            }))

                                        // mini cassette rides the cursor
                                        Rectangle {
                                            width: 76
                                            height: 48
                                            radius: 5
                                            color: "#1d1d1d"
                                            border.width: 1
                                            border.color: Qt.alpha(Theme.orange, 0.75)
                                            Rectangle {
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.top: parent.top
                                                anchors.margins: 5
                                                height: 20
                                                radius: 2
                                                color: cassette.labelA
                                                ArtworkImage {
                                                    anchors.fill: parent
                                                    source: cassette.labelArt
                                                    fillMode: Image.PreserveAspectCrop
                                                    asynchronous: true
                                                }
                                                Text {
                                                    anchors.centerIn: parent
                                                    width: parent.width - 6
                                                    horizontalAlignment: Text.AlignHCenter
                                                    text: tapeTile.modelData.name
                                                    font.family: Theme.displayFamily
                                                    font.pixelSize: 9
                                                    color: cassette.ink
                                                    elide: Text.ElideRight
                                                }
                                            }
                                            Row {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: 6
                                                spacing: 18
                                                Repeater {
                                                    model: 2
                                                    Rectangle {
                                                        width: 11; height: 11
                                                        radius: 5.5
                                                        color: "#0d0d0d"
                                                        border.width: 1.5
                                                        border.color: "#333333"
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: LibraryService.playlists.length === 0
                            spacing: Theme.spaceSm
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "no mixtapes yet"
                                font.pixelSize: 16
                                font.weight: Font.Light
                                color: Theme.textDim
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "hit ＋ new mixtape, then add from anywhere in your music"
                                font.pixelSize: 12
                                color: Theme.textGhost
                            }
                        }
                    }
                }
            }

            // Drill-down details
            Loader {
                anchors.fill: parent
                active: root.drill !== null && root.drill.kind === "artist"
                sourceComponent: ArtistDetail {
                    artistName: root.drill ? root.drill.name : ""
                    tracks: root.drill ? root.tracksByArtist(root.drill.name, root.searchText) : []
                    localArt: !root.deviceSource
                    deviceSource: root.deviceSource
                    fullTracks: root.drill ? root.tracksByArtist(root.drill.name, "") : []
                    albumTracksResolver: album => root.tracksInAlbum(album, "")
                    onContextRequested: root.openArtistMenu(artistName)
                    onAlbumContextRequested: album => root.openAlbumMenu(album)
                    onAlbumSelected: album => root.openAlbum(album)
                    onAlbumPlayRequested: album =>
                        root.playFromList(root.tracksInAlbum(album, ""), 0)
                    onAlbumAddRequested: album => root.deviceSource
                        ? root.saveAlbumToLibrary(album)
                        : root.queueAlbum(album)
                    onAlbumPickRequested: (album, anchor) =>
                        root.openAlbumPicker(album, anchor)
                    onTrackEditRequested: track => editSheet.openFor(track)
                }
            }

            Loader {
                anchors.fill: parent
                active: root.drill !== null && root.drill.kind === "genre"
                sourceComponent: GenreDetail {
                    genreName: root.drill ? root.drill.name : ""
                    customArt: {
                        void LibraryService.collectionArtRevision
                        return root.deviceSource || !root.drill ? ""
                            : LibraryService.collectionArt("genre", root.drill.name)
                    }
                    onContextRequested: root.openGenreMenu(genreName)
                    tracks: root.drill
                        ? root.tracksByGenre(root.drill.name, root.searchText) : []
                    artUrls: {
                        void LibraryService.artPaths
                        void DeviceService.artPaths
                        return root.drill === null ? []
                            : root.genreAlbumRefs(root.drill.name).map(e =>
                                root.deviceSource
                                    ? (DeviceService.artPaths[String(e.itemId)] ?? "")
                                    : (LibraryService.artPaths[
                                          LibraryService.artKey(e.artist, e.album)] ?? ""))
                                .filter(u => u !== "")
                    }
                    localArt: !root.deviceSource
                    deviceSource: root.deviceSource
                    fullTracks: root.drill ? root.tracksByGenre(root.drill.name, "") : []
                    albumTracksResolver: album => root.tracksInAlbum(album, "")
                    onAlbumContextRequested: album => root.openAlbumMenu(album)
                    onAlbumSelected: album => root.openAlbum(album)
                    onArtistSelected: artist => {
                        root.popDrill()
                        root.pushDrill({ kind: "artist", name: artist })
                    }
                    onAlbumPlayRequested: album =>
                        root.playFromList(root.tracksInAlbum(album, ""), 0)
                    onAlbumAddRequested: album => root.deviceSource
                        ? root.saveAlbumToLibrary(album)
                        : root.queueAlbum(album)
                    onAlbumPickRequested: (album, anchor) =>
                        root.openAlbumPicker(album, anchor)
                    onTrackEditRequested: track => editSheet.openFor(track)
                }
            }

            Loader {
                anchors.fill: parent
                active: root.drill !== null && root.drill.kind === "album"
                sourceComponent: AlbumDetail {
                    albumName: root.drill ? root.drill.name : ""
                    albumArtist: root.drill ? root.drill.artist : ""
                    tracks: root.drill ? root.tracksInAlbum(root.drill, root.searchText) : []
                    fullTracks: root.drill ? root.tracksInAlbum(root.drill, "") : []
                    localArt: !root.deviceSource
                    onContextRequested: root.openAlbumMenu(root.drill)
                    onArtistSelected: artist => {
                        root.popDrill()
                        root.pushDrill({ kind: "artist", name: artist })
                    }
                    onTrackEditRequested: track => editSheet.openFor(track)
                }
            }
        }
    }

    // While the playlist tray is building, every library ＋ feeds the
    // tray instead of the sync queue (UX-2 contract).
    readonly property bool trayFeeds: !deviceSource && TrayState.building

    // ── UX-1: album quick-add helpers + the ▾ track picker ──
    function queueAlbum(album) {
        const tracks = root.tracksInAlbum(album, "")
        if (root.trayFeeds) {
            TrayState.addTracks(tracks.map(t => root.syncItemOf(t)))
            return
        }
        queueForDevice(tracks)
    }
    function queueForDevice(tracks) {
        if (!DeviceService.connected) return
        const result = SyncEngine.addTracks(tracks.map(t => root.syncItemOf(t)))
        toastHost.show(result.error || ("queued " + result.added + " tracks"))
    }
    function saveAlbumToLibrary(album) {
        if (!DeviceService.connected) return
        const tracks = root.tracksInAlbum(album, "")
        for (const t of tracks)
            DeviceService.saveTrackToLibrary(t.itemId, t.title,
                                             t.artist, t.album)
        toastHost.show("saving " + tracks.length + " tracks…")
    }
    property var _pickerTracks: []
    function openAlbumPicker(album, anchor) {
        if (!DeviceService.connected && !root.trayFeeds) return
        _pickerTracks = root.tracksInAlbum(album, "")
        const rows = _pickerTracks.map((t, i) => ({
            id: String(i),
            label: t.title,
            sublabel: t.artist
        }))
        if (anchor)
            trackPicker.openAt(anchor, album.name, rows)
        else
            trackPicker.openFor(album.name, rows)
    }
    // UX-2 Edit Info — one sheet shared by every library track list
    EditTrackSheet { id: editSheet }

    QuickPicker {
        id: trackPicker
        actionWord: root.deviceSource ? "save" : root.trayFeeds ? "add" : "queue"
        onPicked: ids => {
            if (!DeviceService.connected && !root.trayFeeds) return
            const chosen = ids.map(i => root._pickerTracks[Number(i)])
                              .filter(t => t !== undefined)
            if (root.deviceSource) {
                for (const t of chosen)
                    DeviceService.saveTrackToLibrary(t.itemId, t.title,
                                                     t.artist, t.album)
                toastHost.show("saving " + chosen.length + " tracks…")
            } else if (root.trayFeeds) {
                TrayState.addTracks(chosen.map(t => root.syncItemOf(t)))
            } else {
                root.queueForDevice(chosen)
            }
        }
    }

    // ── Library playlists (UX-2 rows → P4 mixtapes) ──
    // Which mixtape is on the player (drives the turning reels).
    // Cleared when the queue CONTENT is replaced by anything else —
    // queueChanged also fires on plain track advances (shared NOTIFY
    // with queueIndex), so compare content, don't just clear.
    property double playingPlaylistId: -1
    property int _plQueueLen: 0
    property string _plFirstPath: ""
    // Bumped each time the mixtape wall is shown — re-rolls which
    // cover each tape's label wears.
    property int tapeShuffleTick: 0
    Connections {
        target: PlayerService
        function onQueueChanged() {
            if (root.playingPlaylistId < 0)
                return
            const q = PlayerService.queue
            if (q.length !== root._plQueueLen
                || (q.length > 0 && (q[0].filepath || "") !== root._plFirstPath))
                root.playingPlaylistId = -1
        }
    }
    function playLibraryPlaylist(id) {
        const rows = LibraryService.playlistTracks(id)
        if (rows.length === 0)
            return
        PlayerService.setQueue(rows.map(t => ({
            filepath: t.filepath, title: t.title,
            artist: t.artist, album: t.album, libraryId: t.id,
            discNumber: t.discNumber || 0, year: t.year || 0
        })), 0)
        root._plQueueLen = rows.length
        root._plFirstPath = rows[0].filepath || ""
        root.playingPlaylistId = id
    }
    // Ride-along sync: queues the missing member tracks AND the
    // playlist forge itself (engine resolves + forges after the sends).
    function syncPlaylistToDevice(id, name) {
        if (!DeviceService.connected) return
        const rows = LibraryService.playlistTracks(id)
        if (rows.length === 0) {
            toastHost.show("'" + name + "' is empty — nothing to sync")
            return
        }
        const res = SyncEngine.addPlaylist(name, rows.map(t => ({
            filepath: t.filepath, title: t.title, artist: t.artist,
            albumartist: t.albumartist || t.artist, album: t.album, genre: "",
            trackNumber: t.trackNumber || 0,
            discNumber: t.discNumber || 0, year: t.year || 0,
            durationMs: t.durationMs || 0, libraryId: t.id
        })))
        toastHost.show(res.playlistQueued
            ? "queued '" + name + "' + " + res.added + " tracks"
            : "'" + name + "' is already queued")
    }
    ZuneMenu {
        id: libPlMenu
        objectName: "mixtapeContextMenu"
        property var target: null
        ZuneMenuItem {
            text: "Edit in Builder"
            onTriggered: TrayState.openExisting(libPlMenu.target.id,
                                                libPlMenu.target.name)
        }
        ZuneMenuItem {
            text: "Customize…"
            onTriggered: root.customizeMixtape(libPlMenu.target.id)
        }
        ZuneMenuItem {
            text: "Play"
            onTriggered: root.playLibraryPlaylist(libPlMenu.target.id)
        }
        ZuneMenuItem {
            text: "Sync to Zune"
            visible: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: root.syncPlaylistToDevice(libPlMenu.target.id,
                                                   libPlMenu.target.name)
        }
        ZuneMenuItem {
            text: "Delete Playlist"
            onTriggered: {
                LibraryService.deletePlaylist(libPlMenu.target.id)
                toastHost.show("deleted '" + libPlMenu.target.name + "'")
            }
        }
    }

    // ── Album context menu (Phase 8 parity: right-click everywhere).
    //    Library albums queue; device albums download or delete. ──
    function openAlbumMenu(album) {
        albumCtxMenu.albumRef = { name: album.name, artist: album.artist }
        albumCtxMenu.popup()
    }
    function openArtistMenu(artist) {
        artistCtxMenu.artistName = artist
        artistCtxMenu.popup()
    }
    function openGenreMenu(name) {
        if (root.deviceSource) return
        genreCtxMenu.genreName = name
        genreCtxMenu.popup()
    }
    function customizeGenre(name) {
        if (root.deviceSource) return
        const members = root.tracksByGenre(name, "")
        if (!members.length) return
        const art = root.genreAlbumRefs(name).map(e =>
            LibraryService.artPaths[LibraryService.artKey(e.artist, e.album)] || "")
            .filter(u => u !== "")
        musicCustomize.openFor({kind: "genre", name: name, title: name,
            count: members.length, artUrls: art,
            poster: LibraryService.collectionArt("genre", name)})
    }
    function customizeMixtape(id) {
        if (root.deviceSource) return
        const playlist = LibraryService.playlists.find(p => p.id === id)
        if (!playlist) return
        musicCustomize.openFor({kind: "mixtape", id: id,
            name: playlist.name, title: playlist.name, count: playlist.count,
            artUrls: root.mixtapeArtUrls(id),
            poster: LibraryService.collectionArt("mixtape", String(id))})
    }
    function mixtapeArtUrls(id) {
        const urls = []
        const seen = ({})
        for (const track of LibraryService.playlistTracks(id)) {
            const key = LibraryService.artKey(MusicIdentity.owner(track), track.album || "")
            const url = LibraryService.artPaths[key] || ""
            if (url && !seen[url]) {
                seen[url] = true
                urls.push(url)
            }
            if (urls.length === 8) break
        }
        return urls
    }
    ZuneMenu {
        id: genreCtxMenu
        objectName: "genreContextMenu"
        property string genreName: ""
        ZuneMenuItem {
            text: "Customize…"
            onTriggered: root.customizeGenre(genreCtxMenu.genreName)
        }
    }
    property double customizeAnchorId: -1
    function customizeAlbum(album) {
        // Resolve the current group at action time, including updated metadata.
        const first = root.tracksInAlbum(album, "")[0]
        if (!first) return
        customizeAnchorId = first.libraryId
        musicCustomize.openFor({ kind: "album", title: first.album || album.name,
            album: first.album || "", artist: MusicIdentity.owner(first),
            year: first.year || "", genre: first.genre || "",
            poster: LibraryService.artPaths[LibraryService.artKey(
                MusicIdentity.owner(first), first.album || "")] || "" })
    }
    ZuneMenu {
        id: albumCtxMenu
        objectName: "albumContextMenu"
        property var albumRef: ({ name: "", artist: "" })
        ZuneMenuItem {
            text: "Queue Album for Device"
            visible: !root.deviceSource
            enabled: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: root.queueForDevice(root.tracksInAlbum(albumCtxMenu.albumRef, ""))
        }
        ZuneMenuItem {
            text: "Customize…"
            visible: !root.deviceSource
            height: visible ? implicitHeight : 0
            onTriggered: root.customizeAlbum(albumCtxMenu.albumRef)
        }
        ZuneMenuItem {
            text: "Save Album to Library"
            visible: root.deviceSource
            enabled: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: root.saveAlbumToLibrary(albumCtxMenu.albumRef)
        }
        ZuneMenuItem {
            text: "Delete Album from Zune"
            visible: root.deviceSource
            enabled: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (!DeviceService.connected) return
                const tracks = root.tracksInAlbum(albumCtxMenu.albumRef, "")
                DeviceService.purgeItems(tracks.map(t => t.itemId))
                toastHost.show("deleting " + tracks.length + " tracks…")
            }
        }
    }
    // Artist context menu — mirrors the album menu but over every track
    // by the artist. Delete/Save had no artist-level entry before, so a
    // right-click on an artist did nothing.
    ZuneMenu {
        id: artistCtxMenu
        objectName: "artistContextMenu"
        property string artistName: ""
        ZuneMenuItem {
            text: "Queue Artist for Device"
            visible: !root.deviceSource
            enabled: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: root.queueForDevice(root.tracksByArtist(artistCtxMenu.artistName, ""))
        }
        ZuneMenuItem {
            text: "Customize…"
            visible: !root.deviceSource
            height: visible ? implicitHeight : 0
            onTriggered: {
                musicCustomize.openFor({
                    kind: "artist",
                    name: artistCtxMenu.artistName,
                    title: artistCtxMenu.artistName,
                    poster: LibraryService.artistImages[
                        artistCtxMenu.artistName.toLowerCase()] ?? ""
                })
            }
        }
        ZuneMenuItem {
            text: "Save Artist to Library"
            visible: root.deviceSource
            enabled: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (!DeviceService.connected) return
                const tracks = root.tracksByArtist(artistCtxMenu.artistName, "")
                for (const t of tracks)
                    DeviceService.saveTrackToLibrary(t.itemId, t.title,
                                                     t.artist, t.album)
                toastHost.show("saving " + tracks.length + " tracks…")
            }
        }
        ZuneMenuItem {
            text: "Delete Artist from Zune"
            visible: root.deviceSource
            enabled: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (!DeviceService.connected) return
                const tracks = root.tracksByArtist(artistCtxMenu.artistName, "")
                DeviceService.purgeItems(tracks.map(t => t.itemId))
                toastHost.show("deleting " + tracks.length + " tracks…")
            }
        }
    }
    // Customize… sheet for albums and artists (W13 parity with video).
    // Artwork = custom cover/photo from a file; Identity = manual fields
    // rewritten across the group's tracks. Online DB match is the next slice.
    CustomizeSheet {
        id: musicCustomize
        onSaved: message => {
            root.rebuildSnapshot()
            if (identityMode === "manual") {
                const before = current
                const title = identityFields.title.trim()
                root.navStack = root.navStack.map(d => {
                    if (kind === "album" && d.kind === "album"
                        && MusicIdentity.key(d) === MusicIdentity.key({
                            name: before.album || before.title,
                            artist: before.artist || "Unknown Artist" }))
                        return Object.assign({ kind: "album" }, MusicIdentity.fromTrack(
                            root.allTracks.find(t => t.libraryId === root.customizeAnchorId)
                            || {album: title, albumartist: identityFields.albumartist.trim()}))
                    if (kind === "artist") {
                        const oldName = String(before.name || before.title).toLowerCase()
                        if (d.kind === "artist" && d.name.toLowerCase() === oldName)
                            return { kind: "artist", name: title }
                        if (d.kind === "album" && d.artist.toLowerCase() === oldName)
                            return { kind: "album", name: d.name, artist: title }
                    }
                    return d
                })
            }
            toastHost.show(message)
        }
    }

    // Album → Add to Playlist ▸ (library side only — attached, not
    // declared inline: submenus can't hide via visible/height).
    AddToPlaylistMenu {
        id: albumPlSubmenu
        onPicked: (playlistId, name) => {
            const ids = root.tracksInAlbum(albumCtxMenu.albumRef, "")
                .map(t => t.libraryId).filter(i => i !== undefined && i >= 0)
            LibraryService.addToPlaylist(playlistId, ids)
            toastHost.show("added " + ids.length + " tracks to '" + name + "'")
        }
        onNewRequested: {
            TrayState.openNew()
            TrayState.addTracks(root.tracksInAlbum(albumCtxMenu.albumRef, "")
                .map(t => root.syncItemOf(t)))
        }
    }

    // Download-failure surfacing (successes stay quiet — one toast at
    // kickoff is enough; a stream of per-file toasts is noise).
    Connections {
        target: DeviceService
        function onTrackSaved(filename, ok) {
            if (!ok)
                toastHost.show("failed to save " + filename)
        }
    }
}
