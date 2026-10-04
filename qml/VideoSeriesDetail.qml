import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Zuuned

// UX-3 "S4 poster hero" (approved from Orson's reference): the poster
// IS the page — full-height hero card left with the identity at its
// foot, season pill + episode GRID right. Below the narrow breakpoint
// the hero collapses to a compact identity row and the grid reflows to
// a single-column vertical list (same cards, same data, one layout
// switch). Episode stills are the play buttons — click one to binge
// from that episode.
Item {
    id: page

    property var browser
    property string seriesKey: ""

    signal back()
    signal queueEpisodeRequested(var ep)
    signal queueEpisodesRequested(var eps)
    signal queueSeriesRequested(string key)
    signal playEpisodesRequested(var eps, int startIndex)
    signal customizeRequested(string key)
    signal customizeEpisodeRequested(var ep)
    signal customizeEpisodesRequested(var eps)
    signal renameEpisodeRequested(var ep)

    // ── Device mode (W25): the SAME layout drives the Zune's series
    // drill-down. Library verbs (play, sync-to-device, mark-watched,
    // fix-match) hide; the rule-of-3 becomes save-to-library / delete.
    property bool deviceMode: false
    signal saveEpisodeRequested(var ep)     // ＋ / right-click → pull one
    signal saveSeasonRequested(var eps)     // hero → pull the season
    signal deleteEpisodeRequested(var ep)   // right-click → delete off zune

    property var series: ({})
    property int selectedSeason: 0
    property var episodes: []
    // Library mode: host supplies ep => syncItem so an episode card can
    // drag to the device panel (same payload the "queue" verb produces).
    // Device mode uses the built-in device→library payload instead.
    property var episodeSyncItemFn: null

    // The one layout switch: grid ↔ vertical list.
    readonly property bool narrow: width < 720

    // Hero poster keeps TRUE poster aspect (2:3) and fills the page
    // height: width follows from the available height, bounded only by
    // a share of the window so short-wide panes stay usable.
    readonly property real heroW:
        Math.max(220, Math.min(width * 0.32, (height - 250) * 2 / 3))

    function refresh() {
        if (!browser) return
        const s = browser.seriesDetail(seriesKey)
        if (s.key === undefined) {
            // Series vanished (last episode deleted) — pop back.
            page.back()
            return
        }
        page.series = s
        if (page.selectedSeason === 0
            || !(s.seasons ?? []).includes(page.selectedSeason))
            page.selectedSeason = s.defaultSeason
        page.episodes = browser.episodesForSeason(seriesKey, page.selectedSeason)
    }
    // Hero-only refresh: update the series poster/metadata WITHOUT
    // reassigning page.episodes — the episode grid's array stays the
    // same object, so its delegates aren't rebuilt and the frame-grab
    // stills don't reload (the surgical-refresh fix, 2026-09-08).
    function refreshHero() {
        if (!browser) return
        const s = browser.seriesDetail(seriesKey)
        if (s.key !== undefined)
            page.series = s
    }
    onSelectedSeasonChanged: {
        if (browser) episodes = browser.episodesForSeason(seriesKey, selectedSeason)
    }
    onSeriesKeyChanged: { if (browser) refresh() }
    Component.onCompleted: refresh()
    Connections {
        target: LibraryService
        enabled: !page.deviceMode
        function onVideosChanged() { page.refresh() }
        // A field-only correction of this series' rows: repaint the
        // hero, leave the episode grid (and its stills) alone.
        function onVideoRowsChanged(ids) { page.refreshHero() }
    }
    // Device mode: a delete/save changes the Zune's video list — refresh
    // so the drilled series reflects it (and pops back if it emptied).
    Connections {
        target: DeviceService
        enabled: page.deviceMode
        function onStateChanged() { page.refresh() }
    }

    function seasonLabel(s) { return s > 0 ? "Season " + s : "Specials" }

    readonly property int onZuneCount:
        episodes.filter(e => e.isOnDevice).length

    // "▶ next up" — first unwatched episode across the whole series.
    function playNextUp() {
        if (deviceMode) return
        let eps = []
        for (const s of page.series.seasons ?? [])
            eps = eps.concat(browser.episodesForSeason(seriesKey, s))
        if (eps.length === 0)
            return
        const start = eps.findIndex(e => !e.watched)
        page.playEpisodesRequested(eps, start < 0 ? 0 : start)
    }

    // Device object IDs and library IDs are different namespaces. All
    // interaction paths (including menus left open across a disconnect) enter
    // through these guards before dispatching to a service or host signal.
    function currentEpisode(ep) {
        if (deviceMode) return ep
        const overrides = browser?.rowOverrides ?? ({})
        return Object.assign({}, ep, overrides[String(ep.id)] ?? {})
    }
    function currentEpisodes() { return episodes.map(currentEpisode) }
    function allEpisodes() {
        let out = []
        for (const season of series.seasons ?? [])
            out = out.concat(browser.episodesForSeason(seriesKey, season))
        return out.map(currentEpisode)
    }
    function transferEpisode(ep) {
        if (!DeviceService.connected) return
        if (deviceMode) saveEpisodeRequested(ep)
        else queueEpisodeRequested(currentEpisode(ep))
    }
    function transferSeason() {
        if (!DeviceService.connected) return
        if (deviceMode) saveSeasonRequested(currentEpisodes())
        else queueEpisodesRequested(currentEpisodes())
    }
    function transferSeries() {
        if (!DeviceService.connected) return
        if (deviceMode) saveSeasonRequested(allEpisodes())
        else queueSeriesRequested(seriesKey)
    }
    function playEpisode(ep) {
        if (deviceMode) return
        const rows = currentEpisodes()
        const index = rows.findIndex(e => Number(e.id) === Number(ep.id))
        if (index >= 0) playEpisodesRequested(rows, index)
    }
    function playSeason() {
        if (deviceMode) return
        const rows = currentEpisodes()
        const index = rows.findIndex(e => !e.watched)
        playEpisodesRequested(rows, index < 0 ? 0 : index)
    }
    function markWatched(eps, watched) {
        if (!deviceMode) LibraryService.setVideosWatched(eps.map(e => e.id), watched)
    }
    function customizeEpisode(ep) {
        if (!deviceMode) customizeEpisodeRequested(currentEpisode(ep))
    }
    function deleteEpisode(ep) {
        if (deviceMode) {
            if (DeviceService.connected) deleteEpisodeRequested(ep)
        } else LibraryService.deleteVideos([ep.id])
    }
    function episodeDragItems(eps) {
        return deviceMode
            ? eps.map(ep => ({itemId: ep.itemId, name: ep.filename,
                series: ep.series, season: ep.season, episode: ep.episode}))
            : (episodeSyncItemFn ? eps.map(ep => episodeSyncItemFn(currentEpisode(ep))) : [])
    }
    function deleteEpisodes(eps) {
        if (!deviceMode) LibraryService.deleteVideos(eps.map(e => e.id))
        else if (DeviceService.connected) DeviceService.purgeItems(eps.map(e => e.itemId))
    }
    function openEpisodeMenu(ep) {
        epMenu.ep = currentEpisode(ep)
        epMenu.popup()
    }
    component EpisodeGesture: MouseArea {
        id: gesture
        property var ep: ({})
        objectName: "episodeGesture"
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        readonly property bool dragArmed: DeviceService.connected
            && (page.deviceMode || page.episodeSyncItemFn !== null)
        drag.target: dragArmed ? episodeGhost : null
        preventStealing: dragArmed
        onPressed: mouse => episodeGhost.place(gesture, mouse)
        onReleased: episodeGhost.drop()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) page.openEpisodeMenu(ep)
            else if (page.deviceMode) page.transferEpisode(ep)
            else page.playEpisode(ep)
        }
        DragGhost {
            id: episodeGhost
            area: gesture
            dragKind: page.deviceMode ? "device-videos" : "videos"
            dragName: gesture.ep.episodeTitle ?? ""
            dragTracks: () => page.episodeDragItems([gesture.ep])
            Rectangle {
                width: Theme.spaceXxl * 2; height: width * 9 / 16
                radius: Theme.radiusSm; color: Theme.artworkPlaceholder
                ArtworkImage {
                    anchors.fill: parent
                    source: gesture.ep.still ?? ""
                    fillMode: Image.PreserveAspectCrop
                }
            }
        }
    }
    component SeriesGesture: MouseArea {
        id: seriesGesture
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        drag.target: DeviceService.connected ? seriesGhost : null
        preventStealing: DeviceService.connected
        onPressed: mouse => seriesGhost.place(seriesGesture, mouse)
        onReleased: seriesGhost.drop()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) seriesMenu.popup()
            else if (page.deviceMode) page.transferSeries()
            else page.playNextUp()
        }
        DragGhost {
            id: seriesGhost
            area: seriesGesture
            dragKind: page.deviceMode ? "device-videos" : "videos"
            dragName: page.series.name ?? ""
            dragTracks: () => page.episodeDragItems(page.allEpisodes())
            Rectangle {
                width: Theme.spaceXxl; height: width * 1.5
                radius: Theme.radiusSm; color: Theme.artworkPlaceholder
                ArtworkImage { anchors.fill: parent; source: page.series.poster ?? ""; fillMode: Image.PreserveAspectCrop }
            }
        }
    }

    // Episode still with the frame-grab fallback — shared by both the
    // grid card and the list row. The still is the play button.
    component EpStill: Rectangle {
        id: still
        property var ep: ({})
        property bool hovering: false

        radius: Theme.radiusMd
        color: Theme.cardBg
        clip: true
        // Rounded ART needs a mask — a radius on the container
        // doesn't clip the Image (QML clip is rectangular)
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: still.width; height: still.height
                radius: Theme.radiusMd
            }
        }

        ArtworkImage {
            anchors.fill: parent
            source: {
                if ((still.ep.still ?? "") !== "")
                    return still.ep.still
                // Device episodes: the representative-sample device art,
                // read reactively (it lands after requestArt below).
                if (page.deviceMode) {
                    void DeviceService.artPaths
                    return DeviceService.artPaths[
                        String(still.ep.itemId ?? still.ep.id)] ?? ""
                }
                const s = FrameThumbnailService.stills[String(still.ep.id)]
                return s === undefined ? "" : s
            }
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: 360
            Component.onCompleted: {
                if (page.deviceMode)
                    DeviceService.requestArt(still.ep.itemId ?? still.ep.id)
                // Library episodes with no still grab a frame from the file.
                else if ((still.ep.still ?? "") === "" && (still.ep.filepath ?? "") !== "")
                    FrameThumbnailService.requestStill(
                        still.ep.id, still.ep.filepath, still.ep.durationMs)
            }
        }
        // watched tick — top-left, the reference's green dot
        Rectangle {
            visible: still.ep.watched === true
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Theme.spaceXs
            width: 16; height: 16; radius: 8
            color: Theme.green
            Text {
                anchors.centerIn: parent
                text: "✓"
                font.pixelSize: 9
                font.bold: true
                color: "#08130b"
            }
        }
        OnDeviceBadge {
            visible: still.ep.isOnDevice === true
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spaceXs
            size: 16
        }
        // hover play ring — the still is the play button
        Rectangle {
            anchors.centerIn: parent
            width: 34; height: 34; radius: 17
            visible: still.hovering && (!page.deviceMode || DeviceService.connected)
            color: Qt.rgba(0, 0, 0, 0.45)
            border.width: 2
            border.color: Qt.rgba(1, 1, 1, 0.85)
            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 1
                text: page.deviceMode ? "⤓" : "▶"
                font.pixelSize: 13
                color: "white"
            }
        }
        EpisodeGesture {
            anchors.fill: parent
            ep: still.ep
            onEntered: still.hovering = true
            onExited: still.hovering = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // (Back bar removed 2026-09-06: the HOST keeps the pivot band
        // and draws the breadcrumb row — drill chrome is app chrome.)

        // ═══ NARROW: compact identity row ═══
        RowLayout {
            visible: page.narrow
            Layout.fillWidth: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXl
            Layout.topMargin: Theme.spaceLg
            spacing: Theme.spaceMd

            Rectangle {
                SeriesGesture { anchors.fill: parent; z: 1 }
                id: miniPoster
                Layout.preferredWidth: 56
                Layout.preferredHeight: 84
                radius: Theme.radiusMd
                color: Theme.artworkPlaceholder
                clip: true
                // Rounded ART needs a mask — a radius on the container
                // doesn't clip the Image (QML clip is rectangular)
                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: miniPoster.width; height: miniPoster.height
                        radius: Theme.radiusMd
                    }
                }
                ArtworkImage {
                    anchors.fill: parent
                    source: page.series.poster ?? ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 120
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                GradientText {
                    Layout.maximumWidth: parent.width
                    text: (page.series.name ?? "").toLowerCase()
                    font.family: Theme.displayFamily
                    font.pixelSize: 22
                    tracking: 1
                }
                RowLayout {
                    spacing: Theme.spaceXs
                    Text {
                        visible: (page.series.rating ?? 0) > 0
                        text: "★ " + Number(page.series.rating ?? 0).toFixed(1)
                        font.pixelSize: 11
                        color: Theme.orange
                    }
                    Text {
                        text: (page.series.seasonCount ?? 1)
                              + ((page.series.seasonCount ?? 1) === 1
                                 ? " season" : " seasons")
                              + " · " + (page.series.episodeCount ?? 0) + " eps"
                        font.pixelSize: 11
                        color: Theme.textSecondary
                    }
                }
            }
            ColumnLayout {
                Text {
                    visible: !page.deviceMode
                    text: "customize"
                    font.pixelSize: 12; color: Theme.textDim
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { if (!page.deviceMode) page.customizeRequested(page.seriesKey) } }
                }
                SplitAddButton {
                    visible: DeviceService.connected
                    glyph: page.deviceMode ? "⤓" : "＋"
                    showPick: false
                    onAddAll: page.transferSeries()
                }
            }
            Text {
                visible: !page.deviceMode
                text: "▶ next up"
                font.pixelSize: 12
                color: nextUpNarrowArea.containsMouse
                       ? Theme.activePink : Theme.pink
                MouseArea {
                    id: nextUpNarrowArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.playNextUp()
                }
            }
        }

        // ═══ Content row: hero (wide only) + episode pane ═══
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceXl

            // ── WIDE: the poster hero — full height, identity at the foot ──
            ColumnLayout {
                visible: !page.narrow
                // NOT fillWidth: nested layouts default to fillWidth
                // true, which made this column eat the row and shove
                // the episode pane into the corner.
                Layout.fillWidth: false
                Layout.preferredWidth: page.heroW
                Layout.maximumWidth: page.heroW
                Layout.fillHeight: true
                Layout.leftMargin: Gallery.spine(page.width)
                Layout.topMargin: Theme.spaceLg
                Layout.bottomMargin: Theme.spaceLg
                spacing: Theme.spaceSm

                Item {
                    Layout.preferredWidth: page.heroW
                    Layout.preferredHeight: page.heroW * 1.5

                    DropShadow {
                        anchors.fill: heroPoster
                        source: heroPoster
                        radius: 22
                        samples: 25
                        verticalOffset: 8
                        color: Qt.rgba(0, 0, 0, 0.6)
                    }
                    SeriesGesture { anchors.fill: parent; z: 1 }
                    // Frame behind (rounded bg + border)
                    Rectangle {
                        id: heroPoster
                        anchors.fill: parent
                        radius: Theme.radiusMd
                        color: Theme.artworkPlaceholder
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.12)
                    }
                    // Poster rendered offscreen then rounded by the
                    // OpacityMask (layer.effect didn't round the async
                    // Image here — sibling-OpacityMask is the working
                    // pattern in this app, 2026-09-07).
                    ArtworkImage {
                        id: heroImg
                        anchors.fill: parent
                        source: page.series.poster ?? ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: 640
                        visible: false
                    }
                    Rectangle {
                        id: heroImgMask
                        anchors.fill: parent
                        radius: Theme.radiusMd
                        visible: false
                    }
                    OpacityMask {
                        anchors.fill: parent
                        source: heroImg
                        maskSource: heroImgMask
                        visible: heroImg.status === Image.Ready
                    }
                }

                GradientText {
                    Layout.maximumWidth: parent.width
                    text: (page.series.name ?? "").toLowerCase()
                    font.family: Theme.displayFamily
                    font.pixelSize: 28
                    tracking: 1
                    rotation: -1
                    transformOrigin: Item.Left
                }
                // TMDB's official title, when it differs (folder-truth rule)
                Text {
                    visible: (page.series.tmdbTitle ?? "") !== ""
                             && (page.series.tmdbTitle ?? "").toLowerCase()
                                !== (page.series.name ?? "").toLowerCase()
                    text: page.series.tmdbTitle ?? ""
                    font.pixelSize: 11
                    color: Theme.textDim
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs
                    Text {
                        visible: (page.series.rating ?? 0) > 0
                        text: "★ " + Number(page.series.rating ?? 0).toFixed(1)
                        font.pixelSize: 11
                        color: Theme.orange
                    }
                    Text {
                        visible: (page.series.year ?? "") !== ""
                        text: "· " + page.series.year
                        font.pixelSize: 11
                        color: Theme.textSecondary
                    }
                    Text {
                        text: "· " + (page.series.seasonCount ?? 1)
                              + ((page.series.seasonCount ?? 1) === 1
                                 ? " season" : " seasons")
                              + " · " + (page.series.episodeCount ?? 0)
                              + " episodes"
                        font.pixelSize: 11
                        color: Theme.textSecondary
                    }
                    Item { Layout.fillWidth: true }
                }
                // Actions at the hero's foot — secondary verbs lead,
                // ▶ next up rightmost (house rule).
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceXxs
                    spacing: Theme.spaceMd

                    Text {
                        visible: !page.deviceMode
                        text: "customize"
                        font.pixelSize: 12
                        color: fixArea.containsMouse
                               ? Theme.textSecondary : Theme.textDim
                        MouseArea {
                            id: fixArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { if (!page.deviceMode) page.customizeRequested(page.seriesKey) }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    SplitAddButton {
                        showPick: false
                        visible: DeviceService.connected
                        glyph: page.deviceMode ? "⤓" : "＋"
                        onAddAll: page.transferSeries()
                    }
                    Text {
                        visible: !page.deviceMode
                        text: "▶ next up"
                        font.pixelSize: 12
                        color: nextUpArea.containsMouse
                               ? Theme.activePink : Theme.pink
                        MouseArea {
                            id: nextUpArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.playNextUp()
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // ── Episode pane — grid when wide, list when narrow;
            // hero + pane together fill the standard frame ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: page.narrow ? Gallery.spine(page.width) : 0
                Layout.topMargin: Theme.spaceLg
                Layout.rightMargin: Math.max(Theme.spaceXl,
                    page.width - Gallery.spine(page.width)
                    - Gallery.frameW(page.width))
                spacing: 0

                // Season pill row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.spaceSm
                    spacing: Theme.spaceMd

                    // The gradient season pill from the board
                    ComboBox {
                        id: seasonBox
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: seasonMenu.popup()
                        }
                        model: (page.series.seasons ?? []).map(
                                   s => page.seasonLabel(s))
                        currentIndex: Math.max(0,
                            (page.series.seasons ?? []).indexOf(page.selectedSeason))
                        onActivated: idx =>
                            page.selectedSeason = (page.series.seasons ?? [])[idx]
                        implicitWidth: pillText.implicitWidth + 44
                        implicitHeight: 30
                        font.pixelSize: 13
                        background: Rectangle {
                            radius: height / 2
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Theme.orange }
                                GradientStop { position: 1.0; color: Theme.pink }
                            }
                        }
                        contentItem: Text {
                            id: pillText
                            leftPadding: Theme.spaceLg
                            text: seasonBox.displayText
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: "white"
                            verticalAlignment: Text.AlignVCenter
                        }
                        indicator: Text {
                            x: seasonBox.width - width - Theme.spaceMd
                            anchors.verticalCenter: parent.verticalCenter
                            text: "▾"
                            font.pixelSize: 12
                            color: "white"
                        }
                    }

                    Text {
                        text: page.episodes.length + " episodes"
                              + (page.onZuneCount > 0
                                 ? " · " + page.onZuneCount + " on zune" : "")
                        font.pixelSize: 11
                        color: Theme.textDim
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: !page.narrow && !page.deviceMode
                        text: "mark watched"
                        font.pixelSize: 12
                        color: markArea.containsMouse
                               ? Theme.textSecondary : Theme.textDim
                        MouseArea {
                            id: markArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.markWatched(page.currentEpisodes(), true)
                        }
                    }
                    Text {
                        visible: DeviceService.connected
                        // Device: pull the whole season to library.
                        text: page.deviceMode ? "⤓ save season" : "+ sync season"
                        font.pixelSize: 12
                        color: syncSeasonArea.containsMouse
                               ? Theme.activePink : Theme.pink
                        MouseArea {
                            id: syncSeasonArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: mouse => {
                                if (mouse.button === Qt.RightButton) seasonMenu.popup()
                                else page.transferSeason()
                            }
                            // W26 · season-level drag: this verb is also the
                            // grab handle for the whole season (click acts,
                            // press-drag drags — same pattern as the cards).
                            readonly property bool dragArmed:
                                DeviceService.connected
                                && (page.deviceMode
                                    || page.episodeSyncItemFn !== null)
                            drag.target: dragArmed ? seasonGhost : null
                            preventStealing: dragArmed
                            onPressed: mouse => seasonGhost.place(syncSeasonArea, mouse)
                            onReleased: seasonGhost.drop()
                        }
                        DragGhost {
                            id: seasonGhost
                            area: syncSeasonArea
                            dragKind: page.deviceMode ? "device-videos" : "videos"
                            dragName: page.seasonLabel(page.selectedSeason)
                            dragTracks: () => page.episodeDragItems(page.currentEpisodes())
                            Rectangle {
                                width: 88; height: 46; radius: 4
                                color: Theme.cardActive
                                border.width: 1
                                border.color: Qt.alpha(Theme.pink, 0.75)
                                Text {
                                    anchors.centerIn: parent
                                    text: page.seasonLabel(page.selectedSeason)
                                    font.pixelSize: 11
                                    color: Theme.textSecondary
                                }
                            }
                        }
                    }
                    Text {
                        visible: !page.deviceMode
                        text: "▶ play season"
                        font.pixelSize: 12
                        color: playSeasonArea.containsMouse
                               ? Theme.activePink : Theme.pink
                        MouseArea {
                            id: playSeasonArea
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.playSeason()
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                }

                // ═══ WIDE: episode GRID ═══
                GridView {
                    id: epGrid
                    visible: !page.narrow
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: Theme.spaceMd
                    clip: true
                    model: page.narrow ? [] : page.episodes
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ZuneScrollBar {
                        parent: page
                        visible: !page.narrow && size < 1.0
                        anchors.top: parent.top
                        anchors.topMargin: 64
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                    }

                    readonly property int columns:
                        Math.max(2, Math.floor(width / 240))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: (cellWidth - Theme.spaceMd) * 9 / 16 + 108

                    delegate: Item {
                        id: epCard
                        readonly property var row: page.currentEpisode(modelData)
                        required property var modelData
                        width: epGrid.cellWidth
                        height: epGrid.cellHeight

                        // ONE hover for the whole card: a HoverHandler sees
                        // the pointer anywhere over the card, even where a
                        // child MouseArea (the still, the verbs) would
                        // otherwise swallow it — so top preview and bottom
                        // info light as a single card, not two halves.
                        property bool hovering: cardHover.hovered
                        HoverHandler { id: cardHover }

                        Rectangle {
                            anchors.fill: parent
                            anchors.rightMargin: Theme.spaceMd
                            anchors.bottomMargin: Theme.spaceMd
                            radius: Theme.radiusMd
                            color: epCard.hovering ? Theme.cardHover : Theme.cardBg
                            Behavior on color {
                                ColorAnimation { duration: Theme.motionFast }
                            }
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.rightMargin: Theme.spaceMd
                            anchors.bottomMargin: Theme.spaceMd
                            spacing: 0

                            EpStill {
                                ep: epCard.row
                                Layout.fillWidth: true
                                Layout.preferredHeight: width * 9 / 16
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.margins: Theme.spaceSm
                                spacing: 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spaceXs
                                    Text {
                                        visible: epCard.row.episode > 0
                                        text: "EPISODE " + epCard.row.episode
                                        font.pixelSize: 9
                                        font.bold: true
                                        font.letterSpacing: 1.5
                                        color: Theme.pink
                                    }
                                    Item { Layout.fillWidth: true }
                                    // hover verbs: queue + watched toggle
                                    Text {
                                        visible: epCard.hovering && DeviceService.connected
                                        text: page.deviceMode ? "save" : "to zune"
                                        font.pixelSize: 10
                                        color: qArea.containsMouse
                                               ? Theme.activePink : Theme.pink
                                        MouseArea {
                                            id: qArea
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: page.transferEpisode(epCard.row)
                                        }
                                    }
                                    Text {
                                        text: epCard.row.watched ? "✔" : "○"
                                        visible: !page.deviceMode && (epCard.hovering
                                                 || epCard.row.watched)
                                        font.pixelSize: 11
                                        color: epCard.row.watched
                                               ? Theme.pink : Theme.textDim
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: page.markWatched([epCard.row], !epCard.row.watched)
                                        }
                                    }
                                }
                                Text {
                                    text: epCard.row.episodeTitle !== ""
                                        ? epCard.row.episodeTitle
                                        : epCard.row.filename
                                    font.pixelSize: 13
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    spacing: Theme.spaceXs
                                    Text {
                                        visible: epCard.row.durationMs > 0
                                                 || epCard.row.episodeRuntime > 0
                                        text: epCard.row.durationMs > 0
                                            ? Math.floor(epCard.row.durationMs
                                                         / 60000) + " min"
                                            : epCard.row.episodeRuntime + " min"
                                        font.pixelSize: 10
                                        color: Theme.textSecondary
                                    }
                                    Text {
                                        visible: epCard.row.episodeRating > 0
                                        text: "★ " + Number(
                                            epCard.row.episodeRating).toFixed(1)
                                        font.pixelSize: 10
                                        color: Theme.orange
                                    }
                                    Text {
                                        visible: epCard.row.airDate !== ""
                                        text: "· " + epCard.row.airDate
                                        font.pixelSize: 10
                                        color: Theme.textDim
                                    }
                                }
                                Text {
                                    visible: epCard.row.description !== ""
                                    text: epCard.row.description
                                    font.pixelSize: 10
                                    font.weight: Font.Light
                                    color: Theme.textDim
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }

                        // Edge-light the WHOLE card on hover (drawn OVER
                        // the opaque still so the top lights too) — the
                        // house hover treatment, one outline for one card.
                        Rectangle {
                            anchors.fill: parent
                            anchors.rightMargin: Theme.spaceMd
                            anchors.bottomMargin: Theme.spaceMd
                            radius: Theme.radiusMd
                            color: "transparent"
                            border.width: 1.5
                            border.color: Qt.alpha(Theme.activePink,
                                                   epCard.hovering ? 0.9 : 0)
                            Behavior on border.color {
                                ColorAnimation { duration: Theme.motionFast }
                            }
                            z: 5
                        }

                        // Rule of 3 · way #2: the ＋ button, top-right
                        // (right-click and drag are the other two).
                        SplitAddButton {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spaceMd + Theme.spaceXs
                            anchors.topMargin: Theme.spaceXs
                            visible: DeviceService.connected
                            showPick: false
                            // Device: ⤓ save-to-library (armed=orange);
                            // library: ＋ queue-for-device.
                            glyph: page.deviceMode ? "⤓" : "＋"
                            armed: page.deviceMode
                            z: 6
                            opacity: epCard.hovering ? 1 : 0
                            Behavior on opacity {
                                NumberAnimation { duration: Theme.motionFast }
                            }
                            onAddAll: page.transferEpisode(epCard.row)
                        }

                        EpisodeGesture {
                            anchors.fill: parent
                            z: -1
                            ep: epCard.row
                        }
                    }
                }

                // ═══ NARROW: vertical episode list ═══
                ListView {
                    visible: page.narrow
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: page.narrow ? page.episodes : []
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 0
                    ScrollBar.vertical: ZuneScrollBar {
                        parent: page
                        visible: page.narrow && size < 1.0
                        anchors.top: parent.top
                        anchors.topMargin: 64
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                    }

                    delegate: Item {
                        id: epRow
                        readonly property var row: page.currentEpisode(modelData)
                        required property var modelData
                        width: ListView.view.width
                        height: 82

                        property bool hovering: narrowHover.hovered
                        HoverHandler { id: narrowHover }

                        Rectangle {
                            anchors.fill: parent
                            color: epRow.hovering
                                   ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 0
                            anchors.rightMargin: Theme.spaceSm
                            spacing: Theme.spaceMd

                            EpStill {
                                ep: epRow.row
                                Layout.preferredWidth: 118
                                Layout.preferredHeight: 66
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: (epRow.row.episode > 0
                                           ? "E" + epRow.row.episode + "  "
                                           : "")
                                          + (epRow.row.episodeTitle !== ""
                                             ? epRow.row.episodeTitle
                                             : epRow.row.filename)
                                    font.pixelSize: 13
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    spacing: Theme.spaceXs
                                    Text {
                                        visible: epRow.row.durationMs > 0
                                                 || epRow.row.episodeRuntime > 0
                                        text: epRow.row.durationMs > 0
                                            ? Math.floor(epRow.row.durationMs
                                                         / 60000) + " min"
                                            : epRow.row.episodeRuntime + " min"
                                        font.pixelSize: 10
                                        color: Theme.textSecondary
                                    }
                                    Text {
                                        visible: epRow.row.episodeRating > 0
                                        text: "★ " + Number(
                                            epRow.row.episodeRating).toFixed(1)
                                        font.pixelSize: 10
                                        color: Theme.orange
                                    }
                                }
                            }
                            Text {
                                visible: epRow.hovering && DeviceService.connected
                                text: page.deviceMode ? "save" : "to zune"
                                font.pixelSize: 11
                                color: qRowArea.containsMouse
                                       ? Theme.activePink : Theme.pink
                                MouseArea {
                                    id: qRowArea
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.transferEpisode(epRow.row)
                                }
                            }
                            Text {
                                visible: !page.deviceMode
                                text: epRow.row.watched ? "✔" : "○"
                                font.pixelSize: 13
                                color: epRow.row.watched
                                       ? Theme.pink : Theme.textDim
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.markWatched([epRow.row], !epRow.row.watched)
                                }
                            }
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: Theme.separator
                        }
                        EpisodeGesture {
                            anchors.fill: parent
                            z: -1
                            ep: epRow.row
                        }
                    }
                }
            }
        }
    }

    ZuneMenu {
        id: seasonMenu
        ZuneMenuItem {
            text: "Play Season"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: page.playSeason()
        }
        ZuneMenuItem {
            text: page.deviceMode ? "Save Season to Library" : "Queue Season for Zune"
            visible: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: page.transferSeason()
        }
        ZuneMenuItem {
            text: "Customize Season…"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: { if (!page.deviceMode) page.customizeEpisodesRequested(page.currentEpisodes()) }
        }
        ZuneMenuItem {
            text: "Mark Season Watched"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: page.markWatched(page.currentEpisodes(), true)
        }
        ZuneMenuItem {
            text: page.deviceMode ? "Delete Season from Zune" : "Delete Season from Library"
            visible: !page.deviceMode || DeviceService.connected
            height: visible ? implicitHeight : 0
            danger: true
            onTriggered: page.deleteEpisodes(page.currentEpisodes())
        }
    }

    ZuneMenu {
        id: seriesMenu
        ZuneMenuItem {
            text: "Play"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: page.playNextUp()
        }
        ZuneMenuItem {
            text: page.deviceMode ? "Save Series to Library" : "Queue Series for Zune"
            visible: DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: page.transferSeries()
        }
        ZuneMenuItem {
            text: "Customize…"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: { if (!page.deviceMode) page.customizeRequested(page.seriesKey) }
        }
        ZuneMenuItem {
            text: page.deviceMode ? "Delete Series from Zune" : "Delete Series from Library"
            visible: !page.deviceMode || DeviceService.connected
            height: visible ? implicitHeight : 0
            danger: true
            onTriggered: {
                page.deleteEpisodes(page.allEpisodes())
            }
        }
    }

    // Rule of 3 · way #1: right-click an episode (＋ button and drag are
    // the other two). Shared menu; the row sets its target.
    ZuneMenu {
        id: epMenu
        objectName: "episodeMenu"
        property var ep: ({})
        ZuneMenuItem {
            text: "Play"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: page.playEpisode(epMenu.ep)
        }
        ZuneMenuItem {
            text: "Queue for Zune"
            visible: !page.deviceMode && DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: page.transferEpisode(epMenu.ep)
        }
        ZuneMenuItem {
            text: "Customize…"
            objectName: "episodeCustomize"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: page.customizeEpisode(epMenu.ep)
        }
        ZuneMenuItem {
            text: epMenu.ep.watched === true ? "Mark Unwatched" : "Mark Watched"
            visible: !page.deviceMode
            height: visible ? implicitHeight : 0
            onTriggered: page.markWatched([epMenu.ep], !(epMenu.ep.watched === true))
        }
        ZuneMenuItem {
            text: "Save to Library"
            visible: page.deviceMode && DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: page.transferEpisode(epMenu.ep)
        }
        ZuneMenuItem {
            text: "Rename on Zune…"
            visible: page.deviceMode && DeviceService.connected
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (page.deviceMode && DeviceService.connected) page.renameEpisodeRequested(epMenu.ep)
            }
        }
        ZuneMenuItem {
            text: page.deviceMode ? "Delete from Zune" : "Delete from Library"
            visible: !page.deviceMode || DeviceService.connected
            height: visible ? implicitHeight : 0
            danger: true
            onTriggered: page.deleteEpisode(epMenu.ep)
        }
    }
}
