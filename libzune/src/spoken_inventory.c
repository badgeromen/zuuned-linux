/* Native spoken-media browsing. Keel/Pavo/Draco use podcast schema 0x10
 * and audiobook schema 0x12; exact property validation remains mandatory.
 * Wildcard enumeration is essential: Pavo omits book parts from MP3 queries.
 * No mutation, fallback creation, title-based grouping or partial publication. */
#include "zune_internal.h"
#include "mtp_text.h"

static int spoken_ready(ZuneDevice *d)
{
    if (!d || !d->mtpz_authenticated || d->disconnecting || d->cancel_requested) {
        zune_set_error("Spoken inventory requires an authenticated, active device");
        return 0;
    }
    if (d->device_family != ZUNE_FAMILY_KEEL && d->device_family != ZUNE_FAMILY_PAVO &&
        d->device_family != ZUNE_FAMILY_DRACO) {
        zune_set_error("Native spoken inventory requires Zune 30, 80/120 or HD");
        return 0;
    }
    return 1;
}

static int spoken_compare_id(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int spoken_handles(ZuneDevice *d, uint32_t **ids, int *count)
{
    *ids = NULL; *count = 0;
    if (!spoken_ready(d)) return 0;
    if (mtp_get_object_handles(&d->ptp, d->storage_id, 0, 0, ids, count) ||
        *count < 0 || *count > 100000 || (*count && !*ids)) goto fail;
    if (*count > 1) qsort(*ids, (size_t)*count, sizeof(**ids), spoken_compare_id);
    for (int i = 0; i < *count; ++i)
        if (!(*ids)[i] || (*ids)[i] == UINT32_MAX || (i && (*ids)[i] == (*ids)[i-1])) goto fail;
    return 1;
fail:
    free(*ids); *ids = NULL; *count = 0;
    zune_set_error("Incomplete or invalid spoken-media inventory");
    return 0;
}

static char *spoken_text(ZuneDevice *d, uint32_t id, uint16_t prop)
{
    if (!spoken_ready(d)) return NULL;
    uint8_t *raw = NULL; uint32_t n = 0; char *out = NULL;
    if (!mtp_get_object_prop_value(&d->ptp, id, prop, &raw, &n) && raw) {
        if (n == 1 && !raw[0]) out = strdup("");
        else if (n >= 3 && raw[0] && n == 1u + 2u * raw[0] && !raw[n-1] && !raw[n-2]) {
            /* Reuse the strict Unicode decoder, including embedded NUL and
             * surrogate checks, rather than silently truncating malformed STR. */
            uint8_t array[514] = {0};
            array[0] = raw[0];
            memcpy(array + 4, raw + 1, n - 1);
            out = mtp_array_to_text(array, n + 3);
        }
    }
    free(raw);
    if (!out) zune_set_error("Unreadable spoken text item %u property 0x%04X", id, prop);
    return out;
}

static int spoken_number(ZuneDevice *d, uint32_t id, uint16_t prop, uint32_t width, uint32_t *out)
{
    if (!spoken_ready(d)) return 0;
    uint8_t *raw = NULL; uint32_t n = 0; *out = 0;
    int ok = !mtp_get_object_prop_value(&d->ptp, id, prop, &raw, &n) && raw && n == width;
    if (ok) for (uint32_t i = 0; i < width; ++i) *out |= (uint32_t)raw[i] << (8*i);
    free(raw);
    if (!ok) zune_set_error("Unreadable spoken number item %u property 0x%04X", id, prop);
    return ok;
}

static int spoken_info(ZuneDevice *d, uint32_t id, uint32_t *parent, uint64_t *size)
{
    if (!spoken_ready(d)) return 0;
    mtp_object_info_t info = {0};
    int ok = !mtp_get_object_info(&d->ptp, id, &info);
    if (ok) {
        *parent = info.parent_object; *size = info.object_compressed_size;
        ok = *parent && *parent != UINT32_MAX && info.object_format != MTP_OFC_Association;
    }
    mtp_free_object_info(&info);
    if (!ok) zune_set_error("Unreadable spoken object info for item %u", id);
    return ok;
}

void zune_free_podcast_episodes(ZunePodcastEpisode *rows, int count)
{
    if (!rows) return;
    for (int i = 0; i < count; ++i) {
        free(rows[i].title); free(rows[i].author); free(rows[i].series_name);
    }
    free(rows);
}

int zune_get_podcast_episodes(ZuneDevice *d, ZunePodcastEpisode **out, int *count)
{
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!out || !count) { zune_set_error("Podcast inventory requires both outputs"); return -1; }
    uint32_t *ids = NULL; int n = 0, used = 0, relevant = 0;
    if (!spoken_handles(d, &ids, &n)) return -1;
    for (int i = 0; i < n; ++i) relevant += (ids[i] >> 24) == 0x10;
    ZunePodcastEpisode *rows = relevant ? calloc((size_t)relevant, sizeof(*rows)) : NULL;
    if (relevant && !rows) { free(ids); zune_set_error("Cannot allocate podcast inventory"); return -1; }
    for (int i = 0; i < n; ++i) {
        if ((ids[i] >> 24) != 0x10) continue;
        ZunePodcastEpisode *r = &rows[used++]; uint32_t category = 0;
        r->item_id = ids[i];
        if (!spoken_info(d, r->item_id, &r->parent_id, &r->filesize) ||
            !spoken_number(d, r->item_id, 0xDA9E, 4, &r->series_id) ||
            !r->series_id || r->series_id == UINT32_MAX ||
            !spoken_number(d, r->item_id, MTP_OPC_MetaGenre, 2, &category) ||
            (category != 0 && category != 64) ||
            !spoken_number(d, r->item_id, MTP_OPC_Duration, 4, &r->duration_ms)) goto fail;
        if (!(r->title = spoken_text(d, r->item_id, MTP_OPC_Name)) ||
            !(r->author = spoken_text(d, r->item_id, MTP_OPC_Artist)) ||
            !(r->series_name = spoken_text(d, r->item_id, ZUNE_OPC_SeriesName))) goto fail;
    }
    if (!spoken_ready(d)) goto fail;
    free(ids); *out = rows; *count = used; return 0;
fail:
    free(ids); zune_free_podcast_episodes(rows, used);
    zune_set_error("Podcast inventory is incomplete or contains invalid metadata");
    return -1;
}

