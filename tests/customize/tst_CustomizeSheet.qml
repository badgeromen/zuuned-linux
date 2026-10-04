import QtQuick
import QtQuick.Window
import QtTest
import Zuuned

Item {
    id: host
    width: 1440
    height: 1000

    RecordingService { id: backend }
    CustomizeSheet { id: sheet; service: backend }

    TestCase {
        id: tests
        name: "CustomizeSheet"
        when: windowShown

        function album() {
            return { kind: "album", title: "Original title", album: "Original title",
                artist: "Original artist", year: "2008", genre: "Alternative", poster: "" }
        }
        function movie() {
            return { kind: "movie", ids: [41], title: "Original film", year: "2009",
                genres: "Drama", overview: "Original overview", tmdbId: 10, poster: "" }
        }
        function cover(index) {
            return "data:image/svg+xml," + encodeURIComponent(
                '<svg xmlns="http://www.w3.org/2000/svg" width="240" height="240">'
                + '<rect width="240" height="240" fill="hsl(' + (index * 31) + ',60%,35%)"/>'
                + '<text x="20" y="60" fill="white" font-size="30">' + index + '</text></svg>')
        }
        function open(context) {
            sheet.openFor(context)
            tryCompare(sheet, "visible", true)
            wait(20)
        }
        function lastRequest(requests) {
            verify(requests.length > 0, "The production sheet issued a request")
            return requests[requests.length - 1]
        }
        function resize(width, height) {
            host.Window.window.width = width
            host.Window.window.height = height
            host.width = width
            host.height = height
            wait(30)
        }
        function labeledField(item, label) {
            if (item.label === label) return item
            const children = item.children || []
            for (let i = 0; i < children.length; ++i) {
                const found = labeledField(children[i], label)
                if (found) return found
            }
            return null
        }
        function cleanup() {
            if (sheet.busy && backend.applications.length > 0) {
                backend.customizationFinished(lastRequest(backend.applications).requestId,
                    false, "Test cleanup")
            }
            sheet.discard()
            tryCompare(sheet, "visible", false)
            backend.clear()
            Prefs.playerStyle = "disc"
            resize(1440, 1000)
        }

        function test_albumFollowsPlayerStyleWithinArtworkColumn() {
            Prefs.playerStyle = "disc"
            open(album())
            tryVerify(() => !!findChild(sheet.contentItem, "customizeDiscStyle"))
            const record = findChild(sheet.contentItem, "customizeSleeveRecord")
            const sleeve = findChild(sheet.contentItem, "customizeSleeve")
            const workspace = findChild(sheet.contentItem, "customizeWorkspace")
            for (const style of ["vinyl", "disc"]) {
                Prefs.playerStyle = style
                const name = style === "vinyl" ? "customizeVinylStyle" : "customizeDiscStyle"
                tryVerify(() => !!findChild(sheet.contentItem, name))
                compare(findChild(sheet.contentItem, name).player, null)
                const right = record.mapToItem(sheet.contentItem, record.width, 0).x
                verify(right <= sleeve.mapToItem(sheet.contentItem, sleeve.width, 0).x,
                       "The record stays inside the artwork column")
                verify(right + Theme.spaceMd < workspace.mapToItem(sheet.contentItem, 0, 0).x,
                       "The record leaves breathing room before the information fields")
            }
            compare(backend.applications.length, 0)
        }

        function test_browsingStagesWithoutWrites() {
            open(album())
            compare(sheet.tab, "art")
            sheet.stageArt(cover(1))
            sheet.setField("title", "My edition")
            sheet.tab = "ident"
            sheet.tab = "art"
            compare(sheet.selectedArt, cover(1))
            compare(sheet.identityFields.title, "My edition")
            compare(backend.applications.length, 0)
        }

        function test_cancelDiscardsBothDrafts() {
            const context = album()
            open(context)
            sheet.stageArt(cover(2))
            sheet.setField("title", "Cancelled title")
            sheet.discard()
            tryCompare(sheet, "visible", false)
            compare(backend.applications.length, 0)
            open(context)
            compare(sheet.selectedArt, "")
            compare(sheet.identityFields.title, "Original title")
            compare(sheet.identityMode, "keep")
        }

        function test_applyCommitsOneCombinedDraft() {
            open(album())
            sheet.stageArt(cover(3))
            sheet.setField("title", "My edition")
            sheet.applyDraft()
            compare(backend.applications.length, 1)
            verify(sheet.busy)
            verify(sheet.visible)
            const application = backend.applications[0]
            compare(application.kind, "album")
            compare(application.context.album, "Original title")
            compare(application.draft.identityMode, "manual")
            compare(application.draft.fields.album, "My edition")
            compare(application.draft.artUrl, cover(3))
            sheet.applyDraft()
            compare(backend.applications.length, 1, "Double Apply cannot submit twice")
            backend.customizationFinished(application.requestId, true, "")
            tryCompare(sheet, "visible", false)
        }

        function test_failedApplyKeepsEditableDraft() {
            open(album())
            sheet.stageArt(cover(4))
            sheet.applyDraft()
            const request = lastRequest(backend.applications)
            backend.customizationFinished("stale-apply", true, "")
            verify(sheet.busy)
            verify(sheet.visible)
            backend.customizationFinished(request.requestId, false, "Artwork could not be saved")
            tryCompare(sheet, "busy", false)
            verify(sheet.visible)
            compare(sheet.selectedArt, cover(4))
            compare(sheet.errorMessage, "Artwork could not be saved")
            sheet.applyDraft()
            compare(backend.applications.length, 2)
        }

        function test_resetIsStagedAndCancelable() {
            open(movie())
            sheet.stageArt(cover(5))
            sheet.setField("title", "Draft title")
            sheet.resetDraft()
            compare(backend.applications.length, 0)
            compare(sheet.identityMode, "reset")
            sheet.discard()
            open(movie())
            compare(sheet.identityMode, "keep")
            compare(sheet.identityFields.title, "Original film")
        }

        function test_identityMatchingDoesNotReplaceSelectedArt() {
            open(movie())
            sheet.stageArt(cover(6))
            sheet.selectMatch({ tmdbId: 82, title: "Different film", year: "2011", poster: cover(8) })
            compare(sheet.identityMode, "match")
            compare(sheet.selectedArt, cover(6))
            sheet.setField("title", "My cut")
            compare(sheet.identityMode, "manual")
            verify(!sheet.selectedMatch || !sheet.selectedMatch.tmdbId)
            compare(sheet.selectedArt, cover(6))
        }

        function test_previousItemArtworkCannotReplaceCurrentItem() {
            open(album())
            const previous = lastRequest(backend.artRequests)
            sheet.discard()
            open({ kind: "artist", name: "Someone else", title: "Someone else", poster: "" })
            const current = lastRequest(backend.artRequests)
            verify(previous.requestId !== current.requestId)
            backend.customizeArtReady(previous.requestId, [cover(7)], "")
            compare(sheet.artUrls.length, 0)
            backend.customizeArtReady(current.requestId, [cover(8)], "")
            tryCompare(sheet, "artLoading", false)
            compare(sheet.artUrls.length, 1)
            compare(String(sheet.artUrls[0]), cover(8))
        }

        function test_newMatchSeedsArtworkWithoutReplacingPick() {
            open(movie())
            const oldArt = lastRequest(backend.artRequests)
            sheet.stageArt(cover(6))
            sheet.showTab("ident")
            sheet.selectMatch({ tmdbId: 82, title: "Different film", year: "2011" })
            compare(sheet.artQuery, "Different film")
            compare(sheet.selectedArt, cover(6))
            backend.customizeArtReady(oldArt.requestId, [cover(7)], "")
            compare(sheet.artUrls.length, 0, "Old-title artwork cannot populate the new identity")
            sheet.showTab("art")
            const request = lastRequest(backend.artRequests)
            compare(request.context.title, "Different film")
            compare(request.context.tmdbId, 82)
            compare(request.context.tv, false)
            compare(backend.applications.length, 0)
        }

        function test_sameTitleMatchUsesChosenDatabaseIdAndType() {
            open(movie())
            sheet.showTab("ident")
            sheet.searchTV = true
            sheet.selectMatch({ tmdbId: 90, title: "Original film", year: "2014" })
            sheet.showTab("art")
            const request = lastRequest(backend.artRequests)
            compare(request.context.title, "Original film")
            compare(request.context.tmdbId, 90, "Use the selected match, even when its title is unchanged")
            compare(request.context.tv, true)
        }

        function test_manualIdentitySeedsArtworkButAllowsIndependentSearch() {
            open(album())
            sheet.showTab("ident")
            sheet.setField("title", "My edition")
            sheet.setField("albumartist", "My artist")
            compare(sheet.artQuery, "My edition")
            sheet.showTab("art")
            let request = lastRequest(backend.artRequests)
            compare(request.context.album, "My edition")
            compare(request.context.artist, "My artist")
            sheet.artQuery = "Borrow another cover"
            sheet.loadArt()
            request = lastRequest(backend.artRequests)
            compare(request.context.title, "Borrow another cover")
            compare(request.context.tmdbId, 0)
            compare(sheet.identityFields.title, "My edition")
            compare(backend.applications.length, 0)
        }

        function test_escapeDiscards() {
            open(album())
            sheet.stageArt(cover(9))
            keyClick(Qt.Key_Escape)
            tryCompare(sheet, "visible", false)
            compare(backend.applications.length, 0)
            open(album())
            compare(sheet.selectedArt, "")
        }

        function test_resetArtworkPreservesManualAlbumIdentity() {
            open(album())
            sheet.setField("title", "My liner notes")
            sheet.stageArt(cover(1))
            sheet.resetDraft()
            compare(sheet.identityFields.title, "My liner notes")
            compare(sheet.identityMode, "manual")
            verify(sheet.artReset)
            compare(sheet.selectedArt, "")
            sheet.stageArt(cover(2))
            verify(!sheet.artReset)
            compare(sheet.identityFields.title, "My liner notes")
        }

        function test_staleSearchResponsesAreIgnored() {
            open(movie())
            sheet.showTab("ident")
            const first = lastRequest(backend.identityRequests)
            sheet.identityQuery = "New query"
            sheet.searchIdentity()
            const second = lastRequest(backend.identityRequests)
            backend.customizeIdentityReady(first.requestId, [{ tmdbId: 1, title: "Old result" }], "")
            compare(sheet.results.length, 0)
            verify(sheet.searching)
            backend.customizeIdentityReady(second.requestId, [{ tmdbId: 2, title: "New result" }], "")
            compare(sheet.results.length, 1)
            compare(sheet.results[0].title, "New result")
            verify(!sheet.searching)
        }

        function test_sourceChangesInvalidateEarlierArtwork() {
            open(movie())
            const first = lastRequest(backend.artRequests)
            sheet.artSource = "fanart"
            sheet.loadArt()
            const second = lastRequest(backend.artRequests)
            backend.customizeArtReady(first.requestId, [cover(1)], "")
            compare(sheet.artUrls.length, 0)
            verify(sheet.artLoading)
            backend.customizeArtReady(second.requestId, [cover(2)], "")
            compare(sheet.artUrls[0], cover(2))
        }

        function test_providerSwitchRetainsResultsPositionAndPreviews() {
            open(movie())
            const urls = []
            for (let i = 0; i < 12; ++i) urls.push(cover(i))
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, urls, "")
            const strip = findChild(sheet.contentItem, "customizeCoverStrip")
            tryCompare(strip, "count", urls.length)
            wait(20)
            sheet.scrollCovers(1)
            strip.currentIndex = 3
            const position = strip.contentX
            verify(position > 0)
            compare(sheet.residentArtUrls.length, 5)
            sheet.stageArt(urls[3])
            sheet.artSource = "fanart"
            sheet.loadArt()
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [cover(20)], "")
            sheet.artSource = "tmdb"
            sheet.loadArt()
            compare(backend.artRequests.length, 2, "Returning to a provider does not repeat its lookup")
            verify(!sheet.artLoading)
            compare(sheet.artUrls.length, 12)
            tryCompare(strip, "currentIndex", 3)
            tryVerify(() => Math.abs(strip.contentX - position) < 1)
            compare(sheet.residentArtUrls.length, 6, "Keep the first five plus the other provider's preview")
            compare(sheet.selectedArt, urls[3])
            sheet.loadArt()
            compare(backend.artRequests.length, 2, "The active tab is also a no-op")
        }

        function test_providerSwitchJoinsPendingRequestAndCachesInactiveResponse() {
            open(movie())
            const tmdb = lastRequest(backend.artRequests)
            sheet.artSource = "fanart"
            sheet.loadArt()
            const fanart = lastRequest(backend.artRequests)
            sheet.artSource = "tmdb"
            sheet.loadArt()
            compare(sheet.artRequest, tmdb.requestId)
            compare(backend.artRequests.length, 2, "An in-flight request is reused")
            backend.customizeArtReady(fanart.requestId, [cover(2)], "")
            verify(sheet.artLoading)
            compare(sheet.artUrls.length, 0)
            backend.customizeArtReady(tmdb.requestId, [cover(1)], "")
            compare(sheet.artUrls[0], cover(1))
            sheet.artSource = "fanart"
            sheet.loadArt()
            compare(sheet.artUrls[0], cover(2), "A response received off-tab is ready immediately")
            verify(!sheet.artLoading)
            compare(backend.artRequests.length, 2)
        }

        function test_artCacheSeparatesSearchAndChosenIdentity() {
            open(movie())
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [cover(1)], "")
            sheet.artQuery = "Another title"
            sheet.loadArt()
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [cover(2)], "")
            sheet.artQuery = "Original film"
            sheet.loadArt()
            compare(backend.artRequests.length, 2)
            compare(sheet.artUrls[0], cover(1))
            sheet.selectMatch({ tmdbId: 90, title: "Original film", year: "2014" })
            compare(backend.artRequests.length, 3, "A same-title different identity must get its own art")
            compare(sheet.artUrls.length, 0)
            compare(lastRequest(backend.artRequests).context.tmdbId, 90)
        }

        function test_emptyResultsAreCachedAndErrorsRetryOnlyOnSearch() {
            open(movie())
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [], "")
            sheet.artSource = "fanart"
            sheet.loadArt()
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [], "Provider is busy")
            sheet.artSource = "tmdb"
            sheet.loadArt()
            verify(!sheet.artLoading)
            compare(sheet.artUrls.length, 0)
            sheet.artSource = "fanart"
            sheet.loadArt()
            compare(sheet.artError, "Provider is busy")
            compare(backend.artRequests.length, 2)
            sheet.loadArt(true)
            compare(backend.artRequests.length, 3)
            verify(sheet.artLoading)
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [cover(4)], "")
            compare(sheet.artError, "")
            compare(sheet.artUrls[0], cover(4))
        }

        function test_artCacheIsBoundedAndReleasedOnClose() {
            open(album())
            for (let i = 0; i < 15; ++i) {
                sheet.artQuery = "Search " + i
                sheet.loadArt()
                backend.customizeArtReady(lastRequest(backend.artRequests).requestId, [cover(i)], "")
            }
            compare(sheet.artCacheOrder.length, 12)
            compare(Object.keys(sheet.artCache).length, 12)
            compare(sheet.residentArtUrls.length, 12)
            sheet.discard()
            tryCompare(sheet, "visible", false)
            compare(sheet.artCacheOrder.length, 0)
            compare(sheet.residentArtUrls.length, 0)
        }

        function test_actualTitleKeyboardEditsDraft() {
            open(album())
            sheet.showTab("ident")
            const field = findChild(sheet.contentItem, "customizeTitleField")
            verify(field !== null)
            const input = findChild(field, "customizeFieldInput")
            input.forceActiveFocus()
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_Z)
            compare(sheet.identityFields.title.toLowerCase(), "z")
            compare(sheet.identityMode, "manual")
            compare(backend.applications.length, 0)
        }

        function test_focusedOverviewDoesNotPolluteNextItem() {
            open(movie())
            sheet.showTab("ident")
            sheet.idTabMode = "manual"
            wait(20)
            const story = labeledField(sheet.contentItem, "The story")
            verify(story !== null)
            const paragraph = findChild(story, "customizeFieldParagraph")
            verify(paragraph !== null)
            verify(paragraph.visible)
            paragraph.forceActiveFocus()
            const next = movie()
            next.title = "Second film"
            next.overview = "A completely different story"
            sheet.openFor(next)
            wait(20)
            compare(sheet.identityFields.title, "Second film")
            compare(sheet.identityFields.overview, "A completely different story")
            compare(sheet.identityMode, "keep")
            verify(!sheet.dirty)
            sheet.selectMatch({ tmdbId: 99, title: "Matched film", overview: "Matched story" })
            compare(sheet.identityMode, "match")
            compare(sheet.identityFields.overview, "Matched story")
            sheet.resetDraft()
            compare(sheet.identityMode, "reset")
        }

        function test_coverStripKeyboardAndScroll() {
            open(album())
            const urls = []
            for (let i = 0; i < 12; ++i) urls.push(cover(i))
            backend.customizeArtReady(lastRequest(backend.artRequests).requestId, urls, "")
            const strip = findChild(sheet.contentItem, "customizeCoverStrip")
            tryCompare(strip, "count", 12)
            verify(strip.contentWidth > strip.width)
            compare(strip.orientation, ListView.Horizontal)
            strip.currentIndex = 0
            strip.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(strip.currentIndex, 1)
            keyClick(Qt.Key_Return)
            compare(sheet.selectedArt, urls[1])
            const next = findChild(sheet.contentItem, "customizeCoversNext")
            verify(next.enabled)
            mouseClick(next)
            verify(strip.contentX > 0)
            sheet.showTab("ident")
            sheet.showTab("art")
            compare(sheet.selectedArt, urls[1])
            compare(backend.applications.length, 0)
        }

        function test_shapesAndWindowBounds_data() {
            const rows = []
            for (const kind of ["album", "artist", "movie", "series"]) {
                for (const size of [{ width: 1440, height: 1000 }, { width: 850, height: 700 },
                                   { width: 390, height: 700 }]) {
                    rows.push({ tag: kind + "-" + size.width, kind, width: size.width, height: size.height })
                }
            }
            return rows
        }

        function test_shapesAndWindowBounds(data) {
            resize(data.width, data.height)
            const context = data.kind === "album" || data.kind === "artist" ? album() : movie()
            context.kind = data.kind
            if (data.kind === "series") { context.tmdbId = -1; context.ids = [41, 42] }
            open(context)
            const hero = findChild(sheet.contentItem, "customizeHero")
            verify(hero !== null)
            verify(hero.width > 0)
            fuzzyCompare(hero.height / hero.width,
                data.kind === "movie" || data.kind === "series" ? 1.5 : 1, 0.01)
            if (data.kind === "artist") fuzzyCompare(hero.cornerRadius, hero.width / 2, 0.01)
            verify(sheet.width <= data.width && sheet.height <= data.height)
            const footer = findChild(sheet.contentItem, "customizeFooter")
            const artFooterY = footer.y
            sheet.showTab("ident")
            wait(30)
            fuzzyCompare(footer.y, artFooterY, 1)
            for (const name of ["customizeCancel", "customizeApply"]) {
                const button = findChild(sheet.contentItem, name)
                const point = button.mapToItem(sheet.contentItem, 0, 0)
                verify(point.x >= 0 && point.x + button.width <= sheet.width + 1,
                    name + " fits width at " + data.width)
                verify(point.y >= 0 && point.y + button.height <= sheet.height + 1,
                    name + " fits height at " + data.height)
            }
        }
    }
}
