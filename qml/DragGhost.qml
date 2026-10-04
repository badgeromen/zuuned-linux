import QtQuick
import QtQuick.Controls.Basic
import Qt5Compat.GraphicalEffects
import Zuuned

// UX-2 drag machinery, packaged: the floating ghost that rides the
// cursor. Hosts bind `area` (the MouseArea whose drag drives it), set
// the payload (dragTracks/dragItems + dragKind), and put the visual
// inside — the ghost handles overlay parenting, tilt, and the little
// drop shadow (design contract).
//
// Host wiring (see TrackListRow for the long-form original):
//   MouseArea {
//       drag.target: armed ? ghost : null
//       preventStealing: armed          // Flickables steal the gesture
//       onPressed: m => ghost.place(this, m)
//       onReleased: ghost.drop()
//   }
Item {
    id: ghost

    property Item area: null
    // Track/playlist drags can land on the builder or device panel.
    // Local photo organizers opt in explicitly; all device keys require a connection.
    property string dragKind: "tracks"
    property bool localPhotoDrag: false
    readonly property bool localPhotoAllowed: localPhotoDrag && dragKind === "photos"
    property string dragName: ""
    // () => [items] resolver (lazy — costs nothing until the drop) or
    // a plain array. Track-shaped for tracks/playlist, addVideos/
    // addPhotos-shaped for those kinds.
    property var dragTracks: null
    // Unified device→library payload the library sidebar reads (same
    // value as dragTracks; a distinct name so drop targets can accept
    // any ghost regardless of its host's payload naming).
    property var dragPayload: dragTracks
    property real tilt: 3
    default property alias content: slot.data

    parent: Overlay.overlay ?? area
    width: slot.childrenRect.width
    height: slot.childrenRect.height
    visible: Drag.active
    Drag.active: (DeviceService.connected || localPhotoAllowed) && area ? area.drag.active : false
    Drag.keys: localPhotoAllowed ? (DeviceService.connected ? ["zuuned-local-photos", "zuuned-photos"] : ["zuuned-local-photos"])
               : dragKind === "tracks"
               ? ["zuuned-tracks"] : ["zuuned-" + dragKind]
    Drag.hotSpot.x: 10
    Drag.hotSpot.y: 10
    Drag.onActiveChanged: Drag.active ? TrayState.dragStarted()
                                      : TrayState.dragEnded()

    function place(fromArea, mouse) {
        const p = fromArea.mapToItem(Overlay.overlay, mouse.x, mouse.y)
        ghost.x = p.x + 8
        ghost.y = p.y + 8
    }
    function drop() {
        if ((DeviceService.connected || localPhotoAllowed) && ghost.Drag.active)
            ghost.Drag.drop()
    }

    Item {
        id: slot
        rotation: ghost.tilt
    }
    DropShadow {
        anchors.fill: slot
        source: slot
        radius: 18
        samples: 25
        verticalOffset: 8
        color: Qt.rgba(0, 0, 0, 0.6)
        z: -1
    }
}
