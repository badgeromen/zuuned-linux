/* Hostile ZMDB inputs against the real parser (zmdb.c is compiled into this
 * translation unit so its static parsers are reachable). A device controls
 * every offset, size and count in the database; each malformed blob must be
 * rejected with no partial library and no ASan/UBSan report, while a valid
 * database keeps parsing exactly as before. The USB read and playlist
 * fallback are fixtures; no backend is linked.
 *
 * Optional argument: a real ZMDB dump (raw, or zune_dump_raw output with its
 * 12-byte response header) that must still parse. Never commit device dumps. */
#include "../src/zmdb.c"
#include <assert.h>
#include <signal.h>

static int checks;
static char last_error[1024];

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
}
void zune_clear_error(void) { last_error[0] = 0; }
const char *zune_get_error(void) { return last_error[0] ? last_error : NULL; }

/* Payload the fixture transport returns to zune_infiltrate*. */
static const uint8_t *served;
static size_t served_len;
int zune_zmdb_read_payload(zune_usb_handle_t *usb, uint8_t **payload, uint32_t *length) {
    (void)usb;
    *payload = malloc(served_len ? served_len : 1);
    assert(*payload);
    memcpy(*payload, served, served_len);
    *length = (uint32_t)served_len;
    return 0;
}
static int playlist_fallback_fails;
ZunePlaylist *zune_get_playlists(ZuneDevice *dev, int *count) {
    (void)dev;
    *count = 0;
    if (playlist_fallback_fails) zune_set_error("playlist query failed");
    return NULL;
}
void zune_free_videos(ZuneVideoFile *videos, int count) {
    if (!videos) return;
    for (int i = 0; i < count; i++) {
        free(videos[i].filename);
        free(videos[i].title);
        free(videos[i].object_filename);
    }
    free(videos);
}

static void check(int ok, const char *name) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s (error: %s)\n", name, last_error);
        exit(1);
    }
    printf("PASS: %s\n", name);
    checks++;
}

/* ---- Synthetic database builder ---- */

enum { BLOB = 8192, ZARR = 0x30, INDEX = 0x800, MUSIC = 0x900, ALBUMS = 0x940,
       ARTISTS = 0x980, RECORDS = 0x1000 };
enum { TRACK = 0x01000001, ALBUM = 0x06000001, ARTIST = 0x08000001,
       GENRE = 0x09000001 };

typedef struct {
    uint8_t data[BLOB];
    size_t len;          /* bytes handed to the parser */
    size_t next;         /* next free record byte */
    size_t index_count;
} Blob;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint8_t *desc(Blob *b, int i) { return b->data + ZARR + (size_t)i * 20; }
static void set_desc(Blob *b, int i, uint32_t size, uint32_t count, uint32_t offset) {
    put16(desc(b, i) + 6, size);
    put32(desc(b, i) + 8, count);
    put32(desc(b, i) + 16, offset);
}

/* Append a record (header + fixed bytes + NUL-terminated text) and index it. */
static uint32_t add_record(Blob *b, uint32_t atom, const uint8_t *fixed, size_t fixed_len,
                           const char *text) {
    size_t text_len = text ? strlen(text) + 1 : 0;
    uint32_t offset = (uint32_t)(b->next + 4);
    assert(offset + fixed_len + text_len < BLOB);
    put32(b->data + b->next, (uint32_t)(fixed_len + text_len));
    if (fixed_len) memcpy(b->data + offset, fixed, fixed_len);
    if (text_len) memcpy(b->data + offset + fixed_len, text, text_len);
    b->next = offset + fixed_len + text_len;
    uint8_t *entry = b->data + INDEX + b->index_count * 8;
    put32(entry, atom);
    put32(entry + 4, offset);
    b->index_count++;
    set_desc(b, 0, 8, (uint32_t)b->index_count, INDEX);
    return offset;
}

