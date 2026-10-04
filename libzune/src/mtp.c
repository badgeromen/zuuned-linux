/*
 * mtp.c — MTP (Media Transfer Protocol) operations implementation
 *
 * Implements MTP-level operations on top of the PTP transport layer.
 * Each function builds the appropriate parameters and data payloads,
 * calls ptp_transaction(), and parses the response data.
 *
 * Wire format: all multi-byte values are little-endian. MTP strings
 * use a count-prefixed UCS-2LE encoding. Arrays use a uint32 count
 * prefix followed by typed elements.
 *
 * Part of the native PTP/MTP stack that replaces vendored libmtp.
 *
 * Copyright (c) 2026 BadgerOmens
 */

#include "mtp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================
 * Little-Endian Pack/Unpack Helpers
 *
 * Duplicated from ptp.c since those are static. These are simple
 * inline helpers with no external dependencies.
 * =================================================================== */

static inline void le16_pack(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

static inline void le32_pack(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline void le64_pack(uint8_t *buf, uint64_t val)
{
    for (int i = 0; i < 8; i++)
        buf[i] = (uint8_t)((val >> (i * 8)) & 0xFF);
}

static inline uint16_t le16_unpack(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline uint32_t le32_unpack(const uint8_t *buf)
{
    return (uint32_t)buf[0]
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
}

static inline uint64_t le64_unpack(const uint8_t *buf)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; i++)
        val |= (uint64_t)buf[i] << (i * 8);
    return val;
}

/* ===================================================================
 * MTP String Helpers
 *
 * MTP string wire format:
 *   Byte 0:       uint8_t  num_chars (UCS-2 chars including null terminator)
 *   Bytes 1...:   uint16_t chars[num_chars] (UCS-2LE, null terminated)
 *
 * Empty string: num_chars=0, total wire size = 1 byte.
 * Non-empty string of N characters: num_chars = N+1 (includes null),
 *   total wire size = 1 + (N+1)*2 bytes.
 * =================================================================== */

/*
 * utf8_to_ucs2_chars — Convert UTF-8 to an array of UCS-2LE code units.
 *
 * Handles ASCII (1-byte), 2-byte, and 3-byte UTF-8 sequences. The Zune
 * only uses BMP characters, so surrogate pairs (4-byte UTF-8) are not
 * needed.
 *
 *   utf8:       input UTF-8 string
 *   out_chars:  [out] allocated array of uint16_t code units (no null), caller frees
 *   out_count:  [out] number of code units written
 *
 * Returns 0 on success, -1 on error.
 */
static int utf8_to_ucs2_chars(const char *utf8, uint16_t **out_chars,
                               uint32_t *out_count)
{
    const uint8_t *p = (const uint8_t *)utf8;
    uint32_t alloc_count;
    uint32_t count;
    uint16_t *chars;

    if (!utf8 || *utf8 == '\0') {
        *out_chars = NULL;
        *out_count = 0;
        return 0;
    }

    /* First pass: count UCS-2 characters */
    alloc_count = 0;
    for (const uint8_t *q = p; *q; ) {
        if (*q < 0x80) {
            q += 1;
        } else if ((*q & 0xE0) == 0xC0) {
            if ((q[1] & 0xC0) != 0x80) {
                fprintf(stderr, "[mtp] invalid UTF-8 at byte offset %ld\n",
                        (long)(q - p));
                return -1;
            }
            q += 2;
        } else if ((*q & 0xF0) == 0xE0) {
            if ((q[1] & 0xC0) != 0x80 || (q[2] & 0xC0) != 0x80) {
                fprintf(stderr, "[mtp] invalid UTF-8 at byte offset %ld\n",
                        (long)(q - p));
                return -1;
            }
            q += 3;
        } else {
            /* 4-byte or invalid — skip as replacement character */
            fprintf(stderr, "[mtp] skipping non-BMP character at byte offset %ld\n",
                    (long)(q - p));
            if ((*q & 0xF8) == 0xF0 &&
                (q[1] & 0xC0) == 0x80 &&
                (q[2] & 0xC0) == 0x80 &&
                (q[3] & 0xC0) == 0x80) {
                q += 4;
            } else {
                q += 1;
            }
        }
        alloc_count++;
    }

    chars = (uint16_t *)malloc(alloc_count * sizeof(uint16_t));
    if (!chars) {
        fprintf(stderr, "[mtp] malloc failed for UCS-2 conversion\n");
        return -1;
    }

    /* Second pass: convert */
    count = 0;
    for (const uint8_t *q = p; *q; ) {
        uint16_t cp;
        if (*q < 0x80) {
            cp = *q;
            q += 1;
        } else if ((*q & 0xE0) == 0xC0) {
            cp = (uint16_t)((*q & 0x1F) << 6) | (q[1] & 0x3F);
            q += 2;
        } else if ((*q & 0xF0) == 0xE0) {
            cp = (uint16_t)((*q & 0x0F) << 12)
               | (uint16_t)((q[1] & 0x3F) << 6)
               | (q[2] & 0x3F);
            q += 3;
        } else {
            /* Non-BMP: substitute U+FFFD */
            cp = 0xFFFD;
            if ((*q & 0xF8) == 0xF0 &&
                (q[1] & 0xC0) == 0x80 &&
                (q[2] & 0xC0) == 0x80 &&
                (q[3] & 0xC0) == 0x80) {
                q += 4;
            } else {
                q += 1;
            }
        }
        chars[count++] = cp;
    }

    *out_chars = chars;
    *out_count = count;
    return 0;
}

/*
 * ucs2_char_to_utf8 — Write a single UCS-2 code point as UTF-8.
 *
 * Returns the number of bytes written (1, 2, or 3).
 */
static int ucs2_char_to_utf8(uint16_t cp, uint8_t *out)
{
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    } else {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
}

uint8_t *mtp_string_to_ucs2(const char *utf8, uint32_t *out_len)
{
    uint16_t *chars = NULL;
    uint32_t char_count = 0;
    uint8_t *wire;
    uint32_t wire_len;

    if (!utf8 || *utf8 == '\0') {
        /* Empty string: just the count byte = 0 */
        wire = (uint8_t *)malloc(1);
        if (!wire) return NULL;
        wire[0] = 0;
        if (out_len) *out_len = 1;
        return wire;
    }

    if (utf8_to_ucs2_chars(utf8, &chars, &char_count) < 0)
        return NULL;

    if (char_count == 0) {
        free(chars);
        wire = (uint8_t *)malloc(1);
        if (!wire) return NULL;
        wire[0] = 0;
        if (out_len) *out_len = 1;
        return wire;
    }

    /*
     * Wire format: 1 byte count + (char_count + 1) * 2 bytes
     * The +1 is for the null terminator in UCS-2.
     * Count includes the null terminator.
     */
    /* MTP string length lives in ONE byte (max 255 chars incl. null).
     * A longer string used to keep all its characters while the count
     * byte clamped at 255 — a malformed dataset the device chokes on.
     * Truncate the CHARACTERS to match the count instead. */
    if (char_count > 254) {
        fprintf(stderr, "[mtp] string truncated to 254 chars (was %u)\n",
                char_count);
        char_count = 254;
    }
    uint32_t num_chars = char_count + 1; /* including null */
    wire_len = 1 + num_chars * 2;

    wire = (uint8_t *)malloc(wire_len);
    if (!wire) {
        free(chars);
        return NULL;
    }

    wire[0] = (uint8_t)num_chars;

    /* Pack UCS-2LE characters */
    for (uint32_t i = 0; i < char_count; i++) {
        le16_pack(wire + 1 + i * 2, chars[i]);
    }
    /* Null terminator */
    le16_pack(wire + 1 + char_count * 2, 0x0000);

    free(chars);

    if (out_len) *out_len = wire_len;
    return wire;
}

char *mtp_ucs2_to_string(const uint8_t *data, uint32_t len)
{
    uint8_t num_chars;
    uint32_t i;
    uint8_t utf8_buf[4];
    uint32_t utf8_total;
    char *result;
    uint32_t offset;

    if (!data || len < 1)
        return NULL;

    num_chars = data[0];

    if (num_chars == 0) {
        /* Empty MTP string */
        result = (char *)malloc(1);
        if (result) result[0] = '\0';
        return result;
    }

    /* Validate that enough bytes are present */
    uint32_t needed = 1 + (uint32_t)num_chars * 2;
    if (len < needed) {
        fprintf(stderr, "[mtp] ucs2_to_string: need %u bytes but only %u available\n",
                needed, len);
        return NULL;
    }

    /*
     * First pass: calculate total UTF-8 byte length.
     * The last character is the null terminator — skip it in output.
     */
    utf8_total = 0;
    for (i = 0; i < (uint32_t)(num_chars - 1); i++) {
        uint16_t cp = le16_unpack(data + 1 + i * 2);
        if (cp == 0) break; /* early null terminator */
        utf8_total += (uint32_t)ucs2_char_to_utf8(cp, utf8_buf);
    }

    result = (char *)malloc(utf8_total + 1);
    if (!result) return NULL;

    /* Second pass: write UTF-8 */
    offset = 0;
    for (i = 0; i < (uint32_t)(num_chars - 1); i++) {
        uint16_t cp = le16_unpack(data + 1 + i * 2);
        if (cp == 0) break;
        offset += (uint32_t)ucs2_char_to_utf8(cp, (uint8_t *)result + offset);
    }
    result[offset] = '\0';

    return result;
}

/*
 * mtp_read_string — Read an MTP string from a data buffer at an offset.
 *
 * Advances *offset past the string. Returns an allocated UTF-8 string
 * (caller frees), or NULL on error.
 */
static char *mtp_read_string(const uint8_t *data, uint32_t data_len,
                              uint32_t *offset)
{
    uint8_t num_chars;
    uint32_t str_wire_len;
    char *result;

    if (*offset >= data_len) return NULL;

    num_chars = data[*offset];
    str_wire_len = 1 + (uint32_t)num_chars * 2;

    if (*offset + str_wire_len > data_len) {
        fprintf(stderr, "[mtp] read_string: truncated at offset %u\n", *offset);
        return NULL;
    }

    result = mtp_ucs2_to_string(data + *offset, str_wire_len);
    *offset += str_wire_len;

    return result;
}

/*
 * mtp_pack_string — Pack an MTP string into a buffer at an offset.
 *
 * Writes the MTP string wire format at buf + *offset and advances
 * *offset. Returns 0 on success, -1 on error.
 */
static int mtp_pack_string(uint8_t *buf, uint32_t buf_len,
                            uint32_t *offset, const char *str)
{
    uint32_t wire_len = 0;
    uint8_t *wire = mtp_string_to_ucs2(str, &wire_len);
    if (!wire) return -1;

    if (*offset + wire_len > buf_len) {
        free(wire);
        return -1;
    }

    memcpy(buf + *offset, wire, wire_len);
    *offset += wire_len;
    free(wire);
    return 0;
}

/* ===================================================================
 * Storage Operations
 * =================================================================== */

int mtp_get_storage_ids(ptp_session_t *s, uint32_t **ids, int *count)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    uint16_t resp = 0;
    int ret;

    if (ids) *ids = NULL;
    if (count) *count = 0;

    ret = ptp_transaction(s, PTP_OC_GetStorageIDs,
                          NULL, 0,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          &data, &len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_storage_ids: failed (response 0x%04X)\n", resp);
        return -1;
    }

    if (!data || len < 4) {
        fprintf(stderr, "[mtp] get_storage_ids: no data or too short (%u bytes)\n", len);
        free(data);
        return -1;
    }

    uint32_t n = le32_unpack(data);
    if (len < 4 + n * 4) {
        fprintf(stderr, "[mtp] get_storage_ids: truncated (count=%u, len=%u)\n", n, len);
        free(data);
        return -1;
    }

    uint32_t *result = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!result) {
        free(data);
        return -1;
    }

    for (uint32_t i = 0; i < n; i++)
        result[i] = le32_unpack(data + 4 + i * 4);

    free(data);

    if (ids) *ids = result;
    else free(result);

    if (count) *count = (int)n;

    return 0;
}

