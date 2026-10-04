import QtQuick

// Port of CurvedText.swift — a single line of text distributed along a
// circular arc, each glyph positioned radially and rotated so its
// baseline is tangent to the circle.
//
// Angle convention (as the mac): math degrees in a y-down coordinate
// system — 0° = right, 90° = bottom, 180° = left, 270°/−90° = top. The
// arc is centered on the chosen placement and spans arcDegrees total;
// text reads left-to-right as a viewer expects.
Item {
    id: root

    property string text: ""
    property real radius: 100
    property real arcDegrees: 90
    property string placement: "bottom"   // "bottom" | "top"
    property font font: Qt.font({ pixelSize: 14, weight: Font.Light })
    property color color: Theme.textSecondary
    property int maxChars: 48

    readonly property string _display:
        text.length > maxChars ? text.substring(0, maxChars - 1) + "…" : text

    Repeater {
        model: root._display.length

        delegate: Text {
            required property int index

            // Normalized position along the arc
            readonly property real t: root._display.length > 1
                                      ? index / (root._display.length - 1) : 0.5
            // Polar angle + tangent rotation — direct port of angles(t:)
            readonly property real theta: root.placement === "bottom"
                ? 90.0 + root.arcDegrees / 2.0 - t * root.arcDegrees
                : -90.0 - root.arcDegrees / 2.0 + t * root.arcDegrees
            readonly property real tangentRotation: root.placement === "bottom"
                ? theta - 90.0
                : theta + 90.0

            text: root._display.charAt(index)
            font: root.font
            color: root.color
            rotation: tangentRotation
            x: root.width / 2 + root.radius * Math.cos(theta * Math.PI / 180) - width / 2
            y: root.height / 2 + root.radius * Math.sin(theta * Math.PI / 180) - height / 2
        }
    }
}
