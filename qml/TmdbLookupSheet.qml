import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// Manual TMDB lookup sheet — the mac's tmdbLookupSheet. One chosen
// result applies to EVERY id passed in (bulk fix-match: assigning 30
// episode files to one show in one click).
Popup {
    id: sheet

    property var targetIds: []
    property bool searchTV: false
    property var results: []
    property bool searching: false

    function openFor(ids, query, tv) {
        targetIds = ids
        searchTV = tv
        results = []
        queryField.text = query
        open()
        search()
    }
    function search() {
        if (queryField.text === "")
            return
        searching = true
        LibraryService.tmdbSearch(queryField.text, searchTV)
    }

    Connections {
        target: LibraryService
        function onTmdbSearchResults(list) {
            if (!sheet.visible)
                return
            sheet.results = list
            sheet.searching = false
        }
    }

    width: 600
    height: 520
    modal: true
    padding: 0

    background: Rectangle {
        color: Theme.surfaceBg
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.glassBorder
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spaceLg
            Text {
                text: "TMDB LOOKUP"
                font.pixelSize: 11
                font.letterSpacing: 1
                color: Theme.textDim
            }
            Item { Layout.fillWidth: true }
            Text {
                text: sheet.targetIds.length > 1
                    ? "applies to " + sheet.targetIds.length + " files" : ""
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            Text {
                text: "Cancel"
                font.pixelSize: 13
                color: Theme.textSecondary
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.close()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            spacing: Theme.spaceMd

            ZuneSearchField {
                id: queryField
                Layout.fillWidth: true
                placeholderText: "Search TMDB..."
                onAccepted: sheet.search()
            }

            // Movie / TV Show segmented toggle
            Row {
                spacing: 0
                Repeater {
                    model: ["Movie", "TV Show"]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        readonly property bool active:
                            (index === 1) === sheet.searchTV
                        width: 70
                        height: 28
                        color: active ? Qt.alpha(Theme.pink, 0.25) : Theme.cardBg
                        border.width: 1
                        border.color: active
                            ? Qt.rgba(0.83, 0.21, 0.48, 0.5) : Theme.borderLight
                        radius: index === 0 ? Theme.radiusMd : Theme.radiusMd
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData
                            font.pixelSize: 12
                            color: parent.active ? Theme.textPrimary : Theme.textSecondary
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                sheet.searchTV = parent.index === 1
                                sheet.search()
                            }
                        }
                    }
                }
            }

            Text {
                text: "Search"
                font.pixelSize: 13
                color: queryField.text === "" ? Theme.textDim : Theme.pink
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.search()
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceLg
            visible: sheet.searching
            text: "searching..."
            font.pixelSize: 12
            color: Theme.textDim
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spaceSm
            clip: true
            model: sheet.results
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                required property var modelData
                width: ListView.view.width
                height: resultRow.implicitHeight + Theme.spaceLg

                RowLayout {
                    id: resultRow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceLg
                    anchors.rightMargin: Theme.spaceLg
                    anchors.topMargin: Theme.spaceSm
                    spacing: Theme.spaceMd

                    Rectangle {
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 75
                        Layout.alignment: Qt.AlignTop
                        radius: Theme.radiusSm
                        color: Theme.cardBg
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: parent.parent.parent.modelData.posterUrl
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: resultRow.parent.modelData.title
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: resultRow.parent.modelData.year
                            font.pixelSize: 11
                            color: Theme.textSecondary
                        }
                        Text {
                            text: resultRow.parent.modelData.overview
                            font.pixelSize: 11
                            color: Theme.textDim
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Text {
                        text: "Select"
                        font.pixelSize: 13
                        color: Theme.pink
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: Theme.spaceSm
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                LibraryService.assignTmdbResult(
                                    sheet.targetIds,
                                    resultRow.parent.modelData.tmdbId,
                                    sheet.searchTV)
                                sheet.close()
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.separator
                }
            }
        }
    }
}
