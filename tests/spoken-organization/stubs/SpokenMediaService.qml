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
    function listRow(e) {
        const show = item(e.parentId || e.parent)
        return Object.assign({}, e, {feedUrl: show.identity || show.id, deviceSource: e.deviceSourceUrl || e.sourceUrl || e.id})
    }
    function selectPodcastEpisodes(id, order, count, unplayed, playedSources) {
        const played = playedSources || []
        return entries(id).map(listRow).filter(e => !unplayed || (!e.played && !played.includes(e.deviceSource)))
            .sort((a,b) => order === "oldest" ? a.id.localeCompare(b.id) : b.id.localeCompare(a.id)).slice(0,count)
    }
    // Mirrors the native list contract over the records: record order is
    // newest first; oldest reverses it.
    function listFor(id, o) {
        const played = o.playedSources || [], needle = (o.search || "").toLowerCase()
        const rows = (id ? entries(id) : episodes()).map(listRow).filter(e =>
            (id || o.starred) && (!o.starred || e.favorite)
            && (o.filter === "downloaded" ? !!e.path : o.filter === "starred" ? !!e.favorite
                : o.filter === "unplayed" ? !e.played && !played.includes(e.deviceSource) : true)
            && (!needle || e.title.toLowerCase().includes(needle)))
        return o.oldest ? rows.reverse() : rows
    }
    function episodePage(id, options) {
        const o = options || {}, rows = listFor(id, o)
        if (o.limit !== undefined) {
            const offset = Math.max(0, o.offset || 0)
            return {rows: rows.slice(offset, offset + o.limit), total: rows.length, page: 0, pages: 1, pageSize: o.limit}
        }
        const size = o.pageSize || 100
        const pages = Math.max(1, Math.ceil(rows.length / size))
        const index = Math.min(Math.max(0, o.page || 0), pages - 1)
        return {rows: rows.slice(index * size, (index + 1) * size), total: rows.length, page: index, pages: pages, pageSize: size}
    }
    function episodeIds(id, options, limit) { return listFor(id, options || {}).slice(0, limit).map(e => e.id) }
    function showSummary(id) {
        const all = entries(id)
        return {count: all.length, downloaded: all.filter(e => !!e.path).length, olderState: "complete", olderError: "", readingOlder: false}
    }
    function resumeEpisode(id) {
        const downloaded = entries(id).filter(e => !!e.path)
        const next = downloaded.find(e => !e.played) || downloaded[0]
        return next ? next.id : ""
    }
    function showOnDevice(id, sources) { return entries(id).map(listRow).some(e => (sources || []).includes(e.deviceSource)) }
    property bool loadingOlder: false
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
