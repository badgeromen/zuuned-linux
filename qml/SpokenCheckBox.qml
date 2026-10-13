import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: control
    padding: Theme.spaceSm
    indicator: Rectangle {
        implicitWidth: Theme.spokenCheckSize
        implicitHeight: Theme.spokenCheckSize
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: Theme.radiusSm
        color: control.checked ? Theme.pink : Theme.cardBg
        border.width: Theme.hairline
        border.color: control.activeFocus ? Theme.focusRing : Theme.borderLight
        Text {
            anchors.centerIn: parent
            text: control.checked ? "✓" : ""
            color: Theme.textPrimary
            font.pixelSize: Theme.spokenBody
        }
    }
}