/* One track with album, artist and genre: HD (v5) or Classic (v2) layout. */
static void build(Blob *b, int hd) {
    memset(b, 0, sizeof(*b));
    memcpy(b->data, "ZMDB", 4);
    memcpy(b->data + 0x20, "ZMed", 4);
    put16(b->data + 0x24, hd ? 5 : 2);
    for (int i = 0; i < 96; i++) memcpy(desc(b, i), "ZArr", 4);
    b->next = RECORDS;
    b->len = BLOB;

    uint8_t artist_fixed[4] = {0};
    add_record(b, ARTIST, artist_fixed, hd ? 4 : 1, "Artist");
    uint8_t album_fixed[20] = {0};
    put32(album_fixed, ARTIST);
    add_record(b, ALBUM, album_fixed, hd ? 20 : 12, "Album");
    uint8_t genre_fixed[1] = {0};
    add_record(b, GENRE, genre_fixed, 1, "Rock");

    uint8_t track[32] = {0};
    put32(track + 0, ALBUM);
    put32(track + 4, ARTIST);
    put32(track + 8, GENRE);
    put32(track + 16, 215000);      /* duration ms */
    put32(track + 20, 4000000);     /* size */
    put16(track + 24, 7);           /* track number */
    if (hd) { put16(track + 26, 3); track[30] = 8; }   /* playcount, rating */
    add_record(b, TRACK, track, hd ? 32 : 28, "Song");

    put32(b->data + MUSIC, TRACK);
    set_desc(b, 1, 4, 1, MUSIC);
    put32(b->data + ALBUMS, ALBUM);
    set_desc(b, 3, 4, 1, ALBUMS);
    put32(b->data + ARTISTS, ARTIST);
    set_desc(b, 4, 4, 1, ARTISTS);
}

/* Parse with both parsers; 1 if both reject with no library. */
static int rejected(const Blob *b) {
    last_error[0] = '\0';
    ZuneDBLibrary *full = zmdb_parse_full(b->data, b->len);
    int full_error = strstr(last_error, "ZMDB rejected") != NULL;
    last_error[0] = '\0';
    ZuneLibrary *legacy = zmdb_parse(b->data, b->len);
    int legacy_error = strstr(last_error, "ZMDB rejected") != NULL;
    int ok = !full && !legacy && full_error && legacy_error;
    zune_free_scan(full);
    zune_free_library(legacy);
    return ok;
}

static int parses_one_track(const Blob *b, int hd) {
    ZuneDBLibrary *lib = zmdb_parse_full(b->data, b->len);
    ZuneLibrary *legacy = zmdb_parse(b->data, b->len);
    int ok = lib && legacy && lib->is_hd == hd && lib->track_count == 1
        && legacy->track_count == 1
        && strcmp(lib->tracks[0].title, "Song") == 0
        && strcmp(lib->tracks[0].artist, "Artist") == 0
        && strcmp(lib->tracks[0].album, "Album") == 0
        && strcmp(lib->tracks[0].genre, "Rock") == 0
        && lib->tracks[0].duration_ms == 215000 && lib->tracks[0].tracknumber == 7
        && lib->tracks[0].item_id == TRACK
        && (!hd || (lib->tracks[0].playcount == 3 && lib->tracks[0].rating == 8))
        && lib->album_count == 1 && strcmp(lib->albums[0].title, "Album") == 0
        && strcmp(lib->albums[0].artist, "Artist") == 0
        && lib->artist_count == 1 && strcmp(lib->artists[0].name, "Artist") == 0
        && strcmp(legacy->tracks[0].title, "Song") == 0;
    zune_free_scan(lib);
    zune_free_library(legacy);
    return ok;
}

static uint32_t index_offset_of(const Blob *b, size_t entry) {
    return zune_le32(b->data + INDEX + entry * 8 + 4);
}

