/*
 * album.c — MTP album/artist object hierarchy
 *
 * The Zune reads album/artist metadata from MTP abstract objects,
 * not from individual track properties. After sending tracks, these
 * objects must be created and linked for proper display on the device.
 *
 * Reference: zune-explorer's zune-manager.js _createAlbumObjects()
 *
 * Part of libzune.
 */

#include "zune_internal.h"

/* ---- Failed creation ---- */

uint32_t zune_forge_failed(ZuneDevice *dev, uint32_t id, const char *kind,
                           const char *name, const char *stage) {
    uint16_t original = dev->ptp.last_response;
    int removed = mtp_delete_object(&dev->ptp, id) == 0;
    dev->ptp.last_response = original;
    zune_set_error("%s '%s': %s failed (%s 0x%04X); new object %u %s", kind, name, stage,
                   zune_autopsy_name(original), original, id,
                   removed ? "removed" : "could not be removed; inspect before retrying");
    ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "%s '%s': %s failed (0x%04X); new object %u %s", kind, name,
         stage, original, id, removed ? "removed" : "remains");
    return 0;
}

/* ---- Create album object ---- */

uint32_t zune_forge_album(ZuneDevice *dev,
                                   const char *album_name,
                                   const char *artist_name,
                                   const char *genre,
                                   uint32_t *track_ids, int track_count) {
    zune_clear_error();
    if (!dev || !album_name || !artist_name) {
        zune_set_error("Invalid parameters for album creation");
        return 0;
    }

    /*
     * Album creation via SendObjectPropList (0x9808) — atomic creation.
     * Query the device for supported properties first (like vendored libmtp).
     * Only include properties the Zune reports as supported.
     */

    /* Query supported properties for AbstractAudioAlbum */
    uint16_t *supported = NULL;
    int sup_count = 0;
    if (mtp_get_props_supported(&dev->ptp, MTP_OFC_AbstractAudioAlbum,
                                 &supported, &sup_count) == 0) {
        char label[64];
        snprintf(label, sizeof(label), "album (0xBA03) supported props (%d):", sup_count);
        zune_log_codes(ZUNE_CHATTER_WHISPER, "libzune", label, supported, sup_count);
    }

    /* Build filename */
    size_t namelen = strlen(album_name);
    char *filename = malloc(namelen + 5);
    if (!filename) { free(supported); return 0; }
    snprintf(filename, namelen + 5, "%s.alb", album_name);

    /* Build property list — only include supported properties */
    mtp_prop_entry_t props[6];
    memset(props, 0, sizeof(props));
    int n = 0;

    /* ObjectFileName — always required */
    props[n].prop_code = MTP_OPC_ObjectFileName;
    props[n].data_type = PTP_DTC_STR;
    props[n].value.str = filename;
    n++;

    if (!supported || zune_prop_listed(MTP_OPC_Name, supported, sup_count)) {
        props[n].prop_code = MTP_OPC_Name;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)album_name;
        n++;
    }

    /* Zune does NOT support AlbumArtist (0xDC9B) on album objects.
     * Use Artist (0xDC46), which is in the supported props list. */
    if (!supported || zune_prop_listed(MTP_OPC_Artist, supported, sup_count)) {
        props[n].prop_code = MTP_OPC_Artist;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)artist_name;
        n++;
    }

    if (genre && genre[0] &&
        (!supported || zune_prop_listed(MTP_OPC_Genre, supported, sup_count))) {
        props[n].prop_code = MTP_OPC_Genre;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)genre;
        n++;
    }

    free(supported);

    uint32_t album_id = 0;
    int ret = mtp_send_object_prop_list(&dev->ptp, dev->storage_id, 0,
                                         MTP_OFC_AbstractAudioAlbum, 0,
                                         props, n, &album_id);
    free(filename);

    if (ret != 0) {
        zune_set_error("Failed to forge album: %s by %s", album_name, artist_name);
        return 0;
    }

    /* SendObject commit — ZERO bytes, matching the size declared in
     * SendObjectPropList (0). The old "at least 1 byte" hack CONTRADICTED
     * the declared size and the Zune refused with 0x2002 on every album
     * creation (hardware-observed 2026-08-27); the historically-working
     * SendObjectInfo flow (docs/ALBUM_HIERARCHY.md) also committed with
     * an empty SendObject. Requires the zero-length ZLP guard in ptp.c.
     * On failure, delete the half-created handle — orphaned nameless
     * album objects otherwise adopt freshly synced tracks at re-index
     * and display as their GUID moniker. */
    if (mtp_send_object(&dev->ptp, NULL, 0) != 0)
        return zune_forge_failed(dev, album_id, "Album", album_name, "commit (SendObject)");

    /* Step 6: Set object references (track IDs). An album without its tracks
     * is not the album that was asked for. */
    if (track_ids && track_count > 0 &&
        mtp_set_object_references(&dev->ptp, album_id, track_ids, track_count) != 0) {
        uint16_t code = dev->ptp.last_response;
        if (!zune_response_uncertain(code))
            return zune_forge_failed(dev, album_id, "Album", album_name, "track references");
        /* No answer: the device may have applied the references, and a Zune
         * HD deletes an album's referenced tracks along with the album. Keep
         * the album rather than risk tracks that were just sent. */
        zune_set_error("Album '%s': track references got no answer (%s 0x%04X); new object %u kept, "
                       "since removing it could remove its tracks; inspect it before retrying",
                       album_name, zune_autopsy_name(code), code, album_id);
        ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "Album '%s': track references got no answer (0x%04X); "
             "new object %u kept", album_name, code, album_id);
        return 0;
    }

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "created album \"%s\" by \"%s\" (%d tracks, id=%u)",
            album_name, artist_name, track_count, album_id);
    return album_id;
}

