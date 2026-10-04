import QtQuick

// Boundary double only: all state transitions under test live in CustomizeSheet.
QtObject {
    property var artRequests: []
    property var identityRequests: []
    property var applications: []
    property var albumArtworkStatus: ({})
    property var artistArtworkStatus: ({})
    property var artworkRetries: []
    function artKey(artist, album) { return artist + "/" + album }
    function retryArtwork(kind, context) { artworkRetries = artworkRetries.concat([{kind, context}]) }

    signal customizeArtReady(string requestId, var urls, string error)
    signal customizeIdentityReady(string requestId, var results, string error)
    signal customizationFinished(string requestId, bool success, string error)

    function customizeContext(kind, context) { return context }

    function requestCustomizeArt(requestId, kind, source, context) {
        artRequests = artRequests.concat([{ requestId, kind, source, context }])
    }

    function searchCustomizeIdentity(requestId, query, tv) {
        identityRequests = identityRequests.concat([{ requestId, query, tv }])
    }
    function searchCustomizeMusicIdentity(requestId, kind, query, provider, artistHint) {
        identityRequests = identityRequests.concat([{requestId, kind, query, provider, artistHint}])
    }

    function applyCustomization(requestId, kind, context, draft) {
        applications = applications.concat([{
            requestId, kind,
            context: JSON.parse(JSON.stringify(context)),
            draft: JSON.parse(JSON.stringify(draft))
        }])
    }

    function clear() {
        artRequests = []
        identityRequests = []
        applications = []
        albumArtworkStatus = ({})
        artistArtworkStatus = ({})
        artworkRetries = []
    }
}
