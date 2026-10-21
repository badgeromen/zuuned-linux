/* Experimental podcast objects. Property types are corroborated by our
 * Windows captures; the creation sequence is referenced in SPOKEN_MEDIA_RESEARCH.
 * Every operation is synchronous and must share the caller's device serializer. */
#include "zune_internal.h"
#include "mtp_text.h"
#include <fcntl.h>

#define PODCAST_SERIES 0xba0b
#define IS_PODCAST 0xda9c
#define SERIES_FLAG 0xda9d
#define SERIES_HANDLE 0xda9e
#define SOURCE_URL 0xdd60
#define EPISODE_FLAG 0xdd62

static int podcast_ready(ZuneDevice *dev)
{
    if (!dev || ZUNE_FLAG_GET(dev->disconnecting) || ZUNE_FLAG_GET(dev->cancel_requested)) {
        zune_set_error("Podcast operation cancelled or device unavailable"); return 0;
    }
    return 1;
}

static int podcast_text_valid(const char *text, int required, int short_text)
{
    if (!text) return !required;
    if (required && !*text) return 0;
    uint32_t size = 0;
    uint8_t *wire = mtp_text_to_array(text, &size);
    if (!wire) return 0;
    free(wire);
    if (short_text) {
        /* Preserve the existing STR codec contract, but never silently lose
         * identity through its truncation or non-BMP replacement behavior. */
        if (size > 4 + 255 * 2) return 0;
        wire = mtp_string_to_ucs2(text, &size);
        char *decoded = wire ? mtp_ucs2_to_string(wire, size) : NULL;
        int same = decoded && strcmp(text, decoded) == 0;
        free(decoded); free(wire);
        return same;
    }
    return 1;
}

static int podcast_filename_valid(const char *name, const char *suffix)
{
    if (!podcast_text_valid(name, 1, 1)) return 0;
    size_t n = strlen(name), end = strlen(suffix);
    if (n <= end || n > 240 || strcasecmp(name+n-end, suffix)) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32 || strchr("/\\:*?\"<>|", *p)) return 0;
    return name[0] != '.' && name[n-1] != ' ';
}

static char *podcast_read_text(ZuneDevice *dev, uint32_t id, uint16_t prop, int array)
{
    uint8_t *wire = NULL; uint32_t size = 0;
    int ret = mtp_get_object_prop_value(&dev->ptp, id, prop, &wire, &size);
    char *text = NULL;
    if (ret == 0 && wire) {
        if (array) text = mtp_array_to_text(wire, size);
        else if (size == 1 && wire[0] == 0) text = strdup("");
        else if (size >= 3 && wire[0] && size == 1u+wire[0]*2u &&
                 wire[size-1] == 0 && wire[size-2] == 0) {
            uint8_t converted[512] = {0};
            converted[0] = wire[0]-1;
            memcpy(converted+4, wire+1, size-3);
            text = mtp_array_to_text(converted, 4+size-3);
        }
    }
    if (!text) {
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "unreadable text item=%u property=0x%04X array=%d bytes=%u", id, prop, array, size);
        if (array && wire && size >= 4) {
            uint32_t units = zune_le32(wire);
            unsigned trailing = 0;
            for (uint32_t pos = size; pos >= 6 && wire[pos-1] == 0 && wire[pos-2] == 0; pos -= 2) ++trailing;
            ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "text array declared-units=%u trailing-zero-units=%u", units, trailing);
        }
    }
    free(wire);
    if (!text) zune_set_error("Could not read podcast property 0x%04X on item %u", prop, id);
    return text;
}

void zune_free_podcast_series(ZunePodcastSeries *series, int count)
{
    if (!series) return;
    for (int i = 0; i < count; ++i) {
        free(series[i].name); free(series[i].author); free(series[i].feed_url);
    }
    free(series);
}

