// Existing action fixtures exercise original-art behavior without native
// rendering, filesystem cache writes, or application services. The production
// ArtworkRequest/ArtworkImage pipeline has its own native integration gate.
import QtQuick

QtObject {
    property url source
    property string style: "original"
    property int detail: 100
    property int texture: 14
    property int dotSize: 17
    property bool monochrome: false
    readonly property url result: ""
    readonly property bool busy: false
    readonly property string error: ""
    function refresh() {}
    function displayed(url) {}
}
