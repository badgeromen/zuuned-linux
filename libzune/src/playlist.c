/*
 * playlist.c — Playlist CRUD operations
 *
 * Part of libzune. Pure C, no GTK/GLib dependencies.
 * Uses native PTP/MTP stack (mtp.h) instead of vendored libmtp.
 */

#include "zune_internal.h"

/* Same helper as album.c's (file-static there). */
static int pl_prop_supported(uint16_t prop, uint16_t *list, int count) {
    for (int i = 0; i < count; i++)
        if (list[i] == prop)
            return 1;
    return 0;
}

/* ---- Get all playlists ---- */

ZunePlaylist *zune_get_playlists(ZuneDevice *dev, int *count) {
    if (!dev || !count) {
        zune_set_error("Invalid device or count pointer");
        if (count) *count = 0;
        return NULL;
    }

    *count = 0;

    if (dev->disconnecting) {
        zune_set_error("Device is disconnecting");
        return NULL;
    }

    /* Get all object handles with format=AbstractAudioVideoPlaylist */
    uint32_t *handles = NULL;
    int n = 0;
    int ret = mtp_get_object_handles(&dev->ptp, 0xFFFFFFFF,
                                     MTP_OFC_AbstractAudioVideoPlaylist,
                                     0x00000000, &handles, &n);
    if (ret != 0 || n == 0) {
        free(handles);
        return NULL;
    }

    ZunePlaylist *playlists = calloc(n, sizeof(ZunePlaylist));
    if (!playlists) {
        zune_set_error("Out of memory allocating %d playlists", n);
        free(handles);
        return NULL;
    }

    int valid = 0;
    for (int i = 0; i < n; i++) {
        if (dev->disconnecting) break;

        uint32_t id = handles[i];

        /* Get playlist name from object info */
        mtp_object_info_t info = {0};
        ret = mtp_get_object_info(&dev->ptp, id, &info);
        if (ret != 0) continue;

        /* Also try to get the Name property (DC44) which is the display name */
        uint8_t *name_data = NULL;
        uint32_t name_len = 0;
        char *name = NULL;
        if (mtp_get_object_prop_value(&dev->ptp, id, MTP_OPC_Name,
                                      &name_data, &name_len) == 0 &&
            name_data && name_len > 0) {
            name = mtp_ucs2_to_string(name_data, name_len);
            free(name_data);
        }

        if (!name || !name[0]) {
            free(name);
            name = strdup(info.filename ? info.filename : "Untitled");
        }

        playlists[valid].playlist_id = id;
        playlists[valid].name = name;

        /* Get track references */
        uint32_t *refs = NULL;
        int ref_count = 0;
        if (mtp_get_object_references(&dev->ptp, id, &refs, &ref_count) == 0 &&
            ref_count > 0 && refs) {
            playlists[valid].track_count = ref_count;
            playlists[valid].track_ids = refs;
        } else {
            playlists[valid].track_count = 0;
            playlists[valid].track_ids = NULL;
            free(refs);
        }

        mtp_free_object_info(&info);
        valid++;
    }

    free(handles);

    if (valid == 0) {
        free(playlists);
        return NULL;
    }

    *count = valid;
    return playlists;
}

/* ---- Free playlists ---- */

void zune_free_playlists(ZunePlaylist *playlists, int count) {
    if (!playlists) return;
    for (int i = 0; i < count; i++) {
        free(playlists[i].name);
        free(playlists[i].track_ids);
    }
    free(playlists);
}

/* ---- Create playlist ----
 *
 * Phase 9 research (2026-08-30): the original SendObjectInfo flow
 * created a handle the device DISCARDED at re-index (next session:
 * 0x2009 InvalidObjectHandle) — the same legacy-path trap the album
 * forge hit. Rebuilt on the album template: SendObjectPropList
 * (0x9808, the only creation op the official client ever uses for
 * media), declared size 0 with a zero-byte SendObject commit, then
 * SetObjectReferences. Format defaults to 0xBA05; ZUNE_PL_FMT
 * overrides for hardware experiments (e.g. 0xBA09/0xBA00).
 *
 * Hardware-verified 2026-08-30 on Keel (Zune 30) AND Pavo (Zune HD):
 * both survive finalize + re-index with refs intact, both advertise
 * the identical 15-prop list for 0xBA05, and both device UIs show the
 * playlist. Draco untested but the prop surface matches family-wide.
 */