int mtp_get_storage_info(ptp_session_t *s, uint32_t storage_id,
                         mtp_storage_info_t *info)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    uint16_t resp = 0;
    uint32_t param = storage_id;
    uint32_t offset;
    int ret;

    memset(info, 0, sizeof(*info));
    info->storage_id = storage_id;

    ret = ptp_transaction(s, PTP_OC_GetStorageInfo,
                          &param, 1,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          &data, &len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_storage_info 0x%08X: failed (response 0x%04X)\n",
                storage_id, resp);
        return -1;
    }

    /*
     * StorageInfo dataset layout:
     *   uint16  StorageType
     *   uint16  FilesystemType
     *   uint16  AccessCapability
     *   uint64  MaxCapacity
     *   uint64  FreeSpaceInBytes
     *   uint32  FreeSpaceInImages
     *   MTP String StorageDescription
     *   MTP String VolumeLabel
     *
     * Minimum fixed portion: 2+2+2+8+8+4 = 26 bytes
     */
    if (!data || len < 26) {
        fprintf(stderr, "[mtp] get_storage_info: data too short (%u bytes)\n", len);
        free(data);
        return -1;
    }

    info->storage_type      = le16_unpack(data + 0);
    info->filesystem_type   = le16_unpack(data + 2);
    info->access_capability = le16_unpack(data + 4);
    info->max_capacity      = le64_unpack(data + 6);
    info->free_space        = le64_unpack(data + 14);
    /* FreeSpaceInImages at offset 22 — skip (uint32) */

    offset = 26;
    info->storage_description = mtp_read_string(data, len, &offset);
    info->volume_label        = mtp_read_string(data, len, &offset);

    free(data);
    return 0;
}

