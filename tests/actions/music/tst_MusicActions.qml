import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Zuuned
import "../../../qml/MusicIdentity.js" as MusicIdentity

TestCase {
    id: test
    name: "MusicActions"
    when: windowShown
    width: 1000
    height: 800
    visible: true
    property var fixtures: [
        { title: "Alpha", artist: "First Artist", albumartist: "First Artist", album: "Greatest Hits", filepath: "/fixture/first1.flac", libraryId: 1, itemId: 101, trackNumber: 1, durationMs: 1000, genre: "Rock", year: 1990 },
        { title: "Bravo", artist: "Guest Singer", albumartist: "First Artist", album: "Greatest Hits", filepath: "/fixture/first2.flac", libraryId: 2, itemId: 102, trackNumber: 2, durationMs: 1000, genre: "Rock", year: 1990 },
        { title: "Beta", artist: "Second Artist", albumartist: "Second Artist", album: "Greatest Hits", filepath: "/fixture/second.flac", libraryId: 3, itemId: 201, trackNumber: 1, durationMs: 1000, genre: "Rock", year: 2001 }
    ]
    QtObject { id: toastHost; function show(message) {} }
    Component { id: pageComponent; MusicPage { width: test.width; height: test.height; source: LibraryService } }
    Component { id: rowComponent; TrackListRow { width: 900 } }
    Component { id: badgeComponent; OnDeviceBadge {} }
    SignalSpy { id: actionSpy }

    function init() {
        DeviceService.connected = false
        DeviceService.rows = fixtures.map(t => Object.assign({}, t, {albumartist: ""}))
        DeviceService.saved = []
        DeviceService.deleted = []
        LibraryService.rows = fixtures
        LibraryService.localTrackFound = false
        LibraryService.localTrackIdentityRevision++
        LibraryService.albumsList = MusicIdentity.groupAlbums(fixtures)
        LibraryService.artistsList = [{name: "First Artist", count: 2}, {name: "Second Artist", count: 1}]
        LibraryService.genresList = [{name: "Rock", count: 3}]
        LibraryService.customization = ({})
        LibraryService.playlistAdds = []
        SyncEngine.added = []
        TrayState.building = false
        TrayState.added = []
        PlayerService.queue = []
        PlayerService.appended = []
        PlayerService.nextItems = []
        actionSpy.target = null
        actionSpy.clear()
    }
    function menuItem(menu, text) {
        for (let i = 0; i < menu.count; i++) {
            const entry = menu.itemAt(i)
            if (entry && entry.text === text) return entry
        }
        fail("Missing menu item: " + text)
    }
    function test_deviceSongLocationBadgeReactsInRowsWithoutArtwork() {
        const row = createTemporaryObject(rowComponent, test, {
            track: fixtures[0], saveable: true, showArt: false
        })
        verify(row)
        const badge = findChild(row, "trackComputerBadge")
        verify(badge)
        verify(!badge.visible)
        LibraryService.localTrackFound = true
        LibraryService.localTrackIdentityRevision++
        tryCompare(badge, "visible", true)
        // Local library edits/imports remain reactive without a device.
        verify(!DeviceService.connected)
        LibraryService.localTrackFound = false
        LibraryService.localTrackIdentityRevision++
        tryCompare(badge, "visible", false)
    }
    function test_deviceBadgeUsesTheZuunedAppMark() {
        const badge = createTemporaryObject(badgeComponent, test)
        verify(badge)
        const mark = findChild(badge, "zuunedLocationMark")
        verify(mark.source.toString().endsWith("/images/zuuned.png"))
        tryCompare(mark, "status", Image.Ready)
    }
    function test_albumIdentitySeparatesOwnersAndKeepsCompilationTogether() {
        const albums = MusicIdentity.groupAlbums(fixtures)
        compare(albums.length, 2)
        const first = albums.filter(a => a.artArtist === "First Artist")[0]
        compare(first.count, 2)
        verify(MusicIdentity.matches(fixtures[1], MusicIdentity.fromAlbum(first)))
        verify(!MusicIdentity.matches(fixtures[2], MusicIdentity.fromAlbum(first)))
        verify(MusicIdentity.matches(fixtures[0], {name: "greatest hits", artist: "FIRST ARTIST"}))
        compare(MusicIdentity.fromTrack({artist: "", albumartist: "", album: ""}).artist, "Unknown Artist")
    }
    function test_albumNavigationQueuePickerCustomizeAndPlaylistAreScoped() {
        const page = createTemporaryObject(pageComponent, test)
        verify(page)
        const album = MusicIdentity.fromTrack(fixtures[0])
        page.openAlbum(album)
        compare(page.drill.artist, "First Artist")
        compare(page.tracksInAlbum(page.drill, "").length, 2)
        DeviceService.connected = true
        page.queueAlbum(album)
        compare(SyncEngine.added.length, 2)
        compare(SyncEngine.added[1].libraryId, 2)
        page.openAlbumPicker(album, null)
        compare(page._pickerTracks.length, 2)
        page.customizeAlbum(album)
        compare(LibraryService.customization.artist, "First Artist")
        compare(LibraryService.customization.year, 1990)
        const sheet = findChild(page, "customizeSheet")
        sheet.close()
        DeviceService.connected = false
        SyncEngine.added = []
        page.queueAlbum(album)
        compare(SyncEngine.added.length, 0)
        TrayState.building = true
        page.queueAlbum(album)
        compare(TrayState.added.length, 2)
        compare(SyncEngine.added.length, 0)
    }
    function test_multidiscAlbumPlaybackAndTransfersKeepDiscOrder() {
        const scrambled = [
            { title: "A disc2", discNumber: 2, trackNumber: 1, libraryId: 24 },
            { title: "Z disc1 second", discNumber: 1, trackNumber: 2, libraryId: 22 },
            { title: "Z disc1 first", discNumber: 1, trackNumber: 1, libraryId: 21 }
        ].map(t => Object.assign({}, fixtures[0], t, {filepath: "/fixture/disc" + t.libraryId + ".flac"}))
        LibraryService.rows = scrambled
        LibraryService.albumsList = MusicIdentity.groupAlbums(scrambled)
        const page = createTemporaryObject(pageComponent, test)
        verify(page)
        const album = MusicIdentity.fromTrack(scrambled[0])
        const ordered = page.tracksInAlbum(album, "")
        compare(ordered.map(t => t.libraryId), [21, 22, 24])
        page.playFromList(ordered, 0)
        compare(PlayerService.queue.map(t => t.libraryId), [21, 22, 24])
        compare(PlayerService.queue[2].discNumber, 2)
        compare(PlayerService.queue[2].year, 1990)
        DeviceService.connected = true
        page.queueAlbum(album)
        compare(SyncEngine.added.map(t => t.libraryId), [21, 22, 24])
        compare(SyncEngine.added[2].discNumber, 2)
        compare(SyncEngine.added[2].year, 1990)
        page.openAlbumPicker(album, null)
        compare(page._pickerTracks.map(t => t.libraryId), [21, 22, 24])
    }
    function test_artistDrillGroupsAlbumsAndKeepsTrackOrder() {
        const scrambled = [
            { title: "Five", album: "First Album", trackNumber: 5, libraryId: 15 },
            { title: "One", album: "First Album", trackNumber: 1, libraryId: 11 },
            { title: "Second", album: "Later Album", trackNumber: 2, libraryId: 22 },
            { title: "First", album: "Later Album", trackNumber: 1, libraryId: 21 }
        ].map(t => Object.assign({}, fixtures[0], t,
            {filepath: "/fixture/artist" + t.libraryId + ".flac"}))
        LibraryService.rows = scrambled
        const page = createTemporaryObject(pageComponent, test)
        verify(page)
        compare(page.tracksByArtist("First Artist", "").map(t => t.libraryId),
                [11, 15, 21, 22])
    }
    function test_deviceAlbumSaveAndDeleteNeverCrossSameTitle() {
        const page = createTemporaryObject(pageComponent, test, {source: DeviceService})
        const album = MusicIdentity.fromTrack(DeviceService.rows[0])
        compare(page.computeAlbums(DeviceService.rows).length, 3)
        DeviceService.connected = true
        page.saveAlbumToLibrary(album)
        compare(DeviceService.saved.length, 1)
        compare(DeviceService.saved[0], 101)
        page.openAlbumMenu(album)
        const menu = findChild(page, "albumContextMenu")
        menuItem(menu, "Delete Album from Zune").triggered()
        compare(DeviceService.deleted.length, 1)
        compare(DeviceService.deleted[0], 101)
        DeviceService.connected = false
        DeviceService.deleted = []
        menuItem(menu, "Delete Album from Zune").triggered()
        compare(DeviceService.deleted.length, 0)
        menu.close()
    }
    function test_alphabetPlaybackUsesDisplayedRows() {
        const page = createTemporaryObject(pageComponent, test)
        page.openPivot("Songs")
        page.letterFilter = "b"
        compare(page.displayedSongs.length, 2)
        const list = findChild(page, "songsList")
        tryVerify(() => list.itemAtIndex(1) !== null)
        const row = list.itemAtIndex(1)
        waitForRendering(row)
        mouseDoubleClickSequence(row, 100, 15)
        compare(PlayerService.queue.length, 2)
        compare(PlayerService.queue[PlayerService.queueIndex].title, "Bravo")
    }
    function test_individualDragPreservesDiscAndNumberProvenance() {
        DeviceService.connected = true
        const row = createTemporaryObject(rowComponent,test,{
            track:Object.assign({},fixtures[0],{discNumber:2,trackNumber:7,year:2003,trackNumberReliable:false}),
            syncable:true,localArt:true,displayTrackNumber:23})
        const area = findChild(row,"trackDragArea")
        const payload = area.drag.target.dragTracks[0]
        compare(payload.discNumber,2)
        compare(payload.trackNumber,7)
        compare(payload.trackNumberReliable,false)
        compare(payload.year,2003)
        compare(row.displayTrackNumber,23)
    }
    function test_disconnectCancelsActiveTrackDrag() {
        DeviceService.connected = true
        const row = createTemporaryObject(rowComponent, test, {
            track: fixtures[0], syncable: true, localArt: true })
        const area = findChild(row, "trackDragArea")
        const ghost = area.drag.target
        waitForRendering(row)
        mousePress(area, 100, 15)
        mouseMove(area, 130, 15, 20)
        mouseMove(area, 180, 15, 20)
        verify(area.drag.active)
        verify(ghost.Drag.active)
        DeviceService.connected = false
        verify(!ghost.Drag.active)
        verify(area.drag.target === null)
        mouseRelease(area, 180, 15)
    }
    function test_renamedAlbumDrillFollowsSavedTrackIdentity() {
        const page = createTemporaryObject(pageComponent, test)
        const album = MusicIdentity.fromTrack(fixtures[0])
        page.openAlbum(album)
        page.customizeAlbum(album)
        const sheet = findChild(page, "customizeSheet")
        sheet.setField("title", "New name")
        sheet.setField("albumartist", "")
        LibraryService.rows = fixtures.map(t => t.libraryId <= 2
            ? Object.assign({}, t, {album: "New name", albumartist: ""}) : t)
        sheet.saved("saved")
        compare(page.drill.name, "New name")
        compare(page.drill.artist, "First Artist")
        compare(page.tracksInAlbum(page.drill, "").length, 1)
        sheet.close()
    }
    function test_trackOfflineActionsAndDisconnectAtMenuTrigger() {
        const row = createTemporaryObject(rowComponent, test, {
            track: fixtures[0], syncable: true, localArt: true })
        const area = findChild(row, "trackDragArea")
        const sync = findChild(row, "trackSyncAction")
        const next = findChild(row, "trackPlayNextAction")
        const append = findChild(row, "trackNowPlayingAction")
        actionSpy.target = row
        actionSpy.signalName = "syncRequested"
        verify(!area.dragArmed)
        verify(!sync.enabled)
        next.triggered()
        append.triggered()
        compare(PlayerService.nextItems[0].filepath, fixtures[0].filepath)
        compare(PlayerService.appended[0].filepath, fixtures[0].filepath)
        DeviceService.connected = true
        verify(area.dragArmed)
        verify(sync.enabled)
        DeviceService.connected = false
        sync.triggered()
        compare(actionSpy.count, 0)
        TrayState.building = true
        verify(!area.dragArmed)
        verify(sync.enabled)
        sync.triggered()
        compare(actionSpy.count, 1)
    }
    function test_nestedAlbumsCarryIdentityAndDeviceDragPayload() {
        const page = createTemporaryObject(pageComponent, test)
        page.pushDrill({kind: "genre", name: "Rock"})
        tryVerify(() => findChild(page, "genreHeroArea") !== null)
        const area = findChild(page, "genreHeroArea")
        verify(area.drag.target === null)
        // Read the actual detail through its hero's ancestor chain.
        let detail = area.parent
        while (detail && typeof detail.albumDragTracks !== "function") detail = detail.parent
        verify(detail !== null)
        compare(detail.albums.length, 2)
        compare(detail.albumDragTracks(MusicIdentity.fromTrack(fixtures[0])).length, 2)
        DeviceService.connected = true
        verify(area.drag.target !== null)
        compare(area.drag.target.dragTracks().length, 3)
        DeviceService.connected = false
        TrayState.building = true
        verify(area.drag.target === null)
    }
    function test_genreAlbumDragIncludesMembersOutsideGenre() {
        LibraryService.rows = fixtures.map(t => t.libraryId === 2
            ? Object.assign({}, t, {genre: "Jazz"}) : t)
        const page = createTemporaryObject(pageComponent, test)
        page.pushDrill({kind: "genre", name: "Rock"})
        tryVerify(() => findChild(page, "genreHeroArea") !== null)
        let detail = findChild(page, "genreHeroArea").parent
        while (detail && typeof detail.albumDragTracks !== "function") detail = detail.parent
        compare(detail.tracks.length, 2)
        compare(detail.albumDragTracks(MusicIdentity.fromTrack(fixtures[0])).length, 2)
        page.openPivot("Genres")
        page.source = DeviceService
        DeviceService.connected = true
        page.pushDrill({kind: "genre", name: "Rock"})
        tryVerify(() => findChild(page, "genreHeroArea") !== null)
        detail = findChild(page, "genreHeroArea").parent
        while (detail && typeof detail.albumDragTracks !== "function") detail = detail.parent
        const albumArea = findChild(detail, "albumDragArea")
        verify(albumArea.dragArmed)
        const payload = albumArea.drag.target.dragTracks()
        verify(payload.length > 0)
        verify(payload[0].itemId > 0)
        compare(albumArea.drag.target.Drag.keys[0], "zuuned-device-tracks")
    }
    function test_albumAndArtistHeroesExposeContextAndWholeItemDrag() {
        const page = createTemporaryObject(pageComponent, test)
        page.openAlbum(MusicIdentity.fromTrack(fixtures[0]))
        tryVerify(() => findChild(page, "albumHeroArea") !== null)
        let area = findChild(page, "albumHeroArea")
        tryVerify(() => area.visible && area.width > 0)
        waitForRendering(area)
        mouseClick(area, 60, 60, Qt.RightButton)
        const menu = findChild(page, "albumContextMenu")
        tryCompare(menu, "visible", true)
        verify(menuItem(menu, "Customize…").enabled)
        verify(!menuItem(menu, "Queue Album for Device").enabled)
        menu.close()
        DeviceService.connected = true
        verify(area.drag.target !== null)
        compare(area.drag.target.dragTracks().length, 2)
        page.openPivot("Artists")
        page.pushDrill({kind: "artist", name: "First Artist"})
        tryVerify(() => findChild(page, "artistHeroArea") !== null)
        area = findChild(page, "artistHeroArea")
        tryVerify(() => area.visible && area.width > 0)
        waitForRendering(area)
        compare(area.drag.target.dragTracks().length, 2)
        mouseClick(area, 60, 60, Qt.RightButton)
        const artistMenu = findChild(page, "artistContextMenu")
        tryCompare(artistMenu, "visible", true)
        verify(menuItem(artistMenu, "Customize…").enabled)
        artistMenu.close()
    }
}
