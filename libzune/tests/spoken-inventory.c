/* Real typed inventory implementation, fake read-only MTP boundary. No USB. */
#include "../src/zune_internal.h"
#include <assert.h>

static uint32_t handles[] = {0x12000002, 0x01000001, 0x10000001, 0x12000001, 0x10000002};
static int handle_count = 5, calls, checks, fail_at, malformed, width_bad, bad_category, duplicates, cancel_at;
static ZuneDevice *device;
static char error[512];
void zune_set_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(error, sizeof(error), fmt, ap); va_end(ap);
}
static void check(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL %s: %s\n", label, error); exit(1); }
    printf("PASS %s\n", label); ++checks;
}
static int next(void) {
    ++calls;
    if (calls == cancel_at) device->cancel_requested = 1;
    return calls == fail_at ? -1 : 0;
}
int mtp_get_object_handles(ptp_session_t *s, uint32_t storage, uint32_t format, uint32_t parent, uint32_t **out, int *n) {
    (void)s; (void)storage;
    assert(format == 0 && parent == 0); /* Includes HD rows omitted by format queries. */
    *out = NULL; *n = 0;
    if (next()) return -1;
    *n = handle_count;
    if (*n > 0 && *n <= 5) {
        *out = malloc((size_t)*n * sizeof(**out)); memcpy(*out, handles, (size_t)*n * sizeof(**out));
        if (duplicates) (*out)[*n-1] = (*out)[0];
    }
    return 0;
}
int mtp_get_object_info(ptp_session_t *s, uint32_t id, mtp_object_info_t *out) {
    (void)s; assert((id >> 24) == 0x10 || (id >> 24) == 0x12);
    if (next()) return -1;
    out->parent_object = (id >> 24) == 0x10 ? 50 : 60 + (id & 255);
    out->object_format = (id & 1) ? MTP_OFC_MP3 : MTP_OFC_WMA;
    out->object_compressed_size = 123456 + (id & 255); return 0;
}
void mtp_free_object_info(mtp_object_info_t *info) { (void)info; }
static void number(uint32_t value, unsigned width, uint8_t **out, uint32_t *n) {
    *n = width + (width_bad ? 1 : 0); *out = calloc(1, *n);
    for (unsigned i = 0; i < width; ++i) (*out)[i] = value >> (8*i);
}
static void string(const char *s, uint8_t **out, uint32_t *n) {
    size_t len = strlen(s); *n = (uint32_t)(3 + 2*len); *out = calloc(1, *n); (*out)[0] = (uint8_t)(len+1);
    for (size_t i = 0; i < len; ++i) (*out)[1+2*i] = (uint8_t)s[i];
    if (malformed == 1) (*out)[*n-1] = 1;
    if (malformed == 2 && len > 1) (*out)[1] = 0;
    if (malformed == 3 && len) { (*out)[1] = 0; (*out)[2] = 0xd8; }
}
int mtp_get_object_prop_value(ptp_session_t *s, uint32_t id, uint16_t prop, uint8_t **out, uint32_t *n) {
    (void)s; *out = NULL; *n = 0;
    assert((id >> 24) == 0x10 || (id >> 24) == 0x12);
    if (next()) return -1;
    switch (prop) {
    case 0xDA9E: number(0x0f000001 + (id & 255), 4, out, n); break;
    case 0xDC8B: number(id & 255, 2, out, n); break;
    case MTP_OPC_MetaGenre: number(bad_category ? 1 : 0, 2, out, n); break;
    case MTP_OPC_Duration: number(65000, 4, out, n); break;
    case MTP_OPC_Name: string("Same title", out, n); break;
    case MTP_OPC_Artist: string("Author", out, n); break;
    case ZUNE_OPC_SeriesName: string((id >> 24) == 0x10 ? "" : "Same book", out, n); break;
    default: abort();
    }
    return 0;
}
static void reset(ZuneDevice *d) {
    calls = fail_at = malformed = width_bad = bad_category = duplicates = cancel_at = 0;
    handle_count = 5; d->cancel_requested = 0; d->disconnecting = 0; d->mtpz_authenticated = 1;
    d->device_family = ZUNE_FAMILY_KEEL; device = d;
}
int main(void) {
    ZuneDevice d = {0}; reset(&d);
    ZunePodcastEpisode *episodes = NULL; ZuneAudiobookPart *parts = NULL; int n = 0;
    for (int family = 0; family < 2; ++family) {
        reset(&d); d.device_family = family ? ZUNE_FAMILY_PAVO : ZUNE_FAMILY_KEEL;
        check(!zune_get_podcast_episodes(&d, &episodes, &n) && n == 2, "native episodes exclude music and books on Keel/Pavo");
        check(episodes[0].series_id != episodes[1].series_id && !strcmp(episodes[0].title, episodes[1].title), "same-title episodes preserve independent series IDs");
        check(!*episodes[0].series_name && episodes[0].duration_ms == 65000 && episodes[0].filesize == 123457, "normalized empty series name and exact duration/size survive");
        zune_free_podcast_episodes(episodes, n); episodes = NULL;
        check(!zune_get_audiobook_parts(&d, &parts, &n) && n == 2, "wildcard native books include WMA and MP3 on Keel/Pavo");
        check(parts[0].parent_id != parts[1].parent_id && !strcmp(parts[0].book_title, parts[1].book_title) && parts[1].part_number == 2, "same-title books preserve folder identity and part numbers");
        zune_free_audiobook_parts(parts, n); parts = NULL;
    }
    /* Every failed read stops immediately, including errors after a full first row. */
    for (int kind = 0; kind < 2; ++kind) {
        reset(&d);
        if (kind) { zune_get_audiobook_parts(&d, &parts, &n); zune_free_audiobook_parts(parts, n); parts = NULL; }
        else { zune_get_podcast_episodes(&d, &episodes, &n); zune_free_podcast_episodes(episodes, n); episodes = NULL; }
        int total = calls;
        for (int at = 1; at <= total; ++at) {
            reset(&d); fail_at = at; n = 99;
            int result = kind ? zune_get_audiobook_parts(&d, &parts, &n) : zune_get_podcast_episodes(&d, &episodes, &n);
            check(result == -1 && !parts && !episodes && !n && calls == at, "every read failure publishes nothing and stops immediately");
        }
        for (int mode = 1; mode <= 3; ++mode) {
            reset(&d); malformed = mode;
            int r = kind ? zune_get_audiobook_parts(&d, &parts, &n) : zune_get_podcast_episodes(&d, &episodes, &n);
            check(r == -1 && !parts && !episodes && !n, "malformed terminator/NUL/surrogate rejected without partial output");
        }
        for (int mode = 0; mode < 8; ++mode) {
            reset(&d);
            if (mode == 0) width_bad = 1;
            if (mode == 1) bad_category = 1;
            if (mode == 2) duplicates = 1;
            if (mode == 3) handle_count = 100001;
            if (mode == 4) handle_count = -1;
            if (mode == 5) d.mtpz_authenticated = 0;
            if (mode == 6) d.device_family = ZUNE_FAMILY_UNKNOWN;
            if (mode == 7) cancel_at = 2;
            int r = kind ? zune_get_audiobook_parts(&d, &parts, &n) : zune_get_podcast_episodes(&d, &episodes, &n);
            check(r == -1 && !parts && !episodes && !n, "invalid width/category/IDs/count/session/cancellation fails closed");
        }
        reset(&d); handle_count = 0;
        int r = kind ? zune_get_audiobook_parts(&d, &parts, &n) : zune_get_podcast_episodes(&d, &episodes, &n);
        check(!r && !n && !parts && !episodes, "empty inventory is successful and owned outputs remain NULL");
    }
    check(zune_get_podcast_episodes(&d, NULL, &n) == -1 && !n, "podcast output required");
    check(zune_get_audiobook_parts(&d, &parts, NULL) == -1 && !parts, "book count required");
    zune_free_podcast_episodes(NULL, 0); zune_free_audiobook_parts(NULL, 0);
    printf("%d spoken inventory checks passed\n", checks);
    return 0;
}
