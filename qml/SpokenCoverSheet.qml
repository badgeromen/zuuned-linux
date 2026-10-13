import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zuuned

Popup {
    id: sheet
    property var context: ({})
    property url draftSource: ""
    property bool reset: false
    property bool saving: false
    property bool dirty: false
    property string errorText: ""
    property int generation: 0
    function openFor(entry) {
        context = entry
        draftSource = SpokenMediaService.cover(entry.id)
        reset = false; saving = false; dirty = false; errorText = ""; generation++
        open()
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.spokenSheetWidth, parent.width - Theme.spaceHuge)
    height: Math.min(Theme.spokenSheetHeight, parent.height - Theme.spaceHuge)
    modal: true
    closePolicy: saving ? Popup.NoAutoClose : Popup.CloseOnEscape
    padding: Theme.spaceXxl
    background: Rectangle { color: Theme.surfaceBg; radius: Theme.radiusLg; border.width: Theme.hairline; border.color: Theme.focusRing }
    Overlay.modal: Rectangle { color: Theme.spokenScrim }
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceLg
        RowLayout {
            Layout.fillWidth: true
            Text { text: "MAKE IT YOURS."; font.family: Theme.displayFamily; font.pixelSize: Theme.spokenTitle; color: Theme.activePink; Layout.fillWidth: true }
            SpokenButton { text: "×"; enabled: !sheet.saving; onClicked: sheet.close() }
        }
        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: Theme.spaceXxl
            ColumnLayout {
                Layout.preferredWidth: sheet.width * Theme.spokenCoverFraction
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * (sheet.context.kind === "book" ? Theme.spokenBookRatio : 1)
                    color: Theme.artworkPlaceholder
                    Image { anchors.fill: parent; source: sheet.draftSource; sourceSize.width: Theme.spokenImageDecode; fillMode: Image.PreserveAspectFit; asynchronous: true }
                }
                Text { Layout.fillWidth: true; text: sheet.context.title ?? ""; font.pixelSize: Theme.spokenBody; color: Theme.textPrimary; wrapMode: Text.Wrap }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceMd
                Text { text: "artwork"; color: Theme.activePink; font.pixelSize: Theme.spokenBody }
                Text { Layout.fillWidth: true; text: "Choose your cover. Your choice stays put when feeds refresh or files are rescanned."; color: Theme.textSecondary; wrapMode: Text.Wrap; font.pixelSize: Theme.spokenBody }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.spokenDropHeight
                    color: imageDrop.containsDrag ? Theme.rowSelectedPink : Theme.cardBg
                    border.width: Theme.hairline; border.color: Theme.focusRing; radius: Theme.radiusMd
                    Column { anchors.centerIn: parent; spacing: Theme.spaceSm
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "drop a cover here"; font.family: Theme.displayFamily; font.pixelSize: Theme.spokenTitle; color: Theme.activePink }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "or click to choose · JPEG / PNG / WebP"; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
                    }
                    MouseArea { anchors.fill: parent; enabled: !sheet.saving; cursorShape: Qt.PointingHandCursor; onClicked: { imagePicker.generation = sheet.generation; imagePicker.open() } }
                    DropArea { id: imageDrop; anchors.fill: parent; enabled: !sheet.saving
                        onDropped: drop => {
                            if (!drop.hasUrls || drop.urls.length !== 1 || !drop.urls[0].toString().startsWith("file:")) return
                            sheet.draftSource = drop.urls[0]; sheet.reset = false; sheet.dirty = true; sheet.errorText = ""; drop.acceptProposedAction()
                        }
                    }
                }
                Text { Layout.fillWidth: true; text: sheet.errorText; visible: text.length > 0; color: Theme.error; wrapMode: Text.Wrap; font.pixelSize: Theme.spokenCaption }
                Item { Layout.fillHeight: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            SpokenButton { text: "reset to automatic"; enabled: !sheet.saving; onClicked: { sheet.reset = true; sheet.draftSource = sheet.context.autoArt ?? ""; sheet.dirty = true } }
            Item { Layout.fillWidth: true }
            SpokenButton { objectName: "spokenCancelCover"; text: "cancel"; enabled: !sheet.saving; onClicked: sheet.close() }
            SpokenButton { objectName: "spokenApplyCover"; text: sheet.saving ? "saving…" : "apply changes"; enabled: sheet.dirty && !sheet.saving; onClicked: { sheet.saving = true; SpokenMediaService.saveCover(sheet.context.id, sheet.draftSource, sheet.reset, sheet.context.coverRevision ?? 0) } }
        }
    }
    FileDialog { id: imagePicker; property int generation: 0; title: "Choose a cover"; nameFilters: ["Images (*.jpg *.jpeg *.png *.webp)"]
        onAccepted: { if (!sheet.visible || generation !== sheet.generation) return; sheet.draftSource = selectedFile; sheet.reset = false; sheet.dirty = true }
    }
    Connections { target: SpokenMediaService
        function onOperationFinished(operation, id, ok, error) {
            if (operation !== "save cover" || id !== sheet.context.id || !sheet.saving) return
            sheet.saving = false
            if (ok) sheet.close(); else sheet.errorText = error
        }
    }
}
