/*
 * id3_rewrite.c — Pure C ID3v2.3 tag rewriter
 *
 * Reads ID3v2.x (v2.2/v2.3/v2.4) tags from an MP3, rewrites as ID3v2.3 + ID3v1.1.
 * Audio data is byte-copied (no re-encoding). Zero external dependencies.
 *
 * The Zune ignores ID3v2.4 tags entirely. This is the fix.
 */

#include "id3_rewrite.h"
#include "disc_tag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* ---- Constants ---- */

#define ID3V2_HEADER_SIZE  10
#define ID3V1_TAG_SIZE     128
#define MAX_TAG_SIZE       (16 * 1024 * 1024)  /* 16 MB sanity limit */
#define MAX_FRAMES         256
#define PADDING_SIZE       1024  /* 1 KB padding after frames */

/* ---- ID3v2 frame storage ---- */

typedef struct {
    char     id[5];      /* Frame ID: "TIT2", "TPE1", etc. (NUL-terminated) */
    uint8_t *data;       /* Raw frame data (after frame header) */
    uint32_t size;       /* Size of data */
    uint16_t flags;      /* Frame flags */
} ID3Frame;

typedef struct {
    char     title[256];
    char     artist[256];
    char     album[256];
    char     genre[256];
    char     year[8];
    uint16_t track;
} ID3v1Info;

/* ---- Syncsafe integer decode/encode ---- */

static uint32_t decode_syncsafe(const uint8_t *b)
{
    return ((uint32_t)b[0] << 21) | ((uint32_t)b[1] << 14) |
           ((uint32_t)b[2] << 7)  | (uint32_t)b[3];
}

static void encode_syncsafe(uint32_t val, uint8_t *b)
{
    b[0] = (val >> 21) & 0x7F;
    b[1] = (val >> 14) & 0x7F;
    b[2] = (val >> 7)  & 0x7F;
    b[3] = val & 0x7F;
}

static uint32_t decode_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static void encode_be32(uint32_t val, uint8_t *b)
{
    b[0] = (val >> 24) & 0xFF;
    b[1] = (val >> 16) & 0xFF;
    b[2] = (val >> 8)  & 0xFF;
    b[3] = val & 0xFF;
}

/* ---- Text frame decoding ---- */

/*
 * Extract a Latin-1/ASCII string from an ID3v2 text frame's data.
 * Handles encoding byte: 0x00=ISO-8859-1, 0x01=UTF-16, 0x02=UTF-16BE, 0x03=UTF-8.
 * For UTF-16, extracts ASCII chars (lossy but sufficient for ID3v1).
 * Returns a malloc'd string. Caller frees.
 */
static char *text_frame_to_latin1(const uint8_t *data, uint32_t size)
{
    if (size < 2) return NULL;

    uint8_t encoding = data[0];
    const uint8_t *text = data + 1;
    uint32_t text_len = size - 1;

    if (encoding == 0x00 || encoding == 0x03) {
        /* ISO-8859-1 or UTF-8 — copy as-is (ASCII subset) */
        uint32_t len = text_len;
        /* Trim trailing NULs */
        while (len > 0 && text[len - 1] == 0) len--;
        char *result = malloc(len + 1);
        if (!result) return NULL;
        memcpy(result, text, len);
        result[len] = '\0';
        return result;
    }

    if (encoding == 0x01 || encoding == 0x02) {
        /* UTF-16 (with or without BOM) — extract ASCII chars */
        const uint8_t *p = text;
        uint32_t remaining = text_len;
        int le = 1; /* default little-endian */

        /* Check for BOM */
        if (remaining >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) { le = 1; p += 2; remaining -= 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { le = 0; p += 2; remaining -= 2; }
            else if (encoding == 0x02) { le = 0; } /* UTF-16BE, no BOM */
        }

        uint32_t chars = remaining / 2;
        char *result = malloc(chars + 1);
        if (!result) return NULL;

        uint32_t out = 0;
        for (uint32_t i = 0; i < chars; i++) {
            uint16_t ch = le ? (p[i*2] | (p[i*2+1] << 8)) : ((p[i*2] << 8) | p[i*2+1]);
            if (ch == 0) break;
            result[out++] = (ch < 128) ? (char)ch : '?';
        }
        result[out] = '\0';
        return result;
    }

    return NULL;
}