int zune_get_podcast_series(ZuneDevice *dev, ZunePodcastSeries **out, int *count)
{
    zune_clear_error();
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!out || !count || !podcast_ready(dev)) return ZUNE_BLOWN;
    if (!dev->mtpz_authenticated) { zune_set_error("Podcast inventory requires successful MTPZ authentication"); return ZUNE_BLOWN; }
    uint32_t *ids = NULL; int n = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, PODCAST_SERIES, 0, &ids, &n) != 0) {
        free(ids); zune_set_error("Podcast series inventory failed"); return ZUNE_BLOWN;
    }
    if (n < 0 || n > 100000 || (n && !ids)) { free(ids); zune_set_error("Invalid podcast inventory size"); return ZUNE_BLOWN; }
    if (!n) { free(ids); return ZUNE_OK; }
    ZunePodcastSeries *series = calloc((size_t)n, sizeof(*series));
    if (!series) { free(ids); zune_set_error("Cannot allocate podcast inventory"); return ZUNE_BLOWN; }
    for (int i = 0; i < n; ++i) {
        if (!podcast_ready(dev)) goto fail;
        series[i].item_id = ids[i];
        series[i].name = podcast_read_text(dev, ids[i], MTP_OPC_Name, 0);
        if (!series[i].name) goto fail;
        series[i].author = podcast_read_text(dev, ids[i], MTP_OPC_Artist, 0);
        if (!series[i].author) goto fail;
        series[i].feed_url = podcast_read_text(dev, ids[i], SOURCE_URL, 1);
        if (!series[i].feed_url) goto fail;
    }
    free(ids); *out = series; *count = n; return ZUNE_OK;
fail:
    free(ids); zune_free_podcast_series(series, n); return ZUNE_BLOWN;
}

static int podcast_preflight(ZuneDevice *dev, uint16_t format, mtp_prop_entry_t *props, int count)
{
    uint16_t *supported = NULL; int n = 0;
    if (mtp_get_props_supported(&dev->ptp, format, &supported, &n) != 0) {
        free(supported); zune_set_error("Podcast capability query failed for 0x%04X", format); return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (!podcast_ready(dev)) { free(supported); return -1; }
        if (!zune_prop_listed(props[i].prop_code, supported, n)) {
            free(supported); zune_set_error("Podcast property 0x%04X is not advertised", props[i].prop_code); return -1;
        }
        if (!mtp_prop_desc_writable(&dev->ptp, props[i].prop_code, format, props[i].data_type)) {
            free(supported); zune_set_error("Podcast property 0x%04X has unavailable or incompatible descriptor", props[i].prop_code); return -1;
        }
    }
    free(supported); return 0;
}

static int podcast_date_readback(const char *expected, const char *actual)
{
    if (!expected || !actual || strlen(expected) < 15 || strlen(actual) < 15 || strncmp(expected, actual, 15)) return 0;
    /* Keel returns whole-second MTP timestamps with .0 and without the Z.
     * Never accept a changed date/time, nonzero fractional part or offset. */
    const char *tail = actual + 15;
    if (*tail == '.') {
        ++tail;
        if (*tail != '0') return 0;
        while (*tail == '0') ++tail;
    }
    if (*tail == 'Z') ++tail;
    return *tail == 0;
}

/* Native podcast filenames are device metadata, not host filesystem paths.
 * Keel returns Name + ".mp3", including punctuation forbidden in wire names.
 * Accept that exact derivation only; all other identity/size checks still run. */
static int podcast_title_filename(const char *value, const char *title)
{
    if (!value || !title || !podcast_text_valid(value, 1, 1)) return 0;
    size_t n = strlen(title);
    if (strlen(value) != n + 4 || memcmp(value, title, n) || strcmp(value + n, ".mp3")) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
        if (*p < 32 || *p == 127) return 0;
    return 1;
}

/* Bounded, escaped metadata only. Never print source URLs or auth material. */
static void podcast_log_filename(const char *value)
{
    /* 256 source bytes, each at most "\xNN", plus "..." */
    char text[256 * 4 + 8];
    size_t used = 0;
    if (value) {
        size_t i = 0;
        for (; value[i] && i < 256; ++i) {
            unsigned char c = (unsigned char)value[i];
            if (c >= 32 && c < 127 && c != '\\' && c != '\"') text[used++] = (char)c;
            else used += (size_t)snprintf(text + used, sizeof(text) - used, "\\x%02X", c);
        }
        if (value[i]) { memcpy(text + used, "...", 3); used += 3; }
        text[used] = '\0';
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "returned filename=\"%s\"", text);
    } else {
        ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "returned filename=\"<unreadable>\"");
    }
}