/* ---- Update existing album ---- */

int zune_rewire_album(ZuneDevice *dev, uint32_t album_id,
                              const char *album_name,
                              const char *artist_name,
                              uint32_t *track_ids, int track_count) {
    zune_clear_error();
    if (!dev) {
        zune_set_error("Invalid device for album update");
        return ZUNE_BLOWN;
    }

    /* Update Name property */
    if (album_name && album_name[0]) {
        int ret = mtp_set_object_prop_value_str(&dev->ptp, album_id,
                                                 MTP_OPC_Name, album_name);
        if (ret != 0) {
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "warning: update Name on album %u failed", album_id);
        }
    }

    /* Update Artist property (Zune does not support AlbumArtist on albums) */
    if (artist_name && artist_name[0]) {
        int ret = mtp_set_object_prop_value_str(&dev->ptp, album_id,
                                                 MTP_OPC_Artist, artist_name);
        if (ret != 0) {
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "warning: update Artist on album %u failed", album_id);
        }
    }

    /* Update object references (track IDs) */
    if (track_ids && track_count > 0) {
        int ret = mtp_set_object_references(&dev->ptp, album_id,
                                             track_ids, track_count);
        if (ret != 0) {
            zune_set_error("Failed to update album %u", album_id);
            return ZUNE_BLOWN;
        }
    }

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "updated album id=%u (%d tracks)",
            album_id, track_count);
    return ZUNE_OK;
}

/* ---- Create artist object ---- */

/*
 * Artist objects are MTP abstract objects with format 0xB218 (AbstractArtist).
 * zune-explorer sends them as: SendObjectInfo -> SendObject(empty) -> SetPropValue(Name).
 *
 * With the native MTP stack we can send 0xB218 directly since we control
 * the format code without an enum mapping layer.
 */

/* Names are UTF-8: fold ASCII case only, preserving accented bytes rather
 * than conflating different Unicode names. Ignore outer ASCII whitespace. */
static int artist_name_equal(const char *a, const char *b) {
    while (*a && (unsigned char)*a <= 32) a++;
    while (*b && (unsigned char)*b <= 32) b++;
    size_t na = strlen(a), nb = strlen(b);
    while (na && (unsigned char)a[na - 1] <= 32) na--;
    while (nb && (unsigned char)b[nb - 1] <= 32) nb--;
    if (na != nb) return 0;
    for (size_t i = 0; i < na; i++) {
        unsigned char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
    }
    return 1;
}

