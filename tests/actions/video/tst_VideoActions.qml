import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Item {
    id: host
    width: 1280; height: 900
    QtObject { id: toastHost; function show(message) {} }
    VideoBrowserModel { id: fixtureBrowser }
    Component { id: seriesComponent; VideoSeriesDetail { browser: fixtureBrowser; seriesKey: "show"; height: 850 } }
    Component { id: libraryComponent; LibraryVideosView { width: 1250; height: 850 } }
    Component { id: movieComponent; VideoMovieDetail { browser: fixtureBrowser; videoId: 10; width: 1250; height: 850 } }
    SignalSpy { id: playSpy; signalName: "playEpisodesRequested" }
    SignalSpy { id: queueSpy; signalName: "queueEpisodeRequested" }
    SignalSpy { id: saveSpy; signalName: "saveEpisodeRequested" }
    SignalSpy { id: editSpy; signalName: "customizeEpisodeRequested" }
    SignalSpy { id: movieMenuSpy; signalName: "menuRequested" }
    TestCase {
        id: test
        name: "VideoActions"
        when: windowShown
        function episode(id) {
            return { id: id, itemId: id, filename: "Show S01E0" + id + ".mkv",
                filepath: "/fixtures/episode" + id + ".mkv", series: "Show", season: 1,
                episode: id, episodeTitle: "Episode " + id, still: "", poster: "",
                posterPath: "", watched: false, durationMs: 1200000, filesize: 123,
                episodeRuntime: 20, episodeRating: 0, airDate: "", description: "",
                isOnDevice: false, isTV: true, category: "tv", title: "Show", year: "", genres: "" }
        }
        function movie() {
            return Object.assign(episode(10), {isTV: false, category: "movie", title: "Old title", series: "", versionIds: [10], versionCount: 1, versionLabel: "1080p"})
        }
        function init() {
            host.Window.window.width = 1280
            host.Window.window.height = 900
            DeviceService.connected = false
            LibraryService.scanning = false
            LibraryService.scanTotal = 0
            LibraryService.scanCurrent = 0
            LibraryService.scanStage = ""
            LibraryService.scanCurrentFile = ""
            DeviceService.deleted = []
            LibraryService.rows = [episode(1), episode(2), movie()]
            LibraryService.watched = []
            LibraryService.deleted = []
            LibraryService.customization = ({})
            LibraryService.unmatched = []
            LibraryService.unmatchedGroups = []
            SyncEngine.calls = []
            VideoPlayerService.calls = []
            fixtureBrowser.rowOverrides = ({})
            TrayState.started = 0
            for (const spy of [playSpy, queueSpy, saveSpy, editSpy, movieMenuSpy]) { spy.target = null; spy.clear() }
        }
        function visibleChild(item, name) {
            if (item.objectName === name && item.visible && item.width > 0 && item.height > 0) return item
            for (const child of item.children ?? []) {
                const match = visibleChild(child, name)
                if (match) return match
            }
            return null
        }
        function visibleTexts(item) {
            if (!item.visible) return []
            let labels = typeof item.text === "string" ? [item.text] : []
            for (const child of item.children ?? []) labels = labels.concat(visibleTexts(child))
            return labels
        }
        function makeSeries(width, device) {
            const page = createTemporaryObject(seriesComponent, host, {width: width, deviceMode: device,
                episodeSyncItemFn: ep => ({libraryId: ep.id, title: ep.episodeTitle, filepath: ep.filepath})})
            verify(page !== null)
            playSpy.target = page; queueSpy.target = page; saveSpy.target = page; editSpy.target = page
            wait(30)
            return page
        }
        function test_episodePointerParity_data() {
            return [{tag: "wide local", width: 1250, device: false},
                {tag: "narrow local", width: 650, device: false},
                {tag: "wide device", width: 1250, device: true},
                {tag: "narrow device", width: 650, device: true}]
        }
        function test_episodePointerParity(data) {
            DeviceService.connected = true
            const page = makeSeries(data.width, data.device)
            if (data.device) {
                const labels = visibleTexts(page)
                verify(!labels.some(t => t.indexOf("next up") >= 0 || t.indexOf("play season") >= 0 || t === "to zune" || t === "○"),
                    "Device layouts hide local playback, transfer-to-device and watched verbs")
            }
            const gesture = visibleChild(page, "episodeGesture")
            verify(gesture !== null, "Actual still's MouseArea exists")
            mouseClick(gesture, gesture.width / 2, gesture.height / 2)
            compare(data.device ? saveSpy.count : playSpy.count, 1)
            compare(data.device ? playSpy.count : saveSpy.count, 0)
            mouseClick(gesture, gesture.width / 2, gesture.height / 2, Qt.RightButton)
            const menu = findChild(page, "episodeMenu")
            tryCompare(menu, "opened", true)
            const edit = findChild(menu, "episodeCustomize")
            compare(edit.visible, !data.device)
            menu.close()
            wait(20)
            mousePress(gesture, gesture.width / 2, gesture.height / 2)
            mouseMove(gesture, gesture.width / 2 + 35, gesture.height / 2 + 25, 30)
            mouseMove(gesture, gesture.width / 2 + 70, gesture.height / 2 + 40, 30)
            verify(TrayState.started > 0, "Dragging starts on the still in each layout")
            mouseRelease(gesture, gesture.width / 2 + 70, gesture.height / 2 + 40)
            compare(LibraryService.watched.length, 0)
        }
        function test_deviceIdsCannotReachLocalWatchedOrPlayback() {
            DeviceService.connected = true
            const page = makeSeries(650, true)
            page.markWatched([episode(1)], true)
            page.playEpisode(episode(1)); page.playSeason(); page.playNextUp()
            page.customizeEpisode(episode(1))
            compare(LibraryService.watched.length, 0)
            compare(playSpy.count, 0); compare(editSpy.count, 0)
            page.deleteEpisode(episode(1))
            compare(LibraryService.deleted.length, 0)
        }
        function test_transferGuardRechecksConnectionAndEditingStaysOffline() {
            const page = makeSeries(1250, false)
            page.transferEpisode(episode(1)); page.transferSeason(); page.transferSeries()
            compare(queueSpy.count, 0)
            page.customizeEpisode(episode(1))
            compare(editSpy.count, 1)
            compare(editSpy.signalArguments[0][0].episode, 1)
            page.markWatched([episode(1)], true)
            compare(LibraryService.watched.length, 1)
            const gesture = visibleChild(page, "episodeGesture")
            compare(gesture.dragArmed, false)
            DeviceService.connected = true
            page.transferEpisode(episode(1))
            compare(queueSpy.count, 1)
            DeviceService.connected = false
            page.transferEpisode(episode(1))
            compare(queueSpy.count, 1)
        }
        function test_episodeOverrideReachesActionPayload() {
            DeviceService.connected = true
            const page = makeSeries(1250, false)
            fixtureBrowser.rowOverrides = ({"1": {episodeTitle: "Corrected episode", posterPath: "/new-poster"}})
            page.transferEpisode(episode(1))
            compare(queueSpy.signalArguments[0][0].episodeTitle, "Corrected episode")
            page.playEpisode(episode(1))
            compare(playSpy.signalArguments[0][0][0].episodeTitle, "Corrected episode")
        }
        function test_libraryActionsResolveFreshRowsAndRejectOfflineTransfers() {
            const page = createTemporaryObject(libraryComponent, host)
            verify(page !== null)
            wait(30)
            const old = movie()
            const fresh = Object.assign({}, old, {title: "New title", posterPath: "/new-poster", filepath: "/new-file"})
            LibraryService.rows = [episode(1), episode(2), fresh]
            page.queueMovie(old)
            compare(SyncEngine.calls.length, 0)
            DeviceService.connected = true
            page.queueMovie(old)
            compare(SyncEngine.calls[0][0].title, "New title")
            compare(SyncEngine.calls[0][0].posterPath, "/new-poster")
            page.playMovie(old)
            compare(VideoPlayerService.calls[0].rows[0].title, "New title")
            compare(VideoPlayerService.calls[0].rows[0].filepath, "/new-file")
            page.customizeVideo(episode(1))
            compare(LibraryService.customization.ids, [1])
            compare(LibraryService.customization.kind, "series")
            compare(LibraryService.customization.episode, 1)
        }
        function test_needsMatchRowsOfferMenuAndDrag_data() {
            return [{tag: "loose", group: false}, {tag: "series", group: true}]
        }
        function test_needsMatchRowsOfferMenuAndDrag(data) {
            DeviceService.connected = true
            if (data.group) LibraryService.unmatchedGroups = [{name: "Show", episodeCount: 2, episodeIds: [1, 2]}]
            else LibraryService.unmatched = [movie()]
            const page = createTemporaryObject(libraryComponent, host, {currentTab: "Needs Match"})
            verify(page !== null)
            wait(30)
            const gesture = visibleChild(page, "unmatchedGesture")
            verify(gesture !== null)
            mouseClick(gesture, gesture.width / 3, gesture.height / 2, Qt.RightButton)
            const menu = findChild(page, "unmatchedMenu")
            tryCompare(menu, "opened", true)
            menu.close(); wait(20)
            mousePress(gesture, gesture.width / 3, gesture.height / 2)
            mouseMove(gesture, gesture.width / 3 + 35, gesture.height / 2 + 15, 30)
            mouseMove(gesture, gesture.width / 3 + 70, gesture.height / 2 + 20, 30)
            verify(TrayState.started > 0)
            mouseRelease(gesture, gesture.width / 3 + 70, gesture.height / 2 + 20)
            DeviceService.connected = false
            compare(gesture.drag.target, null)
            if (data.group) page.customizeSeries("show")
            else page.customizeVideo(movie())
            compare(LibraryService.customization.ids, data.group ? [1, 2] : [10])
        }
        function test_movieHeroPointerMenuAndDrag() {
            DeviceService.connected = true
            const page = createTemporaryObject(movieComponent, host, {
                movieSyncItemFn: row => ({libraryId: row.id, filepath: row.filepath})})
            verify(page !== null)
            movieMenuSpy.target = page
            wait(30)
            const gesture = visibleChild(page, "movieGesture")
            verify(gesture !== null)
            mouseClick(gesture, gesture.width / 2, gesture.height / 2, Qt.RightButton)
            compare(movieMenuSpy.count, 1)
            mousePress(gesture, gesture.width / 2, gesture.height / 2)
            mouseMove(gesture, gesture.width / 2 + 35, gesture.height / 2 + 25, 30)
            mouseMove(gesture, gesture.width / 2 + 70, gesture.height / 2 + 40, 30)
            verify(TrayState.started > 0)
            mouseRelease(gesture, gesture.width / 2 + 70, gesture.height / 2 + 40)
        }
        function test_deletedRowsDoNotQueueOrPlay() {
            const page = createTemporaryObject(libraryComponent, host)
            verify(page !== null)
            DeviceService.connected = true
            LibraryService.rows = []
            page.queueMovie(movie()); page.queueEpisode(episode(1)); page.playMovie(movie())
            compare(SyncEngine.calls.length, 0)
            compare(VideoPlayerService.calls.length, 0)
            page.customizeVideo(movie())
            compare(Object.keys(LibraryService.customization).length, 0)
        }
        function test_newCategoryIsUsedByTransfer() {
            const page = createTemporaryObject(libraryComponent, host)
            verify(page !== null)
            DeviceService.connected = true
            LibraryService.rows = [Object.assign(movie(), {isTV: true, category: "tv", series: "New Show",
                season: 2, episode: 3, episodeTitle: "New episode"})]
            page.queueMovie(movie())
            compare(SyncEngine.calls[0][0].series, "New Show")
            compare(SyncEngine.calls[0][0].episode, 3)
            compare(SyncEngine.calls[0][0].title, "New episode")
        }
        function test_movieHeroMenuIncludesVersionScope() {
            const page = createTemporaryObject(movieComponent, host)
            verify(page !== null)
            movieMenuSpy.target = page
            page.menuFor(movie(), [10, 11])
            compare(movieMenuSpy.count, 1)
            compare(movieMenuSpy.signalArguments[0][0].versionIds, [10, 11])
        }
        function test_backgroundScanDoesNotMoveOrResizeVideoGallery() {
            const page = createTemporaryObject(libraryComponent, host)
            verify(page !== null)
            wait(30)
            const gallery = findChild(page, "videoContent")
            verify(gallery !== null, "Production gallery has a stable test identity")
            const beforeY = gallery.mapToItem(host, 0, 0).y
            const beforeHeight = gallery.height
            const beforeWidth = gallery.width
            for (const scanning of [true, false, true, false]) {
                LibraryService.scanTotal = 250
                LibraryService.scanCurrent = scanning ? 137 : 250
                LibraryService.scanStage = "Scanning"
                LibraryService.scanCurrentFile = "A long video filename that used to reserve a progress strip.mkv"
                LibraryService.scanning = scanning
                wait(30)
                compare(gallery.mapToItem(host, 0, 0).y, beforeY)
                compare(gallery.height, beforeHeight)
                compare(gallery.width, beforeWidth)
                verify(!visibleTexts(page).some(t => t.indexOf(LibraryService.scanCurrentFile) >= 0))
            }
        }
    }
}
