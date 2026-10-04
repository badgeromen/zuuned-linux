pragma Singleton
import QtQuick

QtObject {
    id: service
    property var rows: []
    property var playlistRows: []
    property var saved: null
    property var created: []
    property bool saveResult: true
    property QtObject tracks: QtObject {
        function rowsSnapshot() { return service.rows }
    }
    function playlistTracks(id) { return playlistRows.map(t => Object.assign({}, t)) }
    function editPlaylistEntries(id, name, tracks, original) {
        saved = {id: id, name: name, tracks: tracks.slice(), original: original.slice()}
        return saveResult
    }
    function createPlaylist(name, ids) { created = ids.slice(); return 77 }
}
