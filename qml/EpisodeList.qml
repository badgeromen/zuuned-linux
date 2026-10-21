import QtQuick
import Zuuned

// A show's episodes, or every show's starred ones, for a ListView: read from
// the library a batch at a time as the view scrolls (loadMore), rows changed
// in place (KeyedRows).
KeyedRows {
    id: list
    property string show: ""
    property bool starred: false       // every show's starred episodes
    property string filter: "all"
    property string search: ""
    property bool newestFirst: true
    property var playedSources: []     // audio URLs the Zune reports played
    property int batch: 25
    property int total: 0
    readonly property int revision: SpokenMediaService.revision
    property int wanted: batch
    // A new question (show, filter, search, order): views return to the top.
    signal reloaded()
    function options(extra) {
        return Object.assign({filter: filter, search: search, starred: starred, oldest: !newestFirst,
                              playedSources: playedSources}, extra || {})
    }
    // Worked out when asked: a change handler can run before a binding on
    // the same property has caught up.
    function active() { return show !== "" || starred }
    // A different question starts again at the top.
    function reload() { wanted = batch; sync(true); reloaded() }
    function loadMore() {
        if (!active() || count >= total || wanted > count) return
        wanted = count + batch
        sync(false)
    }
    // "select all": the IDs of the whole list (at most limit).
    function ids(limit) { return active() ? SpokenMediaService.episodeIds(show, options(), limit) : [] }
    function sync(fresh) {
        if (!active()) { clear(); total = 0; return }
        const page = SpokenMediaService.episodePage(show, options({offset: 0, limit: Math.max(wanted, batch)}))
        total = page.total
        apply(page.rows, fresh)
    }
    onShowChanged: reload()
    onStarredChanged: reload()
    onFilterChanged: reload()
    onSearchChanged: reload()
    onNewestFirstChanged: reload()
    onPlayedSourcesChanged: { if (filter === "unplayed") sync(false) }
    onRevisionChanged: sync(false)
    Component.onCompleted: reload()
}
