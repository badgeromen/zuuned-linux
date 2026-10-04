import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Zuuned

TestCase {
    id: test
    name: "CollectionArt"
    when: windowShown
    width: 1100
    height: 850
    visible: true
    QtObject { id: toastHost; function show(message) {} }
    Component { id: musicPage; MusicPage { width: test.width; height: test.height; source: LibraryService } }
    Component { id: playlistBuilder; PlaylistTray { width: 320; height: 700 } }
    Component { id: artwork; CustomizeArtwork { width: 240; height: 240 } }

    function cover(color) {
        return Qt.url("data:image/svg+xml," + encodeURIComponent(
            '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><rect width="64" height="64" fill="' + color + '"/></svg>')).toString()
    }
    function init() {
        DeviceService.connected = false
        LibraryService.rows = [{libraryId: 1, id: 1, itemId: 101, title: "Song", artist: "Singer",
            albumartist: "Singer", album: "Album", genre: "Alternative", durationMs: 1000,
            filepath: "/fixture/song.flac", trackNumber: 1}]
        DeviceService.rows = LibraryService.rows
        LibraryService.artistsList = [{name: "Singer", count: 1}]
        LibraryService.albumsList = [{name: "Album", artist: "Singer", count: 1}]
        LibraryService.genresList = [{name: "Alternative", count: 1}]
        DeviceService.genresList = LibraryService.genresList
        LibraryService.playlists = [{id: 7, name: "My tape", count: 1}]
        LibraryService.playlistRows = ({"7": LibraryService.rows})
        LibraryService.artPaths = ({"Singer\nAlbum": cover("#207080")})
        LibraryService.collectionArts = ({})
        LibraryService.collectionArtRevision++
        LibraryService.customization = ({})
        LibraryService.applications = []
        TrayState.playlistId = -1
        TrayState.name = ""
        TrayState.tracks = []
        TrayState.building = false
        PlayerService.queue = []
    }
    function cleanup() {
        TrayState.building = false
        LibraryService.artPaths = ({})
        LibraryService.collectionArts = ({})
        LibraryService.collectionArtRevision++
        LibraryService.playlists = []
    }
    function menuItem(menu, text) {
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.text === text) return item
        }
        fail("Missing menu entry " + text)
    }
    function chooseCustomize(page, menuName) {
        const menu = findChild(page, menuName)
        verify(menu)
        tryCompare(menu, "visible", true)
        const item = menuItem(menu, "Customize…")
        mouseClick(item, item.width / 2, item.height / 2)
        const sheet = findChild(page, "customizeSheet")
        tryCompare(sheet, "visible", true)
        return sheet
    }
    function test_genreCardContextOpensArtworkDraftOfflineAndCancelKeepsMosaic() {
        const page = createTemporaryObject(musicPage, test)
        page.openPivot("Genres")
        tryVerify(() => findChild(page, "genreTileArea") !== null)
        const area = findChild(page, "genreTileArea")
        mouseClick(area, area.width / 2, area.height / 2, Qt.RightButton)
        const sheet = chooseCustomize(page, "genreContextMenu")
        compare(sheet.kind, "genre")
        verify(sheet.artworkOnly)
        verify(!findChild(sheet, "customizePivots").visible)
        compare(sheet.artSources.length, 1)
        compare(sheet.artSources[0].value, "file")
        compare(sheet.current.name, "Alternative")
        compare(sheet.current.artUrls.length, 1)
        compare(PlayerService.queue.length, 0)
        sheet.stageArt(cover("#dd4488"))
        compare(LibraryService.collectionArt("genre", "Alternative"), "")
        mouseClick(findChild(sheet, "customizeCancel"))
        tryCompare(sheet, "visible", false)
        compare(LibraryService.applications.length, 0)
        verify(findChild(page, "genreTileMosaic").visible)
    }
    function test_genreOverrideUpdatesCardAndDrillThenResetIsOnlyStaged() {
        const page = createTemporaryObject(musicPage, test)
        page.openPivot("Genres")
        const picked = cover("#dd4488")
        LibraryService.collectionArts = ({"genre:Alternative": picked})
        LibraryService.collectionArtRevision++
        tryVerify(() => findChild(page, "genreTileCustomArt") !== null)
        const card = findChild(page, "genreTileCustomArt")
        tryCompare(card, "status", Image.Ready)
        compare(card.source.toString(), picked)
        verify(!findChild(page, "genreTileMosaic").visible)
        const area = findChild(page, "genreTileArea")
        mouseClick(area, area.width / 2, area.height / 2)
        tryVerify(() => findChild(page, "genreDetailCustomArt") !== null)
        const hero = findChild(page, "genreDetailCustomArt")
        tryCompare(hero, "status", Image.Ready)
        compare(hero.source.toString(), picked)
        const heroArea = findChild(page, "genreHeroArea")
        mouseClick(heroArea, heroArea.width / 2, heroArea.height / 2, Qt.RightButton)
        const sheet = chooseCustomize(page, "genreContextMenu")
        sheet.resetDraft()
        compare(sheet.identityMode, "keep")
        compare(LibraryService.collectionArt("genre", "Alternative"), picked)
        const preview = findChild(sheet, "customizeHero")
        verify(findChild(preview, "customizeCollectionMosaic").visible)
        sheet.applyDraft()
        compare(LibraryService.applications.length, 1)
        compare(LibraryService.applications[0].draft.resetArt, true)
        compare(LibraryService.applications[0].draft.identityMode, "keep")
        LibraryService.customizationFinished(sheet.applyRequest, true, "")
    }
    function test_mixtapeWallAndDraftKeepCassetteAndStablePlaylistId() {
        const page = createTemporaryObject(musicPage, test)
        page.openPivot("Playlists")
        tryVerify(() => findChild(page, "mixtapeCassette7") !== null)
        const cassette = findChild(page, "mixtapeCassette7")
        compare(cassette.labelArt.toString(), cover("#207080"))
        const picked = cover("#bb6622")
        LibraryService.collectionArts = ({"mixtape:7": picked})
        LibraryService.collectionArtRevision++
        tryCompare(cassette, "labelArt", Qt.url(picked))
        const area = findChild(page, "mixtapeArea7")
        mouseClick(area, area.width / 2, area.height / 2, Qt.RightButton)
        const sheet = chooseCustomize(page, "mixtapeContextMenu")
        compare(sheet.kind, "mixtape")
        verify(sheet.artworkOnly)
        verify(!findChild(sheet, "customizePivots").visible)
        compare(sheet.current.id, 7)
        compare(sheet.current.title, "My tape")
        const preview = findChild(sheet, "customizeHero")
        const tape = findChild(preview, "customizeCassette")
        verify(tape.visible)
        compare(tape.name, "My tape")
        compare(tape.labelArt.toString(), picked)
        verify(preview.width > preview.height)
        mouseClick(findChild(sheet, "customizeCancel"))
        compare(LibraryService.applications.length, 0)
        compare(PlayerService.queue.length, 0)
    }
    function test_savedBuilderCustomizesArtWithoutSavingItsPendingNameOrMembers() {
        TrayState.openExisting(7, "Unsaved name")
        const tray = createTemporaryObject(playlistBuilder, test)
        wait(Theme.motionSlow)
        const area = findChild(tray, "builderCollectionArtArea")
        mouseClick(area, area.width / 2, area.height / 2, Qt.RightButton)
        const menu = findChild(tray, "builderCollectionMenu")
        tryCompare(menu, "visible", true)
        const item = menuItem(menu, "Customize…")
        mouseClick(item, item.width / 2, item.height / 2)
        const sheet = findChild(tray, "builderCollectionCustomize")
        tryCompare(sheet, "visible", true)
        compare(sheet.current.id, 7)
        compare(sheet.current.title, "My tape")
        sheet.stageArt(cover("#883399"))
        mouseClick(findChild(sheet, "customizeCancel"))
        compare(TrayState.name, "Unsaved name")
        compare(TrayState.tracks.length, 1)
        compare(LibraryService.applications.length, 0)
        LibraryService.collectionArts = ({"mixtape:7": cover("#225599")})
        LibraryService.collectionArtRevision++
        tryCompare(findChild(tray, "builderCollectionCassette"), "labelArt", Qt.url(cover("#225599")))
    }
    function test_deviceGenreDoesNotBorrowLocalOverridesOrOfferLocalCustomize() {
        DeviceService.connected = true
        LibraryService.collectionArts = ({"genre:Alternative": cover("#dd4488")})
        LibraryService.collectionArtRevision++
        const page = createTemporaryObject(musicPage, test, {source: DeviceService})
        page.openPivot("Genres")
        tryVerify(() => findChild(page, "genreTileArea") !== null)
        const image = findChild(page, "genreTileCustomArt")
        compare(image.source.toString(), "")
        const area = findChild(page, "genreTileArea")
        mouseClick(area, area.width / 2, area.height / 2, Qt.RightButton)
        verify(!findChild(page, "genreContextMenu").visible)
        verify(!findChild(page, "customizeSheet").visible)
    }
    function test_emptyGenrePreviewUsesFlatSurfaceAndRestoresAutomaticMosaic() {
        const image = createTemporaryObject(artwork, test, {kind: "genre"})
        compare(image.source.toString(), "")
        verify(!findChild(image, "customizeCollectionMosaic").visible)
        image.artUrls = [cover("#207080"), cover("#bb6622")]
        verify(findChild(image, "customizeCollectionMosaic").visible)
        image.source = cover("#dd4488")
        tryCompare(findChild(image, "customizeArtworkImage"), "status", Image.Ready)
        verify(!findChild(image, "customizeCollectionMosaic").visible)
        image.source = ""
        verify(findChild(image, "customizeCollectionMosaic").visible)
    }
}
