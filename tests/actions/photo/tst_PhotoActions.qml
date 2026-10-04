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
        keys: ["zuuned-photos", "zuuned-device-photos"]
        property var received: []
        onDropped: drop => {
            const items = drop.source.dragPayload
            received = typeof items === "function" ? items() : items
            drop.accept()
        }
    }
    DropArea {
        id: localDrop
        x: 950; y: 430; width: 140; height: 180; z: 3
        keys: ["zuuned-local-photos"]
        property var received: []
        onDropped: drop => {
            const payload = drop.source.dragPayload
            received = typeof payload === "function" ? payload() : payload
            PhotoTrayState.addPhotos(received)
            drop.accept()
        }
    }
    Component { id: localPage; LibraryPhotosView { width: host.width; height: host.height; gallery: viewer } }
    Component { id: devicePage; DevicePicturesPage { width: host.width; height: host.height; gallery: viewer } }
    PhotoGalleryView { id: viewer; anchors.fill: parent; z: mediaDragging ? -1 : 2 }
    TestCase {
        name: "PhotoActions"
        when: windowShown
        function init() {
            PhotoTrayState.discard()
            DeviceService.connected = false
            DeviceService.savedPhotos = []
            DeviceService.photosList = [{itemId:902, name:"device.jpg", parentId:80}]
            DeviceService.photoAlbumsList = [{itemId:80, name:"Device album"}]
            LibraryService.photos = [{id:17, filename:"one.jpg", url:"", filesize:123},
                                     {id:18, filename:"two.jpg", url:"", filesize:456}]
            LibraryService.customAlbums = []
            LibraryService.members = ({})
            LibraryService.albumFailure = ""
            SyncEngine.photos = []
            dropTarget.received = []
            localDrop.received = []
        }
        function cleanup() { viewer.close(); DeviceService.connected = false }
        function test_localThumbnailPlusQueuesInsteadOfOpeningViewer() {
            DeviceService.connected = true
            const page = createTemporaryObject(localPage, host)
            verify(page)
            page.openAlbum({path:"/tmp/album", name:"Album"})
            wait(30)
            const add = findChild(page, "libraryPhotoAdd")
            verify(add)
            mouseMove(add, 15, 13)
            tryCompare(add, "opacity", 1)
            mouseClick(add, 15, 13)
            compare(SyncEngine.photos.length, 1)
            compare(SyncEngine.photos[0].libraryId, 17)
            compare(viewer.visible, false)
            DeviceService.connected = false
            page.queuePhotos(LibraryService.photos, "Album")
            compare(SyncEngine.photos.length, 1)
            compare(add.visible, false)
        }
        function test_deviceThumbnailSaveWinsHitTest() {
            DeviceService.connected = true
            const page = createTemporaryObject(devicePage, host)
            verify(page)
            page.openAlbum = page.albums[0]
            wait(30)
            const add = findChild(page, "devicePhotoSave")
            verify(add)
            mouseMove(add, 15, 13)
            tryCompare(add, "opacity", 1)
            mouseClick(add, 15, 13)
            compare(DeviceService.savedPhotos.length, 1)
            compare(DeviceService.savedPhotos[0].itemId, 902)
            compare(viewer.visible, false)
        }
        function test_fullscreenAndFilmstripKeepLocalIdsAndAlbum() {
            DeviceService.connected = true
            const page = createTemporaryObject(localPage, host)
            page.openGallery(LibraryService.photos, 0, "Album")
            verify(viewer.canTransfer)
            wait(30)
            const transfer = findChild(viewer, "galleryPhotoTransfer")
            mouseClick(transfer, transfer.width / 2, transfer.height / 2)
            compare(SyncEngine.photos.length, 1)
            compare(SyncEngine.photos[0].libraryId, 17)
            wait(30)
            const film = findChild(viewer, "galleryFilmArea1")
            verify(film)
            mouseMove(film, 12, 12)
            const add = findChild(viewer, "galleryFilmTransfer1")
            tryCompare(add, "visible", true)
            mouseClick(add, 15, 13)
            compare(SyncEngine.photos.length, 2)
            compare(SyncEngine.photos[1].libraryId, 18)
            compare(SyncEngine.photos[1].album, "Album")
            compare(viewer.index, 0) // the add button must not select its thumbnail
            compare(viewer.payloadAt(1)[0].libraryId, 18)
            DeviceService.connected = false
            viewer.transferAt(1)
            compare(SyncEngine.photos.length, 2)
            compare(viewer.payloadAt(1).length, 1)
            verify(film.drag.target !== null)
            verify(!viewer.canTransfer)
        }
        function test_deviceViewerOnlySavesDeviceIds() {
            DeviceService.connected = true
            const page = createTemporaryObject(devicePage, host)
            page.openAlbum = page.albums[0]
            page.openGallery(0)
            verify(viewer.deviceMode)
            viewer.transferAt(0)
            compare(DeviceService.savedPhotos.length, 1)
            compare(DeviceService.savedPhotos[0].itemId, 902)
            compare(DeviceService.savedPhotos[0].subdir, "Device album")
            compare(SyncEngine.photos.length, 0)
            compare(viewer.payloadAt(0)[0].itemId, 902)
            verify(viewer.payloadAt(0)[0].libraryId === undefined)
            DeviceService.connected = false
            viewer.transferAt(0)
            compare(DeviceService.savedPhotos.length, 1)
        }
        function test_lateDeviceResolverCannotReplaceAnotherAlbum() {
            let delivered = null
            viewer.open([{url:"", filename:"old"}], 0, (i, cb) => { delivered = cb })
            viewer.close()
            viewer.open([{url:"", filename:"new"}], 0)
            delivered("file:///late-old-photo.jpg")
            compare(viewer.sourceFor(0), "")
        }
        function test_fullscreenDragRevealsAndReachesUnderlyingDropTarget() {
            DeviceService.connected = true
            const page = createTemporaryObject(localPage, host)
            page.openGallery(LibraryService.photos, 0, "Album")
            wait(30)
            mousePress(host, 400, 300, Qt.LeftButton)
            mouseMove(host, 450, 300, 20)
            mouseMove(host, 500, 300, 20)
            tryCompare(viewer, "mediaDragging", true)
            compare(viewer.opacity, 0)
            mouseMove(host, 1000, 300, 20)
            mouseRelease(host, 1000, 300, Qt.LeftButton)
            tryCompare(viewer, "mediaDragging", false)
            compare(viewer.opacity, 1)
            compare(dropTarget.received.length, 1)
            compare(dropTarget.received[0].libraryId, 17)
        }
        function test_customAlbumsNavigateAndQueueDirectMembershipOnly() {
            LibraryService.customAlbums = [
                {id:1,parentId:0,name:"Family",pathLabel:"Family",count:1,covers:[]},
                {id:2,parentId:1,name:"Holiday",pathLabel:"Family / Holiday",count:1,covers:[]}]
            LibraryService.members = ({1:[17],2:[18]})
            const page = createTemporaryObject(localPage, host)
            page.switchPivot("Albums")
            compare(page.shownAlbums.length, 1)
            page.openAlbum(page.shownAlbums[0])
            compare(page.openCustomId, 1)
            compare(page.childAlbums.length, 1)
            compare(page.albumPhotos.length, 1)
            page.queuePhotos(page.albumPhotos, page.openAlbumName)
            compare(SyncEngine.photos.length, 0)
            DeviceService.connected = true
            page.queuePhotos(page.albumPhotos, page.openAlbumName)
            compare(SyncEngine.photos.length, 1)
            compare(SyncEngine.photos[0].libraryId, 17)
            page.goToCustom(2)
            compare(page.albumTrail.length, 2)
            compare(page.albumPhotos[0].id, 18)
        }
        function test_newAndExistingUseStagedBuilder() {
            const page = createTemporaryObject(localPage, host)
            page.switchPivot("Albums")
            const create = findChild(page, "newPhotoAlbum")
            mouseClick(create, create.width / 2, create.height / 2)
            verify(PhotoTrayState.building)
            PhotoTrayState.name = "Family"
            compare(LibraryService.customAlbums.length, 0)
            PhotoTrayState.addPhotos(LibraryService.photos)
            LibraryService.albumFailure = "Disk full"
            verify(!PhotoTrayState.save())
            compare(PhotoTrayState.error, "Disk full")
            LibraryService.albumFailure = ""
            verify(PhotoTrayState.save())
            page.goToCustom(LibraryService.customAlbums[0].id)
            wait(30)
            const add = findChild(page,"addAlbumPhotos")
            mouseClick(add, add.width / 2, add.height / 2)
            verify(PhotoTrayState.building)
            compare(PhotoTrayState.photos.length,2)
            compare(page.pivot,"Folders")
        }
        function test_selectionUsesClicksRangesAndPersistsAcrossFolders() {
            const page = createTemporaryObject(localPage, host)
            page.openAlbum({path:"/tmp/album", name:"Album"})
            wait(30)
            const first = findChild(page,"photoArea17"), second = findChild(page,"photoArea18")
            verify(first); verify(second)
            mouseClick(first,40,40,Qt.LeftButton,Qt.ControlModifier)
            compare(page.selectedCount,1)
            mouseClick(second,40,40,Qt.LeftButton,Qt.ShiftModifier)
            compare(page.selectedCount,2)
            compare(viewer.visible,false)
            page.switchPivot("Albums")
            wait(30)
            compare(page.selectedCount,2)
            const add = findChild(page,"photoSelectionAdd")
            mouseClick(add,add.width/2,add.height/2)
            compare(PhotoTrayState.photos.length,2)
            compare(LibraryService.customAlbums.length,0)
            compare(SyncEngine.photos.length,0)
        }
        function test_offlineSelectionDragReachesLocalTargetOnly() {
            const page = createTemporaryObject(localPage,host)
            page.openAlbum({path:"/tmp/album",name:"Album"})
            page.selectAllPhotos()
            PhotoTrayState.openNew()
            wait(30)
            const area = findChild(page,"photoArea17")
            const start = area.mapToItem(host,40,40)
            mousePress(host,start.x,start.y,Qt.LeftButton)
            mouseMove(host,start.x+30,start.y,20)
            mouseMove(host,start.x+60,start.y,20)
            mouseMove(host,1000,490,20)
            mouseRelease(host,1000,490,Qt.LeftButton)
            compare(localDrop.received.length,2)
            compare(PhotoTrayState.photos.length,2)
            compare(page.selectedCount,2)
            compare(dropTarget.received.length,0)
            compare(SyncEngine.photos.length,0)
        }
        function test_offlineFolderDragAddsWholeFolder() {
            const page = createTemporaryObject(localPage,host)
            PhotoTrayState.openNew()
            wait(30)
            const area = findChild(page,"photoFolderDrag")
            verify(area)
            const start = area.mapToItem(host,40,40)
            mousePress(host,start.x,start.y,Qt.LeftButton)
            mouseMove(host,start.x+30,start.y,20)
            mouseMove(host,start.x+60,start.y,20)
            mouseMove(host,1000,490,20)
            mouseRelease(host,1000,490,Qt.LeftButton)
            compare(PhotoTrayState.photos.length,2)
            compare(PhotoTrayState.photos[0].id,17)
            compare(SyncEngine.photos.length,0)
        }
        function test_offlineViewerDragUsesLocalTarget() {
            const page = createTemporaryObject(localPage,host)
            PhotoTrayState.openNew()
            page.openGallery(LibraryService.photos,0,"Album")
            wait(30)
            mousePress(host,400,300,Qt.LeftButton)
            mouseMove(host,450,300,20)
            mouseMove(host,500,300,20)
            mouseMove(host,1000,490,20)
            mouseRelease(host,1000,490,Qt.LeftButton)
            compare(localDrop.received.length,1)
            compare(PhotoTrayState.photos[0].id,17)
            compare(SyncEngine.photos.length,0)
        }
        function test_offlineFilmstripDragUsesLocalTarget() {
            const page = createTemporaryObject(localPage,host)
            PhotoTrayState.openNew()
            page.openGallery(LibraryService.photos,0,"Album")
            wait(30)
            const area = findChild(viewer,"galleryFilmArea1")
            verify(area)
            const start = area.mapToItem(host,30,30)
            mousePress(host,start.x,start.y,Qt.LeftButton)
            mouseMove(host,start.x+30,start.y,20)
            mouseMove(host,start.x+60,start.y,20)
            mouseMove(host,1000,490,20)
            mouseRelease(host,1000,490,Qt.LeftButton)
            compare(localDrop.received.length,1)
            compare(PhotoTrayState.photos[0].id,18)
            compare(SyncEngine.photos.length,0)
        }
        function test_selectionKeepsSourceOrderAndPlusAddsOffline() {
            LibraryService.photos = [LibraryService.photos[1],LibraryService.photos[0]]
            const page = createTemporaryObject(localPage,host)
            page.openAlbum({path:"/tmp/album",name:"Album"})
            page.selectAllPhotos()
            compare(page.selectedRows[0].id,18)
            compare(page.dragRows(LibraryService.photos[0])[0].id,18)
            PhotoTrayState.openNew()
            wait(30)
            const add = findChild(page,"libraryPhotoAdd")
            verify(add.visible)
            mouseMove(add,15,13)
            tryCompare(add,"opacity",1)
            mouseClick(add,15,13)
            compare(PhotoTrayState.photos[0].id,18)
            compare(SyncEngine.photos.length,0)
        }
        function test_staleDraftPickerCannotQueueOrAddToReplacement() {
            const page = createTemporaryObject(localPage,host)
            PhotoTrayState.openNew()
            wait(30)
            const split = findChild(page,"folderPhotoAdd")
            split.pickParts()
            const picker = findChild(page,"photoDeviceOrDraftPicker")
            verify(picker.opened)
            compare(picker.actionWord,"add to album")
            PhotoTrayState.discard()
            DeviceService.connected = true
            PhotoTrayState.openNew()
            picker.picked(["0"])
            compare(PhotoTrayState.photos.length,0)
            compare(SyncEngine.photos.length,0)
            picker.close()
        }
        function test_bulkSelectionRetainsVirtualizedGrid() {
            const rows = []
            for (let i=1;i<=5000;++i) rows.push({id:i,filename:i+".jpg",url:"",filesize:1})
            LibraryService.photos = rows
            const page = createTemporaryObject(localPage,host)
            page.openAlbum({path:"/tmp/album",name:"Album"})
            page.forceActiveFocus()
            keyClick(Qt.Key_A,Qt.ControlModifier)
            compare(page.selectedCount,5000)
            const grid = findChild(page,"photoGrid")
            verify(grid.contentItem.children.length < 100)
            page.addToDraft(Object.values(page.selectedPhotos))
            compare(PhotoTrayState.photos.length,5000)
            PhotoTrayState.addPhotos(Object.values(page.selectedPhotos))
            compare(PhotoTrayState.photos.length,5000)
        }
        function test_pivotClearsDisplayedSearch() {
            const page = createTemporaryObject(localPage, host)
            const search = findChild(page,"photoAlbumSearch")
            search.forceActiveFocus()
            keyClick(Qt.Key_A)
            compare(page.search, "a")
            page.switchPivot("Albums")
            compare(page.search, "")
            compare(search.text, "")
        }
        function test_addToFirstAlbumStagesBeforeSave() {
            const page = createTemporaryObject(localPage, host)
            page.addToDraft([LibraryService.photos[0]])
            compare(PhotoTrayState.photos[0].id,17)
            compare(LibraryService.customAlbums.length,0)
            PhotoTrayState.name = "First"
            verify(PhotoTrayState.save())
            compare(LibraryService.customAlbumPhotos(LibraryService.customAlbums[0].id)[0].id,17)
        }
        function test_membershipOrderingAndAlbumDeletionPreservePhotos() {
            LibraryService.customAlbums = [{id:1,parentId:0,name:"Family",pathLabel:"Family",count:2,covers:[]}]
            LibraryService.members = ({1:[17,18]})
            const page = createTemporaryObject(localPage, host)
            page.switchPivot("Albums")
            page.goToCustom(1)
            PhotoTrayState.openExisting(1)
            PhotoTrayState.move(1, 0)
            compare(page.albumPhotos[0].id, 17)
            verify(PhotoTrayState.save())
            compare(page.albumPhotos[0].id, 18)
            page.editAlbum("delete", LibraryService.customAlbums[0])
            compare(LibraryService.customAlbums.length, 1)
            findChild(page,"photoAlbumEditor").save()
            compare(LibraryService.customAlbums.length, 0)
            compare(LibraryService.photos.length, 2)
            compare(page.openCustomId, 0)
        }
        function test_encodedLocalPhotoPathIsDecodedForSync() {
            const page = createTemporaryObject(localPage, host)
            const payload = page.photoSyncItem({id:2, filename:"some photo.jpg", url:"file:///tmp/some%20photo.jpg", filesize:10}, "Test")
            compare(payload.filepath, "/tmp/some photo.jpg")
        }
    }
}
