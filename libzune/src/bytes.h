/*
 * bytes.h — little-endian packing for PTP/MTP wire data, ZMDB records and
 * device property values, shared by every layer of libzune.
 */

#ifndef ZUNE_BYTES_H
#define ZUNE_BYTES_H

#include <stdint.h>

static inline uint16_t zune_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static inline uint32_t zune_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t zune_le64(const uint8_t *p)
{
    return (uint64_t)zune_le32(p) | (uint64_t)zune_le32(p + 4) << 32;
}

static inline void zune_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void zune_put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static inline void zune_put_le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

#endif /* ZUNE_BYTES_H */
