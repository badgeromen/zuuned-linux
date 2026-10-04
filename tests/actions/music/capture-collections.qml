import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Rectangle {
    id: host
    width: 1280
    height: 900
    color: Theme.bg
    CustomizeSheet { id: sheet }
    QtObject { id: toastHost; function show(message) {} }
    PlaylistTray { id: builder; x: 48; y: 48; width: 320; height: 700 }
    TestCase {
        name: "CollectionArtCapture"
        when: windowShown
        function test_capture() {
            const album = Qt.resolvedUrl("../../../mockups/customize/assets/after-the-last-train.png").toString()
            const artist = Qt.resolvedUrl("../../../mockups/customize/assets/artist.png").toString()
            const series = Qt.resolvedUrl("../../../mockups/customize/assets/series.png").toString()
            host.Window.window.width = host.width
            host.Window.window.height = host.height
            for (const context of [
                {kind: "genre", name: "Alternative", title: "Alternative", artUrls: [album, artist, series, album]},
                {kind: "mixtape", id: 7, name: "After hours", title: "After hours", poster: album, artUrls: [album, artist]}
            ]) {
                sheet.openFor(context)
                wait(Theme.motionSlow)
                const snapshot = grabImage(sheet.contentItem.parent)
                verify(snapshot.width > 0)
                snapshot.save("/tmp/zuuned-collection-" + context.kind + ".png")
                sheet.discard()
                tryCompare(sheet, "visible", false)
            }
            LibraryService.playlists = [{id: 7, name: "After hours", count: 0}]
            LibraryService.collectionArts = ({"mixtape:7": album})
            LibraryService.collectionArtRevision++
            TrayState.openExisting(7, "After hours")
            wait(Theme.motionSlow)
            const snapshot = grabImage(builder)
            verify(snapshot.width > 0)
            snapshot.save("/tmp/zuuned-collection-builder.png")
            TrayState.building = false
        }
    }
}