void zune_free_audiobook_parts(ZuneAudiobookPart *rows, int count)
{
    if (!rows) return;
    for (int i = 0; i < count; ++i) {
        free(rows[i].title); free(rows[i].author); free(rows[i].book_title);
    }
    free(rows);
}

int zune_get_audiobook_parts(ZuneDevice *d, ZuneAudiobookPart **out, int *count)
{
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!out || !count) { zune_set_error("Audiobook inventory requires both outputs"); return -1; }
    uint32_t *ids = NULL; int n = 0, used = 0, relevant = 0;
    if (!spoken_handles(d, &ids, &n)) return -1;
    for (int i = 0; i < n; ++i) relevant += (ids[i] >> 24) == 0x12;
    ZuneAudiobookPart *rows = relevant ? calloc((size_t)relevant, sizeof(*rows)) : NULL;
    if (relevant && !rows) { free(ids); zune_set_error("Cannot allocate audiobook inventory"); return -1; }
    for (int i = 0; i < n; ++i) {
        if ((ids[i] >> 24) != 0x12) continue;
        ZuneAudiobookPart *r = &rows[used++]; uint32_t category = 0;
        r->item_id = ids[i];
        if (!spoken_info(d, r->item_id, &r->parent_id, &r->filesize) ||
            !spoken_number(d, r->item_id, 0xDC8B, 2, &r->part_number) ||
            !spoken_number(d, r->item_id, MTP_OPC_MetaGenre, 2, &category) ||
            (category != 0 && category != 18) ||
            !spoken_number(d, r->item_id, MTP_OPC_Duration, 4, &r->duration_ms)) goto fail;
        if (!(r->title = spoken_text(d, r->item_id, MTP_OPC_Name)) ||
            !(r->author = spoken_text(d, r->item_id, MTP_OPC_Artist)) ||
            !(r->book_title = spoken_text(d, r->item_id, 0xDA9A))) goto fail;
    }
    if (!spoken_ready(d)) goto fail;
    free(ids); *out = rows; *count = used; return 0;
fail:
    free(ids); zune_free_audiobook_parts(rows, used);
    zune_set_error("Audiobook inventory is incomplete or contains invalid metadata");
    return -1;
}
