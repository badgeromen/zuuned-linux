pragma Singleton
import QtQuick
QtObject {
    property bool scanning: false
    property int scanTotal: 0
    property int scanCurrent: 0
    property string scanStage: ""
    property string scanCurrentFile: ""
    property var unmatched: []
    property var unmatchedGroups: []
    property var rows: []
    property var watched: []
    property var deleted: []
    property var customization: ({})
    signal videosChanged()
    signal videoRowsChanged(var ids)
    function setVideosWatched(ids, value) { watched = watched.concat([{ids: ids, value: value}]) }
    function deleteVideos(ids) { deleted = deleted.concat([ids]) }
}
