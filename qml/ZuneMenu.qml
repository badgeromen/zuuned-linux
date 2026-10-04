import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects

// The canonical context menu surface — glass body with real elevation
// (constitution rule 3: tight contact shadow + soft throw). Use with
// ZuneMenuItem children; every section shares this one look.
Menu {
    id: menu

    property real menuWidth: 210

    // Auto-created rows (submenu titles) match ZuneMenuItem styling —
    // without this they render in the default Basic look.
    delegate: ZuneMenuItem {}

    background: Item {
        implicitWidth: menu.menuWidth
        Rectangle {
            id: body
            anchors.fill: parent
            color: Qt.rgba(0.08, 0.08, 0.09, 0.97)
            border.width: 1
            border.color: Theme.glassBorder
            // Same corner radius as the island panels (secondary tier)
            radius: Theme.radiusLg
        }
        DropShadow {
            anchors.fill: body
            source: body
            radius: 18
            samples: 25
            verticalOffset: 6
            color: Qt.rgba(0, 0, 0, 0.55)
            z: -1
        }
    }
}