static int podcast_verify_properties(ZuneDevice *dev, uint32_t id, mtp_prop_entry_t *props, int count,
                                       int allow_audio_filename)
{
    int mismatches = 0;
    uint16_t first_mismatch = 0;
    /* On Keel, Pavo and Draco, the firmware's returned atom handle carries the record schema.
     * Audio podcast schema 0x10 is authoritative even though MetaGenre reads 0
     * and the redundant SeriesName reads empty. The series handle and URL are
     * still checked exactly, as are format/size outside this helper. */
    int native_episode = (dev->device_family == ZUNE_FAMILY_KEEL ||
                          dev->device_family == ZUNE_FAMILY_PAVO ||
                          dev->device_family == ZUNE_FAMILY_DRACO) && (id >> 24) == 0x10;
    const char *title = NULL;
    for (int i = 0; i < count; ++i)
        if (props[i].prop_code == MTP_OPC_Name && props[i].data_type == PTP_DTC_STR)
            title = props[i].value.str;
    for (int i = 0; i < count; ++i) {
        if (!podcast_ready(dev)) return -1;
        mtp_prop_entry_t *p = &props[i];
        if (p->data_type == PTP_DTC_STR || p->data_type == PTP_DTC_AUINT16) {
            char *value = podcast_read_text(dev, id, p->prop_code, p->data_type == PTP_DTC_AUINT16);
            int same = value && !strcmp(value, p->value.str ? p->value.str : "");
            /* Wire basenames remain strict. Native readback may instead be
             * the exact title-derived metadata observed on the firmware. */
            if (allow_audio_filename && p->prop_code == MTP_OPC_ObjectFileName)
                same = podcast_filename_valid(value, ".mp3") ||
                    (native_episode && podcast_title_filename(value, title));
            if (native_episode && p->prop_code == ZUNE_OPC_SeriesName && value && !*value) same = 1;
            if (p->prop_code == 0xdc47 && value) same = podcast_date_readback(p->value.str, value);
            if (!same) ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "text mismatch item=%u property=0x%04X expected-bytes=%zu actual-bytes=%zu",
                               id, p->prop_code, p->value.str ? strlen(p->value.str) : 0, value ? strlen(value) : 0);
            if (!same && value && p->value.str) {
                size_t at = 0;
                while (value[at] && p->value.str[at] && value[at] == p->value.str[at]) ++at;
                ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "first-difference=%zu expected-byte=%u actual-byte=%u",
                        at, (unsigned char)p->value.str[at], (unsigned char)value[at]);
            }
            if (!same && p->prop_code == MTP_OPC_ObjectFileName) podcast_log_filename(value);
            free(value);
            if (!same) goto mismatch;
        } else {
            uint8_t *data = NULL; uint32_t size = 0;
            unsigned expected_size = p->data_type == PTP_DTC_UINT8 ? 1 : p->data_type == PTP_DTC_UINT16 ? 2 : 4;
            uint32_t expected = expected_size == 1 ? p->value.u8 : expected_size == 2 ? p->value.u16 : p->value.u32;
            int result = mtp_get_object_prop_value(&dev->ptp, id, p->prop_code, &data, &size);
            uint32_t value = 0;
            if (result == 0 && data && size == expected_size)
                for (unsigned j = 0; j < expected_size; ++j) value |= (uint32_t)data[j] << (8*j);
            int same = result == 0 && data && size == expected_size && value == expected;
            if (native_episode && p->prop_code == MTP_OPC_MetaGenre && expected == 64 &&
                result == 0 && data && size == 2 && value == 0) same = 1;
            if (!same) ZLOG(ZUNE_CHATTER_SHOUT, "libzune-podcast", "numeric mismatch item=%u property=0x%04X expected=%u actual=%u bytes=%u",
                               id, p->prop_code, expected, value, size);
            free(data);
            if (!same) goto mismatch;
        }
        continue;
/* label deliberately inside the loop to retain the failing property */
mismatch:
        if (!mismatches) first_mismatch = p->prop_code;
        ++mismatches;
        /* Read the remaining fields on a responsive device to diagnose the
         * complete new object. A transport/protocol refusal stops immediately. */
        if (dev->ptp.last_response != PTP_RC_OK) break;
    }
    if (mismatches) {
        zune_set_error("Podcast readback mismatch on item %u property 0x%04X (%d mismatches)", id, first_mismatch, mismatches);
        return -1;
    }
    return 0;
}

static int podcast_cleanup(ZuneDevice *dev, uint32_t id, uint32_t *out, const char *stage)
{
    uint16_t original = dev->ptp.last_response;
    char detail[256];
    snprintf(detail, sizeof(detail), "%s", zune_get_error() ? zune_get_error() : "");
    int removed = mtp_delete_object(&dev->ptp, id) == 0;
    *out = removed ? 0 : id;
    dev->ptp.last_response = original;
    zune_set_error("Podcast %s failed (0x%04X, %s); new item %u %s", stage, original, detail, id,
                   removed ? "removed" : "remains; inspect before retrying");
    return removed ? ZUNE_PODCAST_FAILED_CLEANED : ZUNE_BLOWN;
}

