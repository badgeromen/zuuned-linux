/*
 * zmdb.c — Zune Media Database (ZMDB) binary parser
 *
 * Reads the Zune's internal media database via vendor opcode 0x1792,
 * returning the full library (tracks, videos, photos) in a single USB
 * operation. This is 10-100x faster than MTP per-object enumeration.
 *
 * Binary format reverse-engineered from:
 *   - zune-explorer's zmdb-parser.js (NiceBeard)
 *   - XuneSyncLibrary by magicisinthehole
 *
 * Two format versions:
 *   ZMed v2 = Classic (Zune 30/80/120, flash 4/8/16)
 *   ZMed v5 = Zune HD
 *
 * Part of libzune.
 */

#include "zune_internal.h"

/* ---- Schema type constants (upper byte of atom_id) ---- */
#define SCHEMA_MUSIC          0x01
#define SCHEMA_VIDEO          0x02
#define SCHEMA_PICTURE        0x03
#define SCHEMA_FILENAME       0x05
#define SCHEMA_ALBUM          0x06
#define SCHEMA_PLAYLIST       0x07
#define SCHEMA_ARTIST         0x08
#define SCHEMA_GENRE          0x09
#define SCHEMA_PHOTOALBUM     0x0B

/* ---- Fixed-size entry portion by device type ---- */
#define ENTRY_SIZE_MUSIC_HD       32
#define ENTRY_SIZE_MUSIC_CLASSIC  28
#define ENTRY_SIZE_VIDEO          32
#define ENTRY_SIZE_PICTURE        24
#define ENTRY_SIZE_FILENAME       8
#define ENTRY_SIZE_ALBUM_HD       20
#define ENTRY_SIZE_ALBUM_CLASSIC  12
#define ENTRY_SIZE_ARTIST_HD      4
#define ENTRY_SIZE_ARTIST_CLASSIC 1
#define ENTRY_SIZE_GENRE          1

/* ---- Descriptor-to-schema mapping ---- */
typedef struct { int desc_idx; int schema; } ZMDBDescMap;

/* HD descriptor indices that contain media */
static const ZMDBDescMap HD_DESC_MAP[] = {
    {1,  SCHEMA_MUSIC},
    {12, SCHEMA_VIDEO},
    {16, SCHEMA_PICTURE},
    {-1, -1}
};

static const ZMDBDescMap CLASSIC_DESC_MAP[] = {
    {1,  SCHEMA_MUSIC},
    {12, SCHEMA_VIDEO},
    {16, SCHEMA_PICTURE},    /* Discovered: Classic DOES have photos at desc 16 */
    {-1, -1}
};

/* ---- Binary reading helpers ---- */

