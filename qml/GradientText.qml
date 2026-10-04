import QtQuick
import Qt5Compat.GraphicalEffects

// Text filled with the brand orange→pink gradient — the ContentView
// header treatment (`.foregroundStyle(Color.zuneGradient)`).
Item {
    id: root

    property alias text: label.text
    property alias font: label.font
    property alias tracking: label.font.letterSpacing

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Text {
        id: label
        visible: false
    }

    LinearGradient {
        anchors.fill: root
        start: Qt.point(0, 0)
        end: Qt.point(root.width, 0)
        source: label
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.orange }
            GradientStop { position: 1.0; color: Theme.pink }
        }
    }
}
