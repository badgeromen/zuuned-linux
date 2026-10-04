import QtQuick
import QtQuick.Controls.Basic

// Styled MenuItem — the TrackListRow context-menu look, reusable.
MenuItem {
    id: item

    // Destructive entries render error-red when highlighted.
    property bool danger: false
    opacity: enabled ? 1 : Theme.disabledOpacity

    implicitHeight: 30
    contentItem: Text {
        text: item.text
        font.pixelSize: 13
        font.weight: Font.Light
        color: item.highlighted ? (item.danger ? Theme.error : Theme.textPrimary)
                                : Theme.textMid
        leftPadding: Theme.spaceSm
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: item.highlighted ? Theme.rowHoverPink : "transparent"
    }
}
