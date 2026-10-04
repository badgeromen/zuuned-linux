pragma Singleton
import QtQuick
import QtCore

// W15 "make it yours" preferences — set in first-run onboarding, editable
// in Settings, persisted via QtCore.Settings. The app reads these live
// (e.g. NowPlayingIsland picks Disc vs Vinyl off playerStyle).
QtObject {
    // "disc"  → SpinningDiscView   |  "vinyl" → VinylRecordView
    property alias playerStyle: store.playerStyle
    // "none" | "grime" | "dusk" | a file:// path to the user's own image
    property alias backdrop: store.backdrop
    // "marker" (Permanent Marker) | "graffiti"
    property alias displayFont: store.displayFont

    // QtObject has no default property, so the persistent store lives in
    // an explicit property rather than as a bare child.
    property Settings store: Settings {
        id: store
        category: "appearance"
        property string playerStyle: "disc"
        property string backdrop: "grime"
        property string displayFont: "marker"
    }
}
