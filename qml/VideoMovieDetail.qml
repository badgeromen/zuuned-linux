import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Zuuned

// UX-3 "M4 poster + dossier" (approved): full-height poster hero left —
// the POSTER is the play button (house rule: the object is the button),
// with the overview + genre chips living on the card itself. Right: the
// dossier — top billing (initials circles until cast headshots land),
// director, versions, file facts, and a same-shelf row of genre
// neighbors. Below the narrow breakpoint the dossier flows underneath
// the hero in one scrolling column.
Item {
    id: page

    property var browser
    property double videoId: -1

    signal back()
    signal queueRequested(var movie)
    signal playRequested(var movie)
    signal movieSelected(double id)
    signal customizeRequested(var movie, var ids)
    signal menuRequested(var movie)
    property var movieSyncItemFn: null

    property var movie: ({})
    function refresh() {
        if (!browser || videoId < 0) return
        const m = browser.movieDetail(videoId)
        if (m.id === undefined) {
            // Movie vanished (deleted from library) — pop back.
            page.back()
            return
        }
        page.movie = m
    }
    Component.onCompleted: refresh()
    onVideoIdChanged: refresh()   // same-shelf navigation re-targets the page
    Connections {
        target: LibraryService
        function onVideosChanged() { page.refresh() }
        // Surgical refresh fires this instead — the hero is a single
        // image, so re-fetching the row just repaints it (no reset).
        function onVideoRowsChanged(ids) {
            for (const id of ids)
                if (Number(id) === Number(page.videoId)) {
                    page.refresh()
                    return
                }
        }
    }

    readonly property bool narrow: width < 720

    // Hero poster keeps TRUE poster aspect (2:3) and fills the page
    // height: width follows from the available height, bounded only by
    // a share of the window so short-wide panes stay usable.
    readonly property real heroW:
        Math.max(220, Math.min(width * 0.32, (height - 290) * 2 / 3))

    function formatBytes(bytes) {
        if (bytes >= 1e9) return (bytes / 1e9).toFixed(2) + " GB"
        if (bytes >= 1e6) return (bytes / 1e6).toFixed(1) + " MB"
        return Math.round(bytes / 1e3) + " KB"
    }
    function formatDuration(ms) {
        const secs = Math.floor(ms / 1000)
        if (secs >= 3600)
            return Math.floor(secs / 3600) + "h "
                 + Math.floor((secs % 3600) / 60) + "m"
        return Math.floor(secs / 60) + " min"
    }
    function formatClock(ms) {
        const secs = Math.floor(ms / 1000)
        const m = Math.floor(secs / 60)
        const s = secs % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    readonly property var castNames: {
        const c = page.movie.cast ?? ""
        return c === "" ? [] : c.split(",").map(n => n.trim()).filter(n => n !== "")
    }
    function initialsOf(name) {
        const parts = name.split(" ").filter(p => p !== "")
        if (parts.length === 0) return "?"
        if (parts.length === 1) return parts[0].charAt(0).toUpperCase()
        return (parts[0].charAt(0) + parts[parts.length - 1].charAt(0)).toUpperCase()
    }

    readonly property var genreList: {
        const g = page.movie.genres ?? ""
        return g === "" ? [] : g.split(",").map(x => x.trim()).filter(x => x !== "")
    }

    // Same shelf: genre neighbors, best overlap first. Pure QML over
    // the browser's movieGroups — no new queries needed.
    readonly property var sameShelf: {
        if (page.genreList.length === 0) return []
        const mine = page.genreList
        return (browser.movieGroups ?? [])
            .filter(m => m.id !== page.movie.id && (m.genres ?? "") !== "")
            .map(m => ({ m: m, shared: m.genres.split(",")
                            .map(x => x.trim()).filter(g => mine.includes(g)).length }))
            .filter(e => e.shared > 0)
            .sort((a, b) => b.shared - a.shared)
            .slice(0, 6)
            .map(e => e.m)
    }

    // Mac resume rules: only when >60s in and not inside the last 30s.
    readonly property int resumeMs: {
        const pos = page.movie.lastPositionMs ?? 0
        const dur = page.movie.durationMs ?? 0
        return (pos > 60000 && (dur === 0 || pos < dur - 30000)) ? pos : 0
    }

    readonly property var versionIds:
        (page.movie.versions ?? []).length > 0
            ? page.movie.versions.map(v => v.id)
            : [page.movie.id]

    function menuFor(m, ids) {
        page.menuRequested(Object.assign({}, m, {versionIds: ids ?? [m.id]}))
    }
    function queueMovie(m) {
        if (DeviceService.connected) page.queueRequested(m)
    }
    component MovieGesture: MouseArea {
        id: gesture
        objectName: "movieGesture"
        property var row: ({})
        property var ids: [row.id]
        property bool navigate: false
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        drag.target: DeviceService.connected && page.movieSyncItemFn ? movieGhost : null
        preventStealing: DeviceService.connected
        onPressed: mouse => movieGhost.place(gesture, mouse)
        onReleased: movieGhost.drop()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) page.menuFor(row, ids)
            else if (navigate) page.movieSelected(row.id)
            else page.playRequested(row)
        }
        DragGhost {
            id: movieGhost
            area: gesture
            dragKind: "videos"
            dragName: gesture.row.title ?? ""
            dragTracks: () => page.movieSyncItemFn ? [page.movieSyncItemFn(gesture.row)] : []
            Rectangle {
                width: Theme.spaceXxl; height: width * 1.5
                color: Theme.artworkPlaceholder; radius: Theme.radiusSm
                ArtworkImage { anchors.fill: parent; source: gesture.row.poster ?? ""; fillMode: Image.PreserveAspectCrop }
            }
        }
    }

    // ═══ The poster hero — full height, the play button ═══
    component HeroPoster: Item {
        id: hero
        property bool hovering: posterArea.containsMouse

        // Frame behind (rounded bg + border); the shadow sources it.
        Rectangle {
            id: posterCard
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.artworkPlaceholder
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }

        // Visual content, rendered offscreen then rounded by the
        // OpacityMask below. (layer.effect on the container did NOT
        // round the async Image here — PosterCard's sibling-OpacityMask
        // is the pattern that actually works in this app, 2026-09-07.)
        Item {
            id: posterVisual
            anchors.fill: parent
            visible: false

            ArtworkImage {
                anchors.fill: parent
                source: page.movie.poster ?? ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 640
            }
            // Overview + genre chips live ON the card (reference-true)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: scrimCol.implicitHeight + Theme.spaceXxl
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.35; color: Qt.rgba(0, 0, 0, 0.72) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.92) }
                }
                ColumnLayout {
                    id: scrimCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spaceMd
                    spacing: Theme.spaceXs

                    Text {
                        visible: (page.movie.description ?? "") !== ""
                        text: page.movie.description ?? ""
                        font.pixelSize: 11
                        font.weight: Font.Light
                        color: Qt.rgba(1, 1, 1, 0.75)
                        wrapMode: Text.WordWrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        visible: page.genreList.length > 0
                        spacing: Theme.spaceXxs
                        Repeater {
                            model: page.genreList.slice(0, 4)
                            delegate: Rectangle {
                                required property string modelData
                                radius: Theme.radiusSm
                                color: Qt.rgba(0, 0, 0, 0.5)
                                border.width: 1
                                border.color: Theme.borderLight
                                implicitWidth: chipText.implicitWidth + Theme.spaceMd
                                implicitHeight: chipText.implicitHeight + Theme.spaceXs
                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    font.pixelSize: 9
                                    color: Theme.textSecondary
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // Resume bar — sits on the card's bottom edge
            Rectangle {
                visible: page.resumeMs > 0 && (page.movie.durationMs ?? 0) > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 3
                color: Qt.rgba(1, 1, 1, 0.15)
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * page.resumeMs
                           / Math.max(1, page.movie.durationMs)
                    color: Theme.pink
                }
            }
        }
        Rectangle {
            id: posterMask
            anchors.fill: parent
            radius: Theme.radiusMd
            visible: false
        }
        OpacityMask {
            anchors.fill: parent
            source: posterVisual
            maskSource: posterMask
        }

        // Live overlays on top (never masked — they stay interactive
        // and never touch the corners)
        Rectangle {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -parent.height * 0.08
            width: 56; height: 56; radius: 28
            visible: hero.hovering
            color: Qt.rgba(0, 0, 0, 0.45)
            border.width: 2
            border.color: Qt.rgba(1, 1, 1, 0.85)
            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 2
                text: "▶"
                font.pixelSize: 20
                color: "white"
            }
        }
        MovieGesture {
            id: posterArea
            anchors.fill: parent
            row: page.movie
            ids: page.versionIds
        }
        DropShadow {
            z: -1
            anchors.fill: posterCard
            source: posterCard
            radius: 22
            samples: 25
            verticalOffset: 8
            color: Qt.rgba(0, 0, 0, 0.6)
        }
    }

    // ═══ Identity under the poster ═══
    component HeroIdentity: ColumnLayout {
        spacing: Theme.spaceXs

        GradientText {
            Layout.maximumWidth: parent.width
            text: (page.movie.title ?? "").toLowerCase()
            font.family: Theme.displayFamily
            font.pixelSize: 28
            tracking: 1
            rotation: -1
            transformOrigin: Item.Left
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs
            Text {
                visible: (page.movie.rating ?? 0) > 0
                text: "★ " + Number(page.movie.rating ?? 0).toFixed(1)
                font.pixelSize: 11
                color: Theme.orange
            }
            Text {
                visible: (page.movie.year ?? "") !== ""
                text: "· " + page.movie.year
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            Text {
                visible: (page.movie.durationMs ?? 0) > 0
                text: "· " + page.formatDuration(page.movie.durationMs)
                font.pixelSize: 11
                color: Theme.textSecondary
            }
            Text {
                visible: page.resumeMs > 0
                text: "· resumes at " + page.formatClock(page.resumeMs)
                font.pixelSize: 11
                color: Theme.activePink
            }
            Item { Layout.fillWidth: true }
        }
        // Secondary verbs only — play lives on the poster.
        // Queue rightmost (house rule).
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXxs
            spacing: Theme.spaceMd

            Text {
                text: page.movie.watched ? "✔ watched" : "○ watched"
                font.pixelSize: 12
                color: page.movie.watched ? Theme.pink
                     : watchedArea.containsMouse ? Theme.textSecondary
                                                 : Theme.textDim
                MouseArea {
                    id: watchedArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: LibraryService.setVideosWatched(
                        [page.movie.id], !page.movie.watched)
                }
            }
            Text {
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
                    onClicked: page.customizeRequested(page.movie, page.versionIds)
                }
            }
            Item { Layout.fillWidth: true }
            OnDeviceBadge {
                size: 20
                visible: page.movie.isOnDevice === true
            }
            SplitAddButton {
                visible: DeviceService.connected
                showPick: false
                onAddAll: page.queueMovie(page.movie)
            }
        }
    }

    // ═══ Dossier building blocks (page scope — inline components
    // can't nest inside other inline components) ═══
    component InfoCard: Rectangle {
        default property alias content: cardCol.data
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + Theme.spaceLg * 2
        radius: Theme.radiusLg
        color: Theme.glassBg
        border.width: 1
        border.color: Theme.glassBorderSubtle
        ColumnLayout {
            id: cardCol
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spaceLg
            spacing: Theme.spaceSm
        }
    }
    component SectionLabel: Text {
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1
        color: Theme.textDim
    }
    component FileRow: ColumnLayout {
        property string label
        property string value
        spacing: 2
        Layout.fillWidth: true
        Text {
            text: parent.label
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 1
            color: Theme.textDim
        }
        Text {
            text: parent.value
            font.pixelSize: 13
            font.weight: Font.Light
            color: Theme.textSecondary
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
    }

    // ═══ The dossier ═══
    component Dossier: ColumnLayout {
        spacing: Theme.spaceLg

        // Top billing — initials circles until cast headshots land
        InfoCard {
            visible: page.castNames.length > 0
                     || (page.movie.director ?? "") !== ""
            SectionLabel {
                visible: page.castNames.length > 0
                text: "TOP BILLING"
            }
            RowLayout {
                visible: page.castNames.length > 0
                Layout.fillWidth: true
                spacing: Theme.spaceMd
                Repeater {
                    model: page.castNames
                    delegate: ColumnLayout {
                        required property string modelData
                        spacing: Theme.spaceXxs
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 46; height: 46; radius: 23
                            color: Theme.cardBg
                            border.width: 1
                            border.color: Qt.rgba(0.83, 0.21, 0.48, 0.4)
                            Text {
                                anchors.centerIn: parent
                                text: page.initialsOf(parent.parent.modelData)
                                font.pixelSize: 15
                                font.weight: Font.Light
                                color: Theme.textSecondary
                            }
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 62
                            text: parent.modelData
                            font.pixelSize: 9
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            SectionLabel {
                visible: (page.movie.director ?? "") !== ""
                text: "DIRECTOR"
            }
            Text {
                visible: (page.movie.director ?? "") !== ""
                text: page.movie.director ?? ""
                font.pixelSize: 13
                font.weight: Font.Light
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
        }

        InfoCard {
            visible: (page.movie.versions ?? []).length > 1
            RowLayout {
                spacing: Theme.spaceSm
                SectionLabel { text: "VERSIONS" }
                Text {
                    text: (page.movie.versions ?? []).length
                          + " files for this movie"
                    font.pixelSize: 11
                    color: Theme.textDim
                }
            }
            Repeater {
                model: page.movie.versions ?? []
                delegate: RowLayout {
                    id: versionRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.spaceMd
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: versionInfo.implicitHeight
                        ColumnLayout {
                            id: versionInfo
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Theme.spaceXxxs
                            Text {
                                text: versionRow.modelData.versionLabel || versionRow.modelData.filename
                                font.pixelSize: 13
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: page.formatBytes(versionRow.modelData.filesize)
                                    + (versionRow.modelData.width > 0
                                       ? " · " + versionRow.modelData.width + "x" + versionRow.modelData.height : "")
                                font.pixelSize: 11
                                color: Theme.textSecondary
                            }
                        }
                        MovieGesture { anchors.fill: parent; row: versionRow.modelData }
                    }
                    Text {
                        text: "customize"
                        font.pixelSize: 12; color: Theme.textDim
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: page.customizeRequested(versionRow.modelData, [versionRow.modelData.id]) }
                    }
                    Text {
                        visible: DeviceService.connected
                        text: "to zune"
                        font.pixelSize: 12
                        color: Theme.pink
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.queueMovie(versionRow.modelData)
                        }
                    }
                }
            }
        }

        InfoCard {
            SectionLabel { text: "FILE" }
            FileRow {
                label: "QUALITY"
                value: {
                    const parts = []
                    if ((page.movie.width ?? 0) > 0)
                        parts.push(page.movie.width + " x " + page.movie.height)
                    parts.push(page.formatBytes(page.movie.filesize ?? 0))
                    if ((page.movie.durationMs ?? 0) > 0)
                        parts.push(page.formatDuration(page.movie.durationMs))
                    return parts.join(" · ")
                }
            }
            FileRow { label: "FILENAME"; value: page.movie.filename ?? "" }
            FileRow { label: "PATH"; value: page.movie.filepath ?? "" }
        }

        // Genre neighbors — pure QML over the browser's movie groups
        InfoCard {
            visible: page.sameShelf.length > 0
            SectionLabel { text: "FROM THE SAME SHELF" }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceMd
                Repeater {
                    model: page.sameShelf
                    delegate: ColumnLayout {
                        required property var modelData
                        spacing: Theme.spaceXxs
                        Rectangle {
                            id: shelfPoster
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 96
                            radius: Theme.radiusMd
                            color: Theme.cardBg
                            clip: true
                            // Rounded ART needs a mask — a radius on the container
                            // doesn't clip the Image (QML clip is rectangular)
                            layer.enabled: true
                            layer.effect: OpacityMask {
                                maskSource: Rectangle {
                                    width: shelfPoster.width; height: shelfPoster.height
                                    radius: Theme.radiusMd
                                }
                            }
                            border.width: 1
                            border.color: shelfArea.containsMouse
                                          ? Qt.rgba(0.83, 0.21, 0.48, 0.5)
                                          : Theme.borderFaint
                            ArtworkImage {
                                anchors.fill: parent
                                source: parent.parent.modelData.poster ?? ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: 128
                            }
                            MovieGesture {
                                id: shelfArea
                                anchors.fill: parent
                                row: parent.parent.modelData
                                ids: row.versionIds ?? [row.id]
                                navigate: true
                            }
                        }
                        Text {
                            Layout.preferredWidth: 64
                            text: parent.modelData.title
                            font.pixelSize: 9
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        Item { Layout.preferredHeight: Theme.spaceLg }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // (Back bar removed 2026-09-06: the HOST keeps the pivot band
        // and draws the breadcrumb row — drill chrome is app chrome.)

        // ═══ WIDE: hero | dossier ═══
        RowLayout {
            visible: !page.narrow
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spaceXl

            ColumnLayout {
                // NOT fillWidth: nested layouts default to fillWidth
                // true, which made this column eat the row and crush
                // the dossier against the right edge.
                Layout.fillWidth: false
                Layout.preferredWidth: page.heroW
                Layout.maximumWidth: page.heroW
                Layout.fillHeight: true
                Layout.leftMargin: Gallery.spine(page.width)
                Layout.topMargin: Theme.spaceLg
                Layout.bottomMargin: Theme.spaceLg
                spacing: Theme.spaceSm

                HeroPoster {
                    Layout.preferredWidth: page.heroW
                    Layout.preferredHeight: page.heroW * 1.5
                }
                HeroIdentity { Layout.fillWidth: true }
                Item { Layout.fillHeight: true }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Hero + dossier together fill the page's standard
                // frame — the poster stays the big thing, the dossier
                // takes the rest of the frame (2026-09-06)
                Layout.maximumWidth: Math.max(420,
                    Gallery.frameW(page.width) - page.heroW - Theme.spaceXl)
                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                Layout.topMargin: Theme.spaceLg
                Layout.bottomMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceXl
                clip: true
                contentHeight: wideDossier.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ZuneScrollBar {
                    parent: page
                    visible: !page.narrow && size < 1.0
                    anchors.top: parent.top
                    anchors.topMargin: 64
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                }

                Dossier {
                    id: wideDossier
                    width: parent.width
                }
            }
        }

        // ═══ NARROW: one scrolling column ═══
        Flickable {
            visible: page.narrow
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Gallery.spine(page.width)
            Layout.rightMargin: Theme.spaceXl
            Layout.topMargin: Theme.spaceLg
            clip: true
            contentHeight: narrowCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ZuneScrollBar {
                parent: page
                visible: page.narrow && size < 1.0
                anchors.top: parent.top
                anchors.topMargin: 64
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }

            ColumnLayout {
                id: narrowCol
                width: parent.width
                spacing: Theme.spaceLg

                HeroPoster {
                    Layout.preferredWidth: Math.min(280, narrowCol.width)
                    Layout.preferredHeight: Math.min(280, narrowCol.width) * 1.5
                }
                HeroIdentity { Layout.fillWidth: true }
                Dossier { Layout.fillWidth: true }
            }
        }
    }
}
