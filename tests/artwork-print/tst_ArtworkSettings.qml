import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Rectangle {
    id: host
    width: 800
    height: 880
    color: Theme.bg
    ArtworkStyleSettings {
        id: picker
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spaceXl
    }
    TestCase {
        name: "ArtworkSettings"
        when: windowShown
        function control(name) { return findChild(picker, name) }
        function selectStyle(style) {
            mouseClick(control("artworkStyle_" + style))
            compare(AppSettings.artworkStyle, style)
        }
        function tune() {
            mouseClick(control("artworkFineTune"))
            compare(picker.fineTuning, true)
        }
        function init() {
            for (const style of ["cleanInk", "halftone", "wornPrint"]) {
                AppSettings.artworkStyle = style
                AppSettings.resetArtworkStyle()
            }
            AppSettings.artworkStyle = "original"
            picker.fineTuning = false
            host.width = 800
            host.Window.window.width = 800
            wait(20)
        }
        function test_defaults_and_opt_in() {
            compare(AppSettings.artworkStyle, "original")
            verify(!control("artworkFineTune").visible)
            verify(!control("artworkTuningControls").visible)
            selectStyle("halftone")
            compare(picker.fineTuning, false)
            tune()
            verify(control("artworkHalftoneTexture").visible)
            verify(control("artworkHalftoneDotSize").visible)
            verify(!control("artworkCleanDetail").visible)
            verify(!control("artworkWornTexture").visible)
            compare(control("artworkHalftoneTexture").amount, 14)
            compare(control("artworkHalftoneDotSize").amount, 17)
        }
        function test_slider_keyboard_color_and_independent_reset() {
            selectStyle("halftone")
            tune()
            const strength = control("artworkHalftoneTextureSlider")
            strength.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(AppSettings.artworkHalftoneTexture, 15)
            const dots = control("artworkHalftoneDotSizeSlider")
            mouseClick(dots, dots.width * 0.8, dots.height / 2)
            verify(AppSettings.artworkHalftoneDotSize > 60)
            mouseClick(control("artworkHalftoneMonochrome"))
            compare(AppSettings.artworkHalftoneMonochrome, true)
            selectStyle("wornPrint")
            verify(!picker.fineTuning)
            tune()
            const wear = control("artworkWornTextureSlider")
            wear.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(AppSettings.artworkWornTexture, 15)
            selectStyle("halftone")
            tune()
            compare(AppSettings.artworkHalftoneTexture, 15)
            verify(AppSettings.artworkHalftoneMonochrome)
            mouseClick(control("artworkResetLook"))
            compare(AppSettings.artworkHalftoneTexture, 14)
            compare(AppSettings.artworkHalftoneDotSize, 17)
            compare(AppSettings.artworkHalftoneMonochrome, false)
            compare(AppSettings.artworkWornTexture, 15)
            selectStyle("cleanInk")
            tune()
            verify(control("artworkCleanDetail").visible)
            compare(control("artworkCleanDetail").amount, 100)
            verify(!control("artworkHalftoneTexture").visible)
            verify(!control("artworkWornTexture").visible)
        }
        function test_narrow_layout_and_source_pair() {
            host.width = 380
            host.Window.window.width = 380
            selectStyle("wornPrint")
            tune()
            wait(30)
            const last = control("artworkStyle_wornPrint")
            verify(last.x + last.width <= picker.width)
            const sample = control("artworkTreatedSample")
            verify(sample.width > 0 && sample.height > 0)
            verify(Math.abs(sample.width - control("artworkOriginalSample").width) <= 1)
            compare(sample.source, control("artworkOriginalSample").source)
            verify(control("artworkWornTextureSlider").width <= picker.width)
            verify(control("artworkResetLook").visible)
        }
    }
}
