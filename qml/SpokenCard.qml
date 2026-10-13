import QtQuick
import QtQuick.Controls.Basic
import Zuuned

Item {
    id: card
    property var entry: ({})
    property bool book: false
    property bool onDevice: false
    property real cardSize: Theme.spokenCardSize
    property bool transferReady: false
    readonly property bool organizing: book && BookTrayState.building
    readonly property bool dragArmed: organizing || (transferReady && DeviceService.connected)
    signal played()
    signal favoriteRequested()
    readonly property bool hovering: hover.containsMouse || playArea.containsMouse || addButton.hovered || starButton.hovered
    property bool playable: false
    scale: hovering ? 1.02 : 1
    Behavior on scale { NumberAnimation { duration: Theme.motionFast } }
    signal selected()
    signal contextRequested()
    signal addAll()
    signal pickParts()
    implicitWidth: cardSize
    implicitHeight: art.height + Theme.spokenCardCaption
    Rectangle {
        id: art
        width: card.cardSize
        height: card.book ? width * Theme.spokenBookRatio : width
        radius: Theme.radiusMd
        color: Theme.artworkPlaceholder
        border.width: Theme.hairline
        border.color: card.hovering ? Theme.pink : Theme.border
        RoundedArtwork {
            anchors.fill: parent
            source: { void SpokenMediaService.revision; return SpokenMediaService.cover(card.entry.id ?? "") }
            fillMode: Image.PreserveAspectCrop
        }
        MouseArea {
            id: hover
            objectName: "spokenCardDragArea"
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => mouse.button === Qt.RightButton ? card.contextRequested() : card.selected()
            drag.target: card.dragArmed ? cardGhost : null
            preventStealing: card.dragArmed
            onPressed: mouse => cardGhost.place(this, mouse)
            onReleased: cardGhost.drop()
        }
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spaceXl * 2; height: width; radius: width / 2
            color: playArea.containsMouse ? Theme.pink : Theme.spokenScrim
            border.width: Theme.hairline; border.color: Theme.activePink
            visible: card.playable
            opacity: card.hovering ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
            Text { anchors.centerIn: parent; text: "▶"; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody }
            MouseArea { id: playArea; anchors.fill: parent; hoverEnabled: true; enabled: card.playable && card.hovering; cursorShape: Qt.PointingHandCursor; onClicked: card.played() }
        }
        SpokenButton {
            id: starButton
            objectName: "spokenCardStar"
            anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.spaceXs
            text: card.entry.favorite ? "★" : "☆"
            Accessible.name: card.entry.favorite ? "Unstar " + card.entry.title : "Star " + card.entry.title
            flat: true; highlighted: !!card.entry.favorite
            width: Theme.spaceXl + Theme.spaceSm
            height: width
            padding: 0
            horizontalPadding: 0
            background: Rectangle {
                radius: width / 2
                color: Theme.spokenScrim
                border.width: starButton.activeFocus ? Theme.hairline : 0
                border.color: Theme.focusRing
            }
            opacity: card.entry.favorite || card.hovering || activeFocus ? 1 : 0
            onClicked: card.favoriteRequested()
        }
        OnDeviceBadge {
            objectName: "spokenCardOnDevice"
            anchors.bottom: parent.bottom; anchors.right: parent.right
            anchors.margins: Theme.spaceXs
            visible: card.onDevice
        }
        SplitAddButton {
            id: addButton
            objectName: "spokenSplitAdd"
            showPick: !card.book
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spaceXs
            opacity: card.hovering ? 1 : 0
            enabled: card.organizing ? !BookTrayState.locked : DeviceService.connected && card.transferReady
            onAddAll: card.organizing ? BookTrayState.addBooks([card.entry.id]) : card.addAll()
            onPickParts: card.pickParts()
        }
    }
    DragGhost {
        id: cardGhost
        objectName: "spokenCardGhost"
        area: hover
        localBookDrag: card.organizing
        dragKind: card.book ? "audiobook-books" : "podcast-shows"
        dragName: card.entry.title ?? ""
        dragTracks: () => [card.entry.id]
        property var openTransferPicker: () => card.pickParts()
        Rectangle {
            width: Theme.spokenDragCoverSize
            height: Theme.spokenDragCoverSize
            radius: Theme.radiusMd
            color: Theme.artworkPlaceholder
            border.width: Theme.hairline
            border.color: Theme.activePink
            RoundedArtwork {
                anchors.fill: parent
                anchors.margins: Theme.hairline
                source: { void SpokenMediaService.revision; return SpokenMediaService.cover(card.entry.id ?? "") }
                fillMode: Image.PreserveAspectCrop
            }
        }
    }
    Column {
        anchors.top: art.bottom
        anchors.topMargin: Theme.spaceSm
        width: parent.width
        spacing: Theme.spaceXxs
        Text { width: parent.width; text: card.entry.title ?? ""; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody; elide: Text.ElideRight }
        Text { width: parent.width; text: card.entry.author ?? ""; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption; elide: Text.ElideRight }
        Text { width: parent.width; text: card.book ? ((card.entry.count ?? 0) === 1 ? "1 audio file" : (card.entry.count ?? 0) + " audio files") : (card.entry.count ?? 0) + " episodes"; color: Theme.textDim; font.pixelSize: Theme.spokenCaption; elide: Text.ElideRight }
    }
}
