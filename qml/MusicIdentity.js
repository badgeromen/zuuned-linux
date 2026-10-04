.pragma library

// The library groups albums by album artist + title. Device snapshots
// lack albumartist, so their track artist is the available owner.
function owner(track) {
    return track.albumartist || track.artist || "Unknown Artist"
}

function fromTrack(track) {
    return { name: track.album || "Unknown Album", artist: owner(track) }
}

function fromAlbum(album) {
    return { name: album.name || "Unknown Album",
             artist: album.artArtist || album.artist || album.subtitle || "Unknown Artist" }
}

function key(album) {
    return JSON.stringify([album.artist.toLowerCase(), album.name.toLowerCase()])
}

function matches(track, album) {
    return album && key(fromTrack(track)) === key(album)
}

// Local music keeps disc identity through album drill-downs, playback and sync.
// Unknown disc behaves as disc1; unknown track retains a stable title/path order.
function compareAlbumTracks(a, b) {
    const disc = (a.discNumber > 0 ? a.discNumber : 1) - (b.discNumber > 0 ? b.discNumber : 1)
    if (disc) return disc
    return (a.trackNumber || 0) - (b.trackNumber || 0)
        || (a.title || "").localeCompare(b.title || "", undefined, { sensitivity: "base" })
        || (a.filepath || "").localeCompare(b.filepath || "")
}

function groupAlbums(tracks) {
    const groups = {}
    for (const track of tracks) {
        const album = fromTrack(track)
        const id = key(album)
        if (!groups[id]) {
            groups[id] = { name: album.name, subtitle: album.artist,
                artArtist: album.artist, artAlbum: track.album || "",
                artItemId: track.itemId || 0,
                firstTrackPath: track.filepath || "", count: 0 }
        }
        groups[id].count++
    }
    return Object.values(groups).sort((a, b) =>
        a.name.localeCompare(b.name, undefined, { sensitivity: "base" })
        || a.artArtist.localeCompare(b.artArtist, undefined, { sensitivity: "base" }))
}
