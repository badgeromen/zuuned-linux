import QtQuick
import QtQuick.Controls.Basic

// Approximation of ZuneSearchFieldStyle: dark card field, light 13px
// text, dim placeholder, subtle border that warms on focus.
TextField {
    id: field

    implicitHeight: 28
    font.pixelSize: 13
    font.weight: Font.Light
    color: Theme.textPrimary
    placeholderText: "search"
    placeholderTextColor: Theme.textDim
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.cardBg
        border.width: 1
        border.color: field.activeFocus ? Qt.rgba(0.83, 0.21, 0.48, 0.5)
                                        : Theme.borderLight
    }
}
