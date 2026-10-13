import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Shared by setup (a staged draft) and Settings (live preferences).
ColumnLayout {
    id: root
    property bool autoQueue: false
    property int episodeCount: 10
    property bool autoCleanup: false
    property bool customCount: episodeCount !== 5 && episodeCount !== 10
    signal queuePicked(bool value)
    signal countPicked(int value)
    signal cleanupPicked(bool value)
    spacing: Theme.spaceMd

    component Explanation: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.spokenCaption
        color: Theme.textSecondary
    }
    component PreferenceCheck: SpokenCheckBox {
        id: check
        Layout.fillWidth: true
        contentItem: Text {
            text: check.text
            leftPadding: check.indicator.width + check.spacing
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.spokenBody
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
        }
    }
    PreferenceCheck {
        objectName: "podcastAutoQueueToggle"
        text: "Queue starred shows when a Zune connects"
        checked: root.autoQueue
        onClicked: root.queuePicked(checked)
    }
    Explanation {
        text: "Refresh starred shows and queue their newest unplayed episodes. Episodes already on the Zune count toward the selection. Review the queue, then press Sync."
    }
    Text { text: "Episodes per show"; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody }
    RowLayout {
        spacing: Theme.spaceSm
        Repeater {
            model: [5, 10]
            delegate: SpokenButton {
                required property int modelData
                objectName: "podcastCount" + modelData
                text: String(modelData)
                highlighted: !root.customCount && root.episodeCount === modelData
                onClicked: { root.customCount = false; root.countPicked(modelData) }
            }
        }
        SpokenButton {
            objectName: "podcastCountCustom"
            text: "custom"
            highlighted: root.customCount
            onClicked: root.customCount = true
        }
        ZuneSearchField {
            objectName: "podcastAutoQueueCount"
            visible: root.customCount
            Layout.preferredWidth: Theme.spokenThumbSize
            text: String(root.episodeCount)
            validator: IntValidator { bottom: 1; top: 5000 }
            inputMethodHints: Qt.ImhDigitsOnly
            Accessible.name: "Default number of podcast episodes per show"
            onEditingFinished: {
                if (acceptableInput) root.countPicked(Number(text))
                text = Qt.binding(() => String(root.episodeCount))
            }
        }
    }
    Explanation {
        text: "A saved show count overrides this default. The show + uses its saved selection; its arrow offers newest, oldest or individual episodes."
    }
    PreferenceCheck {
        objectName: "podcastAutoCleanupToggle"
        text: "Remove completed episodes at the next sync"
        checked: root.autoCleanup
        onClicked: root.cleanupPicked(checked)
    }
    Explanation {
        text: "Only episodes marked played by this Zune are removed. Local downloads stay. Choose Keep on Zune from an episode's right-click menu to protect it, even beyond the episode count."
    }
}
