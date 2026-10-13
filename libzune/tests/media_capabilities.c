/* Real mtp.c property decoder + research command; no USB or media writes. */
#include "../tools/media_capabilities.h"
#include <assert.h>

enum Scenario { NORMAL, EMPTY, TRUNCATED, WRAPPED, TOO_MANY,
                LIST_ERROR, DESC_ERROR, SHORT_DESC, WRONG_PROP, BAD_ACCESS,
                CANCEL_AFTER_LIST, MISSING_LIST, OVERSIZED_DESC };
static enum Scenario scenario;
static int queries, descriptors, checks;
static ZuneDevice device;

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params,
                    int nparams, uint16_t flags, uint8_t *send, uint64_t send_len,
                    uint8_t **data, uint32_t *length, uint16_t *response,
                    uint32_t *response_params, int *response_count)
{
    assert(flags == PTP_DP_GETDATA && !send && send_len == 0);
    assert(opcode == MTP_OC_GetObjectPropsSupported || opcode == MTP_OC_GetObjectPropDesc);
    ++queries;
    s->last_response = *response = 0x2001;
    if (opcode == MTP_OC_GetObjectPropsSupported) {
        assert(nparams == 1 && params[0] == 0xba0b);
        if (scenario == MISSING_LIST) { *data = NULL; *length = 0; return 0; }
        *length = scenario == TOO_MANY ? 4+257*2 : 8;
        *data = calloc(1, *length);
        assert(*data);
        (*data)[0] = 2;
        (*data)[4] = 0x9c; (*data)[5] = 0xda;
        (*data)[6] = 0x60; (*data)[7] = 0xdd;
        if (scenario == EMPTY) { (*data)[0] = 0; *length = 4; }
        if (scenario == TRUNCATED) *length = 5;
        if (scenario == WRAPPED) { memset(*data, 0xff, 4); *length = 4; }
        if (scenario == TOO_MANY) { (*data)[0] = 1; (*data)[1] = 1; }
        if (scenario == CANCEL_AFTER_LIST) device.cancel_requested = 1;
        if (scenario == LIST_ERROR) { s->last_response = *response = 0x200b; return -1; }
    } else {
        ++descriptors;
        assert(nparams == 2 && params[1] == 0xba0b);
        assert(params[0] == 0xda9c || params[0] == 0xdd60);
        *length = scenario == OVERSIZED_DESC ? 65537 : 16;
        *data = calloc(1, *length);
        assert(*data);
        (*data)[0] = params[0] & 0xff; (*data)[1] = params[0] >> 8;
        (*data)[2] = params[0] == 0xdd60 ? 4 : 2;
        (*data)[3] = params[0] == 0xdd60 ? 0x40 : 0;
        (*data)[4] = 1;
        if (scenario == DESC_ERROR) { s->last_response = *response = 0x02ff; return -1; }
        if (scenario == SHORT_DESC) *length = 4;
        if (scenario == WRONG_PROP) (*data)[0] ^= 1;
        if (scenario == BAD_ACCESS) (*data)[4] = 2;
    }
    return 0;
}

static void check(int success, const char *message)
{
    if (!success) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
    printf("PASS: %s\n", message); ++checks;
}

static void report(enum Scenario input, int success, int expected_queries, const char *marker)
{
    memset(&device, 0, sizeof(device));
    queries = descriptors = 0; scenario = input;
    char *text = NULL; size_t length = 0;
    FILE *output = open_memstream(&text, &length);
    assert(output);
    int result = media_probe_report(&device, 0xba0b, output);
    fclose(output);
    check((result == 0) == success && queries == expected_queries && strstr(text, marker), marker);
    if (!success) check(!strstr(text, "status\tcomplete"), "partial/error inventory cannot report completion");
    if (input == NORMAL) check(strstr(text, "0xDD60\t0x4004") != NULL,
                             "AUINT16 is reported without treating it as an MTP string");
    free(text);
}

int main(void)
{
    uint16_t format = 0;
    check(media_probe_format("0xBA0B", &format) == 0 && format == 0xba0b, "hex format accepted");
    check(media_probe_format("47627", &format) == 0 && format == 0xba0b, "decimal format accepted");
    const char *invalid[] = {"", "0", "-1", "+1", " 1", "0x10000", "12junk", "999999999999999999999999999"};
    int rejected = 0;
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i)
        rejected += media_probe_format(invalid[i], &format) != 0;
    check(rejected == 8, "invalid formats rejected before any device connection");
    report(NORMAL, 1, 3, "status\tcomplete");
    report(EMPTY, 1, 1, "properties\t0");
    report(TRUNCATED, 0, 1, "status\tquery-failed");
    report(WRAPPED, 0, 1, "status\tquery-failed");
    report(TOO_MANY, 0, 1, "status\tproperty-limit-or-invalid-list");
    report(LIST_ERROR, 0, 1, "response\t0x200B");
    report(DESC_ERROR, 0, 2, "response\t0x02FF");
    report(SHORT_DESC, 0, 2, "status\tinvalid-descriptor-header");
    report(WRONG_PROP, 0, 2, "status\tinvalid-descriptor-header");
    report(BAD_ACCESS, 0, 2, "status\tinvalid-descriptor-header");
    report(CANCEL_AFTER_LIST, 0, 1, "status\tcancelled");
    report(MISSING_LIST, 0, 1, "status\tquery-failed");
    report(OVERSIZED_DESC, 0, 2, "status\tinvalid-descriptor-header");
    device.cancel_requested = 1; queries = 0;
    FILE *sink = tmpfile(); assert(sink);
    check(media_probe_report(&device, 0xba0b, sink) != 0 && queries == 0, "cancelled probe performs no requests");
    check(media_probe_report(NULL, 0xba0b, sink) != 0, "null device rejected");
    fclose(sink);
    scenario = NORMAL; memset(&device, 0, sizeof(device));
    sink = fopen("/dev/full", "w"); assert(sink);
    check(media_probe_report(&device, 0xba0b, sink) != 0,
          "buffered report output failure is not reported as success");
    fclose(sink);
    printf("Media capabilities: %d checks passed; no USB\n", checks);
}
