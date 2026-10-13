/* Bounded UTF-8 <-> MTP AUINT16 text. Unlike an MTP STR this has a uint32
 * element count and no 254-character truncation. Text sends include a counted
 * terminal NUL; readback accepts one terminal NUL or unterminated text. */
#ifndef ZUNE_MTP_TEXT_H
#define ZUNE_MTP_TEXT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define MTP_TEXT_MAX_BYTES 65536u

static inline uint8_t *mtp_text_to_array(const char *text, uint32_t *length)
{
    if (length) *length = 0;
    if (!text) text = "";
    size_t n = strnlen(text, MTP_TEXT_MAX_BYTES + 1u);
    if (n > MTP_TEXT_MAX_BYTES) return NULL;
    uint8_t *wire = calloc(1, 4 + (n + 1) * 2);
    if (!wire) return NULL;
    uint32_t units = 0;
    for (size_t i = 0; i < n;) {
        uint8_t first = (uint8_t)text[i++];
        uint32_t cp; unsigned trailing; uint32_t minimum;
        if (first < 0x80) { cp = first; trailing = 0; minimum = 0; }
        else if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; trailing = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; trailing = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; trailing = 3; minimum = 0x10000; }
        else goto invalid;
        if (trailing > n - i) goto invalid;
        for (unsigned j = 0; j < trailing; ++j) {
            uint8_t next = (uint8_t)text[i++];
            if ((next & 0xc0) != 0x80) goto invalid;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) goto invalid;
        uint16_t values[2]; unsigned count = 1;
        if (cp < 0x10000) values[0] = (uint16_t)cp;
        else { cp -= 0x10000; values[0] = 0xd800 | (cp >> 10); values[1] = 0xdc00 | (cp & 1023); count = 2; }
        for (unsigned j = 0; j < count; ++j) {
            wire[4 + units * 2] = values[j] & 255;
            wire[5 + units * 2] = values[j] >> 8;
            ++units;
        }
    }
    ++units; /* Counted UTF-16 terminator: firmware consumes these as text. */
    for (unsigned i = 0; i < 4; ++i) wire[i] = (uint8_t)(units >> (8*i));
    if (length) *length = 4 + units * 2;
    return wire;
invalid:
    free(wire); return NULL;
}

static inline char *mtp_array_to_text(const uint8_t *wire, uint32_t length)
{
    if (!wire || length < 4) return NULL;
    uint32_t count = (uint32_t)wire[0] | ((uint32_t)wire[1] << 8) |
                     ((uint32_t)wire[2] << 16) | ((uint32_t)wire[3] << 24);
    if (count > MTP_TEXT_MAX_BYTES + 1u || count != (length - 4) / 2 || (length - 4) % 2) return NULL;
    uint32_t end = count;
    if (end && wire[4+(end-1)*2] == 0 && wire[5+(end-1)*2] == 0) --end;
    if (end > MTP_TEXT_MAX_BYTES) return NULL;
    char *text = malloc((size_t)count * 3 + 1);
    if (!text) return NULL;
    size_t out = 0;
    for (uint32_t i = 0; i < end; ++i) {
        uint32_t cp = wire[4+i*2] | ((uint32_t)wire[5+i*2] << 8);
        if (cp >= 0xd800 && cp <= 0xdbff) {
            if (++i == end) goto invalid;
            uint32_t low = wire[4+i*2] | ((uint32_t)wire[5+i*2] << 8);
            if (low < 0xdc00 || low > 0xdfff) goto invalid;
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
        } else if (cp >= 0xdc00 && cp <= 0xdfff) goto invalid;
        /* Embedded terminators cannot silently change an identity URL. */
        if (!cp) goto invalid;
        if (cp < 0x80) text[out++] = (char)cp;
        else if (cp < 0x800) { text[out++] = 0xc0 | (cp >> 6); text[out++] = 0x80 | (cp & 63); }
        else if (cp < 0x10000) { text[out++] = 0xe0 | (cp >> 12); text[out++] = 0x80 | ((cp >> 6) & 63); text[out++] = 0x80 | (cp & 63); }
        else { text[out++] = 0xf0 | (cp >> 18); text[out++] = 0x80 | ((cp >> 12) & 63); text[out++] = 0x80 | ((cp >> 6) & 63); text[out++] = 0x80 | (cp & 63); }
    }
    text[out] = 0;
    return text;
invalid:
    free(text); return NULL;
}
#endif
