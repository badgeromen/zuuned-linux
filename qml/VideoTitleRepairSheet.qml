import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

Popup {
    id: sheet

    // Kept injectable for native fixture tests; normal callers only open.
    property alias repair: controller
    property QtObject libraryService: null
    property QtObject deviceService: null
    property QtObject syncEngine: null
    property string resultText: ""
    signal completed(string message)

    function openForPreview() {
        resultText = ""
        repair.rebuildPreview()
        open()
    }

    width: Math.min(parent.width - Theme.spaceXxxl, Theme.customizeCompactAt)
    height: Math.min(parent.height - Theme.spaceXxxl, Theme.customizeHeight)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    modal: true
    focus: true
    padding: 0
    closePolicy: repair.busy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    VideoTitleRepair {
        id: controller
        library: sheet.libraryService
        device: sheet.deviceService
        sync: sheet.syncEngine
        onFinished: function(updated, failed, skipped) {
            sheet.resultText = updated + (updated === 1 ? " title saved" : " titles saved")
            if (failed || skipped)
                sheet.resultText += " · " + failed + " failed · " + skipped + " skipped"
            sheet.completed(sheet.resultText)
        }
    }

    background: Rectangle {
        color: Theme.surfaceBg
        radius: Theme.radiusLg
        border.width: Theme.hairline
        border.color: Theme.glassBorder
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceLg

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXxl
            Layout.leftMargin: Theme.spaceXxl
            Layout.rightMargin: Theme.spaceXxl
            spacing: Theme.spaceSm
            Text {
                text: "use library titles"
                font.pixelSize: Theme.customizeHeadingSize
                font.weight: Font.Light
                color: Theme.textPrimary
            }
            Text {
                Layout.fillWidth: true
                text: "Choose the titles to use on your Zune. Episodes match by show, season and episode; movies need one clear match."
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.customizeBodySize
                color: Theme.textSecondary
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXxl
            Layout.rightMargin: Theme.spaceXxl
            spacing: Theme.spaceMd
            Text {
                Layout.fillWidth: true
                text: repair.selectedCount + " selected · " + repair.unchangedCount + " already correct"
                font.pixelSize: Theme.customizeBodySize
                color: Theme.textMid
            }
            Text {
                text: repair.selectedCount > 0 ? "clear selection" : "select all"
                font.pixelSize: Theme.customizeBodySize
                color: Theme.pink
                opacity: repair.busy ? Theme.disabledOpacity : 1
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Theme.spaceXs
                    enabled: !repair.busy
                    cursorShape: Qt.PointingHandCursor
                    onClicked: repair.selectAll(repair.selectedCount === 0)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }

        ListView {
            id: titleList
            objectName: "videoTitleRepairList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXxl
            Layout.rightMargin: Theme.spaceXxl
            model: repair.rows
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            delegate: Item {
                id: titleRow
                required property var modelData
                width: ListView.view.width
                height: Math.max(Theme.customizeCompactArt, rowLayout.implicitHeight + Theme.spaceLg)

                RowLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceMd

                    CheckBox {
                        id: chooseTitle
                        objectName: "chooseVideoTitle_" + titleRow.modelData.itemId
                        checked: titleRow.modelData.selected
                        enabled: !repair.busy && titleRow.modelData.status === "pending"
                        padding: 0
                        implicitWidth: Theme.spaceXl
                        implicitHeight: Theme.spaceXl
                        Accessible.name: "Use " + titleRow.modelData.after
                        onClicked: repair.setSelected(titleRow.modelData.itemId, checked)
                        indicator: Rectangle {
                            implicitWidth: Theme.spaceLg
                            implicitHeight: Theme.spaceLg
                            anchors.centerIn: parent
                            radius: Theme.radiusSm
                            color: chooseTitle.checked ? Theme.pink : Theme.transparent
                            border.width: Theme.hairline
                            border.color: chooseTitle.checked ? Theme.pink : Theme.textSubtle
                            opacity: chooseTitle.enabled ? 1 : Theme.disabledOpacity
                            Text {
                                anchors.centerIn: parent
                                text: chooseTitle.checked ? "✓" : ""
                                color: Theme.bg
                                font.pixelSize: Theme.customizeCaptionSize
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceXxs
                        Text {
                            Layout.fillWidth: true
                            text: titleRow.modelData.before || "untitled"
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.customizeBodySize
                            color: Theme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "→ " + titleRow.modelData.after
                            elide: Text.ElideRight
                            font.pixelSize: Theme.customizeBodySize
                            color: Theme.textPrimary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: titleRow.modelData.error || titleRow.modelData.detail
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.customizeCaptionSize
                            color: titleRow.modelData.error ? Theme.warning : Theme.textSubtle
                        }
                    }
                    Text {
                        text: titleRow.modelData.status === "pending" ? "" : titleRow.modelData.status
                        font.pixelSize: Theme.customizeCaptionSize
                        color: titleRow.modelData.status === "saved" ? Theme.green : Theme.textSecondary
                    }
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.hairline
                    color: Theme.separator
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spaceXxl
                visible: repair.candidateCount === 0
                text: repair.blockedReason || "No title changes to review. Unmatched and ambiguous videos are left alone."
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.customizeBodySize
                color: Theme.textSecondary
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXxl
            Layout.rightMargin: Theme.spaceXxl
            spacing: Theme.spaceXs
            Text {
                Layout.fillWidth: true
                text: repair.unmatchedCount + " without a match · " + repair.ambiguousCount + " ambiguous"
                font.pixelSize: Theme.customizeCaptionSize
                color: Theme.textSubtle
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: repair.error || repair.blockedReason || sheet.resultText
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.customizeBodySize
                color: repair.error || repair.blockedReason ? Theme.warning : Theme.green
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceXxl
            Layout.rightMargin: Theme.spaceXxl
            Layout.bottomMargin: Theme.spaceXxl
            spacing: Theme.spaceMd
            DetailActionButton {
                Layout.fillWidth: false
                Layout.preferredWidth: Theme.customizeCompactArt
                title: "refresh"
                enabled: !repair.busy
                opacity: enabled ? 1 : Theme.disabledOpacity
                onClicked: { sheet.resultText = ""; repair.rebuildPreview() }
            }
            Item { Layout.fillWidth: true }
            DetailActionButton {
                Layout.fillWidth: false
                Layout.preferredWidth: Theme.customizeCompactArt
                title: "close"
                enabled: !repair.busy
                opacity: enabled ? 1 : Theme.disabledOpacity
                onClicked: sheet.close()
            }
            DetailActionButton {
                objectName: "applyVideoTitles"
                Layout.fillWidth: false
                Layout.preferredWidth: Theme.customizeThumbSize + Theme.spaceXxl
                title: repair.busy ? "saving…" : "apply selected"
                kind: "primary"
                enabled: repair.canApply
                opacity: enabled ? 1 : Theme.disabledOpacity
                onClicked: repair.applySelected()
            }
        }
    }
}