/*
 * Convert a text frame's data from v2.4 encoding to v2.3-compatible encoding.
 * UTF-8 (0x03) → UTF-16LE with BOM (0x01).
 * Returns a new malloc'd buffer with the converted data, or NULL if no conversion needed.
 * Sets *out_size to the new size.
 */
static uint8_t *convert_text_encoding(const uint8_t *data, uint32_t size, uint32_t *out_size)
{
    if (size < 2) return NULL;
    uint8_t encoding = data[0];

    if (encoding == 0x03) {
        /* UTF-8 → UTF-16LE with BOM */
        const char *utf8 = (const char *)(data + 1);
        uint32_t utf8_len = size - 1;
        /* Trim trailing NUL */
        while (utf8_len > 0 && utf8[utf8_len - 1] == 0) utf8_len--;

        /* Allocate: encoding byte + BOM (2) + worst case 2 bytes per UTF-8 byte + NUL pair */
        uint32_t alloc = 1 + 2 + utf8_len * 2 + 2;
        uint8_t *out = malloc(alloc);
        if (!out) return NULL;

        out[0] = 0x01; /* UTF-16 with BOM */
        out[1] = 0xFF; /* BOM: LE */
        out[2] = 0xFE;
        uint32_t pos = 3;

        /* Simple UTF-8 → UTF-16LE conversion (BMP only) */
        for (uint32_t i = 0; i < utf8_len; ) {
            uint32_t cp;
            uint8_t c = (uint8_t)utf8[i];
            if (c < 0x80) {
                cp = c; i += 1;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8_len) {
                cp = ((c & 0x1F) << 6) | (utf8[i+1] & 0x3F); i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8_len) {
                cp = ((c & 0x0F) << 12) | ((utf8[i+1] & 0x3F) << 6) | (utf8[i+2] & 0x3F); i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8_len) {
                cp = 0xFFFD; i += 4; /* Replacement char for non-BMP */
            } else {
                cp = 0xFFFD; i += 1;
            }
            if (cp <= 0xFFFF && pos + 2 <= alloc) {
                out[pos++] = cp & 0xFF;
                out[pos++] = (cp >> 8) & 0xFF;
            }
        }
        /* NUL terminator */
        if (pos + 2 <= alloc) { out[pos++] = 0; out[pos++] = 0; }
        *out_size = pos;
        return out;
    }

    if (encoding == 0x02) {
        /* UTF-16BE without BOM → UTF-16 with BOM (add BOM, keep data, set encoding to 0x01) */
        uint32_t new_size = size + 2; /* +2 for BOM */
        uint8_t *out = malloc(new_size);
        if (!out) return NULL;
        out[0] = 0x01;  /* encoding = UTF-16 with BOM */
        out[1] = 0xFE;  /* BOM: BE */
        out[2] = 0xFF;
        memcpy(out + 3, data + 1, size - 1);
        *out_size = new_size;
        return out;
    }

    return NULL; /* No conversion needed */
}

/* ---- ID3v2 tag parsing ---- */

/*
 * Parse ID3v2 tag frames from a buffer.
 * Returns number of frames parsed. Frames stored in `frames` array.
 */