int zune_forge_podcast_series(ZuneDevice *dev, uint32_t parent, const char *filename,
                             const char *name, const char *author, const char *feed, uint32_t *out)
{
    zune_clear_error();
    if (out) *out = 0;
    if (!out || !podcast_ready(dev)) return ZUNE_BLOWN;
    if (!dev->mtpz_authenticated) { zune_set_error("Podcast writes require successful MTPZ authentication"); return ZUNE_BLOWN; }
    if (!parent || !podcast_filename_valid(filename, ".ser") || !podcast_text_valid(name, 1, 1) ||
        !podcast_text_valid(author, 0, 1) || !podcast_text_valid(feed, 1, 0)) {
        zune_set_error("Invalid podcast series metadata or parent folder"); return ZUNE_BLOWN;
    }
    ZunePodcastSeries *series = NULL; int n = 0;
    if (zune_get_podcast_series(dev, &series, &n) != 0) return ZUNE_BLOWN;
    uint32_t match = 0; int matches = 0;
    for (int i = 0; i < n; ++i) if (!strcmp(series[i].feed_url, feed)) { match = series[i].item_id; ++matches; }
    zune_free_podcast_series(series, n);
    if (matches > 1) { zune_set_error("Multiple device podcast series match this feed; no writes performed"); return ZUNE_BLOWN; }
    if (matches == 1) { *out = match; return ZUNE_OK; }
    mtp_prop_entry_t props[] = {
        {.prop_code=IS_PODCAST, .data_type=PTP_DTC_UINT8, .value.u8=1},
        {.prop_code=SERIES_FLAG, .data_type=PTP_DTC_UINT32, .value.u32=1},
        {.prop_code=MTP_OPC_Artist, .data_type=PTP_DTC_STR, .value.str=(char *)(author ? author : "")},
        {.prop_code=MTP_OPC_ObjectFileName, .data_type=PTP_DTC_STR, .value.str=(char *)filename},
        {.prop_code=SOURCE_URL, .data_type=PTP_DTC_AUINT16, .value.str=(char *)feed},
        {.prop_code=MTP_OPC_Name, .data_type=PTP_DTC_STR, .value.str=(char *)name}
    };
    if (podcast_preflight(dev, PODCAST_SERIES, props, 6) != 0) return ZUNE_BLOWN;
    uint32_t id = 0;
    if (mtp_send_object_prop_list(&dev->ptp, dev->storage_id, parent, PODCAST_SERIES, 0, props, 6, &id) != 0 ||
        !id || id == UINT32_MAX) {
        zune_set_error("Podcast series creation failed or returned an invalid handle (0x%04X); inspect before retrying", dev->ptp.last_response); return ZUNE_BLOWN;
    }
    if (mtp_send_object(&dev->ptp, NULL, 0) != 0) return podcast_cleanup(dev, id, out, "series commit");
    if (podcast_verify_properties(dev, id, props, 6, 0) != 0) return podcast_cleanup(dev, id, out, "series readback");
    *out = id; return ZUNE_OK;
}

static int podcast_date_valid(const char *s)
{
    if (!s || !*s) return 1;
    size_t n = strlen(s);
    if (n != 15 && n != 16) return 0;
    if (s[8] != 'T' || (n == 16 && s[15] != 'Z')) return 0;
    for (size_t i = 0; i < 15; ++i) if (i != 8 && (s[i] < '0' || s[i] > '9')) return 0;
    unsigned year = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + s[3]-'0';
    unsigned month = (s[4]-'0')*10 + s[5]-'0', day = (s[6]-'0')*10 + s[7]-'0';
    unsigned hour = (s[9]-'0')*10 + s[10]-'0', minute = (s[11]-'0')*10 + s[12]-'0';
    unsigned second = (s[13]-'0')*10 + s[14]-'0';
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!year || month < 1 || month > 12 || !day || hour > 23 || minute > 59 || second > 59) return 0;
    unsigned maximum = days[month-1] + (month == 2 && !(year%4) && ((year%100) || !(year%400)));
    return day <= maximum;
}

