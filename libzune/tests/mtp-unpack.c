/* Device-supplied array counts in MTP replies, against the real mtp.c and
 * devinfo.c parsers. ptp_transaction is replaced: each call returns a crafted
 * data phase. A count chosen so that 4 + n * 4 wraps must be refused with no
 * out-of-bounds access; well-formed replies keep their exact results. */
#include "../src/zune_internal.h"
#include <assert.h>
#include <limits.h>
#include <signal.h>

static int checks;
static uint8_t reply[64];
static uint32_t reply_len;
static int transactions;

int ptp_transaction(ptp_session_t *s, uint16_t opcode,
                    uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)opcode; (void)params; (void)nparams; (void)flags;
    (void)send_data; (void)send_len; (void)resp_params; (void)resp_nparams;
    transactions++;
    s->last_response = PTP_RC_OK;
    if (response_code) *response_code = PTP_RC_OK;
    if (recv_data) {
        /* Exactly reply_len bytes: ASan flags any read past the reply. */
        *recv_data = malloc(reply_len ? reply_len : 1);
        assert(*recv_data);
        memcpy(*recv_data, reply, reply_len);
        *recv_len = reply_len;
    }
    return 0;
}

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
    printf("PASS: %s\n", name);
    checks++;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* count, then values; reply_len is the bytes actually present. */
static void array_reply(uint32_t count, const uint32_t *values, int present) {
    memset(reply, 0, sizeof(reply));
    put32(reply, count);
    for (int i = 0; i < present; i++) put32(reply + 4 + i * 4, values[i]);
    reply_len = 4 + (uint32_t)present * 4;
}

typedef int (*Unpack32)(ptp_session_t *, uint32_t **, int *);
static int storage_ids(ptp_session_t *s, uint32_t **out, int *count) {
    return mtp_get_storage_ids(s, out, count);
}
static int references(ptp_session_t *s, uint32_t **out, int *count) {
    return mtp_get_object_references(s, 77, out, count);
}
static int handles(ptp_session_t *s, uint32_t **out, int *count) {
    return mtp_get_object_handles(s, 0x10001, 0, 0, out, count);
}

static void uint32_arrays(const char *what, Unpack32 unpack) {
    ptp_session_t s;
    memset(&s, 0, sizeof(s));
    const uint32_t values[] = { 0x00010001, 0x00020001, 0x00030001 };
    char name[128];
    uint32_t *out = NULL;
    int count = -1;

    /* n = 0x40000000: 4 + n * 4 wraps to 4 in uint32_t. */
    const uint32_t wrapping[] = { 0x40000000u, 0x40000001u, 0x7FFFFFFFu, 0xFFFFFFFFu };
    for (int i = 0; i < 4; i++) {
        array_reply(wrapping[i], values, 1);
        out = (uint32_t *)1;
        count = -1;
        snprintf(name, sizeof(name), "%s: count 0x%08X in an 8-byte reply is refused",
                 what, wrapping[i]);
        check(unpack(&s, &out, &count) == -1 && out == NULL && count == 0, name);
    }

    array_reply(3, values, 2);
    snprintf(name, sizeof(name), "%s: count one past the reply is refused", what);
    check(unpack(&s, &out, &count) == -1 && out == NULL, name);

    array_reply(3, values, 3);
    snprintf(name, sizeof(name), "%s: exact-length reply keeps every value", what);
    check(unpack(&s, &out, &count) == 0 && count == 3 && out
          && out[0] == values[0] && out[2] == values[2], name);
    free(out);

    array_reply(0, values, 0);
    out = (uint32_t *)1;
    snprintf(name, sizeof(name), "%s: an empty array succeeds as NULL/0", what);
    check(unpack(&s, &out, &count) == 0 && count == 0 && out == NULL, name);
}

static void props_supported(void) {
    ptp_session_t s;
    memset(&s, 0, sizeof(s));
    uint16_t *props = NULL;
    int count = 0;
    memset(reply, 0, sizeof(reply));
    put32(reply, 0x80000000u);     /* 4 + n * 2 wraps */
    reply_len = 8;
    check(mtp_get_props_supported(&s, 0x3009, &props, &count) == -1 && !props,
          "props supported: wrapping uint16 count is refused");
}

