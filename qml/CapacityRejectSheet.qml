import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// Shown when the capacity gate refuses queue adds: exactly which
// episodes/movies/tracks did NOT fit, next to what made it in — so
// "N items not queued" is a decision aid, not a mystery.
Popup {
    id: sheet

    property var rejectedItems: []
    property int addedCount: 0
    property double queuedGB: 0
    property double freeGB: 0

    function openWith(items, added, queued, free) {
        if (visible) {
            // Rapid successive adds (row buttons clicked one by one):
            // ACCUMULATE — replacing the list hid every rejection but
            // the last one.
            rejectedItems = rejectedItems.concat(items)
            addedCount += added
        } else {
            rejectedItems = items
            addedCount = added
        }
        queuedGB = queued
        freeGB = free
        open()
    }
    function fmtGB(gb) {
        return gb < 1 ? (gb * 1000).toFixed(0) + " MB" : gb.toFixed(1) + " GB"
    }

    width: 480
    height: Math.min(520, contentCol.implicitHeight + 40)
    modal: true
    padding: 0

    background: Rectangle {
        color: Theme.surfaceBg
        radius: Theme.radiusLg
        border.width: 1
        border.color: Qt.rgba(1.0, 0.55, 0.0, 0.35)   // orange edge — a warning
    }

    contentItem: ColumnLayout {
        id: contentCol
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spaceLg
            spacing: Theme.spaceSm
            Text {
                text: "DIDN'T FIT ON THE ZUNE"
                font.pixelSize: 11
                font.bold: true
                font.letterSpacing: 1.5
                color: Theme.orange
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "✕"
                font.pixelSize: 14
                color: Theme.textSecondary
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.close()
                }
            }
        }

        // The verdict — lead with what's LEFT after the sync, not two
        // numbers the reader has to subtract in their head.
        Text {
            readonly property double leftAfter:
                Math.max(0, sheet.freeGB - sheet.queuedGB)
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            text: "once the current queue syncs, the zune will only have ~"
                  + sheet.fmtGB(leftAfter) + " of its "
                  + sheet.fmtGB(sheet.freeGB) + " free space left — "
                  + "not enough for "
                  + (sheet.rejectedItems.length === 1
                     ? "this:" : "these " + sheet.rejectedItems.length + " items:")
            font.pixelSize: 12
            font.weight: Font.Light
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        Text {
            visible: sheet.addedCount > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceXxs
            text: "(" + sheet.addedCount + " item"
                  + (sheet.addedCount === 1 ? "" : "s")
                  + " from this add did fit and got queued)"
            font.pixelSize: 11
            font.weight: Font.Light
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }

        // What was refused, with the size each would have cost
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spaceLg
            Layout.topMargin: Theme.spaceMd
            implicitHeight: Math.min(contentHeight, 300)
            clip: true
            model: sheet.rejectedItems
            boundsBehavior: Flickable.StopAtBounds

            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                spacing: Theme.spaceMd

                Text {
                    text: modelData.title
                    font.pixelSize: 12
                    font.weight: Font.Light
                    color: Theme.textPrimary
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Text {
                    text: "~" + sheet.fmtGB(modelData.estGB)
                    font.pixelSize: 11
                    font.family: "monospace"
                    color: Theme.textDim
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            text: "free up space (delete from the device) or trim the queue, "
                  + "then queue these again"
            font.pixelSize: 11
            color: Theme.textDim
            wrapMode: Text.WordWrap
        }

        DetailActionButton {
            Layout.margins: Theme.spaceLg
            Layout.fillWidth: true
            title: "got it"
            kind: "primary"
            onClicked: sheet.close()
        }
    }
}
