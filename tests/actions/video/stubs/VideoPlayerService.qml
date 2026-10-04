pragma Singleton
import QtQuick
QtObject {
    property var calls: []
    function setQueue(rows, index, position) { calls = calls.concat([{rows: rows, index: index, position: position}]) }
}
