import QtQuick
import Zuuned

// A–Z jump rail — letter strip riding the gallery's right shoulder
// (grid study 2026-09-06: bigger type, real click targets) with a
// FISHEYE: letters magnify around the cursor, clicks snap to the
// nearest live letter. Letters present in the data are live; the rest
// are ghosts.
Item {
    id: rail

    // lowercase first-letters that actually exist in the current rows
    property var available: []
    // Optional filter highlight ("" = none). Section-jump hosts leave
    // this empty; legacy filter hosts still light their letter.
    property string activeLetter: ""
    signal jump(string letter)

    implicitWidth: 24

    readonly property var alphabet: ["#","A","B","C","D","E","F","G","H",
        "I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X",
        "Y","Z"]

    readonly property real cellH:
        Math.max(13, Math.min(19, height / alphabet.length))
    readonly property real colTop:
        (height - alphabet.length * cellH) / 2

    property real hoverY: -1
    readonly property bool hovering: hoverY >= 0

    Column {
        y: rail.colTop
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 0

        Repeater {
            model: rail.alphabet
            delegate: Item {
                id: cell
                required property string modelData
                required property int index
                readonly property bool live:
                    rail.available.indexOf(modelData.toLowerCase()) !== -1
                // Fisheye: swell by proximity to the cursor
                readonly property real dist: rail.hovering
                    ? Math.abs((index + 0.5) * rail.cellH - (rail.hoverY - rail.colTop))
                    : 1e9
                readonly property real mag:
                    Math.max(1.0, 2.4 - dist / (rail.cellH * 1.6))

                width: rail.width
                height: rail.cellH

                readonly property bool isActive:
                    rail.activeLetter === modelData.toLowerCase()
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    text: cell.modelData
                    font.pixelSize: 13 * cell.mag
                    font.weight: cell.isActive || cell.mag > 1.6
                                 ? Font.Bold : Font.DemiBold
                    color: !cell.live ? Qt.rgba(1, 1, 1, 0.10)
                         : cell.isActive ? Theme.orange
                         : cell.mag > 1.6 ? Theme.activePink
                         : cell.mag > 1.05 ? Theme.textMid
                                           : Theme.textDim
                    Behavior on font.pixelSize {
                        NumberAnimation { duration: 60 }
                    }
                }
            }
        }
    }

    // One area rules the rail: hover drives the fisheye, click snaps
    // to the NEAREST live letter (forgiving targets).
    MouseArea {
        anchors.fill: parent
        anchors.leftMargin: -10   // fatter approach zone
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPositionChanged: mouse => rail.hoverY = mouse.y
        onExited: rail.hoverY = -1
        onClicked: mouse => {
            const idx = Math.floor((mouse.y - rail.colTop) / rail.cellH)
            let best = -1, bestDist = 1e9
            for (let i = 0; i < rail.alphabet.length; i++) {
                if (rail.available.indexOf(
                        rail.alphabet[i].toLowerCase()) === -1)
                    continue
                const d = Math.abs(i - idx)
                if (d < bestDist) { bestDist = d; best = i }
            }
            if (best >= 0) {
                const letter = rail.alphabet[best].toLowerCase()
                // Same letter again = clear the filter
                rail.jump(letter === rail.activeLetter ? "" : letter)
            }
        }
    }
}
