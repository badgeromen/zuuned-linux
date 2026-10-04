import QtQuick
import QtQuick.Controls.Basic

// A distinct, flat monitor mark for a device song found in the local
// library. Matching is metadata identity, independent of its file format.
Item {
    id: badge
    property real size: Theme.locationBadgeSize
    width: size
    height: size
    Accessible.role: Accessible.StaticText
    Accessible.name: "In your library"

    Rectangle {
        id: monitor
        width: badge.size
        height: badge.size * 0.65
        anchors.top: parent.top
        anchors.topMargin: badge.size * 0.1
        color: Theme.transparent
        border.color: Theme.green
        border.width: Theme.hairline
        radius: Theme.spaceXxxs
    }
    Rectangle {
        width: Theme.spaceXxxs
        height: badge.size * 0.2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: monitor.bottom
        color: Theme.green
    }
    Rectangle {
        width: badge.size * 0.55
        height: Theme.hairline
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        color: Theme.green
    }
    HoverHandler { id: hover }
    ToolTip {
        id: locationTip
        visible: hover.hovered
        delay: Theme.locationTooltipDelay
        text: "In your library"
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