static inline uint16_t rd16(const uint8_t *buf) {
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

static inline uint32_t rd32(const uint8_t *buf) {
    return (uint32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
}

static inline int32_t rdi32(const uint8_t *buf) {
    return (int32_t)rd32(buf);
}

static inline uint64_t rd64(const uint8_t *buf) {
    return (uint64_t)rd32(buf) | ((uint64_t)rd32(buf + 4) << 32);
}

/* Read null-terminated UTF-8 string, stripping leading encoding artifacts */
static char *read_utf8(const uint8_t *buf, size_t max_len) {
    size_t i = 0;
    while (i < max_len && buf[i] != 0) i++;
    if (i == 0) return strdup("");

    /* Skip leading BOM/replacement chars (common in ZMDB records) */
    const uint8_t *start = buf;
    while (start < buf + i && (*start == 0xEF || *start == 0xBF || *start == 0xBD))
        start++;

    size_t len = (buf + i) - start;
    char *s = malloc(len + 1);
    if (!s) return strdup("");
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

/* ---- Descriptor structure ---- */

typedef struct {
    uint16_t entry_size;
    uint32_t entry_count;
    uint32_t data_offset;
} ZMDBDescriptor;

/* ---- Index table entry ---- */

typedef struct {
    uint32_t atom_id;
    uint32_t record_offset;
} ZMDBIndexEntry;

/* ---- Internal parser state ---- */

typedef struct {
    const uint8_t *data;
    size_t data_len;
    int is_hd;
    ZMDBDescriptor descs[96];
    ZMDBIndexEntry *index;
    int index_count;
} ZMDBState;

/* ---- Record reading ---- */

static const uint8_t *zmdb_read_record(ZMDBState *st, uint32_t offset, uint32_t *out_size) {
    if (offset < 4 || offset >= st->data_len) return NULL;

    uint32_t header = rd32(st->data + offset - 4);
    if (header & 0x80000000) return NULL;  /* sign bit must be 0 */

    uint32_t rec_size = header & 0x00FFFFFF;
    if (offset + rec_size > st->data_len) return NULL;

    if (out_size) *out_size = rec_size;
    return st->data + offset;
}

/* ---- Index lookup ---- */

static uint32_t zmdb_lookup(ZMDBState *st, uint32_t atom_id) {
    for (int i = 0; i < st->index_count; i++) {
        if (st->index[i].atom_id == atom_id)
            return st->index[i].record_offset;
    }
    return 0;
}

/* ---- Resolve string reference ---- */

static char *zmdb_resolve_string(ZMDBState *st, uint32_t atom_id) {
    if (!atom_id) return strdup("");

    uint32_t offset = zmdb_lookup(st, atom_id);
    if (!offset) return strdup("");

    uint32_t rec_size = 0;
    const uint8_t *rec = zmdb_read_record(st, offset, &rec_size);
    if (!rec || rec_size < 2) return strdup("");

    int schema = (atom_id >> 24) & 0xFF;

    switch (schema) {
    case SCHEMA_FILENAME:
        if (rec_size > 8) return read_utf8(rec + 8, rec_size - 8);
        break;
    case SCHEMA_GENRE:
        if (rec_size > 1) return read_utf8(rec + 1, rec_size - 1);
        break;
    case SCHEMA_ARTIST: {
        int es = st->is_hd ? ENTRY_SIZE_ARTIST_HD : ENTRY_SIZE_ARTIST_CLASSIC;
        if (rec_size > (uint32_t)es)
            return read_utf8(rec + es, rec_size - es);
        break;
    }
    case SCHEMA_ALBUM: {
        int es = st->is_hd ? ENTRY_SIZE_ALBUM_HD : ENTRY_SIZE_ALBUM_CLASSIC;
        if (rec_size > (uint32_t)es)
            return read_utf8(rec + es, rec_size - es);
        break;
    }
    }

    return strdup("");
}

/* ---- Backwards varint field parser ---- */

/*
 * ZMDB records can have optional fields appended AFTER the null-terminated
 * title string. These are encoded backwards from the end of the record:
 *   - Field ID: 1-2 bytes (high bit set = 2-byte encoding)
 *   - Field size: 1-3 bytes (high bit set = multi-byte)
 *   - Field data: field_size bytes
 *
 * Known field IDs (from XuneSyncLibrary, LGPL-2.1):
 *   0x63 — skip_count (uint32)
 *   0x6C — disc_number (uint32)
 *   0x70 — last_played (uint64, Windows FILETIME)
 *   0x44 — UTF-16LE filename
 *   0x14 — GUID (16 bytes)
 */
typedef struct {
    uint8_t  field_id;
    uint32_t field_size;
    const uint8_t *field_data;
} ZMDBVarintField;

static int zmdb_parse_varint_fields(const uint8_t *rec, uint32_t rec_size,
                                     uint32_t title_start_offset,
                                     ZMDBVarintField *out, int max_fields) {
    /* Find where the title string ends (first null byte after fixed portion) */
    uint32_t title_end = title_start_offset;
    while (title_end < rec_size && rec[title_end] != 0)
        title_end++;
    title_end++; /* skip the null terminator */

    if (title_end >= rec_size)
        return 0; /* no varint data after title */

    /* Parse fields backwards from the end of the record */
    int count = 0;
    uint32_t pos = rec_size;

    while (pos > title_end && count < max_fields) {
        if (pos < title_end + 2) break;

        /* Read field ID (1-2 bytes, from end) */
        pos--;
        uint8_t id_byte1 = rec[pos];
        uint16_t field_id;
        if (id_byte1 & 0x80) {
            if (pos < title_end + 1) break;
            pos--;
            uint8_t id_byte2 = rec[pos];
            field_id = (uint16_t)((id_byte2 << 7) | (id_byte1 & 0x7F));
        } else {
            field_id = id_byte1;
        }

        /* Read field size (1-3 bytes, from end) */
        if (pos < title_end + 1) break;
        pos--;
        uint8_t sz_byte1 = rec[pos];
        uint32_t field_size;
        if (sz_byte1 & 0x80) {
            if (pos < title_end + 1) break;
            pos--;
            uint8_t sz_byte2 = rec[pos];
            if (sz_byte2 & 0x80) {
                if (pos < title_end + 1) break;
                pos--;
                uint8_t sz_byte3 = rec[pos];
                field_size = ((uint32_t)(sz_byte3) << 14) |
                             ((uint32_t)(sz_byte2 & 0x7F) << 7) |
                             (sz_byte1 & 0x7F);
            } else {
                field_size = ((uint32_t)(sz_byte2) << 7) | (sz_byte1 & 0x7F);
            }
        } else {
            field_size = sz_byte1;
        }

        /* Read field data */
        if (field_size > pos - title_end) break;
        pos -= field_size;

        out[count].field_id   = (uint8_t)field_id;
        out[count].field_size = field_size;
        out[count].field_data = rec + pos;
        count++;
    }

    return count;
}

/* ---- Parse track record ---- */

static int zmdb_parse_track(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                             uint32_t atom_id, ZuneTrack *out) {
    int es = st->is_hd ? ENTRY_SIZE_MUSIC_HD : ENTRY_SIZE_MUSIC_CLASSIC;
    if (rec_size < (uint32_t)es) return -1;

    /* Fixed fields */
    uint32_t album_ref    = rd32(rec + 0);
    uint32_t artist_ref   = rd32(rec + 4);
    uint32_t genre_ref    = rd32(rec + 8);
    uint32_t filename_ref = rd32(rec + 12);
    int32_t  duration     = rdi32(rec + 16);
    int32_t  size         = rdi32(rec + 20);
    uint16_t track_num    = rd16(rec + 24);

    /* Skip root entries (all refs zero) */
    if (album_ref == 0 && artist_ref == 0 && genre_ref == 0)
        return -1;

    /* HD-only fixed fields at known offsets */
    uint16_t playcount = 0;
    uint8_t  rating    = 0;
    if (st->is_hd && rec_size >= 32) {
        playcount = rd16(rec + 26);
        /* offset 28-29: codec_id (uint16) — not exposed yet */
        rating    = rec[30];
    }

    /* Title: UTF-8 after fixed portion */
    char *title = (rec_size > (uint32_t)es)
        ? read_utf8(rec + es, rec_size - es)
        : strdup("Unknown");

    out->item_id     = atom_id;
    out->title       = (title && title[0]) ? title : strdup("Unknown");
    if (title && !title[0]) free(title);
    out->artist      = zmdb_resolve_string(st, artist_ref);
    out->album       = zmdb_resolve_string(st, album_ref);
    out->genre       = zmdb_resolve_string(st, genre_ref);
    out->duration_ms = (duration > 0) ? (uint32_t)duration : 0;
    out->filesize    = (size > 0) ? (uint64_t)size : 0;
    out->tracknumber = track_num;
    out->disc_number = 0;
    out->playcount   = playcount;
    out->rating      = rating;
    out->skip_count  = 0;
    out->last_played = 0;

    /* Parse backwards varint extras (skip count, disc number, last played) */
    ZMDBVarintField fields[8];
    int nfields = zmdb_parse_varint_fields(rec, rec_size, (uint32_t)es, fields, 8);
    for (int i = 0; i < nfields; i++) {
        switch (fields[i].field_id) {
        case 0x63: /* skip_count */
            if (fields[i].field_size >= 4)
                out->skip_count = rd32(fields[i].field_data);
            break;
        case 0x6C: /* disc_number */
            if (fields[i].field_size >= 4)
                out->disc_number = (uint16_t)rd32(fields[i].field_data);
            else if (fields[i].field_size >= 2)
                out->disc_number = rd16(fields[i].field_data);
            break;
        case 0x70: /* last_played timestamp (FILETIME) */
            if (fields[i].field_size >= 8)
                out->last_played = rd64(fields[i].field_data);
            break;
        }
    }

    (void)filename_ref;
    return 0;
}

/* ---- Parse video record ---- */

static int zmdb_parse_video(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                             uint32_t atom_id, ZuneVideoFile *out) {
    if (rec_size < ENTRY_SIZE_VIDEO) return -1;

    uint32_t folder_ref   = rd32(rec + 0);
    uint32_t title_ref    = rd32(rec + 4);
    uint32_t filename_ref = rd32(rec + 12);

    /* Skip root entries */
    if (folder_ref == 0 && title_ref == 0 && filename_ref == 0)
        return -1;

    out->item_id   = atom_id;
    out->parent_id = 0;

    /* Video title lives at offset +40 as UTF-8 (same pattern as music tracks).
     * filenameRef at +12 is always 0, titleRef at +4 resolves empty
     * (VideoTitle schema 0x0A not handled by resolver). */
    if (rec_size > 40)
        out->filename = read_utf8(rec + 40, rec_size - 40);
    else
        out->filename = strdup("");
    out->title = strdup(out->filename ? out->filename : "");
    /* This record exposes Name, not the on-disk ObjectFileName. */
    out->object_filename = strdup("");
    out->object_format = rec_size >= 34 ? rd16(rec + 32) : 0;

    /* Filesize at +12 (treating as direct value, not a reference).
     * B1 guard: MTP objects cap at 4GB (32-bit ObjectCompressedSize);
     * anything larger read here is a misparsed field, not a size. */
    if (rec_size >= 20)
        out->filesize = (uint64_t)rd32(rec + 16);
    else
        out->filesize = 0;
    if (out->filesize > 0xFFFFFF00ull)
        out->filesize = 0;

    /* Format code at +32 (0xB981=WMV, 0xB982=MP4) */
    /* MetaGenre equivalent at +36 — discovered via hex analysis:
     *   0x0002 = Movie  (MTP MetaGenre 0x25)
     *   0x0004 = TV Show (MTP MetaGenre 0x26)
     *   0x0001 = Music Video? (MTP MetaGenre 0x23) — unconfirmed
     *   other  = Other (MTP MetaGenre 0x21)
     */
    /* Video record fixed layout (40 bytes):
     *   +0:  folderRef (uint32)     +4:  titleRef (uint32)
     *   +8:  unknown (uint32)       +12: filenameRef (uint32)
     *   +16: filesize? (uint32)     +20: zeros (12 bytes)
     *   +32: formatCode (uint16) — 0xB981=WMV, 0xB982=MP4
     *   +34: padding (uint16)       +36: padding (uint16)
     *   +38: zuneType (uint16) — MetaGenre equivalent
     */
    if (rec_size >= 40) {
        uint16_t zune_type = rd16(rec + 38);
        switch (zune_type) {
        case 0x0002: out->metagenre = 0x25; break;  /* Movie */
        case 0x0004: out->metagenre = 0x26; break;  /* TV Show — confirmed via diagnostic */
        case 0x0001: out->metagenre = 0x23; break;  /* Music Video (still unconfirmed) */
        default:     out->metagenre = 0x21; break;  /* Other */
        }
    } else {
        out->metagenre = 0x21;
    }

    return 0;
}

/* ---- Parse picture record ---- */

static int zmdb_parse_picture(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                               uint32_t atom_id, ZunePhotoFile *out) {
    if (rec_size < ENTRY_SIZE_PICTURE) return -1;

    uint32_t filename_ref = rd32(rec + 8);

    /* Skip root entries */
    if (rd32(rec + 0) == 0 && rd32(rec + 4) == 0 && filename_ref == 0)
        return -1;

    out->item_id   = atom_id;
    out->filename  = zmdb_resolve_string(st, filename_ref);
    /* B1 guard: on the 80/120 generation +12 is a FILETIME, not a
     * size — interpreting it as bytes summed to petabytes of phantom
     * photos. No real Zune photo approaches 1GB; larger = misparse. */
    out->filesize  = (uint64_t)rdi32(rec + 12);
    if (out->filesize > (1024ull * 1024 * 1024))
        out->filesize = 0;
    out->parent_id = rd32(rec + 0);
    return 0;
}

/* ---- Parse album record ---- */

static int zmdb_parse_album(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                             uint32_t atom_id, ZuneDBAlbum *out) {
    int es = st->is_hd ? ENTRY_SIZE_ALBUM_HD : ENTRY_SIZE_ALBUM_CLASSIC;
    if (rec_size < (uint32_t)es) return -1;

    uint32_t artist_ref = rd32(rec + 0);

    /* Skip root entries */
    if (artist_ref == 0 && rec_size <= (uint32_t)es)
        return -1;

    out->album_id = atom_id;
    out->artist   = zmdb_resolve_string(st, artist_ref);
    out->title    = (rec_size > (uint32_t)es)
        ? read_utf8(rec + es, rec_size - es)
        : strdup("Unknown Album");
    out->year     = 0;

    /* HD: extract year from FILETIME at +12 */
    if (st->is_hd && rec_size >= 20) {
        uint64_t ticks = (uint64_t)rd32(rec + 12) | ((uint64_t)rd32(rec + 16) << 32);
        if (ticks > 0) {
            int64_t unix_ts = (int64_t)(ticks / 10000000ULL) - 11644473600LL;
            struct tm *tm = gmtime((time_t *)&unix_ts);
            if (tm) out->year = (uint16_t)(tm->tm_year + 1900);
        }
    }

    return 0;
}

/* ---- Parse artist record ---- */

static int zmdb_parse_artist(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                              uint32_t atom_id, ZuneDBArtist *out) {
    int es = st->is_hd ? ENTRY_SIZE_ARTIST_HD : ENTRY_SIZE_ARTIST_CLASSIC;
    if (rec_size < (uint32_t)es) return -1;

    out->artist_id = atom_id;
    out->name = (rec_size > (uint32_t)es)
        ? read_utf8(rec + es, rec_size - es)
        : strdup("Unknown Artist");

    /* Skip empty/root entries */
    if (!out->name || !out->name[0]) {
        free(out->name);
        return -1;
    }

    return 0;
}

/* ---- Parse photo album record ---- */

static int zmdb_parse_photo_album(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                                    uint32_t atom_id, ZuneDBPhotoAlbum *out) {
    (void)st;
    /* PhotoAlbum record: schema 0x0B. Layout TBD — try name after small fixed header. */
    if (rec_size < 4) return -1;

    out->album_id = atom_id;

    /* Try name after 4 bytes, then after 8 bytes, then after 1 byte */
    if (rec_size > 8) {
        char *name = read_utf8(rec + 8, rec_size - 8);
        if (name && name[0]) { out->name = name; return 0; }
        free(name);
    }
    if (rec_size > 4) {
        char *name = read_utf8(rec + 4, rec_size - 4);
        if (name && name[0]) { out->name = name; return 0; }
        free(name);
    }
    if (rec_size > 1) {
        char *name = read_utf8(rec + 1, rec_size - 1);
        if (name && name[0]) { out->name = name; return 0; }
        free(name);
    }

    out->name = strdup("Unknown Album");
    return 0;
}

/* ---- Discovery-based descriptor mapping ---- */

typedef struct {
    int music;         /* first descriptor with SCHEMA_MUSIC */
    int video;         /* first with SCHEMA_VIDEO */
    int photo;         /* first with SCHEMA_PICTURE */
    int album;         /* first with SCHEMA_ALBUM */
    int playlist;      /* first with SCHEMA_PLAYLIST */
    int artist;        /* first with SCHEMA_ARTIST */
    int genre;         /* first with SCHEMA_GENRE */
    int photo_album;   /* first with SCHEMA_PHOTOALBUM */
} ZMDBDiscovered;

static void zmdb_discover(ZMDBState *st, const uint8_t *data, size_t data_len,
                           ZMDBDiscovered *disc) {
    disc->music = disc->video = disc->photo = -1;
    disc->album = disc->playlist = disc->artist = -1;
    disc->genre = disc->photo_album = -1;

    for (int i = 1; i < 96; i++) {  /* skip desc 0 (index) */
        ZMDBDescriptor *d = &st->descs[i];
        if (d->entry_count == 0) continue;
        if (d->data_offset + 4 > data_len) continue;

        uint32_t first_atom = rd32(data + d->data_offset);
        int schema = (first_atom >> 24) & 0xFF;

        /* Store FIRST descriptor found for each schema */
        switch (schema) {
        case SCHEMA_MUSIC:      if (disc->music < 0)       disc->music = i;       break;
        case SCHEMA_VIDEO:      if (disc->video < 0)       disc->video = i;       break;
        case SCHEMA_PICTURE:    if (disc->photo < 0)       disc->photo = i;       break;
        case SCHEMA_ALBUM:      if (disc->album < 0)       disc->album = i;       break;
        case SCHEMA_PLAYLIST:   if (disc->playlist < 0)    disc->playlist = i;    break;
        case SCHEMA_ARTIST:     if (disc->artist < 0)      disc->artist = i;      break;
        case SCHEMA_GENRE:      if (disc->genre < 0)       disc->genre = i;       break;
        case SCHEMA_PHOTOALBUM: if (disc->photo_album < 0) disc->photo_album = i; break;
        }
    }
}

/* ---- Main parser ---- */

static ZuneLibrary *zmdb_parse(const uint8_t *data, size_t data_len) {
    if (data_len < 0x40) return NULL;

    /* Verify ZMDB magic */
    if (memcmp(data, "ZMDB", 4) != 0) {
        fprintf(stderr, "[libzune-zmdb] bad magic\n");
        return NULL;
    }

    /* Verify ZMed header and detect version */
    if (memcmp(data + 0x20, "ZMed", 4) != 0) {
        fprintf(stderr, "[libzune-zmdb] missing ZMed header\n");
        return NULL;
    }
    uint16_t version = rd16(data + 0x24);
    int is_hd = (version >= 5);
    fprintf(stderr, "[libzune-zmdb] ZMed version=%d, is_hd=%d\n", version, is_hd);

    /* Find ZArr descriptor block (search 0x30 to 0x100) */
    uint32_t zarr_offset = 0;
    for (uint32_t off = 0x30; off < 0x100 && off + 4 <= data_len; off += 4) {
        if (memcmp(data + off, "ZArr", 4) == 0) {
            zarr_offset = off;
            break;
        }
    }
    if (!zarr_offset) {
        fprintf(stderr, "[libzune-zmdb] ZArr not found\n");
        return NULL;
    }

    /* Initialize parser state */
    ZMDBState st = {0};
    st.data = data;
    st.data_len = data_len;
    st.is_hd = is_hd;

    /* Parse 96 descriptors (20 bytes each) */
    for (int i = 0; i < 96; i++) {
        uint32_t doff = zarr_offset + (i * 20);
        if (doff + 20 > data_len) break;
        st.descs[i].entry_size  = rd16(data + doff + 6);
        st.descs[i].entry_count = rd32(data + doff + 8);
        st.descs[i].data_offset = rd32(data + doff + 16);
    }

    /* Build index table from descriptor 0 */
    if (st.descs[0].entry_count > 0 && st.descs[0].entry_size == 8) {
        st.index_count = (int)st.descs[0].entry_count;
        st.index = calloc(st.index_count, sizeof(ZMDBIndexEntry));
        if (st.index) {
            for (int i = 0; i < st.index_count; i++) {
                uint32_t eoff = st.descs[0].data_offset + (i * 8);
                if (eoff + 8 > data_len) { st.index_count = i; break; }
                st.index[i].atom_id       = rd32(data + eoff);
                st.index[i].record_offset = rd32(data + eoff + 4);
            }
        }
    }
    fprintf(stderr, "[libzune-zmdb] %d index entries\n", st.index_count);

    /* Allocate result */
    ZuneLibrary *lib = calloc(1, sizeof(ZuneLibrary));
    if (!lib) { free(st.index); return NULL; }

    /* Pre-allocate arrays (we'll realloc down later) */
    int max_tracks = 10000, max_videos = 1000, max_photos = 5000;
    lib->tracks = calloc(max_tracks, sizeof(ZuneTrack));
    lib->videos = calloc(max_videos, sizeof(ZuneVideoFile));
    lib->photos = calloc(max_photos, sizeof(ZunePhotoFile));

    if (!lib->tracks || !lib->videos || !lib->photos) {
        free(lib->tracks); free(lib->videos); free(lib->photos);
        free(lib); free(st.index);
        return NULL;
    }

    /* Walk descriptors and parse media */
    const ZMDBDescMap *desc_map = is_hd ? HD_DESC_MAP : CLASSIC_DESC_MAP;

    for (int m = 0; desc_map[m].desc_idx >= 0; m++) {
        int di = desc_map[m].desc_idx;
        int schema = desc_map[m].schema;
        if (di >= 96) continue;

        ZMDBDescriptor *desc = &st.descs[di];
        if (!desc->entry_count) continue;

        for (uint32_t i = 0; i < desc->entry_count; i++) {
            uint32_t eoff = desc->data_offset + (i * desc->entry_size);
            if (eoff + 4 > data_len) break;

            uint32_t atom_id = rd32(data + eoff);
            int atom_schema = (atom_id >> 24) & 0xFF;
            if (atom_schema != schema) continue;

            uint32_t rec_offset = zmdb_lookup(&st, atom_id);
            if (!rec_offset) continue;

            uint32_t rec_size = 0;
            const uint8_t *rec = zmdb_read_record(&st, rec_offset, &rec_size);
            if (!rec) continue;

            switch (schema) {
            case SCHEMA_MUSIC:
                if (lib->track_count < max_tracks) {
                    if (zmdb_parse_track(&st, rec, rec_size, atom_id,
                                          &lib->tracks[lib->track_count]) == 0)
                        lib->track_count++;
                }
                break;
            case SCHEMA_VIDEO:
                /* HD video records have a different (larger, variable) layout
                 * than Classic's 40-byte fixed records. The Classic parser
                 * misreads offset +38 on HD and tags everything as "Other".
                 * Skip on HD; AppState falls back to live MTP reads. */
                if (st.is_hd) break;
                if (lib->video_count < max_videos) {
                    if (zmdb_parse_video(&st, rec, rec_size, atom_id,
                                          &lib->videos[lib->video_count]) == 0)
                        lib->video_count++;
                }
                break;
            case SCHEMA_PICTURE:
                if (lib->photo_count < max_photos) {
                    if (zmdb_parse_picture(&st, rec, rec_size, atom_id,
                                            &lib->photos[lib->photo_count]) == 0)
                        lib->photo_count++;
                }
                break;
            }
        }
    }

    free(st.index);

    fprintf(stderr, "[libzune-zmdb] parsed %d tracks, %d videos, %d photos\n",
            lib->track_count, lib->video_count, lib->photo_count);
    return lib;
}

/* ==================================================================
 * ZMDB USB I/O helpers
 *
 * The ZMDB read uses vendor opcode 0x1792, but it is NOT a standard
 * PTP transaction. It is a raw 16-byte bulk request followed by a
 * raw bulk response (12-byte header + payload in 64KB chunks).
 * We use the zune_usb_handle_t backend directly for these transfers.
 * ================================================================== */

/*
 * zmdb_usb_read — Send the 16-byte ZMDB request and read back the
 * full payload. Returns an allocated payload buffer (caller frees)
 * and sets *out_len to the number of valid bytes received.
 *
 * Returns 0 on success, -1 on error.
 */
static int zmdb_usb_read(zune_usb_handle_t *usb, uint8_t **out_payload,
                          uint32_t *out_len) {
    const zune_usb_backend_t *be = usb->backend;
    uint8_t outep = usb->bulk_out_ep;
    uint8_t inep  = usb->bulk_in_ep;

    fprintf(stderr, "[libzune-zmdb] using endpoints: OUT=0x%02x, IN=0x%02x\n",
            outep, inep);

    /* Step 1: Build 16-byte ZMDB request */
    uint8_t request[16] = {0};
    request[0]  = 0x10;    /* length (16) */
    request[4]  = 0x01;    /* command marker */
    request[6]  = 0x17;    /* opcode high byte */
    request[7]  = 0x92;    /* opcode low byte -> 0x1792 */
    request[8]  = 0x03;    /* music library object ID */
    request[9]  = 0x92;
    request[10] = 0x1f;
    request[12] = 0x01;    /* trailer */

    /* Step 2: Send request via bulk OUT */
    uint32_t transferred = 0;
    int ret = be->bulk_write(usb, outep, request, 16, &transferred, 5000);
    if (ret != 0 || transferred != 16) {
        zune_set_error("ZMDB request send failed (transferred=%u)", transferred);
        return -1;
    }
    fprintf(stderr, "[libzune-zmdb] request sent (16 bytes)\n");

    /* Step 3: Wait 250ms for device to prepare response */
    usleep(250000);

    /* Step 4: Read first chunk (header + initial payload, up to 64KB) */
    uint8_t *first_chunk = malloc(65536);
    if (!first_chunk) {
        zune_set_error("Out of memory for ZMDB first chunk");
        return -1;
    }

    transferred = 0;
    ret = be->bulk_read(usb, inep, first_chunk, 65536, &transferred, 5000);
    if (ret != 0 || transferred < 12) {
        free(first_chunk);
        zune_set_error("ZMDB header read failed (got %u bytes)", transferred);
        return -1;
    }

    /* Step 5: Parse header — totalSize at offset 0 */
    uint32_t total_size = rd32(first_chunk);
    if (total_size <= 12 || total_size > 64 * 1024 * 1024) {
        free(first_chunk);
        zune_set_error("ZMDB invalid total size: %u", total_size);
        return -1;
    }

    uint32_t payload_size = total_size - 12;
    fprintf(stderr, "[libzune-zmdb] totalSize=%u, payloadSize=%u, first chunk=%u bytes\n",
            total_size, payload_size, transferred);

    /* Step 6: Allocate full payload buffer and copy what we already have */
    uint8_t *payload = malloc(payload_size);
    if (!payload) {
        free(first_chunk);
        zune_set_error("Out of memory for ZMDB payload (%u bytes)", payload_size);
        return -1;
    }

    /* The first chunk contains 12-byte header + initial payload data */
    uint32_t first_payload = transferred - 12;
    if (first_payload > payload_size) first_payload = payload_size;
    memcpy(payload, first_chunk + 12, first_payload);
    free(first_chunk);

    /* Step 7: Read remaining payload in 64KB chunks */
    uint32_t received = first_payload;
    while (received < payload_size) {
        uint32_t chunk = payload_size - received;
        if (chunk > 65536) chunk = 65536;

        transferred = 0;
        ret = be->bulk_read(usb, inep, payload + received, chunk,
                            &transferred, 10000);
        if (ret != 0) {
            fprintf(stderr, "[libzune-zmdb] read error at %u/%u\n",
                    received, payload_size);
            break;
        }
        if (transferred == 0) break;
        received += transferred;
    }

    /* Step 8: Drain residual data (race 512-byte read against 100ms timeout).
     * Matches zune-explorer: errors are expected and silently ignored. */
    uint8_t drain[512];
    uint32_t drain_transferred = 0;
    be->bulk_read(usb, inep, drain, sizeof(drain), &drain_transferred, 100);

    fprintf(stderr, "[libzune-zmdb] received %u/%u bytes\n", received, payload_size);

    if (received < payload_size / 2) {
        free(payload);
        zune_set_error("ZMDB read incomplete: got %u of %u bytes", received, payload_size);
        return -1;
    }

    *out_payload = payload;
    *out_len = received;
    return 0;
}

/* ---- Public API: Read ZMDB from device ---- */

int zune_infiltrate_legacy(ZuneDevice *dev, ZuneLibrary **out) {
    if (!dev || !out) {
        zune_set_error("Invalid parameters for ZMDB read");
        return -1;
    }

    *out = NULL;

    uint8_t *payload = NULL;
    uint32_t received = 0;
    int ret = zmdb_usb_read(&dev->usb, &payload, &received);
    if (ret != 0) return -1;

    /* Parse the ZMDB binary data */
    ZuneLibrary *lib = zmdb_parse(payload, received);
    free(payload);

    if (!lib) {
        zune_set_error("ZMDB parse failed");
        return -1;
    }

    *out = lib;
    return 0;
}

/* ---- ZuneDB: Full library scan with discovery-based parsing ---- */

static ZuneDBLibrary *zmdb_parse_full(const uint8_t *data, size_t data_len) {
    if (data_len < 0x40) return NULL;

    if (memcmp(data, "ZMDB", 4) != 0) {
        fprintf(stderr, "[zunedb] bad magic\n");
        return NULL;
    }
    if (memcmp(data + 0x20, "ZMed", 4) != 0) {
        fprintf(stderr, "[zunedb] missing ZMed header\n");
        return NULL;
    }

    uint16_t version = rd16(data + 0x24);
    int is_hd = (version >= 5);

    /* Find ZArr */
    uint32_t zarr_offset = 0;
    for (uint32_t off = 0x30; off < 0x100 && off + 4 <= data_len; off += 4) {
        if (memcmp(data + off, "ZArr", 4) == 0) {
            zarr_offset = off;
            break;
        }
    }
    if (!zarr_offset) {
        fprintf(stderr, "[zunedb] ZArr not found\n");
        return NULL;
    }

    /* Init parser state */
    ZMDBState st = {0};
    st.data = data;
    st.data_len = data_len;
    st.is_hd = is_hd;

    for (int i = 0; i < 96; i++) {
        uint32_t doff = zarr_offset + (i * 20);
        if (doff + 20 > data_len) break;
        st.descs[i].entry_size  = rd16(data + doff + 6);
        st.descs[i].entry_count = rd32(data + doff + 8);
        st.descs[i].data_offset = rd32(data + doff + 16);
    }

    /* Build index from descriptor 0 */
    if (st.descs[0].entry_count > 0 && st.descs[0].entry_size == 8) {
        st.index_count = (int)st.descs[0].entry_count;
        st.index = calloc(st.index_count, sizeof(ZMDBIndexEntry));
        if (st.index) {
            for (int i = 0; i < st.index_count; i++) {
                uint32_t eoff = st.descs[0].data_offset + (i * 8);
                if (eoff + 8 > data_len) { st.index_count = i; break; }
                st.index[i].atom_id       = rd32(data + eoff);
                st.index[i].record_offset = rd32(data + eoff + 4);
            }
        }
    }

    /* Discover descriptors dynamically */
    ZMDBDiscovered disc;
    zmdb_discover(&st, data, data_len, &disc);

    /* Allocate library */
    ZuneDBLibrary *lib = calloc(1, sizeof(ZuneDBLibrary));
    if (!lib) { free(st.index); return NULL; }
    lib->is_hd = is_hd;

    /* Pre-allocate arrays */
    int max_tracks = 10000, max_videos = 1000, max_photos = 5000;
    int max_albums = 2000, max_artists = 2000, max_playlists = 200;
    int max_photo_albums = 100;

    lib->tracks       = calloc(max_tracks, sizeof(ZuneTrack));
    lib->videos       = calloc(max_videos, sizeof(ZuneVideoFile));
    lib->photos       = calloc(max_photos, sizeof(ZunePhotoFile));
    lib->albums       = calloc(max_albums, sizeof(ZuneDBAlbum));
    lib->artists      = calloc(max_artists, sizeof(ZuneDBArtist));
    lib->playlists    = calloc(max_playlists, sizeof(ZunePlaylist));
    lib->photo_albums = calloc(max_photo_albums, sizeof(ZuneDBPhotoAlbum));

    if (!lib->tracks || !lib->videos || !lib->photos) {
        /* Critical arrays failed */
        free(lib->tracks); free(lib->videos); free(lib->photos);
        free(lib->albums); free(lib->artists); free(lib->playlists);
        free(lib->photo_albums);
        free(lib); free(st.index);
        return NULL;
    }

    /* Helper: iterate a descriptor and parse each entry */
    #define PARSE_DESC(desc_idx, schema_id, parse_fn, array, count, max, type) \
        if ((desc_idx) >= 0 && (desc_idx) < 96) { \
            ZMDBDescriptor *_d = &st.descs[(desc_idx)]; \
            for (uint32_t _i = 0; _i < _d->entry_count; _i++) { \
                uint32_t _eoff = _d->data_offset + (_i * _d->entry_size); \
                if (_eoff + 4 > data_len) break; \
                uint32_t _aid = rd32(data + _eoff); \
                if (((_aid >> 24) & 0xFF) != (schema_id)) continue; \
                uint32_t _roff = zmdb_lookup(&st, _aid); \
                if (!_roff) continue; \
                uint32_t _rsz = 0; \
                const uint8_t *_rec = zmdb_read_record(&st, _roff, &_rsz); \
                if (!_rec) continue; \
                if ((count) < (max)) { \
                    if (parse_fn(&st, _rec, _rsz, _aid, &(array)[(count)]) == 0) \
                        (count)++; \
                } \
            } \
        }

    PARSE_DESC(disc.music, SCHEMA_MUSIC, zmdb_parse_track, lib->tracks, lib->track_count, max_tracks, ZuneTrack);
    /* HD video records have a different layout than Classic's 40-byte
     * fixed records; zmdb_parse_video would misread them and tag every
     * video as "Other". Skip on HD — AppState falls back to live MTP reads. */
    if (!is_hd) {
        PARSE_DESC(disc.video, SCHEMA_VIDEO, zmdb_parse_video, lib->videos, lib->video_count, max_videos, ZuneVideoFile);
    }
    PARSE_DESC(disc.photo, SCHEMA_PICTURE, zmdb_parse_picture, lib->photos, lib->photo_count, max_photos, ZunePhotoFile);
    PARSE_DESC(disc.album, SCHEMA_ALBUM, zmdb_parse_album, lib->albums, lib->album_count, max_albums, ZuneDBAlbum);
    PARSE_DESC(disc.artist, SCHEMA_ARTIST, zmdb_parse_artist, lib->artists, lib->artist_count, max_artists, ZuneDBArtist);
    PARSE_DESC(disc.photo_album, SCHEMA_PHOTOALBUM, zmdb_parse_photo_album, lib->photo_albums, lib->photo_album_count, max_photo_albums, ZuneDBPhotoAlbum);

    #undef PARSE_DESC

    /* Playlists: hex dump for research, then fall back to MTP internally later */
    if (disc.playlist >= 0 && disc.playlist < 96) {
        ZMDBDescriptor *pd = &st.descs[disc.playlist];
        for (uint32_t i = 0; i < pd->entry_count && i < 5; i++) {
            uint32_t eoff = pd->data_offset + (i * pd->entry_size);
            if (eoff + 4 > data_len) break;
            uint32_t aid = rd32(data + eoff);
            if (((aid >> 24) & 0xFF) != SCHEMA_PLAYLIST) continue;
            uint32_t roff = zmdb_lookup(&st, aid);
            if (!roff) continue;
            uint32_t rsz = 0;
            const uint8_t *rec = zmdb_read_record(&st, roff, &rsz);
            if (!rec) continue;
            fprintf(stderr, "[zunedb] playlist record atom=%08X size=%u\n", aid, rsz);
            /* Hex dump first 64 bytes for analysis */
            uint32_t dlen = rsz < 64 ? rsz : 64;
            for (uint32_t b = 0; b < dlen; b++) {
                if (b % 16 == 0) fprintf(stderr, "  %04x: ", b);
                fprintf(stderr, "%02x ", rec[b]);
                if (b % 16 == 15 || b == dlen - 1) fprintf(stderr, "\n");
            }
        }
    }

    free(st.index);

    fprintf(stderr, "[zunedb] scanned: %d tracks, %d videos, %d photos, "
            "%d albums, %d artists, %d playlists, %d photo albums\n",
            lib->track_count, lib->video_count, lib->photo_count,
            lib->album_count, lib->artist_count, lib->playlist_count,
            lib->photo_album_count);
    return lib;
}

int zune_infiltrate(ZuneDevice *dev, ZuneDBLibrary **out) {
    if (!dev || !out) {
        zune_set_error("Invalid parameters for ZuneDB scan");
        return -1;
    }
    *out = NULL;

    uint8_t *payload = NULL;
    uint32_t received = 0;
    int ret = zmdb_usb_read(&dev->usb, &payload, &received);
    if (ret != 0) return -1;

    /* Parse */
    ZuneDBLibrary *lib = zmdb_parse_full(payload, received);
    free(payload);

    if (!lib) {
        zune_set_error("ZuneDB parse failed");
        return -1;
    }

    /* Playlists: internal MTP fallback (ZMDB playlist format not yet decoded) */
    if (lib->playlist_count == 0) {
        int pl_count = 0;
        ZunePlaylist *pls = zune_get_playlists(dev, &pl_count);
        if (pls && pl_count > 0) {
            free(lib->playlists);
            lib->playlists = pls;
            lib->playlist_count = pl_count;
        }
    }

    /* Cache for duplicate detection (zune_find_track/video/photo) */
    dev->cached_library = lib;

    *out = lib;
    return 0;
}

void zune_free_scan(ZuneDBLibrary *lib) {
    if (!lib) return;
    if (lib->tracks) {
        for (int i = 0; i < lib->track_count; i++) {
            free(lib->tracks[i].title);
            free(lib->tracks[i].artist);
            free(lib->tracks[i].album);
            free(lib->tracks[i].genre);
        }
        free(lib->tracks);
    }
    zune_free_videos(lib->videos, lib->video_count);
    if (lib->photos) {
        for (int i = 0; i < lib->photo_count; i++)
            free(lib->photos[i].filename);
        free(lib->photos);
    }
    if (lib->playlists) {
        for (int i = 0; i < lib->playlist_count; i++) {
            free(lib->playlists[i].name);
            free(lib->playlists[i].track_ids);
        }
        free(lib->playlists);
    }
    if (lib->albums) {
        for (int i = 0; i < lib->album_count; i++) {
            free(lib->albums[i].title);
            free(lib->albums[i].artist);
        }
        free(lib->albums);
    }
    if (lib->artists) {
        for (int i = 0; i < lib->artist_count; i++)
            free(lib->artists[i].name);
        free(lib->artists);
    }
    if (lib->photo_albums) {
        for (int i = 0; i < lib->photo_album_count; i++)
            free(lib->photo_albums[i].name);
        free(lib->photo_albums);
    }
    free(lib);
}

/* ---- Research/Debug: Dump raw ZMDB binary to file ---- */

int zune_dump_raw(ZuneDevice *dev, const char *output_path) {
    if (!dev || !output_path) return -1;

    const zune_usb_backend_t *be = dev->usb.backend;
    uint8_t outep = dev->usb.bulk_out_ep;
    uint8_t inep  = dev->usb.bulk_in_ep;

    /* Send ZMDB request */
    uint8_t request[16] = {0};
    request[0] = 0x10; request[4] = 0x01;
    request[6] = 0x17; request[7] = 0x92;
    request[8] = 0x03; request[9] = 0x92; request[10] = 0x1f;
    request[12] = 0x01;

    uint32_t transferred = 0;
    if (be->bulk_write(&dev->usb, outep, request, 16, &transferred, 5000) != 0)
        return -1;

    usleep(250000);

    /* Read first chunk */
    uint8_t *first = malloc(65536);
    if (!first) return -1;
    transferred = 0;
    if (be->bulk_read(&dev->usb, inep, first, 65536, &transferred, 5000) != 0 ||
        transferred < 12) {
        free(first); return -1;
    }

    uint32_t total = rd32(first);
    uint32_t psize = total - 12;

    /* Write the FULL response (header + payload) to file */
    FILE *fp = fopen(output_path, "wb");
    if (!fp) { free(first); return -1; }

    /* Write the 12-byte header */
    fwrite(first, 1, 12, fp);

    /* Write initial payload */
    uint32_t initial = transferred - 12;
    if (initial > psize) initial = psize;
    fwrite(first + 12, 1, initial, fp);
    free(first);

    /* Read remaining chunks */
    uint32_t received = initial;
    uint8_t *chunk = malloc(65536);
    if (chunk) {
        while (received < psize) {
            uint32_t want = psize - received;
            if (want > 65536) want = 65536;
            transferred = 0;
            int r = be->bulk_read(&dev->usb, inep, chunk, want, &transferred, 10000);
            if (r != 0 || transferred == 0) break;
            fwrite(chunk, 1, transferred, fp);
            received += transferred;
        }
        free(chunk);
    }

    fclose(fp);

    /* Drain */
    uint8_t drain[512];
    uint32_t dt = 0;
    be->bulk_read(&dev->usb, inep, drain, 512, &dt, 100);

    fprintf(stderr, "[zunedb] dumped %u bytes (payload=%u) to %s\n",
            received + 12, received, output_path);
    return 0;
}

/* ---- Research/Debug: Scan all 96 descriptors ---- */

int zune_infiltrate_deep(ZuneDevice *dev) {
    if (!dev) return -1;

    /* Read ZMDB payload using shared helper */
    uint8_t *payload = NULL;
    uint32_t received = 0;
    int ret = zmdb_usb_read(&dev->usb, &payload, &received);
    if (ret != 0) return -1;

    /* Parse headers */
    if (received < 0x40 || memcmp(payload, "ZMDB", 4) != 0) {
        free(payload);
        fprintf(stderr, "[zunedb] not a ZMDB (bad magic or too small)\n");
        return -1;
    }

    uint16_t version = rd16(payload + 0x24);
    fprintf(stderr, "\n[zunedb] === DESCRIPTOR SCAN ===\n");
    fprintf(stderr, "[zunedb] ZMed version: %d (%s)\n", version, version >= 5 ? "HD" : "Classic");
    fprintf(stderr, "[zunedb] Payload: %u bytes\n\n", received);

    /* Find ZArr */
    uint32_t zarr = 0;
    for (uint32_t off = 0x30; off < 0x100 && off + 4 <= received; off += 4) {
        if (memcmp(payload + off, "ZArr", 4) == 0) { zarr = off; break; }
    }
    if (!zarr) { free(payload); fprintf(stderr, "[zunedb] ZArr not found\n"); return -1; }

    fprintf(stderr, "[zunedb] ZArr at offset 0x%04x\n\n", zarr);
    fprintf(stderr, "  Desc  EntrySize  EntryCount  DataOffset  FirstAtomSchema\n");
    fprintf(stderr, "  ----  ---------  ----------  ----------  ---------------\n");

    /* Scan all 96 descriptors */
    for (int i = 0; i < 96; i++) {
        uint32_t doff = zarr + (i * 20);
        if (doff + 20 > received) break;

        uint16_t entry_size  = rd16(payload + doff + 6);
        uint32_t entry_count = rd32(payload + doff + 8);
        uint32_t data_offset = rd32(payload + doff + 16);

        if (entry_count == 0) continue;  /* skip empty descriptors */

        /* Peek at first atom to determine schema */
        char schema_str[32] = "???";
        if (data_offset + 4 <= received) {
            uint32_t first_atom = rd32(payload + data_offset);
            uint8_t schema_byte = (first_atom >> 24) & 0xFF;
            switch (schema_byte) {
                case 0x01: snprintf(schema_str, sizeof(schema_str), "Music (0x%02x)", schema_byte); break;
                case 0x02: snprintf(schema_str, sizeof(schema_str), "Video (0x%02x)", schema_byte); break;
                case 0x03: snprintf(schema_str, sizeof(schema_str), "Picture (0x%02x)", schema_byte); break;
                case 0x05: snprintf(schema_str, sizeof(schema_str), "Filename (0x%02x)", schema_byte); break;
                case 0x06: snprintf(schema_str, sizeof(schema_str), "Album (0x%02x)", schema_byte); break;
                case 0x07: snprintf(schema_str, sizeof(schema_str), "Playlist (0x%02x)", schema_byte); break;
                case 0x08: snprintf(schema_str, sizeof(schema_str), "Artist (0x%02x)", schema_byte); break;
                case 0x09: snprintf(schema_str, sizeof(schema_str), "Genre (0x%02x)", schema_byte); break;
                case 0x0A: snprintf(schema_str, sizeof(schema_str), "VideoTitle (0x%02x)", schema_byte); break;
                case 0x0B: snprintf(schema_str, sizeof(schema_str), "PhotoAlbum (0x%02x)", schema_byte); break;
                case 0x0C: snprintf(schema_str, sizeof(schema_str), "Collection (0x%02x)", schema_byte); break;
                case 0x0F: snprintf(schema_str, sizeof(schema_str), "PodcastShow (0x%02x)", schema_byte); break;
                case 0x10: snprintf(schema_str, sizeof(schema_str), "PodcastEp (0x%02x)", schema_byte); break;
                case 0x11: snprintf(schema_str, sizeof(schema_str), "AudiobkTitle (0x%02x)", schema_byte); break;
                case 0x12: snprintf(schema_str, sizeof(schema_str), "AudiobkTrack (0x%02x)", schema_byte); break;
                default:   snprintf(schema_str, sizeof(schema_str), "Unknown (0x%02x)", schema_byte); break;
            }
        }

        fprintf(stderr, "  %3d   %7u    %8u    0x%08x  %s\n",
                i, entry_size, entry_count, data_offset, schema_str);
    }

    /* Dump first 3 video records in hex for analysis */
    fprintf(stderr, "\n[zunedb] === SAMPLE VIDEO RECORDS ===\n");
    int videos_shown = 0;
    for (int i = 0; i < 96 && videos_shown < 3; i++) {
        uint32_t doff = zarr + (i * 20);
        if (doff + 20 > received) break;
        uint16_t entry_size  = rd16(payload + doff + 6);
        uint32_t entry_count = rd32(payload + doff + 8);
        uint32_t data_offset = rd32(payload + doff + 16);
        if (entry_count == 0) continue;
        if (data_offset + 4 > received) continue;

        uint32_t first_atom = rd32(payload + data_offset);
        if (((first_atom >> 24) & 0xFF) != 0x02) continue;  /* Not video */

        for (uint32_t j = 0; j < entry_count && videos_shown < 3; j++) {
            uint32_t eoff = data_offset + (j * entry_size);
            if (eoff + 4 > received) break;

            uint32_t atom_id = rd32(payload + eoff);
            if (((atom_id >> 24) & 0xFF) != 0x02) continue;

            /* Look up record in index (descriptor 0) */
            uint32_t idx_esize = rd16(payload + zarr + 6);
            uint32_t idx_count = rd32(payload + zarr + 8);
            uint32_t idx_off   = rd32(payload + zarr + 16);
            uint32_t rec_offset = 0;
            for (uint32_t k = 0; k < idx_count; k++) {
                uint32_t io = idx_off + (k * idx_esize);
                if (io + 8 > received) break;
                if (rd32(payload + io) == atom_id) {
                    rec_offset = rd32(payload + io + 4);
                    break;
                }
            }
            if (!rec_offset || rec_offset < 4 || rec_offset >= received) continue;

            uint32_t hdr = rd32(payload + rec_offset - 4);
            uint32_t rec_size = hdr & 0x00FFFFFF;
            if (rec_offset + rec_size > received) continue;

            fprintf(stderr, "\n  Video atom_id=0x%08x, record at 0x%08x, size=%u bytes\n",
                    atom_id, rec_offset, rec_size);

            /* Hex dump first 48 bytes (or full record if shorter) */
            uint32_t dump_len = rec_size < 64 ? rec_size : 64;
            for (uint32_t b = 0; b < dump_len; b++) {
                if (b % 16 == 0) fprintf(stderr, "  %04x: ", b);
                fprintf(stderr, "%02x ", payload[rec_offset + b]);
                if (b % 16 == 15 || b == dump_len - 1) fprintf(stderr, "\n");
            }
            videos_shown++;
        }
    }

    fprintf(stderr, "\n[zunedb] === SCAN COMPLETE ===\n\n");
    free(payload);
    return 0;
}

void zune_free_library(ZuneLibrary *lib) {
    if (!lib) return;
    if (lib->tracks) {
        for (int i = 0; i < lib->track_count; i++) {
            free(lib->tracks[i].title);
            free(lib->tracks[i].artist);
            free(lib->tracks[i].album);
            free(lib->tracks[i].genre);
        }
        free(lib->tracks);
    }
    zune_free_videos(lib->videos, lib->video_count);
    if (lib->photos) {
        for (int i = 0; i < lib->photo_count; i++)
            free(lib->photos[i].filename);
        free(lib->photos);
    }
    free(lib);
}
