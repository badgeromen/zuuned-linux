import QtQuick
import QtTest
import Zuuned

TestCase {
    name: "PhotoTray"
    visible: true
    when: windowShown
    width: 400
    height: 700
    PhotoAlbumTray { id: tray; width: 340; height: 640 }
    Item {
        id: dragSource
        x: 360; y: 680; width: 10; height: 10
        property var dragPayload: []
        Drag.keys: ["zuuned-local-photos"]
        Drag.source: dragSource
        Drag.hotSpot.x: 5
        Drag.hotSpot.y: 5
    }
    function init() {
        PhotoTrayState.discard()
        LibraryService.albumFailure = ""
        LibraryService.photos = [{id:1,filename:"one.jpg",url:""},{id:2,filename:"two.jpg",url:""},{id:3,filename:"three.jpg",url:""}]
        LibraryService.customAlbums = [{id:8,name:"Original",parentId:0,pathLabel:"Original"}]
        LibraryService.members = ({8:[1,2]})
    }
    function cleanup() { PhotoTrayState.discard() }
    function test_offlineDraftCancelPreservesLibrary() {
        DeviceService.connected = false
        verify(PhotoTrayState.openNew(0, LibraryService.photos))
        PhotoTrayState.name = "New"
        verify(PhotoTrayState.dirty)
        compare(LibraryService.customAlbums.length, 1)
        PhotoTrayState.requestDiscard()
        verify(PhotoTrayState.discardConfirmation)
        compare(PhotoTrayState.addPhotos([{id:999,filename:"blocked"}]),0)
        PhotoTrayState.cancelDiscard()
        verify(PhotoTrayState.building)
        PhotoTrayState.requestDiscard()
        PhotoTrayState.confirmDiscard()
        verify(!PhotoTrayState.building)
        compare(LibraryService.customAlbums.length, 1)
    }
    function test_bulkDedupAndOrder() {
        PhotoTrayState.openNew()
        const rows = []
        for (let i=1;i<=5000;i++) rows.push({id:i,filename:"photo"+i,url:""})
        compare(PhotoTrayState.addPhotos(rows),5000)
        compare(PhotoTrayState.addPhotos(rows),0)
        compare(PhotoTrayState.photos.length,5000)
        PhotoTrayState.move(0,4999)
        compare(PhotoTrayState.photos[4999].id,1)
        PhotoTrayState.removeAt(4999)
        compare(PhotoTrayState.photos.length,4999)
    }
    function test_dirtyReplacementRequiresConfirmation() {
        PhotoTrayState.openNew()
        PhotoTrayState.name = "Draft"
        verify(!PhotoTrayState.openExisting(8))
        compare(PhotoTrayState.name,"Draft")
        PhotoTrayState.cancelDiscard()
        compare(PhotoTrayState.name,"Draft")
        verify(!PhotoTrayState.openExisting(8))
        PhotoTrayState.confirmDiscard()
        compare(PhotoTrayState.name,"Original")
        compare(PhotoTrayState.photos.length,2)
        verify(!PhotoTrayState.dirty)
    }
    function test_existingSaveAndFailure() {
        PhotoTrayState.openExisting(8)
        PhotoTrayState.name = "Edited"
        PhotoTrayState.removeAt(0)
        PhotoTrayState.addPhotos([LibraryService.photos[2]])
        LibraryService.albumFailure = "Conflict"
        verify(!PhotoTrayState.save())
        compare(PhotoTrayState.error,"Conflict")
        verify(PhotoTrayState.building)
        compare(LibraryService.customAlbums[0].name,"Original")
        LibraryService.albumFailure = ""
        verify(PhotoTrayState.save())
        verify(!PhotoTrayState.building)
        compare(LibraryService.customAlbums[0].name,"Edited")
        compare(LibraryService.members[8],[2,3])
    }
    function test_narrowVirtualizedLargeDraft() {
        tray.width = 250
        PhotoTrayState.openNew()
        PhotoTrayState.name = "Summer photographs"
        const rows = []
        for (let i=1;i<=5000;i++) rows.push({id:i,filename:"Holiday photograph " + i + ".jpg",url:Qt.resolvedUrl("../../../qml/images/zuuned.png")})
        PhotoTrayState.addPhotos(rows)
        tryCompare(tray,"reveal",1)
        const list = findChild(tray,"photoTrayList")
        tryCompare(list,"count",5000)
        wait(100)
        verify(list.contentItem.children.length < 40,"Only visible rows and a bounded cache are instantiated")
        const image = grabImage(tray)
        image.save("/tmp/zuuned-photo-tray-narrow.png")
        list.positionViewAtEnd()
        wait(100)
        verify(list.contentItem.children.length < 40,"End of list remains virtualized")
        tray.width = 340
    }
    function test_localDropOffline() {
        DeviceService.connected = false
        PhotoTrayState.openNew()
        tryCompare(tray,"reveal",1)
        dragSource.dragPayload = LibraryService.photos
        dragSource.x = 360; dragSource.y = 680
        dragSource.Drag.active = true
        dragSource.x = 120; dragSource.y = 300
        wait(30)
        dragSource.Drag.drop()
        dragSource.Drag.active = false
        compare(PhotoTrayState.photos.length,3)
        compare(LibraryService.customAlbums.length,1)
    }
    function test_nameAndParentDirty() {
        PhotoTrayState.openExisting(8)
        verify(!PhotoTrayState.dirty)
        PhotoTrayState.parentId = 99
        verify(PhotoTrayState.dirty)
        PhotoTrayState.parentId = 0
        verify(!PhotoTrayState.dirty)
        PhotoTrayState.name = ""
        verify(!PhotoTrayState.save())
        verify(PhotoTrayState.error.length > 0)
    }
    function test_uiControlsStageChanges() {
        PhotoTrayState.openNew(0,LibraryService.photos)
        PhotoTrayState.name = "Saved through UI"
        tryCompare(tray,"reveal",1)
        tryVerify(() => findChild(tray,"photoTrayAction1_2") !== null)
        mouseClick(findChild(tray,"photoTrayAction1_2"))
        compare(PhotoTrayState.photos.length,2)
        mouseClick(findChild(tray,"photoTrayCancel"))
        verify(PhotoTrayState.discardConfirmation)
        compare(PhotoTrayState.addPhotos([{id:999,filename:"blocked"}]),0)
        mouseClick(findChild(tray,"photoTrayKeepEditing"))
        verify(!PhotoTrayState.discardConfirmation)
        mouseClick(findChild(tray,"photoTraySave"))
        verify(!PhotoTrayState.building)
        compare(LibraryService.customAlbums.length,2)
    }
}
