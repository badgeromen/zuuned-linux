import QtQuick

// Native, monochrome line icons. No platform-dependent emoji glyphs.
Canvas {
    id: icon
    property string kind: "podcast"
    property color ink: Theme.textSecondary
    implicitWidth: Theme.spokenIconSize
    implicitHeight: Theme.spokenIconSize
    onKindChanged: requestPaint()
    onInkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset(); c.scale(width / 24, height / 24)
        c.strokeStyle = ink; c.lineWidth = 1.7; c.lineCap = "round"; c.lineJoin = "round"
        c.beginPath()
        if (kind === "audiobook") {
            c.moveTo(12, 5); c.quadraticCurveTo(7, 2, 2, 4); c.lineTo(2, 19)
            c.quadraticCurveTo(7, 17, 12, 20); c.quadraticCurveTo(17, 17, 22, 19)
            c.lineTo(22, 4); c.quadraticCurveTo(17, 2, 12, 5); c.lineTo(12, 20)
        } else {
            c.moveTo(8, 6); c.arc(12, 6, 4, Math.PI, 0)
            c.lineTo(16, 11); c.arc(12, 11, 4, 0, Math.PI); c.closePath()
            c.moveTo(5, 10); c.lineTo(5, 11); c.arc(12, 11, 7, Math.PI, 0, true)
            c.lineTo(19, 10); c.moveTo(12, 18); c.lineTo(12, 22)
            c.moveTo(8, 22); c.lineTo(16, 22)
        }
        c.stroke()
    }
    Component.onCompleted: requestPaint()
}
