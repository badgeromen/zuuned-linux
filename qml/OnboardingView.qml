import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// First-run onboarding wizard — port of OnboardingView.swift: 3
// fullscreen steps (welcome → typed folders + poster source →
// scanning), max content width 600, step dots, back/skip. Folders are
// STAGED locally and only committed as watch folders on "next"; step 3
// kicks the scan and the open button never blocks on it.
//
// Fills its parent, paints Theme.bg, emits finished(); the container
// owns dismissal. Shows whenever the library is empty (mac behavior).
Item {
    id: root

    signal finished()

    property int step: 0
    // Staged {path, type} rows — committed on "next"
    property var addedFolders: []

    // Named step indices — the StackLayout children below sit in this
    // order. Building the flow one page at a time, so these grow; every
    // nav reference (dots, skip, scan trigger) reads them, not literals.
    readonly property int stepWelcome:  0
    readonly property int stepMusic:    1
    readonly property int stepVideo:    2
    readonly property int stepPhotos:   3
    readonly property int stepPlayer:   4
    readonly property int stepBackdrop: 5
    readonly property int stepFont:     6
    readonly property int stepWalk:     7
    readonly property int stepScan:     8
    readonly property int stepCount:    9

    // Folders are staged locally across the three folder screens, then
    // committed ALL AT ONCE when leaving the last one — one batch, one
    // scan pass (per-folder commits triggered a fresh pass each time and
    // the progress bar kept snapping back to 0). The scan then overlaps
    // the make-it-yours steps.
    property bool foldersCommitted: false
    function commitAllFolders() {
        if (root.foldersCommitted || root.addedFolders.length === 0)
            return
        LibraryService.addWatchFolders(root.addedFolders)
        root.foldersCommitted = true
    }
    function countFor(types) {
        return root.addedFolders.filter(f => types.indexOf(f.type) !== -1).length
    }

    function typeColor(t) {
        if (t === "music") return Theme.pink
        if (t === "movies") return Theme.orange
        if (t === "tv") return Theme.info
        if (t === "anime") return Theme.purple
        if (t === "photos") return Theme.green
        return Theme.textSecondary
    }

    function complete() {
        AppSettings.onboardingComplete = true
        root.finished()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }
    // Swallow clicks so the app underneath never reacts
    MouseArea { anchors.fill: parent }

    // Background-scan indicator — pinned to the true top-right corner of
    // the window (not the centred content column) and scaled up on larger
    // displays. Rides along the folder + make-it-yours screens.
    ScanChip {
        id: cornerScan
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Math.round(24 * cornerScan.k)
        anchors.rightMargin: Math.round(28 * cornerScan.k)
        visible: root.step >= root.stepMusic && root.step <= root.stepPlayer
    }

    Item {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(600, parent.width)
        height: parent.height

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item { Layout.fillHeight: true }

            // ── Step content ──
            StackLayout {
                id: stepStack
                Layout.fillWidth: true
                Layout.leftMargin: 60
                Layout.rightMargin: 60
                // A StackLayout's implicit height is the MAX of all
                // steps (hidden ones included), which collapses the
                // centering spacers. Size to the CURRENT step only so
                // the content truly centers.
                Layout.preferredHeight: children[currentIndex]
                                        ? children[currentIndex].implicitHeight : 0
                Layout.maximumHeight: Layout.preferredHeight
                currentIndex: root.step

                // ═══ Step 1: Welcome ═══
                ColumnLayout {
                    spacing: Theme.spaceXl

                    // W7/W15 #1: brand the first impression — the marker
                    // wordmark carries the hello, ZUUNED as the hero.
                    // A plain Column (NOT ColumnLayout) so the negative
                    // spacing that tightens the two lines is honored —
                    // layouts clamp negative margins to zero.
                    Column {
                        spacing: -Theme.spaceMd
                        // 1 · "welcome to" fades in at the top.
                        GradientText {
                            text: "welcome to"
                            font.family: Theme.displayFamily
                            font.pixelSize: 30
                            tracking: 1
                            rotation: -2
                            opacity: 0
                            SequentialAnimation on opacity {
                                running: true
                                // wait out the 500ms splash fade first, so
                                // this actually fades in on screen.
                                PauseAnimation { duration: 700 }
                                NumberAnimation { from: 0; to: 1; duration: 750
                                    easing.type: Easing.OutQuad }
                            }
                        }
                        // 2 · ZUUNED starts as a pixel, then pulls forward
                        //     and grows until it's the full word.
                        GradientText {
                            id: wordmark
                            text: "zuuned"
                            font.family: Theme.displayFamily
                            font.pixelSize: 92
                            tracking: 2
                            rotation: -2
                            transformOrigin: Item.Center
                            scale: 0.02
                            opacity: 0   // nothing there until it materializes
                            SequentialAnimation {
                                running: true
                                PauseAnimation { duration: 1600 }
                                ParallelAnimation {
                                    // appear from nothing…
                                    NumberAnimation {
                                        target: wordmark; property: "opacity"
                                        from: 0; to: 1; duration: 450
                                        easing.type: Easing.OutQuad
                                    }
                                    // …and pull forward, growing to the word.
                                    NumberAnimation {
                                        target: wordmark; property: "scale"
                                        from: 0.02; to: 1.0; duration: 1500
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.2
                                    }
                                }
                            }
                        }
                    }
                    // 3 · everything below fades into the foreground AFTER
                    //     the wordmark lands.
                    ColumnLayout {
                        id: belowGroup
                        Layout.fillWidth: true
                        spacing: Theme.spaceXl
                        opacity: 0
                        SequentialAnimation on opacity {
                            running: true
                            PauseAnimation { duration: 3450 }
                            NumberAnimation { from: 0; to: 1; duration: 850
                                easing.type: Easing.OutQuad }
                        }
                        Text {
                            // W15: promise the flow — folders, then make-it-
                            // yours, then the tour (mixtapes are the showpiece).
                            Layout.topMargin: Theme.spaceSm
                            text: "your zune, at home on linux.\nfolders first, then make it yours, then a quick tour."
                            font.pixelSize: 14
                            font.weight: Font.Light
                            lineHeight: 1.35
                            color: Theme.textSecondary
                        }
                        // W7 #2: acknowledge a Zune that's already plugged in.
                        RowLayout {
                            Layout.topMargin: Theme.spaceSm
                            spacing: Theme.spaceXs
                            visible: DeviceService.connected || DeviceService.devicePresent
                            Text {
                                text: DeviceService.connected ? "✓" : "○"
                                font.pixelSize: 12
                                color: DeviceService.connected ? Theme.green : Theme.textDim
                            }
                            Text {
                                text: DeviceService.connected
                                    ? (DeviceService.modelName !== ""
                                       ? DeviceService.modelName : "Zune") + " detected"
                                    : "Zune detected — connecting…"
                                font.pixelSize: 12
                                font.weight: Font.Light
                                color: DeviceService.connected ? Theme.green : Theme.textSecondary
                            }
                        }
                        PinkCta {
                            Layout.topMargin: Theme.spaceLg
                            label: "get started"
                            onActivated: root.step = root.stepMusic
                        }
                    }
                }

                // ═══ Step 2: Music folder ═══
                ColumnLayout {
                    spacing: Theme.spaceLg

                    Text {
                        text: "where's your music?"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "point us at your music folder — we start scanning it "
                              + "right away so it's ready by the time you're done."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        implicitHeight: musicCard.implicitHeight
                        radius: Theme.radiusMd
                        color: Theme.cardBg
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            id: musicCard
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 0
                            FolderTypeSection { title: "Music"; type: "music" }
                        }
                    }
                    ManualPathRow { typeModel: ["music"] }

                    PinkCta {
                        Layout.topMargin: Theme.spaceSm
                        label: "next"
                        enabled: root.countFor(["music"]) > 0
                        onActivated: root.step = root.stepVideo
                    }
                }

                // ═══ Step 3: Movies, shows & more (skippable) ═══
                ColumnLayout {
                    spacing: Theme.spaceLg

                    Text {
                        text: "movies, shows & more"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "the rest of what the Zune plays. add what you have, "
                              + "skip the rest — only what you point at gets scanned."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        implicitHeight: videoCard.implicitHeight
                        radius: Theme.radiusMd
                        color: Theme.cardBg
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            id: videoCard
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 0
                            FolderTypeSection { title: "Movies"; type: "movies" }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                            FolderTypeSection { title: "TV Shows"; type: "tv" }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                            FolderTypeSection { title: "Anime"; type: "anime" }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                            FolderTypeSection { title: "Other"; type: "other" }
                        }
                    }
                    ManualPathRow { typeModel: ["movies", "tv", "anime", "other"] }

                    // Poster source — artwork for movies & tv
                    ColumnLayout {
                        Layout.topMargin: Theme.spaceXs
                        spacing: Theme.spaceSm
                        Text {
                            text: "POSTER SOURCE"
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 1
                            color: Theme.textDim
                        }
                        RowLayout {
                            spacing: 0
                            component SegBtn: Rectangle {
                                property string label
                                property string tag
                                readonly property bool active: AppSettings.posterSource === tag
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 28
                                radius: Theme.radiusMd
                                color: active ? Theme.cardActive : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: parent.label
                                    font.pixelSize: 12
                                    font.weight: Font.Light
                                    color: parent.active ? Theme.textPrimary : Theme.textSecondary
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: AppSettings.posterSource = parent.tag
                                }
                            }
                            SegBtn { label: "TMDB"; tag: "tmdb" }
                            SegBtn { label: "Fanart.tv"; tag: "fanart" }
                        }
                        Text {
                            text: "source used for movie & tv artwork — changeable in settings later."
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textDim
                        }
                    }

                    PinkCta {
                        Layout.topMargin: Theme.spaceSm
                        label: root.countFor(["movies", "tv", "anime", "other"]) > 0
                               ? "next" : "skip"
                        onActivated: root.step = root.stepPhotos
                    }
                }

                // ═══ Step 4: Photos (skippable) ═══
                ColumnLayout {
                    spacing: Theme.spaceLg

                    Text {
                        text: "your photos?"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "last folder — your image gallery, synced to the "
                              + "Zune's picture library."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        implicitHeight: photosCard.implicitHeight
                        radius: Theme.radiusMd
                        color: Theme.cardBg
                        border.width: 1
                        border.color: Theme.border
                        ColumnLayout {
                            id: photosCard
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 0
                            FolderTypeSection { title: "Photos"; type: "photos" }
                        }
                    }
                    ManualPathRow { typeModel: ["photos"] }

                    Text {
                        text: "you can always add more folders later in settings."
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Theme.textDim
                    }

                    PinkCta {
                        Layout.topMargin: Theme.spaceSm
                        label: root.countFor(["photos"]) > 0 ? "next" : "skip"
                        onActivated: {
                            root.commitAllFolders()
                            root.step = root.stepPlayer
                        }
                    }
                }

                // ═══ Step 3: Player style — Disc vs Vinyl ═══
                // Uses the REAL SpinningDiscView / VinylRecordView (not a
                // mock) driven by a tiny demo clock so both spin live. The
                // choice writes Prefs.playerStyle, which NowPlayingIsland
                // reads to pick which one it shows.
                ColumnLayout {
                    id: playerStep
                    spacing: Theme.spaceXl

                    // Demo player: enough of the PlayerService surface for
                    // both views (playing + advancing position + the scrub
                    // no-ops). Only ticks while this step is visible.
                    QtObject {
                        id: demoPlayer
                        property bool playing: true
                        property real positionMs: 0
                        property real durationMs: 214000
                        property real scrubMs: -1
                        property real seekMs: -1
                        function playPause() {}
                        function beginScrub(ms) {}
                        function endScrubMs(ms) {}
                    }
                    Timer {
                        running: root.step === root.stepPlayer
                        repeat: true
                        interval: 60
                        onTriggered: demoPlayer.positionMs =
                            (demoPlayer.positionMs + 60) % demoPlayer.durationMs
                    }

                    Text {
                        text: "pick your player"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "how now-playing spins while a track plays. "
                              + "switch anytime in settings."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        spacing: Theme.spaceLg

                        // ── Disc option ──
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: discCol.implicitHeight + Theme.spaceXl * 2
                            radius: Theme.radiusLg
                            readonly property bool active: Prefs.playerStyle === "disc"
                            color: active ? Theme.cardActive : Theme.cardBg
                            border.width: active ? 2 : 1
                            border.color: active ? Theme.pink : Theme.border
                            Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }

                            ColumnLayout {
                                id: discCol
                                anchors.centerIn: parent
                                width: parent.width - Theme.spaceXl * 2
                                spacing: Theme.spaceMd

                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 150
                                    implicitHeight: 150
                                    SpinningDiscView {
                                        anchors.centerIn: parent
                                        player: demoPlayer
                                        diameter: 130
                                        artSource: ""
                                    }
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "disc"
                                    font.pixelSize: 18
                                    font.weight: Font.Light
                                    color: Theme.textPrimary
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: "album art on a CD, edge glows with the track."
                                    font.pixelSize: 11
                                    font.weight: Font.Light
                                    color: Theme.textDim
                                    wrapMode: Text.WordWrap
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Prefs.playerStyle = "disc"
                            }
                        }

                        // ── Vinyl option ──
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            implicitHeight: vinylCol.implicitHeight + Theme.spaceXl * 2
                            radius: Theme.radiusLg
                            readonly property bool active: Prefs.playerStyle === "vinyl"
                            color: active ? Theme.cardActive : Theme.cardBg
                            border.width: active ? 2 : 1
                            border.color: active ? Theme.pink : Theme.border
                            Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }

                            ColumnLayout {
                                id: vinylCol
                                anchors.centerIn: parent
                                width: parent.width - Theme.spaceXl * 2
                                spacing: Theme.spaceMd

                                Item {
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 150
                                    implicitHeight: 150
                                    VinylRecordView {
                                        anchors.centerIn: parent
                                        player: demoPlayer
                                        size: 150
                                        dragSeekEnabled: false
                                        posterSource: ""
                                    }
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "vinyl"
                                    font.pixelSize: 18
                                    font.weight: Font.Light
                                    color: Theme.textPrimary
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: "a grooved record, poster on the label, trail seeks."
                                    font.pixelSize: 11
                                    font.weight: Font.Light
                                    color: Theme.textDim
                                    wrapMode: Text.WordWrap
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Prefs.playerStyle = "vinyl"
                            }
                        }
                    }

                    PinkCta {
                        Layout.topMargin: Theme.spaceMd
                        label: "next"
                        onActivated: root.step = root.stepBackdrop
                    }
                }

                // ═══ Step 6: Backdrop — preset or your own image ═══
                // Grime/Dusk are real presets now (EnvironmentBackground
                // veils; grime stays presence-modulated per the core
                // rule). Same pills as Settings → Appearance.
                ColumnLayout {
                    id: backdropStep
                    spacing: Theme.spaceLg

                    readonly property bool hasImage: AppSettings.backgroundImagePath.length > 0

                    Text {
                        text: "pick a backdrop"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "behind the whole app. keep it clean, or drop in your "
                              + "own image — dimmed so text stays readable."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                    }

                    // Live preview
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        implicitHeight: 160
                        radius: Theme.radiusMd
                        color: Theme.bg
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        Image {
                            anchors.fill: parent
                            visible: backdropStep.hasImage
                            source: backdropStep.hasImage
                                    ? "file://" + AppSettings.backgroundImagePath : ""
                            fillMode: Image.PreserveAspectCrop
                            opacity: AppSettings.backgroundOpacity
                            asynchronous: true
                        }
                        GradientText {
                            anchors.centerIn: parent
                            visible: backdropStep.hasImage
                            text: "zuuned"
                            font.family: Theme.displayFamily
                            font.pixelSize: 40
                            tracking: 1
                            rotation: -2
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !backdropStep.hasImage
                            text: AppSettings.backdropPreset === "grime"
                                  ? "grime — deepens while a zune is docked"
                                  : AppSettings.backdropPreset === "dusk"
                                    ? "dusk — quiet purple fade"
                                    : "default ambient"
                            font.pixelSize: 13
                            font.weight: Font.Light
                            color: Theme.textDim
                        }
                    }

                    SegRow {
                        options: [{ label: "None", value: "none" },
                                  { label: "Grime", value: "grime" },
                                  { label: "Dusk", value: "dusk" },
                                  { label: "Your image…", value: "image" }]
                        current: backdropStep.hasImage ? "image"
                                                       : AppSettings.backdropPreset
                        onPicked: function(v) {
                            if (v === "image") {
                                bgImageDialog.open()
                            } else {
                                AppSettings.backgroundImagePath = ""
                                AppSettings.backdropPreset = v
                            }
                        }
                    }

                    // Dimness — only meaningful with an image
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 460
                        visible: backdropStep.hasImage
                        spacing: Theme.spaceMd
                        Text {
                            text: "DIMNESS"
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 1
                            color: Theme.textDim
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.05; to: 0.8
                            value: AppSettings.backgroundOpacity
                            onMoved: AppSettings.backgroundOpacity = value
                        }
                    }

                    PinkCta {
                        Layout.topMargin: Theme.spaceSm
                        // Every preset, including None, is a valid choice.
                        label: "next"
                        onActivated: root.step = root.stepFont
                    }
                }

                // ═══ Step 7: Title face — Permanent Marker or Graffiti ═══
                ColumnLayout {
                    spacing: Theme.spaceLg

                    Text {
                        text: "pick your title face"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "the loud face on identity moments — the wordmark, "
                              + "page titles, now-playing. body text stays clean."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                    }

                    // Live preview — updates as you toggle (Theme.displayFamily
                    // reads AppSettings.headerFont).
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spaceSm
                        implicitHeight: 150
                        radius: Theme.radiusMd
                        color: Theme.cardBg
                        border.width: 1
                        border.color: Theme.border
                        GradientText {
                            anchors.centerIn: parent
                            text: "zuuned"
                            font.family: Theme.displayFamily
                            font.pixelSize: 72
                            tracking: 2
                            rotation: -2
                        }
                    }

                    SegRow {
                        options: [{ label: "Permanent Marker", value: "permanentMarker" },
                                  { label: "Specimen Alien", value: "specimenAlien" }]
                        current: AppSettings.headerFont
                        onPicked: function(v) { AppSettings.headerFont = v }
                    }

                    PinkCta {
                        Layout.topMargin: Theme.spaceSm
                        label: "next"
                        onActivated: root.step = root.stepWalk
                    }
                }

                // ═══ Step 8: Walkthrough — the three ways, both directions ═══
                ColumnLayout {
                    id: walkStep
                    spacing: Theme.spaceLg

                    // Step-1 diagram state. phase 0 = you're viewing your
                    // LIBRARY music, dragging a card onto the Zune; phase 1 =
                    // viewing DEVICE music, dragging a card onto the library.
                    // `dropping` = the card reached the target, so that side
                    // shows its drop-hover highlight (mirrors the real app).
                    property int phase: 0
                    property bool dropping: false

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "three ways, both directions"
                        font.pixelSize: 30
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }

                    // ── Step 1: Drag it — either direction (animated) ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd
                        WalkNum { n: "1" }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Drag it — the pillar you drop on is the action"
                                font.pixelSize: 15
                                font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "onto the Zune → queues it for the next sync. "
                                      + "onto the library → saves it into your library. "
                                      + "the target pillar lights up so you always know "
                                      + "which side you're working."
                                font.pixelSize: 12
                                font.weight: Font.Light
                                color: Theme.textDim
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // The real app chrome: Library sidebar │ content grid │
                    // Zune panel. The side you're viewing shows its music
                    // section pink (source); the OPPOSITE side is the drop
                    // target and lights up on hover.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 190
                        radius: Theme.radiusMd
                        color: Theme.bg
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        // Page title, top-left — "music" (library) ⇄ "device
                        // music" (zune), static marker gradient like the app.
                        GradientText {
                            id: walkTitle
                            anchors.left: libPanel.left
                            anchors.bottom: libPanel.top
                            anchors.bottomMargin: 3
                            text: walkStep.phase === 1 ? "device music" : "music"
                            font.family: Theme.displayFamily
                            font.pixelSize: 16
                            rotation: -2
                        }

                        // Left — Library sidebar (green). Source when phase 0,
                        // drop target when phase 1.
                        WalkPanel {
                            id: libPanel
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.spaceMd
                            title: "LIBRARY"
                            sections: ["music", "videos", "photos", "playlists"]
                            accent: Theme.green
                            dropLabel: "＋ add to library"
                            sourceActive: walkStep.phase === 0
                            dropTarget: walkStep.phase === 1 && walkStep.dropping
                        }
                        // Right — Zune panel (pink). Source when phase 1, drop
                        // target when phase 0.
                        WalkPanel {
                            id: zunePanel
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: Theme.spaceMd
                            title: "ZUNE"
                            battery: true
                            sections: ["music", "videos", "pictures", "playlists"]
                            accent: Theme.pink
                            dropLabel: "＋ zune"
                            sourceActive: walkStep.phase === 1
                            dropTarget: walkStep.phase === 0 && walkStep.dropping
                        }
                        // Center — the content grid (albums)
                        Grid {
                            anchors.centerIn: parent
                            columns: 3
                            rowSpacing: 8
                            columnSpacing: 8
                            Repeater {
                                model: 6
                                Rectangle {
                                    width: 26; height: 26; radius: 5
                                    color: Theme.cardHover
                                    border.width: 1
                                    border.color: Theme.border
                                }
                            }
                        }
                        // The dragged card
                        Rectangle {
                            id: chip
                            width: 28; height: 28; radius: 6
                            y: parent.height / 2 - 14
                            x: parent.width / 2 - 14
                            opacity: 0
                            color: Theme.cardActive
                            border.width: 1
                            border.color: Theme.border

                            readonly property real centerX: parent.width / 2 - 14
                            readonly property real toZuneX: parent.width - 150
                            readonly property real toLibX: 26
                        }

                        SequentialAnimation {
                            running: root.step === root.stepWalk
                            loops: Animation.Infinite
                            // Phase 0: viewing library → drag a card to the Zune
                            ScriptAction { script: {
                                walkStep.phase = 0; walkStep.dropping = false
                                chip.opacity = 0; chip.x = chip.centerX
                            } }
                            NumberAnimation { target: chip; property: "opacity"; to: 1; duration: 300 }
                            ParallelAnimation {
                                NumberAnimation { target: chip; property: "x"; to: chip.toZuneX
                                    duration: 1100; easing.type: Easing.InOutQuad }
                                SequentialAnimation {
                                    PauseAnimation { duration: 800 }
                                    ScriptAction { script: walkStep.dropping = true }
                                }
                            }
                            PauseAnimation { duration: 950 }
                            // Phase 1: viewing device music → drag a card to the library
                            ScriptAction { script: {
                                walkStep.phase = 1; walkStep.dropping = false
                                chip.opacity = 0; chip.x = chip.centerX
                            } }
                            NumberAnimation { target: chip; property: "opacity"; to: 1; duration: 300 }
                            ParallelAnimation {
                                NumberAnimation { target: chip; property: "x"; to: chip.toLibX
                                    duration: 1100; easing.type: Easing.InOutQuad }
                                SequentialAnimation {
                                    PauseAnimation { duration: 800 }
                                    ScriptAction { script: walkStep.dropping = true }
                                }
                            }
                            PauseAnimation { duration: 950 }
                        }
                    }

                    // Live caption — which view you're in and where the drag goes.
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: walkStep.phase === 0
                              ? "browsing your library → drag onto the Zune to sync it over"
                              : "browsing the Zune → drag onto the library to save it in"
                        font.pixelSize: 12
                        font.weight: Font.Light
                        color: walkStep.phase === 0 ? Theme.pink : Theme.green
                    }

                    // ── Step 2: The corner button ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd
                        WalkNum { n: "2" }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "The corner button"
                                font.pixelSize: 15
                                font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "hover any card or row and tap the glass ＋. the "
                                      + "▾ opens a picker to grab just some parts."
                                font.pixelSize: 12
                                font.weight: Font.Light
                                color: Theme.textDim
                                wrapMode: Text.WordWrap
                            }
                        }
                        // demo card with corner split-add button
                        Rectangle {
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 92
                            radius: Theme.radiusMd
                            color: Theme.cardHover
                            border.width: 1
                            border.color: Theme.border
                            Text {
                                anchors.centerIn: parent
                                text: "album"
                                font.pixelSize: 11
                                color: Theme.textDim
                            }
                            Rectangle {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 6
                                width: 34; height: 20; radius: Theme.radiusSm
                                color: Theme.cardActive
                                border.width: 1
                                border.color: Theme.border
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Text { text: "＋"; font.pixelSize: 11; color: Theme.pink }
                                    Text { text: "▾"; font.pixelSize: 9; color: Theme.textDim }
                                }
                            }
                        }
                    }

                    // ── Step 3: Right-click anything ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd
                        WalkNum { n: "3" }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Right-click anything"
                                font.pixelSize: 15
                                font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "song · album · artist · movie · series · season · "
                                      + "episode · music video · photo · Mixtape — one "
                                      + "menu, everywhere."
                                font.pixelSize: 12
                                font.weight: Font.Light
                                color: Theme.textDim
                                wrapMode: Text.WordWrap
                            }
                        }
                        // demo context menu
                        Rectangle {
                            Layout.preferredWidth: 168
                            implicitHeight: menuCol.implicitHeight
                            radius: Theme.radiusMd
                            color: Theme.cardBg
                            border.width: 1
                            border.color: Theme.border
                            Column {
                                id: menuCol
                                width: parent.width
                                Repeater {
                                    model: [{ t: "Add to Sync Queue", d: false },
                                            { t: "Add to Mixtape  ›", d: false },
                                            { t: "Save to Library", d: false },
                                            { t: "Delete from Zune", d: true },
                                            { t: "Edit…", d: false }]
                                    Item {
                                        required property var modelData
                                        required property int index
                                        width: parent.width
                                        height: 26
                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: Theme.spaceMd
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.t
                                            font.pixelSize: 11
                                            color: modelData.d ? Theme.error : Theme.textSecondary
                                        }
                                        Rectangle {
                                            visible: index < 4
                                            anchors.bottom: parent.bottom
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            height: 1
                                            color: Theme.border
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Step 4: Mixtapes ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceMd
                        WalkNum { n: "4" }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Mixtapes"
                                font.pixelSize: 15
                                font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "build one in the tray and sync it over; drag one "
                                      + "off the Zune and we keep what you have, pull "
                                      + "what you don't."
                                font.pixelSize: 12
                                font.weight: Font.Light
                                color: Theme.textDim
                                wrapMode: Text.WordWrap
                            }
                        }
                        // cassette
                        Rectangle {
                            Layout.preferredWidth: 110
                            Layout.preferredHeight: 70
                            radius: Theme.radiusMd
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#1a1420" }
                                GradientStop { position: 1.0; color: "#0f0f13" }
                            }
                            border.width: 1
                            border.color: Theme.border
                            Rectangle {   // label strip
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 10
                                height: 13; radius: 3
                                color: Theme.cardHover
                            }
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 12
                                spacing: 22
                                Repeater {
                                    model: 2
                                    Rectangle {
                                        width: 20; height: 20; radius: 10
                                        color: Theme.bg
                                        border.width: 3
                                        border.color: Theme.cardHover
                                    }
                                }
                            }
                        }
                    }

                    // skip · let's go
                    RowLayout {
                        Layout.topMargin: Theme.spaceSm
                        spacing: Theme.spaceMd
                        Text {
                            text: "skip tour"
                            font.pixelSize: 13
                            font.weight: Font.Light
                            color: walkSkip.containsMouse ? Theme.textSecondary : Theme.textDim
                            MouseArea {
                                id: walkSkip
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                // Only stop on the finishing screen if the scan
                                // is still running; otherwise open straight in.
                                onClicked: {
                                    if (LibraryService.scanning)
                                        root.step = root.stepScan
                                    else
                                        root.complete()
                                }
                            }
                        }
                        PinkCta {
                            label: "let's go"
                            onActivated: {
                                if (LibraryService.scanning)
                                    root.step = root.stepScan
                                else
                                    root.complete()
                            }
                        }
                    }
                }

                // ═══ Step 9: Finishing the scan (only if still running) ═══
                ColumnLayout {
                    id: scanStep
                    spacing: Theme.spaceLg

                    readonly property real fraction:
                        LibraryService.scanTotal > 0
                            ? LibraryService.scanCurrent / LibraryService.scanTotal
                            : (LibraryService.scanning ? 0 : 1)

                    Text {
                        text: !LibraryService.scanning ? "all set"
                              : scanStep.fraction >= 0.55 ? "just finishing your library…"
                              : "scanning your library…"
                        font.pixelSize: 36
                        font.weight: Font.ExtraLight
                        color: Theme.textPrimary
                    }
                    Text {
                        Layout.maximumWidth: 460
                        wrapMode: Text.WordWrap
                        text: LibraryService.scanning
                              ? "you can open Zuuned now — it'll keep going inside."
                              : "your library's ready."
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: Theme.textDim
                    }

                    // Thin pink→orange gradient bar (mock .miniprog)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 470
                        Layout.topMargin: Theme.spaceSm
                        implicitHeight: 7
                        radius: 4
                        color: Theme.cardActive
                        clip: true
                        Rectangle {
                            width: parent.width * scanStep.fraction
                            height: parent.height
                            radius: 4
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Theme.pink }
                                GradientStop { position: 1.0; color: Theme.orange }
                            }
                            Behavior on width { NumberAnimation { duration: Theme.motionFast } }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 470
                        Text {
                            Layout.fillWidth: true
                            text: {
                                if (!LibraryService.scanning)
                                    return "done"
                                var s = LibraryService.scanStage
                                var human = s === "music"    ? "reading your music"
                                          : s === "videos"   ? "reading your videos"
                                          : s === "photos"   ? "reading your photos"
                                          : s === "starting" ? "getting started"
                                          : s
                                if (LibraryService.scanTotal > 0)
                                    human += " · " + LibraryService.scanCurrent
                                           + " of " + LibraryService.scanTotal
                                return human
                            }
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textDim
                            elide: Text.ElideRight
                        }
                        Text {
                            text: Math.round(scanStep.fraction * 100) + "%"
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textDim
                        }
                    }

                    PinkCta {
                        Layout.topMargin: Theme.spaceMd
                        label: LibraryService.scanning ? "open zuuned (scan continues)" : "open zuuned"
                        onActivated: root.complete()
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // ── Navigation: back · dots · skip ──
            Item {
                Layout.fillWidth: true
                Layout.leftMargin: 60
                Layout.rightMargin: 60
                Layout.bottomMargin: 40
                implicitHeight: 24

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.step > 0
                    text: "‹  back"
                    font.pixelSize: 14
                    font.weight: Font.Light
                    color: backArea.containsMouse ? Theme.textPrimary : Theme.textSecondary
                    MouseArea {
                        id: backArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.step -= 1
                    }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spaceSm
                    Repeater {
                        model: root.stepCount
                        Rectangle {
                            required property int index
                            width: 8; height: 8; radius: 4
                            color: index === root.step
                                   ? Theme.pink
                                   : Qt.rgba(1, 1, 1, 0.105)
                        }
                    }
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.step < root.stepScan
                    text: "skip setup  ›"
                    font.pixelSize: 14
                    font.weight: Font.Light
                    color: skipArea.containsMouse ? Theme.textSecondary : Theme.textDim
                    MouseArea {
                        id: skipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.complete()
                    }
                }
            }
        }
    }

    // Scan-as-you-go: folders kick the scan when committed on each folder
    // screen, so the finishing screen just watches it complete. A safety
    // net only — if we somehow land here with watch folders but no scan
    // running (and nothing found yet), start one.
    onStepChanged: {
        if (step === root.stepScan && !LibraryService.scanning
                && LibraryService.empty)
            LibraryService.rescan()
    }

    // ── Pink CTA button (mac's zunePink capsule) ──
    component PinkCta: Rectangle {
        id: cta
        property string label
        signal activated()
        Layout.alignment: Qt.AlignLeft
        implicitWidth: ctaLabel.implicitWidth + Theme.spaceXxl * 2
        implicitHeight: ctaLabel.implicitHeight + Theme.spaceSm * 2
        radius: Theme.radiusMd
        color: enabled
               ? (ctaArea.containsMouse ? Theme.activePink : Theme.pink)
               : Qt.rgba(0.5, 0.5, 0.5, 0.3)

        Text {
            id: ctaLabel
            anchors.centerIn: parent
            text: cta.label
            font.pixelSize: 14
            font.weight: Font.Light
            color: "#ffffff"
        }
        MouseArea {
            id: ctaArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: cta.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (cta.enabled) cta.activated()
        }
    }

    // ── Typed add-folder section: add row + staged folders inline ──
    // (The mac shows an SF Symbol per type; no symbol font here, so the
    // right side is a color-coded type chip matching Settings.)
    // ── Live background-scan indicator (rounded rectangle, not a pill) ──
    // Corner-pinned; `k` scales the whole chip up on larger windows.
    component ScanChip: Rectangle {
        id: chip
        // Barely-there growth: normal on typical displays, at most ~15%
        // larger on a 4K/ultrawide (never the 2× that ballooned it).
        readonly property real k: Math.max(1.0, Math.min(1.15, root.width / 2200))
        readonly property int items: LibraryService.trackCount
                                     + LibraryService.movieCount
                                     + LibraryService.tvEpisodeCount
                                     + LibraryService.photoCount
        readonly property bool busy: LibraryService.scanning
        implicitWidth: chipRow.implicitWidth + Math.round(Theme.spaceLg * k)
        implicitHeight: Math.round(30 * k)
        radius: Theme.radiusMd
        color: Theme.cardBg
        border.width: 1
        border.color: chip.busy ? Qt.rgba(Theme.pink.r, Theme.pink.g, Theme.pink.b, 0.35)
                                : Theme.border

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: Math.round(Theme.spaceSm * chip.k)
            Rectangle {
                width: Math.round(8 * chip.k); height: width; radius: width / 2
                color: chip.busy ? Theme.pink : Theme.green
                SequentialAnimation on opacity {
                    running: chip.busy
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.3; to: 1; duration: 550; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 1; to: 0.3; duration: 550; easing.type: Easing.InOutQuad }
                }
            }
            Text {
                // Presence only — the real progress lives on the finishing
                // screen. No count here that could read as fake progress.
                text: chip.busy
                      ? "scanning your library…"
                      : (chip.items > 0 ? "library ready"
                                        : "we start scanning the moment you continue")
                font.pixelSize: Math.round(11.5 * chip.k)
                font.weight: Font.Light
                color: chip.busy ? Theme.textSecondary
                                 : (chip.items > 0 ? Theme.green : Theme.textDim)
            }
        }
    }

    // ── Manual path entry — network mounts (SMB/NFS, /mnt/…) the picker
    // may not surface. Stages into addedFolders like the pickers do. ──
    component ManualPathRow: RowLayout {
        id: mp
        property var typeModel: ["music"]
        Layout.fillWidth: true
        spacing: Theme.spaceSm

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            radius: Theme.radiusSm
            color: Theme.cardBg
            border.color: mpInput.activeFocus ? Theme.focusRing : Theme.borderFaint
            TextInput {
                id: mpInput
                anchors.fill: parent
                anchors.leftMargin: Theme.spaceSm
                anchors.rightMargin: Theme.spaceSm
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textPrimary
                font.pixelSize: 12
                clip: true
                selectByMouse: true
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: mpInput.text.length === 0
                    text: "…or type a path (e.g. /mnt/MissionControl/" + mp.typeModel[0] + ")"
                    color: Theme.textGhost
                    font.pixelSize: 12
                }
            }
        }
        ComboBox {
            id: mpType
            model: mp.typeModel
            visible: mp.typeModel.length > 1
            implicitWidth: 104
            implicitHeight: 32
            font.pixelSize: 12
        }
        Rectangle {
            implicitWidth: mpAddLabel.implicitWidth + Theme.spaceLg * 2
            implicitHeight: 32
            radius: Theme.radiusSm
            color: mpInput.text.trim().length > 1 ? Theme.pink : Theme.cardBg
            Text {
                id: mpAddLabel
                anchors.centerIn: parent
                text: "add path"
                font.pixelSize: 12
                color: mpInput.text.trim().length > 1 ? "#ffffff" : Theme.textDim
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    const p = mpInput.text.trim()
                    if (p.length > 1) {
                        root.addedFolders = root.addedFolders.concat(
                            [{ path: p, type: mp.typeModel.length > 1
                                             ? mpType.currentText : mp.typeModel[0] }])
                        mpInput.text = ""
                    }
                }
            }
        }
    }

    // ── Walkthrough: numbered step badge ──
    component WalkNum: Rectangle {
        property string n: ""
        Layout.alignment: Qt.AlignTop
        implicitWidth: 26
        implicitHeight: 26
        radius: Theme.radiusSm
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        Text {
            anchors.centerIn: parent
            text: parent.n
            font.pixelSize: 12
            font.weight: Font.Bold
            color: Theme.textDim
        }
    }

    // ── Walkthrough: an island that lights up (hot) as the chip lands ──
    // One side of the app chrome (library sidebar OR zune panel), showing
    // its sections. `sourceActive` = you're viewing this side (its music
    // section goes accent). `dropTarget` = a drag is hovering it, so the nav
    // dims and the accent drop-label takes over with an accent border —
    // exactly the app's drop-hover (green sidebar / pink zune panel).
    component WalkPanel: Rectangle {
        id: pan
        property string title: ""
        property var sections: []
        property color accent: Theme.pink
        property string dropLabel: ""
        property bool battery: false
        property bool sourceActive: false
        property bool dropTarget: false
        width: 118
        height: 150
        radius: Theme.radiusMd
        color: dropTarget ? Qt.rgba(accent.r, accent.g, accent.b, 0.12) : Theme.cardBg
        border.width: dropTarget ? 2 : 1
        border.color: dropTarget ? accent : Theme.border
        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on border.color { ColorAnimation { duration: 150 } }

        // Nav — dims out while this side is the drop target
        Column {
            anchors.fill: parent
            anchors.margins: Theme.spaceSm
            spacing: 3
            opacity: pan.dropTarget ? 0.15 : 1
            Behavior on opacity { NumberAnimation { duration: 150 } }

            Row {
                spacing: 5
                leftPadding: 4
                bottomPadding: 2
                Text {
                    text: pan.title
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 1
                    color: Theme.textDim
                }
                Rectangle {
                    visible: pan.battery
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13; height: 6; radius: 2
                    color: Theme.green
                    opacity: 0.8
                }
            }
            Repeater {
                model: pan.sections
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool act: pan.sourceActive && modelData === "music"
                    width: pan.width - Theme.spaceSm * 2
                    height: 22
                    radius: Theme.radiusSm
                    color: act ? Qt.rgba(pan.accent.r, pan.accent.g,
                                         pan.accent.b, 0.18) : "transparent"
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        font.pixelSize: 12
                        font.weight: parent.act ? Font.DemiBold : Font.Light
                        color: parent.act ? pan.accent : Theme.textSecondary
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                }
            }
        }
        // Drop-hover label (＋ zune / ＋ add to library) over the dimmed nav
        Text {
            anchors.centerIn: parent
            width: parent.width - 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: pan.dropLabel
            font.pixelSize: 13
            font.weight: Font.Bold
            color: pan.accent
            opacity: pan.dropTarget ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
        }
    }

    // ── Combined segmented selector (one rounded-rectangle block, divided,
    // pink underline on the active segment — the look Orson approved). ──
    component SegRow: Rectangle {
        id: seg
        property var options: []        // [{ label, value }]
        property string current: ""
        signal picked(string value)
        implicitHeight: 34
        implicitWidth: segRow.implicitWidth
        radius: Theme.radiusMd
        color: Theme.cardBg
        border.width: 1
        border.color: Theme.border
        clip: true

        Row {
            id: segRow
            height: parent.height
            Repeater {
                model: seg.options
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: segLabel.implicitWidth + Theme.spaceXl
                    height: seg.height

                    Rectangle {
                        anchors.fill: parent
                        color: seg.current === modelData.value ? Theme.cardActive : "transparent"
                    }
                    Rectangle {   // divider between segments
                        visible: index < seg.options.length - 1
                        anchors.right: parent.right
                        width: 1; height: parent.height
                        color: Theme.border
                    }
                    Rectangle {   // active underline
                        visible: seg.current === modelData.value
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Theme.pink
                    }
                    Text {
                        id: segLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: seg.current === modelData.value
                               ? Theme.textPrimary : Theme.textSecondary
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: seg.picked(modelData.value)
                    }
                }
            }
        }
    }

    // ── Typed folder row (mock frow): colored bar + title/status on the
    // left, a rounded-rect browse button on the right. Staged folders of
    // this type list underneath with a remove. ──
    component FolderTypeSection: ColumnLayout {
        id: section
        property string title
        property string type
        readonly property var staged: root.addedFolders.filter(f => f.type === section.type)
        Layout.fillWidth: true
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: 46

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spaceLg
                anchors.rightMargin: Theme.spaceLg
                spacing: Theme.spaceMd

                Rectangle {
                    Layout.preferredWidth: 4
                    Layout.preferredHeight: 30
                    radius: 2
                    color: root.typeColor(section.type)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: section.title
                        font.pixelSize: 14
                        font.weight: Font.Light
                        color: Theme.textPrimary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: section.staged.length === 0
                              ? "add a folder…"
                              : section.staged.length + " folder"
                                + (section.staged.length > 1 ? "s" : "") + " added"
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Theme.textDim
                        elide: Text.ElideRight
                    }
                }
                Rectangle {
                    Layout.preferredHeight: 28
                    implicitWidth: browseLabel.implicitWidth + Theme.spaceLg
                    radius: Theme.radiusSm
                    color: browseArea.containsMouse ? Theme.cardHover : Theme.cardBg
                    border.width: 1
                    border.color: Theme.border
                    Text {
                        id: browseLabel
                        anchors.centerIn: parent
                        text: section.staged.length === 0 ? "browse" : "add"
                        font.pixelSize: 12
                        font.weight: Font.Light
                        color: Theme.textSecondary
                    }
                    MouseArea {
                        id: browseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            folderDialog.pendingType = section.type
                            folderDialog.open()
                        }
                    }
                }
            }
        }

        // Staged folders of this type — path + remove, indented under the bar
        Repeater {
            model: section.staged

            Item {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 24

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceLg + Theme.spaceMd + 4
                    anchors.rightMargin: Theme.spaceLg
                    spacing: Theme.spaceSm

                    Text {
                        Layout.fillWidth: true
                        text: modelData.path
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Theme.textSecondary
                        elide: Text.ElideMiddle
                    }
                    Text {
                        text: "✕"
                        font.pixelSize: 10
                        color: removeArea.containsMouse ? Theme.textSecondary : Theme.textDim
                        MouseArea {
                            id: removeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.addedFolders =
                                root.addedFolders.filter(f => f.path !== modelData.path
                                                           || f.type !== modelData.type)
                        }
                    }
                }
            }
        }
        Item { Layout.fillWidth: true; implicitHeight: section.staged.length > 0 ? Theme.spaceXs : 0 }
    }

    FolderDialog {
        id: folderDialog
        property string pendingType: "music"
        title: "Choose a " + pendingType + " folder"
        onAccepted: {
            console.warn("[onboarding] folder accepted:", selectedFolder)
            const p = decodeURIComponent(
                selectedFolder.toString().replace(/^file:\/\//, ""))
            if (p.length > 1)
                root.addedFolders = root.addedFolders.concat([{ path: p, type: pendingType }])
        }
        onRejected: console.warn("[onboarding] folder dialog rejected/cancelled")
    }

    // Backdrop image picker (mirrors SettingsPage) — sets the live setting.
    FileDialog {
        id: bgImageDialog
        title: "Choose a background image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
            const path = decodeURIComponent(
                selectedFile.toString().replace(/^file:\/\//, ""))
            if (path.length > 1)
                AppSettings.backgroundImagePath = path
        }
    }
}