static void outbound_arrays(void) {
    ptp_session_t s;
    memset(&s, 0, sizeof(s));
    uint32_t refs[2] = { 1, 2 };
    transactions = 0;
    check(mtp_set_object_references(&s, 5, refs, -1) == -1 && transactions == 0,
          "set references: a negative count never reaches the wire");
    check(mtp_set_object_references(&s, 5, refs, INT_MAX) == -1 && transactions == 0,
          "set references: a count whose byte size wraps never reaches the wire");
    uint16_t chars[2] = { 'h', 'i' };
    check(mtp_set_object_prop_value_ucs2(&s, 5, 0xDC44, chars, 0x80000000u) == -1
          && transactions == 0,
          "set UCS-2 property: a count whose byte size wraps never reaches the wire");
    check(mtp_set_object_references(&s, 5, refs, 2) == 0 && transactions == 1,
          "set references: a normal list is still sent");
}

/* GetDeviceInfo: header, vendor string, functional mode, five arrays, then
 * manufacturer/model/version/serial strings. */
static size_t put_string(uint8_t *p, const char *ascii) {
    size_t n = strlen(ascii) + 1;
    p[0] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) { p[1 + 2 * i] = (uint8_t)ascii[i]; p[2 + 2 * i] = 0; }
    return 1 + 2 * n;
}

static size_t device_info(uint8_t *buf, uint32_t first_count) {
    size_t off = 8;
    memset(buf, 0, 512);
    off += put_string(buf + off, "microsoft.com: 1.0");
    off += 2;                                   /* FunctionalMode */
    for (int a = 0; a < 5; a++) {
        put32(buf + off, a == 0 ? first_count : 1);
        off += 4;
        if (a == 0 && first_count != 2) continue;   /* hostile count: no values */
        uint32_t n = a == 0 ? 2 : 1;
        for (uint32_t i = 0; i < n; i++) { buf[off] = 0x01; buf[off + 1] = 0x10; off += 2; }
    }
    off += put_string(buf + off, "Microsoft");
    off += put_string(buf + off, "Zune HD");
    off += put_string(buf + off, "1.0");
    off += put_string(buf + off, "SERIAL123");
    return off;
}

static void devinfo(void) {
    static uint8_t buf[512];
    ZuneDevice dev;

    memset(&dev, 0, sizeof(dev));
    size_t len = device_info(buf, 2);
    zune_parse_device_info(&dev, buf, (uint32_t)len);
    check(dev.model && strcmp(dev.model, "Zune HD") == 0 && dev.serial
          && strcmp(dev.serial, "SERIAL123") == 0,
          "GetDeviceInfo: model and serial still parse from a valid dataset");
    free(dev.model); free(dev.serial);

    /* 0x80000000 * 2 wrapped the uint32 cursor back onto earlier bytes. */
    const uint32_t hostile[] = { 0x80000000u, 0xFFFFFFFFu, 0x7FFFFFFFu, 200 };
    for (int i = 0; i < 4; i++) {
        memset(&dev, 0, sizeof(dev));
        len = device_info(buf, hostile[i]);
        zune_parse_device_info(&dev, buf, (uint32_t)len);
        char name[96];
        snprintf(name, sizeof(name), "GetDeviceInfo: array count 0x%08X leaves model/serial unset",
                 hostile[i]);
        check(!dev.model && !dev.serial, name);
    }

    for (uint32_t cut = 0; cut < 12; cut++) {
        memset(&dev, 0, sizeof(dev));
        uint8_t *tiny = malloc(cut ? cut : 1);
        memcpy(tiny, buf, cut);
        zune_parse_device_info(&dev, tiny, cut);
        free(tiny);
        free(dev.model); free(dev.serial);
    }
    check(1, "GetDeviceInfo: datasets of 0..11 bytes are read in bounds");
}

int main(void) {
    alarm(30);
    uint32_arrays("storage IDs", storage_ids);
    uint32_arrays("object references", references);
    uint32_arrays("object handles", handles);
    props_supported();
    outbound_arrays();
    devinfo();
    printf("MTP array unpacking: %d checks passed; no USB\n", checks);
    return 0;
}
