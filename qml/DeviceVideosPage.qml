import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// What's ON the Zune — port of DeviceVideosView.swift, gallery
// treatment per the grid study (2026-09-06). Metagenre pivots (Movies /
// TV Shows / Anime / Music Videos / Other — one pivot family with the
// library); TV grouped by the series vendor property (0xDA9A) read at
// connect; posters are the device's own representative samples (the art
// we synced). The Zune has no anime metagenre — the library's anime
// shelf names split the TV pivot. Actions: Save to Library (download
// into ~/Videos/Zuuned Imports) and Delete.
Item {
    id: root

    property string searchText: ""
    property string currentTab: "Movies"
    onCurrentTabChanged: deviceGallery.letterFilter = ""
    // Non-null: {name, episodes: [...]} — the drilled series
    property var openSeries: null
    // Save-all bookkeeping: register the imports watch folder once the
    // last download lands (not per file — each add triggers a rescan).
    property int pendingSaves: 0

    readonly property var videos: DeviceService.videosList

    VideoTitleRepairSheet {
        id: titleRepairSheet
        libraryService: LibraryService
        deviceService: DeviceService
        syncEngine: SyncEngine
        onCompleted: message => toastHost.show(message)
    }

    function matches(text) {
        return searchText === ""
            || text.toLowerCase().includes(searchText.toLowerCase())
    }
    function stripExt(name) {
        return name.replace(/\.(wmv|mp4|m4v|mkv|avi|mov|asf|mpeg|mpg|webm|3gp)$/i, "")
    }
    function ofKind(mg) {
        return videos.filter(v => (v.metagenre ?? 0) === mg
                                  && matches(v.name))
    }
    readonly property var movieVideos: ofKind(0x25)
    readonly property var musicVideos: ofKind(0x23)
    readonly property var otherVideos: videos.filter(
        v => (v.metagenre ?? 0) !== 0x25 && (v.metagenre ?? 0) !== 0x23
             && (v.metagenre ?? 0) !== 0x26 && matches(v.name))

    // TV grouped by the series vendor prop; filename fallback for
    // episodes whose props didn't read.
    readonly property var seriesList: {
        const groups = {}
        for (const v of videos) {
            if ((v.metagenre ?? 0) !== 0x26 || !matches(v.name))
                continue
            const series = (v.series ?? "") !== "" ? v.series : stripExt(v.name)
            const key = series.toLowerCase()
            if (groups[key] === undefined)
                groups[key] = { key: key, name: series, episodes: [] }
            groups[key].episodes.push(v)
        }
        const out = Object.values(groups)
        out.sort((a, b) => a.name.localeCompare(b.name, undefined,
                                                {sensitivity: "base"}))
        for (const g of out)
            g.episodes.sort((a, b) => (a.season ?? 1) - (b.season ?? 1)
                                      || (a.episode ?? 0) - (b.episode ?? 0))
        return out
    }

    // The Zune has no anime metagenre — split the TV groups by the
    // library's anime shelf names (folder truth is the authority).
    VideoBrowserModel {
        id: libShelf
        library: LibraryService
    }
    readonly property var animeNames: {
        const out = {}
        for (const s of libShelf.animeList)
            out[s.name.toLowerCase()] = true
        return out
    }
    readonly property var tvSeriesList:
        seriesList.filter(g => !animeNames[g.key])
    readonly property var animeSeriesList:
        seriesList.filter(g => animeNames[g.key] === true)

    function saveOne(v, subdir) {
        if (!DeviceService.connected) return
        root.pendingSaves++
        DeviceService.saveVideoToLibrary(v.itemId, v.name, subdir ?? "")
    }
    // ▾ picker on device series cards — pick episodes to save
    property var _pickerEpisodes: []
    property string _pickerSeriesName: ""
    QuickPicker {
        id: deviceEpisodePicker
        actionWord: "save"
        onPicked: ids => {
            if (!DeviceService.connected) return
            const set = {}
            for (const i of ids) set[i] = true
            const chosen = root._pickerEpisodes.filter((e, i) => set[String(i)])
            for (const ep of chosen)
                root.saveOne(ep, root._pickerSeriesName)
            toastHost.show("saving " + chosen.length + " episodes…")
        }
    }
    Connections {
        target: DeviceService
        function onVideoSaved(filename, ok) {
            if (root.pendingSaves > 0)
                root.pendingSaves--
            toastHost.show(ok ? "saved " + filename + " to library"
                              : "download failed: " + filename)
            // Last one in: make the imports folder a scanned source.
            if (root.pendingSaves === 0 && ok)
                LibraryService.addWatchFolder(DeviceService.importsDir(), "all")
        }
    }

    // ── Series drill ── (W25: reuse the library VideoSeriesDetail so the
    // device drill-down is the SAME approved layout. A tiny adapter feeds
    // it the Zune's series/episodes in the shape it expects.)
    QtObject {
        id: deviceBrowser
        function seriesDetail(key) {
            const g = (root.seriesList).find(s => s.key === key)
            if (!g) return ({})
            const set = ({})
            for (const ep of g.episodes) set[ep.season ?? 1] = true
            const seasons = Object.keys(set).map(Number).sort((a, b) => a - b)
            const first = g.episodes.length > 0 ? g.episodes[0] : null
            return {
                key: g.key,
                name: g.name,
                seasons: seasons,
                defaultSeason: seasons.length > 0 ? seasons[0] : 0,
                seasonCount: seasons.length,
                episodeCount: g.episodes.length,
                poster: first ? (DeviceService.artPaths[
                    String(first.itemId)] ?? "") : "",
                rating: 0, tmdbTitle: ""
            }
        }
        function episodesForSeason(key, season) {
            const g = (root.seriesList).find(s => s.key === key)
            if (!g) return []
            return g.episodes
                .filter(ep => (ep.season ?? 1) === season)
                .sort((a, b) => (a.episode ?? 0) - (b.episode ?? 0))
                .map(ep => ({
                    id: ep.itemId, itemId: ep.itemId,
                    episode: ep.episode ?? 0, season: ep.season ?? 1,
                    episodeTitle: root.stripExt(ep.name), filename: ep.name,
                    still: DeviceService.artPaths[String(ep.itemId)] ?? "",
                    watched: false, durationMs: 0, episodeRuntime: 0,
                    episodeRating: 0, airDate: "", description: "",
                    isOnDevice: true, filepath: "",
                    series: (ep.series && ep.series !== "") ? ep.series : g.name
                }))
        }
    }
    // ── Page column: band (always) · breadcrumb (drilled) · content ──
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header band: fixed height, pivots on the spine (2026-09-06)
        PageHeaderBand {
            Layout.leftMargin: Gallery.spine(root.width)
            Layout.rightMargin: Theme.spaceXxxl

            PivotBar {
                // Compresses on narrow windows, floored at the tabs'
                // natural width — a fixed reservation inflates this
                // page past its column and clips the grid under the
                // device panel (see LibraryVideosView, 2026-09-11)
                Layout.preferredWidth: Math.min(560,
                    Math.max(implicitWidth, root.width - 300))
                Layout.minimumWidth: implicitWidth
                tabs: ["Movies", "TV Shows", "Anime", "Music Videos", "Other"]
                current: root.currentTab
                onCurrentChanged: {
                    root.currentTab = current
                    root.openSeries = null
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: titleRepairAction
                text: "titles…"
                enabled: DeviceService.connected && !SyncEngine.isSyncing
                    && !DeviceService.busy && !DeviceService.pulling && !DeviceService.purging
                padding: Theme.spaceSm
                Accessible.name: "Use library titles on your Zune"
                contentItem: Text {
                    text: titleRepairAction.text
                    font.pixelSize: Theme.customizeBodySize
                    font.underline: titleRepairAction.hovered
                    color: titleRepairAction.enabled ? Theme.pink : Theme.textDim
                }
                background: Item { }
                onClicked: titleRepairSheet.openForPreview()
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: root.width > 1060
                text: root.movieVideos.length + " movies, "
                      + root.seriesList.length + " tv shows"
                font.pixelSize: 11
                font.weight: Font.Light
                color: Theme.textDim
            }

            ZuneSearchField {
                Layout.preferredWidth: 160
                Layout.minimumWidth: 110
                Layout.alignment: Qt.AlignVCenter
                onTextChanged: root.searchText = text
            }
        }

        // Breadcrumb under the band — tabs never leave
        MusicBreadcrumbBar {
            Layout.fillWidth: true
            visible: root.openSeries !== null
            inset: Gallery.spine(root.width)
            crumbs: [root.currentTab,
                     root.openSeries ? root.openSeries.name : ""]
            onBackClicked: root.openSeries = null
            onCrumbClicked: root.openSeries = null
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spaceMd

            // Empty state — a silent void read as "broken page"
            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spaceSm
                visible: deviceGallery.rows.length === 0
                         && root.openSeries === null
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.searchText !== "" ? "nothing matches"
                        : root.currentTab === "Movies" ? "no movies on this zune"
                        : root.currentTab === "TV Shows" ? "no tv shows on this zune"
                        : root.currentTab === "Anime" ? "no anime on this zune"
                        : root.currentTab === "Music Videos" ? "no music videos on this zune"
                        : "nothing filed under other on this zune"
                    font.pixelSize: 14
                    font.weight: Font.Light
                    color: Theme.textSecondary
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.searchText === ""
                    text: "queue " + (root.currentTab === "TV Shows"
                                      || root.currentTab === "Anime"
                                      ? "episodes" : root.currentTab.toLowerCase())
                          + " from your library and sync to fill this shelf"
                    font.pixelSize: 11
                    color: Theme.textDim
                }
            }

            // Capped-and-centered gallery (grid study 2026-09-06) — the
            // exact same math as the library grids
            GalleryView {
                id: deviceGallery
                anchors.fill: parent
                visible: root.openSeries === null
                idealCell: 190
                maxCols: 8
                // Matches the library video wall — scales down, never
                // lifts the column cap (Hick's law)
                liftColumnCap: false
                anchorX: Gallery.spine(root.width)
                cellHeightFor: (w) => Math.round(w * 1.5) + 56
                nameOf: (r) => r.name ?? ""
                rows: root.currentTab === "Movies" ? root.movieVideos
                    : root.currentTab === "TV Shows" ? root.tvSeriesList
                    : root.currentTab === "Anime" ? root.animeSeriesList
                    : root.currentTab === "Music Videos" ? root.musicVideos
                    : root.otherVideos
                cardDelegate: Item {
                width: deviceGallery.plan.cell
                height: deviceGallery.cellHeightFor(deviceGallery.plan.cell)
                required property var modelData

                readonly property bool isSeries: root.currentTab === "TV Shows"
                                                 || root.currentTab === "Anime"
                readonly property var posterItem: isSeries
                    ? (modelData.episodes.length > 0 ? modelData.episodes[0] : null)
                    : modelData

                PosterCard {
                    posterW: deviceGallery.plan.cell
                    posterSource: {
                        void DeviceService.artPaths   // reactivity
                        if (parent.posterItem === null) return ""
                        const p = DeviceService.artPaths[String(parent.posterItem.itemId)]
                        return p === undefined ? "" : p
                    }
                    title: parent.isSeries ? parent.modelData.name
                                           : root.stripExt(parent.modelData.name)
                    subtitle: parent.isSeries
                        ? parent.modelData.episodes.length
                          + (parent.modelData.episodes.length === 1
                             ? " episode" : " episodes")
                        : (parent.modelData.sizeMB > 0
                           ? Number(parent.modelData.sizeMB).toFixed(0) + " MB" : "")
                    badge: root.currentTab === "Movies" ? "MOVIE"
                         : root.currentTab === "TV Shows" ? "TV"
                         : root.currentTab === "Anime" ? "ANIME"
                         : root.currentTab === "Music Videos" ? "MV" : ""
                    badgeColor: root.currentTab === "Movies" ? Theme.orange
                              : root.currentTab === "Music Videos" ? Theme.activePink
                              : Theme.pink

                    // Device side: no ▶ (can't play without extracting);
                    // quick action is ⤓ save-to-library. Series get the
                    // ▾ episode picker; single videos a plain ⤓.
                    deviceSide: true
                    isContainer: parent.isSeries
                    // Drag device→library: a series card carries all its
                    // episodes; a single video carries itself.
                    dragItems: parent.isSeries
                        ? parent.modelData.episodes.map(ep => ({
                            itemId: ep.itemId, name: ep.name,
                            series: (ep.series && ep.series !== "")
                                    ? ep.series : parent.modelData.name,
                            season: ep.season || 0, episode: ep.episode || 0 }))
                        : [{ itemId: parent.modelData.itemId,
                             name: parent.modelData.name,
                             series: parent.modelData.series || "",
                             season: parent.modelData.season || 0,
                             episode: parent.modelData.episode || 0 }]
                    onOpened: {
                        if (parent.isSeries)
                            root.openSeries = parent.modelData
                    }
                    onAddAll: {
                        if (!DeviceService.connected) return
                        if (parent.isSeries) {
                            for (const ep of parent.modelData.episodes)
                                root.saveOne(ep, parent.modelData.name)
                            toastHost.show("saving "
                                + parent.modelData.episodes.length + " episodes…")
                        } else {
                            root.saveOne(parent.modelData, "")
                            toastHost.show("saving "
                                + root.stripExt(parent.modelData.name) + "…")
                        }
                    }
                    onPickParts: {
                        if (!DeviceService.connected) return
                        // Season-first groups (design pivot)
                        const eps = parent.modelData.episodes
                        root._pickerEpisodes = eps
                        root._pickerSeriesName = parent.modelData.name
                        const bySeason = {}
                        eps.forEach((ep, i) => {
                            const s = ep.season ?? 0
                            if (!bySeason[s]) bySeason[s] = []
                            bySeason[s].push({
                                id: String(i),
                                label: (ep.episode > 0 ? "E" + ep.episode + " · " : "")
                                       + root.stripExt(ep.name),
                                sublabel: ""
                            })
                        })
                        const groups = Object.keys(bySeason)
                            .sort((a, b) => Number(a) - Number(b))
                            .map(s => ({
                                id: "season" + s,
                                label: Number(s) > 0 ? "Season " + s : "Specials",
                                sublabel: bySeason[s].length + " eps",
                                children: bySeason[s]
                            }))
                        deviceEpisodePicker.openAt(parent, parent.modelData.name,
                                                   groups)
                    }
                    onRightClicked: {
                        if (parent.isSeries) {
                            deviceSeriesMenu.target = parent.modelData
                            deviceSeriesMenu.popup()
                        } else {
                            deviceVideoMenu.target = parent.modelData
                            deviceVideoMenu.popup()
                        }
                    }

                    Component.onCompleted: {
                        if (parent.posterItem !== null)
                            DeviceService.requestArt(parent.posterItem.itemId)
                    }
                }
            }
            }

            Loader {
                anchors.fill: parent
                active: root.openSeries !== null
                sourceComponent: VideoSeriesDetail {
                    browser: deviceBrowser
                    seriesKey: root.openSeries ? root.openSeries.key : ""
                    deviceMode: true
                    onBack: root.openSeries = null
                    onSaveEpisodeRequested: ep => {
                        root.saveOne({ itemId: ep.itemId, name: ep.filename },
                                     root.openSeries ? root.openSeries.name : "")
                        toastHost.show("saving " + root.stripExt(ep.filename) + "…")
                    }
                    onSaveSeasonRequested: eps => {
                        for (const ep of eps)
                            root.saveOne({ itemId: ep.itemId, name: ep.filename },
                                         root.openSeries ? root.openSeries.name : "")
                        toastHost.show("saving " + eps.length + " episodes…")
                    }
                    onDeleteEpisodeRequested: ep => {
                        if (DeviceService.connected) DeviceService.purgeItems([ep.itemId])
                    }
                    onRenameEpisodeRequested: ep => {
                        if (DeviceService.connected) renameSheet.openFor(ep.itemId, root.stripExt(ep.filename))
                    }
                }
            }
        }
    }

    // ── Context menus ──
    Menu {
        id: deviceVideoMenu
        property var target: null
        background: Rectangle {
            implicitWidth: 210
            color: Theme.surfaceBg
            border.width: 1
            border.color: Theme.glassBorder
            radius: Theme.radiusLg
        }
        ZuneMenuItem {
            text: "Save to Library"
            enabled: DeviceService.connected
            onTriggered: root.saveOne(deviceVideoMenu.target)
        }
        ZuneMenuItem {
            text: "Rename on Zune…"
            enabled: DeviceService.connected
            onTriggered: {
                if (DeviceService.connected) renameSheet.openFor(
                    deviceVideoMenu.target.itemId, root.stripExt(deviceVideoMenu.target.name))
            }
        }
        ZuneMenuItem {
            text: "Delete from Device"
            danger: true
            enabled: DeviceService.connected
            onTriggered: { if (DeviceService.connected) DeviceService.purgeItems([deviceVideoMenu.target.itemId]) }
        }
    }
    Menu {
        id: deviceSeriesMenu
        property var target: null
        background: Rectangle {
            implicitWidth: 230
            color: Theme.surfaceBg
            border.width: 1
            border.color: Theme.glassBorder
            radius: Theme.radiusLg
        }
        ZuneMenuItem {
            text: "Save All Episodes to Library"
            enabled: DeviceService.connected
            onTriggered: {
                for (const ep of deviceSeriesMenu.target.episodes)
                    root.saveOne(ep, deviceSeriesMenu.target.name)
            }
        }
        ZuneMenuItem {
            text: "Delete Series from Device"
            danger: true
            enabled: DeviceService.connected
            onTriggered: { if (DeviceService.connected) DeviceService.purgeItems(
                deviceSeriesMenu.target.episodes.map(e => e.itemId)) }
        }
    }

    RenameSheet {
        id: renameSheet
        anchors.centerIn: parent
    }
    Connections {
        target: DeviceService
        function onRenameFinished(itemId, newName, ok) {
            toastHost.show(ok ? "renamed to \"" + newName + "\""
                              : "rename failed — see log")
        }
    }

}