static int parse_id3v2_frames(const uint8_t *tag_data, uint32_t tag_size,
                               uint8_t version_major, ID3Frame *frames, int max_frames)
{
    int count = 0;
    uint32_t pos = 0;

    while (pos + 10 <= tag_size && count < max_frames) {
        /* Check for padding (all zeros) */
        if (tag_data[pos] == 0) break;

        /* Frame ID: 4 printable ASCII chars */
        if (tag_data[pos] < 0x20 || tag_data[pos] > 0x7E) break;

        char id[5];
        memcpy(id, tag_data + pos, 4);
        id[4] = '\0';
        pos += 4;

        /* Frame size */
        uint32_t frame_size;
        if (version_major == 4) {
            frame_size = decode_syncsafe(tag_data + pos);
        } else {
            frame_size = decode_be32(tag_data + pos);
        }
        pos += 4;

        /* Frame flags */
        uint16_t frame_flags = ((uint16_t)tag_data[pos] << 8) | tag_data[pos + 1];
        pos += 2;

        /* Sanity check */
        if (frame_size == 0 || frame_size > tag_size - pos) break;

        /* Store frame */
        frames[count].data = malloc(frame_size);
        if (!frames[count].data) break;
        memcpy(frames[count].data, tag_data + pos, frame_size);
        frames[count].size = frame_size;
        frames[count].flags = frame_flags;
        memcpy(frames[count].id, id, 5);

        /* Convert v2.4 frame IDs to v2.3 equivalents */
        if (version_major == 4) {
            if (strcmp(id, "TDRC") == 0) {
                memcpy(frames[count].id, "TYER", 5);
                /* TDRC format: "YYYY-MM-DD..." → truncate to "YYYY" for TYER */
                if (frames[count].size > 5) {
                    /* encoding byte + at least 4 chars */
                    uint8_t enc = frames[count].data[0];
                    if (enc == 0x00 || enc == 0x03) {
                        /* ASCII/UTF-8: truncate to first 4 chars after encoding byte */
                        if (frames[count].size > 5)
                            frames[count].size = 5; /* encoding + "YYYY" */
                    }
                }
            }
        }

        /* Convert v2.4 text encoding to v2.3 compatible */
        if (id[0] == 'T' && version_major == 4) {
            uint32_t new_size = 0;
            uint8_t *converted = convert_text_encoding(frames[count].data, frame_size, &new_size);
            if (converted) {
                free(frames[count].data);
                frames[count].data = converted;
                frames[count].size = new_size;
            }
        }

        pos += frame_size;
        count++;
    }

    return count;
}

/* ---- Find MPEG sync word ---- */

/*
 * Find the first MPEG audio sync word in the buffer.
 * MPEG sync: 0xFF followed by 0xE0-0xFF (11 sync bits set).
 * Returns offset from start, or -1 if not found.
 */
static long find_mpeg_sync(const uint8_t *buf, long size)
{
    for (long i = 0; i + 1 < size; i++) {
        if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
            /* Verify it's a valid MPEG frame header */
            uint8_t version = (buf[i + 1] >> 3) & 0x03;
            uint8_t layer   = (buf[i + 1] >> 1) & 0x03;
            if (version != 0x01 && layer != 0x00) {
                return i;
            }
        }
    }
    return -1;
}

/* ---- Write ID3v2.3 tag ---- */

/*
 * Write ID3v2.3 frames to a file. Returns total bytes written for frame data
 * (not including the 10-byte header).
 */
static uint32_t write_id3v2_3_frames(FILE *fp, const ID3Frame *frames, int count)
{
    uint32_t total = 0;

    for (int i = 0; i < count; i++) {
        /* Skip empty frames */
        if (frames[i].size == 0) continue;

        /* Frame header: ID(4) + Size(4, big-endian) + Flags(2) */
        uint8_t hdr[10];
        memcpy(hdr, frames[i].id, 4);
        encode_be32(frames[i].size, hdr + 4);
        hdr[8] = 0;  /* No flags in v2.3 output */
        hdr[9] = 0;
        fwrite(hdr, 1, 10, fp);
        fwrite(frames[i].data, 1, frames[i].size, fp);
        total += 10 + frames[i].size;
    }

    return total;
}

/* ---- Write ID3v1 tag ---- */

