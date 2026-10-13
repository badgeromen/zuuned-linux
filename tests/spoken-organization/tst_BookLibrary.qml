import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Zuuned

Item {
    id: host
    width: 1200; height: 900
    Component { id: trayComponent; BookLibraryTray { x: 20; y: 40; width: 320; height: 700 } }
    Component { id: sleeveComponent; SpokenShelf { x: 500; y: 400; book: true; source: SpokenMediaService.cover("book-a") } }
    Component { id: cardComponent; SpokenCard { x: 500; y: 100; entry: SpokenMediaService.item("book-a"); book: true; transferReady: true } }
    TestCase {
        id: test
        name: "BookLibrary"
        when: windowShown
        property var tray
        function init() {
            failOnWarning(/.*(Binding loop|TypeError|ReferenceError|Unable to assign).*/)
            BookTrayState.reset()
            DeviceService.connected = false
            SpokenMediaService.reset()
            SpokenMediaService.records = [
                {id:"book-a",kind:"book",title:"Book A",author:"Author"},
                {id:"book-b",kind:"book",title:"Book B",author:"Author"},
                {id:"show-a",kind:"show",title:"A show"}]
            SpokenMediaService.shelves = [{id:"shelf-a",kind:"book-collection",title:"Series",bookIds:["book-b","book-a"],revision:4}]
            SpokenMediaService.revision++
            tray = createTemporaryObject(trayComponent,host)
            verify(tray !== null)
        }
        function cleanup() { BookTrayState.reset() }
        function click(name) {
            const control = findChild(tray,name)
            verify(control !== null)
            mouseClick(control,control.width/2,control.height/2)
        }
        function openExisting() {
            BookTrayState.openFor(SpokenMediaService.shelves[0])
            wait(300)
        }
        function test_draftCancelRequiresDiscardAndNeverWrites() {
            openExisting()
            BookTrayState.move(1,0)
            BookTrayState.removeAt(1)
            compare(BookTrayState.bookIds,["book-a"])
            click("bookTrayCancel")
            compare(BookTrayState.discardConfirmation,true)
            click("bookTrayKeepEditing")
            compare(BookTrayState.building,true)
            click("bookTrayCancel"); click("bookTrayConfirm")
            compare(BookTrayState.building,false)
            compare(SpokenMediaService.shelves[0].bookIds,["book-b","book-a"])
            compare(SpokenMediaService.calls.length,0)
        }
        function test_saveCarriesRevisionAndIgnoresUnrelatedCompletion() {
            openExisting()
            BookTrayState.move(1,0); BookTrayState.name = "Our series"
            click("bookTraySave")
            compare(BookTrayState.saving,true)
            compare(SpokenMediaService.calls[0].args,["shelf-a","Our series",["book-a","book-b"],4])
            SpokenMediaService.operationFinished("save audiobook collection","unrelated",true,"")
            verify(BookTrayState.saving)
            SpokenMediaService.operationFinished("save cover","shelf-a",true,"")
            verify(BookTrayState.saving)
            SpokenMediaService.operationFinished("save audiobook collection","shelf-a",true,"")
            compare(BookTrayState.building,false)
        }
        function test_staleFailureKeepsDraft() {
            openExisting()
            BookTrayState.removeAt(0); BookTrayState.save()
            SpokenMediaService.operationFinished("save audiobook collection","shelf-a",false,"Library changed")
            compare(BookTrayState.saving,false)
            compare(BookTrayState.building,true)
            compare(BookTrayState.bookIds,["book-a"])
            compare(BookTrayState.error,"Library changed")
        }
        function test_replacementPreservesDraftUntilConfirmed() {
            BookTrayState.openFor({},"book-a")
            BookTrayState.openFor(SpokenMediaService.shelves[0],"book-a")
            verify(BookTrayState.discardConfirmation)
            compare(BookTrayState.bookIds,["book-a"])
            BookTrayState.confirmDiscard()
            compare(BookTrayState.bookIds,["book-b","book-a"])
            compare(BookTrayState.context.id,"shelf-a")
            compare(SpokenMediaService.calls.length,0)
        }
        function test_emptyLibraryAndPendingSaveLocks() {
            BookTrayState.openFor({}); BookTrayState.save()
            compare(SpokenMediaService.calls.length,0)
            BookTrayState.name = "Future books"; BookTrayState.save()
            BookTrayState.addBooks(["book-a"]); BookTrayState.openFor(SpokenMediaService.shelves[0]); BookTrayState.requestDiscard(); BookTrayState.save()
            compare(BookTrayState.bookIds,[])
            compare(SpokenMediaService.calls.length,1)
            compare(SpokenMediaService.calls[0].args,["","Future books",[],0])
            compare(findChild(tray,"bookTraySave").enabled,false)
            compare(findChild(tray,"bookTrayName").enabled,false)
        }
        function test_deleteRequiresConfirmationAndPreservesBooksOnFailure() {
            openExisting(); click("bookTrayDelete")
            compare(SpokenMediaService.calls.length,0)
            click("bookTrayConfirm")
            compare(SpokenMediaService.calls[0].method,"deleteAudiobookCollection")
            compare(SpokenMediaService.calls[0].args,["shelf-a",4])
            SpokenMediaService.operationFinished("delete audiobook collection","shelf-a",false,"Library changed")
            compare(BookTrayState.building,true)
            compare(BookTrayState.error,"Library changed")
            compare(SpokenMediaService.records.length,3)
        }
        function test_offlinePlusDeduplicatesAndRejectsOtherKinds() {
            BookTrayState.openFor({})
            const card = createTemporaryObject(cardComponent,host)
            verify(card.dragArmed)
            const plus = findChild(card,"spokenSplitAdd")
            verify(plus.enabled)
            plus.addAll(); plus.addAll()
            BookTrayState.addBooks(["show-a","unknown","book-b"])
            compare(BookTrayState.bookIds,["book-a","book-b"])
            compare(SpokenMediaService.calls.length,0)
            const ghost = findChild(card,"spokenCardGhost")
            compare(ghost.Drag.keys,["zuuned-local-books"])
            DeviceService.connected = true
            compare(ghost.Drag.keys,["zuuned-local-books","zuuned-audiobook-books"])
            BookTrayState.reset()
            compare(ghost.Drag.keys,["zuuned-audiobook-books"])
        }
        function test_coverAndIslandPreview() {
            SpokenMediaService.coverPaths = ({"book-a": "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="200" height="250"><rect width="200" height="250" fill="#c34579"/><circle cx="100" cy="110" r="60" fill="#ff9322"/></svg>')})
            SpokenMediaService.revision++
            BookTrayState.openFor({}, "book-a")
            const card = createTemporaryObject(cardComponent,host)
            wait(350)
            grabImage(card).save("/tmp/zuuned-rounded-book.png")
            card.book = false
            wait(50)
            grabImage(card).save("/tmp/zuuned-rounded-podcast.png")
            grabImage(tray).save("/tmp/zuuned-book-library-art.png")
            const sleeve = createTemporaryObject(sleeveComponent,host)
            wait(150)
            grabImage(sleeve).save("/tmp/zuuned-rounded-book-sleeve.png")
        }
        function test_actualOfflineDragIntoIsland() {
            BookTrayState.openFor({}); wait(300)
            const card = createTemporaryObject(cardComponent,host)
            const area = findChild(card,"spokenCardDragArea")
            mousePress(area,90,100)
            mouseMove(area,120,100,30); mouseMove(area,150,100,30)
            const ghost = findChild(card,"spokenCardGhost")
            verify(ghost.Drag.active)
            mouseMove(host,150,350,30)
            mouseRelease(host,150,350)
            compare(BookTrayState.bookIds,["book-a"])
            compare(SpokenMediaService.calls.length,0)
        }
        function test_actualReadingOrderDragAndArrow() {
            openExisting()
            const handle = findChild(tray,"bookReorder_book-b")
            verify(handle !== null)
            mousePress(handle,10,20)
            mouseMove(handle,10,45,30); mouseMove(handle,10,55,30)
            mouseMove(handle,10,85,30)
            mouseRelease(handle,10,85)
            compare(BookTrayState.bookIds,["book-a","book-b"])
            click("bookTrayAction_book-b_0")
            compare(BookTrayState.bookIds,["book-b","book-a"])
            grabImage(tray).save("/tmp/zuuned-book-library-island.png")
        }
    }
}
