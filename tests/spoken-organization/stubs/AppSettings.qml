pragma Singleton
import QtQuick
QtObject {
    property bool onboardingComplete: false
    property string backdropPreset: "none"
    property string backgroundImagePath: ""
    property real backgroundOpacity: 0.2
    property string posterSource: "tmdb"
    property bool podcastAutoCleanup: false
    property bool podcastAutoQueue: false
    property int podcastAutoQueueCount: 10
    property bool letterBreaks: false
    property string headerFont: "marker"
    readonly property string artworkStyle: "original"
    readonly property int artworkCleanDetail: 100
    readonly property int artworkHalftoneTexture: 14
    readonly property int artworkHalftoneDotSize: 17
    readonly property bool artworkHalftoneMonochrome: false
    readonly property int artworkWornTexture: 14
}
