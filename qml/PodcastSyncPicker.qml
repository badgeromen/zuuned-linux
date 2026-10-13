import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

Popup {
    id: picker
    property var show: ({})
    property string mode: "newest"
    property int episodeCount: 10
    PodcastStatus { id: podcastStatus }
    onEpisodeCountChanged: countField.text = String(episodeCount)
    property bool unplayedOnly: true
    property var checks: ({})
    readonly property var rows: {
        void SpokenMediaService.revision
        void DeviceService.podcastEpisodesList
        if (!show.id) return []
        if (mode === "manual") return SpokenMediaService.entries(show.id)
        return SpokenMediaService.selectPodcastEpisodes(show.id, mode, 5000, false)
            .filter(e => !unplayedOnly || !podcastStatus.played(e)).slice(0, episodeCount)
    }
    readonly property var selectedIds: rows.filter(e => checks[e.id] && !podcastStatus.deviceFor(e.id).onDevice).map(e => e.id)
    signal picked(var ids)
    function selectPreview() {
        const next = {}
        if (mode !== "manual") for (const row of rows) if (!podcastStatus.deviceFor(row.id).onDevice) next[row.id] = true
        checks = next
    }
    onModeChanged: selectPreview()
    onRowsChanged: {
        if (mode !== "manual") { selectPreview(); return }
        const next = {}
        for (const row of rows) if (checks[row.id]) next[row.id] = true
        checks = next
    }
    function toggle(id) {
        if (podcastStatus.deviceFor(id).onDevice) return
        const next = Object.assign({}, checks)
        if (next[id]) delete next[id]; else next[id] = true
        checks = next
    }
    function openAt(anchor, entry) {
        show = SpokenMediaService.item(entry.id)
        mode = show.syncOrder === "oldest" ? "oldest" : "newest"
        episodeCount = Math.max(1, Math.min(5000, show.syncCount || AppSettings.podcastAutoQueueCount))
        unplayedOnly = show.syncUnplayedOnly !== false
        modeTabs.current = mode === "oldest" ? "Oldest" : "Newest"
        selectPreview()
        const ov = Overlay.overlay
        parent = ov
        const p = anchor.mapToItem(ov, anchor.width, 0)
        x = Math.max(Theme.spaceSm, Math.min(p.x + Theme.spaceXs, ov.width - width - Theme.spaceSm))
        y = Math.max(Theme.spaceSm, Math.min(p.y, ov.height - height - Theme.spaceSm))
        open()
    }
    function addSelection() {
        if (!DeviceService.connected || SyncEngine.isSyncing || !selectedIds.length
            || (mode !== "manual" && !countField.acceptableInput)) return
        const ids = selectedIds.slice()
        if (mode !== "manual") SpokenMediaService.setPodcastSyncSelection(show.id, mode, episodeCount, unplayedOnly)
        picked(ids)
        close()
    }
    width: Math.min(Theme.spokenSheetWidth, parent ? parent.width - Theme.spaceHuge : Theme.spokenSheetWidth)
    height: Math.min(Theme.spokenSheetHeight, parent ? parent.height - Theme.spaceHuge : Theme.spokenSheetHeight)
    padding: Theme.spaceLg
    modal: true
    background: Rectangle { color: Theme.surfaceBg; radius: Theme.radiusMd; border.width: Theme.hairline; border.color: Theme.borderLight }
    Overlay.modal: Rectangle { color: Theme.spokenScrim }
    contentItem: ColumnLayout {
        spacing: Theme.spaceMd
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: picker.show.title || "Choose episodes"; elide: Text.ElideRight; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody }
            SpokenButton { text: "×"; onClicked: picker.close() }
        }
        PivotBar {
            id: modeTabs
            objectName: "podcastSelectionMode"
            tabs: ["Newest", "Oldest", "Manual"]
            current: "Newest"
            onCurrentChanged: picker.mode = current.toLowerCase()
        }
        RowLayout {
            visible: picker.mode !== "manual"
            Layout.fillWidth: true
            Text { text: "Episodes"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
            SpokenButton { text: "−"; enabled: picker.episodeCount > 1; onClicked: picker.episodeCount-- }
            ZuneSearchField {
                id: countField
                objectName: "podcastSelectionCount"
                Layout.preferredWidth: Theme.spokenThumbSize
                text: String(picker.episodeCount)
                validator: IntValidator { bottom: 1; top: 5000 }
                inputMethodHints: Qt.ImhDigitsOnly
                Accessible.name: "Number of podcast episodes"
                onEditingFinished: { if (acceptableInput) picker.episodeCount = Number(text); text = String(picker.episodeCount) }
            }
            SpokenButton { text: "+"; enabled: picker.episodeCount < 5000; onClicked: picker.episodeCount++ }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            visible: picker.mode !== "manual"
            SpokenCheckBox {
                objectName: "podcastSelectionUnplayed"
                Accessible.name: "Only unplayed episodes"
                checked: picker.unplayedOnly
                onClicked: picker.unplayedOnly = !picker.unplayedOnly
            }
            Text { text: "Only unplayed episodes"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
        }
        Text {
            Layout.fillWidth: true
            text: "Choose how many newest or oldest episodes you want. Episodes already on this Zune are marked and stay out of the queue. Review the selection before adding it to the queue. Downloads happen during sync. To replay a cleaned-up episode, choose Keep on Zune from its right-click menu."
            wrapMode: Text.Wrap; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption
        }
        ListView {
            objectName: "podcastSelectionRows"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: picker.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {}
            delegate: RowLayout {
                required property var modelData
                readonly property var deviceStatus: podcastStatus.deviceFor(modelData.id)
                width: ListView.view.width; height: Theme.spokenRowHeight
                SpokenCheckBox { enabled: !parent.deviceStatus.onDevice; checked: !!picker.checks[modelData.id] && !parent.deviceStatus.onDevice; Accessible.name: "Include " + modelData.title; onClicked: picker.toggle(modelData.id) }
                OnDeviceBadge {
                    objectName: "podcastPickerOnDevice"
                    visible: !!parent.deviceStatus.onDevice
                    size: Theme.locationRowBadgeSize
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Theme.spaceXxs
                    Text { Layout.fillWidth: true; text: modelData.title; elide: Text.ElideRight; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody }
                    Text { Layout.fillWidth: true; text: (modelData.published ? new Date(modelData.published).toLocaleDateString() : "date unknown") + (podcastStatus.played(modelData) ? " · " + podcastStatus.playedLabel(modelData) : "") + (modelData.keepOnDevice ? " · kept on Zune" : ""); elide: Text.ElideRight; color: Theme.textDim; font.pixelSize: Theme.spokenCaption }
                }
            }
            Text { anchors.centerIn: parent; visible: picker.rows.length === 0; text: "No episodes match this selection"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: picker.selectedIds.length + " selected"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
            SpokenButton { objectName: "podcastSelectionAdd"; text: "add to queue"; highlighted: true; enabled: DeviceService.connected && !SyncEngine.isSyncing && picker.selectedIds.length > 0 && (picker.mode === "manual" || countField.acceptableInput); onClicked: picker.addSelection() }
        }
    }
}
