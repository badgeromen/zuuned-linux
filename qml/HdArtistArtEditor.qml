import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt5Compat.GraphicalEffects

// This component edits a draft only. The shared Customize Apply owns saving.
ScrollView {
    id: editor
    objectName: "hdArtistArtEditor"
    property var draft: ({background:"", thumbnail:"", gallery:[], biography:"", expectedRevision:""})
    property bool busy: false
    property int contextVersion: 0
    property int pickerVersion: -1
    property string pickRole: "background"
    signal edited(var next)
    clip: true
    contentWidth: availableWidth
    onVisibleChanged: if (!visible) { pickerVersion = -1; picker.close() }
    onContextVersionChanged: { pickerVersion = -1; picker.close() }
    function change(key, value) {
        if (!busy) edited(Object.assign({}, draft, {[key]: value}))
    }
    function choose(role) {
        if (busy) return
        pickRole = role; pickerVersion = contextVersion; picker.open()
    }
    function addGallery(urls) {
        const next = (draft.gallery || []).slice()
        for (const url of urls) {
            const path = String(url)
            if (path.startsWith("file:") && next.indexOf(path) < 0 && next.length < 6) next.push(path)
        }
        change("gallery", next)
    }
    function moveGallery(index, direction) {
        const next = (draft.gallery || []).slice()
        const target = index + direction
        if (target < 0 || target >= next.length) return
        const item = next.splice(index, 1)[0]; next.splice(target, 0, item)
        change("gallery", next)
    }
    function removeGallery(index) {
        const next = (draft.gallery || []).slice(); next.splice(index, 1); change("gallery", next)
    }
    function clearDraft() {
        if (busy) return
        edited({background:"", thumbnail:"", gallery:[], biography:"", expectedRevision:draft.expectedRevision || ""})
    }
    component Action: Button {
        id: action
        font.pixelSize: Theme.customizeCaptionSize
        padding: Theme.spaceSm
        contentItem: Text {
            text: action.text; font: action.font
            color: !action.enabled ? Theme.textSubtle : action.hovered ? Theme.activePink : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
        }
        background: Rectangle {
            color: Theme.transparent
            radius: Theme.radiusSm
            border.width: action.activeFocus ? 1 : 0
            border.color: Theme.activePink
        }
    }
    component Caption: Text {
        font.pixelSize: Theme.customizeCaptionSize
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }
    component Preview: Image {
        id: preview
        // Use raw source pixels here, never the desktop print-effect pipeline.
        autoTransform: true
        asynchronous: true
        sourceSize.width: Theme.customizeThumbSize * 2
        sourceSize.height: Theme.customizeThumbSize * 2
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: preview.width; height: preview.height
                radius: Theme.radiusMd
            }
        }
    }
    ColumnLayout {
        width: editor.availableWidth
        spacing: Theme.spaceLg
        Caption {
            Layout.fillWidth: true
            text: "Give their HD artist page a face. Saved here, included when you sync new music to a Zune HD."
        }
        Caption {
            Layout.fillWidth: true
            color: Theme.warning
            text: "Existing artists may keep cached content. This does not remove or recreate anything on your Zune."
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceLg
            Repeater {
                model: [{role:"background", label:"now playing", ratio:1}, {role:"thumbnail",label:"artist thumbnail",ratio:4/3}]
                ColumnLayout {
                    id: roleColumn
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm
                    Caption { text: roleColumn.modelData.label }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.customizeThumbSize
                        color: Theme.artworkPlaceholder
                        radius: Theme.radiusMd
                        Preview {
                            anchors.centerIn: parent
                            width: Math.min(parent.width, parent.height * roleColumn.modelData.ratio)
                            height: width / roleColumn.modelData.ratio
                            source: editor.draft[roleColumn.modelData.role] || (roleColumn.modelData.role === "thumbnail" ? editor.draft.background || "" : "")
                            fillMode: Image.PreserveAspectCrop
                            clip: true
                        }
                        DropArea {
                            anchors.fill: parent
                            onEntered: drag => { drag.accepted = !editor.busy && drag.hasUrls && drag.urls.length === 1 && String(drag.urls[0]).startsWith("file:") }
                            onDropped: drop => {
                                if (!editor.busy && drop.hasUrls && drop.urls.length === 1 && String(drop.urls[0]).startsWith("file:")) {
                                    editor.change(roleColumn.modelData.role, String(drop.urls[0])); drop.acceptProposedAction()
                                }
                            }
                        }
                    }
                    Action {
                        text: "choose image…"; enabled: !editor.busy
                        onClicked: editor.choose(roleColumn.modelData.role)
                    }
                    Action {
                        visible: roleColumn.modelData.role === "thumbnail" && !!editor.draft.thumbnail
                        text: "use background"; enabled: !editor.busy
                        onClicked: editor.change("thumbnail", "")
                    }
                }
            }
        }
        Caption {
            Layout.fillWidth: true
            text: "Background and thumbnail use a centered crop. Gallery images keep their proportions. Your desktop portrait stays separate."
        }
        RowLayout {
            Caption { Layout.fillWidth: true; text: "artist gallery · " + (editor.draft.gallery || []).length + " / 6" }
            Action { text: "+ images"; enabled: !editor.busy && (editor.draft.gallery || []).length < 6; onClicked: editor.choose("gallery") }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spaceSm
            Repeater {
                model: editor.draft.gallery || []
                Column {
                    id: galleryItem
                    required property string modelData
                    required property int index
                    width: Theme.customizeThumbSize
                    Preview {
                        width: parent.width; height: width
                        source: galleryItem.modelData
                        fillMode: Image.PreserveAspectFit
                    }
                    Row {
                        Action { text: "‹"; enabled: galleryItem.index > 0; Accessible.name: "Move picture earlier"; onClicked: editor.moveGallery(galleryItem.index, -1) }
                        Action { text: "×"; Accessible.name: "Remove gallery picture"; onClicked: editor.removeGallery(galleryItem.index) }
                        Action { text: "›"; enabled: galleryItem.index < (editor.draft.gallery || []).length - 1; Accessible.name: "Move picture later"; onClicked: editor.moveGallery(galleryItem.index, 1) }
                    }
                }
            }
        }
        CustomizeField {
            objectName: "hdArtistBiography"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.customizeFieldHeight * 3
            label: "biography"
            placeholder: "Tell their story. Plain text, up to 4096 UTF-8 bytes."
            value: editor.draft.biography || ""
            multiline: true
            onEdited: value => editor.change("biography", value)
        }
        Action {
            text: "clear saved HD choices"
            enabled: !editor.busy
            onClicked: editor.clearDraft()
        }
    }
    FileDialog {
        id: picker
        title: editor.pickRole === "gallery" ? "Choose up to six artist gallery images" : "Choose artist image"
        fileMode: editor.pickRole === "gallery" ? FileDialog.OpenFiles : FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
            if (editor.busy || !editor.visible || editor.pickerVersion !== editor.contextVersion) return
            if (editor.pickRole === "gallery") editor.addGallery(selectedFiles)
            else editor.change(editor.pickRole, selectedFile.toString())
            editor.pickerVersion = -1
        }
    }
}
