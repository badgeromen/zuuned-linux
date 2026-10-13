import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zuuned

Item {
    id: page
    property bool books: false
    PodcastStatus { id: podcastStatus }
    function ensureDeviceStatus() {
        if (visible && !books && DeviceService.connected) DeviceService.ensureSpokenMedia()
    }
    Component.onCompleted: ensureDeviceStatus()
    onVisibleChanged: ensureDeviceStatus()
    Connections {
        target: DeviceService
        function onStateChanged() { page.ensureDeviceStatus() }
    }
    property string collectionId: ""
    property var checks: ({})
    property string filter: "all"
    readonly property bool deviceTransferReady: true
    readonly property var collection: { void SpokenMediaService.revision; return SpokenMediaService.item(collectionId) }
    property string pivot: books ? "Books" : "Shows"
    property string shelfId: ""
    onPivotChanged: { if (topPivots && topPivots.current !== pivot) topPivots.current = pivot }
    onFilterChanged: syncDetailPivot()
    function syncDetailPivot() {
        const current = books && filter === "all" ? "chapters" : filter
        if (detailPivots && detailPivots.current !== current) detailPivots.current = current
    }
    onBooksChanged: {
        ensureDeviceStatus()
        pivot = books ? "Books" : "Shows"
        shelfId = ""; collectionId = ""; checks = {}; filter = "all"
        syncDetailPivot()
    }
    readonly property var shelves: { void SpokenMediaService.revision; return SpokenMediaService.audiobookCollections() }
    readonly property var shelf: shelves.find(s => s.id === shelfId) || ({})
    readonly property bool browsingShelves: books && pivot === "Libraries" && !shelfId && !collectionId
    readonly property var containers: {
        void SpokenMediaService.revision
        const all = shelfId ? SpokenMediaService.collectionBooks(shelfId) : SpokenMediaService.collections(books ? "book" : "show")
        const filtered = all.filter(c => ((pivot !== "Starred" && pivot !== "Starred shows") || c.favorite) && (!search.text || (c.title + " " + (c.author || "")).toLowerCase().includes(search.text.toLowerCase())))
        return shelfId ? filtered : filtered.sort((a, b) => a.title.toLowerCase().localeCompare(b.title.toLowerCase()) || a.id.localeCompare(b.id))
    }
    readonly property var galleryRows: browsingShelves ? shelves.filter(c => !search.text || c.title.toLowerCase().includes(search.text.toLowerCase())).sort((a, b) => a.title.toLowerCase().localeCompare(b.title.toLowerCase()) || a.id.localeCompare(b.id)) : containers
    readonly property bool showingEntries: !!collectionId || (!books && pivot === "Starred episodes")
    readonly property var rows: {
        void SpokenMediaService.revision
        let items = SpokenMediaService.entries(collectionId)
        if (!books && !collectionId && pivot === "Starred episodes") {
            items = []
            for (const show of SpokenMediaService.collections("show"))
                items = items.concat(SpokenMediaService.entries(show.id))
            items = items.filter(e => e.favorite && (!search.text || e.title.toLowerCase().includes(search.text.toLowerCase())))
        }
        return items.filter(e => filter === "downloaded" ? !!e.path : filter === "unplayed" ? !(page.books ? e.played : podcastStatus.played(e)) : filter === "starred" ? !!e.favorite : true)
    }
    readonly property var listRows: {
        if (!books) return rows
        let flat = []
        for (const part of rows) {
            flat.push(part)
            const children = filter === "bookmarks" ? part.bookmarks ?? [] : filter === "files" ? [] : part.chapters ?? []
            for (const child of children) flat.push({ id: part.id, title: child.title || child.note || "bookmark", path: part.path, durationMs: child.startMs ?? child.positionMs ?? 0, detail: true })
        }
        return flat
    }
    onShelvesChanged: { if (shelfId && !shelves.some(s => s.id === shelfId)) { shelfId = ""; openCollection("") } }
    function time(ms) { const s = Math.max(0, Math.floor(ms / 1000)); return Math.floor(s / 60) + ":" + (s % 60).toString().padStart(2, "0") }
    function toggle(id) { const next = Object.assign({}, checks); if (next[id]) delete next[id]; else next[id] = true; checks = next }
    function playCollection(entry) {
        if (books) { SpokenMediaService.play(entry.id); return }
        const episodes = SpokenMediaService.entries(entry.id)
        const next = episodes.find(e => e.path && !e.played) || episodes.find(e => e.path)
        if (next) SpokenMediaService.play(next.id)
    }
    function back() {
        if (collectionId) openCollection("")
        else shelfId = ""
    }
    function editLibrary(entry, seed) {
        BookTrayState.openFor(entry, seed)
        pivot = "Books"; shelfId = ""; openCollection("")
    }
    function addBook(id) {
        if (BookTrayState.building) BookTrayState.addBooks([id])
        else SpokenMediaService.queueAudiobooks([id])
    }
    function openCollection(id) { collectionId = id; checks = {}; filter = "all" }
    function contextFor(entry) { contextMenu.entry = entry; contextMenu.popup() }
    function queueItems(ids) { if (books) SpokenMediaService.queueAudiobooks(ids); else SpokenMediaService.queuePodcasts(ids) }
    function pick(anchor, entry) {
        if (books) { SpokenMediaService.queueAudiobooks([entry.id]); return }
        picker.openAt(anchor, entry)
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceMd
        PageHeaderBand {
            objectName: "spokenHeaderBand"
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            spacing: Theme.spaceSm
            PivotBar {
                id: topPivots
                objectName: "spokenTopPivots"
                tabs: page.books ? ["Books", "Libraries", "Starred"] : ["Shows", "Starred shows", "Starred episodes"]
                current: page.pivot
                Layout.minimumWidth: implicitWidth
                onCurrentChanged: {
                    page.pivot = current; page.shelfId = ""; page.openCollection("")
                    search.text = ""; grid.letterFilter = ""
                }
            }
            Item { Layout.fillWidth: true }
            SpokenButton { objectName: "spokenNewCollection"; visible: page.books && page.pivot === "Libraries" && page.width >= Theme.spokenSheetWidth; text: "+ library"; onClicked: page.editLibrary({}) }
            SpokenButton { visible: page.books && page.width > Theme.spokenSheetWidth; text: "+ book folder"; enabled: !SpokenMediaService.busy; onClicked: folderImport.begin() }
            SpokenButton { objectName: page.books ? "spokenAddBook" : "spokenAddFeed"; text: page.width < Theme.spokenSheetWidth ? "+" : page.books ? "+ books" : "+ podcast"; Accessible.name: page.books ? "Add books" : "Add podcast"; enabled: !SpokenMediaService.busy; onClicked: page.books ? bookImportMenu.popup() : importSheet.openFeed() }
            ZuneSearchField { id: search; objectName: "spokenSearch"; visible: !page.collectionId; placeholderText: page.books ? page.browsingShelves ? "search libraries" : "search books" : page.pivot === "Starred episodes" ? "search episodes" : "search shows"; Layout.fillWidth: true; Layout.maximumWidth: Theme.spokenSearchWidth; Layout.preferredWidth: Theme.spokenSearchWidth; Layout.minimumWidth: Theme.spaceHuge }
        }
        PodcastCleanupControl {
            visible: !page.books && !page.collectionId
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
        }
        MusicBreadcrumbBar {
            clip: true
            Layout.fillWidth: true
            visible: !!page.collectionId || !!page.shelfId
            inset: Gallery.spine(page.width)
            crumbs: [page.pivot].concat(page.shelfId ? [page.shelf.title || "Library"] : []).concat(page.collectionId ? [page.collection.title || ""] : [])
            onBackClicked: page.back()
            onCrumbClicked: depth => { page.openCollection(""); if (depth === 0) page.shelfId = "" }
        }
        RowLayout {
            visible: !!page.shelfId && !page.collectionId
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            Text { Layout.fillWidth: true; text: page.containers.length + " books · your reading order"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
            SpokenButton { objectName: "spokenEditCollection"; text: "edit library"; onClicked: page.editLibrary(page.shelf) }
        }
        RowLayout {
            visible: SpokenMediaService.foregroundBusy && SpokenMediaService.status !== "position"
            Layout.fillWidth: true
            Text { text: SpokenMediaService.status + "…"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption; Layout.fillWidth: true }
            SpokenButton { text: "cancel task"; onClicked: SpokenMediaService.cancelWork() }
        }
        GalleryView {
            id: grid
            objectName: "spokenGallery"
            visible: !page.showingEntries
            Layout.fillWidth: true
            Layout.fillHeight: true
            anchorX: Gallery.spine(page.width)
            idealCell: Theme.spokenCardSize
            maxCols: 7
            liftColumnCap: !page.books
            lettered: !page.shelfId
            cellHeightFor: w => w * (page.books ? Theme.spokenBookRatio : 1) + Theme.spokenCardCaption
            rows: page.galleryRows
            cardDelegate: Component {
                Item {
                    required property var modelData
                    width: grid.plan.cell
                    height: grid.cellHeightFor(width)
                    SpokenCard {
                        objectName: "spokenCollectionCard_" + entry.id
                        anchors.fill: parent
                        visible: !page.browsingShelves
                        entry: parent.modelData; book: page.books
                        onDevice: !page.books && SpokenMediaService.entries(entry.id).some(e => podcastStatus.deviceFor(e.id).onDevice)
                        cardSize: grid.plan.cell
                        transferReady: page.deviceTransferReady
                        playable: (entry.downloaded ?? 0) > 0
                        onPlayed: page.playCollection(entry)
                        onFavoriteRequested: SpokenMediaService.setFavorite(entry.id, !entry.favorite)
                        onSelected: page.openCollection(entry.id)
                        onContextRequested: page.contextFor(entry)
                        onPickParts: page.pick(this, entry)
                        onAddAll: page.books ? page.addBook(entry.id) : SpokenMediaService.queuePodcastSelection(entry.id)
                    }
                    Column {
                        visible: page.browsingShelves
                        width: parent.width; spacing: Theme.spaceSm
                        Rectangle {
                            width: parent.width; height: width * Theme.spokenBookRatio
                            color: Theme.artworkPlaceholder; radius: Theme.radiusMd
                            RoundedArtwork { anchors.fill: parent; source: { void SpokenMediaService.revision; return SpokenMediaService.cover(modelData.bookIds?.[0] || "") } fillMode: Image.PreserveAspectCrop }
                            MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; cursorShape: Qt.PointingHandCursor; onClicked: mouse => { if (mouse.button === Qt.RightButton) page.editLibrary(modelData); else { page.shelfId = modelData.id; grid.letterFilter = "" } } }
                        }
                        Text { width: parent.width; text: modelData.title || ""; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody; elide: Text.ElideRight }
                        Text { text: (modelData.bookIds?.length || 0) + " books"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: page.galleryRows.length === 0
                width: Math.min(parent.width, Theme.spokenEmptyWidth)
                text: search.text ? "No matching titles" : (page.pivot === "Starred" || page.pivot === "Starred shows") ? "Your favorites belong here.\nStar a " + (page.books ? "book" : "show") + " to keep it close." : page.browsingShelves ? "Keep a series together.\nCreate a library and drag books into its island." : page.books ? "A place for every story.\nAdd a book folder or audio files to get started." : "Follow the signal.\nAdd an RSS feed, then choose your episodes."
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                color: Theme.textSecondary; font.pixelSize: Theme.spokenBody; lineHeight: Theme.spokenLineHeight
            }
        }
        GridLayout {
            visible: !!page.collectionId
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            objectName: "spokenDetailHeader"
            Layout.topMargin: Theme.spaceLg
            Layout.maximumWidth: Gallery.frameW(page.width)
            columns: page.width < Theme.spokenDetailBreakpoint ? 1 : 2
            columnSpacing: Theme.spaceXl
            rowSpacing: Theme.spaceMd
            Item {
                id: heroArt
                Layout.preferredWidth: Theme.spokenShelfWidth
                Layout.preferredHeight: Theme.spokenShelfHeight
                SpokenShelf {
                    anchors.fill: parent
                    book: page.books
                    source: { void SpokenMediaService.revision; return SpokenMediaService.cover(page.collectionId) }
                    hovering: heroArea.containsMouse
                }
                MouseArea {
                    id: heroArea
                    objectName: "spokenHeroDragArea"
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    readonly property bool dragArmed: (page.books && BookTrayState.building) || (page.deviceTransferReady && DeviceService.connected)
                    drag.target: dragArmed ? heroGhost : null
                    preventStealing: dragArmed
                    onClicked: mouse => { if (mouse.button === Qt.RightButton) page.contextFor(page.collection); else page.playCollection(page.collection) }
                    onPressed: mouse => heroGhost.place(this, mouse)
                    onReleased: heroGhost.drop()
                }
                DragGhost {
                    id: heroGhost
                    area: heroArea
                    localBookDrag: page.books && BookTrayState.building
                    dragKind: page.books ? "audiobook-books" : "podcast-shows"
                    dragName: page.collection.title ?? ""
                    dragTracks: () => [page.collectionId]
                    property var openTransferPicker: () => page.pick(heroArt, page.collection)
                    Rectangle {
                        width: Theme.spokenDragCoverSize; height: Theme.spokenDragCoverSize
                        color: Theme.artworkPlaceholder; radius: Theme.radiusMd
                        border.width: Theme.hairline; border.color: Theme.activePink
                        RoundedArtwork { anchors.fill: parent; source: { void SpokenMediaService.revision; return SpokenMediaService.cover(page.collectionId) } fillMode: Image.PreserveAspectCrop }
                    }
                }

            }
            SpokenDetailHeading {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                category: page.books ? "audiobook" : "podcast"
                title: page.collection.title ?? ""
                author: page.collection.author ?? ""
                narrator: page.collection.narrator ?? ""
                summary: {
                    void SpokenMediaService.revision
                    const all = SpokenMediaService.entries(page.collectionId)
                    const available = all.filter(e => !!e.path).length
                    if (page.books) {
                        const chapters = all.reduce((count, e) => count + (e.chapters || []).length, 0)
                        return (chapters ? chapters + " chapters · " : "") + all.length + (all.length === 1 ? " audio file" : " audio files")
                    }
                    return all.length + " episodes · " + available + " downloaded"
                }
                SpokenButton { objectName: "spokenDetailPlay"; flat: true; text: "▶ resume"; enabled: page.rows.some(e => !!e.path); onClicked: page.playCollection(page.collection) }
                SplitAddButton { objectName: "spokenDetailAdd"; showPick: !page.books; enabled: page.books && BookTrayState.building ? !BookTrayState.locked : DeviceService.connected && page.deviceTransferReady; onAddAll: page.books ? page.addBook(page.collectionId) : SpokenMediaService.queuePodcastSelection(page.collectionId); onPickParts: page.pick(this, page.collection) }
                SpokenButton { objectName: "spokenDetailStar"; text: page.collection.favorite ? "★ starred" : "☆ star"; flat: true; highlighted: !!page.collection.favorite; onClicked: SpokenMediaService.setFavorite(page.collectionId, !page.collection.favorite) }
                SpokenButton { visible: !page.books; flat: true; text: "↻ refresh feed"; enabled: !SpokenMediaService.busy; onClicked: SpokenMediaService.refreshFeed(page.collectionId) }
            }
        }
        ColumnLayout {
            visible: page.showingEntries
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            spacing: Theme.spaceXs
            RowLayout {
                Layout.fillWidth: true
            PivotBar {
                id: detailPivots
                tabs: page.books ? ["chapters", "bookmarks", "files"] : ["all", "unplayed", "downloaded", "starred"]
                current: page.filter === "all" && page.books ? "chapters" : page.filter
                onCurrentChanged: { if (current !== (page.books && page.filter === "all" ? "chapters" : page.filter)) { page.filter = current; page.checks = {} } }
            }
                Item { Layout.fillWidth: true }
                SpokenButton { visible: !page.books; flat: true; text: Object.keys(page.checks).length === page.rows.length && page.rows.length ? "clear selection" : "select all"; onClicked: { let selected = {}; if (Object.keys(page.checks).length !== page.rows.length) for (const e of page.rows) selected[e.id] = true; page.checks = selected } }
            }
            Flow {
                objectName: "spokenSelectionActions"
                visible: !page.books && Object.keys(page.checks).length > 0
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                SpokenButton { flat: true; visible: Object.keys(page.checks).length > 0; text: "+ Zune · " + Object.keys(page.checks).length; enabled: DeviceService.connected; onClicked: page.queueItems(Object.keys(page.checks)) }
                SpokenButton { flat: true; visible: Object.keys(page.checks).length > 0; text: "↓ download · " + Object.keys(page.checks).length; enabled: !SpokenMediaService.busy; onClicked: SpokenMediaService.download(Object.keys(page.checks)) }
            }
        }
        ListView {
            id: list
            objectName: "spokenEpisodeList"
            visible: page.showingEntries
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: Gallery.frameW(page.width)
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            clip: true
            model: page.listRows
            spacing: 0
            delegate: Column {
                id: rowGroup
                required property var modelData
                required property int index
                readonly property var deviceStatus: page.books ? ({}) : podcastStatus.deviceFor(modelData.id)
                readonly property string playedLabel: page.books ? (modelData.played ? "played" : "") : podcastStatus.playedLabel(modelData)
                width: list.width
                Rectangle {
                    id: row
                    width: parent.width
                    height: Theme.spokenDetailRowHeight
                    color: page.checks[rowGroup.modelData.id] ? Theme.rowSelectedPink : area.containsMouse ? Theme.rowHoverPink : Theme.spokenTransparent
                    MouseArea {
                        id: area; objectName: "spokenEpisodeDragArea"; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => { if (mouse.button === Qt.RightButton) page.contextFor(rowGroup.modelData); else rowGroup.modelData.detail ? SpokenMediaService.play(rowGroup.modelData.id, rowGroup.modelData.durationMs) : page.books ? SpokenMediaService.play(rowGroup.modelData.id) : page.toggle(rowGroup.modelData.id) }
                        onDoubleClicked: SpokenMediaService.play(rowGroup.modelData.id, rowGroup.modelData.detail ? rowGroup.modelData.durationMs : -1)
                        drag.target: !rowGroup.modelData.detail && ((page.books && BookTrayState.building) || (DeviceService.connected && page.deviceTransferReady)) ? ghost : null
                        preventStealing: !rowGroup.modelData.detail && ((page.books && BookTrayState.building) || (DeviceService.connected && page.deviceTransferReady))
                        onPressed: mouse => ghost.place(this, mouse)
                        onReleased: ghost.drop()
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: Theme.hairline; color: Theme.borderLight; opacity: Theme.spokenDisabledOpacity }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spaceSm
                        spacing: Theme.spaceMd
                        Item {
                            Layout.preferredWidth: Theme.spokenDetailNumberWidth
                            Layout.fillHeight: true
                            Text {
                                objectName: "spokenEpisodeNumber"
                                anchors.centerIn: parent
                                visible: page.books || (!area.containsMouse && !Object.keys(page.checks).length)
                                text: rowGroup.modelData.detail ? "·"
                                    : page.books ? String(page.rows.findIndex(part => part.id === rowGroup.modelData.id) + 1)
                                    : rowGroup.modelData.episodeNumber > 0 ? String(rowGroup.modelData.episodeNumber) : ""
                                color: Theme.textDim
                                font.pixelSize: Theme.spokenCaption
                            }
                            SpokenCheckBox {
                                anchors.centerIn: parent
                                visible: !page.books && (area.containsMouse || Object.keys(page.checks).length > 0)
                                checked: page.checks[rowGroup.modelData.id] === true
                                onClicked: page.toggle(rowGroup.modelData.id)
                            }
                        }
                        Text {
                            Layout.fillWidth: list.width < Theme.spokenDetailBreakpoint
                            Layout.preferredWidth: list.width >= Theme.spokenDetailBreakpoint ? list.width * 0.34 : -1
                            Layout.minimumWidth: 0
                            text: rowGroup.modelData.title
                            color: Theme.textPrimary
                            font.pixelSize: Theme.spokenBody
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: list.width >= Theme.spokenDetailBreakpoint
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: rowGroup.modelData.author || page.collection.author || ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.spokenBody
                            elide: Text.ElideRight
                        }
                        Text {
                            objectName: "spokenEpisodePlayed"
                            visible: !!rowGroup.playedLabel
                            text: "✓ " + rowGroup.playedLabel
                            color: Theme.textSecondary
                            font.pixelSize: Theme.spokenCaption
                        }
                        Text { text: page.time(rowGroup.modelData.durationMs); color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
                        OnDeviceBadge {
                            objectName: "spokenEpisodeOnDevice"
                            visible: !rowGroup.modelData.detail && !!rowGroup.deviceStatus.onDevice
                            size: Theme.locationRowBadgeSize
                        }
                        SpokenButton { id: rowStar; opacity: rowGroup.modelData.favorite || area.containsMouse || hovered || activeFocus ? 1 : 0; objectName: "spokenEpisodeStar"; visible: !page.books && !rowGroup.modelData.detail; flat: true; text: rowGroup.modelData.favorite ? "★" : "☆"; highlighted: !!rowGroup.modelData.favorite; Accessible.name: (rowGroup.modelData.favorite ? "Unstar " : "Star ") + rowGroup.modelData.title; onClicked: SpokenMediaService.setFavorite(rowGroup.modelData.id, !rowGroup.modelData.favorite) }
                        SpokenButton { flat: true; opacity: area.containsMouse || hovered || activeFocus ? 1 : 0; text: rowGroup.modelData.path ? "▶" : "↓"; Accessible.name: (rowGroup.modelData.path ? "Play " : "Download ") + rowGroup.modelData.title; enabled: !!rowGroup.modelData.path || !SpokenMediaService.busy; onClicked: rowGroup.modelData.path ? SpokenMediaService.play(rowGroup.modelData.id, rowGroup.modelData.detail ? rowGroup.modelData.durationMs : -1) : SpokenMediaService.download([rowGroup.modelData.id]) }
                    }
                }
                DragGhost {
                    id: ghost
                    localBookDrag: page.books && BookTrayState.building
                    area: area; dragKind: page.books ? "audiobook-books" : "podcasts"; dragName: rowGroup.modelData.title
                    dragTracks: () => page.books ? [page.collectionId] : page.checks[rowGroup.modelData.id] ? Object.keys(page.checks) : [rowGroup.modelData.id]
                    Rectangle { width: Theme.spokenDragWidth; height: Theme.spokenRowHeight; color: Theme.surfaceBg; radius: Theme.radiusMd; border.width: Theme.hairline; border.color: Theme.pink
                        Text { anchors.centerIn: parent; text: page.books ? (page.collection.title || "") : Math.max(1, Object.keys(page.checks).length) + " episodes"; font.pixelSize: Theme.spokenBody; color: Theme.textPrimary }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: Math.min(parent.width, Theme.spokenEmptyWidth)
                visible: list.count === 0 && !page.books && page.pivot === "Starred episodes"
                text: search.text ? "No matching episodes" : "Keep your favorite episodes close.\nStar an episode in any show to find it here."
                wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.spokenBody; color: Theme.textSecondary
            }
            ScrollBar.vertical: ZuneScrollBar {}
        }

    }
    ZuneMenu {
        id: contextMenu
        objectName: "spokenContextMenu"
        property var entry: ({})
        ZuneMenuItem {
            text: contextMenu.entry.favorite ? "Unstar" : "Star"
            visible: ["show", "episode", "book"].includes(contextMenu.entry.kind)
            height: visible ? implicitHeight : 0
            onTriggered: SpokenMediaService.setFavorite(contextMenu.entry.id, !contextMenu.entry.favorite)
        }
        ZuneMenuItem {
            text: "Add to Library…"
            visible: contextMenu.entry.kind === "book"
            height: visible ? implicitHeight : 0
            onTriggered: bookLibraryMenu.popup()
        }
        ZuneMenuItem {
            objectName: "podcastKeepOnDevice"
            text: contextMenu.entry.keepOnDevice ? "Allow Automatic Cleanup" : "Keep on Zune"
            visible: contextMenu.entry.kind === "episode"
            height: visible ? implicitHeight : 0
            enabled: !SyncEngine.isSyncing && !SpokenMediaService.busy
            onTriggered: SpokenMediaService.setPodcastKeepOnDevice(contextMenu.entry.id, !contextMenu.entry.keepOnDevice)
        }
        ZuneMenuItem {
            text: "Customize…"
            onTriggered: coverSheet.openFor(SpokenMediaService.item(contextMenu.entry.id))
        }
        ZuneMenuItem {
            text: "Refresh Feed"
            visible: contextMenu.entry.kind === "show"
            height: visible ? implicitHeight : 0
            enabled: !SpokenMediaService.busy
            onTriggered: SpokenMediaService.refreshFeed(contextMenu.entry.id)
        }
        ZuneMenuItem {
            text: contextMenu.entry.played ? "Mark Unplayed" : "Mark Played"
            visible: contextMenu.entry.kind === "episode" || contextMenu.entry.kind === "part"
            height: visible ? implicitHeight : 0
            onTriggered: SpokenMediaService.markPlayed(contextMenu.entry.id, !contextMenu.entry.played)
        }
    }
    ZuneMenu {
        id: bookLibraryMenu
        ZuneMenuItem { text: "Add to current library draft"; visible: BookTrayState.building; height: visible ? implicitHeight : 0; enabled: !BookTrayState.locked; onTriggered: BookTrayState.addBooks([contextMenu.entry.id]) }
        ZuneMenuItem { text: "New library…"; onTriggered: page.editLibrary({}, contextMenu.entry.id) }
        Repeater {
            model: page.shelves
            ZuneMenuItem { required property var modelData; text: modelData.title; onTriggered: page.editLibrary(modelData, contextMenu.entry.id) }
        }
    }
    ZuneMenu {
        id: bookImportMenu
        ZuneMenuItem { text: "New library…"; onTriggered: page.editLibrary({}) }
        ZuneMenuItem { text: "Import book folder…"; onTriggered: folderImport.begin() }
        ZuneMenuItem { text: "Choose audio files…"; onTriggered: bookPicker.open() }
    }

    PodcastSyncPicker { id: picker; objectName: "spokenTransferPicker"; onPicked: ids => page.queueItems(ids) }
    SpokenCoverSheet { id: coverSheet; objectName: "spokenCoverSheet" }
    SpokenImportSheet { id: importSheet }
    SpokenFolderImport { id: folderImport; objectName: "spokenFolderSheet" }
    FileDialog { id: bookPicker; title: "Choose the audio files for one book"; fileMode: FileDialog.OpenFiles; nameFilters: ["Audiobooks (*.m4b *.m4a *.mp3 *.flac *.ogg *.opus *.wav *.wma)"]; onAccepted: importSheet.openBook(selectedFiles) }
}
