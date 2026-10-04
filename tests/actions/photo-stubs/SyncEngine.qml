pragma Singleton
import QtQuick
QtObject {
    property var photos: []
    property var music: []
    function addPhotos(items) { photos = photos.concat(items); return {added: items.length, rejected: 0} }
    function addTracks(items) { music = music.concat(items); return {added: items.length, rejected: 0} }
}
