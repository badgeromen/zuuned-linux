/* T1.6: MTPZ keys load once per process, zune_go_dark (mtpz_free_keys) wipes
 * them before freeing, and libgcrypt setup belongs to the host when the host
 * did it first. Uses a mock ~/.mtpz-data with fake values in a temp HOME; no
 * real key material and no USB.
 * Modes: keys | gcrypt-fresh | gcrypt-host (each needs a fresh process). */
#include "../src/mtpz.h"
#include "zune.h"
#include <assert.h>
#include <gcrypt.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

/* ---- Wrapped libc calls made by mtpz.c ---- */

FILE *__real_fopen(const char *path, const char *mode);
void __real_free(void *p);
static int key_file_opens, unwiped_frees, wipe_checks;

FILE *__wrap_fopen(const char *path, const char *mode) {
    size_t n = strlen(path);
    if (n >= 10 && !strcmp(path + n - 10, ".mtpz-data")) ++key_file_opens;
    return __real_fopen(path, mode);
}

/* Fake key text, line by line: exponent, encryption key, modulus, private
 * key, certificates. Freed memory must not still hold any of it. */
static const struct { const char *text; size_t len; } markers[] = {
    { "a5a5a5", 6 }, { "b6b6b6b6", 8 }, { "c7c7c7c7", 8 }, { "d8d8d8d8", 8 } };
static const unsigned char binary_marker[] = { 0xb6, 0xb6, 0xb6, 0xb6 };
static const unsigned char cert_marker[] = { 0xd8, 0xd8, 0xd8, 0xd8 };

void __wrap_free(void *p) {
    if (p) {
        size_t n = malloc_usable_size(p);
        const unsigned char *b = p;
        ++wipe_checks;
        for (size_t i = 0; i + 4 <= n; i++) {
            int text = 0;
            for (size_t m = 0; m < sizeof(markers) / sizeof(markers[0]); m++)
                if (i + markers[m].len <= n && !memcmp(b + i, markers[m].text, markers[m].len)) text = 1;
            if (text || !memcmp(b + i, binary_marker, 4) || !memcmp(b + i, cert_marker, 4)) {
                ++unwiped_frees;
                break;
            }
        }
    }
    __real_free(p);
}

/* ---- Wrapped libgcrypt setup calls ---- */

gcry_error_t __real_gcry_control(enum gcry_ctl_cmds cmd, ...);
const char *__real_gcry_check_version(const char *req);
static int finished_queries, finish_calls, version_checks;
static const char *version_asked;

gcry_error_t __wrap_gcry_control(enum gcry_ctl_cmds cmd, ...) {
    if (cmd == GCRYCTL_INITIALIZATION_FINISHED_P) ++finished_queries;
    if (cmd == GCRYCTL_INITIALIZATION_FINISHED) ++finish_calls;
    return __real_gcry_control(cmd, 0);    /* both commands take no argument */
}

const char *__wrap_gcry_check_version(const char *req) {
    ++version_checks;
    version_asked = req;
    return __real_gcry_check_version(req);
}

/* ---- Mock key file ---- */

static char home[256];

static void repeat(FILE *f, const char *unit, int times) {
    for (int i = 0; i < times; i++) fputs(unit, f);
    fputc('\n', f);
}

static void write_keys(void) {
    char path[300];
    snprintf(path, sizeof(path), "%s/.mtpz-data", home);
    FILE *f = __real_fopen(path, "w");
    assert(f);
    fputs("a5a5a5\n", f);                   /* exponent, 6 hex characters */
    repeat(f, "b6", 16);                    /* encryption key, 16 bytes */
    repeat(f, "c7", 128);                   /* modulus, 256 hex characters */
    repeat(f, "d8", 128);                   /* private key */
    repeat(f, "d8", 629);                   /* certificates, 1258 hex characters */
    fclose(f);
}

