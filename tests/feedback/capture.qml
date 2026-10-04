import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtTest
import Zuuned

Rectangle {
    id: host
    width: 940
    height: 640
    color: Theme.bg
    Text {
        x: Theme.spaceHuge
        y: Theme.spaceHuge
        text: "music"
        font.family: Theme.displayFamily
        font.pixelSize: 64
        color: Theme.pink
    }
    Text {
        x: Theme.spaceHuge
        y: 140
        text: "artists     albums     songs"
        font.pixelSize: 22
        font.weight: Font.Light
        color: Theme.textSecondary
    }
    Row {
        x: Theme.spaceHuge
        y: 210
        spacing: Theme.spaceXl
        Repeater {
            model: ["After the Last Train", "Midnight Signal", "Soft Static"]
            Column {
                spacing: Theme.spaceMd
                Image {
                    width: 190; height: 190
                    fillMode: Image.PreserveAspectCrop
                    source: Qt.resolvedUrl("../../mockups/customize/assets/after-the-last-train.png")
                    opacity: index === 0 ? 1 : 0.35
                }
                Text { text: modelData; font.pixelSize: 15; color: Theme.textPrimary }
                Text { text: "Soft Static"; font.pixelSize: 12; color: Theme.textSecondary }
            }
        }
    }
    ToastHost {
        id: notice
        parent: host.Overlay.overlay ?? host
        z: Theme.toastOverlayZ
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spaceLg
        anchors.horizontalCenter: parent.horizontalCenter
    }
    TestCase {
        name: "FeedbackCapture"
        when: windowShown
        function capture(name, width, text, action) {
            host.Window.window.width = width
            host.width = width
            notice.show(text, action)
            wait(300)
            const snapshot = grabImage(host.Window.window.contentItem)
            verify(snapshot.width > 0)
            const file = "/tmp/zuuned-feedback-" + name + ".png"
            snapshot.save(file)
            console.log("Saved " + file)
        }
        function test_capture() {
            capture("wide", 940, "added 12 tracks to your library", "View")
            capture("long", 940, "Saved After the Last Train — the expanded late-night edition, including "
                    + "the acoustic sessions and live recordings — to your library.", "View")
            capture("narrow", 360, "queued After the Last Train and 12 tracks for your Zune", "View")
            notice.dismiss()
        }
    }
}