void mtp_free_storage_info(mtp_storage_info_t *info)
{
    if (!info) return;
    free(info->storage_description);
    info->storage_description = NULL;
    free(info->volume_label);
    info->volume_label = NULL;
}

/* ===================================================================
 * Object Operations
 * =================================================================== */

int mtp_get_object_handles(ptp_session_t *s, uint32_t storage,
                           uint32_t format, uint32_t parent,
                           uint32_t **handles, int *count)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    uint16_t resp = 0;
    uint32_t params[3];
    int ret;

    if (handles) *handles = NULL;
    if (count) *count = 0;

    params[0] = storage;
    params[1] = format;
    params[2] = parent;

    ret = ptp_transaction(s, PTP_OC_GetObjectHandles,
                          params, 3,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          &data, &len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_object_handles: failed (response 0x%04X)\n", resp);
        return -1;
    }

    if (!data || len < 4) {
        fprintf(stderr, "[mtp] get_object_handles: no data or too short\n");
        free(data);
        /* Zero handles is valid — some storages are empty */
        return (len == 0) ? 0 : -1;
    }

    uint32_t n = le32_unpack(data);
    if (len < 4 + n * 4) {
        fprintf(stderr, "[mtp] get_object_handles: truncated (count=%u, len=%u)\n",
                n, len);
        free(data);
        return -1;
    }

    uint32_t *result = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!result) {
        free(data);
        return -1;
    }

    for (uint32_t i = 0; i < n; i++)
        result[i] = le32_unpack(data + 4 + i * 4);

    free(data);

    if (handles) *handles = result;
    else free(result);

    if (count) *count = (int)n;

    return 0;
}

int mtp_get_object_info(ptp_session_t *s, uint32_t handle,
                        mtp_object_info_t *info)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    uint16_t resp = 0;
    uint32_t param = handle;
    uint32_t offset;
    int ret;

    memset(info, 0, sizeof(*info));

    ret = ptp_transaction(s, PTP_OC_GetObjectInfo,
                          &param, 1,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          &data, &len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_object_info 0x%08X: failed (response 0x%04X)\n",
                handle, resp);
        return -1;
    }

    /*
     * ObjectInfo dataset layout (all little-endian):
     *   Offset  Size  Field
     *   0       4     StorageID
     *   4       2     ObjectFormat
     *   6       2     ProtectionStatus
     *   8       4     ObjectCompressedSize
     *   12      2     ThumbFormat
     *   14      4     ThumbCompressedSize
     *   18      4     ThumbPixWidth
     *   22      4     ThumbPixHeight
     *   26      4     ImagePixWidth
     *   30      4     ImagePixHeight
     *   34      4     ImageBitDepth
     *   38      4     ParentObject
     *   42      2     AssociationType
     *   44      4     AssociationDesc
     *   48      4     SequenceNumber
     *   52      MTP String Filename
     *   ...     MTP String CaptureDate
     *   ...     MTP String ModificationDate
     *   ...     MTP String Keywords
     *
     * Fixed portion: 52 bytes minimum.
     */
    if (!data || len < 52) {
        fprintf(stderr, "[mtp] get_object_info: data too short (%u bytes)\n", len);
        free(data);
        return -1;
    }

    info->storage_id             = le32_unpack(data + 0);
    info->object_format          = le16_unpack(data + 4);
    /* ProtectionStatus at offset 6 — skip */
    info->object_compressed_size = le32_unpack(data + 8);
    /* ThumbFormat, ThumbCompressedSize, ThumbPixWidth, ThumbPixHeight — skip */
    /* ImagePixWidth, ImagePixHeight, ImageBitDepth — skip */
    info->parent_object          = le32_unpack(data + 38);
    info->association_type       = le16_unpack(data + 42);
    /* AssociationDesc at 44, SequenceNumber at 48 — skip */

    /* Parse the Filename string at offset 52 */
    offset = 52;
    info->filename = mtp_read_string(data, len, &offset);

    /* CaptureDate, ModificationDate, Keywords — skip for now */

    free(data);
    return 0;
}

void mtp_free_object_info(mtp_object_info_t *info)
{
    if (!info) return;
    free(info->filename);
    info->filename = NULL;
}

int mtp_get_object(ptp_session_t *s, uint32_t handle,
                   uint8_t **data, uint32_t *len)
{
    uint16_t resp = 0;
    uint32_t param = handle;
    int ret;

    if (data) *data = NULL;
    if (len) *len = 0;

    ret = ptp_transaction(s, PTP_OC_GetObject,
                          &param, 1,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          data, len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_object 0x%08X: failed (response 0x%04X)\n",
                handle, resp);
        return -1;
    }

    return 0;
}

int mtp_get_object_to_file(ptp_session_t *s, uint32_t handle,
                           const char *path)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    FILE *fp;
    size_t written;

    if (mtp_get_object(s, handle, &data, &len) < 0)
        return -1;

    if (!data || len == 0) {
        free(data);
        fprintf(stderr, "[mtp] get_object_to_file 0x%08X: empty data\n", handle);
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[mtp] get_object_to_file: cannot open '%s' for writing\n",
                path);
        free(data);
        return -1;
    }

    written = fwrite(data, 1, len, fp);
    fclose(fp);
    free(data);

    if (written != len) {
        fprintf(stderr, "[mtp] get_object_to_file: wrote %zu of %u bytes to '%s'\n",
                written, len, path);
        return -1;
    }

    return 0;
}

