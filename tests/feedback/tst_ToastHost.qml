import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtTest
import Zuuned

Rectangle {
    id: host
    visible: true
    width: 940
    height: 620
    color: Theme.bg
    property int backgroundClicks: 0
    MouseArea { id: background; anchors.fill: parent; onClicked: host.backgroundClicks++ }
    Popup {
        id: editor
        x: (host.width - width) / 2
        y: (host.height - height) / 2
        width: host.width - 100
        height: host.height - 100
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: Theme.surfaceBg }
    }
    ToastHost {
        id: notice
        parent: host.Overlay.overlay ?? host
        z: Theme.toastOverlayZ
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: editorFocused ? Theme.toastModalClearance : Theme.spaceLg
    }
    SignalSpy { id: actions; target: notice; signalName: "actionTriggered" }
    TestCase {
        name: "ToastHost"
        when: windowShown
        function init() {
            host.Window.window.width = 940; host.Window.window.height = 620
            host.width = 940; host.height = 620
            editor.close(); notice.dismiss(); actions.clear()
            notice.displayDuration = Theme.toastDuration
            host.backgroundClicks = 0
            mouseMove(background, 4, 4)
            background.forceActiveFocus()
            wait(Theme.motionFast + 20)
        }
        function show(text, action) {
            notice.show(text, action)
            tryCompare(notice, "opacity", 1)
            verify(notice.parent.visible)
            verify(notice.width > 0 && notice.height >= 32)
        }
        function test_plain_text_and_rapid_replacement() {
            show("saved to your library", "View")
            for (let i = 0; i < 30; ++i) notice.show("queued " + i + " tracks")
            compare(notice.text, "queued 29 tracks")
            compare(notice.actionLabel, "")
            compare(findChild(notice, "notificationAction").visible, false)
            notice.dismiss()
            wait(Theme.motionFast + 20)
            compare(notice.visible, false)
            show("<b>your file</b> was saved")
            compare(findChild(notice, "notificationText").textFormat, Text.PlainText)
        }
        function test_pointer_action_and_dismiss() {
            show("added to your library", "View")
            const action = findChild(notice, "notificationAction")
            const dismiss = findChild(notice, "notificationDismiss")
            verify(action.width >= 32 && action.height >= 32)
            verify(dismiss.width >= 32 && dismiss.height >= 32)
            mouseClick(action)
            compare(actions.count, 1)
            compare(notice.active, false)
            show("queued 3 tracks")
            mouseClick(dismiss)
            compare(notice.active, false)
        }
        function test_hover_pauses_remaining_time() {
            notice.displayDuration = 500
            notice.show("hover keeps this here")
            wait(100)
            mouseMove(notice, notice.width / 2, notice.height / 2)
            tryCompare(notice, "held", true)
            wait(650)
            compare(notice.active, true)
            mouseMove(background, 4, 4)
            tryCompare(notice, "held", false)
            tryCompare(notice, "active", false, 650)
        }
        function test_wrapping_and_bounded_overflow() {
            host.Window.window.width = 360
            host.width = 360
            show("Saved a long album title from your collection to the Zune transfer queue. "
                 + "This is a complete readable message, including unusual filenames_".repeat(5), "Settings →")
            const message = findChild(notice, "notificationText")
            const scroll = findChild(notice, "notificationTextScroll")
            verify(notice.width <= host.width - Theme.spaceXxxl)
            verify(message.lineCount > 1)
            verify(message.width > 100)
            verify(notice.height <= Theme.toastMaxTextHeight + Theme.spaceSm * 2)
            verify(scroll.contentHeight > scroll.height)
            verify(scroll.interactive)
        }
        function test_no_full_window_input_blanket() {
            show("saved to library")
            mouseClick(background, 10, 10)
            compare(host.backgroundClicks, 1)
            compare(notice.active, true)
        }
        function test_pointer_above_modal() {
            editor.open()
            tryCompare(editor, "opened", true)
            show("saved to library", "View")
            mouseClick(findChild(notice, "notificationAction"))
            compare(actions.count, 1)
            compare(editor.opened, true)
        }
        function test_modal_position_survives_action_focus() {
            editor.open()
            tryCompare(editor, "opened", true)
            show("saved to library", "View")
            tryCompare(notice, "editorFocused", true)
            const before = notice.y
            findChild(notice, "notificationAction").forceActiveFocus()
            tryCompare(notice, "held", true)
            compare(notice.editorFocused, true)
            compare(notice.y, before)
            editor.close()
            tryCompare(notice, "editorFocused", false)
        }
    }
}
