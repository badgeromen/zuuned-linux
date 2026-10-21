/*
 * wiretap.h — internal logging for every libzune layer (wiretap.c).
 *
 * Included by zune_internal.h and directly by the protocol and USB layers,
 * which do not see the device struct. Not part of the public API.
 */

#ifndef ZUNE_WIRETAP_H
#define ZUNE_WIRETAP_H

#include <stdint.h>
#include "zune.h"

/* ZLOG(level, tag, fmt, ...) filters by the process chatter level and passes
 * the formatted text to the installed wiretap, or prints "[tag] text\n" to
 * stderr. fmt has no tag and no trailing newline. */
void zune_log(ZuneChatter level, const char *tag, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
#define ZLOG(level, tag, ...) zune_log((level), (tag), __VA_ARGS__)

/* One message: label followed by " 0x%04X" for each code. */
void zune_log_codes(ZuneChatter level, const char *tag, const char *label,
                    const uint16_t *codes, int count);

#endif /* ZUNE_WIRETAP_H */