/*
 * mtp_pack_object_info — Pack an mtp_object_info_t into ObjectInfo wire format.
 *
 * Allocates and returns the packed buffer. Caller frees.
 * Sets *out_len to the total packed size.
 *
 * Returns the packed buffer, or NULL on error.
 */
static uint8_t *mtp_pack_object_info(mtp_object_info_t *info, uint32_t *out_len)
{
    /*
     * We need to pack the full ObjectInfo dataset. Fields we don't
     * use are set to zero. The four trailing strings (Filename,
     * CaptureDate, ModificationDate, Keywords) must be present.
     *
     * Fixed portion: 52 bytes
     * Variable portion: 4 MTP strings
     */
    uint32_t filename_wire_len = 0;
    uint8_t *filename_wire = mtp_string_to_ucs2(info->filename, &filename_wire_len);
    if (!filename_wire) return NULL;

    /* Empty strings for CaptureDate, ModificationDate, Keywords */
    uint32_t empty_str_size = 1; /* just count byte = 0 */

    uint32_t total = 52 + filename_wire_len + empty_str_size * 3;
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) {
        free(filename_wire);
        return NULL;
    }

    /* Pack fixed fields */
    le32_pack(buf + 0,  info->storage_id);
    le16_pack(buf + 4,  info->object_format);
    le16_pack(buf + 6,  0x0000);  /* ProtectionStatus: no protection */
    le32_pack(buf + 8,  info->object_compressed_size);
    le16_pack(buf + 12, 0x0000);  /* ThumbFormat */
    le32_pack(buf + 14, 0);       /* ThumbCompressedSize */
    le32_pack(buf + 18, 0);       /* ThumbPixWidth */
    le32_pack(buf + 22, 0);       /* ThumbPixHeight */
    le32_pack(buf + 26, 0);       /* ImagePixWidth */
    le32_pack(buf + 30, 0);       /* ImagePixHeight */
    le32_pack(buf + 34, 0);       /* ImageBitDepth */
    le32_pack(buf + 38, info->parent_object);
    le16_pack(buf + 42, info->association_type);
    le32_pack(buf + 44, 0);       /* AssociationDesc */
    le32_pack(buf + 48, 0);       /* SequenceNumber */

    /* Pack Filename */
    uint32_t off = 52;
    memcpy(buf + off, filename_wire, filename_wire_len);
    off += filename_wire_len;
    free(filename_wire);

    /* Pack empty CaptureDate, ModificationDate, Keywords */
    buf[off++] = 0; /* CaptureDate: empty */
    buf[off++] = 0; /* ModificationDate: empty */
    buf[off++] = 0; /* Keywords: empty */

    if (out_len) *out_len = total;
    return buf;
}

int mtp_send_object_info(ptp_session_t *s, uint32_t storage,
                         uint32_t parent, mtp_object_info_t *info,
                         uint32_t *new_handle)
{
    uint8_t *packed = NULL;
    uint32_t packed_len = 0;
    uint16_t resp = 0;
    uint32_t params[2];
    int ret;

    if (new_handle) *new_handle = 0;

    packed = mtp_pack_object_info(info, &packed_len);
    if (!packed) {
        fprintf(stderr, "[mtp] send_object_info: failed to pack ObjectInfo\n");
        return -1;
    }

    params[0] = storage;
    params[1] = parent;

    /*
     * SendObjectInfo (0x100C):
     *   Command params: storage_id, parent_handle
     *   Data: ObjectInfo dataset
     *   Response params: new_storage (p1), new_parent (p2), new_handle (p3)
     *
     * SendObjectInfo response contains:
     *   param1 = new storage ID
     *   param2 = new parent handle
     *   param3 = new object handle
     */
    uint32_t resp_params[PTP_MAX_PARAMS];
    int resp_nparams = 0;

    ret = ptp_transaction(s, PTP_OC_SendObjectInfo,
                          params, 2,
                          PTP_DP_SENDDATA,
                          packed, (uint64_t)packed_len,
                          NULL, NULL,
                          &resp, resp_params, &resp_nparams);
    free(packed);

    if (ret < 0) {
        fprintf(stderr, "[mtp] send_object_info: failed (response 0x%04X)\n", resp);
        return -1;
    }

    /* Extract new handle from response param3. A success response without
     * a real handle is useless — properties would target handle 0 and the
     * caller would record a ghost object. Treat it as failure. */
    if (new_handle) {
        *new_handle = (resp_nparams >= 3) ? resp_params[2] : 0;
        if (*new_handle == 0) {
            fprintf(stderr, "[mtp] send_object_info: OK but no handle in "
                    "response (%d params)\n", resp_nparams);
            return -1;
        }
    }

    return 0;
}

int mtp_send_object(ptp_session_t *s, const uint8_t *data, uint32_t len)
{
    uint16_t resp = 0;
    int ret;

    ret = ptp_transaction(s, PTP_OC_SendObject,
                          NULL, 0,
                          PTP_DP_SENDDATA,
                          (uint8_t *)data, (uint64_t)len,
                          NULL, NULL,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] send_object: failed (response 0x%04X)\n", resp);
        return -1;
    }

    return 0;
}

int mtp_send_object_from_file(ptp_session_t *s, const char *path,
                              uint32_t len)
{
    FILE *fp;
    uint8_t *data;
    size_t bytes_read;
    int ret;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[mtp] send_object_from_file: cannot open '%s'\n", path);
        return -1;
    }

    data = (uint8_t *)malloc(len);
    if (!data) {
        fprintf(stderr, "[mtp] send_object_from_file: malloc failed for %u bytes\n",
                len);
        fclose(fp);
        return -1;
    }

    bytes_read = fread(data, 1, len, fp);
    fclose(fp);

    if (bytes_read != len) {
        fprintf(stderr, "[mtp] send_object_from_file: read %zu of %u bytes from '%s'\n",
                bytes_read, len, path);
        free(data);
        return -1;
    }

    ret = mtp_send_object(s, data, len);
    free(data);
    return ret;
}