/* [mtpz] messages go through the log callback like every other layer. */
static int mtpz_messages;
static ZuneChatter mtpz_level;
static char mtpz_text[256];
static void tap(ZuneChatter level, const char *tag, const char *message, void *ctx) {
    (void)ctx;
    if (strcmp(tag, "mtpz") != 0) return;
    ++mtpz_messages;
    mtpz_level = level;
    snprintf(mtpz_text, sizeof(mtpz_text), "%s", message);
}

static void remove_keys(void) {
    char path[300];
    snprintf(path, sizeof(path), "%s/.mtpz-data", home);
    unlink(path);
}

int main(int argc, char **argv) {
    const char *mode = argc > 2 ? argv[2] : "";
    if (argc < 3) { fprintf(stderr, "usage: mtpz-keys HOME keys|gcrypt-fresh|gcrypt-host\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);       /* results survive a leak-report exit */
    snprintf(home, sizeof(home), "%s", argv[1]);
    setenv("HOME", home, 1);

    if (!strcmp(mode, "keys")) {
        write_keys();
        check(mtpz_load_keys() == 0 && mtpz_keys_available(), "mock ~/.mtpz-data loads");
        int opens = key_file_opens;
        check(mtpz_load_keys() == 0 && key_file_opens == opens && opens == 1,
              "a second load reuses the keys without reading the file again");
        unwiped_frees = wipe_checks = 0;
        mtpz_free_keys();                   /* what zune_go_dark does */
        check(!mtpz_keys_available(), "zune_go_dark releases every key buffer");
        check(wipe_checks >= 5 && unwiped_frees == 0, "each key buffer is wiped before it is freed");
        mtpz_free_keys();
        check(!mtpz_keys_available(), "a second zune_go_dark with nothing loaded is safe");
        check(mtpz_load_keys() == 0 && key_file_opens == 2 && mtpz_keys_available(),
              "after zune_go_dark the next load reads the keys again");
        mtpz_free_keys();
        zune_wiretap(tap, NULL);
        check(mtpz_load_keys() == 0 && mtpz_messages == 1 && mtpz_level == ZUNE_CHATTER_TALK &&
              strstr(mtpz_text, "loaded keys from ~/.mtpz-data") && !strchr(mtpz_text, '\n'),
              "MTPZ messages reach zune_wiretap with their level and the [mtpz] tag");
        mtpz_free_keys();
        zune_set_chatter(ZUNE_CHATTER_SILENT);
        check(mtpz_load_keys() == 0 && mtpz_messages == 1, "ZUNE_CHATTER_SILENT silences MTPZ messages too");
        zune_set_chatter(ZUNE_CHATTER_TALK);
        zune_wiretap(NULL, NULL);
        unwiped_frees = 0;
        mtpz_free_keys();
        remove_keys();
        int before = key_file_opens;
        int first = mtpz_load_keys();       /* embedded defaults or unavailable, per build */
        int again = mtpz_load_keys();
        check(first == again && key_file_opens == before + 1,
              "without a key file the first result is kept and the file is not retried");
        mtpz_free_keys();
        check(unwiped_frees == 0, "no key text survives in freed memory (temporary hex copies included)");
    } else if (!strcmp(mode, "gcrypt-fresh")) {
        int a = mtpz_init_gcrypt(), b = mtpz_init_gcrypt();
        check(a == 0 && b == 0 && finish_calls == 1 && version_checks == 1 && finished_queries == 1,
              "libgcrypt is set up once when the host has not set it up");
        check(version_asked && !strcmp(version_asked, GCRYPT_VERSION),
              "the runtime libgcrypt must be at least the version libzune was built with");
    } else if (!strcmp(mode, "gcrypt-host")) {
        __real_gcry_check_version(NULL);
        __real_gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
        check(mtpz_init_gcrypt() == 0 && finish_calls == 0 && version_checks == 0,
              "a host's own libgcrypt setup is left alone");
    } else {
        return 2;
    }
    printf("MTPZ keys (%s): %d checks, %d failures; no USB\n", mode, checks, failures);
    return failures ? 1 : 0;
}
