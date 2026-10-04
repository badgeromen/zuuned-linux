import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Rectangle {
    id: host
    width: 1440
    height: 940
    color: Theme.bg
    RecordingService { id: backend }
    CustomizeSheet { id: sheet; service: backend }
    TestCase {
        name: "CustomizeCapture"
        when: windowShown

        function test_capture() {
            const assets = "../../mockups/customize/assets/"
            const albumArt = Qt.resolvedUrl(assets + "after-the-last-train.png").toString()
            const artistArt = Qt.resolvedUrl(assets + "artist.png").toString()
            const seriesArt = Qt.resolvedUrl(assets + "series.png").toString()
            const variants = [
                { file: "album", tab: "art", style: "disc", width: 1440, height: 940,
                  context: { kind: "album", title: "After the Last Train", album: "After the Last Train",
                    artist: "Soft Static", year: "2008", genre: "Alternative", poster: albumArt } },
                { file: "album-vinyl", tab: "art", style: "vinyl", width: 1440, height: 940,
                  context: { kind: "album", title: "After the Last Train", album: "After the Last Train",
                    artist: "Soft Static", year: "2008", genre: "Alternative", poster: albumArt } },
                { file: "identity", tab: "ident", width: 1440, height: 940,
                  context: { kind: "album", title: "After the Last Train", album: "After the Last Train",
                    artist: "Soft Static", year: "2008", genre: "Alternative", poster: albumArt } },
                { file: "artist", tab: "art", width: 1440, height: 940,
                  context: { kind: "artist", title: "Alex Mercer", name: "Alex Mercer", poster: artistArt } },
                { file: "series", tab: "ident", width: 1440, height: 940,
                  context: { kind: "series", ids: [1, 2], title: "Midnight Signal: Abridged", tmdbId: -1,
                    year: "2012", genres: "Anime", overview: "A fan edit. A different kind of midnight.",
                    poster: seriesArt } },
                { file: "compact", tab: "art", width: 390, height: 700,
                  context: { kind: "album", title: "After the Last Train", album: "After the Last Train",
                    artist: "Soft Static", year: "2008", poster: albumArt } }
            ]
            for (const variant of variants) {
                Prefs.playerStyle = variant.style || "disc"
                host.Window.window.width = variant.width
                host.Window.window.height = variant.height
                host.width = variant.width
                host.height = variant.height
                sheet.openFor(variant.context)
                backend.customizeArtReady(sheet.artRequest,
                    [albumArt, artistArt, seriesArt, albumArt, artistArt, seriesArt], "")
                sheet.showTab(variant.tab)
                const hero = findChild(sheet.contentItem, "customizeHero")
                tryCompare(hero, "status", Image.Ready)
                wait(350)
                const path = "/tmp/zuuned-sleeve-native-" + variant.file + ".png"
                const snapshot = grabImage(sheet.contentItem.parent)
                verify(snapshot.width > 0 && snapshot.height > 0)
                snapshot.save(path)
                console.log("Saved " + path)
                sheet.discard()
                tryCompare(sheet, "visible", false)
            }
        }
    }
}
