pragma Singleton
import QtQuick
QtObject {
    property int revision: 0
    property bool busy: false
    property bool foregroundBusy: false
    property string status: ""
    property var preview: ({})
    property var records: []
    property var shelves: []
    property var calls: []
    signal operationFinished(string operation, string id, bool ok, string error)
    function record(method, args) { calls = calls.concat([{method:method,args:args}]) }
    function reset() { coverPaths = ({}); records = []; shelves = []; calls = []; busy = false; foregroundBusy = false; revision++ }
    function podcastDeviceIdentity(id) {
        const e = item(id), show = item(e.parentId || e.parent)
        return e.kind === "episode" && show.kind === "show" ? {feedUrl:show.identity || show.id,sourceUrl:e.deviceSourceUrl || e.sourceUrl || e.id} : {}
    }
    function item(id) { return records.concat(shelves).find(r => r.id === id) || {} }
    function collections(kind) { return records.filter(r => r.kind === kind) }
    function entries(id) { return records.filter(r => r.parentId === id) }
    function episodes() { return records.filter(r => r.kind === "episode") }
    property var coverPaths: ({})
    function cover(id) { return coverPaths[id] || "" }
    function audiobookCollections() { return shelves }
    function collectionBooks(id) { const s = shelves.find(r => r.id === id); return s ? s.bookIds.map(id => item(id)) : [] }
    function setFavorite(id, value) {
        record("setFavorite", [id,value])
        records = records.map(r => r.id === id ? Object.assign({},r,{favorite:value}) : r)
        revision++
    }
    function saveAudiobookCollection(id, title, ids, expectedRevision) { record("saveAudiobookCollection",[id,title,ids.slice(),expectedRevision]) }
    function deleteAudiobookCollection(id, expectedRevision) { record("deleteAudiobookCollection",[id,expectedRevision]) }
    function queueAudiobooks(ids) { record("queueAudiobooks",[ids]) }
    function queuePodcastSelection(id) { record("queuePodcastSelection", [id]) }
    function queuePodcasts(ids) { record("queuePodcasts",[ids]) }
    function play(id, offset) { record("play",[id,offset]) }
    function refreshFeed(id) { record("refreshFeed",[id]) }
    function download(ids) { record("download",[ids]) }
    function selectPodcastEpisodes(id, order, count, unplayed) {
        return entries(id).filter(e => !unplayed || !e.played).sort((a,b) => order === "oldest" ? a.id.localeCompare(b.id) : b.id.localeCompare(a.id)).slice(0,count)
    }
    function setPodcastSyncSelection(id, order, count, unplayed) { record("setPodcastSyncSelection", [id,order,count,unplayed]) }
    function setPodcastKeepOnDevice(id, keep) {
        record("setPodcastKeepOnDevice", [id, keep])
        records = records.map(r => r.id === id ? Object.assign({}, r, {keepOnDevice:keep}) : r)
        revision++
    }
    function markPlayed(id, played) { record("markPlayed",[id,played]) }
    function cancelWork() { record("cancelWork",[]) }
    function previewBook(files) {}
    function previewFeed(url) {}
    function saveCover(id, source, reset, revision) {}
}
