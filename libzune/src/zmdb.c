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

/* Little-endian reads come from bytes.h (zune_le16/zune_le32). */

/* ---- Bounds helpers ----
 * Every device-supplied offset, size and count is checked here, in size_t,
 * by subtraction, so no sum of device values can wrap past a check. */

/* 1 if [off, off+need) lies inside a buffer of length len. */
static inline int zmdb_span_ok(size_t len, size_t off, size_t need)
{ return off <= len && need <= len - off; }

/* 1 if count entries of size bytes starting at off lie inside len. */
static inline int zmdb_array_ok(size_t len, size_t off, size_t count, size_t size)
{ return off <= len && size && count <= (len - off) / size; }

/* Research hex dump: one "[zunedb]   0000: xx xx ..." message per 16 bytes. */
static void zmdb_log_hex(ZuneChatter level, const uint8_t *bytes, uint32_t count) {
    for (uint32_t row = 0; row < count; row += 16) {
        char line[8 + 16 * 3 + 1];
        size_t used = (size_t)snprintf(line, sizeof(line), "  %04x:", row);
        for (uint32_t b = row; b < count && b < row + 16; b++)
            used += (size_t)snprintf(line + used, sizeof(line) - used, " %02x", bytes[b]);
        ZLOG(level, "zunedb", "%s", line);
    }
}

static inline int32_t rdi32(const uint8_t *buf) {
    return (int32_t)zune_le32(buf);
}

static inline uint64_t rd64(const uint8_t *buf) {
    return (uint64_t)zune_le32(buf) | ((uint64_t)zune_le32(buf + 4) << 32);
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
    uint32_t record_offset;   /* 0: record flagged unusable (sign bit) */
    uint32_t record_size;     /* validated inside the payload by zmdb_load */
    size_t   position;        /* table order; keeps first-match lookups */
} ZMDBIndexEntry;

#define ZMDB_DESCRIPTORS 96
#define ZMDB_DESCRIPTOR_SIZE 20

/* ---- Internal parser state ---- */

typedef struct {
    const uint8_t *data;
    size_t data_len;
    int is_hd;
    ZMDBDescriptor descs[ZMDB_DESCRIPTORS];
    int desc_count;           /* descriptors before the index starts (see zmdb_load) */
    ZMDBIndexEntry *index;    /* sorted by atom_id, then table position */
    size_t index_count;
} ZMDBState;

/* ---- Index lookup ---- */

static int zmdb_index_order(const void *a, const void *b) {
    const ZMDBIndexEntry *x = a, *y = b;
    if (x->atom_id != y->atom_id) return x->atom_id < y->atom_id ? -1 : 1;
    return (x->position > y->position) - (x->position < y->position);
}

/* Record for atom_id: the first index entry in table order, as the former
 * linear scan returned. NULL when the atom is absent or flagged unusable. */
static const uint8_t *zmdb_find_record(const ZMDBState *st, uint32_t atom_id,
                                       uint32_t *out_size) {
    size_t lo = 0, hi = st->index_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (st->index[mid].atom_id < atom_id) lo = mid + 1;
        else hi = mid;
    }
    if (lo == st->index_count || st->index[lo].atom_id != atom_id
        || !st->index[lo].record_offset)
        return NULL;
    if (out_size) *out_size = st->index[lo].record_size;
    return st->data + st->index[lo].record_offset;
}

/* ---- Resolve string reference ---- */

