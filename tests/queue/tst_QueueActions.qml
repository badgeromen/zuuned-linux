import QtQuick
import QtQuick.Layouts
import QtTest
import Zuuned

Item {
    id: host
    width: 360
    height: 500
    property var removedEntries: []
    property var removedGroups: []
    property int toggles: 0

    Component {
        id: rowFactory
        QueueEntryView {
            width: 188 // the queue inside a 220px panel with 16px side insets
            entryId: "episode-1"
            type: "video"
            title: "A Long Adventure Time Episode Name"
            artist: ""
            albumartist: ""
            album: ""
            filepath: ""
            status: "pending"
            statusNote: ""
            progress: 0
            posterPath: ""
            series: "Adventure Time"
            groupKey: "season-10"
            groupInfo: ({kind:"series", title:"Adventure Time", subtitle:"Season 10",
                         count:12, doneCount:0, failedCount:0, firstEntryId:"episode-1"})
            onToggleRequested: key => { host.toggles++; expanded = !expanded }
            onRemoveEntryRequested: id => host.removedEntries = host.removedEntries.concat([id])
            onRemoveGroupRequested: key => host.removedGroups = host.removedGroups.concat([key])
        }
    }
    Component {
        id: modelFactory
        ColumnLayout {
            width: 188
            Repeater {
                model: ListModel {
                    ListElement {
                        entryId: "real-role-entry"
                        type: "track"
                        title: "Song"
                        artist: "Singer"
                        albumartist: "Singer"
                        album: "Album"
                        filepath: ""
                        status: "pending"
                        statusNote: ""
                        progress: 0
                        posterPath: ""
                        series: ""
                        groupKey: "album-1"
                    }
                }
                delegate: QueueEntryView { objectName: "modelDelegate" }
            }
        }
    }

    TestCase {
        name: "QueueActions"
        when: windowShown
        function init() {
            host.removedEntries = []
            host.removedGroups = []
            host.toggles = 0
            mouseMove(host, 330, 450)
        }
        function create(properties) {
            const entry = createTemporaryObject(rowFactory, host, properties || {})
            verify(entry)
            wait(30)
            return entry
        }
        function test_collapsedGroupHasPersistentWideTargetAndStableLabels() {
            const entry = create()
            const button = findChild(entry, "queueRemoveGroup")
            const title = findChild(entry, "queueGroupTitle")
            const detail = findChild(entry, "queueGroupDetail")
            verify(button.visible)
            compare(button.width, 32)
            compare(button.height, 32)
            const titleWidth = title.width
            const titleX = title.x
            compare(detail.text, "Season 10 · 12 episodes")
            verify(detail.width > 65, "Season remains readable in a 220px panel")
            mouseMove(button, 2, 2)
            compare(title.width, titleWidth)
            compare(title.x, titleX)
            mouseClick(button, 2, 2)
            compare(host.removedGroups.length, 1)
            compare(host.removedGroups[0], "season-10")
            compare(host.removedEntries.length, 0)
            compare(host.toggles, 0)
            mouseMove(host, 330, 450)
            compare(button.visible, true)
            compare(title.width, titleWidth)
        }
        function test_expandedGroupRemovesIndividualAndGroupIndependently() {
            const entry = create()
            const header = findChild(entry, "queueGroupArea")
            mouseClick(header, 55, header.height / 2)
            compare(entry.expanded, true)
            compare(host.toggles, 1)
            wait(30)
            const button = findChild(entry, "queueRemoveEntry")
            verify(button)
            compare(button.width, 32)
            mouseMove(button, 30, 30)
            mouseClick(button, 30, 30)
            compare(host.removedEntries.length, 1)
            compare(host.removedEntries[0], "episode-1")
            compare(host.removedGroups.length, 0)
            const group = findChild(entry, "queueRemoveGroup")
            mouseClick(group, 16, 16)
            compare(host.removedGroups.length, 1)
            compare(host.toggles, 1)
        }
        function test_singleEntryHasPersistentTargetWithoutHeader() {
            const entry = create({groupInfo:null})
            compare(findChild(entry, "queueGroupHeader"), null)
            const button = findChild(entry, "queueRemoveEntry")
            verify(button.visible)
            const title = findChild(entry, "queueEntryTitle")
            const titleWidth = title.width
            mouseMove(button, 2, 2)
            mouseClick(button, 2, 2)
            compare(host.removedEntries.length, 1)
            compare(title.width, titleWidth)
        }
        function test_rightClickGroupDoesNotToggleAndRemovesWholeGroup() {
            const entry = create()
            const header = findChild(entry, "queueGroupArea")
            mouseClick(header, 55, header.height / 2, Qt.RightButton)
            const menu = findChild(entry, "queueRemoveMenu")
            tryCompare(menu, "opened", true)
            compare(host.toggles, 0)
            const action = findChild(menu, "queueMenuRemoveAction")
            compare(action.text, "Remove season from queue")
            mouseClick(action, action.width / 2, action.height / 2)
            compare(host.removedGroups.length, 1)
            compare(host.removedEntries.length, 0)
        }
        function test_rightClickEntryRemovesOnlyEntry() {
            const entry = create({expanded:true})
            const row = findChild(entry, "queueEntryArea")
            mouseClick(row, 55, row.height / 2, Qt.RightButton)
            const menu = findChild(entry, "queueRemoveMenu")
            tryCompare(menu, "opened", true)
            const action = findChild(menu, "queueMenuRemoveAction")
            compare(action.text, "Remove from queue")
            mouseClick(action, action.width / 2, action.height / 2)
            compare(host.removedEntries.length, 1)
            compare(host.removedGroups.length, 0)
        }
        function test_syncDisablesPersistentButtonsAndMenuAtActionTime() {
            const entry = create({expanded:true})
            const header = findChild(entry, "queueGroupArea")
            mouseClick(header, 55, header.height / 2, Qt.RightButton)
            const menu = findChild(entry, "queueRemoveMenu")
            tryCompare(menu, "opened", true)
            const action = findChild(menu, "queueMenuRemoveAction")
            entry.syncLocked = true
            compare(action.enabled, false)
            action.triggered() // queued action arriving after sync starts
            menu.close()
            tryCompare(menu, "opened", false)
            for (const name of ["queueRemoveGroup", "queueRemoveEntry"]) {
                const button = findChild(entry, name)
                verify(button.visible)
                compare(button.width, 32)
                compare(button.enabled, false)
                mouseClick(button, 16, 16)
                button.clicked() // guard also survives a delayed signal
            }
            compare(host.removedEntries.length, 0)
            compare(host.removedGroups.length, 0)
            compare(host.toggles, 0)
        }
        function test_inFlightMemberRemainsVisibleInCollapsedGroup() {
            const entry = create({entryId:"episode-2", status:"syncing", progress:0.5, syncLocked:true})
            compare(entry.headerShown, false)
            compare(entry.rowShown, true)
            const button = findChild(entry, "queueRemoveEntry")
            verify(button.visible)
            compare(button.enabled, false)
        }
        function test_nativeModelRolesPopulateExtractedDelegate() {
            const model = createTemporaryObject(modelFactory, host)
            verify(model)
            wait(30)
            const entry = findChild(model, "modelDelegate")
            verify(entry)
            compare(entry.entryId, "real-role-entry")
            compare(entry.title, "Song")
            verify(findChild(entry, "queueRemoveEntry"))
        }
    }
}