int mtp_delete_object(ptp_session_t *s, uint32_t handle)
{
    uint16_t resp = 0;
    uint32_t params[2];
    int ret;

    /*
     * DeleteObject (0x100B):
     *   Param1: object handle
     *   Param2: object format (0x00000000 = all formats / don't care)
     */
    params[0] = handle;
    params[1] = 0x00000000;

    ret = ptp_transaction(s, PTP_OC_DeleteObject,
                          params, 2,
                          PTP_DP_NODATA,
                          NULL, 0,
                          NULL, NULL,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] delete_object 0x%08X: failed (response 0x%04X)\n",
                handle, resp);
        return -1;
    }

    return 0;
}

/* ===================================================================
 * Property Operations
 * =================================================================== */

int mtp_get_object_prop_value(ptp_session_t *s, uint32_t handle,
                              uint16_t prop, uint8_t **data,
                              uint32_t *len)
{
    uint16_t resp = 0;
    uint32_t params[2];
    int ret;

    if (data) *data = NULL;
    if (len) *len = 0;

    params[0] = handle;
    params[1] = (uint32_t)prop;

    ret = ptp_transaction(s, MTP_OC_GetObjectPropValue,
                          params, 2,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          data, len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_object_prop_value 0x%08X prop 0x%04X: "
                "failed (response 0x%04X)\n", handle, prop, resp);
        return -1;
    }

    return 0;
}

int mtp_set_object_prop_value_u16(ptp_session_t *s, uint32_t handle,
                                  uint16_t prop, uint16_t val)
{
    uint16_t resp = 0;
    uint32_t params[2];
    uint8_t data[2];
    int ret;

    params[0] = handle;
    params[1] = (uint32_t)prop;

    le16_pack(data, val);

    ret = ptp_transaction(s, MTP_OC_SetObjectPropValue,
                          params, 2,
                          PTP_DP_SENDDATA,
                          data, 2,
                          NULL, NULL,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] set_object_prop_value_u16 0x%08X prop 0x%04X "
                "val 0x%04X: failed (response 0x%04X)\n",
                handle, prop, val, resp);
        return -1;
    }

    return 0;
}

int mtp_set_object_prop_value_u32(ptp_session_t *s, uint32_t handle,
                                  uint16_t prop, uint32_t val)
{
    uint16_t resp = 0;
    uint32_t params[2];
    uint8_t data[4];
    int ret;

    params[0] = handle;
    params[1] = (uint32_t)prop;

    le32_pack(data, val);

    ret = ptp_transaction(s, MTP_OC_SetObjectPropValue,
                          params, 2,
                          PTP_DP_SENDDATA,
                          data, 4,
                          NULL, NULL,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] set_object_prop_value_u32 0x%08X prop 0x%04X "
                "val 0x%08X: failed (response 0x%04X)\n",
                handle, prop, val, resp);
        return -1;
    }

    return 0;
}

int mtp_set_object_prop_value_str(ptp_session_t *s, uint32_t handle,
                                  uint16_t prop, const char *str)
{
    uint16_t resp = 0;
    uint32_t params[2];
    uint8_t *wire = NULL;
    uint32_t wire_len = 0;
    int ret;

    params[0] = handle;
    params[1] = (uint32_t)prop;

    wire = mtp_string_to_ucs2(str, &wire_len);
    if (!wire) {
        fprintf(stderr, "[mtp] set_object_prop_value_str: string conversion failed\n");
        return -1;
    }

    ret = ptp_transaction(s, MTP_OC_SetObjectPropValue,
                          params, 2,
                          PTP_DP_SENDDATA,
                          wire, (uint64_t)wire_len,
                          NULL, NULL,
                          &resp, NULL, NULL);
    free(wire);

    if (ret < 0) {
        fprintf(stderr, "[mtp] set_object_prop_value_str 0x%08X prop 0x%04X: "
                "failed (response 0x%04X)\n", handle, prop, resp);
        return -1;
    }

    return 0;
}

int mtp_set_object_prop_value_ucs2(ptp_session_t *s, uint32_t handle,
                                   uint16_t prop, const uint16_t *ucs2,
                                   uint32_t count)
{
    uint16_t resp = 0;
    uint32_t params[2];
    uint32_t data_len;
    uint8_t *data;
    int ret;

    params[0] = handle;
    params[1] = (uint32_t)prop;

    /*
     * AUINT16 wire format:
     *   uint32  count       — number of uint16 elements
     *   uint16  values[]    — the array elements
     *
     * Total: 4 + count * 2 bytes
     */
    data_len = 4 + count * 2;
    data = (uint8_t *)malloc(data_len);
    if (!data) {
        fprintf(stderr, "[mtp] set_object_prop_value_ucs2: malloc failed\n");
        return -1;
    }

    le32_pack(data, count);
    for (uint32_t i = 0; i < count; i++)
        le16_pack(data + 4 + i * 2, ucs2[i]);

    ret = ptp_transaction(s, MTP_OC_SetObjectPropValue,
                          params, 2,
                          PTP_DP_SENDDATA,
                          data, (uint64_t)data_len,
                          NULL, NULL,
                          &resp, NULL, NULL);
    free(data);

    if (ret < 0) {
        fprintf(stderr, "[mtp] set_object_prop_value_ucs2 0x%08X prop 0x%04X: "
                "failed (response 0x%04X)\n", handle, prop, resp);
        return -1;
    }

    return 0;
}

int mtp_get_object_prop_list(ptp_session_t *s, uint32_t handle,
                             uint32_t format, uint16_t prop,
                             uint32_t group, uint32_t depth,
                             uint8_t **data, uint32_t *len)
{
    uint16_t resp = 0;
    uint32_t params[5];
    int ret;

    if (data) *data = NULL;
    if (len) *len = 0;

    params[0] = handle;
    params[1] = format;
    params[2] = (uint32_t)prop;
    params[3] = group;
    params[4] = depth;

    ret = ptp_transaction(s, MTP_OC_GetObjectPropList,
                          params, 5,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          data, len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_object_prop_list 0x%08X: "
                "failed (response 0x%04X)\n", handle, resp);
        return -1;
    }

    return 0;
}

/* ===================================================================
 * References (album/playlist track lists)
 * =================================================================== */

