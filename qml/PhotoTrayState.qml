pragma Singleton
import QtQuick
import Zuuned

// A local, staged organizer. Only Save writes to the library; this draft is
// deliberately independent of the music builder and the device queue.
QtObject {
    id: tray
    property bool building: false
    property int generation: 0
    property double albumId: -1
    property string name: ""
    property double parentId: 0
    property var photos: []
    property var originalSnapshot: ({})
    property string error: ""
    property var pendingRequest: null
    readonly property bool discardConfirmation: pendingRequest !== null
    readonly property bool dirty: building && (name !== (originalSnapshot.name || "")
        || parentId !== (originalSnapshot.parentId || 0)
        || JSON.stringify(photos.map(p => p.id)) !== JSON.stringify(originalSnapshot.photoIds || []))
    signal pulsed()
    signal saved(double id)

    function openNew(parent, rows) {
        return requestOpen({kind: "new", parentId: parent || 0, rows: (rows || []).slice()})
    }
    function openExisting(id) {
        if (building && albumId === id) { pulsed(); return true }
        return requestOpen({kind: "existing", id: id})
    }
    function requestOpen(request) {
        if (dirty) { pendingRequest = request; return false }
        return executeRequest(request)
    }
    function executeRequest(request) {
        if (request.kind === "discard") { discard(); return true }
        if (request.kind === "existing") {
            const snapshot = LibraryService.photoAlbumSnapshot(request.id)
            if (!snapshot || snapshot.id !== request.id) {
                error = "This album is no longer available."
                return false
            }
            const rows = LibraryService.customAlbumPhotos(request.id)
            albumId = request.id
            originalSnapshot = snapshot
            name = snapshot.name
            parentId = snapshot.parentId
            photos = rows
        } else {
            albumId = -1
            originalSnapshot = {name: "", parentId: request.parentId, photoIds: []}
            name = ""
            parentId = request.parentId
            photos = []
        }
        generation++
        error = ""
        building = true
        if (request.kind === "new") addPhotos(request.rows)
        return true
    }
    function requestDiscard() {
        if (dirty) pendingRequest = {kind: "discard"}
        else discard()
    }
    function confirmDiscard() {
        const request = pendingRequest
        pendingRequest = null
        if (request) executeRequest(request)
    }
    function cancelDiscard() { pendingRequest = null }
    function addPhotos(rows) {
        if (!building || discardConfirmation) return 0
        const have = ({})
        const out = photos.slice()
        for (const p of out) have[p.id] = true
        let added = 0
        for (const row of rows || []) {
            const id = Number(row.id !== undefined ? row.id : row.libraryId)
            if (!Number.isSafeInteger(id) || id <= 0 || have[id]) continue
            have[id] = true
            out.push(Object.assign({}, row, {id: id}))
            added++
        }
        if (added) { photos = out; error = "" }
        pulsed()
        return added
    }
    function removeAt(index) {
        if (index < 0 || index >= photos.length) return
        const out = photos.slice()
        out.splice(index, 1)
        photos = out
    }
    function move(from, to) {
        if (from === to || from < 0 || to < 0 || from >= photos.length || to >= photos.length) return
        const out = photos.slice()
        out.splice(to, 0, out.splice(from, 1)[0])
        photos = out
    }
    function save() {
        if (!building) return false
        if (!name.trim()) { error = "Give your album a name."; return false }
        const result = LibraryService.savePhotoAlbumDraft(albumId, name.trim(), parentId,
            photos.map(p => p.id), originalSnapshot)
        if (!result.success) { error = result.error || "Couldn't save this album."; return false }
        const id = result.id
        discard()
        saved(id)
        return true
    }
    function discard() {
        building = false
        pendingRequest = null
        albumId = -1
        name = ""
        parentId = 0
        photos = []
        originalSnapshot = ({})
        generation++
        error = ""
    }
}
