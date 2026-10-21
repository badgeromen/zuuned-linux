/* T3.1: one upload engine. Its decisions over a scripted device: retry after
 * a removed partial object, stop on a mortal refusal, never retry an
 * unanswered creation when strict, report an object it could not remove,
 * keep zune_autopsy on the failure, honour abort, refuse a changed source,
 * treat a creation answered OK without a handle as unanswered.
 * Real upload.c/mtp.c; the device answers at the ptp_transaction level. */
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
const char *zune_autopsy_name(uint16_t code) { (void)code; return "code"; }
int zune_is_aborted(ZuneDevice *dev) { return ZUNE_FLAG_GET(dev->cancel_requested); }
static int unjams;
int zune_unjam(ZuneDevice *dev) { (void)dev; ++unjams; return 0; }
int usleep(useconds_t usec) { (void)usec; return 0; }

static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", name, ok ? "" : " -- ", ok ? "" : last_error);
}

/* Script: answers for successive creates, payloads and deletes. NO_HANDLE
 * answers a create with OK but no handle parameter. */
#define NO_HANDLE 0xFFFF
static uint16_t create_codes[4], payload_codes[4], delete_codes[4];
static int creates, payloads, deletes, abort_after_creates, disconnect_after_creates, truncate_on_create;
static uint32_t handle_seq, last_deleted;
static uint8_t received[64];
static uint32_t received_len;
static ZuneDevice dev;
static const char content[] = "upload engine payload";
static char path[64];

static uint16_t next(uint16_t *codes, int n) { return n < 4 ? codes[n] : PTP_RC_OK; }

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len, uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code, uint32_t *resp_params, int *resp_nparams) {
    (void)nparams; (void)flags;
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    uint16_t code = PTP_RC_OK;
    switch (opcode) {
    case PTP_OC_SendObjectInfo:
    case MTP_OC_SendObjectPropList:
        code = next(create_codes, creates++);
        if (truncate_on_create && creates == truncate_on_create && truncate(path, 4) != 0) return -1;
        if (code == NO_HANDLE) {
            code = PTP_RC_OK;
            *resp_nparams = 0;
        } else if (code == PTP_RC_OK) {
            resp_params[0] = 1; resp_params[1] = params ? params[1] : 0; resp_params[2] = 0x01000000u + ++handle_seq;
            *resp_nparams = 3;
        }
        if (abort_after_creates && creates == abort_after_creates) ZUNE_FLAG_SET(dev.cancel_requested, 1);
        if (disconnect_after_creates && creates == disconnect_after_creates) ZUNE_FLAG_SET(dev.disconnecting, 1);
        break;
    case PTP_OC_SendObject:
        code = next(payload_codes, payloads++);
        if (code == PTP_RC_OK) {
            received_len = (uint32_t)(send_len < sizeof(received) ? send_len : sizeof(received));
            memcpy(received, send_data, received_len);
        }
        break;
    case PTP_OC_DeleteObject:
        last_deleted = params[0];
        code = next(delete_codes, deletes++);
        break;
    default:
        code = PTP_RC_OperationNotSupported;
    }
    s->last_response = code;
    if (response_code) *response_code = code;
    return code == PTP_RC_OK ? 0 : -1;
}


static void reset(void) {
    memset(create_codes, 0, sizeof(create_codes));
    memset(payload_codes, 0, sizeof(payload_codes));
    memset(delete_codes, 0, sizeof(delete_codes));
    for (int i = 0; i < 4; i++) create_codes[i] = payload_codes[i] = delete_codes[i] = PTP_RC_OK;
    creates = payloads = deletes = unjams = abort_after_creates = disconnect_after_creates = truncate_on_create = 0;
    last_deleted = 0; received_len = 0;
    last_error[0] = 0;
    ZUNE_FLAG_SET(dev.cancel_requested, 0);
    ZUNE_FLAG_SET(dev.disconnecting, 0);
    dev.ptp.last_response = 0;
}

