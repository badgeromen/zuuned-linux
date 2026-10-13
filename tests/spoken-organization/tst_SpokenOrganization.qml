import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Zuuned

TestCase {
    id: testCase
    name: "SpokenOrganization"
    width: 1200; height: 950; visible: true
    when: windowShown
    Component { id: pageComponent; SpokenMediaPage { width: 1100; height: 900; Rectangle { anchors.fill: parent; color: Theme.bg; z: -100 } } }
    Component { id: summaryComponent; SyncCompleteSheet {} }
    Component { id: cardComponent; SpokenCard { entry: SpokenMediaService.item("book-a"); book: true; transferReady: true } }
    property var page: null
    function init() {
        failOnWarning(/.*(Binding loop|TypeError|ReferenceError|Unable to assign).*/)
        BookTrayState.reset()
        DeviceService.connected = false
        SyncEngine.isSyncing = false
        AppSettings.podcastAutoQueue = false
        AppSettings.podcastAutoQueueCount = 10
        DeviceService.podcastStatuses = ({})
        DeviceService.podcastEpisodesList = []
        SpokenMediaService.reset()
        SpokenMediaService.records = [
            {id:"book-a",kind:"book",title:"Alpha book",author:"Author",favorite:false,count:1},
            {id:"book-b",kind:"book",title:"Beta book",author:"Author",favorite:true,count:1},
            {id:"show-a",kind:"show",title:"Alpha show",author:"Host",favorite:false,count:2},
            {id:"show-b",kind:"show",title:"Beta show",author:"Host",favorite:true,count:1},
            {id:"episode-a",kind:"episode",parentId:"show-a",title:"First episode",favorite:true,path:"/fixture/a.mp3",durationMs:1000},
            {id:"episode-b",kind:"episode",parentId:"show-a",title:"Second episode",favorite:false,path:"",durationMs:2000},
            {id:"episode-c",kind:"episode",parentId:"show-b",title:"Third episode",favorite:false,path:"/fixture/c.mp3",durationMs:3000},
            {id:"part-a",kind:"part",parentId:"book-a",title:"Book audio",path:"/fixture/book.m4b",durationMs:4000,chapters:[{title:"Opening",startMs:0},{title:"End",startMs:2000}]}]
        SpokenMediaService.shelves = [{id:"shelf-a",kind:"book-collection",title:"A series",bookIds:["book-b","book-a"],revision:4,count:2,cover:""}]
        SpokenMediaService.revision++
        page = createTemporaryObject(pageComponent, testCase)
        verify(page !== null)
    }
    function cleanup() { if (page) page.visible = false; page = null }
    function pivotTab(parent, title) {
        if (parent.modelData === title && parent.active !== undefined) return parent
        for (const child of parent.children || []) {
            const result = pivotTab(child,title)
            if (result) return result
        }
        return null
    }
    function test_switchMediaAfterActualPivotClickKeepsValidActiveTab() {
        const pivots = findChild(page,"spokenTopPivots")
        compare(pivots.current,"Shows")
        let tab = pivotTab(pivots,"Starred episodes")
        verify(tab !== null)
        waitForRendering(page)
        mouseClick(tab,tab.width/2,tab.height/2)
        compare(page.pivot,"Starred episodes")
        compare(pivots.current,"Starred episodes")
        page.books = true
        compare(page.pivot,"Books")
        compare(pivots.current,"Books")
        verify(pivots.tabs.includes(pivots.current))
        tab = pivotTab(pivots,"Libraries")
        verify(tab !== null)
        waitForRendering(page)
        mouseClick(tab,tab.width/2,tab.height/2)
        compare(page.pivot,"Libraries")
        page.shelfId = "shelf-a"
        page.openCollection("book-a")
        page.checks = {"part-a":true}
        page.books = false
        compare(page.pivot,"Shows")
        compare(pivots.current,"Shows")
        verify(pivots.tabs.includes(pivots.current))
        compare(page.collectionId,"")
        compare(page.shelfId,"")
        compare(Object.keys(page.checks).length,0)
    }
    function test_podcastCountOrderManualAndConnection() {
        const picker = findChild(page, "spokenTransferPicker")
        verify(picker !== null)
        picker.openAt(page, SpokenMediaService.item("show-a"))
        compare(picker.mode, "newest")
        compare(picker.selectedIds.length, 2)
        picker.episodeCount = 1
        compare(picker.selectedIds, ["episode-b"])
        picker.mode = "oldest"
        compare(picker.selectedIds, ["episode-a"])
        picker.mode = "manual"
        compare(picker.selectedIds.length, 0)
        picker.toggle("episode-b")
        compare(picker.selectedIds, ["episode-b"])
        SpokenMediaService.revision++
        compare(picker.selectedIds, ["episode-b"])
        picker.addSelection()
        compare(SpokenMediaService.calls.length, 0)
        DeviceService.connected = true
        SyncEngine.isSyncing = true
        picker.addSelection()
        compare(SpokenMediaService.calls.length, 0)
        SyncEngine.isSyncing = false
        picker.addSelection()
        compare(SpokenMediaService.calls[0].method, "queuePodcasts")
        compare(SpokenMediaService.calls[0].args[0], ["episode-b"])
        picker.close()
    }
    function test_podcastPlusUsesNativeSelectionFromCardsAndDetail() {
        DeviceService.connected = true
        let card = null
        tryVerify(() => { card = findChild(page, "spokenCollectionCard_show-a"); return card !== null })
        card.addAll()
        compare(SpokenMediaService.calls[0].method, "queuePodcastSelection")
        compare(SpokenMediaService.calls[0].args, ["show-a"])
        page.openCollection("show-a")
        page.filter = "downloaded"
        const add = findChild(page, "spokenDetailAdd")
        verify(add !== null)
        verify(add.enabled)
        add.addAll()
        compare(SpokenMediaService.calls[1].method, "queuePodcastSelection")
        compare(SpokenMediaService.calls[1].args, ["show-a"])
        DeviceService.connected = false
        verify(!add.enabled)
    }
    function test_podcastPickerUsesGlobalCountAndSavedShowOverride() {
        AppSettings.podcastAutoQueueCount = 5
        const picker = findChild(page, "spokenTransferPicker")
        picker.openAt(page, SpokenMediaService.item("show-a"))
        compare(picker.episodeCount, 5)
        picker.close()
        SpokenMediaService.records = SpokenMediaService.records.map(row => row.id === "show-a" ? Object.assign({}, row, {syncCount: 3, syncOrder: "oldest"}) : row)
        SpokenMediaService.revision++
        AppSettings.podcastAutoQueueCount = 17
        picker.openAt(page, SpokenMediaService.item("show-a"))
        compare(picker.episodeCount, 3)
        compare(picker.mode, "oldest")
        picker.close()
        picker.openAt(page, SpokenMediaService.item("show-b"))
        compare(picker.episodeCount, 17)
        picker.close()
    }
    function test_podcastPresetRemembersOnlyWhenQueued() {
        const picker = findChild(page, "spokenTransferPicker")
        picker.openAt(page, SpokenMediaService.item("show-a"))
        picker.mode = "oldest"; picker.episodeCount = 1
        picker.close()
        compare(SpokenMediaService.calls.length, 0)
        picker.openAt(page, SpokenMediaService.item("show-a"))
        picker.mode = "oldest"; picker.episodeCount = 1
        DeviceService.connected = true
        picker.addSelection()
        compare(SpokenMediaService.calls[0].method, "setPodcastSyncSelection")
        compare(SpokenMediaService.calls[0].args, ["show-a", "oldest", 1, true])
        compare(SpokenMediaService.calls[1].method, "queuePodcasts")
        picker.close()
    }
    function test_keepOnZuneProtectedDuringSync() {
        page.contextFor(SpokenMediaService.item("episode-a"))
        const keep = findChild(page, "podcastKeepOnDevice")
        verify(keep !== null)
        verify(keep.visible)
        verify(keep.enabled)
        SyncEngine.isSyncing = true
        verify(!keep.enabled)
        SyncEngine.isSyncing = false
        keep.triggered()
        compare(SpokenMediaService.calls[0].method, "setPodcastKeepOnDevice")
        compare(SpokenMediaService.calls[0].args, ["episode-a", true])
        keyClick(Qt.Key_Escape)
    }
    function test_cleanupSummaryCountsAndWarnings() {
        const sheet = createTemporaryObject(summaryComponent, testCase)
        verify(sheet !== null)
        sheet.openWith(1000,0,0,0,0,0,0,0,2,"One episode could not be removed")
        compare(sheet.podcastsRemoved,2)
        compare(findChild(sheet,"syncPodcastsRemoved").text,"2 played podcasts removed")
        compare(findChild(sheet,"syncCleanupWarning").text,"One episode could not be removed")
        sheet.close()
    }
    function test_episodeNumberComesFromFeedAndSurvivesFiltering() {
        const records = SpokenMediaService.records.slice()
        records[4] = Object.assign({}, records[4], {episodeNumber:190})
        SpokenMediaService.records = records
        SpokenMediaService.revision++
        page.openCollection("show-a")
        const list = findChild(page,"spokenEpisodeList")
        list.forceLayout()
        tryVerify(() => list.itemAtIndex(1) !== null)
        compare(findChild(list.itemAtIndex(0),"spokenEpisodeNumber").text,"190")
        compare(findChild(list.itemAtIndex(1),"spokenEpisodeNumber").text,"")
        page.filter = "downloaded"
        list.forceLayout()
        tryCompare(list,"count",1)
        compare(findChild(list.itemAtIndex(0),"spokenEpisodeNumber").text,"190")
        page.filter = "all"
        page.openCollection("")
        page.pivot = "Starred episodes"
        list.forceLayout()
        tryCompare(list,"count",1)
        compare(findChild(list.itemAtIndex(0),"spokenEpisodeNumber").text,"190")
    }
    function test_podcastDeviceBadgeAndPlayedStayVisible() {
        DeviceService.connected = true
        DeviceService.podcastStatuses = {"show-a|episode-a":{known:true,onDevice:true,playedKnown:true,played:true}}
        DeviceService.podcastEpisodesList = [{itemId:123}]
        page.openCollection("show-a")
        compare(page.rows.length,2)
        const list = findChild(page,"spokenEpisodeList")
        list.forceLayout()
        tryVerify(() => list.itemAtIndex(0) !== null)
        const row = list.itemAtIndex(0)
        verify(findChild(row,"spokenEpisodeOnDevice").visible)
        compare(findChild(row,"spokenEpisodePlayed").text,"✓ played on Zune")
        page.filter = "unplayed"
        compare(page.rows.length,1)
        page.filter = "all"
        compare(page.rows.length,2)
        DeviceService.connected = false
        compare(page.rows.length,2)
        verify(!findChild(list.itemAtIndex(0),"spokenEpisodeOnDevice").visible)
    }
    function test_podcastNewestTenAndSavedCountWithDeviceBadges() {
        const picker = findChild(page,"spokenTransferPicker")
        DeviceService.connected = true
        DeviceService.podcastStatuses = {"show-a|episode-a":{known:true,onDevice:true,playedKnown:true,played:false}}
        DeviceService.podcastEpisodesList = [{itemId:123}]
        picker.openAt(page,SpokenMediaService.item("show-a"))
        compare(picker.episodeCount,10)
        compare(picker.selectedIds,["episode-b"])
        picker.mode = "manual"
        picker.toggle("episode-a")
        compare(picker.selectedIds.length,0)
        picker.toggle("episode-b")
        compare(picker.selectedIds,["episode-b"])
        picker.close()
        SpokenMediaService.records = SpokenMediaService.records.map(e => e.id === "show-a" ? Object.assign({},e,{syncCount:7,syncOrder:"oldest",syncUnplayedOnly:false}) : e)
        SpokenMediaService.revision++
        picker.openAt(page,SpokenMediaService.item("show-a"))
        compare(picker.episodeCount,7)
        compare(picker.mode,"oldest")
        verify(!picker.unplayedOnly)
        picker.close()
    }
    function test_podcastUnknownStateDoesNotInventBadgeOrPlayed() {
        DeviceService.connected = true
        DeviceService.podcastStatuses = {"show-a|episode-a":{known:false,onDevice:false,playedKnown:false,played:false}}
        DeviceService.podcastEpisodesList = []
        page.openCollection("show-a")
        page.filter = "unplayed"
        compare(page.rows.length,2)
        const list = findChild(page,"spokenEpisodeList")
        list.forceLayout()
        tryVerify(() => list.itemAtIndex(0) !== null)
        verify(!findChild(list.itemAtIndex(0),"spokenEpisodeOnDevice").visible)
        verify(!findChild(list.itemAtIndex(0),"spokenEpisodePlayed").visible)
    }
    function test_initialShows() {
        compare(page.containers.length,2)
        waitForRendering(page)
        grabImage(page).save("/tmp/zuuned-spoken-shows-full.png")
    }
    function test_starredShowFilterAndLiveUnstar() {
        page.pivot = "Starred shows"
        compare(page.containers.map(r => r.id),["show-b"])
        page.openCollection("show-b")
        const star = findChild(page,"spokenDetailStar")
        waitForRendering(page)
        mouseClick(star,star.width/2,star.height/2)
        compare(SpokenMediaService.calls.length,1)
        compare(SpokenMediaService.calls[0].method,"setFavorite")
        compare(SpokenMediaService.calls[0].args,["show-b",false])
        compare(page.containers.length,0)
    }
    function test_starredBookFilterAndOfflineDetailStar() {
        page.books = true
        page.pivot = "Starred"
        compare(page.containers.map(r => r.id),["book-b"])
        page.openCollection("book-a")
        const star = findChild(page,"spokenDetailStar")
        verify(star.enabled)
        waitForRendering(page)
        mouseClick(star,star.width/2,star.height/2)
        compare(SpokenMediaService.calls[0].args,["book-a",true])
        compare(page.containers.length,2)
    }
    function test_episodeFavoritesAreIndependentFromShow() {
        page.openCollection("show-a")
        page.filter = "starred"
        compare(page.rows.map(r => r.id),["episode-a"])
        compare(SpokenMediaService.item("show-a").favorite,false)
        const star = findChild(page,"spokenEpisodeStar")
        verify(star !== null)
        waitForRendering(page)
        mouseClick(star,star.width/2,star.height/2)
        compare(SpokenMediaService.calls[0].args,["episode-a",false])
        compare(page.rows.length,0)
        compare(SpokenMediaService.item("show-a").favorite,false)
    }
    function test_sublibraryMaintainsExplicitBookOrderAndBreadcrumbReturn() {
        page.books = true
        page.pivot = "Libraries"
        compare(page.galleryRows.length,1)
        page.shelfId = "shelf-a"
        compare(page.containers.map(r => r.id),["book-b","book-a"])
        page.openCollection("book-b")
        page.back()
        compare(page.collectionId,"")
        compare(page.shelfId,"shelf-a")
        page.back()
        compare(page.shelfId,"")
        compare(SpokenMediaService.calls.length,0)
    }
    function test_globalStarredEpisodesAndNoImplicitQueue() {
        page.pivot = "Starred episodes"
        compare(page.collectionId,"")
        compare(page.showingEntries,true)
        compare(page.listRows.map(r => r.id),["episode-a"])
        compare(SpokenMediaService.calls.length,0)
    }
    function test_narrowHeaderAndGalleryStayInsidePage_data() {
        return [{tag:"books-500",books:true,width:500},{tag:"books-600",books:true,width:600},
                {tag:"shows-500",books:false,width:500},{tag:"shows-600",books:false,width:600}]
    }
    function test_drillDownKeepsTitleActionsAndRowsInBounds_data() {
        return [{tag:"podcast-wide",books:false,width:1100},{tag:"podcast-narrow",books:false,width:500},
                {tag:"book-wide",books:true,width:1100},{tag:"book-narrow",books:true,width:500}]
    }
    function test_drillDownKeepsTitleActionsAndRowsInBounds(data) {
        page.width = data.width
        page.books = data.books
        const id = data.books ? "book-a" : "show-a"
        SpokenMediaService.records = SpokenMediaService.records.map(r => r.id === id
            ? Object.assign({},r,{title:"A long title for a journey through distant worlds and strange new places"}) : r)
        page.openCollection(id)
        waitForRendering(page)
        const title = findChild(page,"spokenDetailTitle")
        const actions = findChild(page,"spokenDetailActions")
        const list = findChild(page,"spokenEpisodeList")
        verify(title !== null && actions !== null && list !== null)
        verify(title.wrap)
        for (const control of [title,actions,list]) {
            const p = control.mapToItem(page,0,0)
            verify(p.x >= 0 && p.x + control.width <= page.width + 1,"Detail content fits page")
        }
        verify(list.height >= Theme.spokenDetailRowHeight * 2,"Detail leaves usable space for its list")
        verify(actions.mapToItem(page,0,actions.height).y <= list.mapToItem(page,0,0).y,"Actions do not overlap list")
        grabImage(page).save("/tmp/zuuned-drill-" + data.tag + ".png")
    }
    function test_narrowHeaderAndGalleryStayInsidePage(data) {
        page.width = data.width
        page.books = data.books
        waitForRendering(page)
        grabImage(page).save("/tmp/zuuned-spoken-" + data.tag + ".png")
        const header = findChild(page,"spokenHeaderBand")
        const pivots = findChild(page,"spokenTopPivots")
        const search = findChild(page,"spokenSearch")
        const add = findChild(page,data.books ? "spokenAddBook" : "spokenAddFeed")
        verify(header !== null && pivots !== null && search !== null && add !== null)
        for (const control of [pivots,add,search]) {
            const p = control.mapToItem(page,0,0)
            verify(p.x >= 0,"Control starts inside page")
            verify(p.x + control.width <= page.width + 1,"Control ends inside page: " + control.objectName + " at " + (p.x + control.width) + " > " + page.width)
        }
        const searchStart = search.mapToItem(page,0,0).x
        const addEnd = add.mapToItem(page,add.width,0).x
        verify(addEnd <= searchStart,"Add and search do not overlap")
        const grid = findChild(page,"spokenGallery")
        verify(grid.contentX + grid.plan.width <= page.width,"Gallery cards fit available width")
        compare(grid.anchorX,Gallery.spine(page.width))
    }
    function test_cardPlayAffordancePlaysWithoutOpeningOrQueueing() {
        const card = createTemporaryObject(cardComponent,testCase,{playable:true})
        verify(card !== null)
        let played = 0, opened = 0
        card.played.connect(function() { played++ })
        card.selected.connect(function() { opened++ })
        const x = card.cardSize/2, y = card.cardSize*Theme.spokenBookRatio/2
        mouseMove(card,x,y)
        waitForRendering(card)
        mouseClick(card,x,y)
        compare(played,1)
        compare(opened,0)
        compare(SpokenMediaService.calls.length,0)
    }
    function test_bookCardPreservesWholeBookDragAndOfflineGate() {
        const card = createTemporaryObject(cardComponent,testCase)
        verify(card !== null)
        compare(card.dragArmed,false)
        compare(findChild(card,"spokenSplitAdd").enabled,false)
        compare(findChild(card,"spokenSplitAdd").showPick,false)
        const ghost = findChild(card,"spokenCardGhost")
        compare(ghost.dragKind,"audiobook-books")
        compare(ghost.dragTracks(),["book-a"])
        DeviceService.connected = true
        compare(card.dragArmed,true)
        compare(findChild(card,"spokenSplitAdd").enabled,true)
    }
    function test_podcastCardKeepsEpisodePicker() {
        const card = createTemporaryObject(cardComponent,testCase,{book:false,entry:SpokenMediaService.item("show-a")})
        verify(card !== null)
        compare(findChild(card,"spokenSplitAdd").showPick,true)
        compare(findChild(card,"spokenCardGhost").dragKind,"podcast-shows")
        compare(findChild(card,"spokenCardGhost").dragTracks(),["show-a"])
        let picks = 0
        card.pickParts.connect(function() { picks++ })
        findChild(card,"spokenCardGhost").openTransferPicker()
        compare(picks,1)
        compare(SpokenMediaService.calls.length,0)
    }
    function test_starIsAvailableOfflineWithoutDeviceAction() {
        const card = createTemporaryObject(cardComponent,testCase)
        verify(card !== null)
        const star = findChild(card,"spokenCardStar")
        verify(star !== null)
        verify(star.enabled)
        let stars = 0
        card.favoriteRequested.connect(function() { stars++ })
        mouseMove(card,card.width/2,card.height/4)
        tryVerify(function() { return star.opacity > 0 })
        mouseClick(star,star.width/2,star.height/2)
        compare(stars,1)
        compare(SpokenMediaService.calls.length,0)
    }
}
