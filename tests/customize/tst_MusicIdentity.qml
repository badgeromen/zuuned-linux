import QtQuick
import QtTest
import Zuuned

Item {
    width: 1440; height: 1000
    RecordingService { id: backend }
    CustomizeSheet { id: sheet; service: backend }
    TestCase {
        name: "MusicIdentityDraft"
        when: windowShown
        function cleanup() {
            if (sheet.busy) backend.customizationFinished(sheet.applyRequest, false, "fixture cleanup")
            sheet.discard()
            backend.clear()
        }
        function open(kind) {
            sheet.openFor({kind, name: "Christopher Larkin", artist: "Original artist",
                          album: "Original album", title: kind === "album" ? "Original album" : "Christopher Larkin"})
            tryCompare(sheet, "visible", true)
            sheet.idTabMode = "match"
            sheet.showTab("ident")
        }
        function test_albumMatchSeedsExactArtworkAndStagesApply() {
            open("album")
            compare(backend.identityRequests[0].kind, "album")
            compare(backend.identityRequests[0].artistHint, "Original artist")
            const result = {provider:"musicbrainz", providerId:"album-id", mbid:"album-id",
                            title:"New album", artist:"New owner", year:"2012", genre:"Alternative"}
            sheet.selectMatch(result)
            compare(sheet.previewTitle, "New album")
            compare(sheet.identityFields.albumartist, "New owner")
            compare(backend.applications.length, 0)
            sheet.showTab("art")
            const request = backend.artRequests[backend.artRequests.length - 1]
            compare(request.context.album, "New album")
            compare(request.context.musicIdentity.mbid, "album-id")
            sheet.applyDraft()
            compare(backend.applications[0].draft.musicIdentity.mbid, "album-id")
        }
        function test_artworkOnlyMatchDoesNotChangeIdentity() {
            open("album")
            sheet.chooseArtworkMatch()
            sheet.selectMatch({provider:"musicbrainz", providerId:"borrowed", mbid:"borrowed",
                               title:"Another album", artist:"Another artist"})
            compare(sheet.identityMode, "keep")
            compare(sheet.previewTitle, "Original album")
            compare(sheet.identityFields.albumartist, "Original artist")
            compare(sheet.tab, "art")
            compare(backend.applications.length, 0)
            sheet.applyDraft()
            const draft = backend.applications[0].draft
            compare(draft.identityMode, "keep")
            compare(draft.musicIdentity, {})
            compare(draft.artworkMatch.providerId, "borrowed")
            compare(draft.artworkMatch.scope, "general")
        }
        function test_generalCoverPreviewIsStagedAndCancelDiscards() {
            open("album")
            const cover = "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><rect width="64" height="64" fill="#d83070"/></svg>')
            backend.albumArtworkStatus = {"Original artist/Original album": {
                status:"generalCoverAvailable", imageUrl:cover,
                identity:{provider:"musicbrainz", providerId:"group"}}}
            compare(sheet.artworkStatusText, "General album cover available")
            sheet.stageGeneralCover()
            compare(sheet.selectedArt, cover)
            compare(sheet.identityMode, "keep")
            compare(sheet.selectedArtworkMatch.scope, "general")
            compare(backend.applications.length, 0)
            sheet.discard()
            tryCompare(sheet, "selectedArtworkMatch", null)
            compare(backend.applications.length, 0)
        }
        function test_portraitMatchAndStatusRemainSeparateFromMetadata() {
            open("artist")
            backend.artistArtworkStatus = {"christopher larkin": {status:"noArtwork"}}
            compare(sheet.artworkStatusText, "Artist identified, no portrait available")
            sheet.chooseArtworkMatch()
            sheet.selectMatch({provider:"deezer", providerId:"42", title:"Different spelling"})
            compare(sheet.previewTitle, "Christopher Larkin")
            compare(sheet.selectedArtworkMatch.scope, "artist")
            sheet.applyDraft()
            compare(backend.applications[0].draft.identityMode, "keep")
            compare(backend.applications[0].draft.artworkMatch.providerId, "42")
        }
        function test_editionChoicesRemainDistinctWithinOneGroup() {
            open("album")
            sheet.chooseArtworkMatch()
            const deluxe = {provider:"musicbrainz", providerId:"group", releaseGroupId:"group",
                            releaseId:"deluxe", title:"Original album", disambiguation:"Deluxe"}
            const standard = Object.assign({}, deluxe, {releaseId:"standard", disambiguation:"Standard"})
            sheet.selectMatch(deluxe)
            verify(sheet.isSelectedMatch(deluxe))
            verify(!sheet.isSelectedMatch(standard))
            compare(sheet.selectedArtworkMatch.scope, "exact")
            compare(sheet.selectedArtworkMatch.artworkScope, "exactRelease")
            compare(sheet.previewTitle, "Original album")
        }
        function test_artworkStatus_data() {
            return [
                {tag:"local", status:"checkingLocal", text:"Checking local artwork"},
                {tag:"lookup", status:"lookingUp", text:"Looking up artwork"},
                {tag:"ambiguous", status:"needsMatch", text:"Needs a match"},
                {tag:"empty", status:"noArtwork", text:"No cover available"},
                {tag:"offline", status:"providerUnavailable", text:"Provider unavailable"},
                {tag:"general", status:"generalCoverAvailable", text:"General album cover available"},
                {tag:"ready", status:"ready", text:""}
            ]
        }
        function test_artworkStatus(data) {
            open("album")
            sheet.showTab("art")
            backend.albumArtworkStatus = {"Original artist/Original album": {status:data.status}}
            compare(sheet.artworkStatusText, data.text)
            const retry = findChild(sheet, "customizeRetryArtwork")
            verify(retry !== null)
            compare(retry.enabled, data.status !== "lookingUp" && data.status !== "checkingLocal")
            if (data.status === "providerUnavailable") {
                retry.clicked()
                compare(backend.artworkRetries.length, 1)
                compare(backend.artworkRetries[0].kind, "album")
                compare(backend.artworkRetries[0].context.album, "Original album")
            }
            compare(backend.applications.length, 0)
        }
        function test_sameNameArtistsAreSelectedByProviderId() {
            open("artist")
            const composer = {provider:"musicbrainz", providerId:"composer", mbid:"composer", title:"Christopher Larkin"}
            const actor = {provider:"musicbrainz", providerId:"actor", mbid:"actor", title:"Christopher Larkin"}
            sheet.selectMatch(composer)
            verify(sheet.isSelectedMatch(composer))
            verify(!sheet.isSelectedMatch(actor))
            sheet.showTab("art")
            compare(backend.artRequests[backend.artRequests.length - 1].context.musicIdentity.mbid, "composer")
            sheet.artQuery = "Someone else"
            sheet.loadArt()
            compare(backend.artRequests[backend.artRequests.length - 1].context.musicIdentity, {})
            sheet.discard()
            compare(backend.applications.length, 0)
        }
        function test_lateIdentityResponseCannotReplaceNewProviderSearch() {
            open("artist")
            const oldRequest = sheet.identityRequest
            sheet.identityProvider = "deezer"
            sheet.searchIdentity()
            backend.customizeIdentityReady(oldRequest, [{title:"Stale"}], "")
            compare(sheet.results.length, 0)
            backend.customizeIdentityReady(sheet.identityRequest, [{title:"Current"}], "")
            compare(sheet.results[0].title, "Current")
            compare(backend.applications.length, 0)
        }
        function test_manualChangeReleasesDatabaseIdButKeepsPickedPhoto() {
            open("artist")
            sheet.selectMatch({provider:"deezer", providerId:"42", title:"Christopher Larkin"})
            const portrait = "data:image/svg+xml," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><rect width="64" height="64" fill="#d83070"/></svg>')
            sheet.stageArt(portrait)
            sheet.setField("title", "My credit")
            compare(sheet.identityMode, "manual")
            compare(sheet.artMusicIdentity, {})
            compare(sheet.selectedArt, portrait)
            sheet.applyDraft()
            compare(backend.applications[0].draft.musicIdentity, {})
        }
    }
}
