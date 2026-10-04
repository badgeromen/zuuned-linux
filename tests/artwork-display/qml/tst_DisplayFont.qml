import QtQuick
import QtTest
import Zuuned

TestCase {
    name: "DisplayFont"
    when: windowShown
    function test_bundledAlien() {
        const previous = AppSettings.headerFont
        try {
            AppSettings.headerFont = "specimenAlien"
            tryCompare(Theme.specimenAlien, "status", FontLoader.Ready)
            compare(Theme.displayFamily, "Specimen Alien")
            AppSettings.headerFont = "misdemeanor"
            compare(AppSettings.headerFont, "specimenAlien")
            compare(Theme.displayFamily, "Specimen Alien")
            AppSettings.headerFont = "permanentMarker"
            tryCompare(Theme.marker, "status", FontLoader.Ready)
            compare(Theme.displayFamily, Theme.marker.font.family)
        } finally {
            AppSettings.headerFont = previous
        }
    }
}
