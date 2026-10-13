pragma Singleton
import QtQuick
import Zuuned

// Local organization only. The native service owns revision-checked persistence.
QtObject {
    id: tray
    property bool building: false
    property bool saving: false
    property var context: ({})
    property string name: ""
    property var bookIds: []
    property string error: ""
    property string pendingOperation: ""
    property var pendingRequest: null
    property bool deleteConfirmation: false
    readonly property bool discardConfirmation: pendingRequest !== null
    readonly property bool locked: saving || discardConfirmation || deleteConfirmation
    readonly property bool dirty: building && (name !== (context.title || "")
        || JSON.stringify(bookIds) !== JSON.stringify(context.bookIds || []))
    readonly property var books: {
        void SpokenMediaService.revision
        return bookIds.map(id => SpokenMediaService.item(id))
    }
    signal pulsed()
    signal saved(string id)
    signal deleted(string id)

    function openFor(entry, seed) {
        if (saving) return false
        if (building && entry && entry.id && context.id === entry.id) {
            if (seed) addBooks([seed])
            pulsed(); return true
        }
        const request = {entry: Object.assign({}, entry || {}), seed: seed || ""}
        if (dirty) { pendingRequest = request; pulsed(); return false }
        execute(request); return true
    }
    function execute(request) {
        if (request.discard) { reset(); return }
        context = Object.assign({}, request.entry)
        if (context.id) {
            const live = SpokenMediaService.audiobookCollections().find(b => b.id === context.id)
            if (!live) { error = "This library is no longer available."; pulsed(); return }
            context = Object.assign({}, live)
        }
        bookIds = (context.bookIds || []).slice()
        name = context.title || ""
        error = ""; deleteConfirmation = false; building = true
        if (request.seed) addBooks([request.seed])
        pulsed()
    }
    function addBooks(ids) {
        if (!building || locked) return 0
        const next = bookIds.slice()
        for (const id of ids || []) {
            if (next.length >= 5000) { error = "A library can contain up to 5,000 books."; break }
            if (typeof id !== "string" || next.includes(id)) continue
            if (SpokenMediaService.item(id).kind !== "book") continue
            next.push(id)
        }
        const added = next.length - bookIds.length
        bookIds = next
        pulsed()
        return added
    }
    function removeAt(index) {
        if (locked || index < 0 || index >= bookIds.length) return
        const next = bookIds.slice(); next.splice(index, 1); bookIds = next
    }
    function move(from, to) {
        if (locked || from < 0 || to < 0 || from >= bookIds.length || to >= bookIds.length || from === to) return
        const next = bookIds.slice(); next.splice(to, 0, next.splice(from, 1)[0]); bookIds = next
    }
    function requestDiscard() {
        if (saving) return
        if (dirty) pendingRequest = {discard: true}
        else reset()
    }
    function confirmDiscard() {
        if (saving) return
        const request = pendingRequest; pendingRequest = null
        if (request) execute(request)
    }
    function cancelDiscard() { pendingRequest = null }
    function save() {
        if (!building || locked || SpokenMediaService.busy) return
        if (!name.trim()) { error = "Give your library a name."; return }
        error = ""; saving = true; pendingOperation = "save audiobook collection"
        SpokenMediaService.saveAudiobookCollection(context.id || "", name.trim(), bookIds, context.revision || 0)
    }
    function requestDelete() {
        if (!locked && context.id) deleteConfirmation = true
    }
    function confirmDelete() {
        if (!deleteConfirmation || saving || SpokenMediaService.busy) return
        deleteConfirmation = false; saving = true; error = ""; pendingOperation = "delete audiobook collection"
        SpokenMediaService.deleteAudiobookCollection(context.id, context.revision)
    }
    function reset() {
        building = false; saving = false; pendingRequest = null; deleteConfirmation = false
        context = ({}); name = ""; bookIds = []; error = ""; pendingOperation = ""
    }
    property Connections serviceConnection: Connections {
        target: SpokenMediaService
        function onOperationFinished(operation, id, ok, error) {
            if (!tray.building || !tray.saving || operation !== tray.pendingOperation
                || (tray.context.id && id !== tray.context.id)) return
            tray.saving = false; tray.pendingOperation = ""
            if (ok) { tray.reset(); if (operation === "delete audiobook collection") tray.deleted(id); else tray.saved(id) }
            else tray.error = error
        }
    }
}
