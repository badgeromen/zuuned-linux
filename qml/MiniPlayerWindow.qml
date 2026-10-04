import QtQuick
import QtQuick.Layouts
import Zuuned

// Floating mini player — port of MusicMiniPlayerView.swift's NSPanel:
// frameless always-on-top vinyl (280px, drag-to-move; ring-tap seek
// works, drag-seek disabled so disc drags move the window), CurvedText
// title on an 80° bottom arc, artist line, hover close button, and a
// hover-revealed 220px UP NEXT drawer whose space is ALWAYS reserved so
// the window never resizes (450ms retract delay against flicker).
//
// Declared inside Main.qml → transient for the main window, which makes
// Hyprland float it. Wayland offers no global positioning, so geometry
// persistence is size-only (and ours is fixed) — mac's frame restore
// doesn't apply.
Window {
    id: mini

    readonly property int vinylSize: 280
    readonly property int drawerWidth: 220
    property var player: PlayerService
    property url artSource: ""

    width: vinylSize + drawerWidth + 40
    height: vinylSize + 96
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Dialog
    color: "transparent"
    title: "Zuuned Mini Player"

    property bool hovered: hoverProbe.hovered

    Timer {
        id: drawerRetract
        interval: 450
        onTriggered: drawer.expanded = false
    }
    onHoveredChanged: {
        if (hovered) {
            drawerRetract.stop()
            drawer.expanded = true
        } else {
            drawerRetract.restart()
        }
    }

    HoverHandler { id: hoverProbe }

    // Drag anywhere (under the vinyl's tap targets) to move the window
    MouseArea {
        anchors.fill: parent
        onPressed: mini.startSystemMove()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceSm
        spacing: Theme.spaceMd

        // ── Vinyl + title arc + artist ──
        Item {
            Layout.preferredWidth: mini.vinylSize
            Layout.fillHeight: true

            // Disc or Vinyl per Prefs.playerStyle (unified across the app).
            // `vinyl` stays a fixed square so the title arc / artist anchor
            // the same for either player.
            Item {
                id: vinyl
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: mini.vinylSize
                height: mini.vinylSize

                Loader {
                    anchors.centerIn: parent
                    sourceComponent: Prefs.playerStyle === "vinyl" ? miniVinyl : miniDisc
                }
                Component {
                    id: miniVinyl
                    VinylRecordView {
                        player: mini.player
                        posterSource: mini.artSource
                        size: mini.vinylSize
                        dragSeekEnabled: false   // disc drags move the window
                        isVisible: mini.visible
                    }
                }
                Component {
                    id: miniDisc
                    SpinningDiscView {
                        player: mini.player
                        artSource: mini.artSource
                        diameter: mini.vinylSize
                    }
                }
            }

            // Title on an 80° arc hugging the rim (12px outside)
            CurvedText {
                anchors.horizontalCenter: vinyl.horizontalCenter
                anchors.verticalCenter: vinyl.verticalCenter
                text: mini.player.currentTitle
                radius: mini.vinylSize / 2 + 12
                arcDegrees: 80
                placement: "bottom"
                color: Theme.textPrimary
            }

            Text {
                anchors.top: vinyl.bottom
                anchors.topMargin: 34
                anchors.horizontalCenter: parent.horizontalCenter
                text: mini.player.currentArtist
                font.pixelSize: 12
                font.weight: Font.Light
                color: Theme.textSecondary
                elide: Text.ElideRight
                width: mini.vinylSize * 0.8
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // ── UP NEXT drawer (space always reserved) ──
        Item {
            id: drawer
            property bool expanded: false
            Layout.preferredWidth: mini.drawerWidth
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: Theme.spaceLg
                anchors.bottomMargin: Theme.spaceLg
                radius: Theme.radiusLg
                color: Theme.surfaceBg
                border.width: 1
                border.color: Theme.glassBorder
                opacity: drawer.expanded ? 0.96 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    Text {
                        text: "UP NEXT"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2
                        color: Theme.textDim
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: mini.player.queue.slice(mini.player.queueIndex + 1,
                                                       mini.player.queueIndex + 9)
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Item {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 34

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                Text {
                                    width: parent.width
                                    text: modelData.title
                                    font.pixelSize: 12
                                    font.weight: Font.Light
                                    color: upNextArea.containsMouse
                                           ? Theme.textPrimary : Theme.textMid
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.artist
                                    font.pixelSize: 10
                                    font.weight: Font.Light
                                    color: Theme.textDim
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                id: upNextArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: mini.player.jumpTo(
                                    mini.player.queueIndex + 1 + index)
                            }
                        }
                    }
                }
            }
        }
    }

    // Close (hover-revealed, top-right)
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spaceSm
        width: 22
        height: 22
        radius: 11
        color: closeArea.containsMouse ? Theme.cardActive : Theme.surfaceBg
        border.color: Theme.borderLight
        opacity: mini.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }

        Text {
            anchors.centerIn: parent
            text: "✕"
            font.pixelSize: 10
            color: Theme.textMid
        }
        MouseArea {
            id: closeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mini.visible = false
        }
    }
}
