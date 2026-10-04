import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// Song liner notes share the Sleeve language. Saving still uses the track
// editor's existing library + MPEG tag path; a rescan preserves these edits.
Popup {
    id: sheet

    property var service: LibraryService
    property var notifications: toastHost
    property double libraryId: -1
    property var current: ({})
    property var fields: ({})
    property string errorMessage: ""
    readonly property bool narrow: width < Theme.customizeCompactAt - Theme.spaceHuge * 3
    readonly property bool valid: String(fields.title || "").trim() !== ""
    readonly property string artwork: service.artPaths
        ? (service.artPaths[service.artKey(current.albumartist || current.artist || "",
                                          current.album || "")] || "") : ""

    function openFor(track) {
        libraryId = track.libraryId
        current = Object.assign({}, track)
        errorMessage = ""
        fields = {
            title: track.title || "",
            artist: track.artist || "",
            albumartist: track.albumartist || "",
            album: track.album || "",
            genre: track.genre || "",
            trackNumber: track.trackNumber > 0 ? String(track.trackNumber) : "",
            year: track.year > 0 ? String(track.year) : ""
        }
        formScroll.contentY = 0
        open()
        titleField.focusInput(true)
    }

    function setField(key, value) {
        const next = Object.assign({}, fields)
        next[key] = value
        fields = next
        errorMessage = ""
    }

    function commit() {
        if (!valid) return
        const result = service.editTrackMetadata(libraryId, {
            title: fields.title,
            artist: fields.artist,
            albumartist: fields.albumartist,
            album: fields.album,
            genre: fields.genre,
            trackNumber: parseInt(fields.trackNumber) || 0,
            year: parseInt(fields.year) || 0
        })
        if (!result.success) {
            errorMessage = result.error || "Couldn't save your edits. Your draft is still here."
            return
        }
        notifications.show(result.fileWritten ? "saved — tags written to the file"
                                 : "saved to library (file tags unchanged)")
        close()
    }

    width: Math.min(Theme.customizeCompactAt,
                    (Overlay.overlay ? Overlay.overlay.width : Theme.customizeCompactAt) - Theme.spaceXxxl)
    height: Math.min(Theme.customizeHeight - Theme.spaceHuge,
                     (Overlay.overlay ? Overlay.overlay.height : Theme.customizeHeight) - Theme.spaceXxxl)
    modal: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.alpha(Theme.bg, 0.7) }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionBase }
            NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.motionBase; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionFast } }

    background: Rectangle {
        color: Theme.customizeSurface
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.customizeSurface }
            GradientStop { position: 1; color: Theme.surfaceBg }
        }
        radius: Theme.radiusLg
        border.width: 1
        border.color: Qt.alpha(Theme.pink, 0.28)
    }

    component QuietAction: Button {
        id: control
        property bool primary: false
        implicitHeight: Theme.spaceHuge - Theme.spaceSm
        implicitWidth: contentItem.implicitWidth + Theme.spaceLg
        padding: Theme.spaceSm
        hoverEnabled: true
        opacity: enabled ? 1 : 0.4
        font.pixelSize: Theme.customizeCaptionSize
        background: Rectangle {
            radius: Theme.radiusSm
            color: control.primary ? (control.hovered ? Theme.activePink : Theme.pink)
                 : control.hovered ? Theme.cardHover : Theme.transparent
            border.width: control.visualFocus ? Theme.spaceXxxs : 0
            border.color: Theme.focusRing
        }
        contentItem: Text {
            text: control.text
            font: control.font
            color: control.primary || control.hovered ? Theme.textPrimary : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    contentItem: ColumnLayout {
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.customizeHeader
            Column {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spaceXxxl
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spaceSm
                Text {
                    text: "YOUR COLLECTION / YOUR CALL"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.customizeLabelSize
                    font.letterSpacing: 1.6
                }
                GradientText {
                    text: "make it yours."
                    font.family: Theme.displayFamily
                    font.pixelSize: sheet.narrow ? Theme.customizeTitleSize - Theme.spaceSm : Theme.customizeTitleSize
                    rotation: -3
                    transformOrigin: Item.Left
                }
            }
            QuietAction {
                objectName: "editTrackClose"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spaceXl
                text: "×"
                font.pixelSize: Theme.customizeHeadingSize
                Accessible.name: "Cancel song edits"
                onClicked: sheet.close()
            }
        }

        Flickable {
            id: formScroll
            objectName: "editTrackForm"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXxxl
            Layout.rightMargin: Theme.spaceXxxl
            Layout.bottomMargin: Theme.spaceXl
            contentWidth: width
            contentHeight: form.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {}

            ColumnLayout {
                id: form
                width: formScroll.width - Theme.spaceMd
                spacing: Theme.spaceXxl
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXl
                    CustomizeArtwork {
                        objectName: "editTrackArtwork"
                        Layout.preferredWidth: Theme.customizeCompactArt
                        Layout.preferredHeight: Theme.customizeCompactArt
                        source: sheet.artwork
                        kind: "album"
                        rotation: -3
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm
                        Text {
                            text: "SONG / IDENTITY"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.customizeLabelSize
                            font.letterSpacing: 1.6
                        }
                        Text {
                            Layout.fillWidth: true
                            text: sheet.current.title || "Tell its story."
                            color: Theme.textPrimary
                            font.pixelSize: Theme.customizeHeadingSize
                            font.weight: Font.Light
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: [sheet.current.artist, sheet.current.album].filter(value => !!value).join(" · ")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.customizeCaptionSize
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
                GridLayout {
                    id: fieldsGrid
                    Layout.fillWidth: true
                    columns: sheet.narrow ? 1 : 2
                    columnSpacing: Theme.spaceXl
                    rowSpacing: Theme.spaceLg
                    CustomizeField {
                        id: titleField
                        objectName: "editTrackTitle"
                        Layout.fillWidth: true
                        Layout.preferredWidth: fieldsGrid.width
                        Layout.columnSpan: sheet.narrow ? 1 : 2
                        label: "title"
                        value: sheet.fields.title || ""
                        placeholder: "Song title"
                        prominent: true
                        onEdited: value => sheet.setField("title", value)
                    }
                    CustomizeField {
                        objectName: "editTrackArtist"
                        Layout.fillWidth: true
                        Layout.preferredWidth: (fieldsGrid.width - fieldsGrid.columnSpacing) / fieldsGrid.columns
                        label: "artist"
                        value: sheet.fields.artist || ""
                        placeholder: "Artist"
                        onEdited: value => sheet.setField("artist", value)
                    }
                    CustomizeField {
                        objectName: "editTrackAlbumArtist"
                        Layout.fillWidth: true
                        Layout.preferredWidth: (fieldsGrid.width - fieldsGrid.columnSpacing) / fieldsGrid.columns
                        label: "album artist"
                        value: sheet.fields.albumartist || ""
                        placeholder: "Same as artist"
                        onEdited: value => sheet.setField("albumartist", value)
                    }
                    CustomizeField {
                        objectName: "editTrackAlbum"
                        Layout.fillWidth: true
                        Layout.preferredWidth: fieldsGrid.width
                        Layout.columnSpan: sheet.narrow ? 1 : 2
                        label: "album"
                        value: sheet.fields.album || ""
                        placeholder: "Album"
                        onEdited: value => sheet.setField("album", value)
                    }
                    CustomizeField {
                        objectName: "editTrackGenre"
                        Layout.fillWidth: true
                        Layout.preferredWidth: (fieldsGrid.width - fieldsGrid.columnSpacing) / fieldsGrid.columns
                        label: "genre"
                        value: sheet.fields.genre || ""
                        placeholder: "Genre"
                        onEdited: value => sheet.setField("genre", value)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: (fieldsGrid.width - fieldsGrid.columnSpacing) / fieldsGrid.columns
                        spacing: Theme.spaceXl
                        CustomizeField {
                            objectName: "editTrackNumber"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: "track #"
                            value: sheet.fields.trackNumber || ""
                            placeholder: "#"
                            numeric: true
                            validator: IntValidator { bottom: 0; top: 999 }
                            onEdited: value => sheet.setField("trackNumber", value)
                        }
                        CustomizeField {
                            objectName: "editTrackYear"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            label: "year"
                            value: sheet.fields.year || ""
                            placeholder: "Year"
                            numeric: true
                            validator: IntValidator { bottom: 0; top: 9999 }
                            onEdited: value => sheet.setField("year", value)
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "editTrackFooter"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.customizeFooter + (sheet.errorMessage ? saveError.implicitHeight + Theme.spaceMd : 0)
            color: Qt.alpha(Theme.bg, 0.42)
            radius: Theme.radiusLg
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.borderLight
            }
            RowLayout {
                anchors.fill: parent
                anchors.topMargin: sheet.errorMessage ? saveError.implicitHeight + Theme.spaceMd : 0
                anchors.leftMargin: Theme.spaceXxxl
                anchors.rightMargin: Theme.spaceXxxl
                spacing: Theme.spaceLg
                Rectangle {
                    visible: !sheet.narrow
                    width: Theme.spaceXxs
                    height: width
                    radius: width / 2
                    color: Theme.green
                }
                Text {
                    Layout.fillWidth: true
                    visible: !sheet.narrow
                    text: "Your edits stay yours."
                    font.pixelSize: Theme.customizeCaptionSize
                    color: Theme.textSecondary
                }
                Item { Layout.fillWidth: true; visible: sheet.narrow }
                QuietAction {
                    objectName: "editTrackCancel"
                    text: "cancel"
                    onClicked: sheet.close()
                }
                QuietAction {
                    objectName: "editTrackSave"
                    text: "save changes  ↗"
                    primary: true
                    enabled: sheet.valid
                    onClicked: sheet.commit()
                }
            }
            Text {
                id: saveError
                objectName: "editTrackError"
                anchors.top: parent.top
                anchors.left: parent.left; anchors.right: parent.right
                anchors.topMargin: Theme.spaceSm
                anchors.leftMargin: Theme.spaceXxxl; anchors.rightMargin: Theme.spaceXxxl
                visible: sheet.errorMessage !== ""
                text: sheet.errorMessage
                color: Theme.warning
                font.pixelSize: Theme.customizeCaptionSize
                wrapMode: Text.WordWrap
            }
        }
    }
}
