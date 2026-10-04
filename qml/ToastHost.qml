import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Zuuned

// A quiet status strip. Every notice replaces the previous one; imports do
// not build a notification backlog. Hosts own its location in the overlay.
Item {
    id: root
    objectName: "notificationStrip"
    signal actionTriggered()
    property string text: ""
    property string actionLabel: ""
    property bool active: false
    property int displayDuration: Theme.toastDuration
    property real availableWidth: parent ? parent.width - Theme.spaceXxxl : Theme.toastMaxWidth
    readonly property bool held: hover.hovered || actionButton.activeFocus || dismissButton.activeFocus
    property int remaining: displayDuration
    property double deadline: 0
    property Item placementEditor: null
    readonly property bool editorFocused: placementEditor !== null && placementEditor.visible

    width: Math.max(0, Math.min(Theme.toastMaxWidth, availableWidth,
                               messageMetrics.width + actionButton.implicitWidth
                               + dismissButton.implicitWidth + Theme.spaceHuge))
    implicitHeight: Math.max(Theme.spaceXxxl, messageScroll.implicitHeight) + Theme.spaceSm * 2
    height: implicitHeight
    visible: active || opacity > 0
    enabled: active
    opacity: active ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    function show(message, action) {
        hideTimer.stop()
        text = String(message ?? "")
        actionLabel = action === undefined ? "" : String(action)
        remaining = displayDuration
        messageScroll.contentY = 0
        active = text.length > 0
        if (active && !held) resumeTimer()
    }
    function dismiss() { hideTimer.stop(); active = false }
    function resumeTimer() {
        if (!active || held) return
        deadline = Date.now() + remaining
        hideTimer.interval = Math.max(1, remaining)
        hideTimer.restart()
    }
    onHeldChanged: {
        if (!active) return
        if (held) {
            if (hideTimer.running) remaining = Math.max(1, deadline - Date.now())
            hideTimer.stop()
        } else resumeTimer()
    }
    function updatePlacementContext() {
        let item = root.Window.window ? root.Window.window.activeFocusItem : null
        while (item && item.parent !== root.parent) item = item.parent
        // Keyboard or pointer focus inside the strip retains the editor's
        // placement. Its visibility still releases this when the editor closes.
        if (item !== root) placementEditor = item
    }
    onActiveChanged: {
        if (!active) hideTimer.stop()
        else updatePlacementContext()
    }
    Component.onCompleted: updatePlacementContext()
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() { root.updatePlacementContext() }
    }

    Timer { id: hideTimer; onTriggered: root.dismiss() }
    HoverHandler { id: hover }
    TextMetrics {
        id: messageMetrics
        text: root.text
        font.pixelSize: Theme.toastTextSize
        font.weight: Font.Light
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceBg
        radius: Theme.radiusSm
        border.width: Theme.hairline
        border.color: Theme.borderLight
        // Only the strip consumes clicks; the host never blankets the
        // window or takes focus away from the current editor.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spaceSm
        anchors.bottomMargin: Theme.spaceSm
        width: Theme.spaceXxxs
        color: Theme.pink
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceLg
        anchors.rightMargin: Theme.spaceXs
        anchors.topMargin: Theme.spaceSm
        anchors.bottomMargin: Theme.spaceSm
        spacing: Theme.spaceSm

        Flickable {
            id: messageScroll
            objectName: "notificationTextScroll"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: Math.min(message.implicitHeight, Theme.toastMaxTextHeight)
            Layout.preferredHeight: implicitHeight
            contentWidth: width
            contentHeight: message.implicitHeight
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Text {
                id: message
                objectName: "notificationText"
                width: messageScroll.width
                text: root.text
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.toastTextSize
                font.weight: Font.Light
                color: Theme.textPrimary
            }
        }
        Button {
            id: actionButton
            objectName: "notificationAction"
            visible: root.actionLabel.length > 0
            implicitWidth: visible ? Math.max(Theme.spaceXxxl, actionText.implicitWidth + Theme.spaceLg) : 0
            implicitHeight: Theme.spaceXxxl
            Layout.preferredWidth: Math.min(implicitWidth, root.width / 3)
            Layout.minimumWidth: visible ? Theme.spaceXxxl : 0
            Layout.alignment: Qt.AlignVCenter
            hoverEnabled: true
            Accessible.name: root.actionLabel
            contentItem: Text {
                id: actionText
                text: root.actionLabel
                font.pixelSize: Theme.toastTextSize
                color: actionButton.hovered ? Theme.activePink : Theme.pink
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: actionButton.hovered ? Theme.cardHover : Theme.transparent
                border.width: actionButton.visualFocus ? Theme.spaceXxxs : 0
                border.color: Theme.focusRing
            }
            onClicked: { root.dismiss(); root.actionTriggered() }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
        Button {
            id: dismissButton
            objectName: "notificationDismiss"
            implicitWidth: Theme.spaceXxxl
            implicitHeight: Theme.spaceXxxl
            Layout.preferredWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            hoverEnabled: true
            Accessible.name: "Dismiss notification"
            contentItem: Text {
                text: "×"
                font.pixelSize: Theme.spaceXl
                color: dismissButton.hovered ? Theme.textPrimary : Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: dismissButton.hovered ? Theme.cardHover : Theme.transparent
                border.width: dismissButton.visualFocus ? Theme.spaceXxxs : 0
                border.color: Theme.focusRing
            }
            onClicked: root.dismiss()
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }
}