static int run(int strict, int props, uint32_t *handle) {
    static mtp_object_info_t info;
    memset(&info, 0, sizeof(info));
    info.storage_id = 1; info.object_format = MTP_OFC_MP3;
    info.object_compressed_size = sizeof(content); info.filename = "song.mp3";
    static mtp_prop_entry_t list[1];
    list[0].prop_code = MTP_OPC_ObjectFileName; list[0].data_type = PTP_DTC_STR; list[0].value.str = "song.mp3";
    zune_upload_t up = { .kind = "track", .path = path, .size = sizeof(content), .parent = 0,
                         .info = props ? NULL : &info, .format = MTP_OFC_MP3,
                         .props = list, .nprops = 1, .strict = strict };
    return zune_smuggle_core(&dev, &up, handle);
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    snprintf(path, sizeof(path), "/tmp/libzune-upload-engine-%d", (int)getpid());
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(content, 1, sizeof(content), f) != sizeof(content)) return 2;
    fclose(f);
    dev.storage_id = 1;
    uint32_t h = 0;

    reset();
    check(run(0, 0, &h) == ZUNE_UPLOAD_SENT && h && creates == 1 && payloads == 1 && !deletes &&
          received_len == sizeof(content) && !memcmp(received, content, sizeof(content)),
          "a clean upload creates once, sends the file's bytes and returns the handle");
    reset();
    check(run(1, 1, &h) == ZUNE_UPLOAD_SENT && h && creates == 1, "objects created from a property list upload the same way");

    reset();
    payload_codes[0] = PTP_ERROR_IO;
    check(run(0, 0, &h) == ZUNE_UPLOAD_SENT && creates == 2 && deletes == 1 && unjams == 1,
          "a failed payload removes the partial object, clears the pipes and retries");
    reset();
    payload_codes[0] = payload_codes[1] = payload_codes[2] = PTP_ERROR_IO;
    check(run(0, 0, &h) == ZUNE_UPLOAD_REMOVED && !h && creates == 3 && deletes == 3 &&
          dev.ptp.last_response == PTP_ERROR_IO,
          "after three failed payloads every object is removed and autopsy names the payload failure");

    reset();
    payload_codes[0] = 0x200C;         /* StoreFull */
    check(run(0, 0, &h) == ZUNE_UPLOAD_REMOVED && creates == 1 && deletes == 1 && dev.ptp.last_response == 0x200C,
          "a refused payload (StoreFull) is removed once and not retried");
    reset();
    create_codes[0] = 0x200C;
    check(run(0, 0, &h) == ZUNE_UPLOAD_REFUSED && creates == 1 && !payloads && dev.ptp.last_response == 0x200C,
          "a refused creation is not retried and leaves nothing");
    reset();
    create_codes[0] = 0x2019;          /* DeviceBusy: the device answered, nothing was made */
    check(run(1, 1, &h) == ZUNE_UPLOAD_SENT && creates == 2,
          "a creation refused by a busy device is retried, even for strict uploads");

    reset();
    create_codes[0] = PTP_ERROR_IO;
    check(run(1, 1, &h) == ZUNE_UPLOAD_UNCERTAIN && creates == 1 && !h,
          "strict uploads never retry a creation that got no answer");
    reset();
    create_codes[0] = PTP_ERROR_IO;
    check(run(0, 0, &h) == ZUNE_UPLOAD_SENT && creates == 2,
          "media uploads keep their retry after an unanswered creation");
    reset();
    create_codes[0] = NO_HANDLE;
    check(run(1, 1, &h) == ZUNE_UPLOAD_UNCERTAIN && creates == 1 && !h,
          "strict uploads never retry a property-list creation answered OK without a handle");
    reset();
    create_codes[0] = NO_HANDLE;
    check(run(1, 0, &h) == ZUNE_UPLOAD_UNCERTAIN && creates == 1 && !h && strstr(last_error, "may hold"),
          "strict uploads never retry an object-info creation answered OK without a handle");
    reset();
    create_codes[0] = NO_HANDLE;
    check(run(0, 0, &h) == ZUNE_UPLOAD_SENT && creates == 2 && h,
          "media uploads retry a creation answered OK without a handle, as after no answer");
    reset();
    create_codes[0] = PTP_ERROR_IO; create_codes[1] = create_codes[2] = 0x2019;
    check(run(0, 0, &h) == ZUNE_UPLOAD_UNCERTAIN,
          "once a creation went unanswered, a final failure stays uncertain");

    reset();
    payload_codes[0] = PTP_ERROR_IO; delete_codes[0] = PTP_ERROR_IO;
    check(run(0, 0, &h) == ZUNE_UPLOAD_ORPHANED && h == last_deleted && creates == 1 &&
          dev.ptp.last_response == PTP_ERROR_IO && strstr(last_error, "could not be removed"),
          "an object that cannot be removed stops the retries and is returned for inspection");
    reset();
    payload_codes[0] = PTP_ERROR_IO; delete_codes[0] = PTP_RC_InvalidObjectHandle;
    check(run(0, 0, &h) == ZUNE_UPLOAD_SENT && creates == 2,
          "a device that already dropped the partial object counts as removed");

    reset();
    payload_codes[0] = PTP_ERROR_IO; abort_after_creates = 1;
    check(run(0, 0, &h) == ZUNE_UPLOAD_REMOVED && creates == 1 && dev.ptp.last_response == PTP_ERROR_IO &&
          strstr(last_error, "Aborted") && !strstr(last_error, "may hold an incomplete object"),
          "an abort stops before the next attempt and autopsy keeps the failure, not its cleanup");
    reset();
    create_codes[0] = PTP_ERROR_IO; abort_after_creates = 1;
    check(run(0, 0, &h) == ZUNE_UPLOAD_UNCERTAIN && !h && creates == 1 && !payloads && !deletes &&
          dev.ptp.last_response == PTP_ERROR_IO && strstr(last_error, "may hold an incomplete object"),
          "cancelling after an unanswered creation preserves the warning required for transfer recovery");
    reset();
    create_codes[0] = NO_HANDLE; abort_after_creates = 1;
    check(run(0, 0, &h) == ZUNE_UPLOAD_UNCERTAIN && !h && creates == 1 && !payloads && !deletes &&
          dev.ptp.last_response == PTP_RC_OK && strstr(last_error, "may hold an incomplete object"),
          "cancelling after an OK creation without a handle still reports an object may remain");
    reset();
    create_codes[0] = PTP_ERROR_IO; disconnect_after_creates = 1;
    check(run(0, 0, &h) == ZUNE_UPLOAD_UNCERTAIN && !h && creates == 1 && !payloads && !deletes &&
          dev.ptp.last_response == PTP_ERROR_IO && strstr(last_error, "may hold an incomplete object"),
          "disconnecting after an unanswered creation preserves uncertainty without more device writes");
    reset();
    create_codes[0] = PTP_ERROR_IO;
    payload_codes[0] = PTP_ERROR_IO; abort_after_creates = 2;
    check(run(0, 0, &h) == ZUNE_UPLOAD_UNCERTAIN && !h && creates == 2 && payloads == 1 && deletes == 1 &&
          dev.ptp.last_response == PTP_ERROR_IO && strstr(last_error, "may hold an incomplete object"),
          "cancelling after a later attempt was removed cannot erase an earlier unanswered creation");
    reset();
    ZUNE_FLAG_SET(dev.cancel_requested, 1);
    check(run(0, 0, &h) == ZUNE_UPLOAD_REFUSED && !creates && !payloads && !deletes &&
          strstr(last_error, "Aborted") && !strstr(last_error, "may hold an incomplete object"),
          "cancelling before the first attempt does not invent an incomplete object");
    reset();
    ZUNE_FLAG_SET(dev.disconnecting, 1);
    check(run(0, 0, &h) == ZUNE_UPLOAD_REFUSED && !creates, "a disconnecting device gets no new attempt");
    ZUNE_FLAG_SET(dev.disconnecting, 0);

    reset();
    f = fopen(path, "ab");
    fputs("more", f);
    fclose(f);
    check(run(0, 0, &h) == ZUNE_UPLOAD_REFUSED && !creates && strstr(last_error, "changed size"),
          "a source that changed size since it was measured is refused before any device traffic");

    reset();
    f = fopen(path, "wb");
    if (!f || fwrite(content, 1, sizeof(content), f) != sizeof(content)) return 2;
    fclose(f);
    truncate_on_create = 1;
    check(run(0, 0, &h) == ZUNE_UPLOAD_REMOVED && creates == 1 && deletes == 1 && !h &&
          strstr(last_error, "changed size during the upload"),
          "a source that shrinks during an attempt is removed and not created again");

    unlink(path);
    printf("Upload engine: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
