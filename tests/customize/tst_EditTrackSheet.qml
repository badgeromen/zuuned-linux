import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Item {
    id: host
    width: 1100
    height: 900
    QtObject {
        id: backend
        property var artPaths: ({})
        property var saves: []
        property bool writesTags: true
        property bool saveFails: false
        function artKey(artist, album) { return artist + "::" + album }
        function editTrackMetadata(id, fields) {
            saves = saves.concat([{id: id, fields: fields}])
            return {success: !saveFails, fileWritten: !saveFails && writesTags,
                error: saveFails ? "Couldn't save your edits to the library. Try again." : ""}
        }
    }
    QtObject {
        id: notices
        property string message: ""
        function show(text) { message = text }
    }
    EditTrackSheet { id: sheet; service: backend; notifications: notices }
    TestCase {
        name: "EditTrackSheet"
        when: windowShown
        function track() {
            return {libraryId: 47, title: "Original song", artist: "Singer",
                albumartist: "Various Artists", album: "Original album",
                genre: "Alternative", trackNumber: 3, year: 2008}
        }
        function resize(width, height) {
            host.Window.window.width = width
            host.Window.window.height = height
            host.width = width
            host.height = height
            wait(30)
        }
        function open(context) {
            sheet.openFor(context || track())
            tryCompare(sheet, "visible", true)
        }
        function cleanup() {
            sheet.close()
            tryCompare(sheet, "visible", false)
            backend.saves = []
            backend.writesTags = true
            backend.saveFails = false
            backend.artPaths = ({})
            notices.message = ""
            resize(1100, 900)
        }
        function test_openFocusAndTypingRetainAllFields() {
            open()
            const title = findChild(sheet.contentItem, "editTrackTitle")
            const input = findChild(title, "customizeFieldInput")
            tryCompare(input, "activeFocus", true)
            compare(input.selectedText, "Original song")
            keyClick(Qt.Key_N)
            compare(sheet.fields.title, "n")
            compare(sheet.fields.albumartist, "Various Artists")
            compare(sheet.fields.trackNumber, "3")
            compare(sheet.fields.year, "2008")
            compare(backend.saves.length, 0)
        }
        function test_saveUsesExistingTrackApiAndTagResult() {
            for (const writesTags of [true, false]) {
                backend.writesTags = writesTags
                open()
                sheet.setField("title", "My song")
                sheet.setField("albumartist", "My compilation")
                sheet.setField("year", "")
                sheet.setField("trackNumber", "12")
                sheet.commit()
                tryCompare(sheet, "visible", false)
                const saved = backend.saves[backend.saves.length - 1]
                compare(saved.id, 47)
                compare(saved.fields, {title: "My song", artist: "Singer",
                    albumartist: "My compilation", album: "Original album",
                    genre: "Alternative", trackNumber: 12, year: 0})
                compare(notices.message, writesTags ? "saved — tags written to the file"
                    : "saved to library (file tags unchanged)")
            }
            compare(backend.saves.length, 2)
        }
        function test_cancelDoesNotSaveAndReopenStartsFresh() {
            open()
            sheet.setField("title", "Discard me")
            const cancel = findChild(sheet.contentItem, "editTrackCancel")
            mouseClick(cancel)
            tryCompare(sheet, "visible", false)
            compare(backend.saves.length, 0)
            open()
            compare(sheet.fields.title, "Original song")
            keyClick(Qt.Key_Escape)
            tryCompare(sheet, "visible", false)
            compare(backend.saves.length, 0)
        }
        function test_failedSaveKeepsDraftAndCanRetry() {
            resize(390, 700)
            open()
            sheet.setField("title", "Keep this edit")
            backend.saveFails = true
            sheet.commit()
            verify(sheet.visible)
            compare(sheet.fields.title, "Keep this edit")
            verify(sheet.errorMessage.length > 0)
            compare(notices.message, "", "A failed save must never announce success")
            const save = findChild(sheet.contentItem, "editTrackSave")
            wait(20)
            const point = save.mapToItem(sheet.contentItem, 0, 0)
            verify(point.y + save.height <= sheet.height)
            backend.saveFails = false
            sheet.commit()
            tryCompare(sheet, "visible", false)
            compare(backend.saves.length, 2)
        }
        function test_blankTitleCannotSave() {
            open()
            sheet.setField("title", "   ")
            verify(!findChild(sheet.contentItem, "editTrackSave").enabled)
            sheet.commit()
            compare(backend.saves.length, 0)
            verify(sheet.visible)
        }
        function test_compilationArtworkUsesAlbumArtist() {
            const compilation = "data:image/svg+xml," + encodeURIComponent(
                '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100">'
                + '<rect width="100" height="100" fill="teal"/></svg>')
            backend.artPaths = {"Various Artists::Original album": compilation,
                "Singer::Original album": ""}
            open()
            compare(sheet.artwork, compilation)
            sheet.setField("albumartist", "A new credit")
            compare(sheet.artwork, compilation)
        }
        function test_smallWindowKeepsFooterAccessible() {
            resize(390, 620)
            open()
            verify(sheet.narrow)
            const form = findChild(sheet.contentItem, "editTrackForm")
            const footer = findChild(sheet.contentItem, "editTrackFooter")
            const save = findChild(sheet.contentItem, "editTrackSave")
            tryVerify(() => form.contentHeight > form.height)
            verify(footer.y >= form.y + form.height)
            verify(footer.y + footer.height <= sheet.height)
            verify(save.mapToItem(sheet.contentItem, save.width, 0).x <= sheet.width)
            form.contentY = form.contentHeight - form.height
            verify(save.visible)
        }
    }
}
