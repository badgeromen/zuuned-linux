pragma Singleton
import QtQuick
QtObject {
    property string headerFont: "marker"
    property bool letterBreaks: false
    readonly property string artworkStyle: "original"
    readonly property int artworkCleanDetail: 100
    readonly property int artworkHalftoneTexture: 14
    readonly property int artworkHalftoneDotSize: 17
    readonly property bool artworkHalftoneMonochrome: false
    readonly property int artworkWornTexture: 14
}
