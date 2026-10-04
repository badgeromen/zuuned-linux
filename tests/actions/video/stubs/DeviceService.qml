pragma Singleton
import QtQuick
QtObject {
    property bool connected: false
    property var artPaths: ({})
    property var videosList: []
    property var deleted: []
    signal stateChanged()
    function requestArt(id) {}
    function purgeItems(ids) { deleted = deleted.concat([ids]) }
}
