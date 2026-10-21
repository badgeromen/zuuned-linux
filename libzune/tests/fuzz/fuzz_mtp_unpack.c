/* libFuzzer: arbitrary bytes as the data phase of an MTP reply. The first
 * byte picks the parser; the rest is what ptp_transaction returns. */
#include "../../src/zune_internal.h"

static const uint8_t *reply;
static uint32_t reply_len;

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams,
                    uint16_t flags, uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len, uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)opcode; (void)params; (void)nparams; (void)flags; (void)send_data;
    (void)send_len; (void)resp_params; (void)resp_nparams;
    s->last_response = PTP_RC_OK;
    if (response_code) *response_code = PTP_RC_OK;
    if (recv_data) {
        /* An exact-size copy, so ASan sees any read past the reply. */
        *recv_data = malloc(reply_len ? reply_len : 1);
        if (!*recv_data) return -1;
        memcpy(*recv_data, reply, reply_len);
        *recv_len = reply_len;
    }
    return 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1 || size > 1 + UINT32_MAX / 2) return 0;
    reply = data + 1;
    reply_len = (uint32_t)(size - 1);
    ptp_session_t s;
    memset(&s, 0, sizeof(s));
    uint32_t *ids = NULL;
    uint16_t *codes = NULL;
    int count = 0;
    switch (data[0] % 8) {
    case 0: if (mtp_get_storage_ids(&s, &ids, &count) == 0) free(ids); break;
    case 1: if (mtp_get_object_references(&s, 1, &ids, &count) == 0) free(ids); break;
    case 2: if (mtp_get_object_handles(&s, 0x10001, 0, 0, &ids, &count) == 0) free(ids); break;
    case 3: if (mtp_get_props_supported(&s, 0x3009, &codes, &count) == 0) free(codes); break;
    case 4: {
        mtp_object_info_t info;
        if (mtp_get_object_info(&s, 1, &info) == 0) mtp_free_object_info(&info);
        break;
    }
    case 5: {
        uint8_t *sample = NULL;
        uint32_t len = 0;
        if (mtp_get_representative_sample(&s, 1, &sample, &len) == 0) free(sample);
        break;
    }
    case 6: free(mtp_ucs2_to_string(reply, reply_len)); break;
    case 7: {
        ZuneDevice dev;
        memset(&dev, 0, sizeof(dev));
        zune_parse_device_info(&dev, reply, reply_len);
        free(dev.model);
        free(dev.serial);
        break;
    }
    }
    return 0;
}