int mtp_get_object_references(ptp_session_t *s, uint32_t handle,
                              uint32_t **refs, int *count)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    uint16_t resp = 0;
    uint32_t param = handle;
    int ret;

    if (refs) *refs = NULL;
    if (count) *count = 0;

    ret = ptp_transaction(s, MTP_OC_GetObjectReferences,
                          &param, 1,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          &data, &len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_object_references 0x%08X: "
                "failed (response 0x%04X)\n", handle, resp);
        return -1;
    }

    if (!data || len < 4) {
        /* No references is valid (empty array) */
        free(data);
        return 0;
    }

    uint32_t n = le32_unpack(data);
    if (len < 4 + n * 4) {
        fprintf(stderr, "[mtp] get_object_references: truncated "
                "(count=%u, len=%u)\n", n, len);
        free(data);
        return -1;
    }

    uint32_t *result = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!result) {
        free(data);
        return -1;
    }

    for (uint32_t i = 0; i < n; i++)
        result[i] = le32_unpack(data + 4 + i * 4);

    free(data);

    if (refs) *refs = result;
    else free(result);

    if (count) *count = (int)n;

    return 0;
}

int mtp_set_object_references(ptp_session_t *s, uint32_t handle,
                              uint32_t *refs, int count)
{
    uint16_t resp = 0;
    uint32_t param = handle;
    uint32_t data_len;
    uint8_t *data;
    int ret;

    /*
     * SetObjectReferences wire format:
     *   uint32  count
     *   uint32  handles[count]
     */
    data_len = 4 + (uint32_t)count * 4;
    data = (uint8_t *)malloc(data_len);
    if (!data) {
        fprintf(stderr, "[mtp] set_object_references: malloc failed\n");
        return -1;
    }

    le32_pack(data, (uint32_t)count);
    for (int i = 0; i < count; i++)
        le32_pack(data + 4 + i * 4, refs[i]);

    ret = ptp_transaction(s, MTP_OC_SetObjectReferences,
                          &param, 1,
                          PTP_DP_SENDDATA,
                          data, (uint64_t)data_len,
                          NULL, NULL,
                          &resp, NULL, NULL);
    free(data);

    if (ret < 0) {
        fprintf(stderr, "[mtp] set_object_references 0x%08X: "
                "failed (response 0x%04X)\n", handle, resp);
        return -1;
    }

    return 0;
}

/* ===================================================================
 * Folders
 * =================================================================== */

int mtp_create_folder(ptp_session_t *s, uint32_t parent,
                      uint32_t storage, const char *name,
                      uint32_t *new_handle)
{
    mtp_object_info_t folder_info;
    int ret;

    memset(&folder_info, 0, sizeof(folder_info));
    folder_info.storage_id             = storage;
    folder_info.object_format          = MTP_OFC_Association;
    folder_info.object_compressed_size = 0;
    folder_info.parent_object          = parent;
    folder_info.association_type       = 0x0001; /* GenericFolder */
    folder_info.filename               = (char *)name;

    ret = mtp_send_object_info(s, storage, parent, &folder_info, new_handle);
    if (ret < 0) {
        fprintf(stderr, "[mtp] create_folder '%s': send_object_info failed\n", name);
        return -1;
    }

    /*
     * For folders (Association type), no SendObject phase is needed.
     * The folder is created when SendObjectInfo succeeds.
     */

    return 0;
}

/* ===================================================================
 * Representative Samples (album art, video thumbnails)
 * =================================================================== */

int mtp_get_representative_sample(ptp_session_t *s, uint32_t handle,
                                  uint8_t **data, uint32_t *len)
{
    uint8_t *raw = NULL;
    uint32_t raw_len = 0;

    if (data) *data = NULL;
    if (len) *len = 0;

    /*
     * The Zune stores album art / video poster bytes in the
     * RepresentativeSampleData (0xDC86) MTP property — written via
     * SetObjectPropValue in mtp_send_representative_sample. The PTP
     * standard GetThumb (0x100A) opcode reads from a different storage
     * area that we never populate, so it returns 0x2002 for everything.
     * Read back the same property we wrote.
     */
    if (mtp_get_object_prop_value(s, handle,
                                  MTP_OPC_RepresentativeSampleData,
                                  &raw, &raw_len) != 0) {
        return -1;
    }

    if (!raw || raw_len < 4) {
        free(raw);
        fprintf(stderr, "[mtp] get_representative_sample 0x%08X: "
                "empty or short response (%u bytes)\n", handle, raw_len);
        return -1;
    }

    /* AUINT8 wire format: 4-byte LE count + bytes (matches send path). */
    uint32_t count = (uint32_t)raw[0]
                   | ((uint32_t)raw[1] << 8)
                   | ((uint32_t)raw[2] << 16)
                   | ((uint32_t)raw[3] << 24);

    if (count == 0 || count > raw_len - 4) {
        fprintf(stderr, "[mtp] get_representative_sample 0x%08X: "
                "bad AUINT8 count %u in %u-byte payload\n",
                handle, count, raw_len);
        free(raw);
        return -1;
    }

    memmove(raw, raw + 4, count);
    *data = raw;
    *len = count;
    return 0;
}

int mtp_send_representative_sample(ptp_session_t *s, uint32_t handle,
                                   uint16_t format, uint32_t width,
                                   uint32_t height, const uint8_t *data,
                                   uint32_t len)
{
    /*
     * Send representative sample data (album art / thumbnail).
     *
     * The vendored libmtp ONLY sets RepresentativeSampleData (0xDC86)
     * as PTP_DTC_AUINT8 (array of bytes). It does NOT set width/height/
     * size/format individually — the Zune rejects those with 0x200F.
     *
     * The AUINT8 wire format is: uint32_t count + count bytes of data.
     */
    (void)format; (void)width; (void)height; /* unused — Zune doesn't support these props */

    /* Build AUINT8 payload: 4-byte count prefix + raw image bytes */
    uint32_t payload_len = 4 + len;
    uint8_t *payload = (uint8_t *)malloc(payload_len);
    if (!payload) return -1;

    /* Pack count as LE uint32 */
    payload[0] = (uint8_t)(len & 0xFF);
    payload[1] = (uint8_t)((len >> 8) & 0xFF);
    payload[2] = (uint8_t)((len >> 16) & 0xFF);
    payload[3] = (uint8_t)((len >> 24) & 0xFF);
    memcpy(payload + 4, data, len);

    uint16_t resp = 0;
    uint32_t params[2];
    params[0] = handle;
    params[1] = (uint32_t)MTP_OPC_RepresentativeSampleData;

    int ret = ptp_transaction(s, MTP_OC_SetObjectPropValue,
                              params, 2,
                              PTP_DP_SENDDATA,
                              payload, (uint64_t)payload_len,
                              NULL, NULL,
                              &resp, NULL, NULL);
    free(payload);

    if (ret < 0) {
        fprintf(stderr, "[mtp] send_representative_sample 0x%08X: "
                "failed (response 0x%04X)\n", handle, resp);
        return -1;
    }

    return 0;
}