static void write_id3v1(FILE *fp, const ID3Frame *frames, int count)
{
    uint8_t tag[ID3V1_TAG_SIZE];
    memset(tag, 0, sizeof(tag));
    memcpy(tag, "TAG", 3);

    /* Extract metadata from frames */
    for (int i = 0; i < count; i++) {
        char *text = NULL;
        int is_text = (frames[i].id[0] == 'T' && frames[i].size > 0);
        if (is_text) text = text_frame_to_latin1(frames[i].data, frames[i].size);
        if (!text) continue;

        if (strcmp(frames[i].id, "TIT2") == 0) {
            strncpy((char *)tag + 3, text, 30);
        } else if (strcmp(frames[i].id, "TPE1") == 0) {
            strncpy((char *)tag + 33, text, 30);
        } else if (strcmp(frames[i].id, "TALB") == 0) {
            strncpy((char *)tag + 63, text, 30);
        } else if (strcmp(frames[i].id, "TYER") == 0) {
            strncpy((char *)tag + 93, text, 4);
        } else if (strcmp(frames[i].id, "TRCK") == 0) {
            int trk = atoi(text);
            if (trk > 0 && trk < 256) {
                tag[125] = 0;              /* ID3v1.1: byte before track must be 0 */
                tag[126] = (uint8_t)trk;   /* Track number */
            }
        } else if (strcmp(frames[i].id, "TCON") == 0) {
            /* Genre: try numeric first, then store text */
            strncpy((char *)tag + 97, text, 30);
            tag[127] = 0xFF; /* "Unknown" genre ID */
        }

        free(text);
    }

    fwrite(tag, 1, ID3V1_TAG_SIZE, fp);
}

/* ---- Public API ---- */

/* Build a UTF-8 text frame payload: [encoding byte 0x03][UTF-8 bytes]. */
static uint8_t *build_text_frame_utf8(const char *text, uint32_t *out_size)
{
    size_t n = strlen(text);
    uint8_t *buf = malloc(1 + n);
    if (!buf) return NULL;
    buf[0] = 0x03;  /* UTF-8 */
    memcpy(buf + 1, text, n);
    *out_size = (uint32_t)(1 + n);
    return buf;
}

/* Replace (or append) a text frame in the frames array with the given text. */
static void override_text_frame(ID3Frame *frames, int *frame_count,
                                 int max_frames, const char *frame_id,
                                 const char *text)
{
    uint32_t new_size = 0;
    uint8_t *new_data = build_text_frame_utf8(text, &new_size);
    if (!new_data) return;

    for (int i = 0; i < *frame_count; i++) {
        if (strcmp(frames[i].id, frame_id) == 0) {
            free(frames[i].data);
            frames[i].data = new_data;
            frames[i].size = new_size;
            frames[i].flags = 0;
            return;
        }
    }
    if (*frame_count >= max_frames) { free(new_data); return; }
    ID3Frame *f = &frames[*frame_count];
    memset(f->id, 0, sizeof(f->id));
    strncpy(f->id, frame_id, 4);
    f->data  = new_data;
    f->size  = new_size;
    f->flags = 0;
    (*frame_count)++;
}

/* Shared rewrite core. album_artist: TPE1 override (sync path).
 * edits: full-field overrides (metadata editor). out_path_override:
 * exact output path (in-place edit flow); NULL = /tmp temp file. */
