import QtQuick
import QtQuick.Layouts
import Zuuned

ColumnLayout {
    id: controls
    property var player: PlayerService
    property bool showIslandButton: false
    property bool compact: false
    component ControlButton: SpokenButton { padding: controls.compact ? Theme.spaceXxs : Theme.spaceSm; horizontalPadding: Theme.spaceXs; font.pixelSize: Theme.spokenCaption }
    signal islandRequested()
    readonly property var current: player.queueIndex >= 0 && player.queueIndex < player.queue.length ? player.queue[player.queueIndex] : ({})
    readonly property bool book: player.mediaKind === "audiobook"
    implicitWidth: Theme.spokenTransportWidth
    spacing: Theme.spaceXxs
    function time(ms) { const s = Math.max(0, Math.floor(ms / 1000)); return Math.floor(s / 60) + ":" + (s % 60).toString().padStart(2, "0") }
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.spaceXxs
        ControlButton { objectName: "spokenBack15"; text: "↶ 15"; enabled: !controls.player.stopped; onClicked: controls.player.seekMs(Math.max(0, controls.player.positionMs - 15000)) }
        ControlButton { objectName: "spokenPlayPause"; text: controls.player.playing ? "Ⅱ" : "▶"; onClicked: controls.player.playPause() }
        ControlButton { objectName: "spokenForward30"; text: "30 ↷"; enabled: !controls.player.stopped; onClicked: controls.player.seekMs(Math.min(controls.player.durationMs, controls.player.positionMs + 30000)) }
        ControlButton { visible: controls.compact; text: controls.player.playbackRate + "×"; onClicked: rateMenu.popup() }
        ControlButton { visible: controls.compact; text: "+ mark"; enabled: !!controls.current.spokenId && !controls.player.stopped; onClicked: SpokenMediaService.addBookmark(controls.current.spokenId, controls.player.positionMs, "") }
        ControlButton { visible: controls.compact; text: "···"; onClicked: optionsMenu.popup() }
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: controls.time(controls.player.positionMs) + " / " + controls.time(controls.player.durationMs)
        color: Theme.textDim; font.pixelSize: Theme.spokenCaption
    }
    RowLayout {
        visible: !controls.compact
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.spaceXxs
        ControlButton { objectName: "spokenRate"; text: controls.player.playbackRate.toFixed(2).replace(/0$/, "") + "×"; onClicked: rateMenu.popup() }
        ControlButton { objectName: "spokenBookmark"; text: "+ mark"; enabled: !!controls.current.spokenId && !controls.player.stopped; onClicked: SpokenMediaService.addBookmark(controls.current.spokenId, controls.player.positionMs, "") }
        ControlButton { objectName: "spokenOptions"; text: "···"; onClicked: optionsMenu.popup() }
    }
    ZuneMenu {
        id: rateMenu
        Repeater {
            model: [0.75, 1, 1.25, 1.5, 1.75, 2]
            delegate: ZuneMenuItem { required property real modelData; text: modelData + "×"; onTriggered: controls.player.setSpokenRate(modelData) }
        }
    }
    ZuneMenu {
        id: optionsMenu
        ZuneMenuItem { text: "open now playing"; visible: controls.showIslandButton; onTriggered: controls.islandRequested() }
        ZuneMenuItem { text: "previous chapter / part"; visible: controls.book; enabled: !controls.player.stopped; onTriggered: SpokenMediaService.skipChapter(-1) }
        ZuneMenuItem { text: "next chapter / part"; visible: controls.book; enabled: !controls.player.stopped; onTriggered: SpokenMediaService.skipChapter(1) }
        ZuneMenuItem { text: "mark played"; enabled: !!controls.current.spokenId; onTriggered: SpokenMediaService.markPlayed(controls.current.spokenId, true) }
        ZuneMenuItem { text: "sleep in 15 minutes"; enabled: !controls.player.stopped; onTriggered: controls.player.setSleepMinutes(15) }
        ZuneMenuItem { text: "sleep in 30 minutes"; enabled: !controls.player.stopped; onTriggered: controls.player.setSleepMinutes(30) }
        ZuneMenuItem { text: "sleep in 60 minutes"; enabled: !controls.player.stopped; onTriggered: controls.player.setSleepMinutes(60) }
        ZuneMenuItem { text: "cancel sleep timer"; visible: controls.player.sleepMinutes > 0; onTriggered: controls.player.setSleepMinutes(0) }
    }
    Text { Layout.alignment: Qt.AlignHCenter; visible: controls.player.sleepMinutes > 0; text: "sleep timer on"; color: Theme.activePink; font.pixelSize: Theme.spokenCaption }
}