/* Return -1 if the inventory cannot establish absence. Never create on an
 * unreadable inventory. Existing duplicates are retained, using the lowest
 * matching handle for future links; this is not a device cleanup operation. */
static int find_artist(ZuneDevice *dev, const char *name, uint32_t *found) {
    uint32_t *handles = NULL;
    int count = 0;
    *found = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id,
            MTP_OFC_AbstractArtist, 0, &handles, &count) != 0) {
        free(handles);
        return -1;
    }
    if (count < 0 || (count && !handles)) { free(handles); return -1; }
    for (int i = 0; i < count; i++) {
        if (ZUNE_FLAG_GET(dev->cancel_requested)) { free(handles); return -1; }
        char *candidate = mtp_read_string_prop(&dev->ptp, handles[i], MTP_OPC_Name);
        if (!candidate || !candidate[0]) { free(candidate); free(handles); return -1; }
        if (artist_name_equal(candidate, name) && (!*found || handles[i] < *found))
            *found = handles[i];
        free(candidate);
    }
    free(handles);
    return ZUNE_FLAG_GET(dev->cancel_requested) ? -1 : 0;
}

uint32_t zune_forge_artist(ZuneDevice *dev,
                                    const char *artist_name) {
    zune_clear_error();
    if (!dev || !artist_name || artist_name_equal(artist_name, "")) {
        zune_set_error("Invalid parameters for artist creation");
        return 0;
    }

    /*
     * Artist creation via SendObjectPropList (0x9808).
     * Query supported properties first — the Zune may not support
     * AbstractArtist (0xB218) at all.
     */

    /* Check if the device supports this format */
    uint16_t *supported = NULL;
    int sup_count = 0;
    if (mtp_get_props_supported(&dev->ptp, MTP_OFC_AbstractArtist,
                                 &supported, &sup_count) != 0 || sup_count == 0) {
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "artist format 0xB218 not supported by device — skipping");
        free(supported);
        return 0;
    }

    uint32_t existing = 0;
    if (find_artist(dev, artist_name, &existing) != 0) {
        free(supported);
        zune_set_error("Could not check existing artists; refusing duplicate-prone creation");
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "artist lookup failed — creation skipped");
        return 0;
    }
    if (existing) {
        free(supported);
        ZLOG(ZUNE_CHATTER_TALK, "libzune", "reusing artist object \"%s\" (handle=%u)", artist_name, existing);
        return existing;
    }

    {
        char label[64];
        snprintf(label, sizeof(label), "artist (0xB218) supported props (%d):", sup_count);
        zune_log_codes(ZUNE_CHATTER_WHISPER, "libzune", label, supported, sup_count);
    }

    /* Build filename */
    size_t namelen = strlen(artist_name);
    char *filename = malloc(namelen + 5);
    if (!filename) { free(supported); return 0; }
    snprintf(filename, namelen + 5, "%s.art", artist_name);

    /* Build property list — only include supported properties */
    mtp_prop_entry_t props[3];
    memset(props, 0, sizeof(props));
    int n = 0;

    props[n].prop_code = MTP_OPC_ObjectFileName;
    props[n].data_type = PTP_DTC_STR;
    props[n].value.str = filename;
    n++;

    if (zune_prop_listed(MTP_OPC_Name, supported, sup_count)) {
        props[n].prop_code = MTP_OPC_Name;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)artist_name;
        n++;
    }

    free(supported);

    uint32_t artist_handle = 0;
    int ret = mtp_send_object_prop_list(&dev->ptp, dev->storage_id, 0,
                                         MTP_OFC_AbstractArtist, 0,
                                         props, n, &artist_handle);
    free(filename);

    if (ret != 0) {
        zune_set_error("Failed to forge artist: %s", artist_name);
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune", "artist creation failed for \"%s\" — album AlbumArtist will be used instead",
                artist_name);
        return 0;
    }

    /* SendObject — abstract objects commit with zero bytes. Uncommitted
     * artists are soft-skipped like creation failures (album AlbumArtist
     * carries the display name), but returning the handle of an object
     * the device never committed poisons zune_link_artist downstream. */
    /* Zero-byte commit matching the declared size (see album path). */
    if (mtp_send_object(&dev->ptp, NULL, 0) != 0)
        return zune_forge_failed(dev, artist_handle, "Artist", artist_name, "commit (SendObject)");

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "created artist object \"%s\" (handle=%u)",
            artist_name, artist_handle);
    return artist_handle;
}