/* ===================================================================
 * Property List Builder / Packer
 *
 * Used by mtp_send_object_prop_list and mtp_set_object_prop_list.
 * Packs an array of mtp_prop_entry_t into the MTP ObjectPropList
 * wire format:
 *
 *   uint32_t  num_elements
 *   For each element:
 *     uint32_t  ObjectHandle
 *     uint16_t  PropertyCode
 *     uint16_t  DataType
 *     [variable] Value (size depends on DataType)
 *
 * String values (PTP_DTC_STR) are packed in MTP string format:
 *   uint8 count + UCS-2LE chars + null terminator.
 * =================================================================== */

/*
 * mtp_pack_prop_list — Pack property entries into wire format.
 *
 * Allocates and returns the packed buffer. Caller frees.
 * Sets *out_len to the total packed size.
 *
 * Returns the packed buffer, or NULL on error.
 */
static uint8_t *mtp_pack_prop_list(mtp_prop_entry_t *props, int num_props,
                                   uint32_t *out_len)
{
    /*
     * Two-pass approach:
     * Pass 1: calculate total size (and pre-encode strings)
     * Pass 2: pack into allocated buffer
     */
    uint8_t **str_wires = NULL;
    uint32_t *str_lens = NULL;
    uint32_t total_size;
    uint8_t *buf;
    uint32_t off;
    int i;

    if (num_props <= 0) {
        /* Empty list: just the count */
        buf = (uint8_t *)malloc(4);
        if (!buf) return NULL;
        le32_pack(buf, 0);
        if (out_len) *out_len = 4;
        return buf;
    }

    /* Allocate arrays for pre-encoded string values */
    str_wires = (uint8_t **)calloc((size_t)num_props, sizeof(uint8_t *));
    str_lens  = (uint32_t *)calloc((size_t)num_props, sizeof(uint32_t));
    if (!str_wires || !str_lens) {
        free(str_wires);
        free(str_lens);
        return NULL;
    }

    /* Pass 1: calculate total size */
    total_size = 4; /* uint32 num_elements */

    for (i = 0; i < num_props; i++) {
        total_size += 4; /* ObjectHandle */
        total_size += 2; /* PropertyCode */
        total_size += 2; /* DataType */

        switch (props[i].data_type) {
        case PTP_DTC_INT8:
        case PTP_DTC_UINT8:
            total_size += 1;
            break;
        case PTP_DTC_INT16:
        case PTP_DTC_UINT16:
            total_size += 2;
            break;
        case PTP_DTC_INT32:
        case PTP_DTC_UINT32:
            total_size += 4;
            break;
        case PTP_DTC_INT64:
        case PTP_DTC_UINT64:
            total_size += 8;
            break;
        case PTP_DTC_STR: {
            uint32_t wire_len = 0;
            str_wires[i] = mtp_string_to_ucs2(props[i].value.str, &wire_len);
            if (!str_wires[i]) {
                fprintf(stderr, "[mtp] pack_prop_list: string conversion "
                        "failed for prop 0x%04X\n", props[i].prop_code);
                goto fail;
            }
            str_lens[i] = wire_len;
            total_size += wire_len;
            break;
        }
        default:
            fprintf(stderr, "[mtp] pack_prop_list: unsupported data type "
                    "0x%04X for prop 0x%04X\n",
                    props[i].data_type, props[i].prop_code);
            goto fail;
        }
    }

    /* Pass 2: allocate and pack */
    buf = (uint8_t *)malloc(total_size);
    if (!buf) goto fail;

    off = 0;

    /* Number of elements */
    le32_pack(buf + off, (uint32_t)num_props);
    off += 4;

    for (i = 0; i < num_props; i++) {
        /* ObjectHandle */
        le32_pack(buf + off, props[i].handle);
        off += 4;

        /* PropertyCode */
        le16_pack(buf + off, props[i].prop_code);
        off += 2;

        /* DataType */
        le16_pack(buf + off, props[i].data_type);
        off += 2;

        /* Value */
        switch (props[i].data_type) {
        case PTP_DTC_INT8:
        case PTP_DTC_UINT8:
            buf[off] = props[i].value.u8;
            off += 1;
            break;
        case PTP_DTC_INT16:
        case PTP_DTC_UINT16:
            le16_pack(buf + off, props[i].value.u16);
            off += 2;
            break;
        case PTP_DTC_INT32:
        case PTP_DTC_UINT32:
            le32_pack(buf + off, props[i].value.u32);
            off += 4;
            break;
        case PTP_DTC_INT64:
        case PTP_DTC_UINT64:
            le64_pack(buf + off, props[i].value.u64);
            off += 8;
            break;
        case PTP_DTC_STR:
            memcpy(buf + off, str_wires[i], str_lens[i]);
            off += str_lens[i];
            break;
        default:
            /* Already validated in pass 1 — unreachable */
            break;
        }
    }

    /* Clean up pre-encoded strings */
    for (i = 0; i < num_props; i++)
        free(str_wires[i]);
    free(str_wires);
    free(str_lens);

    if (out_len) *out_len = total_size;
    return buf;

fail:
    for (i = 0; i < num_props; i++)
        free(str_wires[i]);
    free(str_wires);
    free(str_lens);
    return NULL;
}

/* ===================================================================
 * Property List Operations
 * =================================================================== */

