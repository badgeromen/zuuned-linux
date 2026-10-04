import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Controls
import QtQuick.Layouts
import Zuuned

// Folder photos and virtual albums share the gallery. Virtual hierarchy is
// local organization; gallery and transfers use direct members only.
Item {
    id: root

    // Gallery plans from ROOT width (loop-safe: this page's layouts
    // never assign root.width). The grids take an explicit centered
    // width from these — GridView margins cannot pin a column count.
    readonly property var albumPlan: Gallery.plan(
        Math.max(220, width - 48), 220, 7)
    readonly property var thumbPlan: Gallery.plan(
        Math.max(220, width - 48), 200, 8)
    // ONE fixed spine (2026-09-06): header and both grids anchor to
    // the album plan's edge — drilling into an album never shifts x
    readonly property real headerSpineX: Gallery.spine(width)

    property var gallery   // PhotoGalleryView instance (Main.qml)

    property var albums: []
    property string openAlbumPath: ""
    property string openAlbumName: ""
    property var albumPhotos: []
    property string search: ""
    property bool selecting: false
    property var selectedPhotos: ({})
    property var selectedOrder: []
    readonly property var selectedRows: selectedOrder.map(id => selectedPhotos[id]).filter(p => p)
    readonly property int selectedCount: selectedOrder.length
    property int selectionAnchor: -1
    onAlbumPhotosChanged: selectionAnchor = -1
    focus: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier) && root.inAlbum) {
            root.selectAllPhotos(); event.accepted = true
        } else if (event.key === Qt.Key_Escape && root.selectedCount > 0) {
            root.clearSelection(); event.accepted = true
        }
    }
    function clearSelection() { selectedPhotos = ({}); selectedOrder = []; selecting = false; selectionAnchor = -1 }
    function selectAllPhotos() {
        const next = Object.assign({}, selectedPhotos)
        const order = selectedOrder.slice()
        for (const photo of albumPhotos) { if (!next[photo.id]) order.push(photo.id); next[photo.id] = photo }
        selectedPhotos = next; selectedOrder = order; selecting = true
    }
    function selectPhoto(index, modifiers) {
        const photo = albumPhotos[index]
        if (!photo) return
        const next = Object.assign({}, selectedPhotos)
        const order = selectedOrder.slice()
        if ((modifiers & Qt.ShiftModifier) && selectionAnchor >= 0) {
            for (let i = Math.min(selectionAnchor, index); i <= Math.max(selectionAnchor, index); ++i) {
                if (!next[albumPhotos[i].id]) order.push(albumPhotos[i].id)
                next[albumPhotos[i].id] = albumPhotos[i]
            }
        } else {
            if (next[photo.id]) delete next[photo.id]
            else { next[photo.id] = photo; order.push(photo.id) }
            selectionAnchor = index
        }
        selectedPhotos = next; selectedOrder = order.filter(id => next[id]); selecting = true
    }
    function addToDraft(rows) {
        if (PhotoTrayState.building) PhotoTrayState.addPhotos(rows)
        else PhotoTrayState.openNew(0, rows)
    }
    function dragRows(photo) {
        return selectedPhotos[photo.id] ? selectedRows : [photo]
    }
    function localDragItem(photo, albumName) {
        return Object.assign({}, photo, photoSyncItem(photo, albumName))
    }

    property string pivot: "Folders"
    property double openCustomId: 0
    property var customAlbums: []
    readonly property bool inAlbum: openAlbumPath !== "" || openCustomId > 0
    readonly property var childAlbums: customAlbums.filter(a => a.parentId === openCustomId)
    readonly property var albumTrail: {
        let trail = [], id = openCustomId
        while (id > 0) {
            const a = customAlbums.find(row => row.id === id)
            if (!a || trail.some(row => row.id === id)) break
            trail.unshift(a); id = a.parentId
        }
        return trail
    }
    function switchPivot(value) {
        pivot = value; openAlbumPath = ""; openCustomId = 0; search = ""
        albumPhotos = []
    }
    function photosFor(a) {
        return a.id !== undefined ? LibraryService.customAlbumPhotos(a.id)
                                 : LibraryService.photosInAlbum(a.path)
    }
    function goToCustom(id) {
        openAlbumPath = ""; openCustomId = id
        const a = customAlbums.find(row => row.id === id)
        openAlbumName = a ? a.name : ""
        albumPhotos = id > 0 ? LibraryService.customAlbumPhotos(id) : []
    }
    function showResult(result) {
        if (!result.success) toastHost.show(result.error || "Could not save photo album")
        return result.success
    }
    function editAlbum(mode, target) {
        albumEditor.mode = mode
        albumEditor.target = target
        albumEditor.error = ""
        albumDestination.currentIndex = 0
        albumEditor.open()
    }

    function editCustomAlbum(id, browse) {
        if (PhotoTrayState.openExisting(id)) {
            if (browse) switchPivot("Folders")
        } else if (PhotoTrayState.error) toastHost.show(PhotoTrayState.error)
    }
    function pickLibraryPhotos() { editCustomAlbum(openCustomId, true) }

    component AlbumButton: DetailActionButton {
        Layout.fillWidth: false
        implicitWidth: title.length * Theme.spaceSm + Theme.spaceXl
    }
    component AlbumCombo: ComboBox {
        id: combo
        background: Rectangle { color: Theme.cardBg; border.color: Theme.borderLight; radius: Theme.radiusMd }
        contentItem: Text {
            text: combo.displayText
            color: Theme.textPrimary
            leftPadding: Theme.spaceMd
            rightPadding: Theme.spaceXxl
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        delegate: ItemDelegate {
            width: combo.width
            contentItem: Text { text: modelData[combo.textRole]; color: Theme.textPrimary; elide: Text.ElideRight }
            background: Rectangle { color: highlighted ? Theme.cardHover : Theme.cardBg }
            highlighted: combo.highlightedIndex === index
        }
        popup: Popup {
            y: combo.height
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight, Theme.spaceHuge * 5)
            padding: Theme.spaceXxs
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollBar.vertical: ZuneScrollBar {}
            }
            background: Rectangle { color: Theme.cardBg; border.color: Theme.borderLight; radius: Theme.radiusMd }
        }
    }

    function deviceStems() {
        return DeviceService.connected
            ? Object.keys(DeviceService.photoOnDeviceKeys) : []
    }
    function stemOf(name) {
        const dot = name.lastIndexOf(".")
        return (dot > 0 ? name.substring(0, dot) : name).toLowerCase()
    }
    function isOnZune(filename) {
        return DeviceService.connected
            && (DeviceService.photoOnDeviceKeys[stemOf(filename)] ?? false)
    }
    function refresh() {
        albums = LibraryService.photoAlbums(deviceStems())
        customAlbums = LibraryService.customPhotoAlbums(deviceStems())
        if (openCustomId > 0) goToCustom(customAlbums.some(a => a.id === openCustomId) ? openCustomId : 0)
        if (openAlbumPath !== "")
            albumPhotos = LibraryService.photosInAlbum(openAlbumPath)
    }
    Component.onCompleted: refresh()
    Connections {
        target: LibraryService
        function onLibraryChanged() { root.refresh() }
    }
    // On-device badges track connects/purges/mid-sync sends. Gate on
    // the photo keys ACTUALLY changing — stateChanged fires constantly
    // (battery, storage, …) and each ungated refresh rebuilt every
    // delegate, redecoding every image: the "flash" on these tabs.
    property int _lastKeyCount: -1
    Connections {
        target: DeviceService
        function onStateChanged() {
            const n = Object.keys(DeviceService.photoOnDeviceKeys).length
                      + (DeviceService.connected ? 1 : 0)
            if (n !== root._lastKeyCount) {
                root._lastKeyCount = n
                root.refresh()
            }
        }
    }

    readonly property var shownAlbums: (pivot === "Albums" ? childAlbums : albums)
        .filter(a => search === "" || a.name.toLowerCase().includes(search.toLowerCase()))

    function openAlbum(a) {
        if (a.id !== undefined) { goToCustom(a.id); return }
        openCustomId = 0
        openAlbumPath = a.path
        openAlbumName = a.name
        albumPhotos = LibraryService.photosInAlbum(a.path)
    }

    // ── Queue for device (mirrors the video views' honest toasts) ──
    function toastAddResult(r) {
        if (r.error)
            toastHost.show(r.error)
        else if (r.added > 0)
            toastHost.show("queued " + (r.added === 1 ? "1 photo"
                           : r.added + " photos") + " for the device")
        else if (r.rejected === 0 && r.onDevice > 0)
            toastHost.show(r.onDevice === 1
                           ? "already on the zune"
                           : "all " + r.onDevice + " already on the zune")
        else if (r.rejected === 0 && r.duplicates > 0)
            toastHost.show(r.duplicates === 1
                           ? "already in the queue"
                           : "all " + r.duplicates + " already in the queue")
    }
    function photoSyncItem(p, albumName) {
        return {
            filepath: decodeURIComponent(String(p.url).replace("file://", "")),
            filename: p.filename,
            album: albumName,
            filesize: p.filesize,
            libraryId: p.id
        }
    }
    function queuePhotos(list, albumName) {
        if (!DeviceService.connected)
            return
        toastAddResult(SyncEngine.addPhotos(
            list.map(p => root.photoSyncItem(p, albumName))))
    }
    function openGallery(list, startIndex, albumName) {
        gallery.open(list, startIndex, null, {
            deviceMode: false,
            localAlbumDrag: true,
            action: photo => root.queuePhotos([photo], albumName),
            payload: photo => root.localDragItem(photo, albumName)
        })
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header band — fixed height, on the spine (2026-09-06) ──
        PageHeaderBand {
            Layout.leftMargin: root.headerSpineX
            Layout.rightMargin: root.headerSpineX
            spacing: Theme.spaceMd

            // Same tab language as every other section; the band
            // NEVER changes — drills add a breadcrumb row below
            PivotBar {
                Layout.preferredWidth: implicitWidth
                Layout.alignment: Qt.AlignVCenter
                tabs: ["Folders", "Albums"]
                current: root.pivot
                onCurrentChanged: if (current !== root.pivot) root.switchPivot(current)
            }
            AlbumButton {
                objectName: "newPhotoAlbum"
                title: root.openCustomId > 0 ? "New subalbum" : "New album"
                visible: root.pivot === "Albums"
                onClicked: PhotoTrayState.openNew(root.openCustomId)
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: root.width > Theme.spaceHuge * 16
                text: !root.inAlbum
                    ? root.shownAlbums.length + (root.pivot === "Albums" ? " albums · " : " folders · ")
                      + LibraryService.photoCount + " photos"
                    : root.albumPhotos.length + " photos"
                font.pixelSize: 12
                color: Theme.textDim
            }
            ZuneSearchField {
                objectName: "photoAlbumSearch"
                text: root.search
                visible: !root.inAlbum && root.width > Theme.spaceHuge * 12
                Layout.preferredWidth: 160
                Layout.minimumWidth: 110
                onTextChanged: root.search = text
            }
        }

        // Breadcrumb under the band (music's drill pattern)
        MusicBreadcrumbBar {
            Layout.fillWidth: true
            visible: root.inAlbum && root.pivot === "Folders"
            inset: Gallery.spine(root.width)
            crumbs: ["Folders", root.openAlbumName]
            onBackClicked: root.openAlbumPath = ""
            onCrumbClicked: root.openAlbumPath = ""
        }

        Flickable {
            visible: root.openCustomId > 0
            Layout.fillWidth: true
            Layout.leftMargin: root.headerSpineX
            Layout.rightMargin: root.headerSpineX
            Layout.preferredHeight: Theme.spaceHuge
            contentWidth: customCrumbs.width
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            Row {
                id: customCrumbs
                height: parent.height
                spacing: Theme.spaceSm
                AlbumButton {
                    title: "‹ back"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: root.goToCustom(root.albumTrail.length > 1 ? root.albumTrail[root.albumTrail.length - 2].id : 0)
                }
                Repeater {
                    model: [{id:0, name:"Albums"}].concat(root.albumTrail)
                    delegate: Text {
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, root.width / 3)
                        text: modelData.name + " /"
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.goToCustom(parent.modelData.id)
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: root.openCustomId > 0
            Layout.leftMargin: root.headerSpineX
            Layout.rightMargin: root.headerSpineX
            spacing: Theme.spaceMd
            AlbumButton {
                objectName: "addAlbumPhotos"
                title: "Edit / Add photos"
                onClicked: root.pickLibraryPhotos()
            }
            AlbumButton {
                title: "Album options"
                onClicked: {
                    libAlbumMenu.target = root.customAlbums.find(a => a.id === root.openCustomId)
                    libAlbumMenu.popup()
                }
            }
            Text { visible: root.width > Theme.spaceHuge * 14; text: "Photos in this album only"; color: Theme.textDim }
        }

        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: root.headerSpineX
            Layout.rightMargin: root.headerSpineX
            Layout.preferredHeight: childrenRect.height
            spacing: Theme.spaceSm
            visible: root.inAlbum || root.selectedCount > 0
            AlbumButton {
                objectName: "photoSelectionToggle"
                title: root.selecting ? "Done selecting" : "Select photos"
                onClicked: { root.selecting = !root.selecting; root.forceActiveFocus() }
            }
            AlbumButton {
                objectName: "photoSelectAll"
                title: "Select all"
                visible: root.inAlbum
                onClicked: { root.selectAllPhotos(); root.forceActiveFocus() }
            }
            AlbumButton {
                objectName: "photoSelectionClear"
                title: "Clear " + root.selectedCount
                visible: root.selectedCount > 0
                onClicked: root.clearSelection()
            }
            AlbumButton {
                objectName: "photoSelectionAdd"
                title: "Add " + root.selectedCount + " photos"
                kind: "primary"
                visible: root.selectedCount > 0
                onClicked: root.addToDraft(root.selectedRows)
            }
        }

        ListView {
            visible: root.openCustomId > 0 && root.childAlbums.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: root.headerSpineX
            Layout.rightMargin: root.headerSpineX
            Layout.preferredHeight: Theme.spaceHuge
            orientation: ListView.Horizontal
            spacing: Theme.spaceSm
            clip: true
            model: root.childAlbums
            delegate: AlbumButton {
                required property var modelData
                objectName: "photoSubalbum" + modelData.id
                title: "▣ " + (modelData.name.length > 28 ? modelData.name.substring(0, 28) + "…" : modelData.name)
                ToolTip.visible: hovering
                ToolTip.text: modelData.name
                onClicked: root.goToCustom(modelData.id)
            }
        }

        // ── Album cards ──
        GridView {
            // Scrollbar at the PAGE edge — one x across all sections
            ScrollBar.vertical: ZuneScrollBar {
                parent: root
                visible: !root.inAlbum && size < 1.0
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
            id: albumGrid
            // The fan's back covers rise above the front card (offset
            // + rotation) — without top air the first row gets shaved
            topMargin: Theme.spaceLg
            bottomMargin: Theme.spaceLg
            visible: !root.inAlbum
            // Explicit centered width = cols × cellWidth: the only way
            // to pin GridView's column count (grid study 2026-09-06)
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: root.headerSpineX
            Layout.preferredWidth: root.albumPlan.cols * cellWidth
            clip: true
            cellWidth: root.albumPlan.cell + Gallery.gutter
            cellHeight: root.albumPlan.cell + 40 + Gallery.rowGap
            model: root.shownAlbums
            delegate: Item {
                id: albumCard
                required property var modelData
                readonly property real cellW: root.albumPlan.cell
                width: cellW
                height: cellW + 40
                readonly property int coverCount:
                    Math.min((modelData.covers ?? []).length, 3)

                Rectangle {
                    visible: albumCard.coverCount === 0
                    width: albumCard.cellW - Theme.spaceLg
                    height: width
                    x: Theme.spaceSm
                    y: Theme.spaceSm
                    radius: Theme.radiusMd
                    color: Theme.cardActive
                    border.color: Theme.glassBorder
                    Text {
                        anchors.centerIn: parent
                        text: "▣"
                        color: Theme.textDim
                        font.pixelSize: Theme.spaceHuge
                    }
                }

                // Stacked covers: up to 3 cards fanned slightly, the
                // front one carrying the newest cover.
                Repeater {
                    model: albumCard.coverCount
                    delegate: Rectangle {
                        id: fanCover
                        required property int index
                        readonly property int back: albumCard.coverCount - 1 - index
                        width: albumCard.cellW - 16
                        height: albumCard.cellW - 16
                        x: 8 + back * 4
                        y: 8 - back * 4
                        radius: Theme.radiusMd
                        rotation: back * 2.5
                        color: Qt.rgba(1, 1, 1, 0.05)
                        border.color: Theme.glassBorder
                        clip: true
                        // Rounded ART needs a mask — a radius on the container
                        // doesn't clip the Image (QML clip is rectangular)
                        layer.enabled: true
                        layer.effect: OpacityMask {
                            maskSource: Rectangle {
                                width: fanCover.width; height: fanCover.height
                                radius: Theme.radiusMd
                            }
                        }
                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: albumCard.modelData.covers[index] ?? ""
                            sourceSize.width: Math.round(albumCard.cellW * 2)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                }

                // Fully-synced album → zune badge; partial → count text
                OnDeviceBadge {
                    size: 24
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    visible: DeviceService.connected
                             && albumCard.modelData.onZune > 0
                             && albumCard.modelData.onZune
                                >= albumCard.modelData.count
                }
                Column {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    spacing: 1
                    Text {
                        text: albumCard.modelData.name
                        font.pixelSize: 14
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        width: albumCard.cellW - 20
                    }
                    Text {
                        text: {
                            const oz = albumCard.modelData.onZune ?? 0
                            const base = albumCard.modelData.count + " photos"
                            return DeviceService.connected && oz > 0
                                && oz < albumCard.modelData.count
                                ? base + " · " + oz + " on zune"
                                : base
                        }
                        font.pixelSize: 11
                        color: Theme.textDim
                    }
                }
                MouseArea {
                    id: paRootArea
                    objectName: "photoFolderDrag"
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    // Local albums accept photos offline; device drops still require connection.
                    readonly property bool dragArmed: true
                    drag.target: dragArmed ? photoAlbumGhost : null
                    preventStealing: dragArmed
                    onPressed: mouse => photoAlbumGhost.place(paRootArea, mouse)
                    onReleased: photoAlbumGhost.drop()
                    onClicked: mouseEvent => {
                        if (mouseEvent.button === Qt.RightButton) {
                            libAlbumMenu.target = albumCard.modelData
                            libAlbumMenu.popup()
                        } else {
                            root.openAlbum(albumCard.modelData)
                        }
                    }
                    z: -1
                }

                // Contract tile anatomy, photo variant: ⌕ ZOOMS into
                // the album's gallery (photos don't "play"),
                // ＋ queues the album, ▾ picks photos
                readonly property bool cardHover:
                    paRootArea.containsMouse || paPlayArea.containsMouse
                    || paSplit.hovered
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 100 - 22
                    width: 44
                    height: 44
                    radius: 22
                    color: paPlayArea.containsMouse
                        ? Qt.alpha(Theme.pink, 0.92) : Qt.rgba(0, 0, 0, 0.55)
                    border.width: 1
                    border.color: paPlayArea.containsMouse
                        ? Theme.pink : Theme.activePink
                    opacity: albumCard.cardHover ? 1 : 0
                    scale: paPlayArea.pressed ? 0.88
                         : paPlayArea.containsMouse ? 1.12
                         : albumCard.cardHover ? 1 : 0.8
                    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                    Behavior on scale { NumberAnimation { duration: Theme.motionFast } }
                    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                    Text {
                        anchors.centerIn: parent
                        text: "⌕"
                        font.pixelSize: 20
                        font.bold: true
                        color: "white"
                    }
                    MouseArea {
                        id: paPlayArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.openAlbum(albumCard.modelData)
                            root.openGallery(
                                root.photosFor(albumCard.modelData), 0, albumCard.modelData.name)
                        }
                    }
                }
                DragGhost {
                    id: photoAlbumGhost
                    area: paRootArea
                    dragKind: "photos"
                    localPhotoDrag: true
                    dragName: albumCard.modelData.name
                    dragTracks: () => root.photosFor(albumCard.modelData)
                        .map(p => root.localDragItem(p, albumCard.modelData.name))

                    Rectangle {
                        width: 64
                        height: 64
                        radius: Theme.radiusMd
                        color: Theme.cardActive
                        border.width: 1
                        border.color: Qt.alpha(Theme.purple, 0.75)
                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: (albumCard.modelData.covers ?? []).length > 0
                                    ? albumCard.modelData.covers[0] : ""
                            fillMode: Image.PreserveAspectCrop
                            visible: (albumCard.modelData.covers ?? []).length > 0
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: (albumCard.modelData.covers ?? []).length === 0
                            text: "▣"
                            font.pixelSize: 20
                            color: Theme.textDim
                        }
                        Rectangle {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            width: folderCount.implicitWidth + Theme.spaceMd; height: folderCount.implicitHeight + Theme.spaceXs
                            radius: Theme.radiusSm; color: Theme.cardActive
                            Text { id: folderCount; anchors.centerIn: parent; text: albumCard.modelData.count; color: Theme.textPrimary }
                        }
                    }
                }

                SplitAddButton {
                    id: paSplit
                    objectName: "folderPhotoAdd"
                    armed: PhotoTrayState.building
                    ToolTip.visible: hovered
                    ToolTip.text: PhotoTrayState.building ? "Add photos to album draft" : "Queue photos for device"
                    visible: PhotoTrayState.building || DeviceService.connected
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.topMargin: 4
                    opacity: albumCard.cardHover ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                    onAddAll: PhotoTrayState.building
                        ? root.addToDraft(root.photosFor(albumCard.modelData))
                        : root.queuePhotos(root.photosFor(albumCard.modelData), albumCard.modelData.name)
                    onPickParts: {
                        if (!PhotoTrayState.building && !DeviceService.connected) return
                        root._pickerForDraft = PhotoTrayState.building
                        root._pickerDraftId = PhotoTrayState.generation
                        root._pickerPhotos = root.photosFor(albumCard.modelData)
                        root._pickerAlbumName = albumCard.modelData.name
                        photoPicker.openAt(albumCard, albumCard.modelData.name,
                            root._pickerPhotos.map((p, i) => ({
                                id: String(i),
                                label: p.filename,
                                sublabel: ""
                            })))
                    }
                }
            }
            Text {
                visible: root.shownAlbums.length === 0
                anchors.centerIn: parent
                text: root.pivot === "Albums" ? "Create an album, then browse folders and drag or select photos"
                    : "no photos yet — add a photo watch folder in settings"
                font.pixelSize: 14
                color: Theme.textDim
            }
        }

        // ── Photo grid (drill-down) ──
        GridView {
            // Scrollbar at the PAGE edge — one x across all sections
            ScrollBar.vertical: ZuneScrollBar {
                parent: root
                visible: root.inAlbum && size < 1.0
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
            id: photoGrid
            objectName: "photoGrid"
            visible: root.inAlbum
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: root.headerSpineX
            Layout.preferredWidth: root.thumbPlan.cols * cellWidth
            clip: true
            cellWidth: root.thumbPlan.cell + 24
            cellHeight: root.thumbPlan.cell + 24
            model: root.albumPhotos
            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spaceXxxl
                visible: root.albumPhotos.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                objectName: "photoAlbumEmptyState"
                text: root.openCustomId > 0
                    ? "No photos in this album yet. Choose Add photos to select from your library."
                    : "No photos in this folder"
                color: Theme.textDim
            }
            delegate: Rectangle {
                id: photoCellDelegate
                required property var modelData
                required property int index
                width: root.thumbPlan.cell
                height: root.thumbPlan.cell
                radius: Theme.radiusMd
                color: Qt.rgba(1, 1, 1, 0.04)
                border.width: selected ? Theme.spaceXxs : 1
                readonly property bool selected: root.selectedPhotos[modelData.id] !== undefined
                border.color: selected || photoArea.containsMouse
                    ? Theme.activePink : Theme.glassBorder
                clip: true
                // Rounded ART needs a mask — a radius on the container
                // doesn't clip the Image (QML clip is rectangular)
                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: photoCellDelegate.width; height: photoCellDelegate.height
                        radius: Theme.radiusMd
                    }
                }
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: parent.modelData.url
                    sourceSize.width: Math.round(photoCellDelegate.width * 2)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                OnDeviceBadge {
                    size: 20
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 4
                    visible: root.isOnZune(parent.modelData.filename)
                }
                Rectangle {
                    visible: root.selecting || photoCellDelegate.selected
                    z: 2
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Theme.spaceSm
                    width: Theme.spaceXl; height: width; radius: Theme.radiusSm
                    color: photoCellDelegate.selected ? Theme.pink : Theme.cardActive
                    border.color: Theme.glassBorder
                    Text { anchors.centerIn: parent; text: photoCellDelegate.selected ? "✓" : ""; color: Theme.textPrimary }
                }
                // Single photo: plain ＋ (no ▾ on one file). Hover is
                // OR'd with the button's own (flicker rule).
                SplitAddButton {
                    id: photoCellAdd
                    objectName: "libraryPhotoAdd"
                    armed: PhotoTrayState.building
                    ToolTip.visible: hovered
                    ToolTip.text: PhotoTrayState.building ? "Add photo to album draft" : "Queue photo for device"
                    visible: PhotoTrayState.building || DeviceService.connected
                    z: 1
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    showPick: false
                    opacity: (photoArea.containsMouse || hovered) ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                    onAddAll: PhotoTrayState.building ? root.addToDraft([parent.modelData])
                        : root.queuePhotos([parent.modelData], root.openAlbumName)
                }
                MouseArea {
                    id: photoArea
                    objectName: "photoArea" + photoCellDelegate.modelData.id
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouseEvent => {
                        if (mouseEvent.button === Qt.RightButton) {
                            libPhotoMenu.target = parent.modelData
                            libPhotoMenu.popup()
                        } else {
                            if (root.selecting || (mouseEvent.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                                root.selectPhoto(parent.index, mouseEvent.modifiers)
                            else root.openGallery(root.albumPhotos, parent.index, root.openAlbumName)
                        }
                    }
                    // Selected photos travel together; local album drops also work offline.
                    readonly property bool dragArmed: true
                    drag.target: dragArmed ? photoCellGhost : null
                    preventStealing: dragArmed
                    onPressed: mouse => { root.forceActiveFocus(); photoCellGhost.place(photoArea, mouse) }
                    onReleased: photoCellGhost.drop()
                }
                DragGhost {
                    id: photoCellGhost
                    area: photoArea
                    dragKind: "photos"
                    localPhotoDrag: true
                    dragName: root.dragRows(photoCellDelegate.modelData).length + " photos"
                    dragTracks: () => root.dragRows(photoCellDelegate.modelData).map(p => root.localDragItem(p, root.openAlbumName))
                    Repeater {
                        model: Math.max(0, Math.min(2, root.dragRows(photoCellDelegate.modelData).length - 1))
                        delegate: Rectangle {
                            required property int index
                            width: Theme.spaceHuge + Theme.spaceMd; height: width
                            x: (index + 1) * Theme.spaceXs; y: -(index + 1) * Theme.spaceXs
                            rotation: (index + 1) * 3
                            radius: Theme.radiusMd; color: Theme.cardActive; border.color: Theme.glassBorder
                            Image {
                                anchors.fill: parent; anchors.margins: Theme.hairline
                                source: root.dragRows(photoCellDelegate.modelData)[index + 1]?.url ?? ""
                                fillMode: Image.PreserveAspectCrop
                                sourceSize.width: Theme.spaceHuge + Theme.spaceMd
                            }
                        }
                    }
                    Rectangle {
                        width: 64; height: 64; radius: Theme.radiusMd
                        color: Theme.cardActive
                        border.width: 1
                        border.color: Qt.alpha(Theme.activePink, 0.75)
                        clip: true
                        Image {
                            anchors.fill: parent; anchors.margins: 1
                            source: photoCellDelegate.modelData.url
                            fillMode: Image.PreserveAspectCrop
                        }
                        Rectangle {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            width: countText.implicitWidth + Theme.spaceMd; height: countText.implicitHeight + Theme.spaceXs
                            radius: Theme.radiusSm; color: Theme.cardActive
                            Text { id: countText; anchors.centerIn: parent; text: root.dragRows(photoCellDelegate.modelData).length; color: Theme.textPrimary }
                        }
                    }
                }
            }
        }
    }

    // ── Photo picker (▾ on album cards) ──
    property bool _pickerForDraft: false
    property double _pickerDraftId: -1
    property var _pickerPhotos: []
    property string _pickerAlbumName: ""
    QuickPicker {
        id: photoPicker
        objectName: "photoDeviceOrDraftPicker"
        actionWord: root._pickerForDraft ? "add to album" : "queue"
        onPicked: ids => {
            const set = {}
            for (const i of ids) set[i] = true
            const rows = root._pickerPhotos.filter((p, i) => set[String(i)])
            if (root._pickerForDraft) {
                if (PhotoTrayState.building && PhotoTrayState.generation === root._pickerDraftId) PhotoTrayState.addPhotos(rows)
            } else root.queuePhotos(rows, root._pickerAlbumName)
        }
    }

    Popup {
        id: albumEditor
        objectName: "photoAlbumEditor"
        anchors.centerIn: Overlay.overlay
        width: Math.min(root.width - Theme.spaceXxxl, Theme.spaceHuge * 10)
        modal: true
        background: Rectangle {
            color: Theme.cardActive
            radius: Theme.radiusMd
            border.color: Theme.glassBorder
        }
        property string mode: "move"
        property var target: null
        property string error: ""
        readonly property var destinations: (mode === "move" ? [{id: 0, pathLabel: "Top level"}] : [])
            .concat(root.customAlbums.filter(a => {
                if (mode !== "move" || !target) return true
                let cursor = a
                while (cursor) {
                    if (cursor.id === target.id) return false
                    cursor = root.customAlbums.find(row => row.id === cursor.parentId)
                }
                return true
            }))
        readonly property string title: mode === "move" ? "Move photo album" : "Delete photo album"
        function save() {
            let result
            if (mode === "delete") result = LibraryService.deletePhotoAlbum(target.id)
            else {
                const destination = destinations[albumDestination.currentIndex]
                if (!destination) { error = "Choose a destination."; return }
                result = LibraryService.movePhotoAlbum(target.id, destination.id)
            }
            if (result.success) close()
            else error = result.error || "Could not save photo album"
        }
        contentItem: ColumnLayout {
            spacing: Theme.spaceMd
            Text {
                Layout.fillWidth: true
                text: albumEditor.title
                color: Theme.textPrimary
                wrapMode: Text.Wrap
            }
            AlbumCombo {
                id: albumDestination
                objectName: "photoAlbumDestination"
                visible: albumEditor.mode === "move"
                Layout.fillWidth: true
                model: albumEditor.destinations
                textRole: "pathLabel"
            }
            Label {
                visible: albumEditor.mode === "delete"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                text: "Delete this album and all its subalbums? Photos stay in your library and on disk."
            }
            Label {
                visible: albumEditor.error !== ""
                text: albumEditor.error
                color: Theme.pink
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AlbumButton { objectName: "photoAlbumCancel"; title: "Cancel"; onClicked: albumEditor.close() }
                AlbumButton {
                    objectName: "photoAlbumSave"
                    kind: "primary"
                    title: albumEditor.mode === "delete" ? "Delete albums" : "Save"
                    onClicked: albumEditor.save()
                }
            }
        }
    }

    // ── Context menus ──
    ZuneMenu {
        id: libPhotoMenu
        property var target: null
        ZuneMenuItem {
            text: "Queue for Device"
            visible: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: root.queuePhotos([libPhotoMenu.target],
                                          root.openAlbumName)
        }
        ZuneMenuItem {
            text: "Add to photo album…"
            onTriggered: root.addToDraft([libPhotoMenu.target])
        }
        ZuneMenuItem {
            text: "Edit album membership…"
            visible: root.openCustomId > 0
            height: visible ? implicitHeight : 0
            onTriggered: root.editCustomAlbum(root.openCustomId, false)
        }
        ZuneMenuItem {
            text: "Arrange album…"
            visible: root.openCustomId > 0
            height: visible ? implicitHeight : 0
            onTriggered: root.editCustomAlbum(root.openCustomId, false)
        }
        ZuneMenuItem {
            text: "Remove from Library"
            onTriggered: LibraryService.deletePhotos([libPhotoMenu.target.id])
        }
    }
    ZuneMenu {
        id: libAlbumMenu
        property var target: null
        ZuneMenuItem {
            text: "Queue Album for Device"
            visible: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: root.queuePhotos(
                root.photosFor(libAlbumMenu.target),
                libAlbumMenu.target.name)
        }
        ZuneMenuItem {
            text: "Add photos to album…"
            onTriggered: {
                root.addToDraft(root.photosFor(libAlbumMenu.target))
            }
        }
        ZuneMenuItem {
            text: "Edit album…"
            visible: libAlbumMenu.target !== null && libAlbumMenu.target.id !== undefined
            height: visible ? implicitHeight : 0
            onTriggered: root.editCustomAlbum(libAlbumMenu.target.id, false)
        }
        ZuneMenuItem {
            text: "Move album…"
            visible: libAlbumMenu.target !== null && libAlbumMenu.target.id !== undefined
            height: visible ? implicitHeight : 0
            onTriggered: root.editAlbum("move", libAlbumMenu.target)
        }
        ZuneMenuItem {
            text: "Delete album and subalbums…"
            visible: libAlbumMenu.target !== null && libAlbumMenu.target.id !== undefined
            height: visible ? implicitHeight : 0
            onTriggered: root.editAlbum("delete", libAlbumMenu.target)
        }
        ZuneMenuItem {
            text: "Remove Folder Photos from Library"
            visible: libAlbumMenu.target !== null && libAlbumMenu.target.id === undefined
            height: visible ? implicitHeight : 0
            onTriggered: LibraryService.deletePhotos(
                LibraryService.photosInAlbum(libAlbumMenu.target.path)
                    .map(p => p.id))
        }
    }
}
