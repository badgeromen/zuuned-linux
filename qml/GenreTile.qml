import QtQuick
import Qt5Compat.GraphicalEffects

// A genre keeps its automatic album mosaic until the collection has its
// own cover. The host owns navigation, customization, and transfer scope.
Item {
    id: tile

    property string name
    property int count: 0
    // Up to 4 album-cover urls — the mac's 2x2 mosaic
    property var artUrls: []
    property url customArt: ""
    // UX-2 drag: host supplies () => [syncItems] for this genre
    property var dragTracks: null
    property bool deviceSide: false

    // Gallery cells scale the tile
    property real tileSize: 200
    implicitWidth: tileSize
    implicitHeight: tileSize + 38

    property bool hovering: false
    signal selected()
    signal contextRequested()

    Column {
        width: parent.width
        spacing: Theme.spaceSm

        Rectangle {
            id: mosaicBox
            width: tile.tileSize
            height: tile.tileSize
            radius: Theme.radiusMd
            color: Theme.artworkPlaceholder
            border.width: 1
            border.color: tile.hovering ? Qt.rgba(0.83, 0.21, 0.48, 0.4) : Theme.border
            clip: true
            // Rounded ART needs a mask — a radius on the container
            // doesn't clip the Image (QML clip is rectangular)
            layer.enabled: true
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: mosaicBox.width; height: mosaicBox.height
                    radius: Theme.radiusMd
                }
            }

            // 2x2 album mosaic; a single cover fills the tile
            Grid {
                anchors.fill: parent
                anchors.margins: 1
                columns: tile.artUrls.length > 1 ? 2 : 1
                objectName: "genreTileMosaic"
                visible: tile.customArt.toString() === "" && tile.artUrls.length > 0

                Repeater {
                    model: tile.artUrls.slice(0, tile.artUrls.length > 1 ? 4 : 1)
                    ArtworkImage {
                        required property var modelData
                        width: tile.artUrls.length > 1
                               ? (tile.tileSize - 2) / 2 : tile.tileSize - 2
                        height: width
                        source: modelData
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
            }
            ArtworkImage {
                objectName: "genreTileCustomArt"
                anchors.fill: parent
                source: tile.customArt
                visible: tile.customArt.toString() !== "" && status === Image.Ready
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: Math.ceil(tile.tileSize * 2)
                asynchronous: true
            }
        }

        Column {
            width: tile.tileSize
            spacing: 1

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: tile.name
                font.pixelSize: 14
                font.weight: Font.Light
                color: tile.hovering ? Theme.textPrimary : Theme.textMid
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: tile.count + (tile.count === 1 ? " track" : " tracks")
                font.pixelSize: 11
                font.weight: Font.Light
                color: Theme.textDim
            }
        }
    }

    MouseArea {
        id: genreArea
        objectName: "genreTileArea"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onEntered: tile.hovering = true
        onExited: tile.hovering = false
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => mouse.button === Qt.RightButton
            ? tile.contextRequested() : tile.selected()
        readonly property bool dragArmed:
            tile.dragTracks !== null
            && DeviceService.connected
        drag.target: dragArmed ? genreGhost : null
        preventStealing: dragArmed
        onPressed: mouse => genreGhost.place(genreArea, mouse)
        onReleased: genreGhost.drop()
    }

    DragGhost {
        id: genreGhost
        area: genreArea
        dragTracks: tile.dragTracks
        dragKind: tile.deviceSide ? "device-tracks" : "tracks"

        Rectangle {
            width: 72
            height: 48
            radius: Theme.radiusMd
            color: Theme.cardActive
            border.width: 1
            border.color: Qt.alpha(Theme.orange, 0.75)
            ArtworkImage {
                anchors.fill: parent
                anchors.margins: 1
                source: tile.customArt.toString() !== "" ? tile.customArt
                    : tile.artUrls.length > 0 ? tile.artUrls[0] : ""
                fillMode: Image.PreserveAspectCrop
                visible: source.toString() !== ""
                opacity: 0.5
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 8
                horizontalAlignment: Text.AlignHCenter
                text: tile.name
                font.pixelSize: 12
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
        }
    }
}
