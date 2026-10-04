import QtQuick

// The universal container add — UX contract "＋ ▾":
//   ＋ adds everything, ▾ opens the quick picker.
// Device pages set glyph "⤓" (save to library). `armed` turns it
// orange while the playlist tray is open (adds feed the tray).
//
// One clean pill: left half rounds its left corners, right half its
// right corners (per-corner radii — no overlapping-rect hacks).
Item {
    id: root

    property string glyph: "＋"
    property bool armed: false
    property bool showPick: true
    opacity: enabled ? 1 : Theme.disabledOpacity

    signal addAll()
    signal pickParts()

    // Hosts OR this into their tile-hover so the reveal doesn't
    // flicker when the cursor reaches the button itself.
    readonly property bool hovered:
        addArea.containsMouse || pickArea.containsMouse

    readonly property color mainColor: armed ? Theme.orange : Theme.pink
    implicitWidth: 30 + (showPick ? 22 : 0)
    implicitHeight: 26

    // ＋ half — left corners rounded (all, when the ▾ half is hidden)
    Rectangle {
        id: addHalf
        width: 30
        height: root.height
        topLeftRadius: height / 2
        bottomLeftRadius: height / 2
        topRightRadius: root.showPick ? 0 : height / 2
        bottomRightRadius: root.showPick ? 0 : height / 2
        color: addArea.containsMouse
            ? Qt.lighter(root.mainColor, 1.18) : root.mainColor
        scale: addArea.pressed ? 0.92 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
        Behavior on color { ColorAnimation { duration: 100 } }

        Text {
            anchors.centerIn: parent
            text: root.glyph
            font.pixelSize: 14
            color: "white"
        }
        MouseArea {
            id: addArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.addAll()
        }
    }

    // ▾ half — right corners rounded
    Rectangle {
        visible: root.showPick
        anchors.left: addHalf.right
        width: 22
        height: root.height
        topRightRadius: height / 2
        bottomRightRadius: height / 2
        color: pickArea.containsMouse
            ? Qt.rgba(0.25, 0.10, 0.18, 0.9) : Qt.rgba(0, 0, 0, 0.62)
        border.width: 1
        border.color: Qt.alpha(root.mainColor, 0.5)
        scale: pickArea.pressed ? 0.92 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
        Behavior on color { ColorAnimation { duration: 100 } }

        Text {
            anchors.centerIn: parent
            text: "▾"
            font.pixelSize: 11
            color: root.armed ? Theme.orange : Theme.activePink
        }
        MouseArea {
            id: pickArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.pickParts()
        }
    }
}
