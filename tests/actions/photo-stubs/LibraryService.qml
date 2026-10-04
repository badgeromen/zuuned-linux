pragma Singleton
import QtQuick
QtObject {
    property int localTrackIdentityRevision: 0
    function hasLocalTrack(artist, album, title, disc, track, discReliable, trackReliable, sourcePeers) { return false }
    function musicIdentityPeers(track) { return [] }
    property var artPaths: ({})
    property var playlists: []
    property var photos: []
    property var edits: []
    property var storedPlaylist: ({})
    property var collectionArts: ({})
    property int collectionArtRevision: 0
    property var tracks: QtObject {
        property var rows: []
        function rowsSnapshot() { return rows }
    }
    signal libraryChanged()
    signal customizeArtReady(string requestId, var urls, string error)
    signal customizeIdentityReady(string requestId, var matches, string error)
    signal customizationFinished(string requestId, bool success, string error)
    function collectionArt(kind, key) { return collectionArts[kind + ":" + key] || "" }
    function customizeContext(kind, context) { return context }
    function requestCustomizeArt(requestId, kind, source, context) {}
    function searchCustomizeIdentity(requestId, query, tv) {}
    function searchCustomizeMusicIdentity(requestId, kind, query, provider, context) {}
    function applyCustomization(requestId, kind, context, draft) {}
    property int photoCount: photos.length
    property var customAlbums: []
    property var members: ({})
    property string albumFailure: ""
    property int nextAlbumId: 100
    function customPhotoAlbums() { return customAlbums }
    function customAlbumPhotos(id) { return (members[id] || []).map(id => photos.find(p => p.id === id)).filter(p => p) }
    function photoAlbumSnapshot(id) {
        const album = customAlbums.find(a => a.id === id)
        return album ? {id:id,name:album.name,parentId:album.parentId,photoIds:(members[id] || []).slice()} : ({})
    }
    function savePhotoAlbumDraft(id,name,parentId,ids,snapshot) {
        if (albumFailure) return {success:false,error:albumFailure}
        if (ids.some(id => !photos.some(p => p.id === id))) return {success:false,error:"Photo is missing"}
        if (id <= 0) return createPhotoAlbum(name,parentId,ids)
        if (JSON.stringify(photoAlbumSnapshot(id)) !== JSON.stringify(snapshot)) return {success:false,error:"Album changed"}
        customAlbums = customAlbums.map(a => a.id === id ? Object.assign({},a,{name:name,parentId:parentId}) : a)
        members[id] = ids.slice()
        libraryChanged(); return {success:true,id:id}
    }
    function createPhotoAlbum(name, parentId, photoIds) {
        if (albumFailure) return {success:false, error:albumFailure}
        const id = nextAlbumId++
        members[id] = photoIds || []
        customAlbums = customAlbums.concat([{id:id,parentId:parentId,name:name,pathLabel:name,count:0,covers:[]}])
        libraryChanged(); return {success:true,id:id}
    }
    function renamePhotoAlbum(id,name) {
        if (albumFailure) return {success:false,error:albumFailure}
        customAlbums = customAlbums.map(a => a.id === id ? Object.assign({},a,{name:name}) : a)
        libraryChanged(); return {success:true}
    }
    function movePhotoAlbum(id,parentId) {
        customAlbums = customAlbums.map(a => a.id === id ? Object.assign({},a,{parentId:parentId}) : a)
        libraryChanged(); return {success:true}
    }
    function deletePhotoAlbum(id) {
        customAlbums = customAlbums.filter(a => a.id !== id && a.parentId !== id)
        libraryChanged(); return {success:true}
    }
    function addPhotosToAlbum(id,ids) {
        members[id] = (members[id] || []).concat(ids.filter(p => !(members[id] || []).includes(p)))
        libraryChanged(); return {success:true}
    }
    function removePhotosFromAlbum(id,ids) {
        members[id] = (members[id] || []).filter(p => !ids.includes(p))
        libraryChanged(); return {success:true}
    }
    function setPhotoAlbumOrder(id,ids) { members[id] = ids; libraryChanged(); return {success:true} }
    function photoAlbums(stems) { return photos.length ? [{name:"Album", path:"/tmp/album", count:photos.length, covers:[]}] : [] }
    function photosInAlbum(path) { return photos }
    function requestArt(artist, album, filepath) {}
    function artKey(artist, album) { return artist + "|" + album }
    function dominantColor(url) { return "#d4367a" }
    function addWatchFolder(path, kind) {}
    function deletePhotos(ids) {}
    function playlistTracks(id) { return tracks.rows.map((t, index) => ({id:t.libraryId,playlistEntryId:1000 + index,title:t.title,artist:t.artist,album:t.album,filepath:t.filepath})) }
    function renamePlaylist(id, name) { storedPlaylist = {id:id, name:name} }
    function setPlaylistTracks(id, ids) { storedPlaylist = Object.assign({}, storedPlaylist, {tracks:ids}) }
    function editPlaylistEntries(id, name, rows, original) {
        storedPlaylist = {id:id, name:name, tracks:rows.map(t => t.id),
            entries:rows.map(t => t.playlistEntryId), original:original.slice()}
        return true
    }
    function createPlaylist(name, ids) { storedPlaylist = {id:77, name:name, tracks:ids}; return 77 }
    function addToPlaylist(id, ids) {}
    function editTrackMetadata(id, fields) { edits = edits.concat([{id:id, fields:fields}]); return {success:true, fileWritten:true} }
}
