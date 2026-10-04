import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The ▾ quick picker — cherry-pick a container's contents without
// drilling in. Two shapes:
//   flat:    items = [{id, label, sublabel}]
//   grouped: items = [{id, label, sublabel, children:[{id,…}]}]
// Grouped (e.g. a TV series) shows GROUP rows first — tri-state
// checkbox takes/clears the whole season, clicking the row drills
// into its episodes; selections accumulate across groups. Nobody
// scrolls 278 flat episodes.
Popup {
    id: picker

    property string heading: ""
    property var items: []
    property string actionWord: "add"

    signal picked(var ids)

    // leaf id → true
    property var checks: ({})
    readonly property int checkedCount: Object.keys(checks).length
    readonly property bool grouped:
        items.length > 0 && items[0].children !== undefined
    // Drill state (grouped mode): null = group list
    property var openGroup: null

    function openFor(title, list) {
        heading = title
        items = list ?? []
        checks = {}
        openGroup = null
        open()
    }
    // Anchored open: next to the tile whose ▾ was clicked, clamped.
    function openAt(anchorItem, title, list) {
        heading = title
        items = list ?? []
        checks = {}
        openGroup = null
        const ov = Overlay.overlay
        parent = ov
        const p = anchorItem.mapToItem(ov, anchorItem.width, 0)
        x = Math.max(8, Math.min(p.x + 6, ov.width - width - 8))
        y = Math.max(8, Math.min(p.y, ov.height - height - 8))
        open()
    }

    function groupState(g) {   // 0 none · 1 some · 2 all
        let have = 0
        for (const c of g.children)
            if (checks[c.id]) have++
        return have === 0 ? 0 : have === g.children.length ? 2 : 1
    }
    function toggleGroup(g) {
        const c = Object.assign({}, checks)
        const takeAll = groupState(g) !== 2
        for (const ch of g.children) {
            if (takeAll) c[ch.id] = true
            else delete c[ch.id]
        }
        checks = c
    }
    function toggleLeaf(id) {
        const c = Object.assign({}, checks)
        if (c[id]) delete c[id]
        else c[id] = true
        checks = c
    }

    width: 290
    height: Math.min(440, list.contentHeight + (backRow.visible ? 140 : 108))
    modal: true
    background: Rectangle {
        color: Qt.rgba(0.07, 0.07, 0.08, 0.96)
        border.width: 1
        border.color: Theme.glassBorder
        radius: Theme.radiusMd
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }

    contentItem: ColumnLayout {
        spacing: 6
        Text {
            text: "PICK FROM \"" + picker.heading.toUpperCase() + "\""
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 1.2
            color: Theme.textDim
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        // Back row while drilled into a season
        Rectangle {
            id: backRow
            visible: picker.openGroup !== null
            Layout.fillWidth: true
            height: visible ? 26 : 0
            radius: Theme.radiusSm
            color: backHover.containsMouse ? Theme.rowHoverPink : "transparent"
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 6
                text: "‹ " + (picker.openGroup ? picker.openGroup.label : "")
                font.pixelSize: 12
                color: Theme.pink
            }
            MouseArea {
                id: backHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: picker.openGroup = null
            }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1
            model: picker.openGroup !== null
                ? picker.openGroup.children : picker.items
            ScrollBar.vertical: ZuneScrollBar {}
            delegate: Rectangle {
                id: pickRow
                required property var modelData
                readonly property bool isGroup:
                    modelData.children !== undefined
                readonly property int gState:
                    isGroup ? picker.groupState(modelData)
                            : (picker.checks[modelData.id] === true ? 2 : 0)
                width: ListView.view.width
                height: 32
                radius: Theme.radiusSm
                color: pickHover.containsMouse
                    ? Theme.rowHoverPink : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 8
                    spacing: 8
                    // Tri-state checkbox — its own hit zone
                    Text {
                        text: pickRow.gState === 2 ? "✔"
                            : pickRow.gState === 1 ? "◐" : "○"
                        font.pixelSize: 14
                        color: pickRow.gState > 0 ? Theme.pink : Theme.textDim
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pickRow.isGroup
                                ? picker.toggleGroup(pickRow.modelData)
                                : picker.toggleLeaf(pickRow.modelData.id)
                        }
                    }
                    Text {
                        text: pickRow.modelData.label ?? ""
                        font.pixelSize: 13
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: pickRow.modelData.sublabel ?? ""
                        font.pixelSize: 11
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        Layout.maximumWidth: 90
                    }
                    Text {
                        visible: pickRow.isGroup
                        text: "▸"
                        font.pixelSize: 12
                        color: Theme.textDim
                    }
                }
                MouseArea {
                    id: pickHover
                    anchors.fill: parent
                    hoverEnabled: true
                    z: -1
                    onClicked: {
                        if (pickRow.isGroup)
                            picker.openGroup = pickRow.modelData
                        else
                            picker.toggleLeaf(pickRow.modelData.id)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: picker.checkedCount + " selected"
                font.pixelSize: 11
                color: Theme.textDim
                Layout.fillWidth: true
            }
            Rectangle {
                readonly property bool ready: picker.checkedCount > 0
                Layout.preferredWidth: goLabel.implicitWidth + 28
                Layout.preferredHeight: 30
                radius: 15
                color: ready
                    ? (goArea.containsMouse
                           ? Qt.lighter(Theme.pink, 1.15) : Theme.pink)
                    : Qt.rgba(1, 1, 1, 0.06)
                border.width: ready ? 0 : 1
                border.color: Theme.glassBorder
                scale: goArea.pressed && ready ? 0.94 : 1
                Behavior on scale { NumberAnimation { duration: 80 } }
                Behavior on color { ColorAnimation { duration: 100 } }
                Text {
                    id: goLabel
                    anchors.centerIn: parent
                    text: picker.actionWord + (picker.checkedCount > 0
                              ? " " + picker.checkedCount : "")
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: parent.ready ? "white" : Theme.textDim
                }
                MouseArea {
                    id: goArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: parent.ready
                        ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (!parent.ready)
                            return
                        picker.picked(Object.keys(picker.checks))
                        picker.close()
                    }
                }
            }
        }
    }
}
