import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// Rename an item ON the Zune — sets the display Name (0xDC44), the
// string every list on the device shows.
Popup {
    id: sheet

    property double itemId: 0

    function openFor(id, currentName) {
        itemId = id
        nameField.text = currentName
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function commit() {
        if (nameField.text.trim() === "")
            return
        DeviceService.renameItem(sheet.itemId, nameField.text.trim())
        sheet.close()
    }

    width: 420
    modal: true
    padding: 0

    background: Rectangle {
        color: Theme.surfaceBg
        radius: Theme.radiusLg
        border.width: 1
        border.color: Theme.glassBorder
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceMd

        Text {
            Layout.topMargin: Theme.spaceLg
            Layout.leftMargin: Theme.spaceLg
            text: "RENAME ON ZUNE"
            font.pixelSize: 11
            font.bold: true
            font.letterSpacing: 1.5
            color: Theme.pink
        }

        ZuneSearchField {
            id: nameField
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            placeholderText: "new name"
            onAccepted: sheet.commit()
        }

        RowLayout {
            Layout.margins: Theme.spaceLg
            Layout.topMargin: 0
            spacing: Theme.spaceMd
            Item { Layout.fillWidth: true }
            Text {
                text: "cancel"
                font.pixelSize: 13
                color: Theme.textSecondary
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.close()
                }
            }
            DetailActionButton {
                Layout.preferredWidth: 100
                title: "rename"
                kind: "primary"
                onClicked: sheet.commit()
            }
        }
    }
}
