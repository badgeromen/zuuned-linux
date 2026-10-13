import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zuuned

Popup {
    id: sheet
    signal imported()
    property bool collection: true
    property bool awaitingPreview: false
    property bool saving: false
    property var books: []
    property int reviewIndex: -1
    property var reviewParts: []
    property string errorText: ""
    property int generation: 0
    function begin() { generation++; books = []; reviewIndex = -1; saving = false; awaitingPreview = false; errorText = ""; open() }
    function review(index) { reviewIndex = index; reviewParts = books[index].parts.slice() }
    function finishReview() { if (reviewIndex >= 0) books[reviewIndex].parts = reviewParts.slice(); reviewIndex = -1 }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.spokenSheetWidth, parent.width - Theme.spaceHuge)
    height: Math.min(Theme.spokenSheetHeight, parent.height - Theme.spaceHuge)
    padding: Theme.spaceXxl
    modal: true
    closePolicy: saving ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.surfaceBg; radius: Theme.radiusLg; border.width: Theme.hairline; border.color: Theme.focusRing }
    Overlay.modal: Rectangle { color: Theme.spokenScrim }
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceMd
        Text { text: "BRING YOUR BOOKS."; color: Theme.activePink; font.family: Theme.displayFamily; font.pixelSize: Theme.spokenTitle }
        Text { Layout.fillWidth: true; text: sheet.collection ? "One book per immediate subfolder. Disc folders stay inside their book. Files directly in the selected folder form a separate book." : "All audio files in the selected folder and its subfolders become one book."; wrapMode: Text.Wrap; color: Theme.textSecondary; font.pixelSize: Theme.spokenBody }
        RowLayout {
            visible: sheet.books.length === 0
            SpokenButton { text: "book collection"; highlighted: sheet.collection; enabled: !sheet.awaitingPreview; onClicked: sheet.collection = true }
            SpokenButton { text: "one book"; highlighted: !sheet.collection; enabled: !sheet.awaitingPreview; onClicked: sheet.collection = false }
            SpokenButton { objectName: "spokenChooseFolder"; text: "choose folder…"; enabled: !SpokenMediaService.busy; onClicked: { picker.generation = sheet.generation; picker.open() } }
        }
        Text { Layout.fillWidth: true; visible: sheet.books.length > 0 && sheet.reviewIndex < 0; text: "Review the titles and part order, then import. Existing books are reused; source files stay in place."; wrapMode: Text.Wrap; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
        ListView {
            visible: sheet.reviewIndex < 0
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: sheet.books
            spacing: Theme.spaceMd
            delegate: ColumnLayout {
                required property var modelData
                required property int index
                width: ListView.view.width; height: Theme.spokenFolderRowHeight
                RowLayout {
                    Layout.fillWidth: true
                    SpokenCheckBox { checked: modelData.included !== false; enabled: !sheet.saving; onClicked: sheet.books[index].included = checked }
                    ZuneSearchField { Layout.fillWidth: true; text: modelData.title; placeholderText: "Book title"; enabled: !sheet.saving; onTextEdited: sheet.books[index].title = text }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ZuneSearchField { Layout.fillWidth: true; text: modelData.author; placeholderText: "Author"; enabled: !sheet.saving; onTextEdited: sheet.books[index].author = text }
                    ZuneSearchField { Layout.fillWidth: true; text: modelData.narrator; placeholderText: "Narrator"; enabled: !sheet.saving; onTextEdited: sheet.books[index].narrator = text }
                }
                Text { Layout.fillWidth: true; text: modelData.folder; elide: Text.ElideMiddle; color: Theme.textDim; font.pixelSize: Theme.spokenCaption }
                SpokenButton { text: modelData.parts.length + " parts · review order"; enabled: !sheet.saving; onClicked: sheet.review(index) }
            }
            ScrollBar.vertical: ZuneScrollBar {}
        }
        SpokenButton { visible: sheet.reviewIndex >= 0; text: "‹ back to books"; onClicked: sheet.finishReview() }
        ListView {
            visible: sheet.reviewIndex >= 0
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: sheet.reviewParts
            delegate: RowLayout {
                required property var modelData
                required property int index
                width: ListView.view.width; height: Theme.spokenRowHeight
                ColumnLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: (index + 1) + ". " + modelData.title; elide: Text.ElideRight; color: Theme.textPrimary; font.pixelSize: Theme.spokenBody }
                    Text { Layout.fillWidth: true; text: modelData.path; elide: Text.ElideMiddle; color: Theme.textDim; font.pixelSize: Theme.spokenCaption }
                }
                SpokenButton { text: "↑"; enabled: index > 0; onClicked: { let p = sheet.reviewParts.slice(); [p[index - 1], p[index]] = [p[index], p[index - 1]]; sheet.reviewParts = p } }
                SpokenButton { text: "↓"; enabled: index + 1 < sheet.reviewParts.length; onClicked: { let p = sheet.reviewParts.slice(); [p[index + 1], p[index]] = [p[index], p[index + 1]]; sheet.reviewParts = p } }
            }
            ScrollBar.vertical: ZuneScrollBar {}
        }
        Text { Layout.fillWidth: true; text: sheet.errorText || (sheet.awaitingPreview || sheet.saving ? SpokenMediaService.status + "…" : ""); wrapMode: Text.Wrap; color: sheet.errorText ? Theme.error : Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
        RowLayout {
            Layout.fillWidth: true
            SpokenButton { text: "cancel"; enabled: !sheet.saving; onClicked: { if (sheet.awaitingPreview) SpokenMediaService.cancelWork(); sheet.close() } }
            Item { Layout.fillWidth: true }
            SpokenButton {
                objectName: "spokenImportFolder"
                text: sheet.saving ? "importing…" : "import books"
                enabled: sheet.books.length > 0 && !SpokenMediaService.busy && !sheet.saving
                onClicked: {
                    sheet.finishReview(); sheet.errorText = ""; sheet.saving = true
                    SpokenMediaService.importBookFolder(sheet.books.map((b, i) => ({index: i, included: b.included !== false, title: b.title, author: b.author, narrator: b.narrator, order: b.parts.map(p => p.path)})).filter(b => b.included))
                }
            }
        }
    }
    FolderDialog {
        id: picker
        property int generation: 0
        title: sheet.collection ? "Choose your audiobook collection folder" : "Choose one audiobook folder"
        onAccepted: {
            if (!sheet.visible || generation !== sheet.generation) return
            sheet.awaitingPreview = true; sheet.errorText = ""
            SpokenMediaService.previewBookFolder(selectedFolder, sheet.collection)
        }
    }
    Connections {
        target: SpokenMediaService
        function onPreviewChanged() {
            if (!sheet.visible || !sheet.awaitingPreview || SpokenMediaService.preview.kind !== "bookFolder") return
            sheet.books = SpokenMediaService.preview.books.map(b => Object.assign({}, b))
        }
        function onOperationFinished(operation, id, ok, error) {
            if (operation === "inspect book folder" && sheet.awaitingPreview) { sheet.awaitingPreview = false; sheet.errorText = error }
            if (operation === "import book folder" && sheet.saving) { sheet.saving = false; if(ok) { sheet.imported(); sheet.close() } else sheet.errorText = error }
        }
    }
}
