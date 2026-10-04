pragma Singleton
import QtQuick
QtObject {
    property var calls: []
    function addVideos(rows) {
        calls = calls.concat([rows])
        return {added: rows.length, rejected: 0, duplicates: 0}
    }
}
