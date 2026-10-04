import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Zuuned

// Post-sync summary modal: elapsed time + per-type counts. Opened by
// Main.qml on SyncEngine.syncSummary (completed syncs only).
Popup {
    id: sheet

    property double elapsedMs: 0
    property int tracks: 0
    property int videos: 0
    property int photos: 0
    property int skipped: 0
    property int failed: 0

    function openWith(elapsed, t, v, p, s, f) {
        elapsedMs = elapsed
        tracks = t; videos = v; photos = p; skipped = s; failed = f
        open()
    }
    function fmtElapsed(ms) {
        const total = Math.round(ms / 1000)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        if (h > 0) return h + "h " + m + "m " + s + "s"
        if (m > 0) return m + "m " + s + "s"
        return s + "s"
    }

    anchors.centerIn: Overlay.overlay
    width: 380
    modal: true
    background: Rectangle {
        color: Theme.surfaceBg
        border.width: 1
        border.color: Theme.glassBorder
        radius: Theme.radiusMd
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceMd

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceSm
            text: "sync completed"
            font.family: Theme.displayFamily
            font.pixelSize: 26
            color: Theme.textPrimary
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "in " + sheet.fmtElapsed(sheet.elapsedMs)
            font.pixelSize: 14
            color: Theme.pink
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.glassBorder
        }

        // Per-type rows — only types that took part show up
        GridLayout {
            Layout.alignment: Qt.AlignHCenter
            columns: 2
            columnSpacing: Theme.spaceLg
            rowSpacing: 6

            component CountRow: RowLayout {
                property string glyph: ""
                property string label: ""
                property int count: 0
                visible: count > 0
                spacing: Theme.spaceSm
                Layout.columnSpan: 2
                Text { text: glyph; font.pixelSize: 16 }
                Text {
                    text: count + " " + label + (count === 1 ? "" : "s")
                    font.pixelSize: 15
                    color: Theme.textPrimary
                }
            }
            CountRow { glyph: "🎵"; label: "song"; count: sheet.tracks }
            CountRow { glyph: "🎬"; label: "video"; count: sheet.videos }
            CountRow { glyph: "🖼"; label: "photo"; count: sheet.photos }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: sheet.skipped > 0
            text: sheet.skipped + " already on the zune (skipped)"
            font.pixelSize: 12
            color: Theme.textDim
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: sheet.failed > 0
            text: sheet.failed + " failed — still in the queue for retry"
            font.pixelSize: 12
            color: Theme.warning
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: sheet.tracks + sheet.videos + sheet.photos === 0
            text: "nothing new to send"
            font.pixelSize: 13
            color: Theme.textDim
        }

        DetailActionButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.spaceSm
            title: "Nice"
            kind: "accent"
            onClicked: sheet.close()
        }
    }
}