int mtp_send_object_prop_list(ptp_session_t *s,
                              uint32_t storage_id,
                              uint32_t parent_handle,
                              uint16_t object_format,
                              uint64_t object_size,
                              mtp_prop_entry_t *props,
                              int num_props,
                              uint32_t *new_handle)
{
    uint8_t *packed = NULL;
    uint32_t packed_len = 0;
    uint16_t resp = 0;
    uint32_t cmd_params[PTP_MAX_PARAMS];
    uint32_t resp_params[PTP_MAX_PARAMS];
    int resp_nparams = 0;
    int ret;

    if (new_handle) *new_handle = 0;

    packed = mtp_pack_prop_list(props, num_props, &packed_len);
    if (!packed) {
        fprintf(stderr, "[mtp] send_object_prop_list: failed to pack property list\n");
        return -1;
    }

    /*
     * SendObjectPropList (0x9808):
     *   Param1: StorageID
     *   Param2: ParentObjectHandle
     *   Param3: ObjectFormatCode (as uint32)
     *   Param4: ObjectSize high 32 bits
     *   Param5: ObjectSize low 32 bits
     *   Data:   packed ObjectPropList
     *   Response params: [StorageID, ParentObjectHandle, NewObjectHandle]
     */
    cmd_params[0] = storage_id;
    cmd_params[1] = parent_handle;
    cmd_params[2] = (uint32_t)object_format;
    cmd_params[3] = (uint32_t)(object_size >> 32);
    cmd_params[4] = (uint32_t)(object_size & 0xFFFFFFFFU);

    ret = ptp_transaction(s, MTP_OC_SendObjectPropList,
                          cmd_params, 5,
                          PTP_DP_SENDDATA,
                          packed, (uint64_t)packed_len,
                          NULL, NULL,
                          &resp, resp_params, &resp_nparams);
    free(packed);

    if (ret < 0) {
        fprintf(stderr, "[mtp] send_object_prop_list: failed (response 0x%04X)\n", resp);
        return -1;
    }

    /* Extract new handle from response param3 — same ghost-object guard
     * as send_object_info: success without a handle is a failure. */
    if (new_handle) {
        *new_handle = (resp_nparams >= 3) ? resp_params[2] : 0;
        if (*new_handle == 0) {
            fprintf(stderr, "[mtp] send_object_prop_list: OK but no handle in "
                    "response (%d params)\n", resp_nparams);
            return -1;
        }
    }

    return 0;
}

int mtp_set_object_prop_list(ptp_session_t *s,
                             mtp_prop_entry_t *props,
                             int num_props)
{
    uint8_t *packed = NULL;
    uint32_t packed_len = 0;
    uint16_t resp = 0;
    int ret;

    packed = mtp_pack_prop_list(props, num_props, &packed_len);
    if (!packed) {
        fprintf(stderr, "[mtp] set_object_prop_list: failed to pack property list\n");
        return -1;
    }

    /*
     * SetObjPropList (0x9806):
     *   No command params
     *   Data: packed ObjectPropList
     *   Response: OK or error
     */
    ret = ptp_transaction(s, MTP_OC_SetObjPropList,
                          NULL, 0,
                          PTP_DP_SENDDATA,
                          packed, (uint64_t)packed_len,
                          NULL, NULL,
                          &resp, NULL, NULL);
    free(packed);

    if (ret < 0) {
        fprintf(stderr, "[mtp] set_object_prop_list: failed (response 0x%04X)\n", resp);
        return -1;
    }

    return 0;
}

int mtp_get_props_supported(ptp_session_t *s, uint16_t format_code,
                            uint16_t **props, int *count)
{
    uint8_t *data = NULL;
    uint32_t len = 0;
    uint16_t resp = 0;
    uint32_t param = (uint32_t)format_code;
    int ret;

    if (props) *props = NULL;
    if (count) *count = 0;

    /*
     * GetObjectPropsSupported (0x9801):
     *   Param1: ObjectFormatCode
     *   Data phase (GETDATA): uint32 count + uint16 array of property codes
     */
    ret = ptp_transaction(s, MTP_OC_GetObjectPropsSupported,
                          &param, 1,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          &data, &len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_props_supported format 0x%04X: "
                "failed (response 0x%04X)\n", format_code, resp);
        return -1;
    }

    if (!data || len < 4) {
        fprintf(stderr, "[mtp] get_props_supported: no data or too short (%u bytes)\n",
                len);
        free(data);
        return -1;
    }

    uint32_t n = le32_unpack(data);
    if (len < 4 + n * 2) {
        fprintf(stderr, "[mtp] get_props_supported: truncated "
                "(count=%u, len=%u)\n", n, len);
        free(data);
        return -1;
    }

    uint16_t *result = (uint16_t *)malloc(n * sizeof(uint16_t));
    if (!result) {
        free(data);
        return -1;
    }

    for (uint32_t i = 0; i < n; i++)
        result[i] = le16_unpack(data + 4 + i * 2);

    free(data);

    if (props) *props = result;
    else free(result);

    if (count) *count = (int)n;

    return 0;
}

int mtp_get_prop_desc(ptp_session_t *s, uint16_t prop_code,
                      uint16_t format_code, uint8_t **data,
                      uint32_t *len)
{
    uint16_t resp = 0;
    uint32_t params[2];
    int ret;

    if (data) *data = NULL;
    if (len) *len = 0;

    /*
     * GetObjectPropDesc (0x9802):
     *   Param1: ObjectPropCode
     *   Param2: ObjectFormatCode
     *   Data phase (GETDATA): raw ObjectPropDesc dataset
     */
    params[0] = (uint32_t)prop_code;
    params[1] = (uint32_t)format_code;

    ret = ptp_transaction(s, MTP_OC_GetObjectPropDesc,
                          params, 2,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          data, len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] get_prop_desc prop 0x%04X format 0x%04X: "
                "failed (response 0x%04X)\n", prop_code, format_code, resp);
        return -1;
    }

    return 0;
}

/* ===================================================================
 * Vendor Operations
 * =================================================================== */

int mtp_vendor_operation(ptp_session_t *s, uint16_t opcode,
                         uint32_t *params, int nparams,
                         uint8_t **recv_data, uint32_t *recv_len)
{
    uint16_t resp = 0;
    uint16_t flags;
    int ret;

    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;

    /* Determine data phase direction based on whether caller wants data */
    flags = (recv_data) ? PTP_DP_GETDATA : PTP_DP_NODATA;

    ret = ptp_transaction(s, opcode,
                          params, nparams,
                          flags,
                          NULL, 0,
                          recv_data, recv_len,
                          &resp, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mtp] vendor_operation 0x%04X: "
                "failed (response 0x%04X)\n", opcode, resp);
        return -1;
    }

    return 0;
}
