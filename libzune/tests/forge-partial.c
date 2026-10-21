/* T3.3: an album or playlist whose commit or track references fail is a
 * failure: the new object is deleted, 0 is returned, zune_autopsy keeps the
 * original response and the error says whether the cleanup worked. Real
 * album.c/playlist.c/mtp.c over an in-memory device. No USB. */
#include "../src/zune_internal.h"
#include <stdarg.h>

static int checks, failures;
static char last_error[1024];

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
}
void zune_clear_error(void) { last_error[0] = 0; }
const char *zune_get_error(void) { return last_error[0] ? last_error : NULL; }
const char *zune_autopsy_name(uint16_t code) { return code == 0x2002 ? "GeneralError" : "other"; }

static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", name, ok ? "" : " -- ", ok ? "" : last_error);
}

static uint16_t commit_code, references_code, delete_code;
static uint32_t deleted, references_set;
static const uint32_t NEW_HANDLE = 0x06000777;

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len, uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code, uint32_t *resp_params, int *resp_nparams) {
    (void)nparams; (void)flags; (void)send_data; (void)send_len;
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    uint16_t code = PTP_RC_OK;
    switch (opcode) {
    case MTP_OC_GetObjectPropsSupported: {
        static const uint16_t props[] = { 0xDC07, 0xDC44, 0xDC46, 0xDC8C };
        uint8_t *d = calloc(1, 4 + sizeof(props));
        d[0] = 4;
        for (int i = 0; i < 4; i++) { d[4 + 2 * i] = (uint8_t)props[i]; d[5 + 2 * i] = (uint8_t)(props[i] >> 8); }
        *recv_data = d;
        *recv_len = 4 + sizeof(props);
        break;
    }
    case PTP_OC_GetObjectHandles: {
        /* No artist exists yet, so zune_forge_artist goes on to create one. */
        *recv_data = calloc(1, 4);
        *recv_len = 4;
        break;
    }
    case MTP_OC_SendObjectPropList:
        resp_params[0] = 0x00010001; resp_params[1] = 0; resp_params[2] = NEW_HANDLE;
        *resp_nparams = 3;
        break;
    case PTP_OC_SendObject:
        code = commit_code;
        break;
    case MTP_OC_SetObjectReferences:
        ++references_set;
        code = references_code;
        break;
    case PTP_OC_DeleteObject:
        deleted = params[0];
        code = delete_code;
        break;
    default:
        code = PTP_RC_OperationNotSupported;
    }
    s->last_response = code;
    if (response_code) *response_code = code;
    return code == PTP_RC_OK ? 0 : -1;
}

static void reset(uint16_t commit, uint16_t references, uint16_t del) {
    commit_code = commit; references_code = references; delete_code = del;
    deleted = references_set = 0;
    last_error[0] = 0;
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));
    dev.storage_id = 0x00010001;
    uint32_t tracks[] = { 0x10000001, 0x10000002 };

    reset(PTP_RC_OK, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_album(&dev, "Album", "Artist", "Rock", tracks, 2) == NEW_HANDLE && !deleted,
          "a complete album is returned and kept");
    reset(0x2002, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_album(&dev, "Album", "Artist", "Rock", tracks, 2) == 0 && deleted == NEW_HANDLE &&
          dev.ptp.last_response == 0x2002 && strstr(last_error, "commit") && strstr(last_error, "removed"),
          "an album whose commit fails is removed and autopsy keeps the commit's code");
    reset(PTP_RC_OK, 0x2002, PTP_RC_OK);
    check(zune_forge_album(&dev, "Album", "Artist", "Rock", tracks, 2) == 0 && deleted == NEW_HANDLE &&
          references_set == 1 && dev.ptp.last_response == 0x2002 && strstr(last_error, "track references"),
          "an album whose track references fail is removed, not returned without its tracks");
    reset(PTP_RC_OK, 0x2002, 0x2009);
    check(zune_forge_album(&dev, "Album", "Artist", "Rock", tracks, 2) == 0 &&
          dev.ptp.last_response == 0x2002 && strstr(last_error, "could not be removed"),
          "when the cleanup fails too, the error says so and autopsy still names the failure");
    reset(PTP_RC_OK, PTP_ERROR_IO, PTP_RC_OK);
    check(zune_forge_album(&dev, "Album", "Artist", "Rock", tracks, 2) == 0 && !deleted &&
          dev.ptp.last_response == PTP_ERROR_IO && strstr(last_error, "kept"),
          "an album whose references got no answer is kept (an HD deletes its tracks with it) and reported");

    reset(0x2002, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_artist(&dev, "Artist") == 0 && deleted == NEW_HANDLE &&
          dev.ptp.last_response == 0x2002 && strstr(last_error, "commit") && strstr(last_error, "removed"),
          "an artist whose commit fails is removed and autopsy keeps the commit's code");
    reset(PTP_RC_OK, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_artist(&dev, "Artist") == NEW_HANDLE && !deleted,
          "a committed artist is returned and kept");

    reset(PTP_RC_OK, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_playlist(&dev, "Mix", tracks, 2) == NEW_HANDLE && !deleted,
          "a complete playlist is returned and kept");
    reset(0x2002, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_playlist(&dev, "Mix", tracks, 2) == 0 && deleted == NEW_HANDLE &&
          dev.ptp.last_response == 0x2002 && strstr(last_error, "commit"),
          "a playlist whose commit fails is removed (its result was ignored before)");
    reset(PTP_RC_OK, 0x2002, PTP_RC_OK);
    check(zune_forge_playlist(&dev, "Mix", tracks, 2) == 0 && deleted == NEW_HANDLE &&
          dev.ptp.last_response == 0x2002 && strstr(last_error, "track references"),
          "a playlist whose track references fail is removed and reported as failed");
    reset(PTP_RC_OK, PTP_RC_OK, PTP_RC_OK);
    check(zune_forge_playlist(&dev, "Empty", NULL, 0) == NEW_HANDLE && references_set == 0,
          "an empty playlist sets no references and is kept");

    printf("Forge partial failures: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
