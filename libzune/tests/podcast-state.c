/* Offline fixtures: fake USB only, never link a hardware backend. */
#include "zune_internal.h"
#include <assert.h>

static int checks;
static void check(int result, const char *name)
{ if (!result) { fprintf(stderr, "FAIL %s: %s\n", name, zune_get_error()); exit(1); } ++checks; }
static char error_text[512];
void zune_set_error(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vsnprintf(error_text, sizeof(error_text), fmt, ap); va_end(ap); }
void zune_clear_error(void) { error_text[0] = 0; }
const char *zune_get_error(void) { return error_text; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8*i)); }
static void put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint8_t blob[70012];
static size_t blob_size, index_at, record_at, title_at;
static void fixture(int version, uint32_t flags)
{
    memset(blob, 0, sizeof(blob));
    uint8_t *d = blob + 12;
    unsigned descriptors = version == 2 ? 33 : 97;
    memcpy(d, "ZMDB", 4); memcpy(d+0x20, "ZMed", 4); put16(d+0x24, version);
    for (unsigned i = 0; i < descriptors; ++i) memcpy(d+0x38+i*20, "ZArr", 4);
    index_at = 0x38+descriptors*20+4;
    record_at = index_at + 36;
    title_at = version == 5 ? 0x24 : 0x20;
    put16(d+0x38+6, 8); put32(d+0x38+8, 1); put32(d+0x38+16, index_at);
    put32(d+index_at, 0x10000001); put32(d+index_at+4, record_at);
    put32(d+record_at-4, title_at+16);
    put32(d+record_at, 0x05000001); put32(d+record_at+4, 0x0f000001);
    put32(d+record_at+8, 90000); put32(d+record_at+12, 40599);
    put32(d+record_at+title_at-4, flags); memcpy(d+record_at+title_at, "Episode \xcf\x80", 11);
    blob_size = 12+record_at+title_at+16;
    put32(blob, blob_size); put16(blob+4, 2); put16(blob+6, 0x9217);
}
static int parse(void)
{
    ZunePodcastState *rows = (void *)1; int n = 22;
    int rc = zune_parse_podcast_states(blob+12, blob_size-12, &rows, &n);
    if (rc) check(!rows && !n, "failed parser clears output");
    zune_free_podcast_states(rows, n);
    return rc;
}
static size_t read_at, cutoff;
static int writes, malformed_count, fail_write, cancel_after_read;
static ZuneDevice device;
static int fake_read(zune_usb_handle_t *h, uint8_t ep, uint8_t *p, uint32_t max,
                     uint32_t *actual, uint32_t timeout)
{
    (void)h; (void)ep; (void)timeout;
    if (read_at >= cutoff) { *actual = 0; return -1; }
    size_t n = cutoff-read_at; if (n > max) n = max;
    memcpy(p, blob+read_at, n); read_at += n; *actual = (uint32_t)n;
    if (malformed_count && read_at > 65536) *actual = max+1;
    if (cancel_after_read) device.cancel_requested = 1;
    return 0;
}
static int fake_write(zune_usb_handle_t *h, uint8_t ep, const uint8_t *p, uint32_t n,
                      uint32_t *actual, uint32_t timeout)
{
    (void)h; (void)ep; (void)timeout;
    assert(n == 16 && p[6] == 0x17 && p[7] == 0x92);
    ++writes; *actual = fail_write ? 0 : n; return fail_write ? -1 : 0;
}
static const zune_usb_backend_t backend = {.bulk_read=fake_read, .bulk_write=fake_write};
static void transport_fixture(void)
{
    fixture(5, 0x200); blob_size = sizeof(blob); put32(blob, blob_size);
    read_at = 0; cutoff = blob_size; malformed_count = fail_write = cancel_after_read = writes = 0;
    memset(&device, 0, sizeof(device)); device.usb.backend = &backend;
    device.mtpz_authenticated = 1; device.device_family = ZUNE_FAMILY_PAVO;
}
static int get(void)
{
    ZunePodcastState *rows = (void *)1; int n = 22;
    int rc = zune_get_podcast_states(&device, &rows, &n);
    if (rc) check(!rows && !n, "failed API clears output");
    else check(n == 1 && rows[0].marked_played && rows[0].bookmark_ms == 40599,
               "complete transport publishes independent state");
    zune_free_podcast_states(rows, n); return rc;
}
int main(int argc, char **argv)
{
    /* Optional offline read of already captured local snapshots. */
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb"); assert(f); assert(!fseek(f, 0, SEEK_END));
        long len = ftell(f); assert(len > 12 && len <= 64*1024*1024); rewind(f);
        uint8_t *data = malloc((size_t)len); assert(data); assert(fread(data, 1, (size_t)len, f) == (size_t)len); fclose(f);
        ZunePodcastState *rows = NULL; int n = 0;
        check(!zune_parse_podcast_states(data+12, (size_t)len-12, &rows, &n), "saved local snapshot");
        printf("Validated %d podcast state records\n", n); zune_free_podcast_states(rows, n); free(data); return 0;
    }
    const uint32_t flags[] = {0, 1, 0x400, 0x200, 0x201, 0xffffffff};
    for (int version = 2; version <= 5; version += 3) {
        for (unsigned i = 0; i < sizeof(flags)/sizeof(flags[0]); ++i) {
            fixture(version, flags[i]); ZunePodcastState *rows = NULL; int n = 0;
            check(!zune_parse_podcast_states(blob+12, blob_size-12, &rows, &n) && n == 1 &&
                  rows[0].item_id == 0x10000001 && rows[0].series_id == 0x0f000001 &&
                  rows[0].duration_ms == 90000 && rows[0].bookmark_ms == 40599 &&
                  rows[0].title && !strcmp(rows[0].title, "Episode \xcf\x80") &&
                  rows[0].raw_flags == flags[i] && rows[0].marked_played == !!(flags[i]&0x200),
                  "model offsets and exact played bit independent of nonzero bookmark");
            zune_free_podcast_states(rows, n);
        }
    }
