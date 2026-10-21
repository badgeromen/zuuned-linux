/* T1.7: every allocation in the MTPZ handshake can fail without a crash or a
 * leak, and nothing built after the failure is sent to the device.
 *
 * mtpz.c is compiled into this file with malloc/calloc/strdup redirected so
 * that allocation N fails. The device is a fake PTP layer: it accepts the
 * certificate message and answers with a response the handshake accepts,
 * built from a throwaway RSA key generated in memory for this test. No MTPZ
 * key material is used or stored. */
#include <gcrypt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../src/ptp.h"
#include "../src/mtpz.h"

static int fail_at, calls;
static void *test_malloc(size_t n) { return ++calls == fail_at ? NULL : malloc(n); }
static void *test_calloc(size_t a, size_t b) { return ++calls == fail_at ? NULL : calloc(a, b); }
static char *test_strdup(const char *s) { return ++calls == fail_at ? NULL : strdup(s); }
#define malloc test_malloc
#define calloc test_calloc
#define strdup test_strdup
#include "../src/mtpz.c"
#undef malloc
#undef calloc
#undef strdup

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

/* ---- Throwaway RSA key and the device's response ---- */

static gcry_mpi_t key_n, key_e;
static unsigned char nonce[16], response[4 + 128 + 4 + 832];
static const unsigned char session_key[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

static char *mpi_hex(gcry_sexp_t key, const char *name) {
    gcry_sexp_t part = gcry_sexp_find_token(key, name, 0);
    gcry_mpi_t value = gcry_sexp_nth_mpi(part, 1, GCRYMPI_FMT_USG);
    unsigned char *hex = NULL;
    gcry_mpi_aprint(GCRYMPI_FMT_HEX, &hex, NULL, value);
    char *copy = strdup((char *)hex);
    gcry_free(hex);
    if (!strcmp(name, "n")) key_n = value;
    else if (!strcmp(name, "e")) key_e = value;
    else gcry_mpi_release(value);
    gcry_sexp_release(part);
    return copy;
}

/* Install a fresh test key pair as the handshake's keys. */
static void install_keys(void) {
    gcry_sexp_t spec = NULL, pair = NULL;
    if (gcry_sexp_build(&spec, NULL, "(genkey (rsa (nbits 4:1024) (rsa-use-e 5:65537)))") ||
        gcry_pk_genkey(&pair, spec)) {
        fprintf(stderr, "cannot generate the throwaway test key\n");
        exit(2);
    }
    gcry_sexp_t secret = gcry_sexp_find_token(pair, "private-key", 0);
    MTPZ_MODULUS = (unsigned char *)mpi_hex(secret, "n");
    MTPZ_PUBLIC_EXPONENT = (unsigned char *)mpi_hex(secret, "e");
    MTPZ_PRIVATE_KEY = (unsigned char *)mpi_hex(secret, "d");
    MTPZ_MODULUS_SIZE = strlen((char *)MTPZ_MODULUS) + 1;
    MTPZ_PUBLIC_EXPONENT_SIZE = strlen((char *)MTPZ_PUBLIC_EXPONENT) + 1;
    MTPZ_PRIVATE_KEY_SIZE = strlen((char *)MTPZ_PRIVATE_KEY) + 1;
    MTPZ_ENCRYPTION_KEY = calloc(1, 16);
    MTPZ_ENCRYPTION_KEY_SIZE = 16;
    MTPZ_CERTIFICATES = calloc(1, 629);
    MTPZ_CERTIFICATES_SIZE = 629;
    mtpz_keys_state = 1;
    gcry_sexp_release(secret);
    gcry_sexp_release(pair);
    gcry_sexp_release(spec);
}

/* A response mtpz_validate_handshake_response accepts: the 128-byte RSA
 * block carries the masked session key; the 832-byte block, encrypted with
 * that key, echoes the nonce and carries a 20-byte MAC hash. */
static void build_response(void) {
    int saved_fail = fail_at, saved_calls = calls;
    fail_at = 0;                            /* the fake device never fails */

    unsigned char plain[832] = { 0 };
    size_t o = 1 + 4;                       /* marker, zero-length certificates */
    plain[o++] = 0; plain[o++] = 16;        /* nonce length, big-endian */
    memcpy(plain + o, nonce, 16); o += 16;
    o += 2;                                 /* no device nonce */
    o += 1 + 2;                             /* marker, no signature */
    o += 1;
    plain[o++] = 0; plain[o++] = 20;        /* MAC hash length */
    for (int i = 0; i < 20; i++) plain[o++] = (unsigned char)(0x40 + i);
    mtpz_encryption_cipher_advanced((unsigned char *)session_key, 16, plain, 832, 1);

    unsigned char block[128] = { 0 };       /* final msg_dec after unmasking */
    memset(block + 1, 0x11, 20);
    memset(block + 21, 0x22, 91);
    memcpy(block + 112, session_key, 16);
    char *state = mtpz_hash_init_state();
    char *mask = mtpz_hash_custom6A5DC(state, (char *)block + 1, 20, 107);
    for (int i = 0; i < 107; i++) block[21 + i] ^= (unsigned char)mask[i];
    free(mask);
    mask = mtpz_hash_custom6A5DC(state, (char *)block + 21, 107, 20);
    for (int i = 0; i < 20; i++) block[1 + i] ^= (unsigned char)mask[i];
    free(mask);
    free(state);

    gcry_mpi_t m = NULL, c = gcry_mpi_new(1024);
    gcry_mpi_scan(&m, GCRYMPI_FMT_USG, block, sizeof(block), NULL);
    gcry_mpi_powm(c, m, key_e, key_n);
    size_t written = 0;
    unsigned char sealed[128] = { 0 };
    gcry_mpi_print(GCRYMPI_FMT_USG, sealed, sizeof(sealed), &written, c);
    memset(response, 0, sizeof(response));
    response[0] = 0x02; response[1] = 0x02; response[3] = 0x80;
    memcpy(response + 4 + (128 - written), sealed, written);
    response[4 + 128 + 2] = 0x03; response[4 + 128 + 3] = 0x40;
    memcpy(response + 4 + 128 + 4, plain, 832);
    gcry_mpi_release(m);
    gcry_mpi_release(c);

    fail_at = saved_fail;
    calls = saved_calls;
}

/* ---- Fake device ---- */

static int sends, sends_after_failure;

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len, uint8_t **data, uint32_t *length,
                    uint16_t *rc, uint32_t *rparams, int *rcount) {
    (void)s; (void)params; (void)nparams; (void)flags; (void)rparams; (void)rcount;
    if (rc) *rc = PTP_RC_OK;
    switch (opcode) {
    case MTP_OC_WMDRMPD_EndTrustedAppSession:
        return 0;
    case MTP_OC_WMDRMPD_SendWMDRMPDAppRequest:
    case MTP_OC_WMDRMPD_EnableTrustedFilesOperations:
        ++sends;
        if (fail_at && calls >= fail_at) ++sends_after_failure;
        if (opcode == MTP_OC_WMDRMPD_SendWMDRMPDAppRequest && send_len == 785) {
            memcpy(nonce, send_data + 7 + 629 + 2, 16);
            build_response();
        }
        return 0;
    case MTP_OC_WMDRMPD_GetWMDRMPDAppResponse:
        *data = malloc(sizeof(response));
        memcpy(*data, response, sizeof(response));
        *length = sizeof(response);
        return 0;
    default:
        if (rc) *rc = PTP_RC_OperationNotSupported;
        return -1;
    }
}

