import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Zuuned

TestCase {
    id: testCase
    name: "DeviceSpoken"
    width: 1200; height: 850; visible: true
    when: windowShown
    Component { id: pageComponent; DeviceSpokenPage { width: 1100; height: 800 } }
    property var page: null
    function episode(id, show, title) {
        return {itemId:id,parentId:71,seriesId:show,title:title,author:"Author",seriesName:"Same show",durationMs:120000,sizeMB:5,artItemId:show}
    }
    function part(id, folder, number) {
        return {itemId:id,parentId:folder,partNumber:number,title:"Chapter " + number,author:"Author",bookTitle:"Same book",durationMs:120000,sizeMB:5,artItemId:id}
    }
    function init() {
        AppSettings.podcastAutoCleanup = false
        DeviceService.connected = false
        DeviceService.busy = false
        DeviceService.purging = false
        DeviceService.pulling = false
        DeviceService.spokenMediaLoading = false
        DeviceService.spokenMediaError = ""
        DeviceService.deviceSerial = "fixture-device-A"
        DeviceService.podcastEpisodesList = []
        DeviceService.audiobookPartsList = []
        DeviceService.removals = []
        DeviceService.refreshes = 0
        SyncEngine.isSyncing = false
        page = createTemporaryObject(pageComponent, testCase)
        verify(page !== null)
    }
    function cleanup() { if (page) page.visible = false; page = null }
    function test_cleanupIsVisibleAndUsesSharedPreference() {
        const toggle = findChild(page, "podcastPageCleanupToggle")
        verify(toggle.visible)
        compare(toggle.checked, false)
        mouseClick(toggle)
        compare(AppSettings.podcastAutoCleanup, true)
        SyncEngine.isSyncing = true
        compare(toggle.enabled, false)
        AppSettings.podcastAutoCleanup = false
        compare(toggle.checked, false)
        page.books = true
        verify(!toggle.visible)
    }
    function test_devicePlayedLabelRequiresConfirmedState() {
        let item = episode(268435457,251658241,"Completed episode")
        item.playedKnown = true
        item.played = true
        DeviceService.podcastEpisodesList = [item]
        DeviceService.connected = true
        page.openCollection(page.groups[0].key)
        const list = findChild(page, "deviceSpokenRows")
        tryCompare(list, "count", 1)
        tryVerify(function() { return findChild(list, "devicePodcastPlayed") !== null })
        verify(findChild(list, "devicePodcastPlayed").visible)
        item.playedKnown = false
        DeviceService.podcastEpisodesList = [item]
        tryVerify(function() { return !findChild(list, "devicePodcastPlayed").visible })
    }
    function test_separateShowsWithSameTitle() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First"),episode(268435458,251658242,"Second")]
        DeviceService.connected = true
        compare(page.groups.length,2)
        verify(String(page.groups[0].key) !== String(page.groups[1].key))
    }
    function test_separateBooksWithSameTitleAndChapterOrder() {
        page.books = true
        DeviceService.audiobookPartsList = [part(301989889,81,10),part(301989890,81,2),part(301989891,82,1)]
        DeviceService.connected = true
        compare(page.groups.length,2)
        var group = page.groups.find(g => g.rows.length === 2)
        verify(group !== undefined)
        compare(group.rows[0].partNumber,2)
        compare(group.rows[1].partNumber,10)
    }
    function test_deleteRequiresConfirmation() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First"),episode(268435458,251658241,"Second")]
        DeviceService.connected = true
        page.requestRemoval([268435457,268435458],"Same show")
        compare(DeviceService.removals.length,0)
        verify(findChild(page,"deviceSpokenDeleteDialog") !== null)
        page.confirmRemoval()
        compare(DeviceService.removals.length,1)
        compare(DeviceService.removals[0].kind,"podcast")
        compare(DeviceService.removals[0].ids.length,2)
    }
    function test_disconnectInvalidatesPendingRemoval() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First")]
        DeviceService.connected = true
        page.requestRemoval([268435457],"First")
        DeviceService.connected = false
        page.confirmRemoval()
        compare(DeviceService.removals.length,0)
    }
    function test_deviceSwapInvalidatesPendingRemoval() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First")]
        DeviceService.connected = true
        page.requestRemoval([268435457],"First")
        DeviceService.deviceSerial = "fixture-device-B"
        page.confirmRemoval()
        compare(DeviceService.removals.length,0)
    }
    function test_syncBlocksPendingRemoval() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First")]
        DeviceService.connected = true
        page.requestRemoval([268435457],"First")
        SyncEngine.isSyncing = true
        page.confirmRemoval()
        compare(DeviceService.removals.length,0)
    }
    function test_audiobookWholeGroupUsesBookKind() {
        page.books = true
        DeviceService.audiobookPartsList = [part(301989889,81,1),part(301989890,81,2)]
        DeviceService.connected = true
        page.requestRemoval([301989889,301989890],"Same book")
        page.confirmRemoval()
        compare(DeviceService.removals.length,1)
        compare(DeviceService.removals[0].kind,"audiobook")
        compare(DeviceService.removals[0].ids.length,2)
    }
    function test_noReadWhileOfflineOrHidden() {
        compare(DeviceService.refreshes,0)
        page.visible = false
        DeviceService.connected = true
        compare(DeviceService.refreshes,0)
        page.visible = true
        tryVerify(function() { return DeviceService.refreshes > 0 })
    }
    function test_selectAllAndConfirmWithMouse() {
        page.books = true
        DeviceService.audiobookPartsList = [part(301989889,81,1),part(301989890,81,2)]
        DeviceService.connected = true
        page.openCollection(page.groups[0].key)
        var select = findChild(page,"deviceSpokenSelectAll")
        verify(select !== null)
        waitForRendering(page)
        mouseClick(select, select.width/2, select.height/2)
        compare(page.selectedIds.length,2)
        var remove = findChild(page,"deviceSpokenRemoveSelected")
        mouseClick(remove, remove.width/2, remove.height/2)
        compare(DeviceService.removals.length,0)
        var confirm = findChild(page,"deviceSpokenConfirmRemoval")
        tryCompare(findChild(page,"deviceSpokenDeleteDialog"),"opened",true)
        mouseClick(confirm, confirm.width/2, confirm.height/2)
        compare(DeviceService.removals.length,1)
        compare(DeviceService.removals[0].ids.length,2)
    }
    function test_inventoryChangePrunesSelectionAndPreservesOpenGroup() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First"),episode(268435458,251658241,"Second")]
        DeviceService.connected = true
        page.openCollection(page.groups[0].key)
        page.selectAll()
        compare(page.selectedIds.length,2)
        DeviceService.podcastEpisodesList = [episode(268435458,251658241,"Second")]
        compare(page.selectedIds.length,1)
        compare(page.openGroup.rows.length,1)
    }
    function test_errorDisablesRemovalButKeepsInventory() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First")]
        DeviceService.connected = true
        DeviceService.spokenMediaError = "Inventory failed"
        compare(page.groups.length,1)
        compare(page.canManage,false)
        page.requestRemoval([268435457],"First")
        page.confirmRemoval()
        compare(DeviceService.removals.length,0)
    }
    function test_missingItemInvalidatesPendingRemoval() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First")]
        DeviceService.connected = true
        page.requestRemoval([268435457],"First")
        DeviceService.podcastEpisodesList = []
        page.confirmRemoval()
        compare(DeviceService.removals.length,0)
    }

    function test_openCollectionRerequestsInvalidatedCover() {
        page.books = true
        DeviceService.audiobookPartsList = [part(301989889,81,1)]
        DeviceService.connected = true
        page.openCollection(page.groups[0].key)
        waitForRendering(page)
        var before = DeviceService.artRequests
        DeviceService.artChanged()
        verify(DeviceService.artRequests > before)
    }

    function test_recycledIdCannotReuseOldConfirmation() {
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"First")]
        DeviceService.connected = true
        page.requestRemoval([268435457],"First")
        DeviceService.podcastEpisodesList = [episode(268435457,251658241,"Replacement")]
        page.confirmRemoval()
        compare(DeviceService.removals.length,0)
    }

}