int zune_smuggle_podcast_episode(ZuneDevice *dev, uint32_t parent, const char *path,
                                 const ZunePodcastEpisodeInput *ep, uint32_t *out)
{
    zune_clear_error();
    if (out) *out = 0;
    if (!out || !podcast_ready(dev)) return ZUNE_BLOWN;
    if (!dev->mtpz_authenticated) { zune_set_error("Podcast writes require successful MTPZ authentication"); return ZUNE_BLOWN; }
    struct stat st;
    if (!ep || !parent || !path || stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > UINT32_MAX || !ep->series_id ||
        !podcast_filename_valid(ep->filename, ".mp3") || !podcast_text_valid(ep->title, 1, 1) ||
        !podcast_text_valid(ep->author, 0, 1) || !podcast_text_valid(ep->series_name, 1, 1) ||
        !podcast_text_valid(ep->source_url, 1, 0) || !podcast_text_valid(ep->description, 0, 0) ||
        !podcast_date_valid(ep->date_authored)) {
        zune_set_error("Invalid podcast episode input; supply a prepared MP3 under 4 GiB"); return ZUNE_BLOWN;
    }
    /* Confirm the selected series still exists before making an episode. */
    ZunePodcastSeries *series = NULL; int series_count = 0, found = 0;
    if (zune_get_podcast_series(dev, &series, &series_count) != 0) return ZUNE_BLOWN;
    for (int i = 0; i < series_count; ++i)
        if (series[i].item_id == ep->series_id && !strcmp(series[i].name, ep->series_name)) found = 1;
    zune_free_podcast_series(series, series_count);
    if (!found) { zune_set_error("Selected podcast series no longer matches the device"); return ZUNE_BLOWN; }
    /* Exact URL + series reuse within the caller's episode folder. Never
     * collapse same-title episodes. Unreadable inventory blocks creation. */
    uint32_t *ids = NULL; int n = 0, matches = 0; uint32_t match = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_MP3, parent, &ids, &n) != 0) {
        free(ids); zune_set_error("Podcast episode inventory failed"); return ZUNE_BLOWN;
    }
    if (n < 0 || n > 100000 || (n && !ids)) { free(ids); zune_set_error("Invalid episode inventory size"); return ZUNE_BLOWN; }
    for (int i = 0; i < n; ++i) {
        if (!podcast_ready(dev)) { free(ids); return ZUNE_BLOWN; }
        uint8_t *data = NULL; uint32_t size = 0;
        int r = mtp_get_object_prop_value(&dev->ptp, ids[i], SERIES_HANDLE, &data, &size);
        if (r || !data || size != 4) { free(data); free(ids); zune_set_error("Unreadable episode series link"); return ZUNE_BLOWN; }
        uint32_t series_id = zune_le32(data);
        free(data);
        if (series_id != ep->series_id) continue;
        char *url = podcast_read_text(dev, ids[i], SOURCE_URL, 1);
        if (!url) { free(ids); return ZUNE_BLOWN; }
        if (!strcmp(url, ep->source_url)) { match = ids[i]; ++matches; }
        free(url);
    }
    free(ids);
    if (matches > 1) { zune_set_error("Multiple device episodes match this series and URL"); return ZUNE_BLOWN; }
    if (matches == 1) {
        mtp_object_info_t info = {0};
        int r = mtp_get_object_info(&dev->ptp, match, &info);
        int same_size = !r && info.object_format == MTP_OFC_MP3 && info.object_compressed_size == (uint32_t)st.st_size;
        mtp_free_object_info(&info);
        mtp_prop_entry_t verify[] = {
            {.prop_code=MTP_OPC_Name,.data_type=PTP_DTC_STR,.value.str=(char *)ep->title},
            {.prop_code=MTP_OPC_MetaGenre,.data_type=PTP_DTC_UINT16,.value.u16=64}
        };
        if (!same_size || podcast_verify_properties(dev, match, verify, 2, 0) != 0) {
            zune_set_error("Existing podcast episode conflicts with this source; no writes performed"); return ZUNE_BLOWN;
        }
        *out = match; return ZUNE_OK;
    }
    mtp_prop_entry_t props[] = {
        {.prop_code=SOURCE_URL,.data_type=PTP_DTC_AUINT16,.value.str=(char *)ep->source_url},
        {.prop_code=MTP_OPC_ObjectFileName,.data_type=PTP_DTC_STR,.value.str=(char *)ep->filename},
        {.prop_code=EPISODE_FLAG,.data_type=PTP_DTC_UINT32,.value.u32=0},
        {.prop_code=ZUNE_OPC_SeriesName,.data_type=PTP_DTC_STR,.value.str=(char *)ep->series_name},
        {.prop_code=0xda9b,.data_type=PTP_DTC_UINT8,.value.u8=0},
        {.prop_code=MTP_OPC_MetaGenre,.data_type=PTP_DTC_UINT16,.value.u16=64},
        {.prop_code=SERIES_HANDLE,.data_type=PTP_DTC_UINT32,.value.u32=ep->series_id},
        {.prop_code=MTP_OPC_Name,.data_type=PTP_DTC_STR,.value.str=(char *)ep->title},
        {.prop_code=MTP_OPC_Duration,.data_type=PTP_DTC_UINT32,.value.u32=ep->duration_ms},
        {.prop_code=MTP_OPC_Artist,.data_type=PTP_DTC_STR,.value.str=(char *)(ep->author ? ep->author : "")},
        {.prop_code=MTP_OPC_Description,.data_type=PTP_DTC_AUINT16,.value.str=(char *)(ep->description ? ep->description : "")},
        {.prop_code=0xdc47,.data_type=PTP_DTC_STR,.value.str=(char *)(ep->date_authored ? ep->date_authored : "")}
    };
    int count = ep->date_authored && *ep->date_authored ? 12 : 11;
    if (podcast_preflight(dev, MTP_OFC_MP3, props, count) != 0) return ZUNE_BLOWN;
    /* Strict: an unanswered creation is never retried, so a lost response
     * cannot produce a second episode. */
    zune_upload_t up = { .kind = "podcast episode", .path = path, .size = (uint32_t)st.st_size,
                         .parent = parent, .format = MTP_OFC_MP3, .props = props, .nprops = count,
                         .strict = 1 };
    uint32_t id = 0;
    int sent = zune_smuggle_core(dev, &up, &id);
    if (sent == ZUNE_UPLOAD_REMOVED) {
        char detail[512];
        snprintf(detail, sizeof(detail), "%s", zune_get_error() ? zune_get_error() : "");
        zune_set_error("%s; the new episode was removed", detail);
        return ZUNE_PODCAST_FAILED_CLEANED;
    }
    if (sent == ZUNE_UPLOAD_ORPHANED) { *out = id; return ZUNE_BLOWN; }   /* inspect before retrying */
    if (sent != ZUNE_UPLOAD_SENT) return ZUNE_BLOWN;
    if (podcast_verify_properties(dev, id, props, count, 1) != 0) return podcast_cleanup(dev, id, out, "episode readback");
    mtp_object_info_t info = {0};
    int r = mtp_get_object_info(&dev->ptp, id, &info);
    int correct = !r && info.object_format == MTP_OFC_MP3 && info.object_compressed_size == (uint32_t)st.st_size;
    mtp_free_object_info(&info);
    if (!correct) return podcast_cleanup(dev, id, out, "episode size readback");
    *out = id; return ZUNE_OK;
}

