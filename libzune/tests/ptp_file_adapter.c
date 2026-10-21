/* For tests whose fake device implements only ptp_transaction: route the
 * upload engine's streamed sends (ptp_transaction_file) through it as one
 * buffer, and give weak defaults for the device helpers the engine calls
 * (a test's own definitions win). */
#include "../src/zune_internal.h"
#include <unistd.h>

int ptp_transaction_file(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams,
                         int fd, uint64_t send_len, uint16_t *response_code) {
    uint8_t *buf = malloc(send_len ? (size_t)send_len : 1);
    if (!buf) return -1;
    uint64_t done = 0;
    while (done < send_len) {
        ssize_t n = pread(fd, buf + done, (size_t)(send_len - done), (off_t)done);
        if (n <= 0) { free(buf); return -1; }
        done += (uint64_t)n;
    }
    int ret = ptp_transaction(s, opcode, params, nparams, PTP_DP_SENDDATA, buf, send_len,
                              NULL, NULL, response_code, NULL, NULL);
    free(buf);
    return ret;
}

__attribute__((weak)) int zune_is_aborted(ZuneDevice *dev) {
    return dev ? ZUNE_FLAG_GET(dev->cancel_requested) : 0;
}
__attribute__((weak)) int zune_unjam(ZuneDevice *dev) { (void)dev; return 0; }
__attribute__((weak)) const char *zune_autopsy_name(uint16_t code) { (void)code; return "fixture"; }
