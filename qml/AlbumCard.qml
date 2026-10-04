import QtQuick
import QtQuick.Controls.Basic
import Qt5Compat.GraphicalEffects

// Port of MusicRows.swift AlbumCard — square art with hover pink border
// and slight scale; title + subtitle ALWAYS visible beneath (never
// hover-only). Hover play/queue circles arrive with the player.
Item {
    id: card

    property bool onDevice: false
    property string name
    property string subtitle
    property int artItemId: 0
    // Local-library art (device art keys by artItemId instead)
    property bool localArt: false
    property string artArtist: ""
    property string artAlbum: ""
    property string artFilepath: ""
    property var artworkDiscovery: ({})
    readonly property string missingArtStatus: {
        if (!localArt || albumImage.artUrl) return ""
        switch (artworkDiscovery.status) {
        case "checkingLocal": return "Checking local artwork"
        case "lookingUp": return "Looking up cover"
        case "needsMatch": return "Needs a match"
        case "noArtwork": return "No cover available"
        case "providerUnavailable": return "Provider unavailable"
        case "generalCoverAvailable": return "General cover available"
        default: return ""
        }
    }
    property real cardSize: 200

    signal selected()
    signal contextRequested()   // right-click — host decides the menu
    signal played()             // centered hover ▶ (library side)
    signal addAll()             // ＋: whole album
    signal pickParts()          // ▾: track picker
    // Device-side cards: quick action is ⤓ save-to-library, no ▶
    property bool deviceSide: false
    property bool addArmed: false
    property bool showQuickAdd: true
    // Host supplies a lazy payload; dragging requires a connected Zune.
    property var dragTracks: null

    // Composed hover (see PosterCard) — overlay areas OR'd in
    readonly property bool hovering:
        albumRootArea.containsMouse || albumPlayArea.containsMouse
        || albumSplitBtn.hovered

    implicitWidth: cardSize
    implicitHeight: cardSize + 38

    scale: hovering ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.InOutQuad } }

    Column {
        width: parent.width
        spacing: Theme.spaceXs

        Item {
            width: card.cardSize
            height: card.cardSize

            AlbumArt {
                id: albumImage
                anchors.fill: parent
                itemId: card.artItemId
                local: card.localArt
                artist: card.artArtist
                album: card.artAlbum
                filepath: card.artFilepath
                cornerRadius: Theme.radiusMd
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: "transparent"
                border.width: 1
                border.color: card.hovering ? Qt.rgba(0.83, 0.21, 0.48, 0.4) : Theme.border
            }

            // Contract tile anatomy: centered hover ▶ (library only)
            Rectangle {
                anchors.centerIn: parent
                width: 44
                height: 44
                radius: 22
                color: albumPlayArea.containsMouse
                    ? Qt.alpha(Theme.pink, 0.92) : Qt.rgba(0, 0, 0, 0.55)
                border.width: 1
                border.color: albumPlayArea.containsMouse
                    ? Theme.pink : Theme.activePink
                visible: !card.deviceSide
                opacity: card.hovering ? 1 : 0
                scale: albumPlayArea.pressed ? 0.88
                     : albumPlayArea.containsMouse ? 1.12
                     : card.hovering ? 1 : 0.8
                Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                Behavior on scale { NumberAnimation { duration: Theme.motionFast } }
                Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                Text {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: 2
                    text: "▶"
                    font.pixelSize: 16
                    color: "white"
                }
                MouseArea {
                    id: albumPlayArea
                    anchors.fill: parent
                    enabled: card.hovering && !card.deviceSide
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.played()
                }
            }

            // Split add — top-right (＋▾ library, ⤓▾ device)
            SplitAddButton {
                id: albumSplitBtn
                objectName: "albumQuickAdd"
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spaceXs
                visible: card.showQuickAdd
                enabled: DeviceService.connected || (!card.deviceSide && card.addArmed)
                opacity: card.hovering ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                glyph: card.deviceSide ? "⤓" : "＋"
                armed: card.addArmed
                onAddAll: { if (enabled) card.addAll() }
                onPickParts: { if (enabled) card.pickParts() }
            }
        }

        Column {
            width: card.cardSize
            spacing: 1

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: card.name
                font.pixelSize: 14
                font.weight: Font.Light
                color: card.hovering ? Theme.textPrimary : Theme.textMid
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: card.missingArtStatus || card.subtitle
                font.pixelSize: 11
                font.weight: Font.Light
                color: Theme.textDim
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: albumRootArea
        objectName: "albumDragArea"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                card.contextRequested()
            else
                card.selected()
        }
        // Threshold keeps plain clicks intact. The connected-device
        // gate also applies while the playlist builder is open.
        readonly property bool dragArmed:
            card.dragTracks !== null
            && DeviceService.connected
        drag.target: dragArmed ? albumDragGhost : null
        preventStealing: dragArmed
        onPressed: mouse => {
            const p = albumRootArea.mapToItem(Overlay.overlay, mouse.x, mouse.y)
            albumDragGhost.x = p.x + 8
            albumDragGhost.y = p.y + 8
        }
        onReleased: {
            if (albumDragGhost.Drag.active)
                albumDragGhost.Drag.drop()
        }
        // Overlay buttons (▶, ＋▾) must win the click
        z: -1
    }

    // Drag ghost — album pill with a little drop shadow, floating on
    // the window overlay above every island.
    Item {
        id: albumDragGhost
        parent: Overlay.overlay ?? card
        width: albumGhostPill.width
        height: albumGhostPill.height
        visible: Drag.active
        Drag.active: albumRootArea.drag.active && DeviceService.connected
        Drag.keys: card.deviceSide ? ["zuuned-device-tracks"] : ["zuuned-tracks"]
        Drag.hotSpot.x: 10
        Drag.hotSpot.y: 10
        Drag.onActiveChanged: Drag.active ? TrayState.dragStarted()
                                          : TrayState.dragEnded()
        property var dragTracks: card.dragTracks
        property var dragPayload: card.dragTracks

        // You're moving the ALBUM, so the ghost IS the album — its
        // art, slightly tilted, floating on a drop shadow.
        Item {
            id: albumGhostPill
            width: 72
            height: 72
            rotation: 4

            AlbumArt {
                anchors.fill: parent
                itemId: card.artItemId
                local: card.localArt
                artist: card.artArtist
                album: card.artAlbum
                filepath: card.artFilepath
                cornerRadius: 4
            }
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: "transparent"
                border.width: 1
                border.color: Qt.alpha(Theme.orange, 0.75)
            }
        }
        DropShadow {
            anchors.fill: albumGhostPill
            source: albumGhostPill
            radius: 18
            samples: 25
            verticalOffset: 8
            color: Qt.rgba(0, 0, 0, 0.6)
            rotation: albumGhostPill.rotation
            z: -1
        }
    }

    // On-device badge — top-left (top-right belongs to the split add)
    OnDeviceBadge {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        size: 28
        visible: card.onDevice
    }
}
