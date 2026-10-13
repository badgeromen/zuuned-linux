import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Zuuned

// The same floating workbench geometry as photo albums and mixtapes.
Item {
    id: tray
    objectName: "bookLibraryTray"
    property real reveal: BookTrayState.building ? 1 : 0
    property real pulse: 0
    Behavior on reveal { NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.OutCubic } }
    visible: reveal > 0
    opacity: reveal
    transform: Translate { y: (1 - tray.reveal) * Theme.spaceXl }
    Connections { target: BookTrayState; function onPulsed() { pulseAnimation.restart() } }
    SequentialAnimation {
        id: pulseAnimation
        NumberAnimation { target: tray; property: "pulse"; to: 1; duration: Theme.motionFast }
        NumberAnimation { target: tray; property: "pulse"; to: 0; duration: Theme.motionSlow }
    }
    DropArea {
        id: bookDrop
        objectName: "bookLibraryDrop"
        anchors.fill: parent
        enabled: BookTrayState.building && !BookTrayState.locked
        keys: ["zuuned-local-books"]
        onDropped: drop => {
            if (!enabled || !drop.source || drop.source.dragKind !== "audiobook-books") return
            const payload = drop.source.dragPayload
            const ids = typeof payload === "function" ? payload() : payload
            if (!Array.isArray(ids)) return
            BookTrayState.addBooks(ids)
            drop.acceptProposedAction()
        }
    }
    Rectangle {
        id: card
        anchors.fill: parent
        color: Theme.customizeSurface; radius: Theme.radiusLg
        border.width: bookDrop.containsDrag ? Theme.hairline * 2 : Theme.hairline
        border.color: bookDrop.containsDrag || tray.pulse > 0 ? Theme.orange : Theme.borderLight
        Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
    }
    DropShadow {
        anchors.fill: card; source: card
        radius: Theme.spaceXxxl; samples: Theme.spokenShadowSamples
        verticalOffset: Theme.spaceLg; color: Theme.customizeShadow; z: -1
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: Theme.spaceMd; spacing: Theme.spaceSm
        enabled: !BookTrayState.locked
        Text { text: BookTrayState.context.id ? "EDITING LIBRARY" : "NEW AUDIOBOOK LIBRARY"; color: Theme.orange; font.pixelSize: Theme.customizeCaptionSize; font.bold: true }
        TextField {
            id: libraryName
            objectName: "bookTrayName"
            Layout.fillWidth: true
            text: BookTrayState.name
            placeholderText: "Library name"
            maximumLength: 500
            color: Theme.textPrimary; placeholderTextColor: Theme.textDim
            font.pixelSize: Theme.customizeHeadingSize; padding: Theme.spaceSm
            background: Rectangle { color: Theme.cardBg; radius: Theme.radiusSm; border.color: libraryName.activeFocus ? Theme.orange : Theme.borderLight }
            onTextEdited: BookTrayState.name = text
        }
        Text { text: BookTrayState.bookIds.length + " books · reading order"; color: Theme.textSecondary; font.pixelSize: Theme.customizeCaptionSize }
        ListView {
            id: members
            objectName: "bookTrayMembers"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: Theme.spaceXxs
            model: BookTrayState.bookIds
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {}
            delegate: Item {
                id: member
                required property string modelData
                required property int index
                readonly property var book: { void SpokenMediaService.revision; return SpokenMediaService.item(modelData) }
                width: ListView.view.width; height: Theme.spokenRowHeight
                DropArea {
                    anchors.fill: parent
                    keys: ["zuuned-library-order"]
                    onDropped: drag => {
                        if (!BookTrayState.locked && drag.source && drag.source.bookId) {
                            const from = BookTrayState.bookIds.indexOf(drag.source.bookId)
                            BookTrayState.move(from, member.index)
                            drag.acceptProposedAction()
                        }
                    }
                }
                RowLayout {
                    anchors.fill: parent; spacing: Theme.spaceXs
                    Item {
                        Layout.preferredWidth: Theme.spaceXl
                        Layout.fillHeight: true
                        Text { anchors.centerIn: parent; text: "≡"; color: Theme.textSecondary; font.pixelSize: Theme.spokenBody }
                        MouseArea {
                            id: reorder
                            objectName: "bookReorder_" + member.modelData
                            anchors.fill: parent; cursorShape: Qt.SizeVerCursor
                            drag.target: orderGhost; preventStealing: true
                            onReleased: orderGhost.Drag.drop()
                            onPressed: mouse => { const p = mapToItem(tray, mouse.x, mouse.y); orderGhost.x = p.x; orderGhost.y = p.y; orderGhost.bookId = member.modelData; orderGhost.dragArea = reorder }
                        }
                    }
                    RoundedArtwork {
                        Layout.preferredWidth: Theme.spokenChapterHeight
                        Layout.preferredHeight: Theme.spokenChapterHeight
                        source: { void SpokenMediaService.revision; return SpokenMediaService.cover(member.modelData) }
                        fillMode: Image.PreserveAspectCrop
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: Theme.spaceXxs
                        Text { Layout.fillWidth: true; text: member.book.title || "Missing book"; elide: Text.ElideRight; color: Theme.textPrimary; font.pixelSize: Theme.customizeBodySize }
                        Text { Layout.fillWidth: true; text: member.book.author || ""; elide: Text.ElideRight; color: Theme.textDim; font.pixelSize: Theme.customizeCaptionSize }
                    }
                    Repeater {
                        model: ["↑", "↓", "×"]
                        delegate: ToolButton {
                            required property string modelData
                            required property int index
                            objectName: "bookTrayAction_" + member.modelData + "_" + index
                            Layout.preferredWidth: Theme.spaceXxl
                            Layout.preferredHeight: Theme.spaceXxxl
                            enabled: index === 2 || (index === 0 ? member.index > 0 : member.index + 1 < BookTrayState.bookIds.length)
                            Accessible.name: index === 0 ? "Move book earlier" : index === 1 ? "Move book later" : "Remove book from library draft"
                            contentItem: Text { text: modelData; color: enabled ? Theme.textPrimary : Theme.textDim; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: parent.hovered ? Theme.cardHover : Theme.transparent; radius: Theme.radiusSm }
                            onClicked: index === 2 ? BookTrayState.removeAt(member.index) : BookTrayState.move(member.index, member.index + (index === 0 ? -1 : 1))
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent; width: parent.width - Theme.spaceLg
                visible: members.count === 0
                text: "Drag books here, or use + while browsing.\nArrange them in your reading order."
                wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                color: Theme.textDim; font.pixelSize: Theme.customizeBodySize
            }
        }
        Text { Layout.fillWidth: true; visible: !!BookTrayState.error; text: BookTrayState.error; wrapMode: Text.WordWrap; color: Theme.error; font.pixelSize: Theme.customizeCaptionSize }
        SpokenButton { objectName: "bookTrayDelete"; visible: !!BookTrayState.context.id; text: "delete library…"; flat: true; onClicked: BookTrayState.requestDelete() }
        RowLayout {
            Layout.fillWidth: true
            SpokenButton { objectName: "bookTrayCancel"; text: "cancel"; onClicked: BookTrayState.requestDiscard() }
            Item { Layout.fillWidth: true }
            SpokenButton { objectName: "bookTraySave"; text: BookTrayState.saving ? "saving…" : "save library"; highlighted: true; enabled: !!BookTrayState.name.trim() && !SpokenMediaService.busy; onClicked: BookTrayState.save() }
        }
    }
    Item {
        id: orderGhost
        property string bookId: ""
        parent: tray
        width: tray.width; height: Theme.spokenChapterHeight
        visible: Drag.active
        property Item dragArea: null
        Drag.active: !BookTrayState.locked && dragArea !== null && dragArea.drag.active
        Drag.keys: ["zuuned-library-order"]
        Drag.hotSpot.x: Theme.spaceMd; Drag.hotSpot.y: Theme.spaceMd
        Rectangle { anchors.fill: parent; color: Theme.cardActive; radius: Theme.radiusSm; border.color: Theme.orange }
        Text { anchors.centerIn: parent; text: SpokenMediaService.item(orderGhost.bookId).title || ""; color: Theme.textPrimary; font.pixelSize: Theme.customizeBodySize }
    }
    Rectangle {
        anchors.fill: parent
        visible: BookTrayState.discardConfirmation || BookTrayState.deleteConfirmation
        color: Theme.customizeSurface; radius: Theme.radiusLg; border.color: Theme.orange
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.centerIn: parent; width: parent.width - Theme.spaceXxl; spacing: Theme.spaceLg
            Text { Layout.fillWidth: true; text: BookTrayState.deleteConfirmation ? "Delete this library?" : "Discard this library draft?"; wrapMode: Text.WordWrap; color: Theme.textPrimary; font.pixelSize: Theme.customizeHeadingSize }
            Text { Layout.fillWidth: true; text: BookTrayState.deleteConfirmation ? "Your books and files stay in your collection." : "Your unsaved changes will be lost."; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: Theme.customizeBodySize }
            SpokenButton { objectName: "bookTrayKeepEditing"; text: "keep editing"; onClicked: { BookTrayState.cancelDiscard(); BookTrayState.deleteConfirmation = false } }
            SpokenButton { objectName: "bookTrayConfirm"; text: BookTrayState.deleteConfirmation ? "delete library" : "discard draft"; enabled: !SpokenMediaService.busy; onClicked: BookTrayState.deleteConfirmation ? BookTrayState.confirmDelete() : BookTrayState.confirmDiscard() }
        }
    }
}
