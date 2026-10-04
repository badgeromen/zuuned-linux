import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Full port of DevicePanelView.swift: fixed header ("ZUNE" eyebrow, device
// name, model + battery), compact sync indicator, device/stats/queue tab
// strip, scrolling tab content (Zune HD-style browse menu, storage +
// stats, queue), pinned sync controls on the queue tab, loading progress,
// connection footer, and the complete disconnected state.
//
// Draws NO background glass — the container (Main.qml) wraps it, matching
// the mac's outside-applied .glassGrunge.
//
// Queue tab, sync controls, and the compact indicator are LIVE against
// SyncEngine (Phase 5); loading-pipeline placeholders remain wired from
// Main.qml.
Item {
    id: root

    // Browse menu integration — mac sets appState.selectedPage; here the
    // container decides what to do with it.
    property string currentPage: ""
    signal browsePageSelected(string page)

    // Drag library→device: the panel's DropArea (in Main.qml) sets these
    // while a drag hovers, so the matching browse entry can light up "＋
    // zune" — the exact mirror of the library island's "+ add to library".
    property bool dropActive: false
    property string dropPage: ""

    // ── Sync engine bindings (Phase 5) ──
    // Both bottom storage bars (device sliver + stats breakdown) share
    // this width and center — they must read as the SAME element.
    readonly property int storageBarWidth: 156
    readonly property bool isSyncing: SyncEngine.isSyncing
    readonly property int syncCurrentIndex: SyncEngine.currentIndex
    readonly property int syncTotalCount: SyncEngine.totalCount
    readonly property string syncCurrentName: SyncEngine.currentName
    readonly property real syncProgress: SyncEngine.overallProgress
    // Repeater.count is reactive; the model's rowCount() is not
    readonly property int queueCount: queueRepeater.count
    readonly property bool awaitingDisconnect: DeviceService.awaitingDisconnect

    // ── Wired from Main.qml (loading pipeline) ──
    property bool isLoadingDeviceData: false
    property string loadingStage: ""
    property real loadingProgress: 0
    property real connectionProgress: 0
    property string connectionError: ""
    property int videoCount: 0      // ZMDB scan has this; DeviceService doesn't expose yet
    property int photoCount: 0
    property int playlistCount: 0

    property string selectedPanelTab: "device"
    // Narrow-window rail (approved): the panel condenses to a slim
    // strip; clicking it asks Main to pin the full island back open.
    property bool railMode: false
    signal expandRequested()
    // Narrow + pinned open: offer » to compress back to the rail
    // (RIGHT island shrinks toward the right edge)
    property bool compressVisible: false
    signal compressRequested()

    // Queue something anywhere → jump to the queue tab so you SEE it
    // land (design contract, UX-5).
    Connections {
        target: SyncEngine
        function onQueueItemsAdded(count) {
            root.selectedPanelTab = "queue"
        }
    }

    // ── Queue grouping (collapsible seasons/albums) ──
    // key → {kind, title, subtitle, count, doneCount, failedCount,
    // firstEntryId, ...}; rebuilt on membership AND status changes so
    // headers show live x/n progress.
    property var queueGroups: ({})
    property var expandedQueueGroups: ({})
    function rebuildQueueGroups() {
        const m = {}
        for (const g of SyncEngine.queue.groupSummary())
            m[g.key] = g
        queueGroups = m
    }
    function toggleQueueGroup(key) {
        const m = Object.assign({}, expandedQueueGroups)
        m[key] = m[key] !== true
        expandedQueueGroups = m
    }
    Connections {
        target: SyncEngine.queue
        function onCountChanged() { root.rebuildQueueGroups() }
        function onEstimateChanged() { root.rebuildQueueGroups() }
    }
    Component.onCompleted: rebuildQueueGroups()

    // ═══════════ Reusable pieces ═══════════

    component SeparatorLine: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.glassBorder
    }

    // SyncProgressBar — pink→activePink fill on cardHover track
    component SyncBar: Rectangle {
        property real fraction: 0
        Layout.fillWidth: true
        implicitHeight: Theme.spaceXs
        radius: 3
        color: Theme.cardHover

        Rectangle {
            height: parent.height
            radius: 3
            width: parent.width * Math.max(0, Math.min(1, parent.fraction))
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.pink }
                GradientStop { position: 1.0; color: Theme.activePink }
            }
            Behavior on width { NumberAnimation { duration: Theme.motionFast } }
        }
    }

    // StorageBar — 8px tall, radius sm
    // Stats-tab stat: big centered number, tiny letter-spaced label,
    // colored chip + GB share (chip color matches its bar segment).
    // Full-width children + horizontalAlignment — centering that no
    // parent layout can override (Layout.alignment on the block was
    // being ignored in the fillWidth column).
    component StatBlock: ColumnLayout {
        id: statBlock
        property int count: 0
        property string label
        property double gb: 0
        property color chip: "transparent"
        Layout.fillWidth: true
        spacing: 2

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: statBlock.count
            font.pixelSize: 30
            font.weight: Font.Light
            color: Theme.textPrimary
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: statBlock.label
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 2
            color: Theme.textDim
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: statBlock.gb > 0.0001
            spacing: Theme.spaceXs
            Rectangle {
                implicitWidth: 6
                implicitHeight: 6
                radius: 3
                color: statBlock.chip
            }
            Text {
                // Photos on a zune are re-armed 480px JPEGs — whole
                // libraries fit in a few MB, so small shares show in
                // MB instead of vanishing as "0.0 GB".
                text: statBlock.gb >= 0.95
                    ? statBlock.gb.toFixed(1) + " GB"
                    : (statBlock.gb * 1000).toFixed(0) + " MB"
                font.pixelSize: 10
                font.weight: Font.Light
                color: Theme.textSecondary
            }
        }
    }

    component StorageBar: Rectangle {
        property real percent: 0
        Layout.fillWidth: true
        implicitHeight: 8
        radius: Theme.radiusSm
        color: Theme.cardHover

        Rectangle {
            height: parent.height
            radius: Theme.radiusSm
            width: parent.width * Math.min(parent.percent, 1.0)
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.pink }
                GradientStop { position: 1.0; color: Theme.activePink }
            }
        }
    }

    // ConnectionProgressBar — pink→orange fill on cardBg track
    component ConnBar: Rectangle {
        property real progress: 0
        Layout.fillWidth: true
        implicitHeight: Theme.spaceXs
        radius: 3
        color: Theme.cardBg

        Rectangle {
            height: parent.height
            radius: 3
            width: parent.width * Math.max(0, Math.min(1, parent.progress))
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.pink }
                GradientStop { position: 1.0; color: Theme.orange }
            }
            Behavior on width { NumberAnimation { duration: Theme.motionBase; easing.type: Easing.InOutQuad } }
        }
    }

    // AccentButton — pink text on translucent pink fill, pink stroke
    component AccentButton: Rectangle {
        id: accentBtn
        property string title
        property bool disabled: false
        property bool hovering: false
        signal clicked()

        implicitWidth: accentLabel.implicitWidth + Theme.spaceXl * 2
        implicitHeight: accentLabel.implicitHeight + Theme.spaceSm * 2
        radius: Theme.radiusMd
        color: Qt.rgba(0.83, 0.21, 0.48, accentBtn.hovering && !accentBtn.disabled ? 0.25 : 0.15)
        border.width: 1
        border.color: Qt.rgba(0.83, 0.21, 0.48, accentBtn.hovering && !accentBtn.disabled ? 0.5 : 0.3)
        opacity: disabled ? 0.4 : 1.0

        Behavior on color { ColorAnimation { duration: Theme.motionBase } }

        Text {
            id: accentLabel
            anchors.centerIn: parent
            text: accentBtn.title
            font.pixelSize: 14
            font.weight: Font.Light
            color: accentBtn.hovering && !accentBtn.disabled ? Theme.activePink : Theme.pink
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: accentBtn.disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
            onEntered: accentBtn.hovering = true
            onExited: accentBtn.hovering = false
            onClicked: if (!accentBtn.disabled) accentBtn.clicked()
        }
    }

    // Battery glyph — no SF Symbols on Linux; drawn body + tip + quartile fill
    component BatteryIcon: Item {
        property int level: 0
        readonly property real fillFraction: level > 75 ? 1.0
                                           : level > 50 ? 0.75
                                           : level > 25 ? 0.50 : 0.25
        readonly property color chargeColor: level > 20 ? Theme.green : Theme.error

        implicitWidth: 20
        implicitHeight: 10

        Rectangle {
            width: 17
            height: 10
            radius: 2
            color: "transparent"
            border.width: 1
            border.color: parent.chargeColor

            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                width: (parent.width - 4) * parent.parent.fillFraction
                height: parent.height - 4
                radius: 1
                color: parent.parent.chargeColor
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: 4
            color: parent.chargeColor
        }
    }

    component StatRow: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Text {
            text: parent.label
            font.pixelSize: 13
            font.weight: Font.Light
            color: Theme.textSecondary
        }
        Item { Layout.fillWidth: true }
        Text {
            text: parent.value
            font.pixelSize: 13
            font.weight: Font.Normal
            color: Theme.textPrimary
        }
    }

    component PanelTabItem: Item {
        id: tabItem
        property string tabId
        property string label: tabId
        property bool hovering: false
        readonly property bool active: root.selectedPanelTab === tabId

        implicitWidth: tabLabel.implicitWidth + Theme.spaceSm * 2
        implicitHeight: tabLabel.implicitHeight + Theme.spaceXs * 2

        Text {
            id: tabLabel
            anchors.centerIn: parent
            text: tabItem.label
            font.pixelSize: 12
            font.weight: Font.Light
            color: tabItem.active ? Theme.textPrimary
                                  : (tabItem.hovering ? Theme.textMid : Theme.textSubtle)
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 2
            color: Theme.pink
            opacity: tabItem.active ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: tabItem.hovering = true
            onExited: tabItem.hovering = false
            onClicked: root.selectedPanelTab = tabItem.tabId
        }
    }

    // Zune HD-style browse button — 24 light → 28 hover orange → 36 active pink
    component BrowseButton: Item {
        id: browseBtn
        property string title
        property string page
        property bool hovering: false
        readonly property bool active: root.currentPage === page
        // A library item of THIS entry's type is hovering the island —
        // light up "＋ zune" to show where the drop lands (mirror of the
        // library island's "+ add to library").
        readonly property bool dropHover:
            root.dropActive && root.dropPage === page

        Layout.fillWidth: true
        implicitHeight: browseLabel.implicitHeight + Theme.spaceXs * 2

        Text {
            id: browseLabel
            // Centered on x — matches the library island's nav (pair)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            text: browseBtn.dropHover ? "＋ zune" : browseBtn.title
            font.pixelSize: browseBtn.dropHover ? 28
                            : (browseBtn.active ? 36
                               : (browseBtn.hovering ? 28 : 24))
            font.weight: browseBtn.active ? Font.Normal : Font.Light
            color: browseBtn.dropHover ? Theme.activePink
                   : (browseBtn.active ? Theme.activePink
                      : (browseBtn.hovering ? Theme.orange : Theme.textSubtle))

            Behavior on font.pixelSize {
                NumberAnimation { duration: Theme.motionFast; easing.type: Easing.InOutQuad }
            }
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: browseBtn.hovering = true
            onExited: browseBtn.hovering = false
            onClicked: root.browsePageSelected(browseBtn.page)
        }
    }

    // ═══════════ Connected state ═══════════

    ColumnLayout {
        // Pinned at FULL width and right-anchored: during the rail
        // animation the layout must not reflow — it slides under the
        // crossfade and the glass's clip does the cropping.
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceMd
        width: 260 - 2 * Theme.spaceMd
        opacity: DeviceService.connected && !root.railMode ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.motionSlow } }
        spacing: 0

        // ── Fixed Header — generous air between the rows (review:
        // the top of the panel needed breathing space) ──
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceXl
            spacing: 0

            Text {
                text: "ZUNE"
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 14
                font.weight: Font.Light
                font.letterSpacing: 2
                color: Theme.textDim
                Layout.bottomMargin: Theme.spaceMd
            }

            // Device name — CLICK TO RENAME the Zune (Friendly Name).
            Item {
                id: deviceNameSlot
                property bool editing: false
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.spaceXs
                implicitHeight: Math.max(deviceNameText.implicitHeight,
                                         deviceNameEdit.implicitHeight)

                Text {
                    id: deviceNameText
                    width: parent.width
                    visible: !deviceNameSlot.editing
                    text: DeviceService.name.length > 0
                          ? DeviceService.name : DeviceService.model
                    font.pixelSize: 22
                    font.weight: Font.Light
                    // Hover affordance = color only (the app's link
                    // idiom) — no icons, no layout shift.
                    color: deviceNameArea.containsMouse
                           ? Theme.activePink : Theme.textPrimary
                    Behavior on color {
                        ColorAnimation { duration: Theme.motionFast }
                    }
                    elide: Text.ElideRight

                    MouseArea {
                        id: deviceNameArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: DeviceService.connected
                        onClicked: {
                            deviceNameEdit.text = DeviceService.name
                            deviceNameSlot.editing = true
                            deviceNameEdit.forceActiveFocus()
                            deviceNameEdit.selectAll()
                        }
                    }
                }

                ZuneSearchField {
                    id: deviceNameEdit
                    width: parent.width
                    visible: deviceNameSlot.editing
                    font.pixelSize: 18
                    placeholderText: "zune name"
                    onAccepted: {
                        if (text.trim() !== "")
                            DeviceService.renameDevice(text.trim())
                        deviceNameSlot.editing = false
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            deviceNameSlot.editing = false
                    }
                    Keys.onEscapePressed: deviceNameSlot.editing = false
                }
            }

            // One identity row: model type left, battery right. (The
            // raw model string is always just "Zune" — three Zune
            // labels in one header was two too many.)
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: DeviceService.modelName
                          + (DeviceService.capacityGB > 0
                                 ? " · " + Math.round(DeviceService.capacityGB) + "GB"
                                 : "")
                    font.pixelSize: 12
                    color: Theme.pink
                }
                Item { Layout.fillWidth: true }
                RowLayout {
                    spacing: Theme.spaceXs
                    BatteryIcon { level: DeviceService.battery }
                    Text {
                        text: DeviceService.battery + "%"
                        font.pixelSize: 12
                        font.weight: Font.Light
                        color: Theme.textSecondary
                    }
                }
            }

            SeparatorLine { Layout.topMargin: Theme.spaceLg }
        }

        // ── Compact Sync Indicator (all tabs, during sync) ──
        ColumnLayout {
            visible: root.isSyncing
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceSm
            Layout.bottomMargin: Theme.spaceSm
            spacing: Theme.spaceXxs

            // W2: phase + item + count, so a long transcode reads as
            // "transcoding", never a freeze. Phase leads (it's what the
            // user is waiting on); the item name is the reassurance
            // that it's THEIR file, not a hang.
            Text {
                readonly property string phase: SyncEngine.syncPhase
                text: (phase !== "" ? phase : "syncing")
                      + " " + root.syncCurrentIndex + "/" + root.syncTotalCount
                      + (SyncEngine.currentName !== ""
                         ? " · " + SyncEngine.currentName : "")
                font.pixelSize: 11
                font.weight: Font.Light
                color: SyncEngine.syncPhase === "transcoding"
                       ? Theme.orange : Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            SyncBar { fraction: root.syncProgress }
            // A transcode moves the bar slowly; say so, so nobody
            // mistakes patience for a hang.
            Text {
                visible: SyncEngine.syncPhase === "transcoding"
                text: "converting for your zune — large videos take a few minutes"
                font.pixelSize: 9
                font.weight: Font.Light
                color: Theme.textDim
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // ── Compact Delete Indicator (all tabs, during a device purge) ──
        // A delete is destructive and can take a beat per object, so it
        // gets its own loud, pulsing indicator — never a silent freeze.
        ColumnLayout {
            visible: DeviceService.purging
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceSm
            Layout.bottomMargin: Theme.spaceSm
            spacing: Theme.spaceXxs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: Theme.pink
                    SequentialAnimation on opacity {
                        running: DeviceService.purging
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "deleting from your zune · "
                          + DeviceService.purgeCurrent + " of "
                          + DeviceService.purgeTotal
                    font.pixelSize: 11
                    font.weight: Font.Light
                    color: Theme.pink
                    elide: Text.ElideRight
                }
            }
            SyncBar {
                fraction: DeviceService.purgeTotal > 0
                          ? DeviceService.purgeCurrent / DeviceService.purgeTotal
                          : 0
            }
        }

        // ── Compact Pull Indicator (drag device→library extract) ──
        // A pull reads the file off the Zune, so it takes a real beat per
        // item; this shows progress and, via the status line, the result.
        ColumnLayout {
            visible: DeviceService.pulling
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceSm
            Layout.bottomMargin: Theme.spaceSm
            spacing: Theme.spaceXxs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: Theme.green
                    SequentialAnimation on opacity {
                        running: DeviceService.pulling
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "pulling to library · " + DeviceService.pullCurrent
                          + " of " + DeviceService.pullTotal
                    font.pixelSize: 11
                    font.weight: Font.Light
                    color: Theme.green
                    elide: Text.ElideRight
                }
            }
            SyncBar {
                fraction: DeviceService.pullTotal > 0
                          ? DeviceService.pullCurrent / DeviceService.pullTotal
                          : 0
            }
        }

        // ── Tab Strip ──
        Item {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            implicitHeight: tabRow.implicitHeight

            RowLayout {
                id: tabRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spaceLg
                PanelTabItem { tabId: "device" }
                PanelTabItem { tabId: "stats" }
                PanelTabItem {
                    tabId: "queue"
                    label: root.queueCount > 0 ? "queue (" + root.queueCount + ")" : "queue"
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        // ── Tab Content (scrolls) ──
        Flickable {
            ScrollBar.vertical: ZuneScrollBar {}
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: Math.max(tabContent.implicitHeight, height)
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: tabContent
                width: parent.width
                height: Math.max(implicitHeight, parent.parent.height)
                spacing: 0

                // B2: the app died mid-send last run — the device may
                // hold a truncated object that CRASHES firmware on
                // playback. Tell the user exactly what to hunt down.
                Rectangle {
                    visible: SyncEngine.interruptedSend !== ""
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spaceLg
                    Layout.rightMargin: Theme.spaceLg
                    Layout.topMargin: Theme.spaceMd
                    implicitHeight: orphanCol.implicitHeight + Theme.spaceLg * 2
                    radius: Theme.radiusMd
                    color: Qt.rgba(0.95, 0.30, 0.30, 0.08)
                    border.width: 1
                    border.color: Qt.alpha(Theme.error, 0.5)

                    ColumnLayout {
                        id: orphanCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spaceLg
                        spacing: Theme.spaceSm

                        Text {
                            text: "a sync was interrupted last run"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: Theme.error
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "'" + SyncEngine.interruptedSend
                                  + "' may be truncated on the zune — playing"
                                  + " it can crash the device. Find it in the"
                                  + " device library, delete it, then eject to"
                                  + " re-index."
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceMd

                            Text {
                                text: "dismiss"
                                font.pixelSize: 11
                                color: orphanDismiss.containsMouse
                                       ? Theme.textPrimary : Theme.textDim
                                MouseArea {
                                    id: orphanDismiss
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: SyncEngine.dismissInterruptedSend()
                                }
                            }
                            Item { Layout.fillWidth: true }
                            // primary action rightmost (house rule)
                            Text {
                                text: "delete it from the zune"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                color: orphanPurge.containsMouse
                                       ? Theme.textPrimary : Theme.error
                                MouseArea {
                                    id: orphanPurge
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const n = SyncEngine.purgeInterruptedSend()
                                        toastHost.show(n > 0
                                            ? "deleting " + n + " item(s) — eject to re-index"
                                            : (SyncEngine.interruptedRecoveryStatus || "recovery remains pending — review the device library"))
                                    }
                                }
                            }
                        }
                    }
                }

                // ── device tab: browse menu, vertically centered ──
                ColumnLayout {
                    visible: root.selectedPanelTab === "device"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Item { Layout.fillHeight: true }

                    ColumnLayout {
                        Layout.fillWidth: true
                        // No offset — labels center on x (pair rule)
                        spacing: Theme.spaceMd

                        BrowseButton { title: "music"; page: "deviceMusic" }
                        BrowseButton { title: "videos"; page: "deviceVideos" }
                        BrowseButton { title: "photos"; page: "devicePictures" }
                        BrowseButton { title: "playlists"; page: "devicePlaylists" }
                    }

                    Item { Layout.fillHeight: true }

                    // ── Storage strip — the glanceable fact, pinned
                    // low so the negative space above stays open.
                    // Narrow and centered (review); the stats tab
                    // keeps the full-width detailed readout. ──
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: Theme.spaceMd
                        spacing: Theme.spaceXs

                        StorageBar {
                            // Component defaults to fillWidth — pin to
                            // the shared bar width, centered.
                            Layout.fillWidth: false
                            Layout.preferredWidth: root.storageBarWidth
                            Layout.alignment: Qt.AlignHCenter
                            percent: DeviceService.capacityGB > 0
                                     ? 1 - DeviceService.freeGB / DeviceService.capacityGB
                                     : 0
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: (DeviceService.capacityGB - DeviceService.freeGB)
                                      .toFixed(1) + " GB used · "
                                  + DeviceService.freeGB.toFixed(1) + " GB free"
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textDim
                        }
                    }
                }

                // ── stats tab — the device tab's mirror: big centered
                // NUMBERS floating in the open space, each with its
                // slice of the disk; a segmented storage bar at the
                // bottom in the same position as the device tab's
                // sliver (approved redesign). ──
                ColumnLayout {
                    visible: root.selectedPanelTab === "stats"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    // Attributed vs reported-used: the gap is "other"
                    // (system data, art caches, rounding).
                    readonly property double usedGB:
                        DeviceService.capacityGB - DeviceService.freeGB
                    readonly property double otherGB:
                        Math.max(0, usedGB - DeviceService.musicGB
                                 - DeviceService.videoGB - DeviceService.photoGB)
                    // Per-type sizes are HD-only (design decision after
                    // checking the 30/80/120: Classic ZMDBs don't carry
                    // sizes at the HD-validated offsets). Classics show
                    // COUNTS with the overall gauge at the bottom.
                    // TODO(protocol): Classic size discovery — batched
                    // GetObjectPropList ObjectSize sweep or zmdbdump
                    // offset hunt — would flip this back on.
                    readonly property bool breakdownKnown:
                        DeviceService.deviceFamily === 0x06   // Pavo/HD
                    id: statsTab

                    Item { Layout.fillHeight: true }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceLg

                        // gb: 0 on Classic — the stray nonzero values
                        // there are mis-parsed garbage, not sizes.
                        StatBlock {
                            count: DeviceService.trackCount
                            label: "TRACKS"
                            gb: statsTab.breakdownKnown ? DeviceService.musicGB : 0
                            chip: Theme.pink
                        }
                        StatBlock {
                            count: root.videoCount
                            label: "VIDEOS"
                            gb: statsTab.breakdownKnown ? DeviceService.videoGB : 0
                            chip: Theme.orange
                        }
                        StatBlock {
                            count: root.photoCount
                            label: "PHOTOS"
                            gb: statsTab.breakdownKnown ? DeviceService.photoGB : 0
                            chip: Theme.purple
                        }
                        StatBlock {
                            count: root.playlistCount
                            label: "PLAYLISTS"
                            gb: 0
                            chip: "transparent"
                        }
                    }

                    Item { Layout.fillHeight: true }

                    // Segmented bar — full breakdown, bottom-pinned,
                    // SAME width and center as the device tab's sliver
                    // (they must read as one element across tabs).
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: Theme.spaceMd
                        spacing: Theme.spaceXs

                        Rectangle {
                            id: segTrack
                            Layout.preferredWidth: root.storageBarWidth
                            Layout.alignment: Qt.AlignHCenter
                            implicitHeight: 8
                            radius: Theme.radiusSm
                            color: Theme.cardHover

                            // Unknown breakdown (Classic) → honest
                            // plain gauge, same as the device tab.
                            Rectangle {
                                visible: !statsTab.breakdownKnown
                                width: parent.width * (statsTab.usedGB
                                       / Math.max(DeviceService.capacityGB, 0.001))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.pink
                            }

                            // Segments masked to the track's rounded
                            // shape — plain clip is rectangular and
                            // left square corners poking out.
                            Item {
                                anchors.fill: parent
                                visible: statsTab.breakdownKnown
                                layer.enabled: true
                                layer.effect: OpacityMask { maskSource: segMask }

                                Row {
                                    anchors.fill: parent
                                    readonly property double cap:
                                        Math.max(DeviceService.capacityGB, 0.001)
                                    Rectangle {
                                        width: parent.width * DeviceService.musicGB / parent.cap
                                        height: parent.height
                                        color: Theme.pink
                                    }
                                    Rectangle {
                                        width: parent.width * DeviceService.videoGB / parent.cap
                                        height: parent.height
                                        color: Theme.orange
                                    }
                                    Rectangle {
                                        width: parent.width * DeviceService.photoGB / parent.cap
                                        height: parent.height
                                        color: Theme.purple
                                    }
                                    Rectangle {
                                        width: parent.width * statsTab.otherGB / parent.cap
                                        height: parent.height
                                        color: Qt.rgba(1, 1, 1, 0.18)
                                    }
                                }
                            }
                            Rectangle {
                                id: segMask
                                anchors.fill: parent
                                radius: segTrack.radius
                                visible: false
                            }
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: statsTab.usedGB.toFixed(1) + " GB used · "
                                  + DeviceService.freeGB.toFixed(1) + " GB free"
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textDim
                        }
                    }
                }

                // ── queue tab ──
                ColumnLayout {
                    visible: root.selectedPanelTab === "queue"
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spaceLg
                    Layout.rightMargin: Theme.spaceLg
                    Layout.topMargin: Theme.spaceSm
                    spacing: Theme.spaceSm

                    // B3: queue built against a different Zune — dedup
                    // re-runs against THIS one at sync start, so it's a
                    // note, not a blocker.
                    Rectangle {
                        visible: SyncEngine.queueDeviceMismatch
                        Layout.fillWidth: true
                        implicitHeight: mismatchText.implicitHeight
                                        + Theme.spaceMd * 2
                        radius: Theme.radiusMd
                        color: Qt.rgba(0.40, 0.60, 0.90, 0.08)
                        border.width: 1
                        border.color: Qt.alpha(Theme.info, 0.45)
                        Text {
                            id: mismatchText
                            anchors.fill: parent
                            anchors.margins: Theme.spaceMd
                            text: "this queue was built for "
                                  + SyncEngine.queueBuiltFor.split("|")[0]
                                  + " — anything already on THIS zune gets"
                                  + " skipped at sync"
                            font.pixelSize: 10
                            font.weight: Font.Light
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }

                    // D1: the previous run's outcome, visible once the
                    // queue has swept itself clean.
                    Text {
                        visible: root.queueCount === 0 && !root.isSyncing
                                 && (SyncEngine.lastSync.when ?? 0) > 0
                        Layout.fillWidth: true
                        text: {
                            const s = SyncEngine.lastSync
                            const parts = []
                            if (s.tracks > 0) parts.push(s.tracks + " tracks")
                            if (s.videos > 0) parts.push(s.videos + " videos")
                            if (s.photos > 0) parts.push(s.photos + " photos")
                            let line = "last sync: "
                                + (parts.length ? parts.join(", ") : "nothing new")
                            if (s.skipped > 0) line += " · " + s.skipped + " skipped"
                            if (s.failed > 0) line += " · " + s.failed + " FAILED"
                            const mins = Math.floor(
                                (Date.now() / 1000 - s.when) / 60)
                            line += mins < 1 ? " · just now"
                                : mins < 60 ? " · " + mins + "m ago"
                                : mins < 1440 ? " · " + Math.floor(mins / 60) + "h ago"
                                : " · " + Math.floor(mins / 1440) + "d ago"
                            return line
                        }
                        font.pixelSize: 10
                        font.weight: Font.Light
                        color: (SyncEngine.lastSync.failed ?? 0) > 0
                               ? Theme.warning : Theme.textDim
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceSm

                        Text {
                            text: "QUEUE"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            font.letterSpacing: 2
                            color: Theme.textDim
                        }

                        Rectangle {
                            visible: root.queueCount > 0
                            implicitWidth: queueCountLabel.implicitWidth + Theme.spaceXs * 2
                            implicitHeight: queueCountLabel.implicitHeight + Theme.spaceXxxs * 2
                            radius: height / 2
                            color: Qt.rgba(0.83, 0.21, 0.48, 0.3)

                            Text {
                                id: queueCountLabel
                                anchors.centerIn: parent
                                text: root.queueCount
                                font.pixelSize: 10
                                font.weight: Font.Medium
                                color: Theme.textPrimary
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        visible: queueRepeater.count === 0
                        text: "no items queued"
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Theme.textDim
                        Layout.topMargin: Theme.spaceXxs
                        Layout.bottomMargin: Theme.spaceXxs
                    }

                    // (Capacity readout lives with the sync button below —
                    // the pinned bar above "sync now".)

                    // Queue rows — art · title/artist · status · remove.
                    // Repeater (not ListView): the outer Flickable owns
                    // scrolling, mac's ScrollView anatomy.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Repeater {
                            id: queueRepeater
                            model: SyncEngine.queue

                            QueueEntryView {
                                groupInfo: root.queueGroups[groupKey] || null
                                expanded: root.expandedQueueGroups[groupKey] === true
                                syncLocked: root.isSyncing
                                onToggleRequested: key => root.toggleQueueGroup(key)
                                onRemoveGroupRequested: key => {
                                    if (!root.isSyncing) SyncEngine.queue.removeGroup(key)
                                }
                                onRemoveEntryRequested: id => {
                                    if (!root.isSyncing) SyncEngine.removeEntry(id)
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // ── Fixed Sync Controls (queue tab only) ──
        ColumnLayout {
            visible: root.selectedPanelTab === "queue"
            Layout.fillWidth: true
            spacing: 0

            SeparatorLine {}

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.topMargin: Theme.spaceSm
                Layout.bottomMargin: Theme.spaceSm
                spacing: Theme.spaceSm

                // ── Capacity bar: what this queue costs vs what fits ──
                ColumnLayout {
                    id: capacityBar
                    readonly property double queuedGB:
                        SyncEngine.queue.pendingEstimatedBytes / 1e9
                    readonly property double freeGB: DeviceService.freeGB
                    readonly property double fillFraction:
                        DeviceService.connected && freeGB > 0
                            ? Math.min(queuedGB / freeGB, 1) : 0
                    // Red is reserved for "does not fit" — a full queue
                    // that still fits is orange, not an alarm. (A ~99%
                    // bar in red read as an error when nothing was wrong.)
                    readonly property color barColor:
                        queuedGB > freeGB ? Theme.error
                        : fillFraction > 0.8 ? Theme.orange
                        : Theme.pink
                    function fmtGB(gb) {
                        return gb < 1 ? (gb * 1000).toFixed(0) + " MB"
                                      : gb.toFixed(1) + " GB"
                    }

                    visible: root.queueCount > 0
                    Layout.fillWidth: true
                    spacing: Theme.spaceXxs

                    // Same width/center as every other panel bar —
                    // one caption line beneath (device-tab strip
                    // pattern).
                    Rectangle {
                        Layout.preferredWidth: root.storageBarWidth
                        Layout.alignment: Qt.AlignHCenter
                        implicitHeight: 8
                        radius: Theme.radiusSm
                        color: Theme.cardHover
                        Rectangle {
                            width: parent.width * capacityBar.fillFraction
                            height: parent.height
                            radius: Theme.radiusSm
                            color: capacityBar.barColor
                            Behavior on width {
                                NumberAnimation { duration: Theme.motionBase }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        // Same rule as the modal: say what's LEFT
                        // after the sync, not a subtraction problem.
                        text: "~" + capacityBar.fmtGB(capacityBar.queuedGB)
                              + " queued"
                              + (DeviceService.connected
                                 ? " · ~" + capacityBar.fmtGB(
                                       Math.max(0, capacityBar.freeGB
                                                   - capacityBar.queuedGB))
                                   + " left after"
                                 : "")
                        font.pixelSize: 10
                        font.weight: Font.Light
                        color: capacityBar.fillFraction > 0.8
                               ? capacityBar.barColor : Theme.textDim
                        elide: Text.ElideRight
                    }
                }

                // Action bottom-RIGHT (house rule)
                AccentButton {
                    Layout.alignment: Qt.AlignRight
                    title: "sync now"
                    disabled: root.queueCount === 0 || !DeviceService.connected || root.isSyncing
                    onClicked: SyncEngine.startSync()
                }

                ColumnLayout {
                    visible: root.isSyncing
                    Layout.fillWidth: true
                    spacing: Theme.spaceXxs

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "syncing " + root.syncCurrentIndex + "/" + root.syncTotalCount
                                  + ": " + root.syncCurrentName
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "cancel"
                            font.pixelSize: 11
                            font.weight: Font.Light
                            color: cancelArea.containsMouse
                                   ? Theme.textPrimary : Theme.error

                            MouseArea {
                                id: cancelArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: SyncEngine.cancelSync()
                            }
                        }
                    }
                    SyncBar { fraction: root.syncProgress }
                }

                ColumnLayout {
                    visible: root.awaitingDisconnect
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceXs
                    spacing: Theme.spaceXs

                    Text {
                        text: "sync complete"
                        font.pixelSize: 14
                        font.weight: Font.Light
                        color: Theme.green
                    }
                    Text {
                        text: "disconnect your Zune to apply changes"
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Theme.textSecondary
                    }
                    AccentButton {
                        title: "eject"
                        onClicked: DeviceService.eject()
                    }
                }
            }
        }

        // ── Loading Progress (pinned at bottom) ──
        ColumnLayout {
            visible: root.isLoadingDeviceData
            Layout.fillWidth: true
            spacing: Theme.spaceXxs

            SeparatorLine {}

            Text {
                text: root.loadingStage
                font.pixelSize: 11
                font.weight: Font.Light
                color: Theme.textDim
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
            }
            ConnBar {
                progress: root.loadingProgress
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.bottomMargin: Theme.spaceXs
            }
        }

        // ── Fixed Footer ──
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            SeparatorLine {}

            // Raised off the bottom edge (review: the footer sat too
            // low against the island rim).
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceLg
                Layout.topMargin: Theme.spaceMd
                Layout.bottomMargin: Theme.spaceLg

                RowLayout {
                    spacing: Theme.spaceXs
                    Rectangle {
                        implicitWidth: Theme.spaceXs
                        implicitHeight: Theme.spaceXs
                        radius: implicitWidth / 2
                        color: Theme.green
                    }
                    Text {
                        text: "Connected"
                        font.pixelSize: 12
                        font.weight: Font.Light
                        font.letterSpacing: 1
                        color: Theme.green
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    id: ejectLabel
                    text: "eject"
                    font.pixelSize: 13
                    font.weight: Font.Light
                    color: ejectArea.containsMouse ? Theme.textPrimary : Theme.textSecondary

                    MouseArea {
                        id: ejectArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: DeviceService.eject()
                    }
                }
            }
        }
    }

    // ═══════════ Disconnected state ═══════════
    // Centered progress + status in the open space, connect pinned at
    // the bottom, red ● Disconnected footer mirroring the green
    // ● Connected one. No queue chrome here — nothing to sync into.

    ColumnLayout {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceMd   // same grunge-rim inset
        width: 260 - 2 * Theme.spaceMd
        opacity: !DeviceService.connected && !root.railMode ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.motionSlow } }
        spacing: 0

        Text {
            text: "ZUNE"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spaceXl
            font.pixelSize: 14
            font.weight: Font.Light
            font.letterSpacing: 2
            color: Theme.textDim
        }

        Item { Layout.fillHeight: true }

        // ── Center: connection progress + status underneath ──
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            ConnBar {
                Layout.fillWidth: false
                Layout.preferredWidth: root.storageBarWidth
                Layout.alignment: Qt.AlignHCenter
                progress: root.connectionProgress
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: DeviceService.busy || DeviceService.devicePresent
                      ? DeviceService.status
                      : "waiting for a zune…"
                font.pixelSize: 12
                font.weight: Font.Light
                color: Theme.textDim
                elide: Text.ElideRight
            }
            Text {
                visible: root.connectionError.length > 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.connectionError
                font.pixelSize: 11
                font.weight: Font.Light
                color: Qt.rgba(0.95, 0.30, 0.30, 0.8)
                wrapMode: Text.WordWrap
            }

            // C3 fix-it card: a Zune is plugged in but the udev rule
            // isn't installed — say so, with the exact fix, instead of
            // letting a stranger stare at "waiting for a zune…".
            Rectangle {
                visible: DeviceService.devicePresent
                         && !DeviceService.connected
                         && !DeviceService.udevRuleOk
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceMd
                implicitHeight: udevCol.implicitHeight + Theme.spaceLg * 2
                radius: Theme.radiusMd
                color: Qt.alpha(Theme.warning, 0.08)
                border.width: Theme.hairline
                border.color: Qt.alpha(Theme.warning, 0.5)

                // Fix the content width to the card: an implicit-width action
                // row used to push the layout (and wrapped text) past its rim.
                Column {
                    id: udevCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spaceLg
                    spacing: Theme.spaceSm

                    property bool showManual: false

                    Text {
                        width: parent.width
                        text: "your zune needs a USB permission rule"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: Theme.warning
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        width: parent.width
                        text: "one click installs it — you'll get your system's "
                              + "password prompt, no terminal needed."
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: udevCol.showManual ? "hide the manual command"
                                                : "trouble? show the manual command"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        color: manualArea.containsMouse
                               ? Theme.textSecondary : Theme.textDim
                        MouseArea {
                            id: manualArea
                            anchors.fill: parent
                            anchors.margins: -Theme.spaceXxs
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: udevCol.showManual = !udevCol.showManual
                        }
                    }

                    // Primary action gets its own line, aligned to the right.
                    Rectangle {
                        anchors.right: parent.right
                        implicitWidth: fixLabel.implicitWidth + Theme.spaceXl
                        implicitHeight: fixLabel.implicitHeight + Theme.spaceLg
                        radius: Theme.radiusMd
                        color: fixArea.containsMouse
                            ? Qt.lighter(Theme.pink, 1.12) : Theme.pink
                        Text {
                            id: fixLabel
                            anchors.centerIn: parent
                            text: "Fix it for me"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        MouseArea {
                            id: fixArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: DeviceService.installUdevRule()
                        }
                    }

                    // Keep the long, selectable fallback command inside the
                    // panel instead of expanding the whole card off-screen.
                    ScrollView {
                        visible: udevCol.showManual
                        width: parent.width
                        height: Math.min(manualCommand.implicitHeight, Theme.spaceHuge * 3)
                        contentWidth: availableWidth
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        TextArea {
                            id: manualCommand
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            text: DeviceService.udevInstallCommand
                            font.family: "monospace"
                            font.pixelSize: 9
                            color: Theme.textPrimary
                            selectionColor: Theme.pink
                            padding: 0
                            background: null
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // ── Bottom footer: red status left, connect RIGHT (house
        // rule — actions live bottom-right, mirroring eject) ──
        SeparatorLine { Layout.topMargin: Theme.spaceMd }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.topMargin: Theme.spaceMd
            Layout.bottomMargin: Theme.spaceLg
            spacing: Theme.spaceXs

            Rectangle {
                implicitWidth: Theme.spaceXs
                implicitHeight: Theme.spaceXs
                radius: implicitWidth / 2
                color: Theme.error
            }
            Text {
                text: "Disconnected"
                font.pixelSize: 12
                font.weight: Font.Light
                font.letterSpacing: 1
                color: Theme.error
            }
            Item { Layout.fillWidth: true }
            AccentButton {
                title: "connect"
                disabled: DeviceService.busy
                onClicked: DeviceService.breach()
            }
        }
    }

    // ═══════════ Rail mode (narrow windows) ═══════════
    ColumnLayout {
        // Centered within the 64px rail glass at the island's right
        // edge — the host container stays 260 (pinned), so inset
        anchors.right: parent.right
        anchors.rightMargin: (64 - 36) / 2
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spaceLg
        anchors.bottomMargin: Theme.spaceLg
        width: 36
        opacity: root.railMode ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.motionSlow } }
        spacing: Theme.spaceMd

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 9
            implicitHeight: 9
            radius: 4.5
            color: DeviceService.connected ? Theme.green : Theme.error
        }
        // Rotated text keeps its UNrotated width for layout — wrap
        // it so the column centers the visual, not the phantom box.
        // Identity rides HIGH under the dot (library-side mirror);
        // the fillHeight spacer BELOW pushes glyphs/storage down.
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: railName.implicitWidth + Theme.spaceMd
            implicitWidth: 16
            Text {
                id: railName
                anchors.centerIn: parent
                text: DeviceService.connected
                      ? (DeviceService.name.length > 0
                         ? DeviceService.name : DeviceService.modelName)
                      : "no zune"
                rotation: 90
                font.pixelSize: 11
                font.letterSpacing: 3
                font.weight: Font.Light
                color: Theme.textSecondary
            }
        }
        Item { Layout.fillHeight: true }
        // Browse glyphs — CLONES of the library rail's icons: same
        // 40px cells, same 16→19→20 ramp, same colors, centered on y
        // by the fillHeight spacers flanking the group
        Repeater {
            model: DeviceService.connected
                   ? [{g: "♪", p: "deviceMusic"}, {g: "▶", p: "deviceVideos"},
                      {g: "▣", p: "devicePictures"}, {g: "♫", p: "devicePlaylists"}]
                   : []
            delegate: Item {
                id: railGlyph
                required property var modelData
                property bool hovering: false
                readonly property bool active:
                    root.currentPage === modelData.p
                Layout.fillWidth: true
                implicitHeight: 40

                Text {
                    anchors.centerIn: parent
                    text: railGlyph.modelData.g
                    font.pixelSize: railGlyph.active ? 20
                                  : (railGlyph.hovering ? 19 : 16)
                    color: railGlyph.active ? Theme.activePink
                         : (railGlyph.hovering ? Theme.orange : Theme.textMid)
                    Behavior on font.pixelSize {
                        NumberAnimation { duration: Theme.motionBase; easing.type: Easing.InOutQuad }
                    }
                    Behavior on color { ColorAnimation { duration: Theme.motionBase } }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: railGlyph.hovering = true
                    onExited: railGlyph.hovering = false
                    onClicked: root.browsePageSelected(railGlyph.modelData.p)
                }
            }
        }
        Item { Layout.fillHeight: true }
        // Storage, turned upright
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            visible: DeviceService.connected
            implicitWidth: 6
            implicitHeight: 72
            radius: 3
            color: Theme.cardHover
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: parent.height * (DeviceService.capacityGB > 0
                    ? 1 - DeviceService.freeGB / DeviceService.capacityGB : 0)
                radius: 3
                color: Theme.pink
            }
        }

        // Click anywhere unclaimed → expand the full panel
        MouseArea {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 36
            Layout.preferredHeight: 30
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expandRequested()
            Text {
                anchors.centerIn: parent
                text: "«"
                font.pixelSize: 14
                color: Theme.textDim
            }
        }
    }

    // » compress — floats bottom-left of the full panel, only while
    // the window is narrow and the panel is pinned open
    Text {
        visible: root.compressVisible && !root.railMode
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceMd
        anchors.bottomMargin: Theme.spaceMd
        z: 50
        text: "»"
        font.pixelSize: 14
        color: panelCompressArea.containsMouse ? Theme.textPrimary : Theme.textDim
        MouseArea {
            id: panelCompressArea
            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.compressRequested()
        }
    }
}