/* ---- Set ArtistId on album or track ---- */

int zune_link_artist(ZuneDevice *dev, uint32_t object_id,
                        uint32_t artist_handle) {
    zune_clear_error();
    if (!dev) {
        zune_set_error("Invalid device for artist linking");
        return ZUNE_BLOWN;
    }

    int ret = mtp_set_object_prop_value_u32(&dev->ptp, object_id,
                                             ZUNE_OPC_ArtistId, artist_handle);
    if (ret != 0) {
        return ZUNE_BLOWN;
    }
    return ZUNE_OK;
}

/* ---- Get existing album objects ---- */

ZuneAlbumObject *zune_get_albums(ZuneDevice *dev, int *count) {
    zune_clear_error();
    if (!dev || !count) {
        if (count) *count = 0;
        return NULL;
    }

    *count = 0;

    /* Get all object handles with format=AbstractAudioAlbum */
    uint32_t *handles = NULL;
    int nhandles = 0;
    int ret = mtp_get_object_handles(&dev->ptp, dev->storage_id,
                                      MTP_OFC_AbstractAudioAlbum,
                                      0x00000000, &handles, &nhandles);
    if (ret != 0 || nhandles == 0) {
        free(handles);
        return NULL;
    }

    ZuneAlbumObject *albums = calloc(nhandles, sizeof(ZuneAlbumObject));
    if (!albums) {
        free(handles);
        return NULL;
    }

    int n = 0;
    for (int i = 0; i < nhandles; i++) {
        /* Get object info for the album name */
        mtp_object_info_t info;
        memset(&info, 0, sizeof(info));
        if (mtp_get_object_info(&dev->ptp, handles[i], &info) != 0)
            continue;

        albums[n].album_id = handles[i];
        albums[n].name = strdup(info.filename ? info.filename : "");

        mtp_free_object_info(&info);

        /* Read the Name property (more accurate than filename) */
        {
            char *name_str = mtp_read_string_prop(&dev->ptp, handles[i], MTP_OPC_Name);
            if (name_str && name_str[0]) {
                free(albums[n].name);
                albums[n].name = name_str;
            } else {
                free(name_str);
            }
        }

        /* Read the Artist property — Zune does NOT support AlbumArtist (0xDC9B)
         * on album objects per ZUNE_MTP_PROTOCOL_FINDINGS.md. Use Artist (0xDC46),
         * which is in the supported props list and matches what tracks use. */
        albums[n].artist = mtp_read_string_prop(&dev->ptp, handles[i], MTP_OPC_Artist);
        if (!albums[n].artist)
            albums[n].artist = strdup("");

        /* Get object references (track IDs) */
        {
            uint32_t *refs = NULL;
            int nrefs = 0;
            if (mtp_get_object_references(&dev->ptp, handles[i],
                                           &refs, &nrefs) == 0
                && refs && nrefs > 0) {
                albums[n].track_ids = refs;
                albums[n].track_count = (uint32_t)nrefs;
            } else {
                free(refs);
                albums[n].track_ids = NULL;
                albums[n].track_count = 0;
            }
        }

        n++;
    }

    free(handles);

    *count = n;
    return albums;
}

void zune_free_albums(ZuneAlbumObject *albums, int count) {
    if (!albums) return;
    for (int i = 0; i < count; i++) {
        free(albums[i].name);
        free(albums[i].artist);
        free(albums[i].track_ids);
    }
    free(albums);
}
