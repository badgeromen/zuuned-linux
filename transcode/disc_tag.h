/* Disc tag grammar mirrors libzune's positive numeric metadata probe:
 * number or number/total, bounded by INT_MAX; preserve a valid total. */
#ifndef ZUUNED_DISC_TAG_H
#define ZUUNED_DISC_TAG_H
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t disc_space(const unsigned char *p, size_t n)
{
    if (!n) return 0;
    if (p[0] == ' ' || (p[0] >= '\t' && p[0] <= '\r')) return 1;
    if (n >= 2 && p[0] == 0xc2 && (p[1] == 0x85 || p[1] == 0xa0)) return 2;
    if (n >= 3) {
        if (p[0] == 0xe1 && p[1] == 0x9a && p[2] == 0x80) return 3;
        if (p[0] == 0xe2 && p[1] == 0x80
            && ((p[2] >= 0x80 && p[2] <= 0x8a) || p[2] == 0xa8 || p[2] == 0xa9 || p[2] == 0xaf)) return 3;
        if (p[0] == 0xe2 && p[1] == 0x81 && p[2] == 0x9f) return 3;
        if (p[0] == 0xe3 && p[1] == 0x80 && p[2] == 0x80) return 3;
    }
    return 0;
}
static int normalize_disc_tag(const char *text, char output[48])
{
    output[0] = 0;
    if (!text) return 0;
    size_t n = strlen(text), space;
    while ((space = disc_space((const unsigned char *)text, n))) { text += space; n -= space; }
    if (!isdigit((unsigned char)*text)) return 0;
    errno = 0;
    char *end;
    unsigned long disc = strtoul(text, &end, 10), total = 0;
    if (errno || !disc || disc > INT_MAX) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end == '/') {
        const char *p = end + 1;
        while (isspace((unsigned char)*p)) ++p;
        if (!isdigit((unsigned char)*p)) return 0;
        errno = 0;
        total = strtoul(p, &end, 10);
        if (errno || total < disc || total > INT_MAX) return 0;
    }
    n = strlen(end);
    while ((space = disc_space((const unsigned char *)end, n))) { end += space; n -= space; }
    if (*end) return 0;
    if (total) snprintf(output, 48, "%lu/%lu", disc, total);
    else snprintf(output, 48, "%lu", disc);
    return 1;
}
#endif
