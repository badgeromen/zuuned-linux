import QtQuick
import QtTest
import Zuuned

Item {
    id: host
    width: 1100
    height: 800
    QtObject { id: toastHost; function show(message) {} }
    DropArea {
        id: dropTarget
        x: 950; y: 200; width: 140; height: 200
        z: 1
        keys: ["zuuned-tracks"]
        property var received: []
        onDropped: drop => {
            const items = drop.source.dragPayload
            received = typeof items === "function" ? items() : items
            drop.accept()
        }
    }
    Component { id: devicePage; PlaylistsPage { width: host.width; height: host.height } }
    Component { id: builder; PlaylistTray { width: 320; height: 700 } }
    TestCase {
        name: "PlaylistActions"
        when: windowShown
        function init() {
            DeviceService.connected = false
            DeviceService.savedTracks = []
            DeviceService.purged = []
            DeviceService.playlistsList = [{itemId:50, name:"Device tape", count:1, trackIds:[902]}]
            LibraryService.edits = []
            LibraryService.storedPlaylist = ({})
            dropTarget.received = []
            LibraryService.tracks.rows = [{libraryId:17, title:"Song", artist:"Singer", albumartist:"Various Artists",
                album:"Soundtrack", genre:"Synthwave", year:2025, trackNumber:3, durationMs:120000, filepath:"/tmp/song.mp3"}]
            TrayState.discard()
        }
        function cleanup() { TrayState.discard(); DeviceService.connected = false }
        function test_deviceMembersSaveDeleteAndNeverUseLibraryIds() {
            DeviceService.connected = true
            const page = createTemporaryObject(devicePage, host)
            page.openPlaylist = DeviceService.playlistsList[0]
            wait(30)
            const row = findChild(page, "devicePlaylistTrack902")
            verify(row)
            compare(row.track.itemId, 902)
            compare(row.track.libraryId, -1)
            compare(row.displayTrackNumber, 1)
            compare(row.track.trackNumber, 7)
            compare(row.playable, false)
            compare(row.playlistable, false)
            verify(row.saveable && row.deletable)
            row.saveRequested()
            compare(DeviceService.savedTracks[0].itemId, 902)
            row.deleteRequested()
            compare(DeviceService.purged[0], 902)
            DeviceService.connected = false
            row.saveRequested()
            row.deleteRequested()
            compare(DeviceService.savedTracks.length, 1)
            compare(DeviceService.purged.length, 1)
        }
        function test_deviceMembershipRefreshesAfterConfirmedDelete() {
            DeviceService.connected = true
            const page = createTemporaryObject(devicePage, host)
            page.openPlaylist = DeviceService.playlistsList[0]
            DeviceService.playlistsList = [{itemId:50, name:"Device tape", count:0, trackIds:[]}]
            DeviceService.stateChanged()
            compare(page.openPlaylist.trackIds.length, 0)
            DeviceService.playlistsList = []
            DeviceService.stateChanged()
            compare(page.openPlaylist, null)
        }
        function test_builderMetadataEditWorksOfflineAndPreservesCompleteTags() {
            TrayState.openExisting(10, "My tape")
            const tray = createTemporaryObject(builder, host)
            tray.editMember(17)
            const editor = findChild(tray, "builderMemberEditor")
            verify(editor)
            tryCompare(editor, "visible", true)
            compare(editor.libraryId, 17)
            compare(editor.fields.albumartist, "Various Artists")
            compare(editor.fields.year, "2025")
            compare(editor.fields.genre, "Synthwave")
            editor.setField("title", "My version")
            editor.commit()
            compare(LibraryService.edits.length, 1)
            compare(LibraryService.edits[0].id, 17)
            compare(LibraryService.edits[0].fields.albumartist, "Various Artists")
            compare(TrayState.playlistId, 10)
            compare(TrayState.tracks[0].id, 17)
            compare(TrayState.tracks[0].playlistEntryId, 1000)
        }
        function test_builderDragRequiresConnectionButNameOrderAndMembershipDoNot() {
            TrayState.openExisting(10, "My tape")
            const tray = createTemporaryObject(builder, host)
            wait(30)
            const area = findChild(tray, "builderTrackArea17")
            verify(area)
            compare(area.drag.target, null)
            DeviceService.connected = true
            verify(area.drag.target !== null)
            DeviceService.connected = false
            compare(area.drag.target, null)
            TrayState.addTracks([{libraryId:18, title:"Second", filepath:"/tmp/second.mp3"}])
            TrayState.move(1, 0)
            TrayState.name = "Renamed offline"
            verify(TrayState.save())
            compare(LibraryService.storedPlaylist.name, "Renamed offline")
            compare(LibraryService.storedPlaylist.tracks[0], 18)
            compare(LibraryService.storedPlaylist.tracks[1], 17)
            compare(LibraryService.storedPlaylist.entries, [0, 1000])
            compare(LibraryService.storedPlaylist.original, [1000])
        }
        function test_builderMemberDragCarriesAuthoritativeTrackMetadata() {
            DeviceService.connected = true
            TrayState.openExisting(10, "My tape")
            const tray = createTemporaryObject(builder, host)
            wait(30)
            const area = findChild(tray, "builderTrackArea17")
            verify(area)
            mousePress(area, 90, 20, Qt.LeftButton)
            mouseMove(area, 140, 20, 20)
            mouseMove(area, 180, 20, 20)
            tryCompare(TrayState, "dragging", true)
            mouseMove(host, 1000, 300, 20)
            mouseRelease(host, 1000, 300, Qt.LeftButton)
            tryCompare(TrayState, "dragging", false)
            compare(dropTarget.received.length, 1)
            compare(dropTarget.received[0].libraryId, 17)
            compare(dropTarget.received[0].albumartist, "Various Artists")
            compare(dropTarget.received[0].genre, "Synthwave")
            compare(TrayState.tracks.length, 1)
        }
        function test_builderHoverButtonsRemainClickableOffline() {
            const original = LibraryService.tracks.rows[0]
            LibraryService.tracks.rows = [original,
                Object.assign({}, original, {libraryId:18, title:"Second"}),
                Object.assign({}, original, {libraryId:19, title:"Third"})]
            TrayState.openExisting(10, "My tape")
            const tray = createTemporaryObject(builder, host)
            wait(30)

            function hoverAndClick(verb, id) {
                const area = findChild(tray, "builderTrackArea" + id)
                verify(area)
                mouseMove(area, 90, 20)
                const button = findChild(tray, "builderMember" + verb + id)
                verify(button)
                tryCompare(button, "visible", true)
                mouseMove(button, button.width / 2, button.height / 2)
                tryCompare(button, "visible", true)
                mouseClick(button, button.width / 2, button.height / 2)
                wait(30)
            }

            hoverAndClick("Edit", 17)
            const editor = findChild(tray, "builderMemberEditor")
            tryCompare(editor, "visible", true)
            compare(editor.libraryId, 17)
            editor.close()
            tryCompare(editor, "visible", false)
            wait(Theme.motionFast)

            hoverAndClick("Down", 17)
            compare(TrayState.tracks[1].id, 17)
            hoverAndClick("Up", 17)
            compare(TrayState.tracks[0].id, 17)
            hoverAndClick("Remove", 18)
            compare(TrayState.tracks.length, 2)
            compare(TrayState.tracks[0].id, 17)
            compare(TrayState.tracks[1].id, 19)
            compare(DeviceService.connected, false)
        }
    }
}
