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

/* Check if a property code is in a supported list */
static int prop_supported(uint16_t prop, uint16_t *list, int count) {
    for (int i = 0; i < count; i++)
        if (list[i] == prop) return 1;
    return 0;
}

/* ---- Create album object ---- */

uint32_t zune_forge_album(ZuneDevice *dev,
                                   const char *album_name,
                                   const char *artist_name,
                                   const char *genre,
                                   uint32_t *track_ids, int track_count) {
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
        fprintf(stderr, "[libzune] album (0xBA03) supported props (%d):", sup_count);
        for (int i = 0; i < sup_count; i++)
            fprintf(stderr, " 0x%04X", supported[i]);
        fprintf(stderr, "\n");
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

    if (!supported || prop_supported(MTP_OPC_Name, supported, sup_count)) {
        props[n].prop_code = MTP_OPC_Name;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)album_name;
        n++;
    }

    /* Zune does NOT support AlbumArtist (0xDC9B) on album objects.
     * Use Artist (0xDC46), which is in the supported props list. */
    if (!supported || prop_supported(MTP_OPC_Artist, supported, sup_count)) {
        props[n].prop_code = MTP_OPC_Artist;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)artist_name;
        n++;
    }

    if (genre && genre[0] &&
        (!supported || prop_supported(MTP_OPC_Genre, supported, sup_count))) {
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
    if (mtp_send_object(&dev->ptp, NULL, 0) != 0) {
        zune_set_error("Album commit (SendObject) failed for: %s (%s 0x%04X)",
                       album_name, zune_autopsy_name(dev->ptp.last_response),
                       dev->ptp.last_response);
        mtp_delete_object(&dev->ptp, album_id);
        return 0;
    }

    /* Step 6: Set object references (track IDs) */
    if (track_ids && track_count > 0) {
        ret = mtp_set_object_references(&dev->ptp, album_id,
                                         track_ids, track_count);
        if (ret != 0) {
            fprintf(stderr, "[libzune] warning: set references on album %u failed\n", album_id);
        }
    }

    fprintf(stderr, "[libzune] created album \"%s\" by \"%s\" (%d tracks, id=%u)\n",
            album_name, artist_name, track_count, album_id);
    return album_id;
}

/* ---- Update existing album ---- */

int zune_rewire_album(ZuneDevice *dev, uint32_t album_id,
                              const char *album_name,
                              const char *artist_name,
                              uint32_t *track_ids, int track_count) {
    if (!dev) {
        zune_set_error("Invalid device for album update");
        return -1;
    }

    /* Update Name property */
    if (album_name && album_name[0]) {
        int ret = mtp_set_object_prop_value_str(&dev->ptp, album_id,
                                                 MTP_OPC_Name, album_name);
        if (ret != 0) {
            fprintf(stderr, "[libzune] warning: update Name on album %u failed\n", album_id);
        }
    }

    /* Update Artist property (Zune does not support AlbumArtist on albums) */
    if (artist_name && artist_name[0]) {
        int ret = mtp_set_object_prop_value_str(&dev->ptp, album_id,
                                                 MTP_OPC_Artist, artist_name);
        if (ret != 0) {
            fprintf(stderr, "[libzune] warning: update Artist on album %u failed\n", album_id);
        }
    }

    /* Update object references (track IDs) */
    if (track_ids && track_count > 0) {
        int ret = mtp_set_object_references(&dev->ptp, album_id,
                                             track_ids, track_count);
        if (ret != 0) {
            zune_set_error("Failed to update album %u", album_id);
            return -1;
        }
    }

    fprintf(stderr, "[libzune] updated album id=%u (%d tracks)\n",
            album_id, track_count);
    return 0;
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
        uint8_t *data = NULL;
        uint32_t len = 0;
        if (dev->cancel_requested || mtp_get_object_prop_value(&dev->ptp,
                handles[i], MTP_OPC_Name, &data, &len) != 0 || !data || !len) {
            free(data); free(handles); return -1;
        }
        char *candidate = mtp_ucs2_to_string(data, len);
        free(data);
        if (!candidate || !candidate[0]) { free(candidate); free(handles); return -1; }
        if (artist_name_equal(candidate, name) && (!*found || handles[i] < *found))
            *found = handles[i];
        free(candidate);
    }
    free(handles);
    return dev->cancel_requested ? -1 : 0;
}

