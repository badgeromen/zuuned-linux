pragma Singleton
import QtQuick
import Zuuned

// UX-2 playlist-builder state — the one source of truth for "is the
// tray open". While `building`, every music ＋ in the app arms orange
// and feeds THIS tray instead of the sync queue (design contract:
// playlists are always created in the library first).
//
// var-property rule: tracks is always REASSIGNED with a fresh array —
// in-place mutation never fires the change signal (see QuickPicker).
QtObject {
    id: tray

    property bool building: false
    // -1 while composing a brand-new unsaved playlist
    property double playlistId: -1
    property string name: ""
    // [{id, title, artist, album, durationMs, filepath}] in order
    property var tracks: []
    // Only these observed occurrences may be removed by an editor save.
    // A pending slot can resolve while the tray is open without being lost.
    property var originalPlaylistEntryIds: []
    property bool dirty: false

    // Fired on every add and on disc-click-while-building — the tray
    // view answers with its glow pulse.
    signal pulsed()

    // Live drag census — Main.qml reveals the device panel while
    // anything is being dragged, so a drop target always exists.
    property int activeDragCount: 0
    readonly property bool dragging: activeDragCount > 0
    function dragStarted() { activeDragCount++ }
    function dragEnded() { activeDragCount = Math.max(0, activeDragCount - 1) }

    function openNew() {
        playlistId = -1
        name = ""
        tracks = []
        originalPlaylistEntryIds = []
        dirty = false
        building = true
    }

    function openExisting(id, plName) {
        playlistId = id
        name = plName
        tracks = LibraryService.playlistTracks(id)
        originalPlaylistEntryIds = tracks.map(t => t.playlistEntryId)
        dirty = false
        building = true
        refreshMetadata()
    }
    // Builder rows also contain newly added unsaved members. Resolve their
    // full library metadata by ID before editing or transferring so missing
    // genre/year/album-artist fields never erase existing tags.
    function trackFor(id) {
        const row = LibraryService.tracks.rowsSnapshot().find(t => t.libraryId === id)
        return row ? Object.assign({}, row, {id: row.libraryId}) : null
    }
    function refreshMetadata() {
        if (!building) return
        const byId = ({})
        for (const t of LibraryService.tracks.rowsSnapshot())
            byId[t.libraryId] = t
        tracks = tracks.map(t => byId[t.id]
            ? Object.assign({}, byId[t.id], {id: t.id, playlistEntryId: t.playlistEntryId}) : t)
    }

    // rows may be sync-queue shapes (libraryId) or tray shapes (id).
    // Dedups against what's already in. Returns how many landed.
    function addTracks(rows) {
        const have = {}
        for (const t of tracks)
            have[t.id] = true
        const out = tracks.slice()
        let added = 0
        for (const r of rows) {
            const tid = r.id !== undefined ? r.id : r.libraryId
            if (tid === undefined || tid < 0 || have[tid])
                continue
            have[tid] = true
            out.push(Object.assign({}, r, { id: tid, libraryId: tid, playlistEntryId: 0, title: r.title || "",
                       artist: r.artist || "", album: r.album || "",
                       durationMs: r.durationMs || 0,
                       filepath: r.filepath || "" }))
            added++
        }
        if (added > 0) {
            tracks = out
            dirty = true
        }
        pulsed()
        return added
    }

    function removeAt(i) {
        const out = tracks.slice()
        out.splice(i, 1)
        tracks = out
        dirty = true
    }

    function move(from, to) {
        if (from === to || from < 0 || to < 0
            || from >= tracks.length || to >= tracks.length)
            return
        const out = tracks.slice()
        out.splice(to, 0, out.splice(from, 1)[0])
        tracks = out
        dirty = true
    }

    function save() {
        const trimmed = name.trim()
        if (trimmed === "")
            return false
        const ids = tracks.map(t => t.id)
        if (playlistId < 0) {
            playlistId = LibraryService.createPlaylist(trimmed, ids)
            if (playlistId < 0)
                return false
        } else {
            if (!LibraryService.editPlaylistEntries(playlistId, trimmed, tracks, originalPlaylistEntryIds))
                return false
        }
        building = false
        dirty = false
        return true
    }

    function discard() {
        building = false
        dirty = false
        playlistId = -1
        name = ""
        tracks = []
        originalPlaylistEntryIds = []
    }
}
