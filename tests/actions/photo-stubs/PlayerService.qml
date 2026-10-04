pragma Singleton
import QtQuick
QtObject {
    property var queue: []
    property int queueIndex: -1
    property bool playing: false
    function playNext(items) {}
    function appendToQueue(items) {}
}
