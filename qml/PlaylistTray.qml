import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Zuuned
import "MusicIdentity.js" as MusicIdentity

// UX-2 playlist builder — a card FLOATING ABOVE the nav island with
// real Z-height (deep drop shadow, lift-in, slight overhang past the
// island edge). The sidebar condenses to an icon rail beside it, so
// you can keep browsing while you build; the tray stays until saved
// or discarded. Adds from anywhere land here; every landing fires the
// glow pulse so the eye knows where it went.
Item {
    id: tray
    readonly property url collectionArtwork: {
        void LibraryService.collectionArtRevision
        return TrayState.playlistId < 0 ? ""
            : LibraryService.collectionArt("mixtape", String(TrayState.playlistId))
    }
    readonly property var collectionArtUrls: {
        void LibraryService.artPaths
        const urls = []
        const seen = ({})
        for (const track of TrayState.tracks) {
            const key = LibraryService.artKey(MusicIdentity.owner(track), track.album || "")
            const url = LibraryService.artPaths[key] || ""
            if (url && !seen[url]) {
                seen[url] = true
                urls.push(url)
            }
            if (urls.length === 8) break
        }
        return urls
    }
    function customizeMixtape() {
        const playlist = LibraryService.playlists.find(p => p.id === TrayState.playlistId)
        if (!playlist) return
        collectionCustomize.openFor({kind: "mixtape", id: playlist.id,
            name: playlist.name, title: playlist.name, count: playlist.count,
            poster: tray.collectionArtwork.toString(), artUrls: tray.collectionArtUrls})
    }
    CustomizeSheet {
        id: collectionCustomize
        objectName: "builderCollectionCustomize"
        onSaved: message => toastHost.show(message)
    }
    ZuneMenu {
        id: collectionMenu
        objectName: "builderCollectionMenu"
        ZuneMenuItem { text: "Customize…"; onTriggered: tray.customizeMixtape() }
    }
    function editMember(id) {
        const track = TrayState.trackFor(id)
        if (track) memberEditor.openFor(track)
    }
    EditTrackSheet { id: memberEditor; objectName: "builderMemberEditor" }
    Connections {
        target: LibraryService
        function onLibraryChanged() { TrayState.refreshMetadata() }
    }
    ZuneMenu {
        id: memberMenu
        property double trackId: -1
        ZuneMenuItem {
            text: "Edit Info…"
            onTriggered: tray.editMember(memberMenu.trackId)
        }
        ZuneMenuItem {
            text: "Queue for Device"
            visible: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (!DeviceService.connected) return
                const track = TrayState.trackFor(memberMenu.trackId)
                if (!track) return
                const result = SyncEngine.addTracks([track])
                if (result.error) toastHost.show(result.error)
                else if (result.added > 0) toastHost.show("queued " + track.title)
            }
        }
    }

    // 1 = floating in place, 0 = settled away
    property real reveal: TrayState.building ? 1 : 0
    Behavior on reveal {
        NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic }
    }
    visible: reveal > 0.01
    opacity: reveal
    // Lift: rises and inflates onto the stack, shadow riding along.
    scale: 0.93 + 0.07 * reveal
    transformOrigin: Item.Left
    transform: Translate { y: (1 - tray.reveal) * 22 }

    // ── Pulse: adds + disc clicks flash the orange edge ──
    property real pulse: 0
    Connections {
        target: TrayState
        function onPulsed() { pulseAnim.restart() }
    }
    SequentialAnimation {
        id: pulseAnim
        NumberAnimation { target: tray; property: "pulse"; to: 1; duration: 90 }
        NumberAnimation {
            target: tray; property: "pulse"; to: 0
            duration: 450; easing.type: Easing.OutQuad
        }
    }

    // ── Drop target (UX-2 drag-and-drop): tracks/albums dragged from
    // the library land here; the card glows while a drag hovers. ──
    DropArea {
        id: trayDrop
        anchors.fill: parent
        // A dropped playlist MERGES its tracks into the builder
        keys: ["zuuned-tracks", "zuuned-playlist"]
        onDropped: drop => {
            if (!DeviceService.connected) return
            const t = drop.source.dragTracks
            TrayState.addTracks(typeof t === "function" ? t() : (t ?? []))
            drop.accept()
        }
    }

    // ── The floating card ── everything visual lives in one item so
    // the drop shadow tracks it exactly through the lift animation.
    Item {
        id: card
        anchors.fill: parent

        // CLEAN card — deliberately no grunge (review call: the
        // builder is a workbench, not another weathered surface). The
        // orange identity lives in a thin border + a faint top sheen;
        // depth comes from the drop shadows, not texture.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Qt.rgba(0.075, 0.07, 0.065, 0.98)
            border.width: trayDrop.containsDrag ? 2 : 1
            border.color: Qt.alpha(Theme.orange,
                                   trayDrop.containsDrag ? 0.85 : 0.32)
            Behavior on border.color { ColorAnimation { duration: 100 } }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1.0, 0.55, 0.0, 0.05) }
                GradientStop { position: 0.25; color: "transparent" }
            }
        }
        // Pulse glow ring
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.radiusLg + 2
            color: "transparent"
            border.width: 2
            border.color: Qt.alpha(Theme.orange, tray.pulse * 0.55)
            visible: tray.pulse > 0.01
        }
    }

    // Dramatic Z-height (constitution rule 3, cranked): a tight dark
    // contact shadow plus a wide soft throw, so the card reads as
    // hovering well above the nav island.
    DropShadow {
        anchors.fill: card
        source: card
        radius: 40
        samples: 49
        verticalOffset: 18
        horizontalOffset: 6
        color: Qt.rgba(0, 0, 0, 0.45 + 0.3 * tray.reveal)
        z: -1
    }
    DropShadow {
        anchors.fill: card
        source: card
        radius: 10
        samples: 17
        verticalOffset: 4
        color: Qt.rgba(0, 0, 0, 0.6)
        z: -1
    }

    ColumnLayout {
        anchors.fill: parent
        // Tighter than spaceLg — the card lost width to the icon rail.
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            CassetteTile {
                objectName: "builderCollectionCassette"
                visible: TrayState.playlistId >= 0
                Layout.preferredWidth: Theme.spaceHuge + Theme.spaceXxl
                Layout.preferredHeight: Layout.preferredWidth / Theme.cassetteAspect
                name: TrayState.name
                labelArt: tray.collectionArtwork.toString() !== "" ? tray.collectionArtwork
                    : tray.collectionArtUrls.length > 0 ? tray.collectionArtUrls[0] : ""
                MouseArea {
                    objectName: "builderCollectionArtArea"
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouse => mouse.button === Qt.RightButton
                        ? collectionMenu.popup() : tray.customizeMixtape()
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXxs
                Text {
                    text: TrayState.playlistId < 0 ? "NEW PLAYLIST" : "EDITING PLAYLIST"
                    font.pixelSize: Theme.customizeLabelSize
                    font.bold: true
                    font.letterSpacing: Theme.spaceXxxs
                    color: Theme.orange
                }
                Text {
                    visible: TrayState.playlistId >= 0
                    text: "customize…"
                    font.pixelSize: Theme.customizeCaptionSize
                    color: collectionCustomizeArea.containsMouse ? Theme.activePink : Theme.textSecondary
                    MouseArea {
                        id: collectionCustomizeArea
                        objectName: "builderCustomizeAction"
                        anchors.fill: parent
                        anchors.margins: -Theme.spaceXxxs
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: tray.customizeMixtape()
                    }
                }
            }
        }

        // Name — the ONE required act, so it leads the card: big
        // marker type, underline that wakes up with focus/emptiness.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            TextInput {
                id: nameInput
                Layout.fillWidth: true
                text: TrayState.name
                onTextEdited: TrayState.name = text
                font.pixelSize: 24
                font.family: Theme.displayFamily
                color: Theme.textPrimary
                clip: true
                selectByMouse: true
                Text {
                    anchors.fill: parent
                    visible: nameInput.text === "" && !nameInput.activeFocus
                    text: "name it…"
                    font.pixelSize: 22
                    font.family: Theme.displayFamily
                    color: Qt.alpha(Theme.orange, 0.45)
                }
                Connections {
                    target: TrayState
                    function onNameChanged() {
                        if (nameInput.text !== TrayState.name)
                            nameInput.text = TrayState.name
                    }
                    function onBuildingChanged() {
                        if (TrayState.building && TrayState.playlistId < 0)
                            nameInput.forceActiveFocus()
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: nameInput.activeFocus ? 2 : 1
                color: nameInput.activeFocus
                    ? Qt.alpha(Theme.orange, 0.8)
                    : nameInput.text === ""
                        ? Qt.alpha(Theme.orange, 0.45) : Theme.borderLight
                Behavior on color { ColorAnimation { duration: 120 } }
            }
        }

        Text {
            text: {
                const n = TrayState.tracks.length
                let ms = 0
                for (const t of TrayState.tracks) ms += t.durationMs
                const m = Math.round(ms / 60000)
                return n + (n === 1 ? " track" : " tracks")
                     + (m > 0 ? " · " + m + " min" : "")
            }
            font.pixelSize: 11
            color: Theme.textDim
        }

        // ── Track list ──
        ListView {
            id: trackList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1
            model: TrayState.tracks
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {}
            // Follow adds so the newest landing is visible
            onCountChanged: if (count > 0) positionViewAtEnd()

            delegate: Rectangle {
                id: trkRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 40
                radius: Theme.radiusSm
                color: trkArea.containsMouse
                    ? Qt.rgba(1, 0.55, 0, 0.07) : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceSm
                    anchors.rightMargin: Theme.spaceSm
                    spacing: Theme.spaceSm

                    Text {
                        text: trkRow.index + 1
                        font.pixelSize: 10
                        color: Theme.textGhost
                        Layout.preferredWidth: 16
                        horizontalAlignment: Text.AlignRight
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: trkRow.modelData.title
                            font.pixelSize: 12
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: trkRow.modelData.artist
                            font.pixelSize: 10
                            color: Theme.textDim
                            elide: Text.ElideRight
                        }
                    }
                    // Metadata edit stays available offline, like name and
                    // membership edits. Dragging needs a connected Zune.
                    Row {
                        spacing: 2
                        visible: trkArea.containsMouse
                        Text {
                            objectName: "builderMemberEdit" + trkRow.modelData.id
                            text: "✎"
                            font.pixelSize: Theme.spaceMd
                            color: editArea.containsMouse ? Theme.orange : Theme.textDim
                            MouseArea {
                                id: editArea
                                anchors.fill: parent
                                anchors.margins: -Theme.spaceXxs
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: tray.editMember(trkRow.modelData.id)
                            }
                        }
                        Text {
                            objectName: "builderMemberUp" + trkRow.modelData.id
                            text: "▲"
                            font.pixelSize: 9
                            color: upArea.containsMouse ? Theme.orange : Theme.textDim
                            visible: trkRow.index > 0
                            MouseArea {
                                id: upArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: TrayState.move(trkRow.index, trkRow.index - 1)
                            }
                        }
                        Text {
                            objectName: "builderMemberDown" + trkRow.modelData.id
                            text: "▼"
                            font.pixelSize: 9
                            color: downArea.containsMouse ? Theme.orange : Theme.textDim
                            visible: trkRow.index < TrayState.tracks.length - 1
                            MouseArea {
                                id: downArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: TrayState.move(trkRow.index, trkRow.index + 1)
                            }
                        }
                        Text {
                            objectName: "builderMemberRemove" + trkRow.modelData.id
                            text: "✕"
                            font.pixelSize: 11
                            color: rmArea.containsMouse ? Theme.error : Theme.textDim
                            MouseArea {
                                id: rmArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: TrayState.removeAt(trkRow.index)
                            }
                        }
                    }
                }
                MouseArea {
                    id: trkArea
                    objectName: "builderTrackArea" + trkRow.modelData.id
                    anchors.fill: parent
                    z: -1
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            memberMenu.trackId = trkRow.modelData.id
                            memberMenu.popup()
                        }
                    }
                    drag.target: DeviceService.connected ? memberGhost : null
                    preventStealing: DeviceService.connected
                    onPressed: mouse => memberGhost.place(trkArea, mouse)
                    onReleased: memberGhost.drop()
                }
                DragGhost {
                    id: memberGhost
                    area: trkArea
                    dragKind: "tracks"
                    dragName: trkRow.modelData.title || ""
                    dragTracks: () => {
                        const track = TrayState.trackFor(trkRow.modelData.id)
                        return track ? [track] : []
                    }
                    AlbumArt {
                        width: Theme.spaceHuge + Theme.spaceLg
                        height: width
                        local: true
                        artist: trkRow.modelData.albumartist || trkRow.modelData.artist || ""
                        album: trkRow.modelData.album || ""
                        filepath: trkRow.modelData.filepath || ""
                        cornerRadius: Theme.radiusMd
                    }
                }
            }

            // Empty state — big breathing ＋ and teach copy at reading
            // size. No frame (a dashed drop-zone read as tacky —
            // review); the breath alone says "waiting", not "broken".
            Item {
                anchors.fill: parent
                anchors.margins: Theme.spaceXs
                visible: TrayState.tracks.length === 0

                Item {
                    id: emptyBreath
                    property real phase: 0
                    SequentialAnimation on phase {
                        running: TrayState.building && TrayState.tracks.length === 0
                        loops: Animation.Infinite
                        NumberAnimation { to: 1; duration: 1600; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 0; duration: 1600; easing.type: Easing.InOutSine }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spaceXl * 2
                    spacing: Theme.spaceMd
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "＋"
                        font.pixelSize: 46
                        color: Qt.alpha(Theme.orange, 0.6 + 0.4 * emptyBreath.phase)
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: "browse your music"
                        font.pixelSize: 17
                        font.family: Theme.displayFamily
                        color: Theme.textMid
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: "every orange ＋ drops tracks here"
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                    }
                }
            }
        }

        // ── Commit row — primary action rightmost (house rule) ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd

            Text {
                text: "discard"
                font.pixelSize: 12
                color: discardArea.containsMouse ? Theme.error : Theme.textDim
                MouseArea {
                    id: discardArea
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: TrayState.discard()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 32
                radius: Theme.radiusMd
                color: saveArea.containsMouse && saveEnabled
                    ? Qt.lighter(Theme.orange, 1.12)
                    : saveEnabled ? Theme.orange : "transparent"
                // Not-ready state says WHY instead of going gray-dead
                border.width: saveEnabled ? 0 : 1
                border.color: Qt.alpha(Theme.orange, 0.35)
                readonly property bool saveEnabled:
                    TrayState.name.trim() !== ""
                scale: saveArea.pressed && saveEnabled ? 0.95 : 1
                Behavior on scale { NumberAnimation { duration: 80 } }
                Behavior on color { ColorAnimation { duration: 100 } }
                Text {
                    anchors.centerIn: parent
                    text: parent.saveEnabled ? "save playlist" : "name it to save"
                    font.pixelSize: 13
                    font.bold: parent.saveEnabled
                    color: parent.saveEnabled ? "white" : Qt.alpha(Theme.orange, 0.65)
                }
                MouseArea {
                    id: saveArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: parent.saveEnabled
                        ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (!parent.saveEnabled) {
                            nameInput.forceActiveFocus()
                            return
                        }
                        const wasNew = TrayState.playlistId < 0
                        if (TrayState.save())
                            toastHost.show(wasNew ? "playlist saved to your library"
                                                  : "playlist updated")
                    }
                }
            }
        }
    }
}
