import QtQuick
import QtQuick.Layouts
import QtTest
import Zuuned

Rectangle {
    id: scene
    width: 960; height: 980
    property bool captured: false
    color: Theme.bg
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXxxl
        spacing: Theme.spaceLg
        Text { text: "APPEARANCE / ARTWORK"; font.pixelSize: Theme.customizeLabelSize; color: Theme.textDim }
        GradientText { text: "YOUR ART. YOUR INK."; font.family: Theme.displayFamily; font.pixelSize: Theme.customizeTitleSize }
        ArtworkStyleSettings {
            id: settings
            sampleSource: artworkFixture.captureSource
        }
        Item { Layout.fillHeight: true }
    }
    TestCase {
        name: "ArtworkAppearanceCapture"
        when: windowShown
        function test_capture() {
            AppSettings.artworkStyle = "halftone"
            AppSettings.resetArtworkStyle()
            settings.fineTuning = true
            const preview = findChild(settings, "artworkTreatedSample")
            tryCompare(preview, "treated", true, 15000)
            tryCompare(preview, "printing", false, 15000)
            waitForRendering(scene)
            verify(scene.grabToImage(result => {
                scene.captured = result.saveToFile("/tmp/zuuned-artwork-appearance.png")
            }))
            tryCompare(scene, "captured", true, 5000)
        }
    }
}
