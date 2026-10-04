import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Settings — the W15 mockboard's five sections (Appearance / Library /
// Device / Mixtapes / Health) under the app's PivotBar, flowing Sblk
// sections with segmented-pill (Seg) controls per the Zuuned First-Run
// mock. Deliberate deviations from the mock: PivotBar instead of the
// sidebar nav (house style), and a "Preferred audio language" section
// the mock omits (transcode needs it).
Item {
    id: root

    function typeColor(t) {
        if (t === "music") return Theme.pink
        if (t === "movies") return Theme.orange
        if (t === "tv") return Theme.info
        if (t === "anime") return Theme.purple
        if (t === "photos") return Theme.green
        return Theme.textSecondary
    }

    function pickFolder(t) {
        mediaFolderDialog.pendingType = t
        mediaFolderDialog.open()
    }

    // ═══════════ Reusable pieces ═══════════

    component SectionTitle: Text {
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 3
        color: Theme.textDim
    }

    component CaptionText: Text {
        font.pixelSize: 12
        font.weight: Font.Light
        color: Theme.textDim
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.maximumWidth: 760
    }

    component PhaseNote: Text {
        font.pixelSize: 12
        font.weight: Font.Light
        color: Theme.textGhost
    }

    component GlassCard: Rectangle {
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + Theme.spaceLg * 2
        radius: Theme.radiusLg
        color: Theme.glassBg
        border.color: Theme.glassBorder
        border.width: 1

        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spaceLg
            spacing: Theme.spaceMd
        }
    }

    component ZSlider: Slider {
        id: zs
        implicitWidth: 200
        implicitHeight: 20
        background: Rectangle {
            x: zs.leftPadding
            y: zs.topPadding + zs.availableHeight / 2 - height / 2
            width: zs.availableWidth
            height: 4
            radius: 2
            color: Theme.cardHover
            Rectangle {
                width: zs.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.pink
            }
        }
        handle: Rectangle {
            x: zs.leftPadding + zs.visualPosition * (zs.availableWidth - width)
            y: zs.topPadding + zs.availableHeight / 2 - height / 2
            width: 14; height: 14; radius: 7
            color: zs.pressed ? Theme.activePink : Theme.pink
        }
    }

    // Mock "sblk": a flowing section — title + description + its control(s).
    component Sblk: ColumnLayout {
        property string title: ""
        property string desc: ""
        property bool divider: true
        default property alias content: sblkBody.data
        Layout.fillWidth: true
        spacing: Theme.spaceSm
        // Mock's sblk h3 — quiet 15px, not a shouting header
        Text {
            text: title
            font.pixelSize: 15
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }
        // Prose keeps a readable measure even in the wide frame
        Text {
            visible: desc.length > 0
            text: desc
            font.pixelSize: 13
            font.weight: Font.Light
            color: Theme.textDim
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.maximumWidth: 760
        }
        ColumnLayout {
            id: sblkBody
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXs
            spacing: Theme.spaceMd
        }
        // Separator between sections
        Rectangle {
            visible: parent.divider
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceLg
            implicitHeight: 1
            color: Theme.separator
        }
    }

    // Mock "pills": one combined rounded-rectangle segmented selector,
    // pink underline on the active segment.
    component Seg: Rectangle {
        id: seg
        property var options: []        // [{ label, value }]
        property string current: ""
        signal picked(string value)
        implicitHeight: 38
        implicitWidth: segRow.implicitWidth
        radius: Theme.radiusMd
        color: Theme.glassBg
        border.width: 1
        border.color: Theme.glassBorder
        Row {
            id: segRow
            height: parent.height
            Repeater {
                model: seg.options
                delegate: Item {
                    required property var modelData
                    required property int index
                    // { disabled: true } — visible but inert (e.g. AMD
                    // until VAAPI decode lands)
                    readonly property bool dim: modelData.disabled === true
                    width: segTxt.implicitWidth + Theme.spaceXl
                    height: seg.height
                    // Active segment — inset rounded thumb + pink underline
                    // (the app's convention), no loud border.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 3
                        radius: Theme.radiusSm
                        visible: seg.current === modelData.value
                        color: Theme.cardActive
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            anchors.bottomMargin: 3
                            height: 2
                            radius: 1
                            color: Theme.pink
                        }
                    }
                    Text {
                        id: segTxt
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 13
                        font.weight: Font.Light
                        color: dim ? Theme.textGhost
                                   : seg.current === modelData.value
                                     ? Theme.textPrimary : Theme.textSecondary
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: dim ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: if (!dim) seg.picked(modelData.value)
                    }
                }
            }
        }
    }

    // Toggle switch (mock ".toggle")
    component Sw: Rectangle {
        id: sw
        property bool on: false
        signal toggled()
        implicitWidth: 40
        implicitHeight: 22
        radius: 11
        color: on ? Qt.rgba(Theme.pink.r, Theme.pink.g, Theme.pink.b, 0.35)
                  : Theme.cardHover
        border.width: 1
        border.color: on ? Qt.rgba(Theme.pink.r, Theme.pink.g, Theme.pink.b, 0.5)
                         : Theme.glassBorder
        Behavior on color { ColorAnimation { duration: 120 } }
        Rectangle {
            width: 16; height: 16; radius: 8
            y: 2
            x: sw.on ? sw.width - width - 2 : 2
            color: "#ffffff"
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sw.toggled()
        }
    }

    // Glass button (mock's rounded-rect controls: "export all (.m3u)",
    // "back up now", "show me") — the app convention, radius not a pill.
    // primary = the mock's ".btn.primary" pink tint; small = row-scale
    // ("type"/"remove" on folder rows).
    component GlassButton: Rectangle {
        id: gb
        property string text: ""
        property bool disabled: false
        property bool primary: false
        property bool small: false
        signal clicked()
        implicitHeight: small ? 26 : 34
        implicitWidth: gbTxt.implicitWidth + (small ? Theme.spaceLg : Theme.spaceXl)
        radius: Theme.radiusMd
        opacity: disabled ? 0.4 : 1.0
        color: primary
               ? Qt.rgba(Theme.pink.r, Theme.pink.g, Theme.pink.b,
                         gbArea.containsMouse && !disabled ? 0.22 : 0.14)
               : (gbArea.containsMouse && !disabled ? Theme.cardHover
                                                    : Theme.glassBg)
        border.width: 1
        border.color: primary
                      ? Qt.rgba(Theme.pink.r, Theme.pink.g, Theme.pink.b, 0.55)
                      : Theme.glassBorder
        Text {
            id: gbTxt
            anchors.centerIn: parent
            text: gb.text
            font.pixelSize: gb.small ? 12 : 13
            font.weight: Font.Light
            color: gb.primary ? "#ffffff" : Theme.textPrimary
        }
        MouseArea {
            id: gbArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: gb.disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
            onClicked: if (!gb.disabled) gb.clicked()
        }
    }

    // A section row: label (fills) + right-aligned value/status, with the
    // mock's hairline under each row (`.statrow` border-bottom; set
    // divider: false on the last row of a group). Used by Health.
    component StatRow: ColumnLayout {
        property string label: ""
        property string value: ""
        property color dot: "transparent"
        property color valueColor: Theme.textDim
        property bool divider: true
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: Theme.spaceSm
            spacing: Theme.spaceMd
            Rectangle {
                visible: dot.a > 0
                Layout.alignment: Qt.AlignVCenter
                width: 8; height: 8; radius: 4
                color: dot
            }
            Text {
                Layout.fillWidth: true
                text: label
                font.pixelSize: 13; font.weight: Font.Light
                color: Theme.textPrimary
            }
            Text {
                text: value
                font.pixelSize: 12; font.weight: Font.Light
                color: valueColor
            }
        }
        Rectangle {
            visible: divider
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.separator
        }
    }

    // ═══════════ Page ═══════════

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeaderBand {
            // The app-wide canonical spine — Settings' tabs sit at
            // the same x as every other section's
            Layout.leftMargin: Gallery.spine(root.width)
            Layout.rightMargin: Theme.spaceXl

            PivotBar {
                id: pivots
                Layout.fillWidth: true
                Layout.maximumWidth: 640
                Layout.alignment: Qt.AlignVCenter
                tabs: ["Appearance", "Library", "Device", "Mixtapes", "Health"]
            // QA harness (screenshot runs): `--settings-tab Device`
            Component.onCompleted: {
                const args = Qt.application.arguments
                const i = args.indexOf("--settings-tab")
                if (i >= 0 && i + 1 < args.length
                    && tabs.indexOf(args[i + 1]) >= 0)
                    current = args[i + 1]
            }
            }
            Item { Layout.fillWidth: true }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spaceLg
            contentHeight: stack.implicitHeight + Theme.spaceXxl
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {}

            StackLayout {
                id: stack
                // Settings fills the page's standard frame like every
                // other section (2026-09-06: 760 felt too narrow next
                // to the rest); long prose stays readable via the
                // desc caps in Sblk/CaptionText.
                x: Gallery.spine(root.width)
                width: Math.min(Gallery.frameW(root.width),
                                flick.width - x - Theme.spaceXl)
                currentIndex: pivots.tabs.indexOf(pivots.current)

                // ── APPEARANCE ──
                ColumnLayout {
                    spacing: Theme.spaceXl

                    Sblk {
                        title: "Now-playing player"
                        desc: "how music looks while it spins — everywhere: now-playing, ribbon, mini-player, and video."
                        RowLayout {
                            spacing: Theme.spaceLg
                            Item {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 96
                                clip: true   // contain the seek-head glow — no bleed into the header
                                Loader {
                                    anchors.centerIn: parent
                                    sourceComponent: Prefs.playerStyle === "vinyl" ? sVinyl : sDisc
                                }
                                Component {
                                    id: sDisc
                                    SpinningDiscView {
                                        player: PlayerService
                                        diameter: 78
                                        playGlyphScale: 1.4
                                    }
                                }
                                Component {
                                    id: sVinyl
                                    VinylRecordView {
                                        player: PlayerService
                                        size: 84
                                        chapterCount: 4      // grooves like the ribbon vinyl
                                        labelFraction: 0.24  // small centre label — a real record
                                        playZoneScale: 1.5
                                    }
                                }
                            }
                            Seg {
                                Layout.alignment: Qt.AlignVCenter
                                options: [{ label: "Disc", value: "disc" },
                                          { label: "Vinyl", value: "vinyl" }]
                                current: Prefs.playerStyle
                                onPicked: (v) => Prefs.playerStyle = v
                            }
                        }
                    }

                    Sblk {
                        title: "Artwork print"
                        desc: "your art, your ink — choose a look, then make it yours."
                        ArtworkStyleSettings {
                            sampleSource: {
                                const covers = LibraryService.artPaths
                                const portraits = LibraryService.artistImages
                                const currentKey = LibraryService.artKey(PlayerService.currentArtist,
                                                                         PlayerService.currentAlbum)
                                if (covers[currentKey]) return covers[currentKey]
                                for (const key of Object.keys(covers))
                                    if (covers[key]) return covers[key]
                                for (const key of Object.keys(portraits))
                                    if (portraits[key]) return portraits[key]
                                return ""
                            }
                        }
                    }

                    Sblk {
                        title: "Backdrop"
                        desc: "behind the whole app — keep it clean, or drop in your own image (dimmed)."
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceLg

                            // Image preview (left)
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 200
                                Layout.preferredHeight: 120
                                radius: Theme.radiusMd
                                color: Theme.cardBg
                                border.color: Theme.borderLight
                                border.width: 1
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    visible: AppSettings.backgroundImagePath.length > 0
                                    source: AppSettings.backgroundImagePath.length > 0
                                            ? "file://" + AppSettings.backgroundImagePath : ""
                                    fillMode: Image.PreserveAspectCrop
                                    opacity: AppSettings.backgroundOpacity
                                    asynchronous: true
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: AppSettings.backgroundImagePath.length === 0
                                    text: "default ambient"
                                    font.pixelSize: 12; font.weight: Font.Light
                                    color: Theme.textDim
                                }
                            }

                            // Controls (beside the image)
                            ColumnLayout {
                                Layout.alignment: Qt.AlignTop
                                spacing: Theme.spaceMd
                                Seg {
                                    options: [{ label: "None", value: "none" },
                                              { label: "Grime", value: "grime" },
                                              { label: "Dusk", value: "dusk" },
                                              { label: "Your image…", value: "image" }]
                                    current: AppSettings.backgroundImagePath.length > 0
                                             ? "image" : AppSettings.backdropPreset
                                    onPicked: (v) => {
                                        if (v === "image") {
                                            wallpaperDialog.open()
                                        } else {
                                            AppSettings.backgroundImagePath = ""
                                            AppSettings.backdropPreset = v
                                        }
                                    }
                                }
                                RowLayout {
                                    visible: AppSettings.backgroundImagePath.length > 0
                                    spacing: Theme.spaceMd
                                    Text {
                                        Layout.preferredWidth: 58
                                        text: "dimness"
                                        font.pixelSize: 12; font.weight: Font.Light
                                        color: Theme.textDim
                                    }
                                    ZSlider {
                                        from: 0.05; to: 0.8
                                        value: AppSettings.backgroundOpacity
                                        onMoved: AppSettings.backgroundOpacity = value
                                    }
                                }
                                RowLayout {
                                    visible: AppSettings.backgroundImagePath.length > 0
                                    spacing: Theme.spaceMd
                                    Text {
                                        Layout.preferredWidth: 58
                                        text: "blur"
                                        font.pixelSize: 12; font.weight: Font.Light
                                        color: Theme.textDim
                                    }
                                    ZSlider {
                                        from: 0; to: 50
                                        value: AppSettings.backgroundBlurAmount
                                        onMoved: AppSettings.backgroundBlurAmount = value
                                    }
                                }
                            }
                        }
                    }

                    Sblk {
                        title: "Ambient glow"
                        desc: "the grime particles that bloom behind the app while a Zune is connected."
                        RowLayout {
                            spacing: Theme.spaceMd
                            Sw {
                                on: AppSettings.ambientSpray
                                onToggled: AppSettings.ambientSpray = !AppSettings.ambientSpray
                            }
                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: AppSettings.ambientSpray ? "on" : "off"
                                font.pixelSize: 13
                                font.weight: Font.Light
                                color: Theme.textSecondary
                            }
                        }
                    }

                    Sblk {
                        title: "Letter breaks"
                        desc: "break the grids into A–Z sections with marker headers. Off, the grid stays one clean block and the side rail filters by letter."
                        RowLayout {
                            spacing: Theme.spaceMd
                            Sw {
                                on: AppSettings.letterBreaks
                                onToggled: AppSettings.letterBreaks = !AppSettings.letterBreaks
                            }
                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: AppSettings.letterBreaks ? "sectioned" : "clean grid"
                                font.pixelSize: 13
                                font.weight: Font.Light
                                color: Theme.textSecondary
                            }
                        }
                    }

                    Sblk {
                        title: "Display font"
                        divider: false
                        desc: "the accent face for page titles and identity moments."
                        RowLayout {
                            spacing: Theme.spaceLg
                            GradientText {
                                Layout.alignment: Qt.AlignVCenter
                                text: "zuuned"
                                font.family: Theme.displayFamily
                                font.pixelSize: 30
                                tracking: 1
                            }
                            Seg {
                                Layout.alignment: Qt.AlignVCenter
                                options: [{ label: "Permanent Marker", value: "permanentMarker" },
                                          { label: "Specimen Alien", value: "specimenAlien" }]
                                current: AppSettings.headerFont
                                onPicked: (v) => AppSettings.headerFont = v
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── LIBRARY ──
                ColumnLayout {
                    spacing: Theme.spaceXl

                    Sblk {
                        visible: (LibraryService.musicProbeFailures || []).length > 0
                        title: "Songs waiting for another read"
                        desc: "These files could not be read. Your saved song information stays intact; the next scan will retry."
                        Repeater {
                            model: (LibraryService.musicProbeFailures || []).slice(0, 5)
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.filepath
                                color: Theme.textSecondary
                                font.pixelSize: Theme.customizeBodySize
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                        CaptionText {
                            visible: (LibraryService.musicProbeFailures || []).length > 5
                            text: (LibraryService.musicProbeFailures || []).length + " songs will retry."
                        }
                    }

                    Sblk {
                        title: "Watch folders"
                        desc: "where zuuned scans — music · movies · tv · anime · other · photos"

                        // D2: health strip — only FAILING checks appear.
                        Repeater {
                            model: {
                                void LibraryService.watchFolders
                                void DeviceService.connected
                                const rows = LibraryService.healthChecks()
                                if (DeviceService.devicePresent
                                    && !DeviceService.connected
                                    && !DeviceService.udevRuleOk)
                                    rows.push({
                                        label: "USB permission rule missing",
                                        detail: "Open Settings → Zune → USB access to install or repair the permission rules."
                                    })
                                return rows
                            }
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: hcCol.implicitHeight
                                                + Theme.spaceMd * 2
                                radius: Theme.radiusMd
                                color: Qt.rgba(0.95, 0.68, 0.20, 0.07)
                                border.width: 1
                                border.color: Qt.alpha(Theme.warning, 0.45)
                                ColumnLayout {
                                    id: hcCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.spaceMd
                                    spacing: 2
                                    Text {
                                        text: modelData.label
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        color: Theme.warning
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.detail
                                        font.pixelSize: 10
                                        font.weight: Font.Light
                                        color: Theme.textSecondary
                                        wrapMode: Text.WrapAnywhere
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        // Watch-folder rows (mock foldrow): type chip ·
                        // path over count · glass "type"/"remove"
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Repeater {
                                model: LibraryService.watchFolders

                                Rectangle {
                                    id: wfRow
                                    required property var modelData
                                    // Live count subline — the void reads
                                    // re-run this as scans land.
                                    readonly property var fc: {
                                        void LibraryService.trackCount
                                        void LibraryService.videoCount
                                        void LibraryService.photoCount
                                        return LibraryService.folderCounts(modelData.path)
                                    }
                                    readonly property string countLabel: {
                                        const t = modelData.type
                                        if (t === "music") return fc.tracks + " tracks"
                                        if (t === "movies") return fc.videos + " movies"
                                        if (t === "tv" || t === "anime")
                                            return fc.videos + " episodes"
                                        if (t === "photos") return fc.photos + " photos"
                                        return (fc.tracks + fc.videos + fc.photos) + " items"
                                    }
                                    Layout.fillWidth: true
                                    implicitHeight: 50
                                    color: wfArea.containsMouse ? Theme.rowHoverPink : "transparent"

                                    MouseArea {
                                        id: wfArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.spaceSm
                                        anchors.rightMargin: Theme.spaceSm
                                        spacing: Theme.spaceMd

                                        Rectangle {
                                            implicitWidth: wfChip.implicitWidth + Theme.spaceMd
                                            implicitHeight: 18
                                            radius: 9
                                            color: Qt.rgba(root.typeColor(wfRow.modelData.type).r,
                                                           root.typeColor(wfRow.modelData.type).g,
                                                           root.typeColor(wfRow.modelData.type).b, 0.15)
                                            Text {
                                                id: wfChip
                                                anchors.centerIn: parent
                                                text: wfRow.modelData.type
                                                font.pixelSize: 10
                                                font.weight: Font.Light
                                                color: root.typeColor(wfRow.modelData.type)
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            Text {
                                                Layout.fillWidth: true
                                                text: wfRow.modelData.path
                                                font.pixelSize: 13
                                                font.weight: Font.Light
                                                color: Theme.textPrimary
                                                elide: Text.ElideMiddle
                                            }
                                            Text {
                                                text: wfRow.countLabel
                                                font.pixelSize: 11
                                                font.weight: Font.Light
                                                color: Theme.textDim
                                            }
                                        }

                                        GlassButton {
                                            small: true
                                            text: "type"
                                            onClicked: {
                                                treatAsPopup.folderId = wfRow.modelData.id
                                                treatAsPopup.parent = wfRow
                                                treatAsPopup.open()
                                            }
                                        }
                                        GlassButton {
                                            small: true
                                            text: "remove"
                                            onClicked: LibraryService.removeWatchFolder(wfRow.modelData.id)
                                        }
                                    }

                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: 1
                                        color: Theme.separator
                                    }
                                }
                            }
                        }

                        // mock's add row: quiet label · type pills · browse
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: Theme.spaceMd
                            Text {
                                Layout.fillWidth: true
                                text: "add a folder"
                                font.pixelSize: 13; font.weight: Font.Light
                                color: Theme.textDim
                            }
                            Seg {
                                id: addType
                                options: [{ label: "music", value: "music" },
                                          { label: "movies", value: "movies" },
                                          { label: "tv", value: "tv" },
                                          { label: "anime", value: "anime" },
                                          { label: "photos", value: "photos" }]
                                current: "music"
                                onPicked: (v) => current = v
                            }
                            GlassButton {
                                text: "browse…"
                                onClicked: root.pickFolder(addType.current)
                            }
                        }

                        // …or type a path — network mounts (SMB/NFS) a
                        // picker may not surface; uses the type pills above
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 28
                                radius: Theme.radiusSm
                                color: Theme.cardBg
                                border.color: manualPath.activeFocus ? Theme.focusRing : Theme.borderFaint
                                TextInput {
                                    id: manualPath
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
                                        visible: manualPath.text.length === 0
                                        text: "…or type a path (e.g. /mnt/MissionControl/music)"
                                        color: Theme.textGhost
                                        font.pixelSize: 12
                                    }
                                }
                            }
                            GlassButton {
                                small: true
                                text: "add path"
                                disabled: manualPath.text.trim().length <= 1
                                onClicked: {
                                    LibraryService.addWatchFolder(manualPath.text.trim(),
                                                                  addType.current)
                                    manualPath.text = ""
                                }
                            }
                        }

                    }

                    Sblk {
                        title: "Artwork source"
                        desc: "movie & tv posters, fan art — applies to new lookups"
                        Seg {
                            options: [{ label: "TMDB", value: "tmdb" },
                                      { label: "Fanart.tv", value: "fanart" }]
                            current: AppSettings.posterSource
                            onPicked: (v) => AppSettings.posterSource = v
                        }
                    }

                    // Wired: every scan ends with LibraryDb's resolve
                    // pass; picking here kicks a rescan so it applies
                    // now (and keepBoth resurrects the dropped format).
                    Sblk {
                        title: "Duplicate formats"
                        desc: "when an album ships FLAC + MP3 of the same tracks — applies on the spot"
                        Seg {
                            options: [{ label: "Keep both", value: "keepBoth" },
                                      { label: "Prefer FLAC", value: "flac" },
                                      { label: "Prefer MP3", value: "mp3" }]
                            current: AppSettings.duplicateFormats
                            onPicked: (v) => {
                                AppSettings.duplicateFormats = v
                                LibraryService.applyDuplicateFormats()
                            }
                        }
                    }

                    Sblk {
                        title: "Re-lookup"
                        desc: LibraryService.videoCount + " videos in library, "
                              + LibraryService.unmatchedVideoCount
                              + " without a TMDB match"
                        GlassButton {
                            text: "retry unmatched lookups ("
                                  + LibraryService.unmatchedVideoCount + ")"
                            disabled: LibraryService.unmatchedVideoCount === 0
                            onClicked: LibraryService.requeueAllUnmatched()
                        }
                    }

                    Sblk {
                        title: "Music artwork"
                        desc: "Missing covers are found from your files first, then online. Your existing artwork stays yours."
                        GlassButton {
                            text: "find missing artwork"
                            disabled: LibraryService.trackCount === 0
                            onClicked: LibraryService.retryMissingArtwork()
                        }
                    }

                    Sblk {
                        title: "Rescan"
                        desc: "re-read every watch folder now"
                        divider: false
                        GlassButton {
                            primary: true
                            text: "rescan library"
                            disabled: LibraryService.scanning
                                     || LibraryService.watchFolders.length === 0
                            onClicked: LibraryService.rescan()
                        }
                        // live scan progress
                        ColumnLayout {
                            visible: LibraryService.scanning
                            Layout.fillWidth: true
                            Layout.maximumWidth: 470
                            spacing: Theme.spaceXxs
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 4
                                radius: 2
                                color: Theme.cardHover
                                Rectangle {
                                    width: parent.width * (LibraryService.scanTotal > 0
                                           ? LibraryService.scanCurrent / LibraryService.scanTotal : 0)
                                    height: parent.height
                                    radius: 2
                                    color: Theme.pink
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: LibraryService.scanStage + " — "
                                      + LibraryService.scanCurrent + "/" + LibraryService.scanTotal
                                      + "   " + LibraryService.scanCurrentFile
                                font.pixelSize: 11
                                font.weight: Font.Light
                                color: Theme.textDim
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── DEVICE ──
                ColumnLayout {
                    spacing: Theme.spaceXl

                    Sblk {
                        title: "USB access"
                        desc: "let zuuned own the Zune without sudo each time."
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: Theme.spaceMd
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: DeviceService.udevRuleOk ? "Rule installed" : "Rule missing"
                                    font.pixelSize: 14; font.weight: Font.Light
                                    color: DeviceService.udevRuleOk ? Theme.textPrimary : Theme.warning
                                }
                                Text {
                                    text: "installs with one click (asks for your password)"
                                    font.pixelSize: 11; font.weight: Font.Light
                                    color: Theme.textDim
                                }
                            }
                            GlassButton {
                                visible: !DeviceService.udevRuleOk
                                Layout.alignment: Qt.AlignVCenter
                                primary: true
                                text: "install rule"
                                onClicked: DeviceService.installUdevRule()
                            }
                            // Read-only state light; flipping it while
                            // missing runs the same one-click install.
                            Sw {
                                Layout.alignment: Qt.AlignVCenter
                                on: DeviceService.udevRuleOk
                                onToggled: if (!DeviceService.udevRuleOk)
                                               DeviceService.installUdevRule()
                            }
                        }
                        // Manual fallback if the pkexec prompt is refused
                        TextEdit {
                            visible: !DeviceService.udevRuleOk
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            text: DeviceService.udevInstallCommand
                            selectionColor: Theme.pink
                            font.pixelSize: 11; font.weight: Font.Light; color: Theme.textGhost
                        }
                        Connections {
                            target: DeviceService
                            function onUdevRuleInstalled(ok, message) {
                                toastHost.show(message)
                            }
                        }
                    }

                    Sblk {
                        title: "Transcode acceleration"
                        desc: "hardware video decode when your GPU supports it — ~4x faster transcodes."
                        Seg {
                            options: [{ label: "Auto", value: "auto" },
                                      { label: "NVIDIA", value: "nvenc" },
                                      { label: "AMD", value: "amd" },
                                      { label: "CPU", value: "cpu" }]
                            current: AppSettings.transcodeAccel
                            onPicked: (v) => AppSettings.transcodeAccel = v
                        }
                        PhaseNote { text: "Auto tries NVDEC, then VAAPI, then software. A missing GPU falls back to software either way." }
                    }

                    Sblk {
                        id: videoProfileCard
                        title: "Default video profile"
                        desc: "quality & size for videos sent to the Zune."

                        // Family (0xD21A) → profile, resolved live from the
                        // connected Zune. -1 when nothing is plugged in.
                        readonly property int autoProfile: {
                            if (!DeviceService.connected)
                                return -1
                            switch (DeviceService.deviceFamily) {
                            case 0x00: return 0   // Keel — Zune 30
                            case 0x06: return 2   // Pavo — Zune HD
                            case 0x02:            // Scorpius — 4/8/16
                            case 0x03: return 1   // Draco — 80/120
                            default:   return 0   // unknown → WMV (safe)
                            }
                        }
                        // What the radios show: an explicit override wins;
                        // otherwise the detected profile lights up — no
                        // abstract "Auto" row to decode.
                        readonly property int shownProfile:
                            AppSettings.videoProfile >= 0
                                ? AppSettings.videoProfile
                                : autoProfile

                        RowLayout {
                            spacing: Theme.spaceSm
                            Text {
                                text: {
                                    if (AppSettings.videoProfile >= 0)
                                        return "manual override"
                                    if (!DeviceService.connected)
                                        return "auto — detects when a zune connects"
                                    return "auto-detected from this zune"
                                }
                                font.pixelSize: 12; font.weight: Font.Light
                                color: AppSettings.videoProfile >= 0
                                       ? Theme.orange : Theme.pink
                            }
                            Text {
                                visible: AppSettings.videoProfile >= 0
                                text: "reset to auto"
                                font.pixelSize: 12
                                color: Theme.textSecondary
                                font.underline: true
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: AppSettings.videoProfile = -1
                                }
                            }
                        }

                        // Mock's pills, real profiles: nothing lit while
                        // unplugged with no override (shownProfile -1).
                        Seg {
                            options: [{ label: "Zune 30 (WMV)", value: "0" },
                                      { label: "Classic (H.264)", value: "1" },
                                      { label: "HD 480p", value: "2" },
                                      { label: "HD 720p", value: "3" }]
                            current: videoProfileCard.shownProfile >= 0
                                     ? String(videoProfileCard.shownProfile) : ""
                            onPicked: (v) => AppSettings.videoProfile = parseInt(v)
                        }

                    }

                    // Not in the mock — kept deliberately: transcode needs
                    // a language pick for multi-audio sources.
                    Sblk {
                        title: "Preferred audio language"
                        desc: "picked when a video carries more than one audio track."
                        divider: false

                        ComboBox {
                            id: langBox
                            implicitWidth: 200
                            model: [
                                { text: "English", tag: "eng" },
                                { text: "Japanese", tag: "jpn" },
                                { text: "Spanish", tag: "spa" },
                                { text: "French", tag: "fre" },
                                { text: "German", tag: "ger" },
                                { text: "Korean", tag: "kor" },
                                { text: "Chinese", tag: "chi" },
                                { text: "Portuguese", tag: "por" },
                                { text: "Italian", tag: "ita" },
                                { text: "Russian", tag: "rus" }
                            ]
                            textRole: "text"
                            currentIndex: {
                                for (let i = 0; i < model.length; i++)
                                    if (model[i].tag === AppSettings.preferredAudioLang)
                                        return i
                                return 0
                            }
                            onActivated: AppSettings.preferredAudioLang = model[currentIndex].tag

                            background: Rectangle {
                                radius: Theme.radiusMd
                                color: Theme.cardBg
                                border.color: Theme.borderLight
                                border.width: 1
                            }
                            contentItem: Text {
                                leftPadding: Theme.spaceMd
                                verticalAlignment: Text.AlignVCenter
                                text: langBox.displayText
                                font.pixelSize: 13
                                font.weight: Font.Light
                                color: Theme.textPrimary
                            }
                            popup: Popup {
                                y: langBox.height + 2
                                width: langBox.width
                                padding: 4
                                background: Rectangle {
                                    radius: Theme.radiusMd
                                    color: Theme.surfaceBg
                                    border.color: Theme.borderLight
                                    border.width: 1
                                }
                                contentItem: ListView {
                                    implicitHeight: Math.min(contentHeight, 280)
                                    clip: true
                                    model: langBox.model
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index
                                        width: ListView.view.width
                                        height: 30
                                        radius: Theme.radiusSm
                                        color: langArea.containsMouse ? Theme.rowHoverPink : "transparent"
                                        Text {
                                            anchors.left: parent.left
                                            anchors.leftMargin: Theme.spaceSm
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.text
                                            font.pixelSize: 13
                                            font.weight: Font.Light
                                            color: Theme.textPrimary
                                        }
                                        MouseArea {
                                            id: langArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                langBox.currentIndex = index
                                                AppSettings.preferredAudioLang = modelData.tag
                                                langBox.popup.close()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── MIXTAPES ──
                ColumnLayout {
                    spacing: Theme.spaceXl

                    Sblk {
                        title: "Your mixtapes"
                        desc: "built here, synced to the Zune."
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: 0
                            Repeater {
                                model: LibraryService.playlists
                                delegate: ColumnLayout {
                                    id: mixRow
                                    required property var modelData
                                    required property int index
                                    // Cover art + accents — the playlists
                                    // shelf's recipe (MusicPage), shrunk
                                    // to a row icon. First cover wears
                                    // the label (stable, no re-roll).
                                    readonly property var artUrls: {
                                        void LibraryService.artPaths
                                        const rows = LibraryService
                                            .playlistTracks(mixRow.modelData.id)
                                        const seen = ({})
                                        const urls = []
                                        for (let i = 0; i < rows.length
                                                 && urls.length < 8; i++) {
                                            const t = rows[i]
                                            const key = LibraryService.artKey(
                                                t.artist, t.album)
                                            if (seen[key])
                                                continue
                                            seen[key] = true
                                            const u = LibraryService.artPaths[key] ?? ""
                                            if (u !== "")
                                                urls.push(u)
                                        }
                                        return urls
                                    }
                                    readonly property var accents: {
                                        if (mixRow.artUrls.length === 0)
                                            return []
                                        const cols = mixRow.artUrls.map(
                                            u => LibraryService.dominantColor(u))
                                        cols.sort((a, b) => a.hslHue - b.hslHue)
                                        return [cols[Math.floor(cols.length / 2)],
                                                cols[Math.floor(cols.length / 4)]]
                                    }
                                    // "on zune" = a device playlist with the
                                    // same name; only knowable while connected
                                    readonly property bool onZune: {
                                        if (!DeviceService.connected)
                                            return false
                                        const dev = DeviceService.playlistsList
                                        for (let i = 0; i < dev.length; ++i)
                                            if (dev[i].name === modelData.name)
                                                return true
                                        return false
                                    }
                                    // Imported tracks still being pulled/
                                    // scanned in (rides playlistsChanged)
                                    readonly property int missing: {
                                        void LibraryService.playlists
                                        return LibraryService
                                            .pendingPlaylistAddCount(modelData.id)
                                    }
                                    readonly property bool pullingNow:
                                        missing > 0 && DeviceService.pulling
                                    Layout.fillWidth: true
                                    spacing: 0
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.topMargin: Theme.spaceSm
                                        Layout.bottomMargin: Theme.spaceSm
                                        spacing: Theme.spaceMd
                                        // the REAL cassette (playlists
                                        // shelf), rendered native and
                                        // scaled down to a row icon
                                        Item {
                                            Layout.alignment: Qt.AlignVCenter
                                            implicitWidth: 60
                                            implicitHeight: 38
                                            CassetteTile {
                                                width: 176; height: 112
                                                scale: 60 / 176
                                                transformOrigin: Item.TopLeft
                                                name: mixRow.modelData.name !== undefined
                                                      ? mixRow.modelData.name : ""
                                                accents: mixRow.accents
                                                labelArt: mixRow.artUrls.length > 0
                                                          ? mixRow.artUrls[0] : ""
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.alignment: Qt.AlignVCenter
                                            spacing: 1
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.name !== undefined ? modelData.name : ""
                                                font.pixelSize: 13
                                                color: Theme.textPrimary
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                text: (modelData.count !== undefined
                                                       ? modelData.count : 0) + " tracks"
                                                      + (mixRow.missing > 0
                                                         ? " · " + mixRow.missing
                                                           + " not in library" : "")
                                                font.pixelSize: 11; font.weight: Font.Light
                                                color: Theme.textDim
                                            }
                                        }
                                        // mock ".sy"/".ns" — pulling while an
                                        // import fills in; blank when unplugged
                                        Text {
                                            text: mixRow.pullingNow ? "pulling…"
                                                  : !DeviceService.connected ? ""
                                                  : onZune ? "on zune" : "not synced"
                                            font.pixelSize: 11; font.weight: Font.Light
                                            color: onZune && !mixRow.pullingNow
                                                   ? Theme.green : Theme.textGhost
                                        }
                                    }
                                    Rectangle {
                                        visible: index < LibraryService.playlists.length - 1
                                        Layout.fillWidth: true
                                        implicitHeight: 1
                                        color: Theme.separator
                                    }
                                }
                            }
                        }
                        Text {
                            visible: LibraryService.playlists.length === 0
                            Layout.topMargin: Theme.spaceXs
                            text: "no mixtapes yet — build one from the tray"
                            font.pixelSize: 14; font.weight: Font.Light
                            color: Theme.textSecondary
                        }
                    }

                    Sblk {
                        title: "Import & export"
                        desc: "move mixtapes as .m3u — the escape hatch from our schema."
                        divider: false
                        RowLayout {
                            spacing: Theme.spaceMd
                            GlassButton {
                                text: "export all (.m3u)"
                                disabled: LibraryService.playlists.length === 0
                                onClicked: {
                                    const dir = LibraryService.exportPlaylistsM3U()
                                    toastHost.show("mixtapes exported to " + dir)
                                }
                            }
                            GlassButton {
                                text: "import a playlist…"
                                onClicked: m3uImportDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── HEALTH ──
                ColumnLayout {
                    spacing: Theme.spaceXl

                    DiagnosticsSettings {
                        Layout.fillWidth: true
                        Layout.maximumWidth: Theme.artworkSettingsMaxWidth
                        onNotification: function(message) { toastHost.show(message) }
                    }

                    Sblk {
                        title: "Status"
                        desc: "everything zuuned needs to run right."

                        // Watch folders reachable — total minus any failing
                        // folder checks the scanner reports.
                        StatRow {
                            readonly property int total: LibraryService.watchFolders.length
                            readonly property int failing: {
                                void LibraryService.watchFolders
                                return LibraryService.healthChecks()
                                    .filter(c => c.id === "folder").length
                            }
                            label: "Watch folders reachable"
                            value: (total - failing) + " of " + total
                            dot: failing === 0 ? Theme.green : Theme.warning
                        }
                        StatRow {
                            readonly property double gb: LibraryService.tempFreeGB()
                            label: "Temp space for transcodes"
                            value: gb < 0 ? "unknown"
                                          : gb.toFixed(gb < 100 ? 1 : 0) + " GB free"
                            dot: gb < 0 ? Theme.textDim
                                        : (gb >= 2 ? Theme.green : Theme.warning)
                        }
                        StatRow {
                            label: "USB rule installed"
                            value: DeviceService.udevRuleOk ? "installed" : "missing or outdated"
                            dot: DeviceService.udevRuleOk ? Theme.green : Theme.warning
                        }
                        StatRow {
                            label: "Zune connected"
                            value: DeviceService.connected ? "ready" : "unplugged"
                            dot: DeviceService.connected ? Theme.green : Theme.warning
                            divider: false
                        }
                    }

                    Sblk {
                        title: "Library"
                        desc: "what's indexed right now."
                        StatRow { label: "Tracks"; value: LibraryService.trackCount
                                  valueColor: Theme.textPrimary }
                        StatRow { label: "Videos"; value: LibraryService.videoCount
                                  valueColor: Theme.textPrimary }
                        StatRow { label: "Photos"; value: LibraryService.photoCount
                                  valueColor: Theme.textPrimary; divider: false }
                    }

                    Sblk {
                        title: "Backups & tour"
                        desc: "keep your library safe; see the tour again."

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spaceXs
                            spacing: Theme.spaceMd
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Back up library DB"
                                    font.pixelSize: 14; font.weight: Font.Light
                                    color: Theme.textPrimary
                                }
                                Text {
                                    id: backupWhen
                                    property string label: LibraryService.lastBackupLabel()
                                    text: label.length > 0 ? "last backup: " + label
                                                           : "never backed up"
                                    font.pixelSize: 11; font.weight: Font.Light
                                    color: Theme.textDim
                                }
                            }
                            GlassButton {
                                text: "back up now"
                                onClicked: {
                                    const when = LibraryService.backupDatabase()
                                    if (when.length > 0) {
                                        backupWhen.label = when
                                        toastHost.show("library backed up")
                                    } else {
                                        toastHost.show("backup failed")
                                    }
                                }
                            }
                        }

                        // mock ".foldrow" hairline between the two rows
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.separator
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Replay the walkthrough"
                                    font.pixelSize: 14; font.weight: Font.Light
                                    color: Theme.textPrimary
                                }
                                Text {
                                    text: "the first-run tour"
                                    font.pixelSize: 11; font.weight: Font.Light
                                    color: Theme.textDim
                                }
                            }
                            GlassButton {
                                text: "show me"
                                onClicked: Window.window.showOnboarding = true
                            }
                        }
                    }

                    Sblk {
                        title: "About"
                        divider: true
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Zuuned " + (Qt.application.version || "development build")
                                  + " · linux · your data stays local."
                            font.pixelSize: 13; font.weight: Font.Light
                            color: Theme.textDim
                        }
                    }

                    Sblk {
                        title: "Powered by"
                        divider: false
                        RowLayout {
                            spacing: Theme.spaceXxl
                            Text {
                                text: "TMDB"
                                font.pixelSize: 14; font.weight: Font.Bold
                                color: Qt.rgba(0.0, 0.71, 0.89, 1)
                            }
                            Text {
                                text: "MusicBrainz"
                                font.pixelSize: 14; font.weight: Font.Bold
                                color: Qt.rgba(0.73, 0.28, 0.56, 1)
                            }
                            Text {
                                text: "fanart.tv"
                                font.pixelSize: 14; font.weight: Font.Bold
                                color: Qt.rgba(0.04, 0.55, 1.0, 1)
                            }
                        }
                        CaptionText {
                            text: "This product uses the TMDB API but is not endorsed or certified "
                                  + "by TMDB. Music metadata powered by MusicBrainz and AcoustID. "
                                  + "Art powered by Fanart.tv and the Cover Art Archive."
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    FolderDialog {
        id: mediaFolderDialog
        property string pendingType: "music"
        title: "Choose a " + pendingType + " folder"
        onAccepted: {
            console.warn("[settings] folder accepted:", selectedFolder)
            const p = decodeURIComponent(
                selectedFolder.toString().replace(/^file:\/\//, ""))
            if (p.length > 1)
                LibraryService.addWatchFolder(p, pendingType)
        }
        onRejected: console.warn("[settings] folder dialog rejected/cancelled")
    }

    // Treat-As menu (shared across watch-folder rows)
    Popup {
        id: treatAsPopup
        property double folderId: -1
        padding: 4
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.surfaceBg
            border.color: Theme.borderLight
            border.width: 1
        }
        contentItem: Column {
            spacing: 2
            Repeater {
                model: ["music", "movies", "tv", "anime", "photos", "all"]
                Rectangle {
                    required property string modelData
                    width: 120
                    height: 26
                    radius: Theme.radiusSm
                    color: taOptArea.containsMouse ? Theme.rowHoverPink : "transparent"
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spaceSm
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        font.pixelSize: 12
                        font.weight: Font.Light
                        color: root.typeColor(modelData)
                    }
                    MouseArea {
                        id: taOptArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            LibraryService.setWatchFolderType(treatAsPopup.folderId, modelData)
                            treatAsPopup.close()
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: wallpaperDialog
        title: "Choose a background image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
            // Store a plain local path (mac stores url.path); the mac also
            // resets opacity to 0.3 on every new pick — match that.
            const path = decodeURIComponent(
                selectedFile.toString().replace(/^file:\/\//, ""))
            AppSettings.backgroundImagePath = path
            AppSettings.backgroundOpacity = 0.3
        }
    }

    // Mixtapes: import a plain .m3u playlist
    FileDialog {
        id: m3uImportDialog
        title: "Import a playlist (.m3u)"
        nameFilters: ["Playlists (*.m3u *.m3u8)"]
        onAccepted: {
            const res = LibraryService.importPlaylistM3U(selectedFile.toString())
            if (res && res.name !== undefined)
                toastHost.show("imported “" + res.name + "”")
            else
                toastHost.show("playlist imported")
        }
    }
}