static char *zmdb_resolve_string(ZMDBState *st, uint32_t atom_id) {
    if (!atom_id) return strdup("");

    uint32_t rec_size = 0;
    const uint8_t *rec = zmdb_find_record(st, atom_id, &rec_size);
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

    /* Parse fields backwards from the end of the record. Every check keeps
     * pos >= title_end, so pos - title_end never wraps. */
    int count = 0;
    uint32_t pos = rec_size;

    while (pos > title_end && count < max_fields) {
        if (pos - title_end < 2) break;

        /* Read field ID (1-2 bytes, from end) */
        pos--;
        uint8_t id_byte1 = rec[pos];
        uint16_t field_id;
        if (id_byte1 & 0x80) {
            if (pos - title_end < 1) break;
            pos--;
            uint8_t id_byte2 = rec[pos];
            field_id = (uint16_t)((id_byte2 << 7) | (id_byte1 & 0x7F));
        } else {
            field_id = id_byte1;
        }

        /* Read field size (1-3 bytes, from end) */
        if (pos - title_end < 1) break;
        pos--;
        uint8_t sz_byte1 = rec[pos];
        uint32_t field_size;
        if (sz_byte1 & 0x80) {
            if (pos - title_end < 1) break;
            pos--;
            uint8_t sz_byte2 = rec[pos];
            if (sz_byte2 & 0x80) {
                if (pos - title_end < 1) break;
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
    uint32_t album_ref    = zune_le32(rec + 0);
    uint32_t artist_ref   = zune_le32(rec + 4);
    uint32_t genre_ref    = zune_le32(rec + 8);
    uint32_t filename_ref = zune_le32(rec + 12);
    int32_t  duration     = rdi32(rec + 16);
    int32_t  size         = rdi32(rec + 20);
    uint16_t track_num    = zune_le16(rec + 24);

    /* Skip root entries (all refs zero) */
    if (album_ref == 0 && artist_ref == 0 && genre_ref == 0)
        return -1;

    /* HD-only fixed fields at known offsets */
    uint16_t playcount = 0;
    uint8_t  rating    = 0;
    if (st->is_hd && rec_size >= 32) {
        playcount = zune_le16(rec + 26);
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
                out->skip_count = zune_le32(fields[i].field_data);
            break;
        case 0x6C: /* disc_number */
            if (fields[i].field_size >= 4)
                out->disc_number = (uint16_t)zune_le32(fields[i].field_data);
            else if (fields[i].field_size >= 2)
                out->disc_number = zune_le16(fields[i].field_data);
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
    (void)st;   /* same signature as the other record parsers */
    if (rec_size < ENTRY_SIZE_VIDEO) return -1;

    uint32_t folder_ref   = zune_le32(rec + 0);
    uint32_t title_ref    = zune_le32(rec + 4);
    uint32_t filename_ref = zune_le32(rec + 12);

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
    out->object_format = rec_size >= 34 ? zune_le16(rec + 32) : 0;

    /* Filesize at +12 (treating as direct value, not a reference).
     * B1 guard: MTP objects cap at 4GB (32-bit ObjectCompressedSize);
     * anything larger read here is a misparsed field, not a size. */
    if (rec_size >= 20)
        out->filesize = (uint64_t)zune_le32(rec + 16);
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
        uint16_t zune_type = zune_le16(rec + 38);
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

    uint32_t filename_ref = zune_le32(rec + 8);

    /* Skip root entries */
    if (zune_le32(rec + 0) == 0 && zune_le32(rec + 4) == 0 && filename_ref == 0)
        return -1;

    out->item_id   = atom_id;
    out->filename  = zmdb_resolve_string(st, filename_ref);
    /* B1 guard: on the 80/120 generation +12 is a FILETIME, not a
     * size — interpreting it as bytes summed to petabytes of phantom
     * photos. No real Zune photo approaches 1GB; larger = misparse. */
    out->filesize  = (uint64_t)rdi32(rec + 12);
    if (out->filesize > (1024ull * 1024 * 1024))
        out->filesize = 0;
    out->parent_id = zune_le32(rec + 0);
    return 0;
}

/* ---- Parse album record ---- */

static int zmdb_parse_album(ZMDBState *st, const uint8_t *rec, uint32_t rec_size,
                             uint32_t atom_id, ZuneDBAlbum *out) {
    int es = st->is_hd ? ENTRY_SIZE_ALBUM_HD : ENTRY_SIZE_ALBUM_CLASSIC;
    if (rec_size < (uint32_t)es) return -1;

    uint32_t artist_ref = zune_le32(rec + 0);

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
        uint64_t ticks = (uint64_t)zune_le32(rec + 12) | ((uint64_t)zune_le32(rec + 16) << 32);
        if (ticks > 0) {
            time_t unix_ts = (time_t)((int64_t)(ticks / 10000000ULL) - 11644473600LL);
            struct tm tm;
            if (gmtime_r(&unix_ts, &tm)) out->year = (uint16_t)(tm.tm_year + 1900);
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

    for (int i = 1; i < st->desc_count; i++) {  /* skip desc 0 (index) */
        ZMDBDescriptor *d = &st->descs[i];
        if (d->entry_count == 0) continue;
        /* Only a readable first atom makes a candidate; whichever descriptor
         * is chosen must then pass zmdb_desc_ok in full before it is walked. */
        if (!zmdb_span_ok(data_len, d->data_offset, 4)) continue;

        uint32_t first_atom = zune_le32(data + d->data_offset);
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

/* ---- Shared header, descriptor table and index loading ----
 *
 * Fail closed, like podcast_state.c: a database that is truncated or points
 * outside itself yields an error and no partial library, never a silently
 * shortened one. The index is validated in full here (every record offset
 * and size), so later lookups cannot leave the payload. */

/* Fixed capacities of a full scan (larger libraries are truncated). */
enum {
    ZMDB_MAX_TRACKS = 10000, ZMDB_MAX_VIDEOS = 1000, ZMDB_MAX_PHOTOS = 5000,
    ZMDB_MAX_ALBUMS = 2000, ZMDB_MAX_ARTISTS = 2000, ZMDB_MAX_PLAYLISTS = 200,
    ZMDB_MAX_PHOTO_ALBUMS = 100
};

static int zmdb_fail(ZMDBState *st, const char *tag, const char *why) {
    ZLOG(ZUNE_CHATTER_SCREAM, tag, "%s", why);
    zune_set_error("ZMDB rejected: %s", why);
    free(st->index);
    st->index = NULL;
    st->index_count = 0;
    return -1;
}

static int zmdb_load(ZMDBState *st, const uint8_t *data, size_t data_len,
                     const char *tag) {
    memset(st, 0, sizeof(*st));
    if (!data || data_len < 0x40)
        return zmdb_fail(st, tag, "database too short for a ZMDB header");

    /* Verify ZMDB magic */
    if (memcmp(data, "ZMDB", 4) != 0)
        return zmdb_fail(st, tag, "bad magic");

    /* Verify ZMed header and detect version */
    if (memcmp(data + 0x20, "ZMed", 4) != 0)
        return zmdb_fail(st, tag, "missing ZMed header");
    uint16_t version = zune_le16(data + 0x24);

    /* Find ZArr descriptor block (search 0x30 to 0x100) */
    size_t zarr_offset = 0;
    for (size_t off = 0x30; off < 0x100 && zmdb_span_ok(data_len, off, 4); off += 4) {
        if (memcmp(data + off, "ZArr", 4) == 0) {
            zarr_offset = off;
            break;
        }
    }
    if (!zarr_offset)
        return zmdb_fail(st, tag, "ZArr not found");
    if (!zmdb_span_ok(data_len, zarr_offset, ZMDB_DESCRIPTOR_SIZE))
        return zmdb_fail(st, tag, "index descriptor is truncated");

    /* Read descriptor 0 first: its index offset bounds the Classic v2
     * table. Keel/Draco have 33 descriptors, not 96; requiring room for 96
     * before finding this bound rejects complete small databases. The index
     * may have alignment padding before it, so round down to whole slots.
     * Keep the full table requirement for HD and unrecognized versions. */
    st->desc_count = ZMDB_DESCRIPTORS;
    uint32_t index_at = zune_le32(data + zarr_offset + 16);
    uint32_t index_entries = zune_le32(data + zarr_offset + 8);
    if (version == 2 && (index_entries || index_at)) {
        if (index_at <= zarr_offset)
            return zmdb_fail(st, tag, "index overlaps the descriptor table");
        size_t fit = (index_at - zarr_offset) / ZMDB_DESCRIPTOR_SIZE;
        /* All supported Classic media slots (through 16) must exist. */
        if (fit <= 16)
            return zmdb_fail(st, tag, "index overlaps required media descriptors");
        if (fit < ZMDB_DESCRIPTORS)
            st->desc_count = (int)fit;
    }
    if (!zmdb_array_ok(data_len, zarr_offset, (size_t)st->desc_count, ZMDB_DESCRIPTOR_SIZE))
        return zmdb_fail(st, tag, "descriptor table is truncated");

    st->data = data;
    st->data_len = data_len;
    st->is_hd = (version >= 5);

    /* Only the validated table slots are descriptors. Classic bytes after
     * that boundary belong to the index/records and must never be discovered
     * as additional media descriptors. */
    for (size_t i = 0; i < (size_t)st->desc_count; i++) {
        const uint8_t *d = data + zarr_offset + i * ZMDB_DESCRIPTOR_SIZE;
        st->descs[i].entry_size  = zune_le16(d + 6);
        st->descs[i].entry_count = zune_le32(d + 8);
        st->descs[i].data_offset = zune_le32(d + 16);
    }

    /* Build index table from descriptor 0. Its whole extent is checked
     * before anything is allocated. */
    const ZMDBDescriptor *ix = &st->descs[0];
    if (ix->entry_count == 0) return 0;
    if (ix->entry_size != 8)
        return zmdb_fail(st, tag, "index entries are not 8 bytes");
    if (!zmdb_array_ok(data_len, ix->data_offset, ix->entry_count, 8))
        return zmdb_fail(st, tag, "index extends past the end of the database");

    size_t count = ix->entry_count;
    st->index = calloc(count, sizeof(ZMDBIndexEntry));
    if (!st->index)
        return zmdb_fail(st, tag, "out of memory for the index");
    for (size_t i = 0; i < count; i++) {
        const uint8_t *e = data + ix->data_offset + i * 8;
        ZMDBIndexEntry *entry = &st->index[i];
        entry->atom_id  = zune_le32(e);
        entry->position = i;
        uint32_t offset = zune_le32(e + 4);
        /* A record is preceded by a 4-byte header: size in the low 24 bits,
         * sign bit set when the record must not be used. */
        if (offset < 4 || offset > data_len)
            return zmdb_fail(st, tag, "index record offset is outside the database");
        uint32_t header = zune_le32(data + offset - 4);
        if (header & 0x80000000) continue;          /* unusable: lookups miss */
        uint32_t size = header & 0x00FFFFFF;
        if (!zmdb_span_ok(data_len, offset, size))
            return zmdb_fail(st, tag, "index record extends past the end of the database");
        entry->record_offset = offset;
        entry->record_size   = size;
    }
    qsort(st->index, count, sizeof(ZMDBIndexEntry), zmdb_index_order);
    st->index_count = count;
    return 0;
}

/* 1 if a descriptor this parser walks is a 4-byte atom array that lies
 * entirely inside the payload. */
static int zmdb_desc_ok(const ZMDBState *st, int idx) {
    const ZMDBDescriptor *d = &st->descs[idx];
    return d->entry_count == 0
        || (d->entry_size >= 4
            && zmdb_array_ok(st->data_len, d->data_offset, d->entry_count, d->entry_size));
}

/* Descriptors that hold media on both Classic and HD (music, album, artist,
 * playlist, video, photo album, picture). Discovery skips unreadable
 * descriptors, so a corrupt one here must fail the scan instead of quietly
 * yielding a library without that media. Slots past the table (index bytes on
 * Classic) are never discovered, so they are not required to be atom arrays. */
static const int ZMDB_MEDIA_SLOTS[] = { 1, 3, 4, 11, 12, 15, 16 };

static int zmdb_media_slots_ok(const ZMDBState *st) {
    for (size_t i = 0; i < sizeof(ZMDB_MEDIA_SLOTS) / sizeof(ZMDB_MEDIA_SLOTS[0]); i++) {
        const ZMDBDescriptor *d = &st->descs[ZMDB_MEDIA_SLOTS[i]];
        if (d->entry_count && !zmdb_span_ok(st->data_len, d->data_offset, 4))
            return 0;
    }
    return 1;
}

/* Atom ID of entry i of a descriptor that passed zmdb_desc_ok. */
static uint32_t zmdb_desc_atom(const ZMDBState *st, const ZMDBDescriptor *d, size_t i) {
    return zune_le32(st->data + d->data_offset + i * d->entry_size);
}

/* ---- Main parser ---- */

static ZuneLibrary *zmdb_parse(const uint8_t *data, size_t data_len) {
    ZMDBState st;
    if (zmdb_load(&st, data, data_len, "libzune-zmdb") < 0) return NULL;
    ZLOG(ZUNE_CHATTER_WHISPER, "libzune-zmdb", "ZMed version=%d, is_hd=%d",
            zune_le16(data + 0x24), st.is_hd);
    ZLOG(ZUNE_CHATTER_WHISPER, "libzune-zmdb", "%zu index entries", st.index_count);

    /* Walk descriptors and parse media */
    const ZMDBDescMap *desc_map = st.is_hd ? HD_DESC_MAP : CLASSIC_DESC_MAP;
    for (int m = 0; desc_map[m].desc_idx >= 0; m++) {
        if (!zmdb_desc_ok(&st, desc_map[m].desc_idx)) {
            zmdb_fail(&st, "libzune-zmdb", "media descriptor extends past the end of the database");
            return NULL;
        }
    }

    /* Allocate result */
    ZuneLibrary *lib = calloc(1, sizeof(ZuneLibrary));
    if (!lib) {
        zmdb_fail(&st, "libzune-zmdb", "out of memory for the library");
        return NULL;
    }

    /* Pre-allocate arrays (we'll realloc down later) */
    const int max_tracks = ZMDB_MAX_TRACKS, max_videos = ZMDB_MAX_VIDEOS,
              max_photos = ZMDB_MAX_PHOTOS;
    lib->tracks = calloc(max_tracks, sizeof(ZuneTrack));
    lib->videos = calloc(max_videos, sizeof(ZuneVideoFile));
    lib->photos = calloc(max_photos, sizeof(ZunePhotoFile));

    if (!lib->tracks || !lib->videos || !lib->photos) {
        free(lib->tracks); free(lib->videos); free(lib->photos);
        free(lib);
        zmdb_fail(&st, "libzune-zmdb", "out of memory for the library");
        return NULL;
    }

    for (int m = 0; desc_map[m].desc_idx >= 0; m++) {
        int schema = desc_map[m].schema;
        const ZMDBDescriptor *desc = &st.descs[desc_map[m].desc_idx];

        for (size_t i = 0; i < desc->entry_count; i++) {
            uint32_t atom_id = zmdb_desc_atom(&st, desc, i);
            int atom_schema = (atom_id >> 24) & 0xFF;
            if (atom_schema != schema) continue;

            uint32_t rec_size = 0;
            const uint8_t *rec = zmdb_find_record(&st, atom_id, &rec_size);
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

    ZLOG(ZUNE_CHATTER_TALK, "libzune-zmdb", "parsed %d tracks, %d videos, %d photos",
            lib->track_count, lib->video_count, lib->photo_count);
    return lib;
}

/* Complete bounded transport is shared in zmdb_io.c. */

/* ---- Public API: Read ZMDB from device ---- */

int zune_infiltrate_legacy(ZuneDevice *dev, ZuneLibrary **out) {
    zune_clear_error();
    if (!dev || !out) {
        zune_set_error("Invalid parameters for ZMDB read");
        return ZUNE_BLOWN;
    }

    *out = NULL;

    uint8_t *payload = NULL;
    uint32_t received = 0;
    int ret = zune_zmdb_read_payload(&dev->usb, &payload, &received);
    if (ret != 0) return ZUNE_BLOWN;

    /* Parse the ZMDB binary data; a rejection sets its own error. */
    ZuneLibrary *lib = zmdb_parse(payload, received);
    free(payload);

    if (!lib) {
        return ZUNE_BLOWN;
    }

    *out = lib;
    return ZUNE_OK;
}

/* ---- ZuneDB: Full library scan with discovery-based parsing ---- */

/* Load the database, discover its descriptors and check that every
 * descriptor walked below lies inside it. -1 (index freed) on rejection. */
static int zmdb_full_preflight(ZMDBState *st, const uint8_t *data, size_t data_len,
                               ZMDBDiscovered *disc) {
    if (zmdb_load(st, data, data_len, "zunedb") < 0) return -1;
    if (!zmdb_media_slots_ok(st)) {
        zmdb_fail(st, "zunedb", "media descriptor points outside the database");
        return -1;
    }

    /* Discover descriptors dynamically */
    zmdb_discover(st, data, data_len, disc);

    /* Every descriptor walked below must lie entirely inside the payload. */
    const int walked[] = { disc->music, disc->video, disc->photo, disc->album,
                           disc->artist, disc->photo_album, disc->playlist };
    for (size_t i = 0; i < sizeof(walked) / sizeof(walked[0]); i++) {
        if (walked[i] >= 0 && !zmdb_desc_ok(st, walked[i])) {
            zmdb_fail(st, "zunedb", "media descriptor extends past the end of the database");
            return -1;
        }
    }

    return 0;
}

/* The library and its fixed-capacity arrays. NULL (index freed) when out of
 * memory. */
static ZuneDBLibrary *zmdb_alloc_library(ZMDBState *st) {
    /* Allocate library */
    ZuneDBLibrary *lib = calloc(1, sizeof(ZuneDBLibrary));
    if (!lib) {
        zmdb_fail(st, "zunedb", "out of memory for the library");
        return NULL;
    }
    lib->is_hd = st->is_hd;

    /* Pre-allocate arrays */
    lib->tracks       = calloc(ZMDB_MAX_TRACKS, sizeof(ZuneTrack));
    lib->videos       = calloc(ZMDB_MAX_VIDEOS, sizeof(ZuneVideoFile));
    lib->photos       = calloc(ZMDB_MAX_PHOTOS, sizeof(ZunePhotoFile));
    lib->albums       = calloc(ZMDB_MAX_ALBUMS, sizeof(ZuneDBAlbum));
    lib->artists      = calloc(ZMDB_MAX_ARTISTS, sizeof(ZuneDBArtist));
    lib->playlists    = calloc(ZMDB_MAX_PLAYLISTS, sizeof(ZunePlaylist));
    lib->photo_albums = calloc(ZMDB_MAX_PHOTO_ALBUMS, sizeof(ZuneDBPhotoAlbum));

    if (!lib->tracks || !lib->videos || !lib->photos) {
        /* Critical arrays failed */
        free(lib->tracks); free(lib->videos); free(lib->photos);
        free(lib->albums); free(lib->artists); free(lib->playlists);
        free(lib->photo_albums);
        free(lib);
        zmdb_fail(st, "zunedb", "out of memory for the library");
        return NULL;
    }

    return lib;
}

/* Parse every record of each media descriptor into the library's arrays. */
static void zmdb_parse_media(ZMDBState *st, const ZMDBDiscovered *disc, ZuneDBLibrary *lib) {
    /* Helper: iterate a validated descriptor and parse each entry */
    #define PARSE_DESC(desc_idx, schema_id, parse_fn, array, count, max) \
        if ((desc_idx) >= 0 && (array)) { \
            const ZMDBDescriptor *_d = &st->descs[(desc_idx)]; \
            for (size_t _i = 0; _i < _d->entry_count && (count) < (max); _i++) { \
                uint32_t _aid = zmdb_desc_atom(st, _d, _i); \
                if (((_aid >> 24) & 0xFF) != (schema_id)) continue; \
                uint32_t _rsz = 0; \
                const uint8_t *_rec = zmdb_find_record(st, _aid, &_rsz); \
                if (!_rec) continue; \
                if (parse_fn(st, _rec, _rsz, _aid, &(array)[(count)]) == 0) \
                    (count)++; \
            } \
        }

    PARSE_DESC(disc->music, SCHEMA_MUSIC, zmdb_parse_track, lib->tracks, lib->track_count, ZMDB_MAX_TRACKS);
    /* HD video records have a different layout than Classic's 40-byte
     * fixed records; zmdb_parse_video would misread them and tag every
     * video as "Other". Skip on HD — AppState falls back to live MTP reads. */
    if (!st->is_hd) {
        PARSE_DESC(disc->video, SCHEMA_VIDEO, zmdb_parse_video, lib->videos, lib->video_count, ZMDB_MAX_VIDEOS);
    }
    PARSE_DESC(disc->photo, SCHEMA_PICTURE, zmdb_parse_picture, lib->photos, lib->photo_count, ZMDB_MAX_PHOTOS);
    PARSE_DESC(disc->album, SCHEMA_ALBUM, zmdb_parse_album, lib->albums, lib->album_count, ZMDB_MAX_ALBUMS);
    PARSE_DESC(disc->artist, SCHEMA_ARTIST, zmdb_parse_artist, lib->artists, lib->artist_count, ZMDB_MAX_ARTISTS);
    PARSE_DESC(disc->photo_album, SCHEMA_PHOTOALBUM, zmdb_parse_photo_album, lib->photo_albums, lib->photo_album_count, ZMDB_MAX_PHOTO_ALBUMS);

    #undef PARSE_DESC
}

/* Playlist records are not decoded yet: log the first few for research
 * (zune_infiltrate falls back to MTP for playlists). */
static void zmdb_log_playlists(ZMDBState *st, const ZMDBDiscovered *disc) {
    if (disc->playlist >= 0) {
        const ZMDBDescriptor *pd = &st->descs[disc->playlist];
        for (size_t i = 0; i < pd->entry_count && i < 5; i++) {
            uint32_t aid = zmdb_desc_atom(st, pd, i);
            if (((aid >> 24) & 0xFF) != SCHEMA_PLAYLIST) continue;
            uint32_t rsz = 0;
            const uint8_t *rec = zmdb_find_record(st, aid, &rsz);
            if (!rec) continue;
            ZLOG(ZUNE_CHATTER_WHISPER, "zunedb", "playlist record atom=%08X size=%u", aid, rsz);
            /* Hex dump first 64 bytes for analysis */
            zmdb_log_hex(ZUNE_CHATTER_WHISPER, rec, rsz < 64 ? rsz : 64);
        }
    }
}

static ZuneDBLibrary *zmdb_parse_full(const uint8_t *data, size_t data_len) {
    ZMDBState st;
    ZMDBDiscovered disc;
    if (zmdb_full_preflight(&st, data, data_len, &disc) < 0)
        return NULL;
    ZuneDBLibrary *lib = zmdb_alloc_library(&st);
    if (!lib)
        return NULL;
    zmdb_parse_media(&st, &disc, lib);
    zmdb_log_playlists(&st, &disc);

    free(st.index);

    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "scanned: %d tracks, %d videos, %d photos, "
            "%d albums, %d artists, %d playlists, %d photo albums",
            lib->track_count, lib->video_count, lib->photo_count,
            lib->album_count, lib->artist_count, lib->playlist_count,
            lib->photo_album_count);
    return lib;
}

int zune_infiltrate(ZuneDevice *dev, ZuneDBLibrary **out) {
    zune_clear_error();
    if (!dev || !out) {
        zune_set_error("Invalid parameters for ZuneDB scan");
        return ZUNE_BLOWN;
    }
    *out = NULL;

    uint8_t *payload = NULL;
    uint32_t received = 0;
    int ret = zune_zmdb_read_payload(&dev->usb, &payload, &received);
    if (ret != 0) return ZUNE_BLOWN;

    /* Parse; a rejection sets its own error. */
    ZuneDBLibrary *lib = zmdb_parse_full(payload, received);
    free(payload);

    if (!lib) {
        return ZUNE_BLOWN;
    }

    /* Playlists: internal MTP fallback (ZMDB playlist format not yet decoded).
     * The scan itself succeeded, so a failed fallback is logged rather than
     * left as this call's error. */
    if (lib->playlist_count == 0) {
        int pl_count = 0;
        ZunePlaylist *pls = zune_get_playlists(dev, &pl_count);
        if (pls && pl_count > 0) {
            free(lib->playlists);
            lib->playlists = pls;
            lib->playlist_count = pl_count;
        } else if (zune_get_error()) {
            ZLOG(ZUNE_CHATTER_SHOUT, "zunedb", "playlist fallback failed: %s", zune_get_error());
            zune_clear_error();
        }
    }

    /* Cache for duplicate detection (zune_find_track/video/photo) */
    dev->cached_library = lib;

    *out = lib;
    return ZUNE_OK;
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
    zune_clear_error();
    if (!dev || !output_path) return ZUNE_BLOWN;

    /* The shared reader validates the header, reads the whole payload and
     * consumes the device's answer after it; a leftover answer would be
     * taken by the next transaction as its own and wedge the session. */
    uint8_t header[12];
    uint8_t *payload = NULL;
    uint32_t received = 0;
    if (zune_zmdb_read_response(&dev->usb, header, &payload, &received) != 0)
        return ZUNE_BLOWN;

    /* The file holds the full response: 12-byte header, then the payload. */
    FILE *fp = fopen(output_path, "wb");
    int ok = fp && fwrite(header, 1, sizeof(header), fp) == sizeof(header) &&
             fwrite(payload, 1, received, fp) == received;
    if (fp && fclose(fp) != 0) ok = 0;
    free(payload);
    if (!ok) {
        if (fp) unlink(output_path);
        zune_set_error("ZMDB dump: cannot write %s", output_path);
        return ZUNE_BLOWN;
    }

    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "dumped %u bytes (payload=%u) to %s",
            received + 12, received, output_path);
    return ZUNE_OK;
}

/* ---- Research/Debug: Scan all 96 descriptors ---- */

int zune_infiltrate_deep(ZuneDevice *dev) {
    zune_clear_error();
    if (!dev) return ZUNE_BLOWN;

    /* Read ZMDB payload using shared helper */
    uint8_t *payload = NULL;
    uint32_t received = 0;
    int ret = zune_zmdb_read_payload(&dev->usb, &payload, &received);
    if (ret != 0) return ZUNE_BLOWN;

    /* Parse headers */
    if (received < 0x40 || memcmp(payload, "ZMDB", 4) != 0) {
        free(payload);
        ZLOG(ZUNE_CHATTER_SCREAM, "zunedb", "not a ZMDB (bad magic or too small)");
        return ZUNE_BLOWN;
    }

    uint16_t version = zune_le16(payload + 0x24);
    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "=== DESCRIPTOR SCAN ===");
    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "ZMed version: %d (%s)", version, version >= 5 ? "HD" : "Classic");
    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "Payload: %u bytes", received);

    /* Find ZArr */
    size_t zarr = 0;
    for (size_t off = 0x30; off < 0x100 && zmdb_span_ok(received, off, 4); off += 4) {
        if (memcmp(payload + off, "ZArr", 4) == 0) { zarr = off; break; }
    }
    if (!zarr) { free(payload); ZLOG(ZUNE_CHATTER_SCREAM, "zunedb", "ZArr not found"); return ZUNE_BLOWN; }

    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "ZArr at offset 0x%04zx", zarr);
    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "  Desc  EntrySize  EntryCount  DataOffset  FirstAtomSchema");
    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "  ----  ---------  ----------  ----------  ---------------");

    /* Scan all 96 descriptors */
    for (size_t i = 0; i < ZMDB_DESCRIPTORS; i++) {
        size_t doff = zarr + i * ZMDB_DESCRIPTOR_SIZE;
        if (!zmdb_span_ok(received, doff, ZMDB_DESCRIPTOR_SIZE)) break;

        uint16_t entry_size  = zune_le16(payload + doff + 6);
        uint32_t entry_count = zune_le32(payload + doff + 8);
        uint32_t data_offset = zune_le32(payload + doff + 16);

        if (entry_count == 0) continue;  /* skip empty descriptors */

        /* Peek at first atom to determine schema */
        char schema_str[32] = "???";
        if (zmdb_span_ok(received, data_offset, 4)) {
            uint32_t first_atom = zune_le32(payload + data_offset);
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

        ZLOG(ZUNE_CHATTER_TALK, "zunedb", "  %3zu   %7u    %8u    0x%08x  %s",
             i, entry_size, entry_count, data_offset, schema_str);
    }

    /* Dump first 3 video records in hex for analysis */
    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "=== SAMPLE VIDEO RECORDS ===");
    int videos_shown = 0;
    /* Index entries need at least 8 bytes; a smaller declared size would
     * reread overlapping bytes (or loop forever at 0). */
    size_t idx_esize = 0, idx_count = 0, idx_off = 0;
    if (zmdb_span_ok(received, zarr, ZMDB_DESCRIPTOR_SIZE)) {
        idx_esize = zune_le16(payload + zarr + 6);
        idx_count = zune_le32(payload + zarr + 8);
        idx_off   = zune_le32(payload + zarr + 16);
    }
    if (idx_esize < 8 || !zmdb_array_ok(received, idx_off, idx_count, idx_esize))
        idx_count = 0;
    for (size_t i = 0; i < ZMDB_DESCRIPTORS && videos_shown < 3; i++) {
        size_t doff = zarr + i * ZMDB_DESCRIPTOR_SIZE;
        if (!zmdb_span_ok(received, doff, ZMDB_DESCRIPTOR_SIZE)) break;
        uint16_t entry_size  = zune_le16(payload + doff + 6);
        uint32_t entry_count = zune_le32(payload + doff + 8);
        uint32_t data_offset = zune_le32(payload + doff + 16);
        if (entry_count == 0 || entry_size < 4) continue;
        if (!zmdb_span_ok(received, data_offset, 4)) continue;

        uint32_t first_atom = zune_le32(payload + data_offset);
        if (((first_atom >> 24) & 0xFF) != 0x02) continue;  /* Not video */

        /* Only the entries that fit; this is a diagnostic scan. */
        size_t usable = (received - data_offset) / entry_size;
        if (usable > entry_count) usable = entry_count;
        for (size_t j = 0; j < usable && videos_shown < 3; j++) {
            uint32_t atom_id = zune_le32(payload + data_offset + j * entry_size);
            if (((atom_id >> 24) & 0xFF) != 0x02) continue;

            /* Look up record in index (descriptor 0) */
            size_t rec_offset = 0;
            for (size_t k = 0; k < idx_count; k++) {
                size_t io = idx_off + k * idx_esize;
                if (zune_le32(payload + io) == atom_id) {
                    rec_offset = zune_le32(payload + io + 4);
                    break;
                }
            }
            if (rec_offset < 4 || rec_offset > received) continue;

            uint32_t hdr = zune_le32(payload + rec_offset - 4);
            uint32_t rec_size = hdr & 0x00FFFFFF;
            if (!zmdb_span_ok(received, rec_offset, rec_size)) continue;

            ZLOG(ZUNE_CHATTER_TALK, "zunedb", "Video atom_id=0x%08x, record at 0x%08zx, size=%u bytes",
                 atom_id, rec_offset, rec_size);

            /* Hex dump first 64 bytes (or full record if shorter) */
            zmdb_log_hex(ZUNE_CHATTER_TALK, payload + rec_offset, rec_size < 64 ? rec_size : 64);
            videos_shown++;
        }
    }

    ZLOG(ZUNE_CHATTER_TALK, "zunedb", "=== SCAN COMPLETE ===");
    free(payload);
    return ZUNE_OK;
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
