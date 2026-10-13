import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// Native device inventory only. Book folders and podcast series are identities;
// equal display titles never combine unrelated objects. All USB work is serialized
// by DeviceService, including the final validated removal.
Item {
    id: page
    property bool books: false
    property var deviceService: DeviceService
    property var syncEngine: SyncEngine
    readonly property string kind: books ? "audiobook" : "podcast"
    readonly property string deviceIdentity: deviceService.connected ? deviceService.deviceSerial : ""
    readonly property bool canManage: deviceService.connected && !deviceService.busy
        && !deviceService.purging && !deviceService.pulling && !syncEngine.isSyncing
        && !deviceService.spokenMediaLoading && !deviceService.spokenMediaError
    readonly property var inventory: books ? deviceService.audiobookPartsList : deviceService.podcastEpisodesList
    property string openGroupKey: ""
    property string searchText: ""
    property var checks: ({})
    property var pendingRemoval: []
    property string pendingDevice: ""
    property string pendingLabel: ""
    property string actionError: ""

    function groupKey(row) {
        const container = books ? row.parentId : row.seriesId
        return container > 0 ? kind + ":" + container : kind + ":item:" + row.itemId
    }
    readonly property var groups: {
        const byId = {}
        for (const row of inventory) {
            const key = groupKey(row)
            if (!byId[key]) byId[key] = {
                key: key,
                title: (books ? row.bookTitle : row.seriesName) || (books ? "Untitled book" : "Untitled podcast"),
                author: row.author || "", artItemId: row.artItemId || 0, rows: []
            }
            byId[key].rows.push(row)
        }
        const result = Object.values(byId)
        for (const group of result) {
            group.rows.sort((a, b) => books
                ? (a.partNumber || 0) - (b.partNumber || 0) || a.itemId - b.itemId
                : a.itemId - b.itemId)
            group.artItemId = group.rows[0].artItemId || 0
        }
        return result.sort((a, b) => a.title.localeCompare(b.title) || a.key.localeCompare(b.key))
    }
    readonly property var filteredGroups: groups.filter(g => !searchText
        || (g.title + " " + g.author).toLowerCase().includes(searchText.toLowerCase()))
    readonly property var openGroup: groups.find(g => g.key === openGroupKey) || null
    readonly property var rows: openGroup ? openGroup.rows : []
    readonly property var selectedIds: rows.filter(row => checks[String(row.itemId)]).map(row => row.itemId)

    function openCollection(key) { openGroupKey = key; checks = {}; actionError = "" }
    function toggle(id) {
        const next = Object.assign({}, checks)
        if (next[String(id)]) delete next[String(id)]; else next[String(id)] = true
        checks = next
    }
    function selectAll() {
        const next = {}
        if (selectedIds.length !== rows.length)
            for (const row of rows) next[String(row.itemId)] = true
        checks = next
    }
    function resetDevice() {
        openGroupKey = ""; checks = {}; pendingRemoval = []; pendingDevice = ""
        actionError = ""; if (removal) removal.close(); if (contextMenu) contextMenu.close()
    }
    function refreshIfVisible() {
        if (visible && deviceService.connected) deviceService.refreshSpokenMedia()
    }
    function requestRemoval(ids, label) {
        if (!canManage || !ids.length) return
        const live = new Set(inventory.map(row => Number(row.itemId)))
        const unique = Array.from(new Set(ids.map(Number)))
        if (unique.some(id => !live.has(id))) return
        pendingRemoval = unique
        pendingDevice = deviceIdentity
        pendingLabel = label
        actionError = ""
        removal.open()
    }
    function confirmRemoval() {
        const live = new Set(inventory.map(row => Number(row.itemId)))
        if (!canManage || !pendingDevice || pendingDevice !== deviceIdentity
            || !pendingRemoval.length || pendingRemoval.some(id => !live.has(id))) {
            actionError = "Device contents changed. Review your selection and try again."
            pendingRemoval = []; removal.close(); return
        }
        deviceService.purgeSpokenItems(kind, pendingRemoval)
        checks = {}; pendingRemoval = []; removal.close()
    }
    function showContext(ids, label) {
        contextMenu.itemIds = ids; contextMenu.label = label; contextMenu.popup()
    }
    function artwork(id) { return deviceService.artPaths[String(id)] || "" }
    function duration(ms) {
        const seconds = Math.max(0, Math.floor((ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + (seconds % 60).toString().padStart(2, "0")
    }
    onVisibleChanged: refreshIfVisible()
    onDeviceIdentityChanged: { resetDevice(); refreshIfVisible() }
    onBooksChanged: resetDevice()
    onInventoryChanged: {
        // Confirmation belongs to the displayed snapshot, not a refreshed row
        // that might reuse the same device handle with different contents.
        if (pendingRemoval.length) {
            pendingRemoval = []; pendingDevice = ""; removal.close()
        }
        const live = new Set(inventory.map(row => String(row.itemId)))
        const next = {}
        for (const key of Object.keys(checks)) if (live.has(key)) next[key] = true
        checks = next
        if (openGroupKey && !inventory.some(row => groupKey(row) === openGroupKey)) openGroupKey = ""
    }
    Component.onCompleted: refreshIfVisible()

    component Cover: Rectangle {
        id: cover
        property double artItemId: 0
        color: Theme.artworkPlaceholder
        radius: Theme.radiusMd
        clip: true
        function request() { if (visible && artItemId > 0 && page.deviceService.connected) page.deviceService.requestArt(artItemId) }
        onArtItemIdChanged: request()
        onVisibleChanged: request()
        Component.onCompleted: request()
        Connections {
            target: page.deviceService
            function onArtChanged() {
                // A refreshed inventory invalidates spoken artwork even when
                // this detail view keeps the same object ID. Missing requests
                // are coalesced by the service, including failed lookups.
                if (!page.artwork(cover.artItemId)) cover.request()
            }
        }
        RoundedArtwork {
            anchors.fill: parent
            source: page.artwork(parent.artItemId)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        MediaKindIcon {
            anchors.centerIn: parent
            visible: !page.artwork(parent.artItemId)
            kind: page.kind
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceMd
        PageHeaderBand {
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            PivotBar { tabs: page.books ? ["Books"] : ["Shows"]; onCurrentChanged: page.openCollection("") }
            Item { Layout.fillWidth: true }
            Text { visible: page.width > Theme.spokenSheetWidth; text: page.groups.length + (page.books ? " books" : " shows"); color: Theme.textDim; font.pixelSize: Theme.spokenCaption }
            ZuneSearchField {
                visible: !page.openGroup
                Layout.preferredWidth: Theme.spokenSearchWidth
                placeholderText: page.books ? "search books" : "search shows"
                onTextChanged: page.searchText = text
            }
            SpokenButton {
                objectName: "deviceSpokenRefresh"
                text: "refresh"
                enabled: page.deviceService.connected && !page.deviceService.busy
                    && !page.deviceService.purging && !page.deviceService.pulling
                    && !page.syncEngine.isSyncing && !page.deviceService.spokenMediaLoading
                onClicked: page.deviceService.refreshSpokenMedia()
            }
        }
        PodcastCleanupControl {
            visible: !page.books && !page.openGroup
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
        }
        MusicBreadcrumbBar {
            clip: true
            Layout.fillWidth: true
            inset: Gallery.spine(page.width)
            visible: !!page.openGroup
            crumbs: [page.books ? "Books" : "Shows", page.openGroup ? page.openGroup.title : ""]
            onBackClicked: page.openCollection("")
            onCrumbClicked: page.openCollection("")
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            visible: !!text
            text: page.actionError || page.deviceService.spokenMediaError
            color: Theme.warning
            font.pixelSize: Theme.spokenBody
            wrapMode: Text.Wrap
        }
        Text {
            visible: page.deviceService.spokenMediaLoading || page.deviceService.purging
            text: page.deviceService.purging ? "removing from your Zune…" : "reading your Zune…"
            color: Theme.textSecondary
            font.pixelSize: Theme.spokenCaption
        }
        GridLayout {
            objectName: "deviceSpokenDetailHeader"
            Layout.topMargin: Theme.spaceLg
            Layout.maximumWidth: Gallery.frameW(page.width)
            visible: !!page.openGroup && page.deviceService.connected
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            columns: page.width < Theme.spokenDetailBreakpoint ? 1 : 2
            columnSpacing: Theme.spaceXl
            rowSpacing: Theme.spaceMd
            SpokenShelf {
                id: deviceShelf
                Layout.preferredWidth: Theme.spokenShelfWidth
                Layout.preferredHeight: Theme.spokenShelfHeight
                property double artItemId: page.openGroup ? page.openGroup.artItemId : 0
                source: page.artwork(artItemId)
                book: page.books
                function request() { if (visible && artItemId > 0 && page.deviceService.connected) page.deviceService.requestArt(artItemId) }
                onArtItemIdChanged: request()
                onVisibleChanged: request()
                Component.onCompleted: request()
                Connections {
                    target: page.deviceService
                    function onArtChanged() { if (!page.artwork(deviceShelf.artItemId)) deviceShelf.request() }
                }
            }
            SpokenDetailHeading {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                category: page.books ? "audiobook · on Zune" : "podcast · on Zune"
                title: page.openGroup ? page.openGroup.title : ""
                author: page.openGroup ? page.openGroup.author : ""
                summary: page.rows.length + (page.books ? " parts on your Zune" : " episodes on your Zune")
                SpokenButton {
                    objectName: "deviceSpokenRemoveCollection"
                    flat: true
                    text: page.books ? "remove book…" : "remove all episodes…"
                    enabled: page.canManage
                    onClicked: page.requestRemoval(page.rows.map(row => row.itemId), page.openGroup.title)
                }
            }
        }
        RowLayout {
            visible: !!page.openGroup && page.deviceService.connected
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXxxl
            SpokenButton {
                objectName: "deviceSpokenSelectAll"
                text: page.selectedIds.length === page.rows.length ? "clear selection" : "select all"
                enabled: page.canManage
                onClicked: page.selectAll()
            }
            Text {
                Layout.fillWidth: true
                text: page.selectedIds.length + " selected"
                font.pixelSize: Theme.spokenCaption
                color: Theme.textSecondary
            }
            SpokenButton {
                objectName: "deviceSpokenRemoveSelected"
                text: "remove selected…"
                enabled: page.canManage && page.selectedIds.length > 0
                onClicked: page.requestRemoval(page.selectedIds, "selected " + (page.books ? "parts" : "episodes"))
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GalleryView {
                id: grid
                objectName: "deviceSpokenGroupGrid"
                anchors.fill: parent
                visible: page.deviceService.connected && !page.openGroup
                anchorX: Gallery.spine(page.width)
                idealCell: Theme.spokenCardSize
                maxCols: 7
                liftColumnCap: !page.books
                cellHeightFor: w => w * (page.books ? Theme.spokenBookRatio : 1) + Theme.spokenCardCaption
                rows: page.filteredGroups
                cardDelegate: Component { Item {
                    id: card
                    required property var modelData
                    width: grid.plan.cell
                    height: grid.cellHeightFor(width)
                    Column {
                        width: parent.width
                        spacing: Theme.spaceXs
                        Cover {
                            width: parent.width
                            height: width * (page.books ? Theme.spokenBookRatio : 1)
                            artItemId: card.modelData.artItemId
                        }
                        Text {
                            width: parent.width
                            text: card.modelData.title
                            color: Theme.textPrimary
                            font.pixelSize: Theme.spokenBody
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: card.modelData.rows.length + (page.books ? " parts" : " episodes")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.spokenCaption
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton)
                                page.showContext(card.modelData.rows.map(row => row.itemId), card.modelData.title)
                            else page.openCollection(card.modelData.key)
                        }
                    }
                } }
            }
            ListView {
                id: rowList
                objectName: "deviceSpokenRows"
                anchors.fill: parent
                anchors.leftMargin: Gallery.spine(page.width)
                anchors.rightMargin: Math.max(Theme.spaceXxxl, parent.width - Gallery.spine(page.width) - Gallery.frameW(page.width))
                visible: page.deviceService.connected && !!page.openGroup
                clip: true
                model: page.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ZuneScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: rowList.width
                    height: Theme.spokenDetailRowHeight
                    color: page.checks[String(modelData.itemId)] ? Theme.rowSelectedPink
                        : rowMouse.containsMouse ? Theme.rowHoverPink : Theme.spokenTransparent
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton) page.showContext([row.modelData.itemId], row.modelData.title)
                            else if (page.canManage) page.toggle(row.modelData.itemId)
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: Theme.hairline; color: Theme.borderLight; opacity: Theme.spokenDisabledOpacity }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spaceSm
                        spacing: Theme.spaceMd
                        SpokenCheckBox {
                            checked: !!page.checks[String(row.modelData.itemId)]
                            enabled: page.canManage
                            Accessible.name: "Select " + row.modelData.title
                            onClicked: page.toggle(row.modelData.itemId)
                        }
                        Text {
                            visible: page.books
                            text: row.modelData.partNumber || ""
                            color: Theme.textDim
                            font.pixelSize: Theme.spokenCaption
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: row.modelData.title || "Untitled"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.spokenBody
                            elide: Text.ElideRight
                        }
                        Text {
                            objectName: "devicePodcastPlayed"
                            visible: !page.books && !!row.modelData.playedKnown && !!row.modelData.played
                            text: "✓ played on Zune"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.spokenCaption
                        }
                        Text {
                            text: page.duration(row.modelData.durationMs)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.spokenCaption
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: Math.min(parent.width, Theme.spokenEmptyWidth)
                visible: !page.deviceService.connected || (!page.openGroup && !page.filteredGroups.length
                    && !page.deviceService.spokenMediaLoading && !page.deviceService.spokenMediaError)
                text: !page.deviceService.connected ? "Plug in a Zune to browse its contents."
                    : page.searchText ? "No matching titles"
                    : page.books ? "No audiobooks on this Zune yet." : "No podcasts on this Zune yet."
                color: Theme.textSecondary
                font.pixelSize: Theme.spokenBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
    ZuneMenu {
        id: contextMenu
        property var itemIds: []
        property string label: ""
        ZuneMenuItem {
            text: "Remove from Zune…"
            danger: true
            enabled: page.canManage
            onTriggered: page.requestRemoval(contextMenu.itemIds, contextMenu.label)
        }
    }
    Popup {
        id: removal
        objectName: "deviceSpokenDeleteDialog"
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.spokenEmptyWidth, page.width)
        padding: Theme.spaceXl
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.surfaceBg
            radius: Theme.radiusLg
            border.width: Theme.hairline
            border.color: Theme.glassBorder
        }
        onClosed: { page.pendingRemoval = []; page.pendingDevice = "" }
        contentItem: ColumnLayout {
            spacing: Theme.spaceLg
            Text {
                Layout.fillWidth: true
                text: "Remove from your Zune?"
                color: Theme.textPrimary
                font.pixelSize: Theme.spokenTitle
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: page.pendingLabel + "\n" + page.pendingRemoval.length
                    + (page.books ? " audiobook parts will be removed." : " podcast episodes will be removed.")
                    + "\nYour local library stays unchanged."
                color: Theme.textSecondary
                font.pixelSize: Theme.spokenBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                SpokenButton { text: "cancel"; onClicked: removal.close() }
                SpokenButton {
                    objectName: "deviceSpokenConfirmRemoval"
                    text: "remove from Zune"
                    highlighted: true
                    enabled: page.canManage && page.pendingDevice === page.deviceIdentity
                    onClicked: page.confirmRemoval()
                }
            }
        }
    }
}
