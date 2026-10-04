import QtQuick
import Qt5Compat.GraphicalEffects

// The same image surface is used for the large sleeve and the source strip.
// The item's shape stays consistent when its artwork changes.
Item {
    id: root
    property url source
    property string kind: "album"
    property string title: ""
    property var artUrls: []
    property bool selected: false
    property bool showSelection: false
    property bool hovered: false
    property bool showShadow: false
    readonly property int status: picture.status
    readonly property real cornerRadius: kind === "artist" ? width / 2 : Theme.radiusMd

    Rectangle {
        id: surface
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.artworkPlaceholder
        visible: root.kind !== "mixtape"
        layer.enabled: root.showShadow
        layer.effect: DropShadow {
            radius: Theme.spaceXl
            samples: 1 + Theme.spaceXl * 2
            verticalOffset: Theme.spaceSm
            color: Theme.customizeShadow
        }
    }
    Grid {
        id: collectionMosaic
        objectName: "customizeCollectionMosaic"
        anchors.fill: parent
        visible: root.kind === "genre" && root.source.toString() === "" && root.artUrls.length > 0
        columns: root.artUrls.length > 1 ? 2 : 1
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: root.width; height: root.height
                radius: root.cornerRadius
            }
        }
        Repeater {
            model: root.artUrls.slice(0, root.artUrls.length > 1 ? 4 : 1)
            ArtworkImage {
                required property var modelData
                width: root.width / collectionMosaic.columns
                height: root.height / collectionMosaic.columns
                source: modelData
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }
    }
    ArtworkImage {
        id: picture
        objectName: "customizeArtworkImage"
        anchors.fill: parent
        source: root.source
        sourceSize.width: Math.ceil(root.width * 2)
        sourceSize.height: Math.ceil(root.height * 2)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: root.kind !== "mixtape" && status === Image.Ready
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: root.width; height: root.height
                radius: root.cornerRadius
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: root.kind === "mixtape"
        sourceComponent: CassetteTile {
            objectName: "customizeCassette"
            name: root.title
            labelArt: root.source.toString() !== "" ? root.source
                : root.artUrls.length > 0 ? root.artUrls[0] : ""
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.transparent
        border.width: root.showSelection && root.selected ? Theme.spaceXxxs : 1
        border.color: root.showSelection && root.selected ? Theme.pink
                    : root.hovered ? Theme.activePink : Theme.glassHighlight
        Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
    }
    Rectangle {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceSm
        width: Theme.spaceXl; height: width; radius: width / 2
        color: Theme.pink
        visible: root.showSelection && root.selected
        Text {
            anchors.centerIn: parent; text: "✓"
            font.pixelSize: Theme.customizeCaptionSize; color: Theme.textPrimary
        }
    }
}
