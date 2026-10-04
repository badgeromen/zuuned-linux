pragma Singleton
import QtQuick

// Geometry for this standalone comparison surface. Colors, lettering,
// spacing, and motion continue to come from the application's Theme.
QtObject {
    readonly property int windowWidth: 1280
    readonly property int windowHeight: 850
    readonly property int minimumWidth: 900
    readonly property int minimumHeight: 650
    readonly property int headingSize: 38
    readonly property int variantHeadingSize: 19
    readonly property int sampleHeight: 56
    readonly property int sampleMaximumWidth: 200
    readonly property int controlWidth: 260
    readonly property int smallThumbnail: 48
    readonly property int largeThumbnail: 80
    readonly property int thumbnailsHeight: 108
    readonly property int artworkMinimumHeight: 128
    readonly property real albumAspect: 1
    readonly property real posterAspect: 2 / 3
}
