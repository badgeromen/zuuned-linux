import QtQuick
import QtQuick.Controls.Basic
import Zuuned

// "Add to Playlist ▸" submenu (UX-2) — the library's playlists plus
// New Playlist… (which opens the builder tray). Hosts connect picked/
// newRequested with their own track set.
ZuneMenu {
    id: sub

    title: "Add to Playlist"

    signal picked(double playlistId, string name)
    signal newRequested()

    Instantiator {
        model: LibraryService.playlists
        delegate: ZuneMenuItem {
            required property var modelData
            text: modelData.name
            onTriggered: sub.picked(modelData.id, modelData.name)
        }
        onObjectAdded: (i, o) => sub.insertItem(i, o)
        onObjectRemoved: (i, o) => sub.removeItem(o)
    }

    ZuneMenuItem {
        text: "New Playlist…"
        onTriggered: sub.newRequested()
    }
}
