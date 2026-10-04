import QtQuick
import QtQuick.Controls.Basic

// Quiet liner-note fields: type and an underline, with room for focus.
Item {
    id: root
    property string label: ""
    property string value: ""
    property string placeholder: ""
    property bool prominent: false
    property bool multiline: false
    property bool numeric: false
    property alias validator: input.validator
    signal edited(string value)
    function focusInput(selectAll) {
        const editor = multiline ? paragraph : input
        editor.forceActiveFocus()
        if (selectAll) editor.selectAll()
    }
    implicitHeight: multiline ? Theme.customizeFieldHeight * 1.7 : Theme.customizeFieldHeight
    Text {
        id: caption
        anchors.top: parent.top; width: parent.width
        text: root.label.toUpperCase()
        font.pixelSize: Theme.customizeLabelSize
        font.letterSpacing: 1.3
        color: Theme.textSecondary
    }
    TextField {
        id: input
        objectName: "customizeFieldInput"
        visible: !root.multiline
        anchors.top: caption.bottom; anchors.topMargin: Theme.spaceXxs
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.spaceSm
        text: root.value
        placeholderText: root.placeholder
        color: Theme.textPrimary; placeholderTextColor: Theme.textSubtle
        selectionColor: Theme.pink; selectedTextColor: Theme.textPrimary
        font.pixelSize: root.prominent ? Theme.customizeHeadingSize : Theme.customizeBodySize
        font.weight: root.prominent ? Font.Light : Font.Normal
        padding: 0
        selectByMouse: true
        inputMethodHints: root.numeric ? Qt.ImhDigitsOnly : Qt.ImhNone
        validator: RegularExpressionValidator { regularExpression: root.numeric ? /[0-9]{0,4}/ : /[^\n]*/ }
        background: null
        Accessible.name: root.label
        onTextEdited: root.edited(text)
    }
    TextArea {
        id: paragraph
        objectName: "customizeFieldParagraph"
        visible: root.multiline
        anchors.top: caption.bottom; anchors.topMargin: Theme.spaceXxs
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.spaceSm
        text: root.value
        placeholderText: root.placeholder
        wrapMode: TextEdit.Wrap
        color: Theme.textPrimary; placeholderTextColor: Theme.textSubtle
        selectionColor: Theme.pink; selectedTextColor: Theme.textPrimary
        font.pixelSize: Theme.customizeBodySize
        padding: 0
        selectByMouse: true
        background: null
        Accessible.name: root.label
        // TextArea has no user-only edit signal on our minimum Qt 6.5.
        // Ignore binding updates (reopen, reset, match) even if focus stays here.
        onTextChanged: if (activeFocus && text !== root.value) root.edited(text)
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: input.activeFocus || paragraph.activeFocus ? Theme.activePink : Theme.borderLight
        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }
}
