/* Photo upload correctness against the real photo/MTP/tool code, with
 * ptp_transaction replaced by a small in-memory device holding folders.
 * A file that is neither convertible nor already JPEG must fail before any
 * device traffic; an album that already exists on the device must be reused
 * in a fresh session; an unreadable folder list must never create a folder. */
#include "../src/zune_internal.h"
#include <assert.h>
#include <dirent.h>
#include <stdarg.h>

static int checks;
static char last_error[1024];

void zune_set_error(const char *fmt, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    memcpy(last_error, text, sizeof(last_error));
}
void zune_clear_error(void) { last_error[0] = 0; }
const char *zune_get_error(void) { return last_error[0] ? last_error : NULL; }
const char *zune_autopsy_name(uint16_t code) { (void)code; return "fixture"; }
int zune_is_aborted(ZuneDevice *dev) { return dev->cancel_requested; }
int zune_unjam(ZuneDevice *dev) { (void)dev; return 0; }

/* ---- In-memory device ---- */

typedef struct { uint32_t handle; char name[64]; } Folder;
/* Two devices: table 1 belongs to the session in `second`. */
static Folder device_folders[2][16];
static int device_folder_count[2];
static ptp_session_t *second;
static int table;                       /* device of the current transaction */
#define folders (device_folders[table])
#define folder_count (device_folder_count[table])
static int transactions, handle_queries, folders_created, photos_sent;
static int fail_handles;                /* GetObjectHandles fails */
static uint32_t fail_info_for;          /* GetObjectInfo fails for this handle */
static uint16_t refuse_code;            /* a photo's SendObjectInfo fails with this */
static int photo_creates;
static uint32_t last_photo_parent;
static uint8_t last_photo[64];
static uint32_t last_photo_len;
static int pending_photo;

static void add_folder(uint32_t handle, const char *name) {
    assert(folder_count < 16);
    folders[folder_count].handle = handle;
    snprintf(folders[folder_count].name, sizeof(folders[folder_count].name), "%s", name);
    folder_count++;
}

static uint8_t *object_info_for(const Folder *f, uint32_t *len) {
    uint32_t wire_len = 0;
    uint8_t *wire = mtp_string_to_ucs2(f->name, &wire_len);
    assert(wire);
    *len = 52 + wire_len + 3;
    uint8_t *data = calloc(1, *len);
    assert(data);
    data[0] = 1;                                  /* storage 0x00010001 */
    data[2] = 1;
    data[4] = (uint8_t)MTP_OFC_Association;
    data[5] = (uint8_t)(MTP_OFC_Association >> 8);
    data[42] = 1;                                 /* GenericFolder */
    memcpy(data + 52, wire, wire_len);
    free(wire);
    return data;
}

int ptp_transaction(ptp_session_t *s, uint16_t opcode,
                    uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)nparams; (void)flags;
    table = s == second;
    transactions++;
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    int ok = 1;
    switch (opcode) {
    case PTP_OC_GetObjectHandles: {
        handle_queries++;
        assert(params[1] == MTP_OFC_Association);
        if (fail_handles) { ok = 0; break; }
        uint32_t len = 4 + 4 * (uint32_t)folder_count;
        uint8_t *data = calloc(1, len);
        assert(data);
        data[0] = (uint8_t)folder_count;
        for (int i = 0; i < folder_count; i++) {
            uint32_t h = folders[i].handle;
            data[4 + 4 * i] = (uint8_t)h; data[5 + 4 * i] = (uint8_t)(h >> 8);
            data[6 + 4 * i] = (uint8_t)(h >> 16); data[7 + 4 * i] = (uint8_t)(h >> 24);
        }
        *recv_data = data;
        *recv_len = len;
        break;
    }
    case PTP_OC_GetObjectInfo:
        ok = 0;
        for (int i = 0; i < folder_count; i++)
            if (folders[i].handle == params[0] && params[0] != fail_info_for) {
                *recv_data = object_info_for(&folders[i], recv_len);
                ok = 1;
            }
        break;
    case PTP_OC_SendObjectInfo: {
        uint16_t format = (uint16_t)(send_data[4] | (send_data[5] << 8));
        uint32_t handle;
        if (format == MTP_OFC_Association) {
            char *name = mtp_ucs2_to_string(send_data + 52, (uint32_t)send_len - 52);
            assert(name);
            handle = (table ? 3000 : 1000) + (uint32_t)folders_created++;
            add_folder(handle, name);
            free(name);
        } else {
            assert(format == MTP_OFC_JPEG);
            photo_creates++;
            if (refuse_code) { ok = 0; break; }
            handle = 2000;
            last_photo_parent = params[1];
            pending_photo = 1;
        }
        resp_params[0] = params[0];
        resp_params[1] = params[1];
        resp_params[2] = handle;
        *resp_nparams = 3;
        break;
    }
    case PTP_OC_SendObject:
        assert(pending_photo);
        pending_photo = 0;
        photos_sent++;
        last_photo_len = (uint32_t)(send_len < sizeof(last_photo) ? send_len : sizeof(last_photo));
        memcpy(last_photo, send_data, last_photo_len);
        break;
    case PTP_OC_CloseSession:
        break;
    default:
        fprintf(stderr, "unexpected opcode 0x%04x\n", opcode);
        abort();
    }
    s->last_response = ok ? PTP_RC_OK
        : opcode == PTP_OC_SendObjectInfo && refuse_code ? refuse_code : 0x2002;
    if (response_code) *response_code = s->last_response;
    return ok ? 0 : -1;
}

/* ---- Fixtures ---- */

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s (error: %s)\n", name, last_error); exit(1); }
    printf("PASS: %s\n", name);
    checks++;
}

