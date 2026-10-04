import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    spacing: Theme.spaceMd
    signal notification(string message)

    Text {
        Layout.fillWidth: true
        text: "Debug report"
        font.pixelSize: Theme.customizeHeadingSize
        font.weight: Font.Light
        color: Theme.textPrimary
    }
    Text {
        Layout.fillWidth: true
        text: "Something acting up? Save a report and tell us what happened."
        font.pixelSize: Theme.customizeBodySize
        color: Theme.textSecondary
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        text: DiagnosticsService.loggingAvailable
              ? "Recent activity is saved on your computer."
              : "Local logs are unavailable. You can still export system details."
        font.pixelSize: Theme.customizeCaptionSize
        color: DiagnosticsService.loggingAvailable ? Theme.textSecondary : Theme.warning
        wrapMode: Text.WordWrap
    }
    Text {
        Layout.fillWidth: true
        visible: !DiagnosticsService.loggingAvailable && text.length > 0
        text: DiagnosticsService.logError
        font.pixelSize: Theme.customizeCaptionSize
        color: Theme.warning
        wrapMode: Text.WrapAnywhere
    }

    component ReportButton: Button {
        id: button
        property bool primary: false
        padding: Theme.spaceSm
        leftPadding: Theme.spaceMd
        rightPadding: Theme.spaceMd
        font.pixelSize: Theme.customizeBodySize
        opacity: enabled ? 1 : Theme.disabledOpacity
        contentItem: Text {
            text: button.text
            font: button.font
            color: button.primary ? Theme.pink : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusMd
            color: button.hovered ? Theme.cardHover : Theme.glassBg
            border.width: Theme.hairline
            border.color: button.activeFocus ? Theme.pink
                : button.primary ? Theme.focusRing : Theme.glassBorder
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spaceSm
        ReportButton {
            objectName: "diagnosticsExportButton"
            primary: true
            text: DiagnosticsService.busy ? "saving report…" : "export debug report ↗"
            enabled: !DiagnosticsService.busy
            onClicked: {
                reportDialog.selectedFile = DiagnosticsService.suggestedReportUrl()
                reportDialog.open()
            }
        }
        ReportButton {
            text: "open logs"
            enabled: DiagnosticsService.loggingAvailable
            onClicked: DiagnosticsService.openLogFolder()
        }
        ReportButton {
            objectName: "diagnosticsOpenReportButton"
            text: "open report ↗"
            visible: DiagnosticsService.lastReportUrl.toString().length > 0
            onClicked: Qt.openUrlExternally(DiagnosticsService.lastReportUrl)
        }
    }
    Text {
        Layout.fillWidth: true
        text: "Reports include recent logs and system details. Common credentials and home paths are masked; media names may remain. Review before sharing."
        font.pixelSize: Theme.customizeCaptionSize
        color: Theme.textDim
        wrapMode: Text.WordWrap
    }
    FileDialog {
        id: reportDialog
        objectName: "diagnosticsSaveDialog"
        title: "Save a ZUUNED debug report"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: ["Debug reports (*.txt)"]
        onAccepted: DiagnosticsService.exportReport(selectedFile)
    }
    Connections {
        target: DiagnosticsService
        function onExportFinished(success, reportUrl, error) {
            root.notification(success ? "debug report saved — review it before sharing" : error)
        }
    }
}
