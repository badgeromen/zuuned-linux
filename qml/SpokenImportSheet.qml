import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

Popup {
    id: sheet
    property bool book: false
    property var orderedParts: []
    property bool saving: false
    property bool previewReady: false
    function openFeed() { previewReady = false; book = false; saving = false; feedUrl.text = ""; open() }
    function openBook(files) { previewReady = false; book = true; saving = false; orderedParts = []; open(); SpokenMediaService.previewBook(files) }
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
        spacing: Theme.spaceMd
        RowLayout {
            Layout.fillWidth: true
            Text { text: sheet.book ? "A PLACE FOR EVERY STORY." : "FOLLOW THE SIGNAL."; font.family: Theme.displayFamily; font.pixelSize: Theme.spokenTitle; color: Theme.activePink; Layout.fillWidth: true; wrapMode: Text.Wrap }
            SpokenButton { text: "×"; enabled: !sheet.saving; onClicked: sheet.close() }
        }
        Text { Layout.fillWidth: true; text: sheet.book ? "Import one book. Review its identity and order all its files before saving." : "Paste the show’s RSS address. Preview it first, then choose what to download."; color: Theme.textSecondary; font.pixelSize: Theme.spokenBody; wrapMode: Text.Wrap }
        RowLayout {
            visible: !sheet.book; Layout.fillWidth: true
            ZuneSearchField { id: feedUrl; objectName: "spokenFeedUrl"; Layout.fillWidth: true; placeholderText: "https://… / feed.rss"; onTextEdited: sheet.previewReady = false }
            SpokenButton { text: "preview"; enabled: !SpokenMediaService.busy && feedUrl.text.trim().length > 0; onClicked: SpokenMediaService.previewFeed(feedUrl.text.trim()) }
        }
        RowLayout {
            visible: !sheet.book && sheet.previewReady && SpokenMediaService.preview.kind === "show"
            Layout.fillWidth: true
            Rectangle { Layout.preferredWidth: Theme.spokenThumbSize; Layout.preferredHeight: Theme.spokenThumbSize; color: Theme.artworkPlaceholder
                Image { anchors.fill: parent; source: SpokenMediaService.preview.autoArt ?? ""; sourceSize.width: Theme.spokenImageDecode; fillMode: Image.PreserveAspectCrop; asynchronous: true }
            }
            ColumnLayout { Layout.fillWidth: true
                Text { text: SpokenMediaService.preview.title ?? ""; color: Theme.textPrimary; font.pixelSize: Theme.spokenTitle; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { text: SpokenMediaService.preview.author ?? ""; color: Theme.textSecondary; font.pixelSize: Theme.spokenBody }
                Text { text: (SpokenMediaService.preview.episodes ?? []).length + " episodes · manual downloads"; color: Theme.textDim; font.pixelSize: Theme.spokenCaption }
            }
        }
        ColumnLayout {
            visible: sheet.book
            Layout.fillWidth: true
            Label { text: "book title"; color: Theme.textSecondary }
            ZuneSearchField { id: bookTitle; Layout.fillWidth: true; placeholderText: "Book title" }
            Label { text: "author / narrator"; color: Theme.textSecondary }
            RowLayout { Layout.fillWidth: true
                ZuneSearchField { id: author; Layout.fillWidth: true; placeholderText: "Author" }
                ZuneSearchField { id: narrator; Layout.fillWidth: true; placeholderText: "Narrator" }
            }
        }
        ListView {
            visible: sheet.book; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: sheet.orderedParts
            delegate: RowLayout {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: Theme.spokenRowHeight
                Text { text: (index + 1) + ". " + modelData.title; color: Theme.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: Theme.spokenBody }
                SpokenButton { text: "↑"; enabled: index > 0 && !sheet.saving; onClicked: { let rows = sheet.orderedParts.slice(); let a = rows[index]; rows[index] = rows[index - 1]; rows[index - 1] = a; sheet.orderedParts = rows } }
                SpokenButton { text: "↓"; enabled: index + 1 < sheet.orderedParts.length && !sheet.saving; onClicked: { let rows = sheet.orderedParts.slice(); let a = rows[index]; rows[index] = rows[index + 1]; rows[index + 1] = a; sheet.orderedParts = rows } }
            }
            ScrollBar.vertical: ZuneScrollBar {}
        }
        Item { visible: !sheet.book; Layout.fillHeight: true }
        Text { text: SpokenMediaService.busy ? SpokenMediaService.status + "…" : ""; color: Theme.textSecondary; font.pixelSize: Theme.spokenCaption }
        RowLayout {
            Layout.fillWidth: true
            SpokenButton { text: "cancel"; enabled: !sheet.saving; onClicked: sheet.close() }
            Item { Layout.fillWidth: true }
            SpokenButton {
                objectName: "spokenSaveImport"
                text: sheet.saving ? "saving…" : sheet.book ? "add book" : "subscribe"
                enabled: !SpokenMediaService.busy && !sheet.saving && (sheet.book ? sheet.orderedParts.length > 0 && bookTitle.text.trim().length > 0 : sheet.previewReady && SpokenMediaService.preview.kind === "show")
                onClicked: {
                    sheet.saving = true
                    if (sheet.book) SpokenMediaService.importBook(bookTitle.text, author.text, narrator.text, sheet.orderedParts.map(p => p.path))
                    else SpokenMediaService.subscribePreview()
                }
            }
        }
    }
    Connections { target: SpokenMediaService
        function onPreviewChanged() {
            if (!sheet.visible) return
            if (!sheet.book && SpokenMediaService.preview.kind === "show") { sheet.previewReady = true; return }
            if (!sheet.book || SpokenMediaService.preview.kind !== "book") return
            sheet.previewReady = true
            const p = SpokenMediaService.preview
            bookTitle.text = p.title ?? ""; author.text = p.author ?? ""; narrator.text = p.narrator ?? ""; sheet.orderedParts = p.parts ?? []
        }
        function onOperationFinished(operation, id, ok, error) {
            if (!sheet.saving || (operation !== "subscribe" && operation !== "import book")) return
            sheet.saving = false; if (ok) sheet.close()
        }
    }
}