#define REJECT(name, mutation) do { fixture(5, 0x200); uint8_t *d = blob+12; (void)d; mutation; check(parse() == -1, name); } while (0)
    REJECT("bad magic", d[0] = 0);
    REJECT("bad inner magic", d[0x20] = 0);
    REJECT("unknown version", put16(d+0x24, 6));
    REJECT("short snapshot", blob_size = 24);
    REJECT("truncated descriptor", blob_size = 12+0x38+40+10);
    REJECT("missing index entry size", put16(d+0x38+6, 4));
    REJECT("index count overflow", put32(d+0x38+8, UINT32_MAX));
    REJECT("index offset overflow", put32(d+0x38+16, UINT32_MAX));
    REJECT("index in header", put32(d+0x38+16, 0));
    REJECT("duplicate atom identity", put32(d+0x38+8, 2); memcpy(d+index_at+8, d+index_at, 8));
    REJECT("aliased record offsets", put32(d+0x38+8, 2); memcpy(d+index_at+8, d+index_at, 8); put32(d+index_at+8, 0x10000002));
    REJECT("record offset past EOF", put32(d+index_at+4, UINT32_MAX));
    REJECT("record offset inside header", put32(d+index_at+4, 0));
    REJECT("record length past EOF", put32(d+record_at-4, 0xffffff));
    REJECT("invalid record flag", put32(d+record_at-4, 0x80000040));
    REJECT("zero object handle", put32(d+index_at, 0));
    REJECT("wildcard object handle", put32(d+index_at, UINT32_MAX));
    REJECT("short podcast record", put32(d+record_at-4, title_at));
    REJECT("not a podcast series", put32(d+record_at+4, 0x01000001));
    REJECT("title has no bounded terminator", memset(d+record_at+title_at, 'x', 16));
    REJECT("invalid UTF8 lead", d[record_at+title_at] = 0xff);
    REJECT("overlong UTF8", memcpy(d+record_at+title_at, "\xc0\x80", 3));
    REJECT("UTF8 surrogate", memcpy(d+record_at+title_at, "\xed\xa0\x80", 4));
    REJECT("UTF8 beyond Unicode", memcpy(d+record_at+title_at, "\xf4\x90\x80\x80", 5));
    REJECT("unfinished UTF8 before terminator", memcpy(d+record_at+title_at, "\xf0\x90", 3));
    REJECT("record overlaps index", put32(d+index_at+4, index_at+16); put32(d+index_at+12, title_at+16); put32(d+0x38+8, 3));
    REJECT("record interval overlap", put32(d+0x38+8, 2); put32(d+index_at+8, 0x05000002); put32(d+index_at+12, record_at+8); put32(d+record_at+4, 8));
    fixture(5, 0); put32(blob+12+0x38+8, 0);
    ZunePodcastState *rows = (void*)1; int n = 77;
    check(!zune_parse_podcast_states(blob+12, blob_size-12, &rows, &n) && !rows && !n, "known empty snapshot");
    fixture(5, 0); put32(blob+12+index_at, 0x01000001);
    check(!zune_parse_podcast_states(blob+12, blob_size-12, &rows, &n) && !rows && !n, "music cannot masquerade as played podcast");
    fixture(5, 0); memcpy(blob+12+record_at+title_at, "\xf4\x8f\xbf\xbf", 5);
    check(!parse(), "maximum Unicode scalar title");
    check(zune_parse_podcast_states(NULL, 100, &rows, &n) == -1 && !rows && !n, "null payload");
    check(zune_parse_podcast_states(blob+12, blob_size-12, NULL, &n) == -1, "required output array");
    fixture(5, 0x200);
    uint8_t *d = blob+12;
    size_t second = blob_size-12+4, record_size = title_at+16;
    put32(d+0x38+8, 2); put32(d+index_at+8, 0x10000002); put32(d+index_at+12, second);
    put32(d+second-4, record_size); memcpy(d+second, d+record_at, record_size);
    blob_size += 4+record_size; put32(blob, blob_size);
    check(!zune_parse_podcast_states(d, blob_size-12, &rows, &n) && n == 2 &&
          rows[0].title != rows[1].title && !strcmp(rows[0].title, rows[1].title),
          "multiple records own separate complete titles");
    zune_free_podcast_states(rows, n);
    put32(d+second+4, 0x01000001);
    check(parse() == -1, "later bad series frees previously allocated titles without partial publication");
    transport_fixture(); check(!get(), "full multichunk payload");
    transport_fixture(); cutoff = 66000; check(get() == -1, "ninety-four percent transfer is unavailable");
    transport_fixture(); cutoff = 30000; check(get() == -1, "short transfer unavailable");
    transport_fixture(); cutoff = 11; check(get() == -1, "short transport header");
    transport_fixture(); put16(blob+4, 3); check(get() == -1, "wrong transport type");
    transport_fixture(); put16(blob+6, 0x1009); check(get() == -1, "wrong transport opcode");
    transport_fixture(); put32(blob, 65500); check(get() == -1, "first transfer exceeds declared size");
    transport_fixture(); put32(blob, 0xffffffff); check(get() == -1, "oversized transport declaration");
    transport_fixture(); malformed_count = 1; check(get() == -1, "backend excess transfer count");
    transport_fixture(); fail_write = 1; check(get() == -1, "request write failure");
    transport_fixture(); device.mtpz_authenticated = 0; check(get() == -1 && !writes, "authentication before I/O");
    transport_fixture(); device.device_family = ZUNE_FAMILY_UNKNOWN; check(get() == -1 && !writes, "unsupported family before I/O");
    transport_fixture(); device.device_family = ZUNE_FAMILY_SCORPIUS; check(get() == -1 && !writes, "unvalidated flash family still fails before I/O");
    transport_fixture(); device.cancel_requested = 1; check(get() == -1 && !writes, "cancellation before I/O");
    transport_fixture(); device.disconnecting = 1; check(get() == -1 && !writes, "disconnect before I/O");
    transport_fixture(); cancel_after_read = 1; check(get() == -1, "cancellation during read");
    transport_fixture(); device.device_family = ZUNE_FAMILY_KEEL; check(get() == -1, "family version mismatch");
    transport_fixture(); device.usb.backend = NULL; check(get() == -1 && !writes, "missing backend");
    const ZuneDeviceFamily families[] = {ZUNE_FAMILY_KEEL, ZUNE_FAMILY_DRACO, ZUNE_FAMILY_PAVO};
    for (unsigned family = 0; family < sizeof(families)/sizeof(families[0]); ++family) {
        for (int version = 2; version <= 5; version += 3) {
            transport_fixture(); fixture(version, 0x200);
            blob_size = sizeof(blob); put32(blob, blob_size); cutoff = blob_size;
            device.device_family = families[family];
            int expected = families[family] == ZUNE_FAMILY_PAVO ? 5 : 2;
            check((get() == 0) == (version == expected),
                  "Keel/Draco require v2 and Pavo requires v5 through authenticated state API");
        }
        for (int failure = 0; failure < 4; ++failure) {
            transport_fixture(); fixture(families[family] == ZUNE_FAMILY_PAVO ? 5 : 2, 0x200);
            blob_size = sizeof(blob); put32(blob, blob_size); cutoff = blob_size;
            device.device_family = families[family];
            if (failure == 0) cutoff = 30000;
            if (failure == 1) device.mtpz_authenticated = 0;
            if (failure == 2) cancel_after_read = 1;
            if (failure == 3) put32(blob+12+record_at+4, 0x01000001);
            check(get() == -1, "all supported families reject partial, unauthenticated, canceled or malformed state");
        }
    }
    printf("%d native podcast state checks passed\n", checks);
    return 0;
}
