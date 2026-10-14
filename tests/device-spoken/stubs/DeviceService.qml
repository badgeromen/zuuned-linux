pragma Singleton
import QtQuick
QtObject {
    property bool connected: false
    property bool audiobookBrowseSupported: true
    property bool busy: false
    property bool purging: false
    property bool pulling: false
    property bool spokenMediaLoading: false
    property string spokenMediaError: ""
    property string deviceSerial: "fixture-device-A"
    property var podcastEpisodesList: []
    property var audiobookPartsList: []
    property var artPaths: ({})
    property int refreshes: 0
    property var removals: []
    signal artChanged()
    property int artRequests: 0
    signal stateChanged()
    signal spokenMediaChanged()
    function refreshSpokenMedia() { ++refreshes }
    function requestArt(id) { ++artRequests }
    function purgeSpokenItems(kind, ids) { removals = removals.concat([{kind:kind, ids:ids}]); return true }
}