int zune_find_podcast_episode(ZuneDevice *dev, const char *feed, const char *url,
                              uint32_t *parent, uint32_t *show, uint32_t *episode)
{
    zune_clear_error();
    if (parent) *parent = 0;
    if (show) *show = 0;
    if (episode) *episode = 0;
    if (!parent || !show || !episode || !podcast_ready(dev) ||
        !podcast_text_valid(feed, 1, 0) || !podcast_text_valid(url, 1, 0)) return ZUNE_BLOWN;
    ZunePodcastSeries *series = NULL;
    int count = 0, matches = 0;
    uint32_t sid = 0, folder = 0, eid = 0;
    if (zune_get_podcast_series(dev, &series, &count)) return ZUNE_BLOWN;
    for (int i = 0; i < count; ++i)
        if (!strcmp(series[i].feed_url, feed)) { sid = series[i].item_id; ++matches; }
    zune_free_podcast_series(series, count);
    if (matches > 1) { zune_set_error("Multiple podcast series match this feed"); return ZUNE_BLOWN; }
    if (!sid) return ZUNE_OK;
    mtp_object_info_t info = {0};
    int ret = mtp_get_object_info(&dev->ptp, sid, &info);
    folder = info.parent_object;
    int valid = !ret && info.object_format == PODCAST_SERIES && folder && folder != UINT32_MAX;
    mtp_free_object_info(&info);
    if (!valid) { zune_set_error("Cannot establish podcast series folder"); return ZUNE_BLOWN; }
    uint32_t *ids = NULL;
    int n = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_MP3, folder, &ids, &n) ||
        n < 0 || n > 100000 || (n && !ids)) {
        free(ids); zune_set_error("Podcast episode inventory failed"); return ZUNE_BLOWN;
    }
    matches = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t *data = NULL; uint32_t size = 0;
        if (!podcast_ready(dev)) { free(ids); return ZUNE_BLOWN; }
        ret = mtp_get_object_prop_value(&dev->ptp, ids[i], SERIES_HANDLE, &data, &size);
        if (ret || !data || size != 4) {
            free(data); free(ids); zune_set_error("Unreadable podcast series link"); return ZUNE_BLOWN;
        }
        uint32_t linked = zune_le32(data);
        free(data);
        if (linked != sid) continue;
        char *source = podcast_read_text(dev, ids[i], SOURCE_URL, 1);
        if (!source) { free(ids); return ZUNE_BLOWN; }
        int same = !strcmp(source, url);
        free(source);
        if (!same) continue;
        mtp_prop_entry_t classification = {.prop_code=MTP_OPC_MetaGenre,
            .data_type=PTP_DTC_UINT16, .value.u16=64};
        if (podcast_verify_properties(dev, ids[i], &classification, 1, 0)) { free(ids); return ZUNE_BLOWN; }
        eid = ids[i]; ++matches;
    }
    free(ids);
    if (matches > 1) { zune_set_error("Multiple podcast episodes match this source"); return ZUNE_BLOWN; }
    *parent = folder; *show = sid; *episode = eid;
    return ZUNE_OK;
}

