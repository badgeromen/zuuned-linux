pragma Singleton
import QtQuick
QtObject {
    signal stateChanged()
    property bool connected: false
    onConnectedChanged: stateChanged()
    property var podcastEpisodesList: []
    function ensureSpokenMedia() {}
    function podcastDeviceStatus(feed, source) { return { known: false, onDevice: false, played: false, playedKnown: false, bookmarkMs: 0 } }
}
