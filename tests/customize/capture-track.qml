import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Rectangle {
    id: host
    width: 1100
    height: 900
    color: Theme.bg
    QtObject {
        id: backend
        property var artPaths: ({"Soft Static::After the Last Train":
            Qt.resolvedUrl("../../mockups/customize/assets/after-the-last-train.png").toString()})
        function artKey(artist, album) { return artist + "::" + album }
    }
    QtObject { id: notices; function show(text) {} }
    EditTrackSheet { id: sheet; service: backend; notifications: notices }
    TestCase {
        name: "EditTrackCapture"
        when: windowShown
        function test_capture() {
            for (const size of [Qt.size(1100, 900), Qt.size(390, 700)]) {
                host.Window.window.width = size.width
                host.Window.window.height = size.height
                host.width = size.width
                host.height = size.height
                // The desktop compositor may keep the runner tiled. Capture the
                // requested viewport explicitly; responsive bindings are tested
                // with real window resizing in the offscreen behavior suite.
                sheet.width = Math.min(Theme.customizeCompactAt, size.width - Theme.spaceXxxl)
                sheet.height = Math.min(Theme.customizeHeight - Theme.spaceHuge, size.height - Theme.spaceXxxl)
                sheet.openFor({libraryId: 47, title: "Small Hours", artist: "Soft Static",
                    albumartist: "Soft Static", album: "After the Last Train", genre: "Alternative",
                    trackNumber: 3, year: 2008})
                const art = findChild(sheet.contentItem, "editTrackArtwork")
                tryCompare(art, "status", Image.Ready)
                findChild(sheet.contentItem, "editTrackSave").forceActiveFocus()
                wait(350)
                const path = "/tmp/zuuned-song-editor-" + size.width + ".png"
                const snapshot = grabImage(sheet.contentItem.parent)
                verify(snapshot.width > 0)
                snapshot.save(path)
                console.log("Saved " + path + " (" + snapshot.width + " × " + snapshot.height + ")")
                sheet.close()
                tryCompare(sheet, "visible", false)
            }
        }
    }
}
