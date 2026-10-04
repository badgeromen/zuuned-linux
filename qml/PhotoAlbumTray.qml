import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Zuuned

Item {
    id: tray
    objectName: "photoAlbumTray"
    property real reveal: PhotoTrayState.building ? 1 : 0
    property real pulse: 0
    property var albums: []
    readonly property var destinations: {
        const excluded = ({})
        excluded[PhotoTrayState.albumId] = true
        let changed = true
        while (changed) {
            changed = false
            for (const a of albums) {
                if (excluded[a.parentId] && !excluded[a.id]) { excluded[a.id] = true; changed = true }
            }
        }
        return [{id: 0, pathLabel: "Top level"}].concat(albums.filter(a => !excluded[a.id]))
    }
    function refreshAlbums() { albums = LibraryService.customPhotoAlbums() }
    Component.onCompleted: refreshAlbums()
    Connections { target: LibraryService; function onLibraryChanged() { tray.refreshAlbums() } }
    Connections {
        target: PhotoTrayState
        function onPulsed() { pulseAnimation.restart() }
        function onBuildingChanged() { if (PhotoTrayState.building) tray.refreshAlbums() }
    }
    Behavior on reveal { NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic } }
    visible: reveal > 0
    opacity: reveal
    transform: Translate { y: (1 - tray.reveal) * Theme.spaceXl }
    SequentialAnimation {
        id: pulseAnimation
        NumberAnimation { target: tray; property: "pulse"; to: 1; duration: Theme.motionFast }
        NumberAnimation { target: tray; property: "pulse"; to: 0; duration: Theme.motionSlow }
    }
    DropArea {
        id: photoDrop
        objectName: "photoAlbumDrop"
        enabled: PhotoTrayState.building && !PhotoTrayState.discardConfirmation
        anchors.fill: parent
        keys: ["zuuned-local-photos"]
        onDropped: drop => {
            if (!drop.source) return
            const value = drop.source.dragPayload
            const rows = typeof value === "function" ? value() : value
            if (!rows) return
            PhotoTrayState.addPhotos(rows)
            drop.acceptProposedAction()
        }
    }
    Rectangle {
        id: card
        anchors.fill: parent
        color: Theme.customizeSurface
        radius: Theme.radiusLg
        border.width: photoDrop.containsDrag ? Theme.hairline * 2 : Theme.hairline
        border.color: photoDrop.containsDrag || tray.pulse > 0 ? Theme.orange : Theme.borderLight
        Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
    }
    DropShadow {
        anchors.fill: card
        source: card
        radius: Theme.spaceXxxl
        samples: 49
        verticalOffset: Theme.spaceLg
        color: Theme.customizeShadow
        z: -1
    }
    component TrayButton: DetailActionButton {
        Layout.fillWidth: false
        implicitWidth: title.length * Theme.spaceSm + Theme.spaceLg
        opacity: enabled ? 1 : Theme.disabledOpacity
    }
    ColumnLayout {
        enabled: !PhotoTrayState.discardConfirmation
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        spacing: Theme.spaceSm
        Text {
            text: PhotoTrayState.albumId < 0 ? "NEW PHOTO ALBUM" : "EDITING PHOTO ALBUM"
            color: Theme.orange
            font.pixelSize: Theme.customizeCaptionSize
            font.bold: true
        }
        TextField {
            id: albumName
            objectName: "photoTrayName"
            Layout.fillWidth: true
            text: PhotoTrayState.name
            placeholderText: "Album name"
            maximumLength: 200
            color: Theme.textPrimary
            placeholderTextColor: Theme.textDim
            font.pixelSize: Theme.customizeHeadingSize
            padding: Theme.spaceSm
            background: Rectangle { color: Theme.cardBg; radius: Theme.radiusSm; border.color: albumName.activeFocus ? Theme.orange : Theme.borderLight }
            onTextEdited: PhotoTrayState.name = text
        }
        Text { text: "PARENT ALBUM"; color: Theme.textSecondary; font.pixelSize: Theme.customizeLabelSize }
        ComboBox {
            id: parentPicker
            objectName: "photoTrayParent"
            Layout.fillWidth: true
            model: tray.destinations
            textRole: "pathLabel"
            currentIndex: tray.destinations.findIndex(a => a.id === PhotoTrayState.parentId)
            onActivated: PhotoTrayState.parentId = tray.destinations[currentIndex].id
            background: Rectangle { color: Theme.cardBg; radius: Theme.radiusSm; border.color: Theme.borderLight }
            contentItem: Text {
                text: parentPicker.displayText
                color: Theme.textPrimary
                font.pixelSize: Theme.customizeBodySize
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.spaceSm
                rightPadding: Theme.spaceXxl
            }
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: parentPicker.width
                contentItem: Text { text: modelData.pathLabel; color: Theme.textPrimary; elide: Text.ElideRight }
                background: Rectangle { color: highlighted ? Theme.cardHover : Theme.customizeSurface }
                highlighted: parentPicker.highlightedIndex === index
            }
            popup: Popup {
                y: parentPicker.height
                width: parentPicker.width
                implicitHeight: Math.min(contentItem.implicitHeight, Theme.spaceHuge * 5)
                padding: Theme.spaceXxs
                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: parentPicker.popup.visible ? parentPicker.delegateModel : null
                    currentIndex: parentPicker.highlightedIndex
                    ScrollBar.vertical: ZuneScrollBar {}
                }
                background: Rectangle { color: Theme.customizeSurface; radius: Theme.radiusMd; border.color: Theme.borderLight }
            }
        }
        Text {
            text: PhotoTrayState.photos.length + (PhotoTrayState.photos.length === 1 ? " photo" : " photos")
            color: Theme.textSecondary
            font.pixelSize: Theme.customizeCaptionSize
        }
        ListView {
            id: photoList
            objectName: "photoTrayList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            model: PhotoTrayState.photos
            spacing: Theme.spaceXxs
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: Theme.spaceHuge + Theme.spaceSm
                radius: Theme.radiusSm
                color: Theme.cardBg
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXxs
                    spacing: Theme.spaceXxs
                    Image {
                        Layout.preferredWidth: Theme.spaceHuge
                        Layout.preferredHeight: Theme.spaceHuge
                        source: row.modelData.url || ""
                        sourceSize.width: Theme.spaceHuge * 2
                        sourceSize.height: Theme.spaceHuge * 2
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.filename || "Photo " + row.modelData.id
                        color: Theme.textPrimary
                        font.pixelSize: Theme.customizeCaptionSize
                        elide: Text.ElideMiddle
                    }
                    Repeater {
                        model: ["↑", "↓", "×"]
                        delegate: ToolButton {
                            id: memberAction
                            required property string modelData
                            required property int index
                            objectName: "photoTrayAction" + row.modelData.id + "_" + index
                            Layout.preferredWidth: Theme.spaceXxl
                            Layout.preferredHeight: Theme.spaceXxxl
                            enabled: index === 2 || (index === 0 ? row.index > 0 : row.index < PhotoTrayState.photos.length - 1)
                            Accessible.name: index === 0 ? "Move photo earlier" : index === 1 ? "Move photo later" : "Remove photo from draft"
                            contentItem: Text { text: memberAction.modelData; color: memberAction.enabled ? Theme.textPrimary : Theme.textDim; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: memberAction.hovered ? Theme.cardHover : Theme.transparent; radius: Theme.radiusSm }
                            onClicked: index === 2 ? PhotoTrayState.removeAt(row.index) : PhotoTrayState.move(row.index, row.index + (index === 0 ? -1 : 1))
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spaceLg
                visible: photoList.count === 0
                text: "Drag photos or folders here,\nor select photos and choose Add."
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: Theme.customizeBodySize
            }
        }
        Text {
            Layout.fillWidth: true
            visible: PhotoTrayState.error !== ""
            text: PhotoTrayState.error
            color: Theme.error
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.customizeCaptionSize
        }
        RowLayout {
            Layout.fillWidth: true
            TrayButton { objectName: "photoTrayCancel"; title: "Cancel"; onClicked: PhotoTrayState.requestDiscard() }
            Item { Layout.fillWidth: true }
            TrayButton { objectName: "photoTraySave"; title: "Save album"; kind: "primary"; enabled: PhotoTrayState.name.trim() !== ""; onClicked: PhotoTrayState.save() }
        }
    }
    Rectangle {
        objectName: "photoTrayDiscardConfirmation"
        anchors.fill: parent
        visible: PhotoTrayState.discardConfirmation
        color: Theme.customizeSurface
        radius: Theme.radiusLg
        border.color: Theme.orange
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - Theme.spaceXxl
            spacing: Theme.spaceLg
            Text { Layout.fillWidth: true; text: "Discard this album draft?"; wrapMode: Text.WordWrap; color: Theme.textPrimary; font.pixelSize: Theme.customizeHeadingSize }
            Text { Layout.fillWidth: true; text: "Your unsaved changes will be lost."; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.customizeBodySize }
            TrayButton { objectName: "photoTrayKeepEditing"; Layout.fillWidth: true; title: "Keep editing"; onClicked: PhotoTrayState.cancelDiscard() }
            TrayButton { objectName: "photoTrayConfirmDiscard"; Layout.fillWidth: true; title: "Discard draft"; onClicked: PhotoTrayState.confirmDiscard() }
        }
    }
}
