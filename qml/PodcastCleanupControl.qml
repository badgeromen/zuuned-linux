import QtQuick
import QtQuick.Layouts
import Zuuned

RowLayout {
    spacing: Theme.spaceSm
    SpokenCheckBox {
        objectName: "podcastPageCleanupToggle"
        checked: AppSettings.podcastAutoCleanup
        enabled: !SyncEngine.isSyncing
        Accessible.name: "Remove completed episodes from Zune on sync"
        onClicked: AppSettings.podcastAutoCleanup = !AppSettings.podcastAutoCleanup
    }
    Text {
        Layout.fillWidth: true
        text: "Remove completed episodes from Zune on sync"
        color: Theme.textSecondary
        font.pixelSize: Theme.spokenCaption
        wrapMode: Text.Wrap
    }
}
