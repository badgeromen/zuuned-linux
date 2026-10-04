import QtQuick
import QtQuick.Layouts

// Local library page. For MUSIC this hosts the shared music browser
// targeted at LibraryService (the CollectionView role) — done here so
// Main.qml's router needs no changes. Videos/photos keep the honest
// empty state until their phases (6 / 8) land.
Item {
    id: root

    property string label: "music"

    Loader {
        id: musicLoader
        anchors.fill: parent
        active: root.label === "music"
        sourceComponent: MusicPage {
            source: LibraryService
        }
    }

    // Sidebar "playlists" → the music browser's Playlists pivot
    function openMusicPivot(name) {
        if (musicLoader.item)
            musicLoader.item.openPivot(name)
    }
    // Live pivot, surfaced for the sidebar's active highlight
    readonly property string musicPivot:
        musicLoader.item ? musicLoader.item.currentPivot : ""

    ColumnLayout {
        anchors.centerIn: parent
        visible: root.label !== "music"
        spacing: Theme.spaceLg

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "your " + root.label + " library"
            color: Theme.textDim
            font.family: Theme.displayFamily
            font.pixelSize: 34
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "the " + root.label + " library arrives with a later phase —\nbrowse your zune from the device panel →"
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textGhost
            font.pixelSize: 14
            font.weight: Font.Light
        }
    }
}