int ptp_set_device_prop_value(ptp_session_t *s, uint16_t propcode, uint8_t *data, uint32_t len) {
    (void)s; (void)propcode; (void)data; (void)len;
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (mtpz_init_gcrypt()) return 2;
    install_keys();
    ptp_session_t session;
    memset(&session, 0, sizeof(session));

    calls = 0; fail_at = 0; sends = 0;
    int result = mtpz_handshake(&session);
    int total = calls;
    check(result == 0 && sends == 3, "the fake device completes a full handshake (certificate, confirmation, session)");
    check(total > 25, "the handshake makes its allocations through the counted allocator");

    int crashed_paths = 0, accepted = 0, leaked_sends = 0;
    for (int k = 1; k <= total; k++) {
        calls = 0; fail_at = k; sends = 0; sends_after_failure = 0;
        if (mtpz_handshake(&session) == 0) {
            ++accepted;
            printf("  allocation %d of %d failed but the handshake succeeded\n", k, total);
        }
        if (sends_after_failure) {
            ++leaked_sends;
            printf("  allocation %d of %d failed but a later message was sent\n", k, total);
        }
    }
    fail_at = 0;
    char label[120];
    snprintf(label, sizeof(label), "each of the %d allocations, failing alone, fails the handshake", total);
    check(accepted == 0 && crashed_paths == 0, label);
    check(leaked_sends == 0, "nothing built after a failed allocation is sent to the device");

    calls = 0; sends = 0;
    check(mtpz_handshake(&session) == 0 && sends == 3, "the next handshake after the failures succeeds");

    mtpz_free_keys();
    gcry_mpi_release(key_n);
    gcry_mpi_release(key_e);
    printf("MTPZ null checks: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
