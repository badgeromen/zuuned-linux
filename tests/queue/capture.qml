import QtQuick
import QtQuick.Layouts
import QtTest
import Zuuned

Rectangle {
    id: scene
    width: 576
    height: 378
    color: Theme.bg

    component PreviewEntry: QueueEntryView {
        property int season: 1
        entryId: "season-" + season
        type: "video"
        title: "S02E01 - It Came from the Nightosphere"
        artist: ""
        albumartist: ""
        album: ""
        filepath: ""
        status: "pending"
        statusNote: ""
        progress: 0
        posterPath: ""
        series: "Adventure Time"
        groupKey: "season-" + season
        groupInfo: ({kind:"series", title:"Adventure Time", subtitle:"Season " + season,
                     count:26, doneCount:0, failedCount:0, firstEntryId:entryId})
        onToggleRequested: expanded = !expanded
    }
    component PreviewPanel: Rectangle {
        property string sizeLabel: ""
        height: 338
        color: Theme.surfaceBg
        radius: Theme.radiusLg
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spaceLg
            spacing: Theme.spaceSm
            Text {
                text: "QUEUE"
                font.pixelSize: Theme.customizeCaptionSize
                font.letterSpacing: 2
                color: Theme.textSecondary
            }
            PreviewEntry { season: 1 }
            PreviewEntry { season: 2; expanded: true }
            PreviewEntry { season: 3 }
            Item { Layout.fillHeight: true }
            Text {
                text: parent.parent.sizeLabel
                font.pixelSize: Theme.customizeCaptionSize
                color: Theme.textDim
            }
        }
    }
    Row {
        anchors.centerIn: parent
        spacing: Theme.spaceLg
        PreviewPanel { width: 220; sizeLabel: "220 px panel" }
        PreviewPanel { width: 300; sizeLabel: "300 px panel" }
    }
    TestCase {
        name: "QueueCapture"
        when: windowShown
        function test_capture() {
            waitForRendering(scene)
            wait(50)
            const snapshot = grabImage(scene)
            verify(snapshot.width > 0 && snapshot.height > 0)
            snapshot.save("/tmp/zuuned-queue-controls.png")
        }
    }
}
