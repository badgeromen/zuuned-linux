/* Send-retry triage against the real track/photo/video send loops and MTP
 * layer, with ptp_transaction replaced. The data phase fails with a chosen
 * response code; the orphan DeleteObject that follows succeeds and overwrites
 * last_response with OK. A refusal (StoreFull, AccessDenied) must be tried
 * once, reported with its own code, and still be what zune_autopsy reads;
 * transport-class failures keep their three attempts. */
#include "../src/zune_internal.h"
#include <assert.h>
#include <stdarg.h>

static int checks;
static char last_error[512];
static int object_infos, sends, deletes, unjams;
static uint16_t send_failure;          /* 0: SendObject succeeds */

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
}
void zune_clear_error(void) { last_error[0] = 0; }
const char *zune_get_error(void) { return last_error[0] ? last_error : NULL; }
const char *zune_autopsy_name(uint16_t code) {
    return code == 0x200C ? "StoreFull" : code == 0x200F ? "AccessDenied"
         : code == 0x2001 ? "OK" : "Other";
}
int zune_is_aborted(ZuneDevice *dev) { return dev->cancel_requested; }
int zune_unjam(ZuneDevice *dev) { (void)dev; unjams++; return 0; }

int ptp_transaction(ptp_session_t *s, uint16_t opcode,
                    uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)nparams; (void)flags; (void)send_data; (void)send_len;
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    uint16_t rc = PTP_RC_OK;
    switch (opcode) {
    case PTP_OC_SendObjectInfo:
        object_infos++;
        resp_params[0] = params[0];
        resp_params[1] = params[1];
        resp_params[2] = 42;
        *resp_nparams = 3;
        break;
    case PTP_OC_SendObject:
        sends++;
        if (send_failure) rc = send_failure;
        break;
    case PTP_OC_DeleteObject:
        deletes++;
        break;
    case MTP_OC_SetObjectPropValue:
    case MTP_OC_SetObjPropList:
        break;
    default:
        fprintf(stderr, "unexpected opcode 0x%04x\n", opcode);
        abort();
    }
    s->last_response = rc;
    if (response_code) *response_code = rc;
    return rc == PTP_RC_OK ? 0 : -1;
}

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s (error: %s)\n", name, last_error); exit(1); }
    printf("PASS: %s\n", name);
    checks++;
}

static void reset(uint16_t failure) {
    object_infos = sends = deletes = unjams = 0;
    send_failure = failure;
    last_error[0] = '\0';
}

static void write_file(const char *path, const void *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    assert(fp && fwrite(data, 1, len, fp) == len);
    fclose(fp);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char track[512], photo[512], video[512];
    snprintf(track, sizeof(track), "%s/song.mp3", argv[1]);
    snprintf(photo, sizeof(photo), "%s/picture.jpg", argv[1]);
    snprintf(video, sizeof(video), "%s/clip.mp4", argv[1]);
    write_file(track, "ID3 audio bytes", 15);
    static const uint8_t jpeg[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F', 0xFF, 0xD9 };
    write_file(photo, jpeg, sizeof(jpeg));
    write_file(video, "mp4 video bytes", 15);

    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));
    dev.storage_id = 0x00010001;
    uint32_t id = 0;

    const uint16_t refusals[] = { 0x200C, 0x200F };
    for (int i = 0; i < 2; i++) {
        char name[160];
        reset(refusals[i]);
        int rc = zune_smuggle_track_tagged(&dev, track, "Song", "Artist", "Album", "",
                                           1, 1000, &id);
        snprintf(name, sizeof(name), "track: %s is tried once and the orphan is deleted",
                 zune_autopsy_name(refusals[i]));
        check(rc == -1 && object_infos == 1 && sends == 1 && deletes == 1 && unjams == 0, name);
        snprintf(name, sizeof(name), "track: autopsy and error report %s, not the cleanup's OK",
                 zune_autopsy_name(refusals[i]));
        check(dev.ptp.last_response == refusals[i] && strstr(last_error, zune_autopsy_name(refusals[i])),
              name);

        reset(refusals[i]);
        rc = zune_smuggle_photo(&dev, photo, NULL);
        snprintf(name, sizeof(name), "photo: %s is tried once and the orphan is deleted",
                 zune_autopsy_name(refusals[i]));
        check(rc == -1 && object_infos == 1 && sends == 1 && deletes == 1 && unjams == 0, name);
        snprintf(name, sizeof(name), "photo: autopsy and error report %s, not the cleanup's OK",
                 zune_autopsy_name(refusals[i]));
        check(dev.ptp.last_response == refusals[i] && strstr(last_error, zune_autopsy_name(refusals[i])),
              name);

        reset(refusals[i]);
        rc = zune_smuggle_video_named(&dev, video, "clip.mp4", "Clip", ZUNE_METAGENRE_OTHER,
                                      NULL, 0, 0, NULL, NULL, 0, &id);
        snprintf(name, sizeof(name), "video: autopsy reports %s after cleanup",
                 zune_autopsy_name(refusals[i]));
        check(rc == -1 && sends == 1 && dev.ptp.last_response == refusals[i], name);
    }

    /* A transport-class failure is still worth the full retry budget. */
    reset(0x2002);
    int rc = zune_smuggle_track_tagged(&dev, track, "Song", "Artist", "Album", "", 1, 1000, &id);
    check(rc == -1 && object_infos == 3 && sends == 3 && deletes == 3 && unjams == 2
          && dev.ptp.last_response == 0x2002,
          "track: GeneralError keeps three attempts and reports 0x2002");
    reset(0x2002);
    rc = zune_smuggle_photo(&dev, photo, NULL);
    check(rc == -1 && sends == 3 && unjams == 2 && dev.ptp.last_response == 0x2002,
          "photo: GeneralError keeps three attempts and reports 0x2002");

    reset(0);
    id = 0;
    rc = zune_smuggle_track_tagged(&dev, track, "Song", "Artist", "Album", "", 1, 1000, &id);
    check(rc == 0 && id == 42 && sends == 1 && deletes == 0, "track: a successful send is unchanged");
    reset(0);
    rc = zune_smuggle_photo(&dev, photo, NULL);
    check(rc == 0 && sends == 1 && deletes == 0, "photo: a successful send is unchanged");

    printf("Send retry triage: %d checks passed; no USB\n", checks);
    return 0;
}
