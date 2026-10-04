pragma Singleton
import QtQuick
QtObject {
    property bool connected: false
    property var photoOnDeviceKeys: ({})
    property var onDeviceKeys: ({})
    property int musicIdentityRevision: 0
    function musicIdentityPeers(track) { return [] }
    function hasDeviceTrack(track, peers) { return false }
    property var artPaths: ({})
    property var photosList: []
    property var photoAlbumsList: []
    property var playlistsList: []
    property var savedPhotos: []
    property var savedTracks: []
    property var purged: []
    signal stateChanged()
    signal photoSaved(string filename, bool ok)
    signal photoFullReady(int itemId, string url)
    signal playlistCreated(string name, bool ok)
    function requestArt(id) {}
    function requestPhotoThumb(id) {}
    function requestPhotoFull(id) {}
    function photoImportsDir() { return "/tmp/photo-action-imports" }
    function savePhotoToLibrary(id, name, subdir) { savedPhotos = savedPhotos.concat([{itemId:id, name:name, subdir:subdir}]) }
    function saveTrackToLibrary(id, title, artist, album) { savedTracks = savedTracks.concat([{itemId:id, title:title, artist:artist, album:album}]) }
    function purgeItems(ids) { purged = purged.concat(ids) }
    function trackInfo(id) { return {title:"Device song", artist:"Device artist", album:"Device album", trackNumber:7, discNumber:2, trackNumberReliable:false} }
}
