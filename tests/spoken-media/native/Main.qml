import QtQuick
import QtQuick.Controls.Basic
import Zuuned
ApplicationWindow {
    id: window
    width: 1400; height: 1000; visible: true; color: Theme.bg
    property bool islandMode: false
    property alias testConnected: connection.enabled
    property string droppedKind: ""
    property var droppedIds: []
    Binding { id: connection; property bool enabled: false; target: DeviceService; property: "connected"; value: enabled }
    DropArea {
        objectName: "spokenTestDeviceDrop"
        x: window.width - 150; y: 60; width: 130; height: 170
        keys: ["zuuned-podcasts", "zuuned-podcast-shows", "zuuned-audiobooks", "zuuned-audiobook-books"]
        enabled: DeviceService.connected
        onDropped: drop => {
            if (!DeviceService.connected) { drop.accepted = false; return }
            window.droppedKind = drop.source.dragKind
            window.droppedIds = drop.source.dragTracks()
            if (window.droppedKind === "podcast-shows") drop.source.openTransferPicker()
            else if (window.droppedKind === "audiobook-books") SpokenMediaService.queueAudiobooks(window.droppedIds)
            drop.accept()
        }
    }
    SidebarView { objectName: "nativeSidebar"; visible: false; railMode: true; width: 240; height: window.height }
    SpokenMediaPage { objectName: "spokenPage"; anchors.fill: parent; anchors.margins: Theme.spaceXxl; anchors.topMargin: 300; anchors.leftMargin: window.islandMode ? 280 : Theme.spaceXxl }
    QueueRibbonBar {
        objectName: "nativeRibbon"
        anchors.top: parent.top; anchors.topMargin: Theme.spaceMd; anchors.horizontalCenter: parent.horizontalCenter
        visible: !window.islandMode && PlayerService.queue.length > 0
        artUrlFor: item => { void SpokenMediaService.revision; return SpokenMediaService.cover(item.spokenId || "") }
        onMiniPlayerRequested: window.islandMode = true
    }
    NowPlayingIsland {
        objectName: "nativeIsland"
        x: 20; y: 20; width: 230; height: window.height - 40
        shown: window.islandMode
        artUrlFor: item => { void SpokenMediaService.revision; return SpokenMediaService.cover(item.spokenId || "") }
        onCloseRequested: window.islandMode = false
    }
}
