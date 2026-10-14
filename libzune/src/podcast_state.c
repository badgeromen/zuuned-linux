/* Persisted podcast listening state. Independently written from our bounded
 * ZMDB inspector and hardware comparisons. Field-location reference:
 * XuneSyncLibrary c2a6eebeac34d1a9e4c805b1ad51ec25895dfdc4,
 * ZuneClassicParser.cpp:545 / ZuneHDParser.cpp:511 / ZMDBTypes.h:218.
 * Reference repository is LGPL-2.1; no implementation code copied.
 * See docs/SPOKEN_MEDIA_RESEARCH.md for evidence and interpretation limits. */
#include "zune_internal.h"

#define STATE_MAX_BYTES (64u * 1024u * 1024u)
#define STATE_MAX_RECORDS 1000000u

static uint16_t state_u16(const uint8_t *p)
{ return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t state_u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct { uint32_t id, offset, size; } StateRecord;
static int state_by_id(const void *a, const void *b)
{
    const StateRecord *x = a, *y = b;
    return (x->id > y->id) - (x->id < y->id);
}
static int state_by_offset(const void *a, const void *b)
{
    const StateRecord *x = a, *y = b;
    return (x->offset > y->offset) - (x->offset < y->offset);
}

/* Validate the complete bounded title, including shortest encodings and Unicode
 * scalar limits. Preserve BOM; no normalization or title identity matching. */
static int state_utf8(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n;) {
        uint32_t cp = p[i++], minimum = 0;
        unsigned tail = 0;
        if (cp < 0x80) continue;
        if (cp >= 0xc2 && cp <= 0xdf) { tail = 1; minimum = 0x80; cp &= 0x1f; }
        else if (cp >= 0xe0 && cp <= 0xef) { tail = 2; minimum = 0x800; cp &= 0x0f; }
        else if (cp >= 0xf0 && cp <= 0xf4) { tail = 3; minimum = 0x10000; cp &= 7; }
        else return 0;
        if (tail > n - i) return 0;
        while (tail--) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (p[i++] & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    }
    return 1;
}

int zune_parse_podcast_states(const uint8_t *data, size_t length,
                             ZunePodcastState **states, int *count)
{
    StateRecord *records = NULL;
    ZunePodcastState *result = NULL;
    unsigned written = 0;
    const char *error = "Invalid podcast state snapshot";
    if (states) *states = NULL;
    if (count) *count = 0;
#define REQUIRE(test, why) do { if (!(test)) { error = why; goto fail; } } while (0)
    REQUIRE(states && count && data && length >= 0x30 && length <= STATE_MAX_BYTES,
            "Invalid podcast snapshot arguments or size");
    REQUIRE(!memcmp(data, "ZMDB", 4) && !memcmp(data + 0x20, "ZMed", 4),
            "Invalid podcast snapshot magic");
    uint16_t version = state_u16(data + 0x24);
    REQUIRE(version == 2 || version == 5, "Unsupported podcast snapshot version");
    size_t descriptor = 0x30;
    while (descriptor < 0x100 && descriptor + 4 <= length &&
           memcmp(data + descriptor, "ZArr", 4)) descriptor += 4;
    REQUIRE(descriptor < 0x100 && descriptor + 20 <= length &&
            !memcmp(data + descriptor, "ZArr", 4), "Missing podcast snapshot index descriptor");
    size_t descriptor_end = descriptor;
    unsigned descriptors = 0;
    while (descriptor_end + 4 <= length && !memcmp(data + descriptor_end, "ZArr", 4)) {
        REQUIRE(descriptor_end + 20 <= length && ++descriptors <= 256,
                "Truncated or excessive podcast snapshot descriptors");
        descriptor_end += 20;
    }
    REQUIRE(state_u16(data + descriptor + 6) == 8, "Invalid podcast index entry size");
    uint32_t entries = state_u32(data + descriptor + 8);
    size_t index = state_u32(data + descriptor + 16);
    REQUIRE(entries <= STATE_MAX_RECORDS && index >= descriptor_end && index <= length &&
            entries <= (length - index) / 8, "Invalid podcast index bounds");
    size_t index_end = index + (size_t)entries * 8;
    if (!entries) return 0;
    records = calloc(entries, sizeof(*records));
    REQUIRE(records, "Out of memory for podcast state index");
    unsigned podcasts = 0;
    for (uint32_t i = 0; i < entries; ++i) {
        StateRecord *r = &records[i];
        r->id = state_u32(data + index + (size_t)i * 8);
        r->offset = state_u32(data + index + (size_t)i * 8 + 4);
        REQUIRE(r->id && r->id != UINT32_MAX, "Invalid podcast snapshot atom ID");
        REQUIRE(r->offset >= descriptor_end + 4 && r->offset <= length,
                "Invalid podcast snapshot record offset");
        uint32_t header = state_u32(data + r->offset - 4);
        r->size = header & 0xffffff;
        REQUIRE(!(header & 0x80000000) && r->size <= length - r->offset,
                "Invalid podcast snapshot record size or flag");
        REQUIRE((size_t)r->offset + r->size <= index || r->offset - 4 >= index_end,
                "Podcast snapshot record overlaps index");
        if ((r->id >> 24) == 0x10) ++podcasts;
    }
    qsort(records, entries, sizeof(*records), state_by_id);
    for (uint32_t i = 1; i < entries; ++i)
        REQUIRE(records[i-1].id != records[i].id, "Duplicate podcast snapshot atom ID");
    qsort(records, entries, sizeof(*records), state_by_offset);
    for (uint32_t i = 1; i < entries; ++i)
        REQUIRE((size_t)records[i-1].offset + records[i-1].size <= records[i].offset - 4,
                "Overlapping podcast snapshot records");
    if (podcasts) {
        result = calloc(podcasts, sizeof(*result));
        REQUIRE(result, "Out of memory for podcast states");
    }
    size_t title_offset = version == 5 ? 0x24 : 0x20;
    for (uint32_t i = 0; i < entries; ++i) {
        const StateRecord *r = &records[i];
        if ((r->id >> 24) != 0x10) continue;
        REQUIRE(r->size > title_offset, "Short podcast state record");
        const uint8_t *p = data + r->offset;
        const uint8_t *end = memchr(p + title_offset, 0, r->size - title_offset);
        REQUIRE(end && state_utf8(p + title_offset, (size_t)(end - p - title_offset)),
                "Invalid or unterminated podcast state title");
        ZunePodcastState *out = &result[written++];
        out->item_id = r->id;
        out->series_id = state_u32(p + 4);
        REQUIRE((out->series_id >> 24) == 0x0f, "Invalid podcast state series identity");
        out->duration_ms = state_u32(p + 8);
        out->bookmark_ms = state_u32(p + 12);
        out->raw_flags = state_u32(p + title_offset - 4);
        out->marked_played = (out->raw_flags & 0x200) != 0;
        size_t title_bytes = (size_t)(end - p - title_offset);
        out->title = malloc(title_bytes + 1);
        REQUIRE(out->title, "Out of memory for podcast state title");
        memcpy(out->title, p + title_offset, title_bytes);
        out->title[title_bytes] = 0;
    }
    free(records);
    *states = result; *count = (int)written;
    return 0;
fail:
    free(records); zune_free_podcast_states(result, (int)written);
    zune_set_error("%s", error);
    return -1;
#undef REQUIRE
}

void zune_free_podcast_states(ZunePodcastState *states, int count)
{
    if (!states) return;
    for (int i = 0; i < count; ++i) free(states[i].title);
    free(states);
}

int zune_get_podcast_states(ZuneDeviceHandle dev, ZunePodcastState **states, int *count)
{
    if (states) *states = NULL;
    if (count) *count = 0;
    if (!states || !count || !dev || !dev->mtpz_authenticated ||
        dev->disconnecting || dev->cancel_requested ||
        (dev->device_family != ZUNE_FAMILY_KEEL && dev->device_family != ZUNE_FAMILY_DRACO &&
         dev->device_family != ZUNE_FAMILY_PAVO)) {
        zune_set_error("Podcast state requires an authenticated active Zune 30, 80/120 or HD");
        return -1;
    }
    uint8_t *payload = NULL;
    uint32_t length = 0;
    if (zune_zmdb_read_payload(&dev->usb, &payload, &length)) return -1;
    /* Keel and Draco share Classic v2; Pavo uses HD v5. Never infer a
     * layout for an unknown model or parse a mismatched database as empty. */
    if (dev->disconnecting || dev->cancel_requested || length < 0x26 ||
        state_u16(payload + 0x24) != (dev->device_family == ZUNE_FAMILY_PAVO ? 5 : 2)) {
        free(payload);
        zune_set_error("Podcast state canceled or model/layout mismatch");
        return -1;
    }
    int result = zune_parse_podcast_states(payload, length, states, count);
    free(payload);
    return result;
}