static int podcast_series_by_id(const void *a, const void *b)
{
    uint32_t x = ((const ZunePodcastSeries *)a)->item_id;
    uint32_t y = ((const ZunePodcastSeries *)b)->item_id;
    return (x > y) - (x < y);
}
static int podcast_series_by_feed(const void *a, const void *b)
{
    return strcmp(((const ZunePodcastSeries *)a)->feed_url,
                  ((const ZunePodcastSeries *)b)->feed_url);
}
static int podcast_source_by_identity(const void *a, const void *b)
{
    const ZunePodcastEpisodeSource *x = a, *y = b;
    int order = strcmp(x->feed_url, y->feed_url);
    return order ? order : strcmp(x->source_url, y->source_url);
}
static int podcast_source_by_id(const void *a, const void *b)
{
    uint32_t x = ((const ZunePodcastEpisodeSource *)a)->item_id;
    uint32_t y = ((const ZunePodcastEpisodeSource *)b)->item_id;
    return (x > y) - (x < y);
}
static int podcast_u32_order(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}
void zune_free_podcast_episode_sources(ZunePodcastEpisodeSource *sources, int count)
{
    if (!sources) return;
    for (int i = 0; i < count; ++i) { free(sources[i].feed_url); free(sources[i].source_url); }
    free(sources);
}

int zune_get_podcast_episode_sources(ZuneDeviceHandle dev, ZunePodcastEpisodeSource **out, int *count)
{
    zune_clear_error();
    ZunePodcastSeries *series = NULL;
    ZunePodcastEpisodeSource *result = NULL;
    uint32_t *parents = NULL, *folders = NULL, *ids = NULL;
    int series_count = 0, written = 0, capacity = 0;
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!out || !count || !podcast_ready(dev) || !dev->mtpz_authenticated ||
        (dev->device_family != ZUNE_FAMILY_KEEL && dev->device_family != ZUNE_FAMILY_PAVO &&
         dev->device_family != ZUNE_FAMILY_DRACO)) {
        zune_set_error("Podcast source inventory requires an authenticated active Zune 30, 80/120 or HD");
        return ZUNE_BLOWN;
    }
