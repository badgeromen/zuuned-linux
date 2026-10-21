/* T3.2: uploads stream from the file. Real mtp.c/ptp.c over a USB
 * backend that speaks enough PTP for SendObjectInfo and SendObject: a 5 MiB
 * file must arrive byte for byte, in split and combined data framing, while
 * no allocation comes close to the file's size (the previous code allocated
 * the whole file). malloc is wrapped to record the largest request. */
#include "../src/zune_internal.h"
#include <stdarg.h>

static int checks, failures;

static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

void *__real_malloc(size_t n);
static size_t largest;
void *__wrap_malloc(size_t n) {
    if (n > largest) largest = n;
    return __real_malloc(n);
}

#define FILE_SIZE (5u * 1024u * 1024u + 333u)
static uint8_t *expected, *received;
static uint32_t received_len;

/* ---- A device at the bulk-pipe level ---- */
enum { COMMAND, DATA, };
static int state = COMMAND;
static uint16_t opcode;
static uint32_t tid, data_left, data_seen;
static uint8_t response[32];
static uint32_t response_len;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static void respond(int nparams, uint32_t handle) {
    response_len = 12 + 4u * (uint32_t)nparams;
    wr32(response, response_len);
    response[4] = 3; response[5] = 0;                 /* response container */
    response[6] = 0x01; response[7] = 0x20;           /* OK */
    wr32(response + 8, tid);
    if (nparams) { wr32(response + 12, 1); wr32(response + 16, 0); wr32(response + 20, handle); }
}

static void payload(const uint8_t *p, uint32_t n) {
    if (opcode == PTP_OC_SendObject && received_len + n <= FILE_SIZE) memcpy(received + received_len, p, n);
    if (opcode == PTP_OC_SendObject) received_len += n;
    data_left -= n;
    if (!data_left) {
        state = COMMAND;
        respond(opcode == PTP_OC_SendObjectInfo ? 3 : 0, 0x01000042);
    }
}

static int fake_write(zune_usb_handle_t *h, uint8_t ep, const uint8_t *p, uint32_t n, uint32_t *actual,
                      uint32_t timeout) {
    (void)h; (void)ep; (void)timeout;
    *actual = n;
    if (n == 0) return 0;                             /* zero-length packet */
    if (state == COMMAND && rd16(p + 4) == 1) {       /* command container */
        opcode = rd16(p + 6);
        tid = rd32(p + 8);
        if (opcode == PTP_OC_SendObjectInfo || opcode == PTP_OC_SendObject) state = DATA, data_seen = 0;
        else respond(0, 0);
        return 0;
    }
    if (state == DATA && !data_seen) {                /* data container header, maybe with payload */
        data_seen = 1;
        data_left = rd32(p) - 12;
        if (n > 12) payload(p + 12, n - 12);
        else if (!data_left) { state = COMMAND; respond(opcode == PTP_OC_SendObjectInfo ? 3 : 0, 0x01000042); }
        return 0;
    }
    if (state == DATA) { payload(p, n); return 0; }
    return -1;
}

static int fake_read(zune_usb_handle_t *h, uint8_t ep, uint8_t *p, uint32_t max, uint32_t *actual,
                     uint32_t timeout) {
    (void)h; (void)ep; (void)timeout;
    *actual = 0;
    if (!response_len || max < response_len) return -1;
    memcpy(p, response, response_len);
    *actual = response_len;
    response_len = 0;
    return 0;
}

static int fake_halt(zune_usb_handle_t *h, uint8_t ep) { (void)h; (void)ep; return 0; }

static zune_usb_backend_t backend = { .bulk_read = fake_read, .bulk_write = fake_write, .clear_halt = fake_halt };

/* Create the object, then send the file: 0 on success. */
static int upload(int split, const char *path, uint32_t *handle) {
    static ZuneDevice dev;
    static zune_usb_handle_t usb = { .backend = &backend, .bulk_in_ep = 0x81, .bulk_out_ep = 0x02,
                                     .bulk_in_maxpacket = 512, .bulk_out_maxpacket = 512 };
    memset(&dev, 0, sizeof(dev));
    ptp_session_init(&dev.ptp, &usb);
    dev.ptp.split_header_data = split;
    dev.storage_id = 1;
    received_len = 0; state = COMMAND; response_len = 0; largest = 0;
    mtp_object_info_t info;
    memset(&info, 0, sizeof(info));
    info.storage_id = 1; info.object_format = MTP_OFC_MP3;
    info.object_compressed_size = FILE_SIZE; info.filename = "stream.mp3";
    if (mtp_send_object_info(&dev.ptp, 1, 0, &info, handle) != 0) return -1;
    return mtp_send_object_from_file(&dev.ptp, path, FILE_SIZE);
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    expected = __real_malloc(FILE_SIZE);
    received = __real_malloc(FILE_SIZE);
    uint32_t x = 12345;
    for (uint32_t i = 0; i < FILE_SIZE; i++) { x = x * 1103515245u + 12345u; expected[i] = (uint8_t)(x >> 16); }
    char path[64];
    snprintf(path, sizeof(path), "/tmp/libzune-upload-stream-%d", (int)getpid());
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(expected, 1, FILE_SIZE, f) != FILE_SIZE) return 2;
    fclose(f);

    for (int split = 1; split >= 0; split--) {
        uint32_t handle = 0;
        int status = upload(split, path, &handle);
        char label[160];
        snprintf(label, sizeof(label), "%s framing: a %u-byte file arrives byte for byte",
                 split ? "split" : "combined", FILE_SIZE);
        check(status == 0 && handle == 0x01000042 && received_len == FILE_SIZE &&
              !memcmp(received, expected, FILE_SIZE), label);
        snprintf(label, sizeof(label), "%s framing: largest allocation %zu bytes, well under the file",
                 split ? "split" : "combined", largest);
        check(largest <= 1048576 + 4096, label);
    }
    unlink(path);
    free(expected);
    free(received);
    printf("Upload streaming: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
