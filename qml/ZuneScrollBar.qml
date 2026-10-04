import QtQuick
import QtQuick.Controls.Basic

// The house scrollbar (fresh-run feedback): ALWAYS visible when the
// content overflows — not the Basic style's fade-in-while-scrolling
// ghost — with a handle fat enough to grab, hover growth, and
// click-anywhere-on-the-track jumps. Attach as
//   ScrollBar.vertical: ZuneScrollBar {}
ScrollBar {
    id: bar

    // Visible iff there is something to scroll; gone entirely when not.
    policy: size < 1.0 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    interactive: true
    minimumSize: 0.08          // huge lists still get a grabbable handle

    implicitWidth: hovered || pressed ? 12 : 8
    Behavior on implicitWidth { NumberAnimation { duration: 120 } }

    contentItem: Rectangle {
        radius: width / 2
        color: bar.pressed ? Theme.activePink
             : bar.hovered ? Qt.alpha(Theme.pink, 0.85)
                           : Qt.rgba(1, 1, 1, 0.22)
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    background: Rectangle {
        radius: width / 2
        color: Qt.rgba(1, 1, 1, 0.05)
    }
}