#define SOURCE_CHECK(ok, why) do { if (!(ok)) { zune_set_error("%s", why); goto fail; } } while (0)
    if (zune_get_podcast_series(dev, &series, &series_count)) return ZUNE_BLOWN;
    if (!series_count) return ZUNE_OK;
    for (int i = 0; i < series_count; ++i)
        SOURCE_CHECK(series[i].item_id && series[i].item_id != UINT32_MAX &&
                     series[i].feed_url && *series[i].feed_url, "Invalid podcast series identity");
    qsort(series, (size_t)series_count, sizeof(*series), podcast_series_by_feed);
    for (int i = 1; i < series_count; ++i)
        SOURCE_CHECK(strcmp(series[i-1].feed_url, series[i].feed_url), "Duplicate podcast feed identity");
    qsort(series, (size_t)series_count, sizeof(*series), podcast_series_by_id);
    for (int i = 1; i < series_count; ++i)
        SOURCE_CHECK(series[i-1].item_id != series[i].item_id, "Duplicate podcast series handle");
    parents = calloc((size_t)series_count, sizeof(*parents));
    folders = calloc((size_t)series_count, sizeof(*folders));
    SOURCE_CHECK(parents && folders, "Cannot allocate podcast folder inventory");
    for (int i = 0; i < series_count; ++i) {
        if (!podcast_ready(dev)) goto fail;
        mtp_object_info_t info = {0};
        int rc = mtp_get_object_info(&dev->ptp, series[i].item_id, &info);
        int valid = !rc && info.object_format == PODCAST_SERIES &&
                    info.parent_object && info.parent_object != UINT32_MAX;
        parents[i] = folders[i] = info.parent_object;
        mtp_free_object_info(&info);
        SOURCE_CHECK(valid, "Cannot establish podcast series folder");
    }
    qsort(folders, (size_t)series_count, sizeof(*folders), podcast_u32_order);
    for (int f = 0; f < series_count; ++f) {
        if (f && folders[f] == folders[f-1]) continue;
        if (!podcast_ready(dev)) goto fail;
        int n = 0;
        SOURCE_CHECK(!mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_MP3,
                                            folders[f], &ids, &n) &&
                     n >= 0 && n <= 100000 && (!n || ids), "Podcast folder inventory failed");
        if (n > 1) qsort(ids, (size_t)n, sizeof(*ids), podcast_u32_order);
        for (int j = 0; j < n; ++j) {
            SOURCE_CHECK(ids[j] && ids[j] != UINT32_MAX && (!j || ids[j] != ids[j-1]),
                         "Duplicate or invalid podcast episode handle");
            if (!podcast_ready(dev)) goto fail;
            uint8_t *wire = NULL; uint32_t length = 0;
            int rc = mtp_get_object_prop_value(&dev->ptp, ids[j], SERIES_HANDLE, &wire, &length);
            uint32_t sid = 0;
            if (!rc && wire && length == 4)
                sid = zune_le32(wire);
            int valid = !rc && wire && length == 4;
            free(wire);
            SOURCE_CHECK(valid, "Unreadable podcast series link");
            ZunePodcastSeries key = {.item_id = sid};
            ZunePodcastSeries *show = bsearch(&key, series, (size_t)series_count,
                                             sizeof(*series), podcast_series_by_id);
            if (!show) continue; // Unrelated MP3, same rule as exact find.
            SOURCE_CHECK(parents[show-series] == folders[f], "Podcast series and episode folder disagree");
            mtp_object_info_t info = {0};
            rc = mtp_get_object_info(&dev->ptp, ids[j], &info);
            valid = !rc && info.object_format == MTP_OFC_MP3 && info.parent_object == folders[f];
            mtp_free_object_info(&info);
            SOURCE_CHECK(valid, "Podcast episode format or parent changed");
            mtp_prop_entry_t classification = {.prop_code = MTP_OPC_MetaGenre,
                .data_type = PTP_DTC_UINT16, .value.u16 = 64};
            if (podcast_verify_properties(dev, ids[j], &classification, 1, 0)) goto fail;
            SOURCE_CHECK(written < 100000, "Podcast source inventory too large");
            if (written == capacity) {
                int next_capacity = capacity ? capacity * 2 : 16;
                if (next_capacity > 100000) next_capacity = 100000;
                void *next = realloc(result, (size_t)next_capacity * sizeof(*result));
                SOURCE_CHECK(next, "Cannot allocate podcast source inventory");
                result = next; capacity = next_capacity;
            }
            ZunePodcastEpisodeSource *row = &result[written++];
            memset(row, 0, sizeof(*row));
            row->item_id = ids[j]; row->series_id = sid; row->parent_id = folders[f];
            row->feed_url = strdup(show->feed_url);
            row->source_url = podcast_read_text(dev, ids[j], SOURCE_URL, 1);
            SOURCE_CHECK(row->feed_url && row->source_url && *row->source_url,
                         "Unreadable or empty podcast source identity");
        }
        free(ids); ids = NULL;
    }
    if (written > 1) {
        qsort(result, (size_t)written, sizeof(*result), podcast_source_by_id);
        for (int i = 1; i < written; ++i)
            SOURCE_CHECK(result[i-1].item_id != result[i].item_id, "Duplicate podcast episode handle");
        qsort(result, (size_t)written, sizeof(*result), podcast_source_by_identity);
        for (int i = 1; i < written; ++i)
            SOURCE_CHECK(podcast_source_by_identity(&result[i-1], &result[i]), "Duplicate exact podcast source identity");
    }
    if (!podcast_ready(dev)) goto fail;
    free(parents); free(folders); zune_free_podcast_series(series, series_count);
    *out = result; *count = written;
    return ZUNE_OK;
fail:
    free(ids); free(parents); free(folders);
    zune_free_podcast_series(series, series_count);
    zune_free_podcast_episode_sources(result, written);
    return ZUNE_BLOWN;
#undef SOURCE_CHECK
}
