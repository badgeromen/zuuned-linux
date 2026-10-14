/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBZUNE_EXAMPLE_COMMON_H
#define LIBZUNE_EXAMPLE_COMMON_H
#include "zune.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline const char *text(const char *s) { return s ? s : ""; }
static inline int failure(ZuneDeviceHandle dev, const char *operation)
{
    fprintf(stderr, "%s: %s (PTP 0x%04x)\n", operation,
            text(zune_get_error()), dev ? zune_autopsy(dev) : 0);
    return 1;
}
static inline int positive_u32(const char *s, uint32_t *value)
{
    char *end;
    if (!s || !*s || strspn(s, "0123456789") != strlen(s)) return -1;
    errno = 0;
    unsigned long long n = strtoull(s, &end, 10);
    if (errno || *end || !n || n > UINT32_MAX) return -1;
    *value = (uint32_t)n;
    return 0;
}
/* The caller must actually encode/inspect the MP3 beforehand. File extension
 * and nonempty bytes cannot establish codec compatibility. */
static inline int prepared_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
           st.st_size > 0 && (uint64_t)st.st_size < UINT32_MAX;
}
static inline int load_cover(const char *path, uint8_t **data, size_t *size)
{
    *data = NULL; *size = 0;
    if (!path) return 0;
    struct stat st;
    /* Deliberate example limit, not an advertised firmware maximum. */
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 3 ||
        st.st_size > 1024 * 1024) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t *bytes = malloc((size_t)st.st_size);
    size_t got = bytes ? fread(bytes, 1, (size_t)st.st_size, f) : 0;
    fclose(f);
    if (!bytes || got != (size_t)st.st_size || bytes[0] != 0xff ||
        bytes[1] != 0xd8 || bytes[2] != 0xff) { free(bytes); return -1; }
    *data = bytes; *size = got;
    return 0;
}
#endif
