import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    padding: Theme.spaceSm
    horizontalPadding: Theme.spaceMd
    hoverEnabled: true
    font.pixelSize: Theme.spokenBody
    opacity: enabled ? 1 : Theme.spokenDisabledOpacity
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.highlighted ? Theme.activePink : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radiusMd
        color: control.down ? Theme.rowSelectedPink : control.hovered ? Theme.rowHoverPink : Theme.spokenTransparent
        border.width: control.flat && !control.activeFocus ? 0 : Theme.hairline
        border.color: control.activeFocus || control.highlighted ? Theme.focusRing : Theme.borderLight
    }
}