uint32_t zune_forge_artist(ZuneDevice *dev,
                                    const char *artist_name) {
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
        fprintf(stderr, "[libzune] artist format 0xB218 not supported by device — skipping\n");
        free(supported);
        return 0;
    }

    uint32_t existing = 0;
    if (find_artist(dev, artist_name, &existing) != 0) {
        free(supported);
        zune_set_error("Could not check existing artists; refusing duplicate-prone creation");
        fprintf(stderr, "[libzune] artist lookup failed — creation skipped\n");
        return 0;
    }
    if (existing) {
        free(supported);
        fprintf(stderr, "[libzune] reusing artist object \"%s\" (handle=%u)\n", artist_name, existing);
        return existing;
    }

    fprintf(stderr, "[libzune] artist (0xB218) supported props (%d):", sup_count);
    for (int i = 0; i < sup_count; i++)
        fprintf(stderr, " 0x%04X", supported[i]);
    fprintf(stderr, "\n");

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

    if (prop_supported(MTP_OPC_Name, supported, sup_count)) {
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
        fprintf(stderr, "[libzune] artist creation failed for \"%s\" — album AlbumArtist will be used instead\n",
                artist_name);
        return 0;
    }

    /* SendObject — abstract objects commit with zero bytes. Uncommitted
     * artists are soft-skipped like creation failures (album AlbumArtist
     * carries the display name), but returning the handle of an object
     * the device never committed poisons zune_link_artist downstream. */
    /* Zero-byte commit matching the declared size (see album path). */
    if (mtp_send_object(&dev->ptp, NULL, 0) != 0) {
        fprintf(stderr, "[libzune] artist commit (SendObject) failed (%s 0x%04X) — deleting orphan\n",
                zune_autopsy_name(dev->ptp.last_response), dev->ptp.last_response);
        mtp_delete_object(&dev->ptp, artist_handle);
        return 0;
    }

    fprintf(stderr, "[libzune] created artist object \"%s\" (handle=%u)\n",
            artist_name, artist_handle);
    return artist_handle;
}

/* ---- Set ArtistId on album or track ---- */

int zune_link_artist(ZuneDevice *dev, uint32_t object_id,
                        uint32_t artist_handle) {
    if (!dev) {
        zune_set_error("Invalid device for artist linking");
        return -1;
    }

    int ret = mtp_set_object_prop_value_u32(&dev->ptp, object_id,
                                             ZUNE_OPC_ArtistId, artist_handle);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

/* ---- Get existing album objects ---- */

ZuneAlbumObject *zune_get_albums(ZuneDevice *dev, int *count) {
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
            uint8_t *prop_data = NULL;
            uint32_t prop_len = 0;
            if (mtp_get_object_prop_value(&dev->ptp, handles[i],
                                           MTP_OPC_Name, &prop_data, &prop_len) == 0
                && prop_data && prop_len > 0) {
                char *name_str = mtp_ucs2_to_string(prop_data, prop_len);
                if (name_str && name_str[0]) {
                    free(albums[n].name);
                    albums[n].name = name_str;
                } else {
                    free(name_str);
                }
                free(prop_data);
            }
        }

        /* Read the Artist property — Zune does NOT support AlbumArtist (0xDC9B)
         * on album objects per ZUNE_MTP_PROTOCOL_FINDINGS.md. Use Artist (0xDC46),
         * which is in the supported props list and matches what tracks use. */
        {
            uint8_t *prop_data = NULL;
            uint32_t prop_len = 0;
            if (mtp_get_object_prop_value(&dev->ptp, handles[i],
                                           MTP_OPC_Artist, &prop_data, &prop_len) == 0
                && prop_data && prop_len > 0) {
                albums[n].artist = mtp_ucs2_to_string(prop_data, prop_len);
                free(prop_data);
            }
            if (!albums[n].artist)
                albums[n].artist = strdup("");
        }

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
