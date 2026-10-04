import QtQuick
import Qt5Compat.GraphicalEffects

// Port of MusicRows.swift ArtistCard — round-photo grid cell: circular
// artist image (fallback: flat dark circle), pink ring + slight
// scale on hover, name + track count beneath.
Item {
    id: card

    property string name
    property int count: 0
    property int artItemId: 0
    // Local portraits use artist photos only. Device browsing can use
    // its representative artwork when no artist photo is available.
    property bool localArt: false
    property string photoUrl: ""
    property var artworkDiscovery: ({})
    readonly property string missingArtStatus: {
        if (!localArt || photoUrl) return ""
        switch (artworkDiscovery.status) {
        case "checkingLocal": return "Checking local artwork"
        case "lookingUp": return "Looking up portrait"
        case "needsMatch": return "Needs a match"
        case "noArtwork": return "No portrait available"
        case "providerUnavailable": return "Provider unavailable"
        default: return ""
        }
    }
    property real diameter: 140

    signal selected()
    signal rightClicked()
    // UX-2 drag: host supplies () => [syncItems] for this artist
    property var dragTracks: null
    // Device artist → drags to the library sidebar (pull whole artist).
    property bool deviceSide: false

    property bool hovering: false

    // Gallery cells scale the circle; the card tracks its diameter
    implicitWidth: Math.max(160, diameter + 20)
    implicitHeight: diameter + 44

    scale: hovering ? 1.03 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.InOutQuad } }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spaceSm

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: card.diameter
            height: card.diameter

            // Keep the portrait's shape when no image is available.
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.artworkPlaceholder
            }

            Loader {
                anchors.fill: parent
                active: !card.localArt
                sourceComponent: AlbumArt {
                    itemId: card.artItemId
                    cornerRadius: width / 2
                    showPlaceholder: false
                }
            }

            // Artist photo; an empty/loading/broken local image leaves
            // the flat circle visible, never an unrelated album cover.
            ArtworkImage {
                id: artistPhoto
                anchors.fill: parent
                source: card.photoUrl
                visible: status === Image.Ready
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                layer.enabled: card.photoUrl !== ""
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: card.diameter
                        height: card.diameter
                        radius: width / 2
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: card.hovering ? Qt.rgba(0.83, 0.21, 0.48, 0.5) : "transparent"
            }
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 1
            width: Math.max(150, card.diameter + 10)

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
                text: card.missingArtStatus || card.count + (card.count === 1 ? " track" : " tracks")
                font.pixelSize: 11
                font.weight: Font.Light
                color: Theme.textDim
            }
        }
    }

    MouseArea {
        id: artistArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onEntered: card.hovering = true
        onExited: card.hovering = false
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                card.rightClicked()
            else
                card.selected()
        }
        readonly property bool dragArmed:
            card.dragTracks !== null
            && DeviceService.connected
        drag.target: dragArmed ? artistGhost : null
        preventStealing: dragArmed
        onPressed: mouse => artistGhost.place(artistArea, mouse)
        onReleased: artistGhost.drop()
    }

    DragGhost {
        id: artistGhost
        area: artistArea
        dragTracks: card.dragTracks
        dragKind: card.deviceSide ? "device-tracks" : "tracks"

        // The same portrait as the card, shrunk. Only device artists
        // may fall back to their representative device artwork.
        Item {
            width: 64
            height: 64
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: Theme.artworkPlaceholder
                border.width: 1
                border.color: Qt.alpha(Theme.orange, 0.75)
                clip: true
                Loader {
                    anchors.fill: parent
                    anchors.margins: 1
                    active: !card.localArt && artistPhoto.status !== Image.Ready
                    sourceComponent: AlbumArt {
                        itemId: card.artItemId
                        cornerRadius: width / 2
                        showPlaceholder: false
                    }
                }
                ArtworkImage {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: card.photoUrl
                    visible: status === Image.Ready
                    fillMode: Image.PreserveAspectCrop
                    layer.enabled: card.photoUrl !== ""
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: 62; height: 62; radius: 31
                        }
                    }
                }
            }
        }
    }
}
