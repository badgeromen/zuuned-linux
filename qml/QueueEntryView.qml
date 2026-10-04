import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One transfer-queue entry, with its optional season/album header. Actions
// stay in a fixed right-hand slot; hovering never shifts the title or count.
ColumnLayout {
    id: entry

    required property var entryId
    required property string type
    required property string title
    required property string artist
    required property string albumartist
    required property string album
    required property string filepath
    required property string status
    required property string statusNote
    required property real progress
    required property string posterPath
    required property string series
    required property string groupKey

    property var groupInfo: null
    property bool expanded: false
    property bool syncLocked: false
    readonly property bool isGrouped: !!groupInfo && groupInfo.count >= 2
    readonly property bool entryInFlight: status === "transcoding" || status === "syncing"
    readonly property bool headerShown: isGrouped && groupInfo.firstEntryId === entryId
    readonly property bool rowShown: !isGrouped || expanded || entryInFlight
    readonly property string groupNoun: groupInfo && groupInfo.kind === "series" ? "season"
        : groupInfo && groupInfo.kind === "photos" ? "photo album" : "album"
    readonly property string groupDetail: {
        if (!groupInfo) return ""
        const noun = groupInfo.kind === "series" ? "episodes"
            : groupInfo.kind === "photos" ? "photos" : "tracks"
        const count = groupInfo.failedCount > 0
            ? groupInfo.failedCount + " failed / " + groupInfo.count
            : groupInfo.doneCount > 0
                ? groupInfo.doneCount + "/" + groupInfo.count + " done"
                : groupInfo.count + " " + noun
        return (groupInfo.subtitle ? groupInfo.subtitle + " · " : "") + count
    }

    signal toggleRequested(string key)
    signal removeGroupRequested(string key)
    signal removeEntryRequested(var id)

    function requestRemoval(wholeGroup) {
        if (syncLocked || entryInFlight) return
        if (wholeGroup) {
            if (isGrouped) removeGroupRequested(groupKey)
        } else {
            removeEntryRequested(entryId)
        }
    }
    function openRemoveMenu(wholeGroup) {
        removeMenu.wholeGroup = wholeGroup
        removeMenu.popup()
    }

    Layout.fillWidth: true
    spacing: 0
    visible: headerShown || rowShown

    component RemoveAction: Item {
        id: actionSlot
        property bool wholeGroup: false
        implicitWidth: Theme.spaceXxxl
        implicitHeight: Theme.spaceXxxl
        Layout.minimumWidth: implicitWidth
        Layout.maximumWidth: implicitWidth
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight
        ToolButton {
            id: action
            objectName: actionSlot.wholeGroup ? "queueRemoveGroup" : "queueRemoveEntry"
            anchors.fill: parent
            readonly property string description: actionSlot.wholeGroup
                ? "Remove " + entry.groupNoun + " from queue" : "Remove from queue"
            padding: 0
            hoverEnabled: true
            enabled: !entry.syncLocked && !entry.entryInFlight
            Accessible.name: description
            opacity: enabled ? 1 : Theme.disabledOpacity
            contentItem: Text {
                text: "−"
                font.pixelSize: Theme.spaceXl
                color: action.hovered ? Theme.activePink : Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusMd
                color: action.hovered ? Theme.cardHover : Theme.transparent
                border.width: action.visualFocus ? Theme.hairline : 0
                border.color: Theme.focusRing
            }
            ToolTip.visible: hovered
            ToolTip.text: description
            onClicked: entry.requestRemoval(actionSlot.wholeGroup)
        }
        // A disabled control must not pass a click into the group header
        // underneath it. Keep its occupied space inert until sync finishes.
        MouseArea {
            anchors.fill: parent
            enabled: !action.enabled
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            ToolTip.visible: containsMouse
            ToolTip.text: "Available when syncing finishes"
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton)
                    entry.openRemoveMenu(actionSlot.wholeGroup)
            }
        }
    }

    component QueueArt: Item {
        implicitWidth: Theme.spaceXxl
        implicitHeight: Theme.spaceXxl
        AlbumArt {
            anchors.fill: parent
            visible: entry.type === "track"
            local: true
            artist: entry.albumartist || entry.artist
            album: entry.album
            filepath: entry.filepath
            cornerRadius: Theme.radiusSm
        }
        Rectangle {
            anchors.fill: parent
            visible: entry.type !== "track"
            color: Theme.artworkPlaceholder
            radius: Theme.radiusSm
            clip: true
            ArtworkImage {
                anchors.fill: parent
                treatmentEnabled: entry.type !== "photo"
                source: entry.type === "photo" && entry.filepath
                    ? "file://" + entry.filepath
                    : entry.posterPath ? "file://" + entry.posterPath : ""
                sourceSize.width: Theme.spaceHuge
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }
    }

    Loader {
        active: entry.headerShown
        visible: active
        Layout.fillWidth: true
        sourceComponent: Rectangle {
            objectName: "queueGroupHeader"
            implicitHeight: Theme.spaceHuge
            radius: Theme.radiusSm
            color: headerHover.hovered ? Theme.rowHoverPink : Theme.cardBg
            HoverHandler { id: headerHover }
            MouseArea {
                objectName: "queueGroupArea"
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton) entry.openRemoveMenu(true)
                    else entry.toggleRequested(entry.groupKey)
                }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spaceXxs
                spacing: Theme.spaceXxs
                Text {
                    text: entry.expanded ? "▾" : "▸"
                    font.pixelSize: Theme.customizeCaptionSize
                    color: Theme.pink
                }
                QueueArt {}
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0
                    Text {
                        objectName: "queueGroupTitle"
                        text: entry.groupInfo.title
                        font.pixelSize: Theme.spaceMd
                        font.weight: Font.Medium
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        objectName: "queueGroupDetail"
                        text: entry.groupDetail
                        font.pixelSize: Theme.customizeCaptionSize
                        color: entry.groupInfo.failedCount > 0 ? Theme.pink
                            : entry.groupInfo.doneCount >= entry.groupInfo.count ? Theme.green
                            : Theme.textSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                RemoveAction { wholeGroup: true }
            }
        }
    }

    Loader {
        active: entry.rowShown
        visible: active
        Layout.fillWidth: true
        Layout.leftMargin: entry.isGrouped ? Theme.spaceMd : 0
        sourceComponent: Rectangle {
            objectName: "queueEntryRow"
            implicitHeight: Math.max(Theme.spaceHuge, rowText.implicitHeight + Theme.spaceSm * 2)
            color: rowHover.hovered ? Theme.rowHoverPink : Theme.transparent
            HoverHandler { id: rowHover }
            MouseArea {
                objectName: "queueEntryArea"
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: entry.openRemoveMenu(false)
            }
            RowLayout {
                anchors.fill: parent
                anchors.topMargin: Theme.spaceSm
                anchors.bottomMargin: Theme.spaceSm
                spacing: Theme.spaceXxs
                QueueArt { Layout.alignment: Qt.AlignTop }
                ColumnLayout {
                    id: rowText
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: Theme.spaceXxxs
                    Text {
                        objectName: "queueEntryTitle"
                        text: entry.title
                        font.pixelSize: Theme.customizeBodySize
                        font.weight: Font.Light
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: entry.type === "video" ? (entry.series || "movie")
                            : entry.type === "photo" ? entry.album : entry.artist
                        font.pixelSize: Theme.customizeCaptionSize
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        visible: entry.entryInFlight
                        Layout.fillWidth: true
                        implicitHeight: Theme.spaceXs
                        radius: Theme.radiusSm
                        color: Theme.cardHover
                        Rectangle {
                            height: parent.height
                            width: parent.width * Math.max(0, Math.min(1, entry.progress))
                            radius: parent.radius
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.pink }
                                GradientStop { position: 1; color: Theme.activePink }
                            }
                            Behavior on width { NumberAnimation { duration: Theme.motionFast } }
                        }
                    }
                    Text {
                        visible: (entry.status === "failed" || entry.status === "sent") && entry.statusNote !== ""
                        text: entry.statusNote
                        font.pixelSize: Theme.customizeCaptionSize
                        color: Theme.activePink
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                Text {
                    Layout.preferredWidth: Theme.spaceMd
                    Layout.alignment: Qt.AlignTop
                    text: entry.entryInFlight ? "" : entry.status === "verified" ? "✓"
                        : entry.status === "sent" ? "!" : entry.status === "failed" ? "✕" : entry.status === "skipped" ? "–" : "•"
                    font.pixelSize: Theme.spaceMd
                    color: entry.status === "verified" ? Theme.green
                        : entry.status === "failed" ? Theme.pink : Theme.textDim
                }
                RemoveAction { Layout.alignment: Qt.AlignTop }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                color: Theme.separator
            }
        }
    }

    ZuneMenu {
        id: removeMenu
        objectName: "queueRemoveMenu"
        property bool wholeGroup: false
        ZuneMenuItem {
            objectName: "queueMenuRemoveAction"
            text: removeMenu.wholeGroup ? "Remove " + entry.groupNoun + " from queue" : "Remove from queue"
            enabled: !entry.syncLocked && !entry.entryInFlight
            onTriggered: entry.requestRemoval(removeMenu.wholeGroup)
        }
    }
}