static char *retag_core(const char *mp3_path, const char *album_artist,
                        const ZuunedId3Edits *edits,
                        const char *out_path_override, int disc_number)
{
    if (!mp3_path) return NULL;

    /* Read entire input file */
    FILE *fin = fopen(mp3_path, "rb");
    if (!fin) return NULL;

    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    if (file_size < 128 || file_size > 500 * 1024 * 1024) {
        fclose(fin);
        return NULL;
    }

    uint8_t *file_data = malloc((size_t)file_size);
    if (!file_data) { fclose(fin); return NULL; }

    size_t read_bytes = fread(file_data, 1, (size_t)file_size, fin);
    fclose(fin);

    if ((long)read_bytes != file_size) { free(file_data); return NULL; }

    /* Check for ID3v2 header */
    uint32_t tag_total_size = 0;
    uint8_t version_major = 0;
    ID3Frame frames[MAX_FRAMES];
    int frame_count = 0;

    if (file_size >= ID3V2_HEADER_SIZE &&
        file_data[0] == 'I' && file_data[1] == 'D' && file_data[2] == '3') {

        version_major = file_data[3];
        /* uint8_t version_minor = file_data[4]; */
        uint8_t flags = file_data[5];
        uint32_t tag_data_size = decode_syncsafe(file_data + 6);

        if (tag_data_size > MAX_TAG_SIZE) { free(file_data); return NULL; }

        tag_total_size = ID3V2_HEADER_SIZE + tag_data_size;

        /* Handle extended header — skip it */
        uint32_t frame_start = ID3V2_HEADER_SIZE;
        if (flags & 0x40) {
            /* Extended header present */
            if (frame_start + 4 <= tag_total_size) {
                uint32_t ext_size;
                if (version_major == 4)
                    ext_size = decode_syncsafe(file_data + frame_start);
                else
                    ext_size = decode_be32(file_data + frame_start);
                frame_start += ext_size;
            }
        }

        /* Handle footer (v2.4) */
        if ((flags & 0x10) && version_major == 4) {
            tag_total_size += 10;
        }

        /* If already v2.3 and no conversion needed, check if we should still rewrite
         * (we always rewrite to ensure ID3v1 is present) */

        /* Parse frames */
        if (frame_start < tag_total_size) {
            frame_count = parse_id3v2_frames(
                file_data + frame_start,
                tag_total_size - frame_start,
                version_major,
                frames, MAX_FRAMES
            );
        }
    }

    /* If no ID3v2 tag found, check for ID3v1 at the end */
    if (frame_count == 0 && file_size >= ID3V1_TAG_SIZE) {
        const uint8_t *v1 = file_data + file_size - ID3V1_TAG_SIZE;
        if (v1[0] == 'T' && v1[1] == 'A' && v1[2] == 'G') {
            /* Extract ID3v1 fields into frames */
            /* We'll pass through with no frames — audio still gets copied */
            tag_total_size = 0; /* No v2 tag to skip */
        }
    }

    /* Override TPE1 (artist) with album_artist if provided. Keeps the device
     * grouping tracks under one artist object instead of splitting on
     * per-track "feat. X" strings. */
    if (album_artist && album_artist[0]) {
        override_text_frame(frames, &frame_count, MAX_FRAMES, "TPE1", album_artist);
    }

    /* Metadata-editor overrides (UX-2 Edit Info). Empty strings keep
     * the existing frame; track/year 0 likewise. */
    if (edits) {
        if (edits->title && edits->title[0])
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TIT2", edits->title);
        if (edits->artist && edits->artist[0])
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TPE1", edits->artist);
        if (edits->album_artist && edits->album_artist[0])
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TPE2", edits->album_artist);
        if (edits->album && edits->album[0])
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TALB", edits->album);
        if (edits->genre && edits->genre[0])
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TCON", edits->genre);
        if (edits->track > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", edits->track);
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TRCK", buf);
        }
        if (edits->year > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", edits->year);
            override_text_frame(frames, &frame_count, MAX_FRAMES, "TYER", buf);
        }
    }

    /* Normalize a source TPOS, omit malformed values, or use the queued disc.
     * Remove duplicate TPOS frames before publishing one canonical value. */
    char disc[48] = {0};
    if (disc_number > 0) snprintf(disc, sizeof(disc), "%d", disc_number);
    for (int i = 0; i < frame_count;) {
        if (strcmp(frames[i].id, "TPOS") != 0) { ++i; continue; }
        if (!disc[0]) {
            char *text = text_frame_to_latin1(frames[i].data, frames[i].size);
            normalize_disc_tag(text, disc);
            free(text);
        }
        free(frames[i].data);
        memmove(&frames[i], &frames[i + 1], (size_t)(frame_count - i - 1) * sizeof(frames[0]));
        --frame_count;
    }
    if (disc[0]) override_text_frame(frames, &frame_count, MAX_FRAMES, "TPOS", disc);

    /* Find audio data start */
    long audio_start;
    if (tag_total_size > 0) {
        audio_start = (long)tag_total_size;
    } else {
        audio_start = 0;
    }

    /* Find MPEG sync to validate audio start */
    long sync_pos = find_mpeg_sync(file_data + audio_start, file_size - audio_start);
    if (sync_pos < 0) {
        /* Try searching from the beginning */
        sync_pos = find_mpeg_sync(file_data, file_size);
        if (sync_pos >= 0) {
            audio_start = sync_pos;
        } else {
            /* No MPEG sync found — not a valid MP3 */
            for (int i = 0; i < frame_count; i++) free(frames[i].data);
            free(file_data);
            return NULL;
        }
    } else {
        audio_start += sync_pos;
    }

    /* Calculate audio data size (exclude trailing ID3v1 if present) */
    long audio_end = file_size;
    if (file_size >= ID3V1_TAG_SIZE) {
        const uint8_t *tail = file_data + file_size - ID3V1_TAG_SIZE;
        if (tail[0] == 'T' && tail[1] == 'A' && tail[2] == 'G') {
            audio_end -= ID3V1_TAG_SIZE;
        }
    }
    long audio_size = audio_end - audio_start;
    if (audio_size <= 0) {
        for (int i = 0; i < frame_count; i++) free(frames[i].data);
        free(file_data);
        return NULL;
    }

    /* Build output path */
    char output_path[1024];
    if (out_path_override) {
        snprintf(output_path, sizeof(output_path), "%s", out_path_override);
    } else {
        const char *base = strrchr(mp3_path, '/');
        base = base ? base + 1 : mp3_path;
        const char *dot = strrchr(base, '.');
        size_t base_len = dot ? (size_t)(dot - base) : strlen(base);
        snprintf(output_path, sizeof(output_path),
                 "/tmp/zuuned_retag_%.*s_%d.mp3",
                 (int)base_len, base, (int)getpid());
    }

    /* Write output file */
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        for (int i = 0; i < frame_count; i++) free(frames[i].data);
        free(file_data);
        return NULL;
    }

    if (frame_count > 0) {
        /* Calculate total frame data size first */
        uint32_t frames_size = 0;
        for (int i = 0; i < frame_count; i++) {
            if (frames[i].size > 0)
                frames_size += 10 + frames[i].size;
        }

        uint32_t tag_content_size = frames_size + PADDING_SIZE;

        /* Write ID3v2.3 header */
        uint8_t header[ID3V2_HEADER_SIZE];
        header[0] = 'I'; header[1] = 'D'; header[2] = '3';
        header[3] = 3;   /* Version 2.3 */
        header[4] = 0;   /* Revision 0 */
        header[5] = 0;   /* No flags */
        encode_syncsafe(tag_content_size, header + 6);
        fwrite(header, 1, ID3V2_HEADER_SIZE, fout);

        /* Write frames */
        write_id3v2_3_frames(fout, frames, frame_count);

        /* Write padding */
        uint8_t zeros[1024];
        memset(zeros, 0, sizeof(zeros));
        fwrite(zeros, 1, PADDING_SIZE, fout);
    }

    /* Write audio data */
    fwrite(file_data + audio_start, 1, (size_t)audio_size, fout);

    /* Write ID3v1 tag */
    if (frame_count > 0) {
        write_id3v1(fout, frames, frame_count);
    }

    fclose(fout);

    /* Cleanup */
    for (int i = 0; i < frame_count; i++) free(frames[i].data);
    free(file_data);

    fprintf(stderr, "[zuuned] retagged to ID3v2.3: %s\n", output_path);
    return strdup(output_path);
}

char *zuuned_retag_mp3(const char *mp3_path, const char *album_artist)
{
    return zuuned_retag_mp3_with_disc(mp3_path, album_artist, 0);
}

char *zuuned_retag_mp3_with_disc(const char *mp3_path, const char *album_artist, int disc_number)
{
    return retag_core(mp3_path, album_artist, NULL, NULL, disc_number);
}

int zuuned_edit_mp3_tags(const char *mp3_path, const ZuunedId3Edits *edits)
{
    if (!mp3_path || !edits) return -1;

    /* Rewrite into a sibling temp file (same filesystem), then rename
     * over the original — atomic, no cross-device copy. */
    char tmp_path[1024];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.zuuned_edit.tmp",
                 mp3_path) >= (int)sizeof(tmp_path))
        return -1;

    char *out = retag_core(mp3_path, NULL, edits, tmp_path, 0);
    if (!out) return -1;
    free(out);

    if (rename(tmp_path, mp3_path) != 0) {
        unlink(tmp_path);
        return -1;
    }
    fprintf(stderr, "[zuuned] tags edited in place: %s\n", mp3_path);
    return 0;
}
