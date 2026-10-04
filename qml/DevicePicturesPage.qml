import QtQuick
import Qt5Compat.GraphicalEffects
import QtQuick.Controls
import QtQuick.Layouts
import Zuuned

// Device photos — Phase 8, mirror of LibraryPhotosView's drill-down:
// device folder-albums (ZMDB photo albums + parentId grouping) →
// thumbnail grid → the shared PhotoGalleryView. Full-res in the
// gallery comes via the resolver (USB extract → cache, thumb shown
// meanwhile). Right-click everywhere: save to library / delete.
Item {
    id: root

    // Gallery plans from ROOT width (loop-safe); grids take an
    // explicit centered width — GridView margins can't pin columns.
    readonly property var albumPlan: Gallery.plan(
        Math.max(220, width - 48), 220, 7)
    readonly property var thumbPlan: Gallery.plan(
        Math.max(220, width - 48), 200, 8)
    // ONE fixed spine (2026-09-06): header and both grids anchor to
    // the album plan's edge — drilling into an album never shifts x
    readonly property real headerSpineX: Gallery.spine(width)

    property var gallery   // PhotoGalleryView instance (Main.qml)

    // ── Album grouping: parentId → album; unknown parents pool under
    //    the root "photos" album ──
    readonly property var albums: {
        const byId = {}
        for (const a of DeviceService.photoAlbumsList)
            byId[a.itemId] = { itemId: a.itemId, name: a.name, photos: [] }
        const rootAlbum = { itemId: 0, name: "photos", photos: [] }
        for (const p of DeviceService.photosList) {
            const home = byId[p.parentId]
            if (home !== undefined)
                home.photos.push(p)
            else
                rootAlbum.photos.push(p)
        }
        const out = []
        for (const k in byId)
            if (byId[k].photos.length > 0)
                out.push(byId[k])
        out.sort((a, b) => a.name.localeCompare(b.name))
        if (rootAlbum.photos.length > 0)
            out.push(rootAlbum)
        return out
    }

    property var openAlbum: null
    readonly property var shownPhotos: openAlbum ? openAlbum.photos : []

    // Keep openAlbum fresh across breaches/purges
    onAlbumsChanged: {
        if (!openAlbum)
            return
        const again = albums.find(a => a.name === openAlbum.name)
        openAlbum = again ?? null
    }

    function thumbFor(itemId) {
        return DeviceService.artPaths[String(itemId)] ?? ""
    }
    // Gallery items + the USB full-res resolver
    function openGallery(startIdx) {
        const subdir = root.openAlbum && root.openAlbum.itemId !== 0
            ? root.openAlbum.name : ""
        const list = root.shownPhotos.map(p => ({
            url: root.thumbFor(p.itemId),
            filename: p.name,
            itemId: p.itemId
        }))
        root.gallery.open(list, startIdx, function(idx, cb) {
            if (!DeviceService.connected) return
            const id = list[idx].itemId
            root._resolveCbs[String(id)] = cb
            DeviceService.requestPhotoFull(id)
        }, {
            deviceMode: true,
            action: photo => root.saveOne({itemId: photo.itemId, name: photo.filename}, subdir),
            payload: photo => ({itemId: photo.itemId, name: photo.filename})
        })
    }
    property var _resolveCbs: ({})
    property int pendingSaves: 0
    Connections {
        target: DeviceService
        function onPhotoFullReady(itemId, url) {
            const cb = root._resolveCbs[String(itemId)]
            if (cb) {
                delete root._resolveCbs[String(itemId)]
                cb(url)
            }
        }
        function onPhotoSaved(filename, ok) {
            if (root.pendingSaves > 0)
                root.pendingSaves--
            toastHost.show(ok ? "saved " + filename + " to library"
                              : "failed to save " + filename)
            // Last one in: make the imports folder a scanned photo
            // source so downloads appear in the library immediately
            // (the videos page's pattern).
            if (root.pendingSaves === 0 && ok)
                LibraryService.addWatchFolder(DeviceService.photoImportsDir(),
                                              "photos")
        }
    }
    function saveOne(p, subdir) {
        if (!DeviceService.connected || !p || !p.itemId) return
        root.pendingSaves++
        DeviceService.savePhotoToLibrary(p.itemId, p.name, subdir ?? "")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header band — fixed height, on the spine (2026-09-06)
        PageHeaderBand {
            Layout.leftMargin: root.headerSpineX
            Layout.rightMargin: root.headerSpineX
            spacing: Theme.spaceMd

            // Same tab language as every other section; the band
            // NEVER changes — drills add a breadcrumb row below
            PivotBar {
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignVCenter
                tabs: ["Albums"]
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.openAlbum === null
                    ? root.albums.length + " albums · "
                      + DeviceService.photosList.length + " photos"
                    : root.shownPhotos.length + " photos"
                font.pixelSize: 12
                color: Theme.textDim
            }
        }

        // Breadcrumb under the band + the album's action on its right
        RowLayout {
            Layout.fillWidth: true
            visible: root.openAlbum !== null
            spacing: 0
            MusicBreadcrumbBar {
                Layout.fillWidth: true
                inset: Gallery.spine(root.width)
                crumbs: ["Albums", root.openAlbum ? root.openAlbum.name : ""]
                onBackClicked: root.openAlbum = null
                onCrumbClicked: root.openAlbum = null
            }
            Text {
                Layout.rightMargin: Theme.spaceXl
                visible: DeviceService.connected
                text: "save album to library"
                font.pixelSize: 12
                color: saveAllArea.containsMouse ? Theme.activePink : Theme.pink
                MouseArea {
                    id: saveAllArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!DeviceService.connected) return
                        for (const p of root.shownPhotos)
                            root.saveOne(p, root.openAlbum.itemId === 0
                                             ? "" : root.openAlbum.name)
                        toastHost.show("saving " + root.shownPhotos.length
                                       + " photos…")
                    }
                }
            }
        }

        // ── Album cards ──
        GridView {
            // Scrollbar at the PAGE edge — one x across all sections
            ScrollBar.vertical: ZuneScrollBar {
                parent: root
                visible: root.openAlbum === null && size < 1.0
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
            id: devAlbumGrid
            // The fan's back covers rise above the front card (offset
            // + rotation) — without top air the first row gets shaved
            topMargin: Theme.spaceLg
            bottomMargin: Theme.spaceLg
            visible: root.openAlbum === null
            // Explicit centered width = cols × cellWidth (grid study)
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: root.headerSpineX
            Layout.preferredWidth: root.albumPlan.cols * cellWidth
            clip: true
            cellWidth: root.albumPlan.cell + Gallery.gutter
            cellHeight: root.albumPlan.cell + 40 + Gallery.rowGap
            model: root.albums
            delegate: Item {
                id: devAlbumCard
                required property var modelData
                readonly property real cellW: root.albumPlan.cell
                width: cellW
                height: cellW + 40

                // Stacked covers, fanned — same shelf language as the
                // library's photo albums (approved 2026-09-07: the fan
                // SAYS "there's more than one in here")
                readonly property int coverCount:
                    Math.min(modelData.photos.length, 3)
                Repeater {
                    model: devAlbumCard.coverCount
                    delegate: Rectangle {
                        id: devFanCover
                        required property int index
                        readonly property int back:
                            devAlbumCard.coverCount - 1 - index
                        width: devAlbumCard.cellW - 16
                        height: devAlbumCard.cellW - 16
                        x: 8 + back * 4
                        y: 8 - back * 4
                        radius: Theme.radiusMd
                        rotation: back * 2.5
                        color: Theme.artworkPlaceholder
                        border.color: Theme.glassBorder
                        clip: true
                        // Rounded ART needs a mask — a radius on the container
                        // doesn't clip the Image (QML clip is rectangular)
                        layer.enabled: true
                        layer.effect: OpacityMask {
                            maskSource: Rectangle {
                                width: devFanCover.width; height: devFanCover.height
                                radius: Theme.radiusMd
                            }
                        }
                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: root.thumbFor(
                                devAlbumCard.modelData.photos[index]?.itemId ?? 0)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            Component.onCompleted: {
                                const ph = devAlbumCard.modelData.photos[index]
                                if (ph)
                                    DeviceService.requestPhotoThumb(ph.itemId)
                            }
                        }
                    }
                }
                Column {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    spacing: 1
                    Text {
                        text: devAlbumCard.modelData.name
                        font.pixelSize: 14
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        width: devAlbumCard.cellW - 20
                    }
                    Text {
                        text: devAlbumCard.modelData.photos.length + " photos"
                        font.pixelSize: 11
                        color: Theme.textDim
                    }
                }
                MouseArea {
                    id: albumArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouseEvent => {
                        if (mouseEvent.button === Qt.RightButton) {
                            albumMenu.target = devAlbumCard.modelData
                            albumMenu.popup()
                        } else {
                            root.openAlbum = devAlbumCard.modelData
                        }
                    }
                    // Drag device→library: the whole album's photos.
                    readonly property bool dragArmed: DeviceService.connected
                    drag.target: dragArmed ? albumGhost : null
                    preventStealing: dragArmed
                    onPressed: mouse => albumGhost.place(albumArea, mouse)
                    onReleased: albumGhost.drop()
                }
                DragGhost {
                    id: albumGhost
                    area: albumArea
                    dragKind: "device-photos"
                    dragTracks: () => devAlbumCard.modelData.photos.map(p => ({
                        itemId: p.itemId, name: p.name }))
                    // Ghost = the album's cover thumb (its first photo),
                    // the replica of the album card.
                    Rectangle {
                        width: 64; height: 64; radius: Theme.radiusMd
                        color: Theme.artworkPlaceholder
                        border.width: 1
                        border.color: Qt.alpha(Theme.pink, 0.75)
                        clip: true
                        Image {
                            anchors.fill: parent; anchors.margins: 1
                            source: devAlbumCard.modelData.photos.length > 0
                                ? root.thumbFor(devAlbumCard.modelData.photos[0].itemId)
                                : ""
                            fillMode: Image.PreserveAspectCrop
                        }
                    }
                }
            }
            Text {
                visible: root.albums.length === 0
                anchors.centerIn: parent
                text: DeviceService.connected
                    ? "no pictures on this zune"
                    : "connect a zune to browse its pictures"
                font.pixelSize: 14
                color: Theme.textDim
            }
        }

        // ── Photo grid ──
        GridView {
            // Scrollbar at the PAGE edge — one x across all sections
            ScrollBar.vertical: ZuneScrollBar {
                parent: root
                visible: root.openAlbum !== null && size < 1.0
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
            visible: root.openAlbum !== null
            id: devPhotoGrid
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignLeft
            Layout.leftMargin: root.headerSpineX
            Layout.preferredWidth: root.thumbPlan.cols * cellWidth
            clip: true
            cellWidth: root.thumbPlan.cell + 24
            cellHeight: root.thumbPlan.cell + 24
            model: root.shownPhotos
            delegate: Rectangle {
                id: devPhotoCell
                required property var modelData
                required property int index
                width: root.thumbPlan.cell
                height: root.thumbPlan.cell
                radius: Theme.radiusMd
                color: Qt.rgba(1, 1, 1, 0.04)
                border.color: cellArea.containsMouse
                    ? Theme.activePink : Theme.glassBorder
                clip: true
                // Rounded ART needs a mask — a radius on the container
                // doesn't clip the Image (QML clip is rectangular)
                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: devPhotoCell.width; height: devPhotoCell.height
                        radius: Theme.radiusMd
                    }
                }
                Component.onCompleted:
                    DeviceService.requestPhotoThumb(modelData.itemId)
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: root.thumbFor(devPhotoCell.modelData.itemId)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                // Device photo: plain ⤓ quick-save (single item)
                SplitAddButton {
                    objectName: "devicePhotoSave"
                    visible: DeviceService.connected
                    z: 1
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    glyph: "⤓"
                    showPick: false
                    opacity: (cellArea.containsMouse || hovered) ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                    onAddAll: root.saveOne(devPhotoCell.modelData,
                        root.openAlbum && root.openAlbum.itemId !== 0
                            ? root.openAlbum.name : "")
                }
                MouseArea {
                    id: cellArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouseEvent => {
                        if (mouseEvent.button === Qt.RightButton) {
                            photoMenu.target = devPhotoCell.modelData
                            photoMenu.popup()
                        } else {
                            root.openGallery(devPhotoCell.index)
                        }
                    }
                    // Drag device→library: this single photo.
                    readonly property bool dragArmed: DeviceService.connected
                    drag.target: dragArmed ? photoGhost : null
                    preventStealing: dragArmed
                    onPressed: mouse => photoGhost.place(cellArea, mouse)
                    onReleased: photoGhost.drop()
                }
                DragGhost {
                    id: photoGhost
                    area: cellArea
                    dragKind: "device-photos"
                    dragTracks: [{ itemId: devPhotoCell.modelData.itemId,
                                   name: devPhotoCell.modelData.name }]
                    Rectangle {
                        width: 64; height: 64; radius: Theme.radiusMd
                        color: Theme.cardActive
                        border.width: 1
                        border.color: Qt.alpha(Theme.pink, 0.75)
                        clip: true
                        Image {
                            anchors.fill: parent; anchors.margins: 1
                            source: root.thumbFor(devPhotoCell.modelData.itemId)
                            fillMode: Image.PreserveAspectCrop
                        }
                    }
                }
            }
        }
    }

    // ── Context menus ──
    ZuneMenu {
        id: photoMenu
        property var target: null
        ZuneMenuItem {
            text: "Save to Library"
            enabled: DeviceService.connected
            onTriggered: root.saveOne(
                photoMenu.target,
                root.openAlbum && root.openAlbum.itemId !== 0
                    ? root.openAlbum.name : "")
        }
        ZuneMenuItem {
            text: "Delete from Zune"
            enabled: DeviceService.connected
            onTriggered: {
                if (!DeviceService.connected) return
                DeviceService.purgeItems([photoMenu.target.itemId])
                toastHost.show("deleting " + photoMenu.target.name + "…")
            }
        }
    }
    ZuneMenu {
        id: albumMenu
        property var target: null
        ZuneMenuItem {
            text: "Save Album to Library"
            enabled: DeviceService.connected
            onTriggered: {
                if (!DeviceService.connected) return
                for (const p of albumMenu.target.photos)
                    root.saveOne(p, albumMenu.target.itemId === 0
                                     ? "" : albumMenu.target.name)
                toastHost.show("saving " + albumMenu.target.photos.length
                               + " photos…")
            }
        }
        ZuneMenuItem {
            text: "Delete Album from Zune"
            enabled: DeviceService.connected
            onTriggered: {
                if (!DeviceService.connected) return
                DeviceService.purgeItems(
                    albumMenu.target.photos.map(p => p.itemId))
                toastHost.show("deleting " + albumMenu.target.photos.length
                               + " photos…")
            }
        }
    }
}
