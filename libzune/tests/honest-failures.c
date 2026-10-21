/* Failures reported honestly, against the real device/track/photo/video/
 * playlist code with ptp_transaction replaced. A failed inventory query must
 * be an error rather than an empty device, failed purges/extracts must set
 * the thread-local error, and ZUNE_PL_FMT must not steer shipping builds. */
#include "../src/zune_internal.h"
#include <assert.h>

static int checks;
static int fail_handles, fail_delete, fail_get_object, folder_handles;
static uint32_t playlist_format;

/* The real thread-local error from device.c. */
#define last_error (zune_get_error() ? zune_get_error() : "")
static void clear_error(void) { zune_set_error("%s", ""); }

static uint8_t *copy_of(const void *data, uint32_t len) {
    uint8_t *out = malloc(len ? len : 1);
    assert(out);
    memcpy(out, data, len);
    return out;
}

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams,
                    uint16_t flags, uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len, uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)nparams; (void)flags; (void)send_data; (void)send_len;
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    uint16_t rc = PTP_RC_OK;
    static const uint8_t empty_array[4] = { 0 };
    switch (opcode) {
    case PTP_OC_GetObjectHandles:
        if (fail_handles) { rc = 0x2002; break; }
        if (folder_handles) {          /* three folders, whose details never arrive */
            static const uint8_t three[16] = { 3, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0 };
            *recv_data = copy_of(three, sizeof(three));
            *recv_len = sizeof(three);
            break;
        }
        *recv_data = copy_of(empty_array, 4);
        *recv_len = 4;
        break;
    case PTP_OC_GetObjectInfo:
        rc = PTP_ERROR_IO;
        break;
    case PTP_OC_DeleteObject:
        if (fail_delete) rc = 0x2009;
        break;
    case PTP_OC_GetObject:
        if (fail_get_object) rc = 0x2009;
        else { *recv_data = copy_of("x", 1); *recv_len = 1; }
        break;
    case MTP_OC_GetObjectPropsSupported:
        *recv_data = copy_of(empty_array, 4);
        *recv_len = 4;
        break;
    case MTP_OC_SendObjectPropList:
        playlist_format = params[2];
        resp_params[0] = params[0]; resp_params[1] = params[1]; resp_params[2] = 77;
        *resp_nparams = 3;
        break;
    case PTP_OC_SendObject:
    case MTP_OC_SetObjectReferences:
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

int main(void) {
    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));
    dev.storage_id = 0x00010001;

    /* Folders */
    fail_handles = 1;
    ZuneFolderEntry *folders = (ZuneFolderEntry *)1;
    clear_error();
    check(zune_get_folders(&dev, &folders) == -1 && folders == NULL
          && strstr(last_error, "Could not list device folders"),
          "zune_get_folders: a failed handle query is -1 with an error, not 0");
    fail_handles = 0;
    clear_error();
    check(zune_get_folders(&dev, &folders) == 0 && folders == NULL && !zune_get_error(),
          "zune_get_folders: a device with no folders is still 0");
    check(zune_get_folders(NULL, &folders) == -1 && zune_get_error(), "zune_get_folders: NULL device sets an error");
    folder_handles = 1;
    clear_error();
    check(zune_get_folders(&dev, &folders) == -1 && folders == NULL
          && strstr(last_error, "Could not read device folders"),
          "zune_get_folders: a device that stops answering mid-list is -1, not a partial list");
    folder_handles = 0;

    /* Tracks */
    fail_handles = 1;
    int count = 7;
    clear_error();
    ZuneTrack *tracks = zune_get_tracks(&dev, &count);
    check(!tracks && count == 0 && strstr(last_error, "Could not list device tracks"),
          "zune_get_tracks: a failed handle query sets an error");
    fail_handles = 0;
    clear_error();
    tracks = zune_get_tracks(&dev, &count);
    check(!tracks && count == 0 && !zune_get_error(), "zune_get_tracks: an empty library is not an error");

    /* Photo albums */
    ZunePhotoFile photo = { .item_id = 1, .filename = "a.jpg", .parent_id = 5 };
    check(zune_get_photo_albums(&dev, &photo, 1, NULL) == NULL,
          "zune_get_photo_albums: a NULL album_count is refused, not dereferenced");

    /* Purge / extract */
    fail_delete = 1;
    clear_error();
    check(zune_purge_video(&dev, 9) == -1 && strstr(last_error, "delete video 9")
          && strstr(last_error, "0x2009"), "zune_purge_video sets the error with the device code");
    clear_error();
    check(zune_purge_photo(&dev, 9) == -1 && strstr(last_error, "delete photo 9")
          && strstr(last_error, "0x2009"), "zune_purge_photo sets the error with the device code");
    fail_delete = 0;
    fail_get_object = 1;
    clear_error();
    check(zune_extract_video(&dev, 9, "/tmp/libzune-honest-video") == -1
          && strstr(last_error, "download video 9"), "zune_extract_video sets the error");
    clear_error();
    check(zune_extract_photo(&dev, 9, "/tmp/libzune-honest-photo") == -1
          && strstr(last_error, "download photo 9"), "zune_extract_photo sets the error");
    fail_get_object = 0;
    unlink("/tmp/libzune-honest-video");
    unlink("/tmp/libzune-honest-photo");

    /* ZUNE_PL_FMT is a research knob, absent from shipping builds. */
    setenv("ZUNE_PL_FMT", "0xBA09", 1);
    uint32_t track_ids[1] = { 42 };
    uint32_t id = zune_forge_playlist(&dev, "Mix", track_ids, 1);
#ifdef ZUNE_DEBUG_KNOBS
    check(id == 77 && playlist_format == 0xBA09, "ZUNE_DEBUG_KNOBS build honours ZUNE_PL_FMT");
#else
    check(id == 77 && playlist_format == MTP_OFC_AbstractAudioVideoPlaylist,
          "shipping build ignores ZUNE_PL_FMT");
#endif
    printf("Honest failures: %d checks passed; no USB\n", checks);
    return 0;
}
