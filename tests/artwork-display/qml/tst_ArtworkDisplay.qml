import QtQuick
import QtTest
import Qt5Compat.GraphicalEffects
import Zuuned

Item {
    id: scene
    width: 400; height: 430
    property bool showComposite: false
    property bool trackPrint: false
    property int rawFlashes: 0
    Connections {
        target: art
        function onTreatedChanged() { if (scene.trackPrint && !art.treated) ++scene.rawFlashes }
    }
    ArtworkImage { id: art; width: 160; height: 200; source: artworkFixture.first }
    Image { id: expected; x: 180; width: 160; height: 200; sourceSize: art.sourceSize; mipmap: true }
    ArtworkImage {
        id: hiddenSource
        width: 160; height: 200
        visible: false
        source: art.source
    }
    Rectangle { id: circle; width: 160; height: 200; radius: width / 2; visible: false }
    OpacityMask { id: masked; y: 210; width: 160; height: 200; source: hiddenSource; maskSource: circle; visible: !scene.showComposite }
    OpacityMask { id: expectedMasked; x: 180; y: 210; width: 160; height: 200; source: expected; maskSource: circle; visible: !scene.showComposite }
    Item {
        id: hiddenComposite
        width: 160; height: 200; visible: false
        ArtworkImage { id: compositeArt; anchors.fill: parent; source: art.source }
        Rectangle { x: 60; y: 140; width: 40; height: 20; color: "#d4367a" }
    }
    Item {
        id: referenceComposite
        width: 160; height: 200; visible: false
        Image { anchors.fill: parent; source: expected.source; mipmap: true }
        Rectangle { x: 60; y: 140; width: 40; height: 20; color: "#d4367a" }
    }
    OpacityMask { id: compositeMask; y: 210; width: 160; height: 200; source: hiddenComposite; maskSource: circle; visible: scene.showComposite }
    OpacityMask { id: referenceMask; x: 180; y: 210; width: 160; height: 200; source: referenceComposite; maskSource: circle; visible: scene.showComposite }

    TestCase {
        name: "ArtworkDisplay"
        when: windowShown
        function init() {
            scene.trackPrint = false
            scene.rawFlashes = 0
            scene.showComposite = false
            art.source = artworkFixture.first
            art.treatmentEnabled = true
            AppSettings.artworkStyle = "cleanInk"
            AppSettings.resetArtworkStyle()
            AppSettings.artworkStyle = "halftone"
            AppSettings.resetArtworkStyle()
            AppSettings.artworkStyle = "wornPrint"
            AppSettings.resetArtworkStyle()
            AppSettings.artworkStyle = "original"
            expected.source = artworkFixture.first
            tryCompare(art, "status", Image.Ready)
        }
        function cleanup() { verify(artworkFixture.originalsUnchanged()) }
        function ready() {
            tryCompare(art, "treated", true, 15000)
            tryCompare(art, "printing", false, 15000)
            tryCompare(expected, "status", Image.Ready)
            waitForRendering(art)
            waitForRendering(expected)
        }
        function test_originalRemainsRawAndReadable() {
            compare(art.source, artworkFixture.first)
            compare(art.displaySource, artworkFixture.first)
            verify(!art.treated)
            verify(!art.printing)
        }
        function test_actualPixelsMatchRenderer_data() {
            return [{tag: "clean", style: "cleanInk", enum: 1},
                    {tag: "halftone", style: "halftone", enum: 2},
                    {tag: "worn", style: "wornPrint", enum: 3}]
        }
        function test_actualPixelsMatchRenderer(data) {
            expected.source = artworkFixture.expected(data.enum, 100, 14, 17, false)
            AppSettings.artworkStyle = data.style
            ready()
            compare(art.source, artworkFixture.first, "The public source remains original")
            verify(art.displaySource !== art.source)
            verify(grabImage(art).equals(grabImage(expected)), "Displayed pixels match the native filter")
        }
        function test_newSourceCannotKeepPreviousPrint() {
            AppSettings.artworkStyle = "wornPrint"
            ready()
            const old = art.displaySource
            art.source = artworkFixture.second
            verify(art.displaySource !== old, "Changing item removes the previous item's print immediately")
            tryCompare(art, "treated", true, 15000)
            verify(art.displaySource !== old)
            compare(art.source, artworkFixture.second)
        }
        function test_originalReturnsImmediatelyDuringWork() {
            AppSettings.artworkStyle = "wornPrint"
            ready()
            AppSettings.artworkWornTexture = 91
            AppSettings.artworkStyle = "original"
            verify(!art.treated)
            compare(art.displaySource, artworkFixture.first)
            wait(500)
            verify(!art.treated, "Late work cannot replace Original")
        }
        function test_photoOptOut() {
            art.treatmentEnabled = false
            AppSettings.artworkStyle = "halftone"
            wait(250)
            verify(!art.treated)
            verify(!art.printing)
            compare(art.displaySource, artworkFixture.first)
        }
        function test_latestSliderSettingsWin() {
            AppSettings.artworkStyle = "halftone"
            ready()
            const current = art.displaySource
            scene.trackPrint = true
            AppSettings.artworkHalftoneTexture = 92
            compare(art.displaySource, current, "Keep the current print while adjusting the same art")
            AppSettings.artworkHalftoneDotSize = 80
            AppSettings.artworkHalftoneMonochrome = true
            AppSettings.artworkHalftoneTexture = 37
            expected.source = artworkFixture.expected(2, 100, 37, 80, true)
            tryVerify(() => art.displaySource !== current && !art.printing, 15000)
            ready()
            verify(grabImage(art).equals(grabImage(expected)))
            compare(scene.rawFlashes, 0, "No raw-art flash during rendering or PNG replacement")
        }
        function test_hiddenMaskSourceRenders() {
            if (GraphicsInfo.api === GraphicsInfo.Software) skip("Requires the OpenGL gate")
            AppSettings.artworkStyle = "cleanInk"
            expected.source = artworkFixture.expected(1, 100, 14, 17, false)
            ready()
            tryCompare(hiddenSource, "treated", true, 15000)
            waitForRendering(masked)
            waitForRendering(expectedMasked)
            const actual = grabImage(masked)
            const reference = grabImage(expectedMasked)
            compare(actual.width, 160)
            compare(reference.width, 160)
            if (!actual.equals(reference)) {
                actual.save("/tmp/zuuned-artwork-mask-actual.png")
                reference.save("/tmp/zuuned-artwork-mask-expected.png")
            }
            verify(actual.equals(reference), "Hidden artwork still supplies mask pixels")
        }
        function test_hiddenCompositePreservesArtAndOverlays() {
            if (GraphicsInfo.api === GraphicsInfo.Software) skip("Requires the OpenGL gate")
            scene.showComposite = true
            for (const style of ["cleanInk", "halftone", "wornPrint", "original"]) {
                AppSettings.artworkStyle = style
                expected.source = artworkFixture.expected(
                    ["original", "cleanInk", "halftone", "wornPrint"].indexOf(style), 100, 14, 17, false)
                tryCompare(compositeArt, "treated", style !== "original", 15000)
                tryCompare(compositeArt, "printing", false, 15000)
                tryCompare(expected, "status", Image.Ready)
                waitForRendering(compositeMask)
                waitForRendering(referenceMask)
                const actual = grabImage(compositeMask)
                const reference = grabImage(referenceMask)
                compare(actual.width, 160)
                compare(reference.width, 160)
                compare(actual.height, 200)
                if (!actual.equals(reference)) {
                    actual.save("/tmp/zuuned-artwork-composite-actual.png")
                    reference.save("/tmp/zuuned-artwork-composite-expected.png")
                }
                verify(actual.equals(reference), "Hidden composite preserves both art and overlays: " + style)
            }
        }
    }
}
