import QtQuick
import QtQuick.Controls.Basic

// The user's ZUUNED app mark, shared with the packaged desktop icon.
// Marks a library item that is already on the connected device.
Item {
    id: badge
    property real size: Theme.locationBadgeSize

    width: size
    height: size
    Accessible.role: Accessible.StaticText
    Accessible.name: "On this Zune"

    Image {
        objectName: "zuunedLocationMark"
        anchors.fill: parent
        source: Qt.resolvedUrl("images/zuuned.png")
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
    HoverHandler { id: hover }
    ToolTip {
        id: locationTip
        visible: hover.hovered
        delay: Theme.locationTooltipDelay
        text: "On this Zune"
        contentItem: Text {
            text: locationTip.text
            font.pixelSize: Theme.customizeCaptionSize
            color: Theme.textPrimary
        }
        background: Rectangle {
            color: Theme.surfaceBg
            border.color: Theme.borderLight
            border.width: Theme.hairline
            radius: Theme.radiusSm
        }
    }
}