static void hostile_cases(int hd) {
    Blob b;
    char name[160];
    const char *model = hd ? "HD" : "Classic";

    build(&b, hd);
    snprintf(name, sizeof(name), "%s: well-formed database still parses completely", model);
    check(parses_one_track(&b, hd), name);

    /* The review's proof: descriptor-0 data_offset 0xFFFFFFF8 passed the
     * uint32 check (offset + 8 wrapped to 0) and segfaulted in rd32. */
    build(&b, hd);
    set_desc(&b, 0, 8, 1, 0xFFFFFFF8u);
    snprintf(name, sizeof(name), "%s: wrapped index data_offset 0xFFFFFFF8 is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 0, 8, 0xFFFFFFFFu, INDEX);
    snprintf(name, sizeof(name), "%s: huge index entry_count is rejected before allocating", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 0, 8, (uint32_t)((BLOB - INDEX) / 8 + 1), INDEX);
    snprintf(name, sizeof(name), "%s: index one entry past the end is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 0, 4, (uint32_t)b.index_count, INDEX);
    snprintf(name, sizeof(name), "%s: non-8-byte index entries are rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    put32(b.data + INDEX + 4, (uint32_t)b.len + 1);
    snprintf(name, sizeof(name), "%s: record offset one byte past the end is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    put32(b.data + INDEX + 4, 2);
    snprintf(name, sizeof(name), "%s: record offset below its header is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    {
        uint32_t offset = index_offset_of(&b, 3);   /* the track record */
        put32(b.data + offset - 4, (uint32_t)(b.len - offset + 1));
    }
    snprintf(name, sizeof(name), "%s: record size one byte past the end is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 1, 4, 1, 0xFFFFFFFCu);
    snprintf(name, sizeof(name), "%s: wrapped music descriptor offset is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 1, 4, (uint32_t)((BLOB - MUSIC) / 4 + 1), MUSIC);
    snprintf(name, sizeof(name), "%s: music descriptor one entry past the end is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 1, 0, 0xFFFFFFFFu, MUSIC);
    snprintf(name, sizeof(name), "%s: zero-size entries with a huge count are rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    set_desc(&b, 3, 4, 0xFFFFFFFFu, ALBUMS);
    snprintf(name, sizeof(name), "%s: oversized album descriptor fails the whole scan", model);
    last_error[0] = '\0';
    ZuneDBLibrary *partial = zmdb_parse_full(b.data, b.len);
    check(!partial && strstr(last_error, "ZMDB rejected"), name);

    build(&b, hd);
    b.len = 0;
    snprintf(name, sizeof(name), "%s: zero-length database is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    b.len = 0x3F;
    snprintf(name, sizeof(name), "%s: header shorter than 0x40 bytes is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    b.len = ZARR + 100;
    snprintf(name, sizeof(name), "%s: truncated descriptor table is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    b.len = INDEX + 12;
    snprintf(name, sizeof(name), "%s: payload cut inside the index is rejected", model);
    check(rejected(&b), name);

    build(&b, hd);
    memcpy(b.data, "ZMDX", 4);
    snprintf(name, sizeof(name), "%s: bad magic is rejected", model);
    check(rejected(&b), name);

    /* Semantics kept: a flagged record is skipped, not fatal. */
    build(&b, hd);
    {
        uint32_t offset = index_offset_of(&b, 3);
        put32(b.data + offset - 4, 0x80000000u | zune_le32(b.data + offset - 4));
    }
    ZuneDBLibrary *lib = zmdb_parse_full(b.data, b.len);
    snprintf(name, sizeof(name), "%s: sign-bit record is skipped, the scan still succeeds", model);
    check(lib && lib->track_count == 0 && lib->album_count == 1, name);
    zune_free_scan(lib);

    /* Semantics kept: an atom missing from the index is skipped. */
    build(&b, hd);
    put32(b.data + MUSIC, 0x01000099);
    lib = zmdb_parse_full(b.data, b.len);
    snprintf(name, sizeof(name), "%s: unindexed atom is skipped, the scan still succeeds", model);
    check(lib && lib->track_count == 0, name);
    zune_free_scan(lib);

    /* Semantics kept: with duplicate atoms the first in table order wins. */
    build(&b, hd);
    {
        uint8_t fixed[4] = {0};
        uint32_t second = add_record(&b, ARTIST, fixed, hd ? 4 : 1, "Second");
        (void)second;
        /* Swap so the new record's entry comes first in the table. */
        uint8_t first[8], last[8];
        memcpy(first, b.data + INDEX, 8);
        memcpy(last, b.data + INDEX + (b.index_count - 1) * 8, 8);
        memcpy(b.data + INDEX, last, 8);
        memcpy(b.data + INDEX + (b.index_count - 1) * 8, first, 8);
    }
    lib = zmdb_parse_full(b.data, b.len);
    snprintf(name, sizeof(name), "%s: duplicate atom resolves to the first index entry", model);
    check(lib && lib->track_count == 1 && strcmp(lib->tracks[0].artist, "Second") == 0, name);
    zune_free_scan(lib);
}

/* ---- Public entry points through the fixture transport ---- */

/* zune_dump_raw's reader: the fixture payload behind its 12-byte container
 * header (the transport itself is tested in zmdb-transport.c). */
static int response_fails;
int zune_zmdb_read_response(zune_usb_handle_t *usb, uint8_t header[12], uint8_t **payload,
                            uint32_t *length) {
    if (response_fails) {
        zune_set_error("ZMDB invalid header or total size: 4");
        return -1;
    }
    put32(header, (uint32_t)served_len + 12);
    put16(header + 4, 2);
    put16(header + 6, 0x9217);
    put32(header + 8, 0x001F9203);
    return zune_zmdb_read_payload(usb, payload, length);
}

static void public_paths(void) {
    Blob b;
    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));

    build(&b, 1);
    set_desc(&b, 0, 8, 1, 0xFFFFFFF8u);
    served = b.data;
    served_len = b.len;
    ZuneDBLibrary *lib = (ZuneDBLibrary *)1;
    last_error[0] = '\0';
    check(zune_infiltrate(&dev, &lib) == -1 && lib == NULL && dev.cached_library == NULL
          && strstr(last_error, "index extends past the end"),
          "zune_infiltrate reports the specific rejection and caches nothing");
    ZuneLibrary *legacy = (ZuneLibrary *)1;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    check(zune_infiltrate_legacy(&dev, &legacy) == -1 && legacy == NULL,
          "zune_infiltrate_legacy rejects the same database");
#pragma GCC diagnostic pop

    /* The research scanner must not wander outside the payload either. */
    build(&b, 1);
    set_desc(&b, 0, 0, 0xFFFFFFFFu, 0xFFFFFFF0u);
    set_desc(&b, 12, 4, 0xFFFFFFFFu, MUSIC);
    put32(b.data + MUSIC, 0x02000001);
    set_desc(&b, 13, 0xFFFF, 0xFFFFFFFFu, 0xFFFFFFFCu);
    served = b.data;
    check(zune_infiltrate_deep(&dev) == 0, "descriptor scan survives wrapped and zero-size descriptors");

    build(&b, 1);
    served = b.data;
    lib = NULL;
    check(zune_infiltrate(&dev, &lib) == 0 && lib && lib->track_count == 1
          && dev.cached_library == lib,
          "zune_infiltrate still returns and caches a valid scan");
    zune_free_scan(lib);
    dev.cached_library = NULL;

    playlist_fallback_fails = 1;
    lib = NULL;
    check(zune_infiltrate(&dev, &lib) == 0 && lib && !last_error[0],
          "a failed playlist fallback leaves no error on a successful scan");
    playlist_fallback_fails = 0;
    zune_free_scan(lib);
    dev.cached_library = NULL;

    char path[] = "/tmp/libzune-zmdb-dump-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    response_fails = 1;
    check(zune_dump_raw(&dev, path) == -1 && strstr(last_error, "total size"),
          "raw dump fails with the reader's error when the response is refused");
    response_fails = 0;
    build(&b, 1);
    served = b.data;
    served_len = b.len;
    check(zune_dump_raw(&dev, path) == 0, "raw dump writes a valid response");
    FILE *fp = fopen(path, "rb");
    uint8_t *written = malloc(b.len + 13);
    size_t got = fp ? fread(written, 1, b.len + 13, fp) : 0;
    if (fp) fclose(fp);
    check(got == b.len + 12 && zune_le32(written) == (uint32_t)b.len + 12 && written[4] == 2 &&
          !memcmp(written + 12, b.data, b.len),
          "the dump file holds the container header, then the exact payload");
    free(written);
    unlink(path);
}

static void real_dump(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "FAIL: cannot open %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    assert(data && fread(data, 1, (size_t)size, fp) == (size_t)size);
    fclose(fp);
    size_t skip = size > 16 && memcmp(data, "ZMDB", 4) != 0 && memcmp(data + 12, "ZMDB", 4) == 0 ? 12 : 0;
    ZuneDBLibrary *lib = zmdb_parse_full(data + skip, (size_t)size - skip);
    ZuneLibrary *legacy = zmdb_parse(data + skip, (size_t)size - skip);
    check(lib && legacy && lib->track_count == legacy->track_count,
          "real device dump parses under the strict bounds");
    printf("  real dump: %d tracks, %d albums, %d artists, %d photos\n",
           lib->track_count, lib->album_count, lib->artist_count, lib->photo_count);
    zune_free_scan(lib);
    zune_free_library(legacy);
    free(data);
}

/* Classic tables hold 33 descriptors and the index follows them (0x2D0 on
 * Keel and Draco); the slots after the table are index bytes. Such bytes
 * that look like an album descriptor must neither fail nor feed the scan. */
static void classic_table_bound(void) {
    Blob b;
    build(&b, 0);
    size_t table_end = ZARR + 33 * 20;
    memmove(b.data + table_end, b.data + INDEX, b.index_count * 8);
    set_desc(&b, 0, 8, (uint32_t)b.index_count, (uint32_t)table_end);
    set_desc(&b, 3, 0, 0, 0);             /* no album descriptor in the table */
    set_desc(&b, 40, 3, 4100, ALBUMS);     /* past the table: looks like one */
    last_error[0] = '\0';
    ZuneDBLibrary *lib = zmdb_parse_full(b.data, b.len);
    check(lib && lib->track_count == 1 && lib->album_count == 0 && !last_error[0],
          "Classic: slots past the 33-descriptor table are not discovered");
    zune_free_scan(lib);
}

/* Keep the whole Classic payload smaller than a fictitious 96-slot table.
 * The padded fixture above cannot catch a table-size check that runs before
 * the real 33-slot bound is discovered. Retain the exact record contents. */
static void build_compact_classic(Blob *b) {
    Blob original;
    build(&original, 0);
    memset(b, 0, sizeof(*b));
    memcpy(b->data, original.data, ZARR + 33 * 20);
    size_t record_bytes = original.next - RECORDS;
    memcpy(b->data + 0x400, original.data + RECORDS, record_bytes);
    for (size_t i = 0; i < original.index_count; i++) {
        memcpy(b->data + 0x2D0 + i * 8, original.data + INDEX + i * 8, 8);
        put32(b->data + 0x2D0 + i * 8 + 4,
              index_offset_of(&original, i) - RECORDS + 0x400);
    }
    set_desc(b, 0, 8, (uint32_t)original.index_count, 0x2D0);
    put32(b->data + 0x300, TRACK);
    set_desc(b, 1, 4, 1, 0x300);
    put32(b->data + 0x304, ALBUM);
    set_desc(b, 3, 4, 1, 0x304);
    put32(b->data + 0x308, ARTIST);
    set_desc(b, 4, 4, 1, 0x308);
    b->len = 0x400 + record_bytes;
    b->index_count = original.index_count;
}

static void compact_classic_cases(void) {
    Blob b;
    build_compact_classic(&b);
    check(b.len < ZARR + 96 * 20 && parses_one_track(&b, 0),
          "Classic: complete compact 33-descriptor database parses without padding");

    build_compact_classic(&b);
    b.len = ZARR + 20 - 1;
    check(rejected(&b), "Classic: truncated descriptor zero is rejected");

    build_compact_classic(&b);
    b.len = ZARR + 33 * 20 - 1;
    check(rejected(&b), "Classic: truncated compact descriptor table is rejected");

    build_compact_classic(&b);
    b.len = 0x2D0 + b.index_count * 8 - 1;
    check(rejected(&b), "Classic: truncated compact index is rejected");

    build_compact_classic(&b);
    b.len--;
    check(rejected(&b), "Classic: truncated compact record is rejected");

    build_compact_classic(&b);
    set_desc(&b, 0, 8, (uint32_t)b.index_count, ZARR + 16 * 20);
    check(rejected(&b), "Classic: index overlapping required media slots is rejected");

    build_compact_classic(&b);
    put16(b.data + 0x24, 5);
    check(rejected(&b), "HD: Classic-sized table is not accepted as a full HD table");

    build_compact_classic(&b);
    put16(b.data + 0x24, 3);
    check(rejected(&b), "unknown version: compact Classic table is not inferred");
}

int main(int argc, char **argv) {
    alarm(30);   /* a looping parser fails instead of hanging the suite */
    hostile_cases(1);
    hostile_cases(0);
    classic_table_bound();
    compact_classic_cases();
    public_paths();
    if (argc > 1 && argv[1][0]) real_dump(argv[1]);
    printf("ZMDB hostile input: %d checks passed; no USB\n", checks);
    return 0;
}
