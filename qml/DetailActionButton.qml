import QtQuick
import QtQuick.Layouts

// The detail-sidebar action button set from AlbumDetailView.swift:
// primary (filled pink, dark text), neutral (bordered), accent (pink
// bordered, pink text). Inert until the player/sync engine port — wired
// callers connect to `clicked`.
Rectangle {
    id: btn

    property string title
    property string kind: "neutral"   // "primary" | "neutral" | "accent"

    signal clicked()

    property bool hovering: false

    Layout.fillWidth: true
    implicitHeight: label.implicitHeight + Theme.spaceSm * 2
    radius: Theme.radiusMd
    color: kind === "primary"
           ? (hovering ? Theme.activePink : Theme.pink)
           : "transparent"
    border.width: kind === "primary" ? 0 : 1
    border.color: kind === "accent"
                  ? Qt.rgba(0.83, 0.21, 0.48, 0.5)
                  : Theme.borderLight

    Behavior on color { ColorAnimation { duration: Theme.motionFast } }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.title
        font.pixelSize: 14
        font.weight: Font.Light
        color: btn.kind === "primary" ? Theme.bg
             : btn.kind === "accent" ? Theme.pink : Theme.textSecondary
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: btn.hovering = true
        onExited: btn.hovering = false
        onClicked: btn.clicked()
    }
}
