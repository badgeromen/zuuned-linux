import QtQuick
import Zuuned
import Qt5Compat.GraphicalEffects

// 2:3 poster card — MovieCardView/SeriesCardView's shared body: poster
// (or flat color fallback), corner type badge, gradient title overlay,
// hover ring + play button, on-device badge. Width scales via posterW
// (the gallery grows cards with window width; 180 is the floor-ish
// default).
Item {
    id: card

    property int posterW: 180
    property string posterSource: ""      // file:// or image:// url
    property string title: ""
    property string subtitle: ""
    property string badge: ""             // "MOVIE" | "TV" | "ANIME"
    property color badgeColor: Theme.pink
    property bool isOnDevice: false
    property int versionCount: 1

    // Device pages: quick action becomes ⤓ save-to-library, no ▶
    property bool deviceSide: false
    // Playlist tray armed → the + feeds the tray (orange)
    property bool addArmed: false
    property bool showQuickAdd: true
    // Containers (series/albums) get the ▾ pick half; SINGLE items
    // (a movie is one file) get a plain ＋ — nothing to pick.
    property bool isContainer: true
    // UX-2 drag-to-queue: host supplies () => [addVideos items];
    // drags arm while a zune is connected (videos can't land on the
    // playlist tray).
    property var dragItems: null
    // Which media this card carries — sets the drag key. Device cards
    // flip to "device-<media>" so they land on the library sidebar.
    property string dragMedia: "videos"

    signal opened()
    signal played()
    signal rightClicked()
    signal addAll()        // ＋ (or ⤓): the whole container
    signal pickParts()     // ▾: quick picker

    // Composed hover: the root area LOSES hover to the overlay
    // buttons' own areas — OR them together or the reveal flickers.
    readonly property bool hovering:
        rootArea.containsMouse || playArea.containsMouse || splitBtn.hovered

    width: posterW
    height: Math.round(posterW * 1.5) + titleBar.height

    scale: hovering ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.motionFast } }

    Rectangle {
        id: posterRect
        width: card.posterW
        height: Math.round(card.posterW * 1.5)
        radius: Theme.radiusMd
        color: Theme.artworkPlaceholder
        clip: true

        ArtworkImage {
            id: posterImage
            anchors.fill: parent
            source: card.posterSource
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            // Decode at display size — full w500 posters would cost ~6MB
            // of decoded memory per card.
            sourceSize.width: card.posterW * 2
            visible: false
        }
        Rectangle {
            id: posterMask
            anchors.fill: parent
            radius: Theme.radiusMd
            visible: false
        }
        OpacityMask {
            anchors.fill: parent
            source: posterImage
            maskSource: posterMask
            visible: posterImage.status === Image.Ready
        }

        // Type badge — top-left, under the on-device badge's row
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Theme.spaceXs
            anchors.topMargin: card.isOnDevice
                ? Theme.spaceXs + 32 : Theme.spaceXs
            width: badgeLabel.implicitWidth + Theme.spaceSm
            height: badgeLabel.implicitHeight + 4
            radius: Theme.radiusSm
            color: Qt.alpha(card.badgeColor, 0.85)
            visible: card.badge !== ""

            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: card.badge
                font.pixelSize: 9
                font.bold: true
                color: "white"
            }
        }

        // Version count — bottom-left ("3 versions")
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: Theme.spaceXs
            width: versionLabel.implicitWidth + Theme.spaceSm
            height: versionLabel.implicitHeight + 4
            radius: Theme.radiusSm
            color: Qt.alpha(Theme.pink, 0.85)
            visible: card.versionCount > 1

            Text {
                id: versionLabel
                anchors.centerIn: parent
                text: card.versionCount + " versions"
                font.pixelSize: 9
                font.bold: true
                color: "white"
            }
        }

        // Quick play — CENTERED, hover-revealed (library side only:
        // device content can't play without extracting first).
        // States: revealed on card hover → fills pink on ITS hover →
        // presses down on click.
        Rectangle {
            id: playBtn
            anchors.centerIn: parent
            width: 46
            height: 46
            radius: 23
            color: playArea.containsMouse
                ? Qt.alpha(Theme.pink, 0.92) : Qt.rgba(0, 0, 0, 0.55)
            border.width: 1
            border.color: playArea.containsMouse ? Theme.pink : Theme.activePink
            visible: !card.deviceSide
            opacity: card.hovering ? 1 : 0
            scale: playArea.pressed ? 0.88
                 : playArea.containsMouse ? 1.12
                 : card.hovering ? 1 : 0.8
            Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
            Behavior on scale { NumberAnimation { duration: Theme.motionFast } }
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }

            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 2
                text: "▶"
                font.pixelSize: 17
                color: "white"
            }
            MouseArea {
                id: playArea
                anchors.fill: parent
                enabled: card.hovering && !card.deviceSide
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: card.played()
            }
        }

        // Split add — top-right, hover-revealed (＋▾ library, ⤓▾ device)
        SplitAddButton {
            id: splitBtn
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spaceXs
            visible: card.showQuickAdd && DeviceService.connected
            opacity: card.hovering ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
            glyph: card.deviceSide ? "⤓" : "＋"
            armed: card.addArmed
            showPick: card.isContainer
            onAddAll: { if (DeviceService.connected) card.addAll() }
            onPickParts: { if (DeviceService.connected) card.pickParts() }
        }
    }

    // On-device badge — overlaps the poster's bottom-left corner
    OnDeviceBadge {
        anchors.left: posterRect.left
        anchors.top: posterRect.top
        anchors.leftMargin: Theme.spaceXs
        anchors.topMargin: Theme.spaceXs
        size: 28
        visible: card.isOnDevice
    }

    // Title bar under the poster
    Column {
        id: titleBar
        anchors.top: posterRect.bottom
        anchors.topMargin: Theme.spaceXs
        width: parent.width
        spacing: 2

        Text {
            width: parent.width
            text: card.title
            font.pixelSize: 13
            font.weight: Font.Medium
            color: Theme.textPrimary
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            text: card.subtitle
            font.pixelSize: 11
            font.weight: Font.Light
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }

    // Hover ring
    Rectangle {
        anchors.fill: posterRect
        radius: Theme.radiusMd
        color: "transparent"
        border.width: 1
        border.color: card.hovering ? Qt.rgba(0.83, 0.21, 0.48, 0.4) : "transparent"
    }

    MouseArea {
        id: rootArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                card.rightClicked()
            else
                card.opened()
        }
        readonly property bool dragArmed:
            card.dragItems !== null && DeviceService.connected
        drag.target: dragArmed ? posterGhost : null
        preventStealing: dragArmed
        onPressed: mouse => posterGhost.place(rootArea, mouse)
        onReleased: posterGhost.drop()
        // Let the overlay buttons' own MouseAreas win
        z: -1
    }

    DragGhost {
        id: posterGhost
        area: rootArea
        dragKind: card.deviceSide ? "device-" + card.dragMedia : card.dragMedia
        dragTracks: card.dragItems

        Rectangle {
            width: 54
            height: 81
            radius: 4
            color: Theme.artworkPlaceholder
            border.width: 1
            border.color: Qt.alpha(Theme.activePink, 0.75)
            ArtworkImage {
                anchors.fill: parent
                anchors.margins: 1
                source: card.posterSource
                fillMode: Image.PreserveAspectCrop
                visible: card.posterSource !== ""
            }
        }
    }
}
