/* Actual MTP property getters, strict read-only transaction allowlist. No USB. */
#include "../tools/hd_artist_probe.h"
#include <assert.h>

enum Scenario { NORMAL, EMPTY, INFO_FAIL, INFO_SHORT, INFO_EXTRA, INFO_COUNT,
    INFO_STRING, INFO_DUP, PROPS_FAIL, PROPS_SHORT, PROPS_EXTRA, PROPS_COUNT,
    PROPS_DUP, PROPS_ZERO, DESC_FAIL, DESC_SHORT, DESC_WRONG, DESC_ACCESS,
    DESC_HUGE, INVENTORY_FAIL, INVENTORY_SHORT, INVENTORY_EXTRA, INVENTORY_COUNT,
    INVENTORY_DUP, INVENTORY_ZERO, INVENTORY_ALL, VALUE_FAIL, VALUE_SHORT,
    VALUE_EXTRA, STRING_NUL, STRING_SURROGATE, STRING_INNER_NUL, CANCEL_INFO,
    CANCEL_DESC, CANCEL_VALUE, DISCONNECT_VALUE, UNKNOWN_TYPE, NO_CANDIDATES };
static enum Scenario scenario;
static ZuneDevice device;
static int calls, reads, checks, descriptors;
static const uint16_t properties[] = {0xdc44, 0xdc07, 0xda99, 0xdc41, 0xda97, 0xdab9};
static void u16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void u32(uint8_t *p, uint32_t v) { u16(p, v); u16(p+2, v>>16); }
static void allocate(uint8_t **p, uint32_t *n, uint32_t len) {
    *n = len; *p = calloc(1, len+16); assert(*p);
}
int ptp_transaction(ptp_session_t *s, uint16_t op, uint32_t *params,
                    int nparams, uint16_t flags, uint8_t *send, uint64_t send_len,
                    uint8_t **data, uint32_t *length, uint16_t *response,
                    uint32_t *response_params, int *response_count) {
    assert(flags == PTP_DP_GETDATA && !send && !send_len);
    assert(op == PTP_OC_GetDeviceInfo || op == MTP_OC_GetObjectPropsSupported ||
           op == MTP_OC_GetObjectPropDesc || op == PTP_OC_GetObjectHandles ||
           op == MTP_OC_GetObjectPropValue);
    calls++; s->last_response = *response = 0x2001;
    if ((op == PTP_OC_GetDeviceInfo && scenario == INFO_FAIL) ||
        (op == MTP_OC_GetObjectPropsSupported && scenario == PROPS_FAIL) ||
        (op == MTP_OC_GetObjectPropDesc && scenario == DESC_FAIL) ||
        (op == PTP_OC_GetObjectHandles && scenario == INVENTORY_FAIL) ||
        (op == MTP_OC_GetObjectPropValue && scenario == VALUE_FAIL)) {
        *data = NULL; *length = 0; s->last_response = *response = 0x2005; return -1;
    }
    if (op == PTP_OC_GetDeviceInfo) {
        assert(nparams == 0 && !params);
        allocate(data, length, 48);
        uint8_t *p = *data; size_t pos = 8;
        p[pos++] = 0; pos += 2;
        u32(p+pos, 3); pos += 4;
        u16(p+pos, 0x1001); u16(p+pos+2, 0x922c); u16(p+pos+4, 0x922d); pos += 6;
        pos += 4*4; /* four remaining empty arrays */
        pos += 4;   /* four empty strings */
        *length = pos;
        if (scenario == INFO_SHORT) *length = 7;
        if (scenario == INFO_EXTRA) ++*length;
        if (scenario == INFO_COUNT) u32(p+11, UINT32_MAX);
        if (scenario == INFO_STRING) p[8] = 255;
        if (scenario == INFO_DUP) u16(p+17, 0x1001);
        if (scenario == CANCEL_INFO) device.cancel_requested = 1;
    } else if (op == MTP_OC_GetObjectPropsSupported) {
        assert(nparams == 1 && params[0] == 0xb218);
        allocate(data, length, 16); u32(*data, 6);
        for (unsigned i = 0; i < 6; ++i) u16(*data+4+2*i, properties[i]);
        if (scenario == PROPS_SHORT) --*length;
        if (scenario == PROPS_EXTRA) ++*length;
        if (scenario == PROPS_COUNT) u32(*data, 257);
        if (scenario == PROPS_DUP) u16(*data+6, properties[0]);
        if (scenario == PROPS_ZERO) u16(*data+4, 0);
        if (scenario == NO_CANDIDATES) { u32(*data, 0); *length = 4; }
    } else if (op == MTP_OC_GetObjectPropDesc) {
        assert(nparams == 2 && params[1] == 0xb218); ++descriptors;
        allocate(data, length, scenario == DESC_HUGE ? 65537 : 32);
        u16(*data, params[0]);
        u16(*data+2, params[0] == 0xdc44 || params[0] == 0xdc07 ? PTP_DTC_STR :
                      params[0] == 0xdab9 || params[0] == 0xda97 ? PTP_DTC_UINT32 : HD_ART_UINT128);
        (*data)[4] = 1;
        if (scenario == DESC_SHORT) *length = 4;
        if (scenario == DESC_WRONG) (*data)[0] ^= 1;
        if (scenario == DESC_ACCESS) (*data)[4] = 2;
        if (scenario == UNKNOWN_TYPE) u16(*data+2, 0x4001);
        if (scenario == CANCEL_DESC) device.cancel_requested = 1;
    } else if (op == PTP_OC_GetObjectHandles) {
        assert(nparams == 3 && params[0] == 0x10001 && params[1] == 0xb218 && params[2] == 0);
        allocate(data, length, 16); u32(*data, 3);
        u32(*data+4, 7); u32(*data+8, 8); u32(*data+12, 9);
        if (scenario == EMPTY) { *length = 4; u32(*data, 0); }
        if (scenario == INVENTORY_SHORT) --*length;
        if (scenario == INVENTORY_EXTRA) ++*length;
        if (scenario == INVENTORY_COUNT) u32(*data, 4097);
        if (scenario == INVENTORY_DUP) u32(*data+12, 7);
        if (scenario == INVENTORY_ZERO) u32(*data+12, 0);
        if (scenario == INVENTORY_ALL) u32(*data+12, UINT32_MAX);
    } else {
        assert(nparams == 2 && params[0] >= 7 && params[0] <= 9); ++reads;
        uint16_t prop = params[1];
        if (prop == 0xdc44 || prop == 0xdc07) {
            allocate(data, length, 7); (*data)[0] = 3;
            u16(*data+1, 'A'); u16(*data+3, '\n');
            if (scenario == STRING_NUL) u16(*data+5, 'B');
            if (scenario == STRING_SURROGATE) u16(*data+3, 0xd800);
            if (scenario == STRING_INNER_NUL) u16(*data+3, 0);
        } else if (prop == 0xda99 || prop == 0xdc41) {
            allocate(data, length, 16); memset(*data, 0xab, 16);
        } else { allocate(data, length, 4); u32(*data, 123); }
        if (scenario == VALUE_SHORT) --*length;
        if (scenario == VALUE_EXTRA) ++*length;
        if (scenario == CANCEL_VALUE) device.cancel_requested = 1;
        if (scenario == DISCONNECT_VALUE) device.disconnecting = 1;
    }
    return 0;
}
static void check(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); exit(1); }
    printf("PASS %s\n", label); ++checks;
}
static void reset(enum Scenario next) {
    memset(&device, 0, sizeof(device)); device.device_family = ZUNE_FAMILY_PAVO;
    device.mtpz_authenticated = 1; device.storage_id = 0x10001;
    device.serial = "PRIVATE-SERIAL-MUST-NOT-APPEAR";
    calls = reads = descriptors = 0; scenario = next;
}
static char *run(int success, unsigned samples, const char *marker) {
    char *text = NULL; size_t len = 0; FILE *out = open_memstream(&text, &len); assert(out);
    int rc = hd_artist_probe_report(&device, samples, out); fclose(out);
    check((rc == 0) == success && strstr(text, marker), marker);
    if (!success) check(!strstr(text, "status\tcomplete"), "failures never claim completion");
    check(!strstr(text, device.serial), "device serial not emitted");
    return text;
}
int main(void) {
    unsigned count;
    check(!hd_art_sample_count("1", &count) && count == 1 && !hd_art_sample_count("8", &count) && count == 8,
          "bounded sample arguments accepted");
    const char *bad[] = {"", "0", "9", "-1", "+1", " 1", "01", "1x", "100000"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); ++i)
        check(hd_art_sample_count(bad[i], &count) != 0, "invalid sample argument rejected before connection");
    reset(NORMAL); char *text = run(1, 2, "artists-sampled\t2");
    check(reads == 12 && calls == 21 && descriptors == 6, "sample limit bounds exact identity reads");
    check(strstr(text, "operation\t0x922C") && strstr(text, "identity-wire-hex\t7\t0xDA99\t0x000A\tabababab"),
          "advertised tunnel opcode and exact GUID bytes reported without a tunnel write");
    check(strstr(text, "0341000a000000") && !strstr(text, "\tA\n"), "string control characters remain wire hex");
    free(text);
    reset(INFO_EXTRA); free(run(1, 1, "device-info-uninterpreted-trailing-bytes\t1"));
    check(reads == 6, "trailing device info bytes are reported, never interpreted as capabilities");
    reset(NORMAL); free(run(1, 8, "artists-sampled\t3"));
    check(reads == 18, "sample count stops at complete inventory length");
    reset(EMPTY); free(run(1, 3, "artists-total\t0")); check(!reads, "empty inventory never reads object zero");
    reset(NO_CANDIDATES); free(run(1, 3, "status\tcomplete")); check(!reads, "unadvertised identities never queried");
    reset(UNKNOWN_TYPE); free(run(1, 1, "candidate-skipped")); check(!reads, "unknown candidate datatype is explicit and never guessed");
    const struct { enum Scenario s; const char *marker; } failures[] = {
        {INFO_FAIL,"device-info-query-failed"},{INFO_SHORT,"invalid-device-info"},
        {INFO_COUNT,"invalid-device-info"},{INFO_STRING,"invalid-device-info"},{INFO_DUP,"invalid-device-info"},
        {PROPS_FAIL,"property-list-query-failed"},{PROPS_SHORT,"invalid-property-list"},{PROPS_EXTRA,"invalid-property-list"},
        {PROPS_COUNT,"invalid-property-list"},{PROPS_DUP,"invalid-property-list"},{PROPS_ZERO,"invalid-property-list"},
        {DESC_FAIL,"descriptor-query-failed"},{DESC_SHORT,"invalid-descriptor-header"},{DESC_WRONG,"invalid-descriptor-header"},
        {DESC_ACCESS,"invalid-descriptor-header"},{DESC_HUGE,"invalid-descriptor-header"},
        {INVENTORY_FAIL,"inventory-query-failed"},{INVENTORY_SHORT,"invalid-inventory"},{INVENTORY_EXTRA,"invalid-inventory"},
        {INVENTORY_COUNT,"invalid-inventory"},{INVENTORY_DUP,"invalid-inventory"},{INVENTORY_ZERO,"invalid-inventory"},
        {INVENTORY_ALL,"invalid-inventory"},{VALUE_FAIL,"identity-query-failed"},{VALUE_SHORT,"invalid-identity-value"},
        {VALUE_EXTRA,"invalid-identity-value"},{STRING_NUL,"invalid-identity-value"},
        {STRING_SURROGATE,"invalid-identity-value"},{STRING_INNER_NUL,"invalid-identity-value"},
        {CANCEL_DESC,"cancelled"},{CANCEL_VALUE,"cancelled"},{DISCONNECT_VALUE,"cancelled"},
        {CANCEL_INFO,"property-list-query-failed"}
    };
    for (unsigned i = 0; i < sizeof(failures)/sizeof(*failures); ++i) {
        reset(failures[i].s); text = run(0, 3, failures[i].marker);
        if (failures[i].s == VALUE_FAIL) check(strstr(text,"response\t0x2005") != NULL, "real refusal response retained");
        free(text);
    }
    reset(NORMAL); device.device_family = ZUNE_FAMILY_DRACO;
    free(run(0, 3, "not-zune-hd")); check(!calls, "non-HD guard precedes all reads");
    reset(NORMAL); device.mtpz_authenticated = 0;
    free(run(0, 3, "not-authenticated")); check(!calls, "authentication guard precedes all reads");
    reset(NORMAL); device.cancel_requested = 1;
    free(run(0, 3, "cancelled")); check(!calls, "cancelled probe performs no reads");
    reset(NORMAL); FILE *out = fopen("/dev/full", "w"); assert(out);
    check(hd_artist_probe_report(&device, 1, out) != 0, "output failure cannot report success"); fclose(out);
    out = tmpfile(); assert(out);
    check(hd_artist_probe_report(NULL, 1, out) != 0 && hd_artist_probe_report(&device, 0, out) != 0 &&
          hd_artist_probe_report(&device, 9, out) != 0 && hd_artist_probe_report(&device, 1, NULL) != 0,
          "invalid report input rejected"); fclose(out);
    printf("HD artist probe: %d checks passed; no USB and only allowlisted reads\n", checks);
}