uint32_t zune_forge_playlist(ZuneDevice *dev, const char *name,
                               uint32_t *track_ids, int count) {
    if (!dev || !name) {
        zune_set_error("Invalid device or playlist name");
        return 0;
    }

    uint16_t fmt = MTP_OFC_AbstractAudioVideoPlaylist;
    const char *fmt_env = getenv("ZUNE_PL_FMT");
    if (fmt_env && fmt_env[0])
        fmt = (uint16_t)strtoul(fmt_env, NULL, 0);

    /* Query supported props for the playlist format (album pattern) */
    uint16_t *supported = NULL;
    int sup_count = 0;
    if (mtp_get_props_supported(&dev->ptp, fmt, &supported, &sup_count) == 0) {
        fprintf(stderr, "[libzune] playlist (0x%04X) supported props (%d):",
                fmt, sup_count);
        for (int i = 0; i < sup_count; i++)
            fprintf(stderr, " 0x%04X", supported[i]);
        fprintf(stderr, "\n");
    }

    size_t namelen = strlen(name);
    char *filename = malloc(namelen + 5);
    if (!filename) { free(supported); return 0; }
    snprintf(filename, namelen + 5, "%s.zpl", name);

    mtp_prop_entry_t props[4];
    memset(props, 0, sizeof(props));
    int n = 0;
    props[n].prop_code = MTP_OPC_ObjectFileName;
    props[n].data_type = PTP_DTC_STR;
    props[n].value.str = filename;
    n++;
    if (!supported || pl_prop_supported(MTP_OPC_Name, supported, sup_count)) {
        props[n].prop_code = MTP_OPC_Name;
        props[n].data_type = PTP_DTC_STR;
        props[n].value.str = (char *)name;
        n++;
    }
    free(supported);

    uint32_t new_handle = 0;
    int ret = mtp_send_object_prop_list(&dev->ptp, dev->storage_id, 0,
                                        fmt, 0, props, n, &new_handle);
    free(filename);
    if (ret != 0 || new_handle == 0) {
        zune_set_error("Failed to create playlist: %s", name);
        return 0;
    }

    /* Zero-byte commit matching the declared size (album-forge rule:
     * a fake 1-byte body contradicts the declaration and the device
     * refuses or later discards the object). */
    mtp_send_object(&dev->ptp, NULL, 0);

    /* Track references */
    if (count > 0 && track_ids) {
        ret = mtp_set_object_references(&dev->ptp, new_handle, track_ids, count);
        if (ret != 0)
            zune_set_error("Failed to set tracks for playlist '%s'", name);
    }

    fprintf(stderr,
            "[libzune] created playlist '%s' (id=0x%08X, fmt=0x%04X, %d tracks)\n",
            name, new_handle, fmt, count);
    return new_handle;
}

/* ---- Update playlist ---- */

int zune_rewire_playlist(ZuneDevice *dev, uint32_t playlist_id,
                          const char *name,
                          uint32_t *track_ids, int count) {
    if (!dev) {
        zune_set_error("Invalid device");
        return -1;
    }

    /* Update name if provided */
    if (name) {
        int ret = mtp_set_object_prop_value_str(&dev->ptp, playlist_id,
                                                MTP_OPC_Name, name);
        if (ret != 0) {
            zune_set_error("Failed to update playlist name for %u", playlist_id);
            return -1;
        }
    }

    /* Update track references */
    int ret = mtp_set_object_references(&dev->ptp, playlist_id,
                                        track_ids, count);
    if (ret != 0) {
        zune_set_error("Failed to update playlist %u", playlist_id);
        return -1;
    }
    return 0;
}

/* ---- Delete playlist ---- */

int zune_purge_playlist(ZuneDevice *dev, uint32_t playlist_id) {
    if (!dev) {
        zune_set_error("Invalid device");
        return -1;
    }

    int ret = mtp_delete_object(&dev->ptp, playlist_id);
    if (ret != 0) {
        zune_set_error("Failed to delete playlist %u", playlist_id);
        return -1;
    }
    return 0;
}
