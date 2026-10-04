import QtQuick
import Qt5Compat.GraphicalEffects

// Album art, both sources:
// - Device (default): itemId > 0 → DeviceService representative-sample
//   art keyed by item id.
// - Local: local: true + artist/album/filepath → LibraryService art
//   (folder art → embedded), keyed LibraryService.artKey(artist, album).
// Requests fetch on demand and swaps the image in when the service's
// artPaths updates. Until then (or on failure) shows the AlbumArtView
// placeholder card.
Item {
    id: art

    property bool local: false
    property int itemId: 0
    property string artist: ""
    property string album: ""
    property string filepath: ""
    // Uniform art corners (2026-09-07): every piece of art in the app
    // wears the video posters' radius
    property real cornerRadius: Theme.radiusMd
    property bool showPlaceholder: true

    readonly property string artUrl: local
        ? (album.length > 0
           ? (LibraryService.artPaths[LibraryService.artKey(artist, album)] || "")
           : "")
        : (itemId > 0 ? (DeviceService.artPaths[String(itemId)] || "") : "")

    onItemIdChanged: request()
    onAlbumChanged: request()
    onArtistChanged: request()
    onLocalChanged: request()
    onFilepathChanged: request()
    Component.onCompleted: request()

    function request() {
        if (local) {
            if (album.length > 0 && filepath.length > 0)
                LibraryService.requestArt(artist, album, filepath)
        } else if (itemId > 0) {
            DeviceService.requestArt(itemId)
        }
    }

    AlbumArtPlaceholder {
        anchors.fill: parent
        visible: art.showPlaceholder && img.status !== Image.Ready
        radius: art.cornerRadius
    }

    ArtworkImage {
        id: img
        anchors.fill: parent
        visible: art.artUrl !== "" && status === Image.Ready
        source: art.artUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        layer.enabled: art.cornerRadius > 0
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: img.width
                height: img.height
                radius: art.cornerRadius
            }
        }
    }
}
