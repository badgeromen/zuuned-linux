import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zuuned

// App chrome — port of ContentView.swift: full-width rotated gradient
// header, 3-column body (floating nav | content | floating device panel).
ApplicationWindow {
    id: window
    width: 1280
    height: 800
    // Mac parity: ContentView's window floor — below this the 3-column
    // chrome (244px sidebar + 320px panel slot) starves the content.
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: "Zuuned"
    color: Theme.bg

    // Two page sets, as on the mac (Page enum): the sidebar's LIBRARY
    // pages are the LOCAL library; the device panel's browse menu opens
    // device pages showing what's on the Zune.
    property string currentPage: "music"
    // N4: the migrating ribbon — while the island is open the top
    // ribbon fades out (one object, never two). Narrow windows retire
    // the ribbon entirely; ◉ in the sidebar is the way back.
    property bool showNowPlaying: false
    // Drafts stay independent. Navigation and explicit additions choose which
    // workbench is visible; changing pages never discards either draft.
    property string builderFocus: "photo"
    readonly property bool photoBuilderActive: PhotoTrayState.building
        && (!TrayState.building || builderFocus === "photo")
    readonly property bool anyBuilderActive: TrayState.building || PhotoTrayState.building
    Connections {
        target: PhotoTrayState
        function onSaved(id) { toastHost.show("photo album saved") }
        function onBuildingChanged() { if (PhotoTrayState.building) window.builderFocus = "photo" }
        function onPulsed() { window.builderFocus = "photo" }
    }
    Connections {
        target: TrayState
        function onBuildingChanged() { if (TrayState.building) window.builderFocus = "music" }
        function onPulsed() { window.builderFocus = "music" }
    }

    // Teach the island (fresh-run feedback): on narrow windows the
    // ribbon is retired, so a first-time user who hits play sees…
    // nothing move. When playback STARTS from stopped on a narrow
    // window, open the island so the user sees what happened and
    // learns where now-playing lives. Track advances never re-open it
    // — closing it stays closed for the rest of the queue.
    property bool _npWasStopped: true
    Connections {
        target: PlayerService
        function onStateChanged() {
            if (PlayerService.playing && window._npWasStopped
                && window.ribbonRetired)
                window.showNowPlaying = true
            window._npWasStopped = PlayerService.stopped
        }
    }

    // Music volume: restored at launch, persisted on change (mute is
    // session-only, matching the video player).
    Component.onCompleted: {
        PlayerService.volume = AppSettings.playerVolume
        // QA harness (screenshot runs): `--page settings` opens there
        const args = Qt.application.arguments
        const i = args.indexOf("--page")
        if (i >= 0 && i + 1 < args.length
            && pageOrder.indexOf(args[i + 1]) >= 0)
            currentPage = args[i + 1]
        const c = args.indexOf("--collapse")
        if (c >= 0 && c + 1 < args.length) {
            const v = parseFloat(args[c + 1])
            if (!isNaN(v))
                collapseOverride = v
        }
        // Space-scarce launch: play the greeting, then tuck (a QA
        // pin owns the chrome — no greeting under it)
        if (spaceScarce && collapseOverride < 0)
            greetingTimer.start()
    }
    Connections {
        target: PlayerService
        function onVolumeChanged() {
            AppSettings.playerVolume = PlayerService.volume
        }
    }
    readonly property bool ribbonRetired: width < 1280
    // Panel rail (narrow windows): condenses unless the user pins the
    // full panel back open; the pin resets when the window widens.
    property bool panelRailPinnedOpen: false
    readonly property bool panelRailMode:
        width < 1200 && showDevicePanel && !panelRailPinnedOpen
    // Library sidebar mirrors the panel: auto-rail when narrow, «/»
    // pin toggles while narrow, pin resets when the window widens
    property bool sidebarRailPinnedOpen: false
    readonly property bool sidebarNarrowRail:
        width < 1200 && !sidebarRailPinnedOpen
        && !anyBuilderActive && !showNowPlaying
    onWidthChanged: {
        if (width >= 1200) {
            panelRailPinnedOpen = false
            sidebarRailPinnedOpen = false
        }
    }
    // Hidden until a Zune is detected — the panel SLIDES IN from the
    // right when one arrives (the Connections below). The ▤ header
    // toggle still opens it manually any time.
    property bool showDevicePanel: false

    readonly property var pageOrder: ["music", "videos", "photos", "settings",
                                      "deviceMusic", "deviceVideos",
                                      "devicePictures", "devicePlaylists"]

    // ── Headroom (ux/7): ADAPTIVE collapsing chrome ──
    // The point is that the app FEELS spacious: the giant header is
    // the greeting, not the permanent state (Orson, 2026-09-10).
    // Tucking scales the title 86→40 (it never leaves) and the
    // islands grow upward, ~156px reclaimed. NO scroll coupling —
    // that model was tried and voided (per-surface state popped the
    // header back on short lists; gesture rules misfired on
    // unscrollable ones). The chrome has exactly two inputs:
    //  · WINDOW HEIGHT — space-scarce windows
    //    (< Theme.headroomCompactBelow: 1080p fullscreen, every
    //    short tile) play the launch greeting — giant title for a
    //    beat, then it tucks itself — and rest compact; tall
    //    windows rest on the big header. Crossing the threshold
    //    adopts the new mode's default.
    //  · THE TITLE — clicking it toggles the chrome any time.
    property bool chromeCollapsed: false
    readonly property bool spaceScarce:
        height < Theme.headroomCompactBelow
    onSpaceScarceChanged: {
        greetingTimer.stop()
        chromeCollapsed = spaceScarce
    }
    // The launch greeting (scarce windows): identity as a MOMENT —
    // the giant title says hello, holds, tucks itself. A page
    // switch or title click cuts it short.
    Timer {
        id: greetingTimer
        interval: Theme.headroomGreetingMs
        onTriggered: window.chromeCollapsed = true
    }
    onCurrentPageChanged: {
        builderFocus = currentPage === "photos" ? "photo" : "music"
        if (greetingTimer.running) {
            greetingTimer.stop()
            chromeCollapsed = true
        }
    }
    // QA harness (screenshot runs): `--collapse 0..1` pins the state
    // so both chrome layouts capture on any window
    property real collapseOverride: -1
    property real collapse: collapseOverride >= 0
        ? Math.min(1, collapseOverride) : (chromeCollapsed ? 1 : 0)
    Behavior on collapse {
        NumberAnimation { duration: Theme.motionBase
                          easing.type: Easing.InOutQuad }
    }
    // Scarce windows densify the galleries too: smaller cells, more
    // columns — every grid and the canonical spine reflow from this
    // one pair (see Gallery.plan)
    Binding {
        target: Gallery
        property: "density"
        value: window.spaceScarce ? Theme.galleryScarceDensity : 1.0
    }
    Binding {
        target: Gallery
        property: "extraCols"
        value: window.spaceScarce ? Theme.galleryScarceExtraCols : 0
    }
    // All three body columns share the island top margin
    readonly property int islandTop:
        Math.round(60 - (60 - Theme.headroomIslandTopMin) * collapse)
    readonly property int headroomHeaderH:
        Math.round(110 - (110 - Theme.headroomHeaderMin) * collapse)
    readonly property int headroomHeaderTop:
        Math.round((Theme.spaceHuge + Theme.spaceMd)
                   - (Theme.spaceHuge + Theme.spaceMd
                      - Theme.headroomHeaderTopMin) * collapse)
    // The islands' COLLAPSE-0 height — the GrungeGlass nine-patch hint
    // (fullHeight), so vertical growth during the scroll never
    // regenerates the mask nor stretches the rim bite.
    readonly property real islandFullHeight:
        height - 110 - (Theme.spaceHuge + Theme.spaceMd) - 60 - Theme.spaceXxl
    readonly property var pageTitles: ({
        music: "music", videos: "videos", photos: "photos",
        settings: "settings",
        deviceMusic: "device music", deviceVideos: "device videos",
        // "photos" everywhere (Orson 2026-09-07): the OG Zune said
        // "pictures", but the library sidebar says photos — one word
        devicePictures: "photos", devicePlaylists: "playlists"
    })

    EnvironmentBackground {
        anchors.fill: parent
        // DevicePresence bloom: spray shifts immediately, warmth blooms
        // at 0.15s, mood color fades in at 0.3s (reverses on disconnect).
        sprayIntensity: !AppSettings.ambientSpray
                        ? 0.0 : (DeviceService.connected ? 0.5 : 0.1)
        warmth: DeviceService.connected ? 1.0 : 0.0
        deviceColor: DeviceService.connected ? DeviceService.moodColor : "transparent"

        Behavior on warmth {
            SequentialAnimation {
                PauseAnimation { duration: 150 }
                NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.InOutQuad }
            }
        }
        Behavior on deviceColor {
            SequentialAnimation {
                PauseAnimation { duration: 300 }
                ColorAnimation { duration: Theme.motionDramatic }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header: giant rotated gradient page title ──
        Item {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceHuge
            Layout.rightMargin: Theme.spaceHuge
            // Drop the header clear of the window's top edge: the 86px
            // marker face, tilted -8° (right end rises), otherwise clips
            // its ascenders against the rounded window top.
            // Headroom: both the drop and the band height shrink with
            // the collapse (60→18, 110→46).
            Layout.topMargin: window.headroomHeaderTop
            implicitHeight: window.headroomHeaderH

            // Measure at full size, then scale to fit: long titles
            // ("device pictures") shrink until they fit a capped width
            // — the cap also bounds how far the -8° rotation can lift
            // the tail above the header. Short titles render at 86
            // exactly as before.
            TextMetrics {
                id: titleMetrics
                font.family: Theme.displayFamily
                font.pixelSize: 86
                font.letterSpacing: 6
                text: window.pageTitles[window.currentPage] ?? window.currentPage
            }
            GradientText {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                text: titleMetrics.text
                font.family: Theme.displayFamily
                // Fit first (long titles shrink to the capped width),
                // THEN the headroom collapse scales the result 86→40.
                // The title only ever scales — it never disappears.
                readonly property real fittedSize: Math.max(34, Math.min(86,
                    86 * Math.min(parent.width - 60, 700)
                       / Math.max(1, titleMetrics.width)))
                font.pixelSize: Math.max(22, Math.round(fittedSize
                    * (86 - (86 - Theme.headroomTitleMin) * window.collapse)
                    / 86))
                tracking: 6
                rotation: -8
                transformOrigin: Item.BottomLeft

                // Headroom: the title IS the chrome toggle — the
                // deliberate control gestures can't cover. Click the
                // small title to bring the big header back (a short
                // list has no top to scroll to), click the giant one
                // to tuck it without scrolling.
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // A click owns the chrome — the pending
                        // greeting tuck must not fire on top of it
                        greetingTimer.stop()
                        window.chromeCollapsed =
                            !window.chromeCollapsed
                    }
                }
            }

            // (W4's header scanning indicator retired by Orson 2026-09-05:
            // scans stay silent up here; live progress lives in Settings →
            // library → rescan.)

            // Device panel toggle (top-right)
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spaceSm
                width: 32
                height: 32
                radius: Theme.radiusMd
                color: "transparent"
                border.width: 1
                border.color: window.showDevicePanel
                              ? Qt.rgba(0.83, 0.21, 0.48, 0.4)
                              : Theme.borderLight

                Text {
                    anchors.centerIn: parent
                    text: "▤"
                    font.pixelSize: 14
                    color: window.showDevicePanel ? Theme.activePink : Theme.textDim
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.showDevicePanel = !window.showDevicePanel
                }
            }
        }

        // ── 3-column body ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Wrapper: the tray must be a SIBLING of the glass, not a
            // child — GrungeGlass clips its content, which cut the
            // card's right overhang and shadow off at the island edge.
            Item {
                // 240 at rest; 64 when the narrow auto-rail kicks in
                // (tray/island keep it full — they float over it).
                property real slotW: window.sidebarNarrowRail ? 64 : 240
                Behavior on slotW {
                    NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.InOutQuad }
                }
                Layout.preferredWidth: slotW
                Layout.fillHeight: true
                Layout.leftMargin: Theme.spaceXxl
                Layout.rightMargin: Theme.spaceXl
                Layout.topMargin: window.islandTop
                Layout.bottomMargin: Theme.spaceXxl
                // While the tray floats, the whole island (card +
                // shadow + right overhang) must paint ABOVE the
                // content column beside it.
                z: window.anyBuilderActive ? 10 : 0

                // The SLIDING SWEEP: this clipper's width follows the
                // animated slot, so the island's right edge physically
                // travels — while inside it the two fixed-size glasses
                // crossfade (grunge never resizes → never stretches or
                // flash-swaps). Slide + dissolve together.
                Item {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.slotW
                    // Clip only when narrowed/sliding — permanent clip
                    // would shear the full glass's glow at rest
                    clip: width < 239.5

                // TWO glasses, both FIXED size, crossfading — each
                // carries the correctly generated mask for its size.
                GrungeGlass {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 240
                    fullHeight: window.islandFullHeight
                    tier: "secondary"
                    grungeLevel: "subtle"
                    seed: 77
                    grungy: DeviceService.connected
                    opacity: window.sidebarNarrowRail ? 0 : 1
                    Behavior on opacity { NumberAnimation { duration: Theme.motionSlow } }
                }
                GrungeGlass {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 64
                    fullHeight: window.islandFullHeight
                    tier: "secondary"
                    grungeLevel: "subtle"
                    seed: 77
                    grungy: DeviceService.connected
                    opacity: window.sidebarNarrowRail ? 1 : 0
                    visible: opacity > 0.01
                    Behavior on opacity { NumberAnimation { duration: Theme.motionSlow } }
                }
                Item {
                    // Nav pinned at full width, left-anchored (the
                    // island's left edge never moves) — contents
                    // crossfade internally, never reflow
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 240

                    SidebarView {
                        id: sidebarNav
                        anchors.fill: parent
                        currentPage: window.currentPage
                        musicPivot: libraryMusicPage.musicPivot
                        // Tray open → nav condenses to the icon rail so
                        // browsing (and adding!) never dies mid-build.
                        railMode: window.anyBuilderActive || window.showNowPlaying
                                  || window.sidebarNarrowRail
                        narrowRail: window.sidebarNarrowRail
                        compressVisible: window.width < 1200
                                         && window.sidebarRailPinnedOpen
                        onExpandRequested: window.sidebarRailPinnedOpen = true
                        onCompressRequested: window.sidebarRailPinnedOpen = false
                        nowPlayingActive: window.showNowPlaying
                        onPageSelected: page => {
                            // "playlists" under LIBRARY = the library's
                            // playlists (music page, Playlists pivot) —
                            // NOT the device page (UX-2).
                            if (page === "nowPlaying") {
                                window.showNowPlaying = !window.showNowPlaying
                            } else if (page === "libraryPlaylists") {
                                window.currentPage = "music"
                                libraryMusicPage.openMusicPivot("Playlists")
                            } else if (page === "music") {
                                // Same page while on the Playlists
                                // pivot — assigning currentPage would
                                // be a no-op and the click felt dead.
                                // music always means the browser root.
                                window.currentPage = "music"
                                if (libraryMusicPage.musicPivot === "Playlists")
                                    libraryMusicPage.openMusicPivot("Artists")
                            } else {
                                window.currentPage = page
                            }
                        }
                        // Drag device→library: pull dropped Zune items
                        // down. Skip anything we already own (and say so),
                        // extract the rest into the matching imports
                        // folder where the scanner picks them up. One
                        // handler covers tracks, videos and photos —
                        // collections arrive pre-expanded into `items`.
                        onDeviceItemsDropped: (page, items, name) => {
                            if (!DeviceService.connected) return
                            // W24 · a device playlist dropped on the
                            // library: recreate it here. Tracks we own join
                            // now; the rest are pulled and auto-join once
                            // scanned (LibraryService keeps them pending).
                            if (page === "libraryPlaylists") {
                                const imported = LibraryService.importDevicePlaylist(
                                    name !== "" ? name : "Imported playlist", items,
                                    DeviceService.deviceTrackRows())
                                if (!imported.success) {
                                    toastHost.show(imported.error)
                                    return
                                }
                                const pulls = imported.itemsToPull || []
                                const toPull = pulls.length
                                const here = imported.here
                                if (toPull > 0)
                                    DeviceService.beginPull("track", toPull, here)
                                for (const e of pulls) {
                                    if (!DeviceService.connected) break
                                    DeviceService.saveTrackToLibrary(
                                        e.itemId, e.title || "", e.artist || "", e.album || "")
                                }
                                toastHost.show("saved playlist ‘" + name + "’ · "
                                    + here + " here"
                                    + (toPull > 0 ? " · pulling " + toPull : ""))
                                return
                            }
                            // Pass 1: split into what we must extract vs.
                            // what we already own — so beginPull knows the
                            // real total BEFORE any extract-done arrives.
                            const noun = page === "videos" ? "video"
                                       : page === "photos" ? "photo" : "track"
                            let toSave = [], existing = 0
                            for (const it of items) {
                                if (!it.itemId || it.itemId <= 0)
                                    continue
                                let have = page === "music"
                                    ? LibraryService.hasTrack(it.artist || "",
                                          it.album || "", it.title || "",
                                          it.discNumber || 0, it.trackNumber || 0,
                                          it.discNumberReliable !== false,
                                          it.trackNumberReliable !== false,
                                          DeviceService.musicIdentityPeers(it))
                                    : page === "videos"
                                    ? LibraryService.hasVideo(it.name || "",
                                          it.series || "", it.season || 0,
                                          it.episode || 0)
                                    : LibraryService.hasPhoto(it.name || "")
                                if (have) existing++
                                else toSave.push(it)
                            }
                            // Arm the "pulling N of M" indicator first, then
                            // fire the extracts it will count down.
                            DeviceService.beginPull(noun, toSave.length, existing)
                            for (const it of toSave) {
                                if (page === "music")
                                    DeviceService.saveTrackToLibrary(
                                        it.itemId, it.title || "",
                                        it.artist || "", it.album || "")
                                else if (page === "videos")
                                    DeviceService.saveVideoToLibrary(
                                        it.itemId, it.name || "", it.series || "")
                                else
                                    DeviceService.savePhotoToLibrary(
                                        it.itemId, it.name || "", "")
                            }
                        }
                    }
                }

                }

                // UX-2 playlist builder — floats ABOVE the nav island
                // with real Z-height: deep drop shadow, lift-in, and a
                // real overhang past the island edge. The icon rail
                // stays clickable to its left.
                NowPlayingIsland {
                    anchors.fill: parent
                    anchors.leftMargin: sidebarNav.railWidth + 8
                    anchors.rightMargin: -30
                    anchors.topMargin: 18
                    anchors.bottomMargin: 18
                    z: 4
                    shown: window.showNowPlaying
                    onCloseRequested: window.showNowPlaying = false
                    artUrlFor: item => LibraryService.artPaths[
                        LibraryService.artKey(item.artist ?? "",
                                              item.album ?? "")] ?? ""
                }

                PlaylistTray {
                    visible: !window.photoBuilderActive && reveal > 0.01
                    anchors.fill: parent
                    // Stacked-card geometry: inset from the island's
                    // top/bottom so the layer BELOW stays visible
                    // around the card, gap after the rail, and a big
                    // overhang past the island's right edge — shifted
                    // on the stack, shadow falling into the gaps.
                    anchors.leftMargin: sidebarNav.railWidth + 8
                    anchors.rightMargin: -56
                    anchors.topMargin: 18
                    anchors.bottomMargin: 18
                }
                Item {
                    anchors.fill: parent
                    visible: window.photoBuilderActive
                    z: 5
                    PhotoAlbumTray {
                        anchors.fill: parent
                        anchors.leftMargin: sidebarNav.railWidth + Theme.spaceSm
                        anchors.rightMargin: -Theme.photoTrayOverhang
                        anchors.topMargin: Theme.photoTrayInset
                        anchors.bottomMargin: Theme.photoTrayInset
                    }
                }
            }

            // Center content — top level with the side islands (60)
            Item {
                id: collectionContent
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: window.islandTop
                Layout.bottomMargin: Theme.spaceXxl
                Layout.rightMargin: window.showDevicePanel ? 0 : Theme.spaceXxxl
                // A tiled window below the minimum can still starve this
                // column — never let overflowing content bleed under the
                // device panel
                clip: true

                // No pivot strip on device pages — the device panel's
                // browse menu IS the device navigation (it's always on
                // screen while a Zune is connected); a second strip
                // here duplicated it and collided with page toolbars.
                //
                // A plain visibility stack, NOT a StackLayout: a
                // StackLayout sizes EVERY child to the widest child's
                // implicit width and overflows its own bounds doing
                // so — one wide page (a header row that won't compress)
                // dragged all pages ~120px wider than this column, so
                // grids centered against the wrong width and the right
                // columns ran under the device panel on narrow windows
                // (Orson 2026-09-10). anchors.fill pins every page to
                // the real column width; collectionContent clips.
                Item {
                    id: pageStack
                    anchors.fill: parent

                    // Local library (sidebar)
                    LocalLibraryPage {
                        id: libraryMusicPage; label: "music"
                        anchors.fill: parent
                        visible: window.currentPage === "music"
                    }
                    LibraryVideosView {
                        anchors.fill: parent
                        visible: window.currentPage === "videos"
                    }
                    LibraryPhotosView {
                        gallery: photoGallery
                        anchors.fill: parent
                        visible: window.currentPage === "photos"
                    }
                    SettingsPage {
                        anchors.fill: parent
                        visible: window.currentPage === "settings"
                    }

                    // Device pages (device panel browse menu) — the Zune's content
                    MusicPage {
                        anchors.fill: parent
                        visible: window.currentPage === "deviceMusic"
                    }
                    DeviceVideosPage {
                        anchors.fill: parent
                        visible: window.currentPage === "deviceVideos"
                    }
                    DevicePicturesPage {
                        gallery: photoGallery
                        anchors.fill: parent
                        visible: window.currentPage === "devicePictures"
                    }
                    PlaylistsPage {
                        anchors.fill: parent
                        visible: window.currentPage === "devicePlaylists"
                    }
                }
            }

            // Panel slot — a wrapper whose ANIMATED plain property drives
            // Layout.preferredWidth. (Behavior on Layout.* attached
            // properties is silently non-functional and broke the layout:
            // the panel stopped reserving space and floated over content.)
            Item {
                id: panelSlot
                // Full slot: left gutter + panel + right gutter
                readonly property real openWidth: Theme.spaceXl + 260 + Theme.spaceXxl
                readonly property real railWidth: Theme.spaceXl + 64 + Theme.spaceXxl
                property real slotWidth: !window.showDevicePanel ? 0
                    : window.panelRailMode ? railWidth : openWidth
                Behavior on slotWidth {
                    NumberAnimation { duration: Theme.motionSlow; easing.type: Easing.InOutQuad }
                }

                Layout.preferredWidth: slotWidth
                Layout.minimumWidth: slotWidth
                Layout.maximumWidth: slotWidth
                Layout.fillHeight: true
                visible: slotWidth > 4
                // Clip only mid-slide — a permanent clip shears the
                // panel's soft glow into hard edges at the slot boundary
                clip: slotWidth < openWidth - 0.5

                // TWO glasses, both FIXED size, crossfading — grunge
                // never resizes → never stretches, never flash-swaps.
                GrungeGlass {
                    id: devicePanelGlass
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceXxl
                    anchors.top: parent.top
                    anchors.topMargin: window.islandTop
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spaceXxl
                    width: 260
                    fullHeight: window.islandFullHeight
                    opacity: window.showDevicePanel
                             && !window.panelRailMode ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.motionSlow }
                    }
                    tier: "accent"
                    grungeLevel: "medium"
                    seed: 42
                    grungy: DeviceService.connected
                    deviceColor: DeviceService.connected ? DeviceService.moodColor : "transparent"
                }
                GrungeGlass {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceXxl
                    anchors.top: parent.top
                    anchors.topMargin: window.islandTop
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spaceXxl
                    width: 64
                    fullHeight: window.islandFullHeight
                    opacity: window.showDevicePanel
                             && window.panelRailMode ? 1 : 0
                    visible: opacity > 0.01
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.motionSlow }
                    }
                    tier: "accent"
                    grungeLevel: "medium"
                    seed: 42
                    grungy: DeviceService.connected
                    deviceColor: DeviceService.connected ? DeviceService.moodColor : "transparent"
                }
                // Content overlay — fixed 260, right-anchored (full
                // content pins right; rail column insets to the 64px
                // glass); everything crossfades, nothing reflows
                Item {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceXxl
                    anchors.top: parent.top
                    anchors.topMargin: window.islandTop
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spaceXxl
                    width: 260
                    opacity: window.showDevicePanel ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Theme.motionSlow }
                    }

                    // UX-2 drag-to-queue: drop a dragged track/album
                    // anywhere on the device panel to queue it for
                    // sync (the queue tab jumps into view on landing).
                    DropArea {
                        id: panelDrop
                        anchors.fill: parent
                        keys: ["zuuned-tracks", "zuuned-playlist",
                               "zuuned-videos", "zuuned-photos"]
                        enabled: DeviceService.connected
                        // Light up the matching browse entry ("＋ zune")
                        // by the item's type — the exact mirror of the
                        // library island lighting up its music/videos/
                        // photos entry. Routes by drag kind.
                        onEntered: drag => {
                            const kind = drag.source.dragKind ?? "tracks"
                            devicePanel.dropPage =
                                  kind === "videos"   ? "deviceVideos"
                                : kind === "photos"   ? "devicePictures"
                                : kind === "playlist" ? "devicePlaylists"
                                : "deviceMusic"
                            devicePanel.dropActive = true
                        }
                        onExited: devicePanel.dropActive = false
                        onDropped: drop => {
                            devicePanel.dropActive = false
                            if (!DeviceService.connected) { drop.accepted = false; return }
                            const src = drop.source
                            const t = src.dragTracks
                            const items = typeof t === "function"
                                          ? t() : (t ?? [])
                            const kind = src.dragKind ?? "tracks"
                            let res
                            if (kind === "playlist")
                                res = SyncEngine.addPlaylist(
                                    src.dragName ?? "playlist", items)
                            else if (kind === "videos")
                                res = SyncEngine.addVideos(items)
                            else if (kind === "photos")
                                res = SyncEngine.addPhotos(items)
                            else
                                res = SyncEngine.addTracks(items)
                            // Honest landing report — fully-on-zune
                            // drops queue NOTHING and say so.
                            const noun = kind === "videos" ? "videos"
                                       : kind === "photos" ? "photos"
                                       : "tracks"
                            if (res.error)
                                toastHost.show(res.error)
                            else if (kind === "playlist" && res.playlistQueued)
                                toastHost.show("queued '" + src.dragName
                                    + "' + " + res.added + " tracks")
                            else if (res.added > 0)
                                toastHost.show("queued " + res.added + " " + noun)
                            else if ((res.onDevice ?? 0) > 0
                                     && res.duplicates === 0)
                                toastHost.show("already on the zune")
                            else if (res.duplicates > 0)
                                toastHost.show("already queued")
                            else if (items.length === 0)
                                toastHost.show("nothing to queue")
                            drop.accept()
                        }
                    }
                    // Island glow while a drag hovers — the matching
                    // browse entry lights up "＋ zune" inside the panel
                    // (DevicePanel.dropActive/dropPage), mirroring the
                    // library island exactly. No banner.
                    Rectangle {
                        // Track the VISIBLE glass, not this fixed-260
                        // content overlay. In rail mode the glass is
                        // 64 wide, so filling the parent drew a drop
                        // highlight ~196px wider than the panel you
                        // can actually see, spilling over the content
                        // column (Orson 2026-09-12).
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: window.panelRailMode ? 64 : 260
                        Behavior on width {
                            NumberAnimation { duration: Theme.motionSlow
                                              easing.type: Easing.InOutQuad }
                        }
                        radius: Theme.radiusLg
                        color: Qt.alpha(Theme.activePink,
                                        panelDrop.containsDrag ? 0.06 : 0)
                        border.width: 2
                        border.color: Qt.alpha(Theme.activePink,
                                               panelDrop.containsDrag ? 0.9 : 0)
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Behavior on border.color { ColorAnimation { duration: 120 } }
                        z: 50
                    }

                    DevicePanel {
                    id: devicePanel
                    anchors.fill: parent
                    railMode: window.panelRailMode
                    onExpandRequested: window.panelRailPinnedOpen = true
                    compressVisible: window.width < 1200
                                     && window.panelRailPinnedOpen
                    onCompressRequested: window.panelRailPinnedOpen = false
                    currentPage: window.currentPage
                    onBrowsePageSelected: page => window.currentPage = page
                    videoCount: DeviceService.videosList.length
                    photoCount: DeviceService.photosList.length
                    playlistCount: DeviceService.playlistsList.length
                    connectionProgress: DeviceService.connectionProgress
                    connectionError: DeviceService.connectionError
                        isLoadingDeviceData: DeviceService.busy
                        loadingStage: DeviceService.connectionStage
                        loadingProgress: DeviceService.connectionProgress
                    }
                }
            }
        }
    }

    ToastHost {
        id: toastHost
        parent: window.Overlay.overlay ?? window.contentItem
        z: Theme.toastOverlayZ
        availableWidth: photoGallery.visible || editorFocused
            ? parent.width - Theme.spaceXxxl
            : Math.min(parent.width - Theme.spaceXxxl,
                       Math.max(Theme.spaceHuge * 4, collectionContent.width - Theme.spaceLg))
        x: Math.max(Theme.spaceLg, Math.min(parent.width - width - Theme.spaceLg,
            photoGallery.visible || editorFocused ? (parent.width - width) / 2
                : collectionContent.parent.mapToItem(parent, collectionContent.x, 0).x
                  + (collectionContent.width - width) / 2))
        anchors.bottom: parent.bottom
        anchors.bottomMargin: photoGallery.visible ? Theme.toastPhotoClearance
            : editorFocused ? Theme.toastModalClearance : Theme.spaceLg
    }

    // W4: a background scan that added new media announces itself, so
    // "found N new" replaces silent success (the thing that made scans
    // feel broken).
    Connections {
        target: DeviceService
        function onUdevRuleInstalled(ok, message) { toastHost.show(message) }
        function onPullToast(message) { toastHost.show(message) }
    }
    Connections {
        target: LibraryService
        function onScanFoundNew(tracks, photos) {
            const parts = []
            if (tracks > 0) parts.push(tracks + (tracks === 1 ? " track" : " tracks"))
            if (photos > 0) parts.push(photos + (photos === 1 ? " photo" : " photos"))
            if (parts.length > 0)
                toastHost.show("added " + parts.join(" and ") + " to your library")
        }
    }

    // Capacity gate: queue adds the Zune can't hold are refused — the
    // modal lists exactly what was left out (vs a vanishing toast).
    CapacityRejectSheet {
        id: capacitySheet
        anchors.centerIn: parent
    }
    Connections {
        target: SyncEngine
        function onCapacityRejected(rejectedItems, addedCount, queuedGB, freeGB) {
            capacitySheet.openWith(rejectedItems, addedCount, queuedGB, freeGB)
        }
        function onSyncSummary(elapsedMs, tracks, videos, photos, skipped, failed) {
            syncCompleteSheet.openWith(elapsedMs, tracks, videos, photos,
                                       skipped, failed)
        }
    }
    SyncCompleteSheet { id: syncCompleteSheet }

    // A live drag needs a landing zone: reveal the device panel while
    // anything is being dragged (drags felt dead when it was closed).
    Connections {
        target: TrayState
        function onDraggingChanged() {
            if (TrayState.dragging && DeviceService.connected
                && !window.showDevicePanel)
                window.showDevicePanel = true
        }
    }

    // The panel slides in from the right the moment a Zune is
    // DETECTED — the connect/MTPZ/scan progress it shows during
    // bring-up IS the plug-in feedback (waiting for the finished view
    // was tried and felt laggy — nothing acknowledged the cable).
    // Latched once per connection: closing it manually stays closed.
    property bool devicePanelAutoOpened: false
    Connections {
        target: DeviceService
        function onStateChanged() {
            if (!DeviceService.devicePresent)
                window.devicePanelAutoOpened = false   // re-arm for next plug-in
            else if (!window.devicePanelAutoOpened) {
                window.devicePanelAutoOpened = true
                window.showDevicePanel = true
            }
        }
    }

    // Video player — fullscreen overlay, permanently mounted (stable
    // identity: the views underneath stay alive), shown while a video
    // plays. Above the ribbon, below onboarding/splash.
    VideoPlayerView {
        anchors.fill: parent
        z: 200
    }

    // Photo gallery — fullscreen viewer shared by library AND device
    // photo pages (Phase 8). Same layer band as the video player.
    PhotoGalleryView {
        id: photoGallery
        anchors.fill: parent
        z: photoGallery.mediaDragging ? 1 : 205
    }

    // Queue ribbon — the mac's top-center overlay pill (40pt down)
    QueueRibbonBar {
        anchors.top: parent.top
        anchors.topMargin: 40
        anchors.horizontalCenter: parent.horizontalCenter
        z: 150
        // Migration spec (locked): fades OUT while the island is open
        // or the window is too narrow — one object traveling
        readonly property bool migrated:
            window.showNowPlaying || window.ribbonRetired
        opacity: migrated ? 0 : 1
        enabled: !migrated
        Behavior on opacity {
            NumberAnimation { duration: Theme.motionSlow }
        }
        player: PlayerService
        artUrlFor: item => LibraryService.artPaths[
                       LibraryService.artKey(item.artist ?? "", item.album ?? "")] ?? ""
        // Contract: while the playlist builder is open the disc click
        // pulses the tray instead of opening now-playing (ignore-and-
        // point, not mode-switch).
        onMiniPlayerRequested: {
            if (TrayState.building)
                TrayState.pulsed()
            else
                window.showNowPlaying = !window.showNowPlaying
        }
        // UX-2: the ribbon's + captures the listening session — the
        // whole play queue lands in the builder as a new playlist.
        onSaveQueueRequested: {
            const q = PlayerService.queue
            if (q.length === 0)
                return
            if (!TrayState.building)
                TrayState.openNew()
            TrayState.addTracks(q.map(it => ({
                id: it.libraryId !== undefined ? it.libraryId : -1,
                title: it.title || "", artist: it.artist || "",
                album: it.album || "", durationMs: it.durationMs || 0,
                filepath: it.filepath || ""
            })))
        }

        // Queue cards need art even for albums no view has browsed yet
        Connections {
            target: PlayerService
            function onQueueChanged() {
                for (const it of PlayerService.queue)
                    LibraryService.requestArt(it.artist ?? "", it.album ?? "",
                                              it.filepath ?? "")
            }
        }
    }

    // Onboarding — shows whenever the library is EMPTY (mac behavior:
    // not just first run; factory reset re-triggers it). Sits under the
    // splash, revealed by its fade.
    property bool showOnboarding: false
    Loader {
        id: onboardingLoader
        anchors.fill: parent
        z: 250
        active: window.showOnboarding
        sourceComponent: OnboardingView {
            onFinished: window.showOnboarding = false
        }
    }

    // Splash — covers everything on launch (min 1.5s), 0.5s fade out,
    // then unloads.
    Loader {
        id: splashLoader
        anchors.fill: parent
        z: 300
        active: true
        sourceComponent: SplashView {
            onFinished: {
                window.showOnboarding = LibraryService.empty
                splashFadeOut.start()
            }
        }
        NumberAnimation {
            id: splashFadeOut
            target: splashLoader
            property: "opacity"
            to: 0
            duration: 500
            onFinished: splashLoader.active = false
        }
    }

    // Floating mini player (ribbon ◉ / mac's ⇧⌘M panel)
    MiniPlayerWindow {
        id: miniPlayer
        visible: false
        artSource: {
            const q = PlayerService.queue
            const i = PlayerService.queueIndex
            if (i < 0 || i >= q.length) return ""
            return LibraryService.artPaths[
                LibraryService.artKey(q[i].artist ?? "", q[i].album ?? "")] ?? ""
        }
    }

    component PlaceholderPage: Item {
        property string label
        Text {
            anchors.centerIn: parent
            text: parent.label + " — coming soon"
            color: Theme.textGhost
            font.family: Theme.displayFamily
            font.pixelSize: 26
        }
    }
}
