import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zuuned

// Sleeve: artwork and identity share one draft. Browsing never writes to the
// library; the backend prepares and commits the complete draft on Apply.
Popup {
    id: sheet
    objectName: "customizeSheet"
    property var service: LibraryService
    property var current: ({})
    property string kind: "album"
    property string tab: "art"
    property string idTabMode: "manual"
    property string identityMode: "keep"
    property var identityFields: ({})
    property var originalFields: ({})
    property var selectedMatch: null
    property bool artworkMatchMode: false
    property var selectedArtworkMatch: null
    property string selectedArt: ""
    property bool artReset: false
    property string artSource: ""
    property var artUrls: []
    property var results: []
    property bool artLoading: false
    property bool searching: false
    property bool busy: false
    property string errorMessage: ""
    property string artError: ""
    property string searchError: ""
    property string artQuery: ""
    property string artIdentityTitle: ""
    property int artIdentityId: 0
    property bool artIdentityTV: false
    property var artMusicIdentity: ({})
    property string identityProvider: "musicbrainz"
    property bool artNeedsRefresh: false
    property string identityQuery: ""
    property bool searchTV: kind === "series"
    property string artRequest: ""
    // Session-local browsing state. Full result lists are cheap; keep the first
    // five decoded previews per search alive so switching sources doesn't flash.
    property var artCache: ({})
    property var artCacheOrder: []
    property string artActiveKey: ""
    property var residentArtUrls: []
    property string identityRequest: ""
    property string applyRequest: ""
    property int requestSerial: 0
    readonly property bool isMusic: kind === "album" || kind === "artist"
    readonly property bool artworkOnly: kind === "genre" || kind === "mixtape"
    readonly property bool squareArt: isMusic || kind === "genre"
    readonly property bool bulk: (current.ids || []).length > 1
    readonly property bool compact: width < Theme.customizeCompactAt
    readonly property bool dirty: identityMode !== "keep" || selectedArt !== "" || artReset || selectedArtworkMatch !== null
    readonly property var artworkDiscovery: {
        if (!isMusic) return ({})
        const states = kind === "album" ? service.albumArtworkStatus : service.artistArtworkStatus
        const key = kind === "album" && service.artKey
            ? service.artKey(current.artist || "", current.album || current.title || "")
            : String(current.name || current.title || "").toLowerCase()
        return (states || ({}))[key] || current.artworkDiscovery || ({})
    }
    readonly property string artworkStatusText: {
        switch (artworkDiscovery.status) {
        case "checkingLocal": return "Checking local artwork"
        case "lookingUp": return "Looking up artwork"
        case "needsMatch": return "Needs a match"
        case "noArtwork": return kind === "artist" ? "Artist identified, no portrait available" : "No cover available"
        case "providerUnavailable": return "Provider unavailable"
        case "generalCoverAvailable": return "General album cover available"
        default: return ""
        }
    }
    readonly property bool valid: identityMode !== "manual" || String(identityFields.title || "").trim().length > 0
    readonly property string previewTitle: String(identityFields.title || current.title || current.name || "Untitled")
    readonly property string previewArt: selectedArt || (artReset ? "" : String(current.poster || ""))
    readonly property string previewSub: kind === "artist" ? String(current.sub || "Artist in your collection")
        : artworkOnly ? String(current.sub || "A collection that’s yours.")
        : kind === "album" ? [identityFields.albumartist, identityFields.year].filter(v => !!v).join(" · ")
        : [identityFields.year, kind === "series" ? current.sub : identityFields.genres].filter(v => !!v).join(" · ")
    readonly property var artSources: kind === "album"
        ? [{label: "cover archive", value: "caa"}, {label: "fanart.tv", value: "fanart"}, {label: "your computer", value: "file"}]
        : kind === "artist"
          ? [{label: "fanart.tv", value: "fanart"}, {label: "deezer", value: "deezer"}, {label: "your computer", value: "file"}]
          : artworkOnly ? [{label: "your computer", value: "file"}]
          : [{label: "tmdb", value: "tmdb"}, {label: "fanart.tv", value: "fanart"},
             {label: "frame grab", value: "grab"}, {label: "your computer", value: "file"}]
    signal saved(string message)

    function token() {
        requestSerial++
        return Date.now().toString(36) + "-" + requestSerial + "-" + Math.random().toString(36).slice(2)
    }
    function openFor(ctx) {
        if (busy) return
        ctx = service.customizeContext(ctx.kind, ctx)
        current = Object.assign({}, ctx)
        kind = ctx.kind
        identityFields = {
            title: String(ctx.title || ctx.name || ctx.album || ""),
            albumartist: String(ctx.artist || ""),
            year: ctx.year ? String(ctx.year) : "",
            genre: String(ctx.genre || ""),
            genres: String(ctx.genres || ""),
            overview: String(ctx.overview || ""),
            type: ctx.type || (kind === "series" ? "tv" : "movie"),
            season: ctx.season ? String(ctx.season) : "",
            episode: ctx.episode ? String(ctx.episode) : "",
            episodeTitle: String(ctx.episodeTitle || "")
        }
        originalFields = Object.assign({}, identityFields)
        identityMode = "keep"
        selectedMatch = null
        artworkMatchMode = false
        selectedArtworkMatch = null
        selectedArt = ""
        artReset = false
        errorMessage = ""
        artError = ""
        searchError = ""
        artUrls = []
        results = []
        artLoading = false
        searching = false
        artRequest = ""
        clearArtCache()
        identityRequest = ""
        applyRequest = ""
        searchTV = kind === "series"
        tab = "art"
        idTabMode = isMusic || Number(ctx.tmdbId || 0) < 0 ? "manual" : "match"
        artSource = artSources[0].value
        artQuery = kind === "album" ? String(ctx.album || ctx.title || "") : identityFields.title
        artIdentityTitle = artQuery
        artIdentityId = Number(ctx.tmdbId || 0)
        artIdentityTV = identityFields.type === "tv"
        artMusicIdentity = ctx.artworkMatch && ctx.artworkMatch.provider
            ? ctx.artworkMatch : ctx.musicIdentity || ({})
        if (kind === "artist" && artMusicIdentity.provider === "deezer") artSource = "deezer"
        identityProvider = artMusicIdentity.provider === "deezer" ? "deezer" : "musicbrainz"
        artNeedsRefresh = false
        identityQuery = identityFields.title
        open()
        loadArt()
    }
    function setField(key, value) {
        if (busy) return
        const next = Object.assign({}, identityFields)
        next[key] = value
        identityFields = next
        selectedMatch = null
        selectedArtworkMatch = null
        identityMode = JSON.stringify(next) === JSON.stringify(originalFields) ? "keep" : "manual"
        if (key === "title" || key === "type" || key === "albumartist")
            syncArtworkIdentity()
        errorMessage = ""
    }
    function selectMatch(result) {
        if (busy) return
        if (artworkMatchMode && isMusic) {
            const scope = kind === "artist" ? "artist" : result.releaseId ? "exact" : "general"
            selectedArtworkMatch = Object.assign({}, result, {
                scope: scope,
                artworkScope: scope === "exact" ? "exactRelease" : scope === "general" ? "generalAlbum" : "artist",
                manual: true
            })
            artMusicIdentity = selectedArtworkMatch
            if (kind === "album" && result.releaseId) artSource = "caa"
            if (kind === "artist" && result.provider === "deezer") artSource = "deezer"
            artQuery = String(result.title || identityFields.title || "")
            artIdentityTitle = artQuery
            artNeedsRefresh = true
            showTab("art")
            return
        }
        selectedMatch = result
        selectedArtworkMatch = null
        identityMode = "match"
        identityFields = Object.assign({}, originalFields, {
            title: result.title, year: String(result.year || ""),
            albumartist: result.artist || originalFields.albumartist,
            genre: result.genre || originalFields.genre,
            overview: result.overview || "", type: searchTV ? "tv" : "movie"
        })
        syncArtworkIdentity()
        errorMessage = ""
    }
    function isSelectedMatch(result) {
        const chosen = artworkMatchMode ? selectedArtworkMatch || current.artworkMatch
            : selectedMatch || (identityMode === "keep" && isMusic ? current.musicIdentity : null)
        if (!chosen) return false
        if (artworkMatchMode && kind === "album")
            return chosen.provider === result.provider && chosen.providerId === result.providerId
                && String(chosen.releaseId || "") === String(result.releaseId || "")
        return isMusic ? chosen.provider === result.provider && chosen.providerId === result.providerId
                       : chosen.tmdbId === result.tmdbId
    }
    function syncArtworkIdentity() {
        // Start artwork browsing at the newly chosen identity. The user can
        // still search a different title and keep any already selected cover.
        artQuery = String(identityFields.title || "")
        artIdentityTitle = artQuery
        artIdentityId = selectedMatch ? Number(selectedMatch.tmdbId || 0)
                      : identityMode === "keep" ? Number(current.tmdbId || 0) : 0
        artIdentityTV = identityFields.type === "tv"
        artMusicIdentity = selectedMatch || (identityMode === "keep" ? current.musicIdentity || ({}) : ({}))
        saveArtPosition()
        artActiveKey = ""
        artRequest = ""
        artUrls = []
        artLoading = false
        artError = ""
        artNeedsRefresh = true
        if (visible && tab === "art") loadArt()
    }
    function stageArt(url) {
        if (busy || !url) return
        if (isMusic && artSource !== "file" && artQuery.trim() !== artIdentityTitle.trim())
            selectedArtworkMatch = null
        selectedArt = String(url)
        artReset = false
        errorMessage = ""
    }
    function chooseArtworkMatch() {
        if (busy || identityMode !== "keep") return
        artworkMatchMode = true
        idTabMode = "match"
        identityQuery = String(current.title || current.name || current.album || "")
        results = artworkDiscovery.candidates || []
        showTab("ident")
    }
    function stageGeneralCover() {
        if (busy || identityMode !== "keep" || !artworkDiscovery.imageUrl) return
        stageArt(artworkDiscovery.imageUrl)
        selectedArtworkMatch = Object.assign({}, artworkDiscovery.identity || ({}),
            {scope: "general", artworkScope: "generalAlbum", releaseId: "", manual: true})
    }
    function clearArtCache() {
        artCache = ({})
        artCacheOrder = []
        artActiveKey = ""
        residentArtUrls = []
    }
    function saveArtPosition() {
        const entry = artCache[artActiveKey]
        if (entry && !entry.loading) {
            entry.position = coverStrip.contentX
            entry.index = coverStrip.currentIndex
        }
    }
    function retainArtPreviews() {
        const urls = []
        for (const key of artCacheOrder) {
            for (const url of artCache[key].urls.slice(0, 5)) {
                if (urls.indexOf(String(url)) < 0) urls.push(String(url))
            }
        }
        residentArtUrls = urls
    }
    function showArtEntry(key, entry) {
        artActiveKey = key
        artRequest = entry.requestId
        artUrls = entry.urls
        artLoading = entry.loading
        artError = entry.error
        // Apply the model synchronously so a delayed restore cannot overwrite
        // a keyboard/scroll action made immediately after returning to a tab.
        coverStrip.forceLayout()
        coverStrip.currentIndex = Math.min(entry.index, coverStrip.count - 1)
        coverStrip.contentX = Math.max(0, Math.min(entry.position,
            coverStrip.contentWidth - coverStrip.width))
    }
    function loadArt(retryError) {
        artNeedsRefresh = false
        saveArtPosition()
        if (artSource === "file" || artworkOnly) {
            artActiveKey = ""; artRequest = ""
            artUrls = []; artError = ""; artLoading = false
            return
        }
        const ctx = Object.assign({}, current, {
            ids: current.ids || [], tv: artIdentityTV, title: artQuery.trim(),
            tmdbId: artQuery.trim() === artIdentityTitle.trim() ? artIdentityId : 0,
            musicIdentity: artQuery.trim() === artIdentityTitle.trim() ? artMusicIdentity : ({}),
            album: artQuery.trim(), artist: identityFields.albumartist || "", name: artQuery.trim()
        })
        const key = JSON.stringify([kind, artSource, ctx.title, ctx.artist,
                                   ctx.tmdbId, ctx.musicIdentity, ctx.tv, ctx.ids, ctx.filepath || ""])
        let entry = artCache[key]
        artCacheOrder = artCacheOrder.filter(value => value !== key).concat([key])
        if (entry && !(retryError && entry.error && !entry.loading)) {
            if (artActiveKey !== key) showArtEntry(key, entry)
            return
        }
        entry = { requestId: token(), urls: [], error: "", loading: true, position: 0, index: 0 }
        artCache[key] = entry
        // Bound retained image memory even during a long run of manual searches.
        while (artCacheOrder.length > 12) {
            const oldest = artCacheOrder[0]
            artCacheOrder = artCacheOrder.slice(1)
            delete artCache[oldest]
        }
        retainArtPreviews()
        showArtEntry(key, entry)
        service.requestCustomizeArt(entry.requestId, kind, artSource, ctx)
    }
    function searchIdentity() {
        identityRequest = token()
        results = []
        searchError = ""
        if (!identityQuery.trim()) { searching = false; return }
        searching = true
        if (isMusic)
            service.searchCustomizeMusicIdentity(identityRequest, kind, identityQuery.trim(), identityProvider,
                                                 kind === "album" ? identityFields.albumartist || "" : "")
        else service.searchCustomizeIdentity(identityRequest, identityQuery.trim(), searchTV)
    }
    function showTab(value) {
        if (artworkOnly) value = "art"
        tab = value
        if (value === "ident" && idTabMode === "match" && results.length === 0 && !searching)
            searchIdentity()
        if (value === "art" && artNeedsRefresh)
            loadArt()
    }
    function resetDraft() {
        if (busy) return
        selectedArt = ""
        artReset = true
        if (!isMusic && !artworkOnly) {
            identityMode = "reset"
            selectedMatch = null
            identityFields = Object.assign({}, originalFields)
            syncArtworkIdentity()
        }
        errorMessage = ""
    }
    function discard() {
        if (busy) return
        close()
    }
    function applyDraft() {
        if (!dirty || busy || !valid) return
        errorMessage = ""
        busy = true
        applyRequest = token()
        const fields = Object.assign({}, identityFields, {
            title: String(identityFields.title || "").trim(),
            album: String(identityFields.title || "").trim(),
            artist: String(identityFields.title || "").trim(),
            albumartist: String(identityFields.albumartist || "").trim()
        })
        service.applyCustomization(applyRequest, kind, current, {
            identityMode: identityMode, fields: fields,
            tmdbId: selectedMatch ? selectedMatch.tmdbId : 0,
            musicIdentity: isMusic && selectedMatch ? selectedMatch : ({}),
            artworkMatch: selectedArtworkMatch,
            tv: identityFields.type === "tv", artUrl: selectedArt, resetArt: artReset
        })
    }
    function scrollCovers(direction) {
        coverStrip.contentX = Math.max(0, Math.min(coverStrip.contentWidth - coverStrip.width,
                                    coverStrip.contentX + direction * (Theme.customizeThumbSize + Theme.spaceLg) * 2))
    }
    onClosed: {
        artRequest = ""; identityRequest = ""
        clearArtCache()
        artLoading = false; searching = false
        selectedArt = ""; selectedMatch = null; artReset = false
        selectedArtworkMatch = null; artworkMatchMode = false
        identityFields = Object.assign({}, originalFields)
        identityMode = "keep"
        artDialog.close()
    }
    Connections {
        target: sheet.service
        function onCustomizeArtReady(requestId, urls, error) {
            if (!sheet.visible) return
            for (const key of sheet.artCacheOrder) {
                const entry = sheet.artCache[key]
                if (entry.requestId !== requestId) continue
                entry.loading = false
                entry.urls = urls
                entry.error = error
                sheet.retainArtPreviews()
                if (key === sheet.artActiveKey) sheet.showArtEntry(key, entry)
                return
            }
        }
        function onCustomizeIdentityReady(requestId, matches, error) {
            if (!sheet.visible || requestId !== sheet.identityRequest) return
            sheet.searching = false
            sheet.results = matches
            sheet.searchError = error
        }
        function onCustomizationFinished(requestId, success, error) {
            if (requestId !== sheet.applyRequest) return
            sheet.busy = false
            if (success) {
                sheet.saved(error || "saved — “" + sheet.previewTitle + "”")
                sheet.close()
            } else sheet.errorMessage = error || "Couldn't save your changes. Your draft is still here."
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.customizeWidth, Math.max(0, parent.width - Theme.spaceHuge))
    height: Math.min(Theme.customizeHeight, Math.max(0, parent.height - Theme.spaceHuge))
    modal: true
    focus: true
    padding: 0
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Qt.alpha(Theme.bg, 0.7) }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionBase }
            NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.motionBase; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionFast } }
    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.customizeSurface
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.customizeSurface }
            GradientStop { position: 1; color: Theme.surfaceBg }
        }
        border.width: 1
        border.color: Qt.alpha(Theme.pink, 0.28)
        Item {
            visible: false
            Repeater {
                model: sheet.residentArtUrls
                Image {
                    required property var modelData
                    source: modelData
                    asynchronous: true
                    cache: true
                    mipmap: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(Theme.customizeThumbSize * 2)
                    sourceSize.height: Math.ceil(Theme.customizeThumbSize * 2 * (sheet.squareArt ? 1 : 1.5))
                }
            }
        }
    }

    component QuietButton: Button {
        id: control
        property bool accent: false
        property bool chosen: false
        property bool filled: false
        property bool tabLike: false
        implicitHeight: Theme.spaceHuge - Theme.spaceSm
        implicitWidth: contentItem.implicitWidth + Theme.spaceLg
        padding: Theme.spaceSm
        font.pixelSize: Theme.customizeCaptionSize
        hoverEnabled: true
        opacity: enabled ? 1 : 0.4
        background: Rectangle {
            radius: Theme.radiusSm
            color: control.filled ? (control.hovered ? Theme.activePink : Theme.pink)
                 : control.tabLike ? Theme.transparent
                 : control.hovered ? Theme.cardHover : Theme.transparent
            border.width: control.activeFocus && !control.tabLike ? Theme.spaceXxxs : 0
            border.color: Theme.focusRing
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: control.tabLike && control.visualFocus ? Theme.spaceXxxs : 1
                color: control.chosen ? Theme.pink : Theme.textSecondary
                visible: control.chosen || (control.tabLike && control.visualFocus)
            }
        }
        contentItem: Text {
            text: control.text
            color: control.filled ? Theme.textPrimary
                 : control.chosen || control.accent ? Theme.activePink
                 : control.hovered ? Theme.textPrimary : Theme.textSecondary
            font: control.font
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }
    component SearchField: TextField {
        color: Theme.textPrimary
        placeholderTextColor: Theme.textSubtle
        selectionColor: Theme.pink
        selectedTextColor: Theme.textPrimary
        font.pixelSize: Theme.customizeBodySize
        padding: Theme.spaceSm
        background: Rectangle { color: Theme.cardBg; radius: Theme.radiusSm }
    }
    component SmallLabel: Text {
        font.pixelSize: Theme.customizeLabelSize
        font.letterSpacing: 1.6
        color: Theme.textSecondary
    }

    contentItem: ColumnLayout {
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: sheet.compact ? Theme.customizeHeader - Theme.spaceLg : Theme.customizeHeader
            Column {
                anchors.left: parent.left; anchors.leftMargin: Theme.spaceXxxl
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spaceSm
                SmallLabel { text: "YOUR COLLECTION / YOUR CALL" }
                GradientText {
                    text: "make it yours."
                    font.family: Theme.displayFamily
                    font.pixelSize: sheet.compact ? Theme.customizeTitleSize - Theme.spaceSm : Theme.customizeTitleSize
                    rotation: -3
                    transformOrigin: Item.Left
                }
            }
            QuietButton {
                objectName: "customizeClose"
                anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: Theme.spaceXl
                text: "×"; font.pixelSize: Theme.customizeHeadingSize
                enabled: !sheet.busy
                Accessible.name: "Cancel customization"
                onClicked: sheet.discard()
            }
        }

        GridLayout {
            id: body
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.leftMargin: Theme.spaceXxxl; Layout.rightMargin: Theme.spaceXxxl
            Layout.bottomMargin: Theme.spaceXl
            columns: sheet.compact ? 1 : 2
            columnSpacing: Theme.spaceXxxl; rowSpacing: Theme.spaceLg
            Item {
                id: sleeve
                objectName: "customizeSleeve"
                Layout.preferredWidth: sheet.compact ? 0 : Math.min(Theme.customizeArtSize + Theme.spaceXl, body.width * 0.35)
                Layout.fillWidth: sheet.compact
                Layout.fillHeight: !sheet.compact
                Layout.preferredHeight: sheet.compact ? Theme.customizeCompactArt + Theme.spaceXxl : 0
                SmallLabel {
                    id: kindCaption
                    anchors.top: parent.top; anchors.left: parent.left
                    text: sheet.kind.toUpperCase()
                }
                Text {
                    anchors.right: parent.right; anchors.top: parent.top
                    text: sheet.artReset || sheet.identityMode === "reset" ? "automatic on apply"
                        : sheet.selectedArt ? "your artwork"
                        : sheet.identityMode === "manual" || Number(sheet.current.tmdbId || 0) < 0 ? "manual identity" : ""
                    color: Theme.activePink; font.pixelSize: Theme.customizeLabelSize
                }
                Item {
                    id: artAssembly
                    anchors.top: kindCaption.bottom
                    anchors.topMargin: sheet.compact ? Theme.spaceMd : Theme.spaceXxl
                    x: sheet.compact ? 0 : Theme.spaceSm
                    width: sheet.compact ? Theme.customizeCompactArt
                        : Math.min(sheet.kind === "album"
                                   ? (sleeve.width - x) / (Theme.customizeRecordInset + Theme.customizeRecordScale)
                                   : sleeve.width - Theme.spaceXl, Theme.customizeArtSize,
                                   Math.max(Theme.customizeCompactArt, sleeve.height - Theme.spaceHuge * 2 - Theme.spaceXxxl))
                    height: width
                    Item {
                        id: sleeveRecord
                        objectName: "customizeSleeveRecord"
                        visible: sheet.kind === "album" && !sheet.compact
                        x: parent.width * Theme.customizeRecordInset
                        y: (parent.height - height) / 2
                        width: parent.width * Theme.customizeRecordScale
                        height: width
                        // Use the same two looks as the player, with no playback
                        // connection. Keep the rim inside the artwork column.
                        clip: true
                        Loader {
                            anchors.fill: parent
                            active: sleeveRecord.visible
                            enabled: false
                            sourceComponent: Prefs.playerStyle === "vinyl" ? recordVinyl : recordDisc
                        }
                        Component {
                            id: recordVinyl
                            VinylRecordView {
                                objectName: "customizeVinylStyle"
                                player: null
                                size: sleeveRecord.width
                                posterSource: sheet.previewArt
                                dragSeekEnabled: false
                                isVisible: sheet.visible
                            }
                        }
                        Component {
                            id: recordDisc
                            SpinningDiscView {
                                objectName: "customizeDiscStyle"
                                player: null
                                diameter: sleeveRecord.width - Theme.spaceMd
                                artSource: sheet.previewArt
                            }
                        }
                    }
                    CustomizeArtwork {
                        id: hero
                        objectName: "customizeHero"
                        anchors.centerIn: parent
                        width: sheet.squareArt || sheet.kind === "mixtape" ? parent.width : parent.width * 0.67
                        height: sheet.kind === "mixtape" ? width / Theme.cassetteAspect : sheet.squareArt ? width : width * 1.5
                        source: sheet.previewArt; kind: sheet.kind
                        title: sheet.previewTitle
                        artUrls: sheet.current.artUrls || []
                        rotation: sheet.kind === "artist" ? 0 : -3
                        showShadow: !sheet.compact
                        hovered: heroHover.hovered
                        Behavior on rotation { NumberAnimation { duration: Theme.motionBase } }
                        HoverHandler { id: heroHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { enabled: !sheet.busy; onTapped: sheet.showTab("art") }
                    }
                    Text {
                        anchors.top: parent.bottom; anchors.right: parent.right
                        anchors.topMargin: Theme.spaceMd
                        visible: !sheet.compact
                        text: "your version ↗"
                        font.family: Theme.displayFamily
                        font.pixelSize: Theme.spaceLg
                        color: Theme.activePink
                        rotation: -5
                    }
                }
                Column {
                    x: sheet.compact ? Theme.customizeCompactArt + Theme.spaceXl : 0
                    y: sheet.compact ? Theme.spaceHuge : artAssembly.y + artAssembly.height + Theme.spaceHuge
                    width: sheet.compact ? sleeve.width - x : sleeve.width
                    spacing: Theme.spaceSm
                    Text {
                        width: parent.width
                        text: sheet.previewTitle
                        font.pixelSize: sheet.compact ? Theme.spaceLg : Theme.customizeHeadingSize - Theme.spaceXxs
                        font.weight: Font.Light
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2; elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width; text: sheet.previewSub
                        font.pixelSize: Theme.customizeCaptionSize
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2; elide: Text.ElideRight
                    }
                }
            }

            ColumnLayout {
                id: workspace
                objectName: "customizeWorkspace"
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: Theme.spaceLg
                enabled: !sheet.busy
                Item {
                    objectName: "customizePivots"
                    visible: !sheet.artworkOnly
                    Layout.fillWidth: true; Layout.preferredHeight: Theme.spaceHuge - Theme.spaceSm
                    Row {
                        id: pivots
                        spacing: Theme.spaceXxl
                        Repeater {
                            model: [{label:"identity",value:"ident"},{label:"artwork",value:"art"}]
                            Button {
                                id: pivot
                                required property var modelData
                                text: modelData.label
                                width: implicitWidth; height: Theme.spaceHuge - Theme.spaceSm
                                font.pixelSize: Theme.spaceXl; font.weight: Font.Light
                                padding: 0
                                contentItem: Text {
                                    text: pivot.text; font: pivot.font
                                    color: sheet.tab === pivot.modelData.value ? Theme.textPrimary : Theme.textSubtle
                                }
                                background: Rectangle {
                                    anchors.bottom: parent.bottom
                                    implicitHeight: Theme.spaceXxxs; height: Theme.spaceXxxs
                                    color: sheet.tab === pivot.modelData.value ? Theme.pink : Theme.transparent
                                }
                                Accessible.role: Accessible.PageTab
                                Accessible.name: text
                                Accessible.selected: sheet.tab === modelData.value
                                Keys.onLeftPressed: sheet.showTab("ident")
                                Keys.onRightPressed: sheet.showTab("art")
                                onClicked: sheet.showTab(modelData.value)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 1; color: Theme.borderLight; z: -1
                    }
                }
                StackLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    currentIndex: sheet.tab === "ident" ? 0 : 1
                    // Identity: video matching and manual liner notes.
                    Flickable {
                        id: identityScroll
                        objectName: "customizeIdentityPane"
                        clip: true
                        contentWidth: width
                        contentHeight: identityColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ZuneScrollBar {}
                        ColumnLayout {
                            id: identityColumn
                            width: identityScroll.width - Theme.spaceMd
                            spacing: Theme.spaceLg
                            Text {
                                text: sheet.artworkMatchMode ? "Match artwork only." : "Tell its story."
                                font.pixelSize: Theme.customizeHeadingSize; font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: sheet.artworkMatchMode ? "Choose the artist or album for artwork. Your names, tags and grouping stay as they are."
                                    : sheet.isMusic ? "The name, the year, the notes. Your collection, in your words."
                                    : "A fan edit belongs in your collection, too."
                                color: Theme.textSecondary; font.pixelSize: Theme.customizeCaptionSize
                                wrapMode: Text.WordWrap
                            }
                            Row {
                                spacing: Theme.spaceMd
                                QuietButton {
                                    tabLike: true
                                    text: "write it yourself"; chosen: sheet.idTabMode === "manual"
                                    onClicked: { sheet.artworkMatchMode = false; sheet.idTabMode = "manual" }
                                }
                                QuietButton {
                                    tabLike: true
                                    text: sheet.isMusic ? "edit identity from a match" : "find a match"
                                    chosen: sheet.idTabMode === "match" && !sheet.artworkMatchMode
                                    onClicked: { sheet.artworkMatchMode = false; sheet.idTabMode = "match"; if (!sheet.results.length) sheet.searchIdentity() }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: sheet.idTabMode === "match"
                                spacing: Theme.spaceMd
                                RowLayout {
                                    Layout.fillWidth: true
                                    SearchField {
                                        objectName: "customizeIdentityQuery"
                                        Layout.fillWidth: true
                                        text: sheet.identityQuery
                                        placeholderText: "Search a title"
                                        onTextEdited: sheet.identityQuery = text
                                        onAccepted: sheet.searchIdentity()
                                        Accessible.name: "Search identity"
                                    }
                                    QuietButton { text: "search ↗"; onClicked: sheet.searchIdentity() }
                                }
                                Row {
                                    spacing: Theme.spaceMd
                                    visible: !sheet.isMusic
                                    QuietButton {
                                        tabLike: true
                                        text: "movie"; chosen: !sheet.searchTV
                                        onClicked: { sheet.searchTV = false; sheet.searchIdentity() }
                                    }
                                    QuietButton {
                                        tabLike: true
                                        text: "tv / series"; chosen: sheet.searchTV
                                        onClicked: { sheet.searchTV = true; sheet.searchIdentity() }
                                    }
                                }
                                Row {
                                    spacing: Theme.spaceMd
                                    visible: sheet.isMusic
                                    QuietButton {
                                        tabLike: true
                                        text: "musicbrainz"; chosen: sheet.identityProvider === "musicbrainz"
                                        onClicked: { sheet.identityProvider = "musicbrainz"; sheet.searchIdentity() }
                                    }
                                    QuietButton {
                                        tabLike: true
                                        visible: sheet.kind === "artist"
                                        text: "deezer"; chosen: sheet.identityProvider === "deezer"
                                        onClicked: { sheet.identityProvider = "deezer"; sheet.searchIdentity() }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: sheet.searching || sheet.searchError !== "" || sheet.results.length === 0
                                    text: sheet.searching ? "searching " + (sheet.isMusic ? sheet.identityProvider : "TMDB") + "…" : sheet.searchError || "No matches here. Try another title, or write it yourself."
                                    font.pixelSize: Theme.customizeBodySize
                                    color: sheet.searchError ? Theme.warning : Theme.textSecondary
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: sheet.results
                                    Button {
                                        id: matchButton
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: Theme.customizeFieldHeight + Theme.spaceSm
                                        padding: Theme.spaceSm
                                        background: Rectangle {
                                            radius: Theme.radiusSm
                                            color: matchButton.hovered ? Theme.cardHover : Theme.cardBg
                                            border.width: 1
                                            border.color: sheet.isSelectedMatch(matchButton.modelData)
                                                        ? Theme.pink : Theme.transparent
                                        }
                                        contentItem: RowLayout {
                                            spacing: Theme.spaceMd
                                            CustomizeArtwork {
                                                Layout.preferredWidth: Theme.spaceXxxl
                                                Layout.preferredHeight: sheet.isMusic ? Theme.spaceXxxl : Theme.spaceHuge
                                                source: matchButton.modelData.posterUrl || ""
                                                kind: sheet.kind
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true; spacing: Theme.spaceXxs
                                                Text {
                                                    Layout.fillWidth: true; text: matchButton.modelData.title
                                                    color: Theme.textPrimary; font.pixelSize: Theme.customizeBodySize
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: [matchButton.modelData.artist, matchButton.modelData.year,
                                                           matchButton.modelData.disambiguation, matchButton.modelData.country,
                                                           matchButton.modelData.type,
                                                           sheet.artworkMatchMode && sheet.kind === "album"
                                                               ? matchButton.modelData.releaseId ? "specific edition" : "general album"
                                                               : ""].filter(v => !!v).join(" · ")
                                                    elide: Text.ElideRight
                                                    color: Theme.textSecondary; font.pixelSize: Theme.customizeCaptionSize
                                                }
                                            }
                                            Text {
                                                text: sheet.isSelectedMatch(matchButton.modelData) ? "selected" : "use this"
                                                color: Theme.activePink; font.pixelSize: Theme.customizeCaptionSize
                                            }
                                        }
                                        onClicked: sheet.selectMatch(modelData)
                                    }
                                }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                visible: sheet.idTabMode === "manual"
                                columns: 2
                                columnSpacing: Theme.spaceXl; rowSpacing: Theme.spaceLg
                                CustomizeField {
                                    objectName: "customizeTitleField"
                                    Layout.fillWidth: true; Layout.columnSpan: 2
                                    label: sheet.kind === "artist" ? "Artist name" : sheet.kind === "album" ? "Album title"
                                         : sheet.identityFields.type === "tv" ? "Series title" : "Title"
                                    value: sheet.identityFields.title || ""
                                    placeholder: "Give it a name"
                                    prominent: true
                                    onEdited: value => sheet.setField("title", value)
                                }
                                CustomizeField {
                                    Layout.fillWidth: true
                                    visible: sheet.kind === "album"
                                    label: "Album artist"; value: sheet.identityFields.albumartist || ""
                                    onEdited: value => sheet.setField("albumartist", value)
                                }
                                Row {
                                    visible: !sheet.isMusic
                                    spacing: Theme.spaceSm
                                    QuietButton {
                                        tabLike: true
                                        text: "movie"; chosen: sheet.identityFields.type !== "tv"
                                        onClicked: sheet.setField("type", "movie")
                                    }
                                    QuietButton {
                                        tabLike: true
                                        text: "tv / series"; chosen: sheet.identityFields.type === "tv"
                                        onClicked: sheet.setField("type", "tv")
                                    }
                                }
                                CustomizeField {
                                    Layout.fillWidth: true
                                    visible: sheet.kind !== "artist"
                                    label: "Year"; value: sheet.identityFields.year || ""; numeric: true
                                    onEdited: value => sheet.setField("year", value)
                                }
                                CustomizeField {
                                    Layout.fillWidth: true; Layout.columnSpan: 2
                                    visible: sheet.kind !== "artist"
                                    label: "Genre"; value: sheet.isMusic ? sheet.identityFields.genre || "" : sheet.identityFields.genres || ""
                                    onEdited: value => sheet.setField(sheet.isMusic ? "genre" : "genres", value)
                                }
                                CustomizeField {
                                    Layout.fillWidth: true
                                    visible: !sheet.isMusic && sheet.identityFields.type === "tv" && !sheet.bulk
                                    label: "Season"; value: sheet.identityFields.season || ""; numeric: true
                                    onEdited: value => sheet.setField("season", value)
                                }
                                CustomizeField {
                                    Layout.fillWidth: true
                                    visible: !sheet.isMusic && sheet.identityFields.type === "tv" && !sheet.bulk
                                    label: "Episode"; value: sheet.identityFields.episode || ""; numeric: true
                                    onEdited: value => sheet.setField("episode", value)
                                }
                                CustomizeField {
                                    Layout.fillWidth: true; Layout.columnSpan: 2
                                    visible: !sheet.isMusic && sheet.identityFields.type === "tv" && !sheet.bulk
                                    label: "Episode title"; value: sheet.identityFields.episodeTitle || ""
                                    onEdited: value => sheet.setField("episodeTitle", value)
                                }
                                CustomizeField {
                                    Layout.fillWidth: true; Layout.columnSpan: 2
                                    label: sheet.isMusic ? "Liner notes" : "The story"; value: sheet.identityFields.overview || ""
                                    multiline: true
                                    onEdited: value => sheet.setField("overview", value)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: sheet.isMusic ? "Changes apply to the tracks in this " + (sheet.kind === "album" ? "album." : "artist’s collection.")
                                     : sheet.bulk ? "Each episode keeps its own season, number, and title." : ""
                                visible: text !== ""
                                color: Theme.textSecondary; font.pixelSize: Theme.customizeCaptionSize
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Artwork: one horizontal strip, independent of identity.
                    Flickable {
                        id: artworkScroll
                        clip: true
                        contentWidth: width
                        contentHeight: artworkColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ZuneScrollBar {}
                        ColumnLayout {
                            id: artworkColumn
                            width: artworkScroll.width - Theme.spaceMd
                            spacing: Theme.spaceLg
                            Text {
                                text: "Find its face."
                                font.pixelSize: Theme.customizeHeadingSize; font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: sheet.kind === "album" ? "Something that feels like this record."
                                     : sheet.kind === "artist" ? "A face for the name. A photo that feels right."
                                     : sheet.kind === "mixtape" ? "Your mix. Your cover."
                                     : sheet.kind === "genre" ? "Give this corner of your collection its own look."
                                     : "The poster you want on your shelf."
                                color: Theme.textSecondary; font.pixelSize: Theme.customizeCaptionSize
                                wrapMode: Text.WordWrap
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: sheet.isMusic
                                spacing: Theme.spaceSm
                                Text {
                                    objectName: "customizeArtworkStatus"
                                    Layout.fillWidth: true
                                    visible: sheet.artworkStatusText !== "" && !sheet.current.poster
                                    text: sheet.artworkStatusText + (sheet.artworkDiscovery.reason ? ". " + sheet.artworkDiscovery.reason : "")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.customizeCaptionSize
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: sheet.selectedArtworkMatch !== null
                                    text: sheet.selectedArtworkMatch && sheet.selectedArtworkMatch.scope === "general"
                                        ? "General album cover selected. This does not identify an exact edition."
                                        : "Artwork match selected. Apply remembers this choice without changing tags."
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.customizeCaptionSize
                                    wrapMode: Text.WordWrap
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceMd
                                    QuietButton {
                                        objectName: "customizeChooseArtworkMatch"
                                        text: sheet.identityMode === "keep" ? "choose artwork match" : "apply identity edits before matching artwork"
                                        enabled: !sheet.busy && sheet.identityMode === "keep"
                                        onClicked: sheet.chooseArtworkMatch()
                                    }
                                    QuietButton {
                                        objectName: "customizeRetryArtwork"
                                        text: "retry lookup"
                                        visible: !sheet.current.poster && sheet.artworkStatusText !== ""
                                        enabled: !sheet.busy && sheet.artworkDiscovery.status !== "lookingUp" && sheet.artworkDiscovery.status !== "checkingLocal"
                                        onClicked: sheet.service.retryArtwork(sheet.kind, sheet.current)
                                    }
                                    QuietButton {
                                        objectName: "customizeGeneralCover"
                                        text: "preview general cover"
                                        enabled: !sheet.busy && sheet.identityMode === "keep"
                                        visible: sheet.kind === "album" && sheet.artworkDiscovery.status === "generalCoverAvailable" && !!sheet.artworkDiscovery.imageUrl
                                        onClicked: sheet.stageGeneralCover()
                                    }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                visible: !sheet.artworkOnly
                                spacing: Theme.spaceSm
                                Repeater {
                                    model: sheet.artSources
                                    QuietButton {
                                        required property var modelData
                                        visible: !(sheet.kind === "album" && modelData.value === "fanart"
                                            && sheet.artMusicIdentity.releaseId
                                            && sheet.artQuery.trim() === sheet.artIdentityTitle.trim())
                                        tabLike: true
                                        text: modelData.label; chosen: sheet.artSource === modelData.value
                                        onClicked: { sheet.artSource = modelData.value; sheet.loadArt() }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: sheet.artSource !== "file" && sheet.artSource !== "grab"
                                SearchField {
                                    objectName: "customizeArtQuery"
                                    Layout.fillWidth: true
                                    text: sheet.artQuery
                                    placeholderText: "Search artwork"
                                    onTextEdited: sheet.artQuery = text
                                    onAccepted: sheet.loadArt(true)
                                    Accessible.name: "Search artwork independently of identity"
                                }
                                QuietButton { text: "search ↗"; onClicked: sheet.loadArt(true) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: sheet.artSource !== "file"
                                SmallLabel {
                                    Layout.fillWidth: true
                                    text: sheet.artSource === "grab" ? "FROM THE FILM" : sheet.kind === "artist" ? "PORTRAITS" : "ALTERNATE COVERS"
                                }
                                Text {
                                    text: sheet.artLoading ? "searching…" : sheet.artUrls.length + " found"
                                    color: Theme.textSecondary; font.pixelSize: Theme.customizeCaptionSize
                                }
                                QuietButton {
                                    objectName: "customizeCoversPrevious"
                                    text: "‹"; font.pixelSize: Theme.spaceXl
                                    enabled: coverStrip.contentX > 0
                                    implicitWidth: Theme.spaceXxl
                                    Accessible.name: sheet.kind === "artist" ? "Previous portraits" : "Previous covers"
                                    onClicked: sheet.scrollCovers(-1)
                                }
                                QuietButton {
                                    objectName: "customizeCoversNext"
                                    text: "›"; font.pixelSize: Theme.spaceXl
                                    enabled: coverStrip.contentX < coverStrip.contentWidth - coverStrip.width
                                    implicitWidth: Theme.spaceXxl
                                    Accessible.name: sheet.kind === "artist" ? "More portraits" : "More covers"
                                    onClicked: sheet.scrollCovers(1)
                                }
                            }
                            ListView {
                                id: coverStrip
                                objectName: "customizeCoverStrip"
                                Layout.fillWidth: true
                                Layout.preferredHeight: (sheet.isMusic ? Theme.customizeThumbSize : Theme.customizeThumbSize * 1.5) + Theme.spaceXxxl
                                visible: sheet.artSource !== "file" && sheet.artUrls.length > 0
                                orientation: ListView.Horizontal
                                spacing: Theme.spaceLg
                                clip: true
                                model: sheet.artUrls
                                boundsBehavior: Flickable.StopAtBounds
                                keyNavigationEnabled: true
                                keyNavigationWraps: false
                                activeFocusOnTab: true
                                highlightMoveDuration: Theme.motionFast
                                ScrollBar.horizontal: ScrollBar {
                                    height: Theme.spaceXxs
                                    policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                                    contentItem: Rectangle { radius: Theme.radiusSm; color: Theme.textSubtle }
                                    background: Rectangle { color: Theme.glassBg; radius: Theme.radiusSm }
                                }
                                Keys.onReturnPressed: if (currentIndex >= 0) sheet.stageArt(sheet.artUrls[currentIndex])
                                Keys.onSpacePressed: if (currentIndex >= 0) sheet.stageArt(sheet.artUrls[currentIndex])
                                Keys.onLeftPressed: if (currentIndex > 0) { decrementCurrentIndex(); positionViewAtIndex(currentIndex, ListView.Contain) }
                                Keys.onRightPressed: if (currentIndex < count - 1) { incrementCurrentIndex(); positionViewAtIndex(currentIndex, ListView.Contain) }
                                WheelHandler {
                                    target: null
                                    onWheel: event => {
                                        const delta = event.pixelDelta.x || event.pixelDelta.y || event.angleDelta.x / 2 || event.angleDelta.y / 2
                                        if (delta && coverStrip.contentWidth > coverStrip.width) {
                                            coverStrip.contentX = Math.max(0, Math.min(coverStrip.contentWidth - coverStrip.width, coverStrip.contentX - delta))
                                            event.accepted = true
                                        } else event.accepted = false
                                    }
                                }
                                delegate: Item {
                                    id: candidate
                                    required property var modelData
                                    required property int index
                                    width: Theme.customizeThumbSize
                                    height: coverStrip.height - Theme.spaceMd
                                    CustomizeArtwork {
                                        id: candidateImage
                                        anchors.top: parent.top
                                        width: parent.width
                                        height: sheet.isMusic ? width : width * 1.5
                                        source: candidate.modelData
                                        kind: sheet.kind
                                        showSelection: true
                                        selected: sheet.selectedArt === String(candidate.modelData)
                                        hovered: candidateHover.hovered || (coverStrip.activeFocus && coverStrip.currentIndex === candidate.index)
                                    }
                                    Text {
                                        anchors.top: candidateImage.bottom; anchors.topMargin: Theme.spaceSm
                                        width: parent.width
                                        text: candidateImage.status === Image.Error ? "unavailable"
                                             : candidateImage.selected ? "your pick"
                                             : (sheet.kind === "artist" ? "portrait " : "cover ") + (candidate.index + 1)
                                        color: candidateImage.selected ? Theme.activePink : Theme.textSecondary
                                        font.pixelSize: Theme.customizeCaptionSize
                                    }
                                    HoverHandler { id: candidateHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler {
                                        enabled: candidateImage.status === Image.Ready
                                        onTapped: { coverStrip.currentIndex = candidate.index; sheet.stageArt(candidate.modelData) }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: sheet.artSource !== "file" && (sheet.artLoading || sheet.artError !== "" || sheet.artUrls.length === 0)
                                text: sheet.artLoading ? (sheet.artSource === "grab" ? "Finding moments in your video…" : "Looking for artwork…")
                                     : sheet.artError || "No artwork here yet. Try another search, another source, or your own file."
                                font.pixelSize: Theme.customizeBodySize
                                color: sheet.artError ? Theme.warning : Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                            QuietButton {
                                visible: sheet.artSource !== "file" && sheet.artError !== "" && !sheet.artLoading
                                text: "try again ↗"
                                onClicked: sheet.loadArt(true)
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.customizeArtSize * 0.65
                                visible: sheet.artSource === "file"
                                color: fileDrop.containsDrag ? Qt.alpha(Theme.pink, 0.08) : Theme.cardBg
                                radius: Theme.radiusMd
                                border.width: 1
                                border.color: fileDrop.containsDrag ? Theme.pink : Theme.borderLight
                                Column {
                                    anchors.centerIn: parent; width: parent.width - Theme.spaceXxxl
                                    spacing: Theme.spaceMd
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "bring your own."
                                        font.family: Theme.displayFamily; font.pixelSize: Theme.customizeHeadingSize
                                        rotation: -3; color: Theme.activePink
                                    }
                                    Text {
                                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                                        text: "Drop an image here, or choose a file."
                                        font.pixelSize: Theme.customizeBodySize; color: Theme.textSecondary
                                        wrapMode: Text.WordWrap
                                    }
                                    QuietButton {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "choose image…"; accent: true
                                        onClicked: artDialog.open()
                                    }
                                }
                                DropArea {
                                    id: fileDrop
                                    anchors.fill: parent
                                    onEntered: drag => { drag.accepted = drag.hasUrls && drag.urls.length === 1 && String(drag.urls[0]).startsWith("file:") }
                                    onDropped: drop => {
                                        if (drop.hasUrls && drop.urls.length === 1 && String(drop.urls[0]).startsWith("file:")) {
                                            sheet.stageArt(drop.urls[0]); drop.acceptProposedAction()
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: sheet.selectedArt ? "Your pick is in the sleeve. Apply when it feels right."
                                     : sheet.kind === "artist" ? "Pick a portrait. Make it theirs."
                                     : "Pick a cover. See it in the sleeve."
                                font.pixelSize: Theme.customizeCaptionSize; color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "customizeFooter"
            Layout.fillWidth: true
            Layout.preferredHeight: footerColumn.implicitHeight + Theme.spaceXxxl
            color: Qt.alpha(Theme.bg, 0.42)
            radius: Theme.radiusLg
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderLight }
            ColumnLayout {
                id: footerColumn
                anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spaceXxxl; anchors.rightMargin: Theme.spaceXxxl
                spacing: Theme.spaceSm
                Text {
                    Layout.fillWidth: true
                    visible: sheet.errorMessage !== ""
                    text: sheet.errorMessage
                    font.pixelSize: Theme.customizeBodySize; color: Theme.warning
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.AlertMessage
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceLg
                    Rectangle { width: Theme.spaceXxs; height: width; radius: width / 2; color: Theme.green }
                    Text {
                        Layout.fillWidth: true
                        text: sheet.busy ? "Saving your changes…"
                             : sheet.identityMode === "reset" || sheet.artReset ? "Automatic on Apply. Cancel keeps your picks."
                             : sheet.dirty ? "Your picks stay yours." : "Make a change. Make it yours."
                        font.pixelSize: Theme.customizeCaptionSize; color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }
                    QuietButton {
                        objectName: "customizeReset"
                        visible: !sheet.compact
                        text: sheet.isMusic || sheet.artworkOnly ? "reset artwork" : "reset to auto"
                        enabled: !sheet.busy
                        onClicked: sheet.resetDraft()
                    }
                    QuietButton {
                        objectName: "customizeCancel"
                        text: "cancel"; enabled: !sheet.busy
                        onClicked: sheet.discard()
                    }
                    QuietButton {
                        objectName: "customizeApply"
                        text: sheet.busy ? "saving…" : "apply changes  ↗"
                        filled: true
                        enabled: sheet.dirty && sheet.valid && !sheet.busy
                        onClicked: sheet.applyDraft()
                    }
                }
                QuietButton {
                    visible: sheet.compact
                    text: sheet.isMusic || sheet.artworkOnly ? "reset artwork" : "reset to auto"
                    enabled: !sheet.busy
                    onClicked: sheet.resetDraft()
                }
            }
        }
    }
    FileDialog {
        id: artDialog
        title: "Choose artwork"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: sheet.stageArt(selectedFile.toString())
    }
}
