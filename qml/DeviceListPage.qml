import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shared list page for Device Videos / Device Pictures / Playlists —
// mirrors the mac's flat device pages: themed rows over ZMDB data with
// an empty state. Entries: {name, kind?, sizeMB?, count?}.
Item {
    id: root

    property var entries: []
    property string emptyLabel: "nothing here"
    property string emptyHint: ""
    property string countNoun: "tracks"   // for playlist count rows
    // Device pictures: fetch + show photo thumbnails per row
    property bool showThumbs: false

    ColumnLayout {
        anchors.centerIn: parent
        visible: !DeviceService.connected || root.entries.length === 0
        spacing: Theme.spaceLg

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: DeviceService.connected ? root.emptyLabel : "no device"
            color: Theme.textDim
            font.family: Theme.displayFamily
            font.pixelSize: 34
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: text.length > 0
            text: DeviceService.connected ? root.emptyHint : "plug in a zune"
            color: Theme.textGhost
            font.pixelSize: 14
            font.weight: Font.Light
        }
    }

    ListView {
        id: list
        anchors.fill: parent
        visible: DeviceService.connected && root.entries.length > 0
        clip: true
        model: root.entries
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ZuneScrollBar {}

        delegate: Rectangle {
            required property var modelData

            width: list.width
            height: root.showThumbs ? 56 : 40
            color: rowArea.containsMouse ? Theme.rowHoverPink : "transparent"

            Component.onCompleted: {
                if (root.showThumbs && modelData.itemId)
                    DeviceService.requestPhotoThumb(modelData.itemId)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spaceSm
                anchors.rightMargin: Theme.spaceMd
                spacing: Theme.spaceMd

                Rectangle {
                    visible: root.showThumbs
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: Theme.radiusSm
                    color: Theme.cardBg
                    clip: true

                    ArtworkImage {
                        anchors.fill: parent
                        source: DeviceService.artPaths[String(modelData.itemId ?? 0)] ?? ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: source !== ""
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !(DeviceService.artPaths[String(modelData.itemId ?? 0)] ?? "")
                        text: "▦"
                        color: Theme.textGhost
                        font.pixelSize: 16
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: modelData.name
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.Light
                    elide: Text.ElideRight
                }
                Text {
                    visible: (modelData.kind ?? "") !== ""
                    text: modelData.kind ?? ""
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    font.weight: Font.Light
                }
                Text {
                    visible: modelData.count !== undefined
                    text: (modelData.count ?? 0) + " " + root.countNoun
                    color: Theme.textDim
                    font.pixelSize: 12
                    font.weight: Font.Light
                }
                Text {
                    visible: modelData.sizeMB !== undefined
                    text: (modelData.sizeMB ?? 0).toFixed(1) + " MB"
                    color: Theme.textDim
                    font.pixelSize: 12
                    font.weight: Font.Light
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.separator
            }

            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }
    }
}
