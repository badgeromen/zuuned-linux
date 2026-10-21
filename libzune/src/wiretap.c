/*
 * libzune — wiretap.c
 *
 * Library log routing. Every "[tag] message" line goes through zune_log:
 * filtered by the process chatter level, then handed to the installed
 * wiretap, or printed by the default sink in the historical stderr format.
 * Process-wide by design (not per device). mtpz.c still writes stderr
 * directly; it is outside this conversion.
 */

#include "zune_internal.h"
#include <pthread.h>
#include <strings.h>

static pthread_mutex_t tap_lock = PTHREAD_MUTEX_INITIALIZER;
static ZuneWiretapFn tap_fn;      /* NULL: default stderr sink */
static void *tap_ctx;
static int chatter = ZUNE_CHATTER_TALK;
static pthread_once_t chatter_once = PTHREAD_ONCE_INIT;

/* ZUNE_CHATTER=whisper|talk|shout|scream|silent sets the initial level. */
static void chatter_from_environment(void)
{
    static const char *const names[] = { "whisper", "talk", "shout", "scream", "silent" };
    const char *value = getenv("ZUNE_CHATTER");
    if (!value) return;
    for (int i = 0; i <= ZUNE_CHATTER_SILENT; i++) {
        if (strcasecmp(value, names[i]) == 0) {
            __atomic_store_n(&chatter, i, __ATOMIC_RELAXED);
            return;
        }
    }
}

void zune_set_chatter(ZuneChatter level)
{
    pthread_once(&chatter_once, chatter_from_environment);
    if ((int)level < ZUNE_CHATTER_WHISPER || (int)level > ZUNE_CHATTER_SILENT) return;
    __atomic_store_n(&chatter, (int)level, __ATOMIC_RELAXED);
}

void zune_wiretap(ZuneWiretapFn fn, void *ctx)
{
    pthread_mutex_lock(&tap_lock);
    tap_fn = fn;
    tap_ctx = fn ? ctx : NULL;
    pthread_mutex_unlock(&tap_lock);
}

void zune_log_codes(ZuneChatter level, const char *tag, const char *label,
                    const uint16_t *codes, int count)
{
    size_t size = strlen(label) + 1 + (count > 0 ? (size_t)count * 7 : 0);
    char *text = malloc(size);
    if (!text) {
        ZLOG(level, tag, "%s (%d codes)", label, count);
        return;
    }
    size_t used = (size_t)snprintf(text, size, "%s", label);
    for (int i = 0; i < count && used < size; i++)
        used += (size_t)snprintf(text + used, size - used, " 0x%04X", codes[i]);
    ZLOG(level, tag, "%s", text);
    free(text);
}

void zune_log(ZuneChatter level, const char *tag, const char *fmt, ...)
{
    pthread_once(&chatter_once, chatter_from_environment);
    if ((int)level >= ZUNE_CHATTER_SILENT
        || (int)level < __atomic_load_n(&chatter, __ATOMIC_RELAXED))
        return;

    char stack[1024];
    char *message = stack;
    va_list ap;
    va_start(ap, fmt);
    int length = vsnprintf(stack, sizeof(stack), fmt, ap);
    va_end(ap);
    if (length < 0) return;
    if ((size_t)length >= sizeof(stack)) {
        /* Long paths: format again in full; on allocation failure the
         * truncated copy is still better than nothing. */
        char *heap = malloc((size_t)length + 1);
        if (heap) {
            va_start(ap, fmt);
            vsnprintf(heap, (size_t)length + 1, fmt, ap);
            va_end(ap);
            message = heap;
        }
    }

    /* Copy the tap under the lock and call it outside, so a callback may
     * itself log or replace the tap. */
    pthread_mutex_lock(&tap_lock);
    ZuneWiretapFn fn = tap_fn;
    void *ctx = tap_ctx;
    pthread_mutex_unlock(&tap_lock);

    if (fn) fn(level, tag ? tag : "libzune", message, ctx);
    else fprintf(stderr, "[%s] %s\n", tag ? tag : "libzune", message);

    if (message != stack) free(message);
}
