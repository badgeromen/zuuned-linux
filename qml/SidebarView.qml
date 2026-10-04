import QtQuick
import QtQuick.Layouts

// Floating glass nav panel — port of SidebarView.swift. Text-only nav:
// resting 16 light → hover 18 orange → active 22 pink, animated.
// Draws no background — the container wraps it in GrungeGlass, matching
// the mac's outside-applied .glassGrunge.
Item {
    id: root

    property string currentPage: "music"
    // The music browser's live pivot — "Playlists" means the playlists
    // nav entry owns the highlight, not music (same page, two homes).
    property string musicPivot: ""
    // ◉ now-playing island state (Main owns it) — the way back to the
    // migrated ribbon, since the island never locks you in
    property bool nowPlayingActive: false
    readonly property string activePage:
        nowPlayingActive ? "nowPlaying"
        : currentPage === "music"
            ? (musicPivot === "Playlists" ? "libraryPlaylists" : "music")
            : currentPage
    // UX-2: while the playlist tray floats above this island, the text
    // nav collapses to a slim icon rail on the left edge so navigation
    // never dies mid-build.
    property bool railMode: false
    // Narrow-window auto-rail (vs tray/island rail): shows the «
    // expand affordance; full nav at narrow width shows » compress
    property bool narrowRail: false
    property bool compressVisible: false
    readonly property int railWidth: 36
    signal pageSelected(string page)
    signal expandRequested()
    signal compressRequested()
    // Drag device→library: Zune items dropped ANYWHERE on this island.
    // The dragged item's own type routes it (a song → music, a video →
    // videos, a photo → photos); Main.qml does the dedup + extract +
    // toast. The whole island is the target — you never aim at a word.
    signal deviceItemsDropped(string page, var items, string name)
    // Live drop state (drives the island glow + the matching entry's
    // "+ add to library" highlight).
    property bool dropActive: false
    property string dropPage: ""
    function pageForKeys(keys) {
        if (keys.indexOf("zuuned-device-tracks") >= 0) return "music"
        if (keys.indexOf("zuuned-device-videos") >= 0) return "videos"
        if (keys.indexOf("zuuned-device-photos") >= 0) return "photos"
        if (keys.indexOf("zuuned-device-playlist") >= 0) return "libraryPlaylists"
        return ""
    }

    // Zune HD browse-menu treatment, mirrored from DevicePanel's
    // BrowseButton so the two flanking islands read as a pair —
    // slightly smaller ramp (the library island is narrower).
    component NavItem: Item {
        id: item
        property string title
        property string page
        property bool hovering: false
        // Heartbeat dot — pulses while music PLAYS, so the entry
        // announces itself before anyone clicks it.
        property bool pulsing: false
        // A device item of THIS entry's type is being dragged over the
        // island — the whole island is the drop zone (below), and the
        // matching entry lights up to show where the drop will land.
        readonly property bool dropHover:
            root.dropActive && root.dropPage === page
        readonly property bool active: root.activePage === page

        Layout.fillWidth: true
        implicitHeight: navLabel.implicitHeight + Theme.spaceXs * 2

        Text {
            id: navLabel
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            text: item.dropHover ? "+ add to library" : item.title
            font.pixelSize: item.dropHover ? 22
                            : (item.active ? 30 : (item.hovering ? 24 : 20))
            font.weight: item.active ? Font.Normal : Font.Light
            color: item.dropHover ? Theme.green
                   : (item.active ? Theme.activePink
                                  : (item.hovering ? Theme.orange : Theme.textSubtle))

            Behavior on font.pixelSize {
                NumberAnimation { duration: Theme.motionFast; easing.type: Easing.InOutQuad }
            }
            Behavior on color {
                ColorAnimation { duration: Theme.motionFast }
            }
        }
        Text {
            visible: item.pulsing
            anchors.left: navLabel.right
            anchors.leftMargin: Theme.spaceXs
            anchors.verticalCenter: navLabel.verticalCenter
            text: "◉"
            font.pixelSize: 11
            color: Theme.activePink
            SequentialAnimation on scale {
                running: item.pulsing
                loops: Animation.Infinite
                NumberAnimation { to: 1.3; duration: 650; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 650; easing.type: Easing.InOutSine }
            }
            onScaleChanged: if (!item.pulsing && scale !== 1) scale = 1
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: item.hovering = true
            onExited: item.hovering = false
            onClicked: root.pageSelected(item.page)
        }

    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        opacity: root.railMode ? 0 : 1
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceXxs
            text: "LIBRARY"
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 3
            color: Theme.textDim
        }

        // Word block sits in the MIDDLE of the island, like the device
        // panel's browse menu — the eyebrow stays pinned at the top.
        Item { Layout.fillHeight: true }

        // LIBRARY = local pages. playlists routes to the LIBRARY
        // playlists (music page's Playlists pivot) — UX-2 made the
        // library the home of playlists; the zune's own live under the
        // device panel like every other device page.
        NavItem { title: "music"; page: "music" }
        NavItem { title: "videos"; page: "videos" }
        NavItem { title: "photos"; page: "photos" }
        NavItem { title: "playlists"; page: "libraryPlaylists" }
        NavItem {
            title: "now playing"
            page: "nowPlaying"
            visible: PlayerService.queue.length > 0
            pulsing: PlayerService.playing
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceMd
            Layout.rightMargin: Theme.spaceMd
            Layout.topMargin: Theme.spaceMd
            Layout.bottomMargin: Theme.spaceMd
            height: 1
            color: Theme.cardHover
        }

        NavItem { title: "settings"; page: "settings" }

        Item { Layout.fillHeight: true }

        // compress (narrow + pinned open) — shrinks back LEFT: «
        Text {
            visible: root.compressVisible
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.spaceMd
            text: "«"
            font.pixelSize: 14
            color: compressArea.containsMouse ? Theme.textPrimary : Theme.textDim
            MouseArea {
                id: compressArea
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.compressRequested()
            }
        }
    }

    // ── Icon rail (railMode) — the nav, condensed to glyphs so the
    // floating tray can take the island without stranding you ──
    component RailIcon: Item {
        id: rail
        property string glyph
        property string page
        property bool hovering: false
        property bool pulsing: false
        property bool drawGear: false   // clean drawn gear instead of an emoji
        readonly property bool active: root.activePage === page
        readonly property color iconColor: rail.active ? Theme.activePink
                               : (rail.hovering ? Theme.orange : Theme.textMid)
        readonly property real iconSize: rail.active ? 20 : (rail.hovering ? 19 : 16)

        Layout.fillWidth: true
        implicitHeight: 40

        Text {
            visible: !rail.drawGear
            anchors.centerIn: parent
            text: rail.glyph
            // Heartbeat while its surface is live (the ◉ island)
            SequentialAnimation on scale {
                running: rail.pulsing
                loops: Animation.Infinite
                NumberAnimation { to: 1.3; duration: 650; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 650; easing.type: Easing.InOutSine }
            }
            onScaleChanged: if (!rail.pulsing && scale !== 1) scale = 1
            font.pixelSize: rail.iconSize
            // Brighter resting tone than the text nav — the rail is
            // tiny, so it can't afford to whisper (design review #2).
            color: rail.iconColor
            Behavior on font.pixelSize {
                NumberAnimation { duration: Theme.motionBase; easing.type: Easing.InOutQuad }
            }
            Behavior on color { ColorAnimation { duration: Theme.motionBase } }
        }

        // Drawn gear (settings) — clean & monochrome, no color emoji.
        Canvas {
            id: gearCv
            visible: rail.drawGear
            anchors.centerIn: parent
            width: rail.iconSize + 5
            height: rail.iconSize + 5
            Behavior on width { NumberAnimation { duration: Theme.motionBase } }
            Behavior on height { NumberAnimation { duration: Theme.motionBase } }
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2
                const R = Math.min(width, height) / 2 - 1
                const teeth = 8
                const toothLen = R * 0.30
                const innerR = R - toothLen
                ctx.fillStyle = rail.iconColor
                for (let i = 0; i < teeth; i++) {
                    ctx.save()
                    ctx.translate(cx, cy)
                    ctx.rotate(i * Math.PI * 2 / teeth)
                    ctx.fillRect(-R * 0.14, -R, R * 0.28, toothLen + 1.5)
                    ctx.restore()
                }
                ctx.beginPath(); ctx.arc(cx, cy, innerR, 0, 2 * Math.PI); ctx.fill()
                // punch the centre hole
                ctx.globalCompositeOperation = "destination-out"
                ctx.beginPath(); ctx.arc(cx, cy, innerR * 0.42, 0, 2 * Math.PI); ctx.fill()
                ctx.globalCompositeOperation = "source-over"
            }
            Connections {
                target: rail
                function onIconColorChanged() { gearCv.requestPaint() }
            }
            Component.onCompleted: requestPaint()
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: rail.hovering = true
            onExited: rail.hovering = false
            onClicked: root.pageSelected(rail.page)
        }
    }

    ColumnLayout {
        // Tray/island mode: hug the left edge (the card sits beside).
        // Narrow mode: center within the 64px rail glass — the HOST
        // container stays 240 wide (pinned), so anchor by inset.
        anchors.left: parent.left
        anchors.leftMargin: root.narrowRail ? (64 - root.railWidth) / 2 : 0
        Behavior on anchors.leftMargin {
            NumberAnimation { duration: Theme.motionSlow }
        }
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.railWidth
        spacing: 0
        opacity: root.railMode ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.motionBase } }

        Item { Layout.fillHeight: true }
        RailIcon { glyph: "♪"; page: "music" }
        RailIcon { glyph: "▶"; page: "videos" }
        RailIcon { glyph: "▣"; page: "photos" }
        RailIcon { glyph: "♫"; page: "libraryPlaylists" }
        RailIcon {
            glyph: "◉"
            page: "nowPlaying"
            visible: PlayerService.queue.length > 0
            // Pulse while music PLAYS — not while the island is open;
            // the heartbeat is the tell BEFORE you click (Orson).
            pulsing: PlayerService.playing
        }
        Item { Layout.preferredHeight: Theme.spaceMd }
        RailIcon { drawGear: true; page: "settings" }
        Item { Layout.fillHeight: true }

        // expand (narrow auto-rail only) — LEFT island grows to the
        // RIGHT, so the chevron points »
        Text {
            visible: root.narrowRail
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.spaceMd
            text: "»"
            font.pixelSize: 14
            color: expandArea.containsMouse ? Theme.textPrimary : Theme.textDim
            MouseArea {
                id: expandArea
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expandRequested()
            }
        }
    }

    // ── Whole-island drop zone (device → library) ──
    // Accepts any device drag anywhere on the island; the item's own key
    // routes it to the right library section. The glow says "drop here",
    // the matching entry's "+ add to library" says where it lands.
    Rectangle {
        // Track the VISIBLE island. This view stays 240 wide so its
        // contents never reflow, but the narrow rail clips it to 64 —
        // filling the parent put the glow's right border outside the
        // clip, so it read as an unclosed box (Orson 2026-09-12).
        // Flush to the island edges and on the glass's own radius —
        // the same treatment as the device panel's drop glow, so both
        // sides read identically. Inset margins made this one float
        // loose inside the narrow rail (Orson 2026-09-12).
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.narrowRail ? 64 : root.width
        Behavior on width {
            NumberAnimation { duration: Theme.motionSlow
                              easing.type: Easing.InOutQuad }
        }
        radius: Theme.radiusLg
        color: Qt.alpha(Theme.green, 0.06)
        border.width: 2
        border.color: Theme.green
        opacity: root.dropActive ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
        z: 40
    }
    DropArea {
        anchors.fill: parent
        enabled: DeviceService.connected
        z: 41
        keys: ["zuuned-device-tracks", "zuuned-device-videos",
               "zuuned-device-photos", "zuuned-device-playlist"]
        onEntered: drag => {
            root.dropPage = root.pageForKeys(drag.keys)
            root.dropActive = root.dropPage !== ""
        }
        onExited: root.dropActive = false
        onDropped: drop => {
            root.dropActive = false
            if (!DeviceService.connected) { drop.accepted = false; return }
            const page = root.pageForKeys(drop.keys)
            if (page !== "" && drop.source && drop.source.dragPayload) {
                // Track rows carry an array; cards carry a lazy () => [...].
                let items = drop.source.dragPayload
                if (typeof items === "function")
                    items = items()
                if (items && items.length > 0) {
                    root.deviceItemsDropped(page, items,
                                            drop.source.dragName ?? "")
                    drop.accept()
                }
            }
        }
    }
}