static void write_file(const char *path, const void *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    assert(fp && fwrite(data, 1, len, fp) == len);
    fclose(fp);
}

static int temp_files(void) {
    DIR *dir = opendir(getenv("TMPDIR"));
    assert(dir);
    int count = 0;
    for (struct dirent *entry; (entry = readdir(dir));)
        if (!strncmp(entry->d_name, "zunelinux_", 10)) count++;
    closedir(dir);
    return count;
}

static void reset_counters(void) {
    transactions = handle_queries = photos_sent = 0;
    last_photo_parent = UINT32_MAX;
    last_error[0] = '\0';
}

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *dir = argv[1], *tool_ok = argv[2], *tool_fail = argv[3];
    char png[512], jpg[512];
    snprintf(png, sizeof(png), "%s/picture.png", dir);
    snprintf(jpg, sizeof(jpg), "%s/picture.jpg", dir);
    static const uint8_t png_bytes[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0 };
    static const uint8_t jpg_bytes[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F', 0xFF, 0xD9 };
    write_file(png, png_bytes, sizeof(png_bytes));
    write_file(jpg, jpg_bytes, sizeof(jpg_bytes));

    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));
    dev.storage_id = 0x00010001;
    add_folder(500, "Vacation");
    char *saved_path = strdup(getenv("PATH"));

    /* Conversion failure: only a real JPEG may stand in for the result. */
    setenv("ZUNE_FFMPEG", tool_fail, 1);
    reset_counters();
    check(zune_smuggle_photo(&dev, png, "Vacation") == -1 && transactions == 0
          && strstr(last_error, "not a JPEG") && temp_files() == 0,
          "PNG whose conversion fails is refused before any device traffic");

    unsetenv("ZUNE_FFMPEG");
    setenv("PATH", dir, 1);          /* no ffmpeg here */
    reset_counters();
    check(zune_smuggle_photo(&dev, png, NULL) == -1 && transactions == 0
          && strstr(last_error, "not a JPEG") && strstr(last_error, "ffmpeg not found"),
          "PNG with ffmpeg missing fails and keeps the tool's reason");
    setenv("PATH", saved_path, 1);

    setenv("ZUNE_FFMPEG", tool_fail, 1);
    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, NULL) == 0 && photos_sent == 1
          && last_photo_len == sizeof(jpg_bytes) && !memcmp(last_photo, jpg_bytes, sizeof(jpg_bytes))
          && !last_error[0],
          "JPEG whose conversion fails is sent unchanged, leaving no error behind");

    /* Fresh session: the album already exists on the device. */
    setenv("ZUNE_FFMPEG", tool_ok, 1);
    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, "Vacation") == 0 && folders_created == 0
          && handle_queries == 1 && last_photo_parent == 500,
          "existing device album is found and reused in a fresh session");
    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, "Vacation") == 0 && handle_queries == 0
          && last_photo_parent == 500,
          "a second photo to that album uses the cache without another query");

    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, "Trip") == 0 && folders_created == 1
          && handle_queries == 1 && last_photo_parent == 1000,
          "a missing album is created once after the device lookup");
    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, "Trip") == 0 && folders_created == 1
          && handle_queries == 0 && last_photo_parent == 1000,
          "two photos share one new album folder");

    /* Unknown inventory never creates. */
    fail_handles = 1;
    reset_counters();
    int created = folders_created;
    check(zune_smuggle_photo(&dev, jpg, "Unread") == -1 && folders_created == created
          && photos_sent == 0 && strstr(last_error, "Could not read"),
          "an unreadable folder list fails without creating a folder");
    fail_handles = 0;
    fail_info_for = 500;
    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, "Partial") == -1 && folders_created == created
          && photos_sent == 0,
          "a folder that cannot be read also blocks creation");
    fail_info_for = 0;
    check(temp_files() == 0, "no prepared temporary file outlives a send");

    /* T3.4: albums are cached per device. A second Zune in the same process
     * has no "Trip" album, so it must look on its own device and create one,
     * never reuse the first Zune's folder 1000. */
    ZuneDevice other;
    memset(&other, 0, sizeof(other));
    other.storage_id = 0x00010001;
    second = &other.ptp;
    reset_counters();
    int before = folders_created;
    check(zune_smuggle_photo(&other, jpg, "Trip") == 0 && handle_queries == 1
          && folders_created == before + 1 && last_photo_parent == 3000 + (uint32_t)before,
          "a second device looks up and creates its own album, not another device's folder");
    reset_counters();
    check(zune_smuggle_photo(&dev, jpg, "Trip") == 0 && handle_queries == 0 && last_photo_parent == 1000,
          "the first device keeps its own cached album");
    zune_free_folder_cache(&dev);
    zune_free_folder_cache(&other);
    check(dev.folder_cache == NULL && other.folder_cache == NULL,
          "each device's cache is freed with it (zune_sever)");

    /* A full device refuses the converted photo: one attempt, the real code,
     * and an error naming the caller's file rather than the prepared copy. */
    setenv("ZUNE_FFMPEG", tool_ok, 1);
    second = NULL;
    refuse_code = 0x200C;
    photo_creates = 0;
    reset_counters();
    check(zune_smuggle_photo(&dev, png, NULL) == -1 && photo_creates == 1 && photos_sent == 0
          && dev.ptp.last_response == 0x200C && strstr(last_error, "0x200C")
          && strstr(last_error, png) && !strstr(last_error, "zunelinux_") && temp_files() == 0,
          "a refused converted photo is tried once and its error names the original file");
    refuse_code = 0;

    free(saved_path);
    printf("Photo upload: %d checks passed; no USB\n", checks);
    return 0;
}
