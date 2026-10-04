import QtQuick

// P4 · MIXTAPES (approved): a playlist as a cassette tape — plastic
// shell, handwritten cream label (the playlist name in the display
// font, literally on the label), and two reels that TURN while this
// playlist is on the player. Pure visual: the host owns clicks, drag,
// and hover verbs.
Item {
    id: tape

    property string name: ""
    property bool playing: false
    // Album-derived theme: [median, partner] dominant colors from the
    // playlist's covers (host computes). Empty → classic cream label.
    property var accents: []
    // A cover from the mixtape's albums, sliced into the label behind
    // the title (host re-rolls it per visit). "" → gradient only.
    property url labelArt: ""

    // VIVID label gradient — hue from the albums, saturation pushed
    // loud; only lightness is pinned so the ink math can work.
    readonly property color labelA: accents.length > 0
        ? Qt.hsla(Math.max(0, accents[0].hslHue),
                  Math.min(0.95, accents[0].hslSaturation * 1.1 + 0.3),
                  0.55, 1)
        : "#efe6d0"
    readonly property color labelB: accents.length > 1
        ? Qt.hsla(Math.max(0, accents[1].hslHue),
                  Math.min(0.95, accents[1].hslSaturation * 1.1 + 0.3),
                  0.38, 1)
        : (accents.length > 0 ? Qt.darker(tape.labelA, 1.5) : "#dcd1b6")
    readonly property bool hasArt: labelArt !== Qt.url("")
    // Ink flips dark/light against the label's real luminance so the
    // scrawled title always reads. Over art it's always light cream —
    // a shadow copy underneath carries the contrast.
    readonly property real _lum:
        0.299 * (labelA.r + labelB.r) / 2
        + 0.587 * (labelA.g + labelB.g) / 2
        + 0.114 * (labelA.b + labelB.b) / 2
    readonly property color ink:
        hasArt ? "#f8f2e4" : (_lum > 0.52 ? "#3a1524" : "#f6ecd6")

    implicitWidth: 176
    implicitHeight: Math.round(width * 112 / 176)
    // Everything inside scales off the width (gallery shelves grow the
    // tapes with the window; 176 is the classic size, k = 1).
    readonly property real k: width / 176

    // ── Shell ──
    Rectangle {
        anchors.fill: parent
        radius: 8 * tape.k
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#242424" }
            GradientStop { position: 1.0; color: "#151515" }
        }
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
    }
    // soft album-color glow rising through the plastic
    Rectangle {
        visible: tape.accents.length > 0
        anchors.fill: parent
        radius: 8 * tape.k
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop {
                position: 1.0
                color: Qt.alpha(tape.labelA, 0.16)
            }
        }
    }
    // corner screws — the little details sell the plastic
    Repeater {
        model: [Qt.point(7, 7), Qt.point(tape.width - 7, 7),
                Qt.point(7, tape.height - 7),
                Qt.point(tape.width - 7, tape.height - 7)]
        delegate: Rectangle {
            required property var modelData
            x: modelData.x - 2
            y: modelData.y - 2
            width: 4; height: 4; radius: 2
            color: Qt.rgba(1, 1, 1, 0.14)
        }
    }

    // ── The label — cream card, marker-scrawled name ──
    Rectangle {
        id: label
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 11 * tape.k
        anchors.rightMargin: 11 * tape.k
        anchors.topMargin: 9 * tape.k
        height: 44 * tape.k
        radius: 3 * tape.k
        clip: true
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: tape.labelA }
            GradientStop { position: 1.0; color: tape.labelB }
        }
        // A slice of one of the mixtape's covers behind the title
        ArtworkImage {
            anchors.fill: parent
            visible: tape.hasArt
            source: tape.labelArt
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: 256
        }
        // The sticker rides ABOVE the gradient (Orson): no color wash
        // over the art — the gradient is only the base underneath.
        // gentle darken so the scrawl never fights the cover
        Rectangle {
            anchors.fill: parent
            visible: tape.hasArt
            color: Qt.rgba(0, 0, 0, 0.26)
        }
        // faint ruled line, like a real tape label — inked to match
        Rectangle {
            visible: !tape.hasArt
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            anchors.bottomMargin: 9
            height: 1
            color: Qt.alpha(tape.ink, 0.3)
        }
        // shadow copy under the title carries contrast over any art
        Text {
            visible: tape.hasArt
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 1
            anchors.verticalCenterOffset: 1
            width: label.width - 14
            horizontalAlignment: Text.AlignHCenter
            text: tape.name
            font.family: Theme.displayFamily
            font.pixelSize: 15 * tape.k
            color: Qt.rgba(0, 0, 0, 0.65)
            rotation: -2
            elide: Text.ElideRight
        }
        Text {
            anchors.centerIn: parent
            width: label.width - 14
            horizontalAlignment: Text.AlignHCenter
            text: tape.name
            font.family: Theme.displayFamily
            font.pixelSize: 15 * tape.k
            color: tape.ink
            rotation: -2
            elide: Text.ElideRight
        }
    }

    // ── Reels — spokes turn while the mixtape plays ──
    component Reel: Item {
        width: 26 * tape.k
        height: 26 * tape.k

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#0d0d0d"
            border.width: 2
            border.color: "#333333"
        }
        Item {
            id: spokes
            anchors.fill: parent
            NumberAnimation on rotation {
                running: tape.playing
                loops: Animation.Infinite
                from: 0; to: 360
                duration: 1800
            }
            Repeater {
                model: [0, 60, 120]
                delegate: Rectangle {
                    required property int modelData
                    anchors.centerIn: parent
                    width: 2 * tape.k
                    height: 16 * tape.k
                    rotation: modelData
                    color: "#2c2c2c"
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 7 * tape.k; height: 7 * tape.k; radius: 3.5 * tape.k
            color: "#0a0a0a"
            border.width: 1
            border.color: "#444444"
        }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: label.bottom
        anchors.topMargin: 9 * tape.k
        spacing: 40 * tape.k

        Reel {}
        Reel {}
    }

    // tape window between the reels
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: label.bottom
        anchors.topMargin: 17 * tape.k
        width: 30 * tape.k
        height: 10 * tape.k
        radius: 2 * tape.k
        color: Qt.rgba(0, 0, 0, 0.55)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }
}
