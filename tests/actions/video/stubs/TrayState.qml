pragma Singleton
import QtQuick
QtObject {
    property int started: 0
    property int ended: 0
    function dragStarted() { started++ }
    function dragEnded() { ended++ }
}
