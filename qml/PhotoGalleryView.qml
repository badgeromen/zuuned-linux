import QtQuick
import QtQuick.Controls.Basic
import Zuuned

// Fullscreen photo gallery — Phase 8's centerpiece, shared by the
// library AND device photo pages (the piece the mac app never had).
//
// open(photos, index): photos = [{url, filename, ...}]. Keyboard ←→,
// Esc, space = slideshow; click zones left/right; filmstrip along the
// bottom (the episode-ribbon pattern) for direct jumps; counter chip
// top-right. Chrome auto-hides on mouse idle like the video player.
//
// Device photos can pass thumbnail urls plus a `resolver` — a function
// (index, callback) that produces a full-res source later (USB
// extract); the viewer shows the thumb immediately and swaps in the
// full image when the callback lands.
Rectangle {
    id: gallery

    property var photos: []
    property int index: 0
    property var resolver: null      // optional: (index, cb(url)) → full-res
    property bool slideshow: false
    property bool chromeShown: true
    // full-res urls delivered by the resolver, keyed by index
    property var resolved: ({})
    // The opening page owns IDs and actions: a device item never passes
    // through a library-ID API. The viewer only presents that contract.
    property var device: DeviceService
    property var actions: ({})
    property int session: 0
    readonly property bool deviceMode: actions.deviceMode === true
    readonly property bool canTransfer: device.connected && typeof actions.action === "function"
    readonly property bool localAlbumDrag: !deviceMode && actions.localAlbumDrag === true
    readonly property bool canDrag: (canTransfer || localAlbumDrag)
        && typeof actions.payload === "function"
    property int activeMediaDrags: 0
    readonly property bool mediaDragging: activeMediaDrags > 0
    opacity: mediaDragging ? 0 : 1

    visible: false
    color: Qt.rgba(0, 0, 0, 0.97)
    focus: visible

    function open(list, startIndex, resolveFn, actionOptions) {
        session++
        actions = actionOptions || ({})
        photos = list ?? []
        if (photos.length === 0)
            return
        resolver = resolveFn ?? null
        resolved = {}
        index = Math.max(0, Math.min(startIndex ?? 0, photos.length - 1))
        slideshow = false
        visible = true
        chromeShown = true
        chromeTimer.restart()
        gallery.forceActiveFocus()
        requestFull(index)
    }
    function close() {
        session++
        photoMenu.close()
        visible = false
        slideshow = false
        photos = []
        resolved = {}
        actions = ({})
    }
    function transferAt(i) {
        if (!canTransfer || i < 0 || i >= photos.length) return
        actions.action(photos[i])
        showChrome()
    }
    function payloadAt(i) {
        if ((!device.connected && !localAlbumDrag) || typeof actions.payload !== "function"
            || i < 0 || i >= photos.length) return []
        return [actions.payload(photos[i])]
    }
    function showActions(i) {
        if (!canTransfer) return
        photoMenu.photoIndex = i
        photoMenu.popup()
        showChrome()
    }
    function step(delta) {
        if (photos.length === 0)
            return
        index = (index + delta + photos.length) % photos.length
        requestFull(index)
        showChrome()
    }
    function showChrome() {
        chromeShown = true
        chromeTimer.restart()
    }
    function sourceFor(i) {
        if (resolved[i] !== undefined)
            return resolved[i]
        return i >= 0 && i < photos.length ? (photos[i].url ?? "") : ""
    }
    function requestFull(i) {
        if (!resolver || resolved[i] !== undefined)
            return
        const gen = i
        const requestSession = session
        resolver(gen, function(url) {
            if (!gallery.visible || !url || requestSession !== gallery.session)
                return
            // Copy-reassign — same-reference var writes don't emit
            // changed, the image would never swap to full-res.
            const r = Object.assign({}, gallery.resolved)
            r[gen] = url
            gallery.resolved = r
        })
    }

    Timer {
        id: chromeTimer
        interval: 2000
        onTriggered: gallery.chromeShown = false
    }
    Timer {
        id: slideTimer
        interval: 4000
        repeat: true
        running: gallery.slideshow && gallery.visible
        onTriggered: gallery.step(1)
    }

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Left: step(-1); break
        case Qt.Key_Right: step(1); break
        case Qt.Key_Space: slideshow = !slideshow; showChrome(); break
        case Qt.Key_Escape: close(); break
        default: return
        }
        event.accepted = true
    }

    // ── Main image (current), with neighbor pre-warm ──
    Image {
        id: mainImg
        anchors.fill: parent
        anchors.margins: 8
        source: gallery.visible ? gallery.sourceFor(gallery.index) : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
        smooth: true
    }
    // Pre-decode the neighbors so stepping is instant
    Image {
        visible: false
        source: gallery.visible && gallery.photos.length > 1
            ? gallery.sourceFor((gallery.index + 1) % gallery.photos.length) : ""
        asynchronous: true
    }
    Image {
        visible: false
        source: gallery.visible && gallery.photos.length > 1
            ? gallery.sourceFor((gallery.index - 1 + gallery.photos.length)
                                % gallery.photos.length) : ""
        asynchronous: true
    }
    // Loading shimmer while a resolver fetch is in flight
    Text {
        anchors.centerIn: parent
        visible: mainImg.status === Image.Loading
        text: "loading…"
        font.pixelSize: 13
        color: Theme.textDim
    }

    // ── Mouse: move shows chrome, click zones page, center toggles ──
    MouseArea {
        id: imageArea
        objectName: "galleryPhotoArea"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        readonly property bool dragArmed: gallery.canDrag
        drag.target: dragArmed ? imageGhost : null
        preventStealing: dragArmed
        drag.onActiveChanged: {
            gallery.activeMediaDrags = Math.max(0, gallery.activeMediaDrags + (drag.active ? 1 : -1))
            if (drag.active) gallery.slideshow = false
        }
        onPressed: mouse => imageGhost.place(imageArea, mouse)
        onReleased: imageGhost.drop()
        cursorShape: gallery.chromeShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: gallery.showChrome()
        onClicked: mouseEvent => {
            if (mouseEvent.button === Qt.RightButton) {
                gallery.showActions(gallery.index)
                return
            }
            if (mouseEvent.x < width * 0.25)
                gallery.step(-1)
            else if (mouseEvent.x > width * 0.75)
                gallery.step(1)
            else
                gallery.showChrome()
        }
    }
    DragGhost {
        id: imageGhost
        area: imageArea
        localPhotoDrag: gallery.localAlbumDrag
        dragKind: gallery.deviceMode ? "device-photos" : "photos"
        dragName: gallery.photos[gallery.index]?.filename || ""
        dragTracks: () => gallery.payloadAt(gallery.index)
        Rectangle {
            width: Theme.spaceHuge + Theme.spaceLg
            height: width
            color: Theme.artworkPlaceholder
            radius: Theme.radiusMd
            clip: true
            Image {
                anchors.fill: parent
                source: gallery.sourceFor(gallery.index)
                fillMode: Image.PreserveAspectCrop
            }
        }
    }

    // Edge arrows (chrome-tied)
    Repeater {
        model: [{ glyph: "‹", left: true }, { glyph: "›", left: false }]
        delegate: Text {
            required property var modelData
            anchors.verticalCenter: parent.verticalCenter
            x: modelData.left ? 24 : gallery.width - width - 24
            text: modelData.glyph
            font.pixelSize: 48
            color: arrowArea.containsMouse ? Theme.activePink
                                           : Qt.rgba(1, 1, 1, 0.4)
            opacity: gallery.chromeShown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            MouseArea {
                id: arrowArea
                anchors.fill: parent
                anchors.margins: -14
                hoverEnabled: true
                onClicked: gallery.step(parent.modelData.left ? -1 : 1)
            }
        }
    }

    // ── Top chrome: filename, counter, slideshow, close ──
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 64
        opacity: gallery.chromeShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.7) }
            GradientStop { position: 1; color: "transparent" }
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: gallery.photos.length > 0 && gallery.index < gallery.photos.length
                ? (gallery.photos[gallery.index].filename ?? "") : ""
            font.pixelSize: 13
            color: Theme.textPrimary
            elide: Text.ElideMiddle
            width: parent.width * 0.5
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            Text {
                objectName: "galleryPhotoTransfer"
                anchors.verticalCenter: parent.verticalCenter
                visible: gallery.canTransfer
                text: gallery.deviceMode ? "⤓ save to library" : "+ queue for device"
                font.pixelSize: Theme.spaceMd
                color: transferArea.containsMouse ? Theme.activePink : Theme.pink
                MouseArea {
                    id: transferArea
                    anchors.fill: parent
                    anchors.margins: -Theme.spaceXs
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: gallery.transferAt(gallery.index)
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (gallery.index + 1) + " / " + gallery.photos.length
                font.pixelSize: 12
                color: Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: gallery.slideshow ? "⏸" : "▶"
                font.pixelSize: 15
                color: slideArea.containsMouse ? Theme.activePink : Theme.textSecondary
                MouseArea {
                    id: slideArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    onClicked: { gallery.slideshow = !gallery.slideshow
                                 gallery.showChrome() }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                font.pixelSize: 15
                color: closeArea.containsMouse ? Theme.activePink : Theme.textSecondary
                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    anchors.margins: -8
                    hoverEnabled: true
                    onClicked: gallery.close()
                }
            }
        }
    }

    // ── Filmstrip (episode-ribbon pattern) ──
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 14
        width: Math.min(strip.contentWidth + 24, gallery.width - 48)
        height: 74
        radius: 12
        color: Qt.rgba(0, 0, 0, 0.55)
        border.color: Theme.glassBorder
        opacity: gallery.chromeShown ? 1 : 0
        enabled: gallery.chromeShown
        Behavior on opacity { NumberAnimation { duration: 200 } }

        ListView {
            id: strip
            anchors.fill: parent
            anchors.margins: 8
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            model: gallery.visible ? gallery.photos : []
            currentIndex: gallery.index
            highlightFollowsCurrentItem: true
            preferredHighlightBegin: width / 2 - 29
            preferredHighlightEnd: width / 2 + 29
            highlightRangeMode: ListView.ApplyRange
            delegate: Rectangle {
                id: filmPhoto
                objectName: "galleryFilmPhoto" + index
                required property var modelData
                required property int index
                width: 58
                height: 58
                radius: 5
                color: Qt.rgba(1, 1, 1, 0.06)
                border.color: index === gallery.index
                    ? Theme.activePink : Theme.glassBorder
                border.width: index === gallery.index ? 2 : 1
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: modelData.url ?? ""
                    sourceSize.width: 116
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                MouseArea {
                    id: filmArea
                    objectName: "galleryFilmArea" + filmPhoto.index
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    readonly property bool dragArmed: gallery.canDrag
                    drag.target: dragArmed ? filmGhost : null
                    preventStealing: dragArmed
                    drag.onActiveChanged: {
                        gallery.activeMediaDrags = Math.max(0, gallery.activeMediaDrags + (drag.active ? 1 : -1))
                        if (drag.active) gallery.slideshow = false
                    }
                    onPressed: mouse => filmGhost.place(filmArea, mouse)
                    onReleased: filmGhost.drop()
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            gallery.showActions(filmPhoto.index)
                            return
                        }
                        gallery.index = parent.index
                        gallery.requestFull(parent.index)
                        gallery.showChrome()
                    }
                }
                SplitAddButton {
                    objectName: "galleryFilmTransfer" + filmPhoto.index
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: Theme.spaceXxxs
                    visible: gallery.canTransfer && (filmArea.containsMouse || hovered)
                    glyph: gallery.deviceMode ? "⤓" : "＋"
                    showPick: false
                    onAddAll: gallery.transferAt(filmPhoto.index)
                }
                DragGhost {
                    id: filmGhost
                    area: filmArea
                    localPhotoDrag: gallery.localAlbumDrag
                    dragKind: gallery.deviceMode ? "device-photos" : "photos"
                    dragName: filmPhoto.modelData.filename || ""
                    dragTracks: () => gallery.payloadAt(filmPhoto.index)
                    Rectangle {
                        width: Theme.spaceHuge + Theme.spaceLg
                        height: width
                        color: Theme.artworkPlaceholder
                        radius: Theme.radiusMd
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: filmPhoto.modelData.url || ""
                            fillMode: Image.PreserveAspectCrop
                        }
                    }
                }
            }
        }
    }
    ZuneMenu {
        id: photoMenu
        property int photoIndex: -1
        ZuneMenuItem {
            text: gallery.deviceMode ? "Save to Library" : "Queue for Device"
            enabled: gallery.canTransfer
            onTriggered: gallery.transferAt(photoMenu.photoIndex)
        }
    }
}
