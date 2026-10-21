/* zune_zmdb_read_payload and zune_zmdb_read_response (zune_dump_raw's
 * reader) against a scripted USB pipe with virtual time.
 * Each IN transfer arrives at a set time; a read returns it when it has
 * arrived by the read's timeout. After a ZMDB read the pipe must be empty,
 * as the next PTP transaction expects: a leftover OK response is read as
 * that transaction's answer (Draco wedge, October 19 2026). */
#include "zune_internal.h"
#include <assert.h>
#include <stdarg.h>

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
void zune_clear_error(void) {}

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

#define MAX_TRANSFERS 8
static struct { uint8_t *data; uint32_t len, sent; uint32_t at_ms; } pipe_in[MAX_TRANSFERS];
static int queued, next;
static uint32_t now_ms;

static void enqueue(const uint8_t *data, uint32_t len, uint32_t at_ms) {
    assert(queued < MAX_TRANSFERS);
    pipe_in[queued].data = malloc(len ? len : 1);
    if (len) memcpy(pipe_in[queued].data, data, len);
    pipe_in[queued].len = len;
    pipe_in[queued].sent = 0;
    pipe_in[queued].at_ms = at_ms;
    queued++;
}

static int pending(void) { return queued - next; }

static void reset_pipe(void) {
    for (int i = 0; i < queued; i++) free(pipe_in[i].data);
    queued = next = 0;
    now_ms = 0;
}

static int fake_read(zune_usb_handle_t *h, uint8_t ep, uint8_t *p, uint32_t max,
                     uint32_t *actual, uint32_t timeout) {
    (void)h; (void)ep;
    *actual = 0;
    if (next >= queued || pipe_in[next].at_ms > now_ms + timeout) {
        now_ms += timeout;                     /* nothing arrived in time */
        return -1;
    }
    if (pipe_in[next].at_ms > now_ms) now_ms = pipe_in[next].at_ms;
    uint32_t left = pipe_in[next].len - pipe_in[next].sent;
    uint32_t n = left < max ? left : max;
    memcpy(p, pipe_in[next].data + pipe_in[next].sent, n);
    pipe_in[next].sent += n;
    *actual = n;
    if (pipe_in[next].sent == pipe_in[next].len) next++;
    return 0;
}

static int writes;
static int fake_write(zune_usb_handle_t *h, uint8_t ep, const uint8_t *p, uint32_t n,
                      uint32_t *actual, uint32_t timeout) {
    (void)h; (void)ep; (void)timeout;
    assert(n == 16 && p[6] == 0x17 && p[7] == 0x92);
    writes++;
    *actual = n;
    return 0;
}

static const zune_usb_backend_t backend = { .bulk_read = fake_read, .bulk_write = fake_write };

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* Script one GetZMDB exchange. split: 12-byte header as its own transfer.
 * zlp: a zero-length packet after the payload. response_at: arrival of the
 * 12-byte response (0 = none sent). */
static uint8_t *script(uint32_t payload_len, int split, int zlp, uint32_t response_at,
                       uint16_t response_code) {
    uint8_t header[12];
    put32(header, payload_len + 12);
    header[4] = 2; header[5] = 0;              /* data container */
    header[6] = 0x17; header[7] = 0x92;        /* 0x9217 */
    put32(header + 8, 0x001F9203);
    uint8_t *payload = malloc(payload_len);
    for (uint32_t i = 0; i < payload_len; i++) payload[i] = (uint8_t)(i * 7 + 3);
    if (split) {
        enqueue(header, 12, 300);
        enqueue(payload, payload_len, 300);
    } else {
        uint8_t *both = malloc(payload_len + 12);
        memcpy(both, header, 12);
        memcpy(both + 12, payload, payload_len);
        enqueue(both, payload_len + 12, 300);
        free(both);
    }
    if (zlp) enqueue(NULL, 0, 300);
    if (response_at) {
        uint8_t response[12];
        put32(response, 12);
        response[4] = 3; response[5] = 0;      /* response container */
        response[6] = (uint8_t)response_code; response[7] = (uint8_t)(response_code >> 8);
        put32(response + 8, 0x001F9203);
        enqueue(response, 12, response_at);
    }
    return payload;
}

static void run(const char *name, uint32_t payload_len, int split, int zlp,
                uint32_t response_at, uint16_t code) {
    reset_pipe();
    uint8_t *expected = script(payload_len, split, zlp, response_at, code);
    zune_usb_handle_t usb = { .backend = &backend, .bulk_in_ep = 0x81, .bulk_out_ep = 0x02 };
    uint8_t *payload = NULL;
    uint32_t len = 0;
    int rc = zune_zmdb_read_payload(&usb, &payload, &len);
    int ok = rc == 0 && len == payload_len && payload && !memcmp(payload, expected, payload_len);
    char label[200];
    snprintf(label, sizeof(label), "%s: payload intact, pipe left empty (%d pending)", name, pending());
    check(ok && pending() == 0, label);
    free(payload);
    free(expected);
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    run("split header, prompt response", 70000, 1, 0, 310, 0x2001);
    run("payload on a 512-byte boundary: ZLP, then response", 65536 + 512 * 3, 1, 1, 310, 0x2001);
    run("busy device answers after 400 ms", 70000, 1, 0, 700, 0x2001);
    run("ZLP and a late response", 65536 * 2, 1, 1, 650, 0x2001);
    run("header and payload in one transfer", 4000, 0, 0, 310, 0x2001);
    run("no response at all still completes", 70000, 1, 0, 0, 0x2001);
    run("non-OK response is consumed too", 70000, 1, 0, 310, 0x2002);

    /* zune_dump_raw writes the container header too: the header variant
     * returns it unchanged and leaves the pipe empty in the same way. */
    reset_pipe();
    uint8_t *expected = script(65536 + 512 * 3, 1, 1, 650, 0x2001);
    zune_usb_handle_t usb = { .backend = &backend, .bulk_in_ep = 0x81, .bulk_out_ep = 0x02 };
    uint8_t header[12] = {0}, *payload = NULL;
    uint32_t len = 0;
    int rc = zune_zmdb_read_response(&usb, header, &payload, &len);
    check(rc == 0 && len == 65536 + 512 * 3 && !memcmp(payload, expected, len) &&
          header[0] == (uint8_t)(len + 12) && header[4] == 2 && header[6] == 0x17 && header[7] == 0x92 &&
          pending() == 0,
          "dump reader: header returned, payload intact, ZLP and late response consumed");
    free(payload);
    free(expected);

    /* A header whose total is smaller than the header itself is refused. */
    reset_pipe();
    uint8_t tiny[16] = {0};
    put32(tiny, 4);
    tiny[4] = 2; tiny[6] = 0x17; tiny[7] = 0x92;
    enqueue(tiny, sizeof(tiny), 300);
    payload = NULL;
    rc = zune_zmdb_read_response(&usb, header, &payload, &len);
    check(rc == -1 && !payload, "a response total below its header size is refused");
    reset_pipe();
    printf("ZMDB transport: %d checks, %d failures; %d requests; no USB\n", checks, failures, writes);
    return failures ? 1 : 0;
}
