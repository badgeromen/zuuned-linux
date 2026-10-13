import QtQuick
import Qt5Compat.GraphicalEffects

// AlbumDetail's sleeve/disc pose, with artwork supplied by spoken identity.
Item {
    id: shelf
    property url source: ""
    property bool book: false
    property bool hovering: false
    implicitWidth: Theme.spokenShelfWidth
    implicitHeight: Theme.spokenShelfHeight
    Item {
        id: disc
        width: Theme.spokenShelfDiscSize
        height: width
        x: shelf.hovering ? Theme.spokenShelfDiscHoverX : Theme.spokenShelfDiscX
        y: Theme.spaceMd
        Behavior on x { NumberAnimation { duration: Theme.motionBase; easing.type: Easing.OutQuad } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.bg
            border.width: Theme.hairline
            border.color: Theme.borderLight
        }
        Repeater {
            model: Prefs.playerStyle === "vinyl" ? [0.90, 0.78, 0.66, 0.54] : []
            Rectangle {
                required property real modelData
                anchors.centerIn: disc
                width: disc.width * modelData
                height: width
                radius: width / 2
                color: Theme.spokenTransparent
                border.width: Theme.hairline
                border.color: Theme.borderLight
            }
        }
        Item {
            anchors.centerIn: parent
            width: Prefs.playerStyle === "vinyl" ? Theme.spokenShelfLabelSize : Theme.spokenShelfDiscArtSize
            height: width
            layer.enabled: true
            layer.effect: OpacityMask { maskSource: labelMask }
            ArtworkImage { anchors.fill: parent; source: shelf.source; fillMode: Image.PreserveAspectCrop }
        }
        Rectangle {
            id: labelMask
            visible: false
            width: Prefs.playerStyle === "vinyl" ? Theme.spokenShelfLabelSize : Theme.spokenShelfDiscArtSize
            height: width
            radius: width / 2
        }
        Rectangle {
            anchors.centerIn: parent
            width: Theme.spaceSm
            height: width
            radius: width / 2
            color: Theme.bg
            border.width: Theme.hairline
            border.color: Theme.borderLight
        }
    }
    Item {
        id: sleeve
        width: Theme.spokenShelfSleeveSize
        height: width
        y: Theme.spaceSm
        rotation: Theme.spokenShelfTilt
        Rectangle { anchors.fill: parent; color: Theme.artworkPlaceholder; radius: Theme.radiusMd }
        RoundedArtwork {
            id: art
            anchors.fill: parent
            source: shelf.source
            // Keep portrait book covers complete within the sleeve.
            fillMode: shelf.book ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.spokenTransparent
            border.width: Theme.hairline
            border.color: Theme.borderLight
        }
    }
    DropShadow {
        anchors.fill: sleeve
        source: art
        rotation: sleeve.rotation
        radius: Theme.spokenIconSize
        samples: Theme.spokenShadowSamples
        horizontalOffset: Theme.spaceXs
        verticalOffset: Theme.spaceSm
        color: Theme.spokenScrim
        z: -1
    }
}
