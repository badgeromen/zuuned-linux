/* Aborting a transfer partway must cancel the device's transaction:
 * PTP-over-USB Cancel Request with the transaction ID, Get Device Status until
 * OK (clearing listed stalled endpoints), then discard queued IN data, leaving
 * the pipe empty for the next command. Real ptp.c over a scripted backend;
 * usleep is replaced so device-status polling costs no wall time. No USB. */
#include "../src/ptp.h"
#include "../src/wiretap.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Virtual monotonic time: usleep and slow status requests advance it, so
 * polling costs no wall time. */
static long long virtual_ms;
static int slow_status;                /* each status request waits 1 s */
int usleep(useconds_t usec) { virtual_ms += usec / 1000; return 0; }
int __real_clock_gettime(clockid_t id, struct timespec *ts);
int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    if (id != CLOCK_MONOTONIC) return __real_clock_gettime(id, ts);
    ts->tv_sec = (time_t)(virtual_ms / 1000);
    ts->tv_nsec = (long)(virtual_ms % 1000) * 1000000L;
    return 0;
}

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

static int abort_flag, abort_after_writes, writes, data_bytes_written;
static int cancels, status_polls, busy_polls, halts[4], halt_count;
static uint8_t cancel_payload[6];
static uint8_t stalled_ep;
static int pending_in;                 /* queued IN transfers left to drain */
static uint32_t recv_total, recv_sent; /* scripted DATA container (receive test) */
static uint16_t recv_opcode;
static uint32_t recv_tid;

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

static int fake_write(zune_usb_handle_t *h, uint8_t ep, const uint8_t *data, uint32_t n,
                      uint32_t *actual, uint32_t timeout) {
    (void)h; (void)ep; (void)data; (void)timeout;
    ++writes;
    if (writes > 2) data_bytes_written += (int)n;      /* command, data header, then payload */
    if (abort_after_writes && writes == abort_after_writes) ZUNE_FLAG_SET(abort_flag, 1);
    *actual = n;
    return 0;
}

static int fake_read(zune_usb_handle_t *h, uint8_t ep, uint8_t *data, uint32_t max,
                     uint32_t *actual, uint32_t timeout) {
    (void)h; (void)ep; (void)timeout;
    *actual = 0;
    if (recv_total && recv_sent < recv_total) {        /* the DATA container, in 64 KiB reads */
        uint32_t n = recv_total - recv_sent < max ? recv_total - recv_sent : max;
        if (n > 65536) n = 65536;
        memset(data, 0x5a, n);
        if (recv_sent == 0) {
            put32(data, recv_total); put16(data + 4, 2); put16(data + 6, recv_opcode); put32(data + 8, recv_tid);
        }
        recv_sent += n;
        *actual = n;
        if (recv_sent >= 65536 * 2 && !ZUNE_FLAG_GET(abort_flag)) ZUNE_FLAG_SET(abort_flag, 1);
        return 0;
    }
    if (pending_in > 0) { --pending_in; memset(data, 0x33, max < 512 ? max : 512); *actual = max < 512 ? max : 512; return 0; }
    return -1;                                         /* timeout: nothing queued */
}

static int fake_clear_halt(zune_usb_handle_t *h, uint8_t ep) {
    (void)h;
    if (halt_count < 4) halts[halt_count++] = ep;
    return 0;
}

static int fake_class(zune_usb_handle_t *h, uint8_t type, uint8_t request, uint8_t *data,
                      uint16_t length, uint32_t timeout) {
    (void)h; (void)timeout;
    if (type == 0x21 && request == 0x64 && length == 6) {
        ++cancels;
        memcpy(cancel_payload, data, 6);
        return 6;
    }
    if (type == 0xA1 && request == 0x67 && length >= 8) {
        ++status_polls;
        if (slow_status) virtual_ms += 1000;
        memset(data, 0, length);
        if (status_polls <= busy_polls) { put16(data, 4); put16(data + 2, 0x2019); return 4; }
        if (stalled_ep) { put16(data, 8); put16(data + 2, 0x2001); data[4] = stalled_ep; return 8; }
        put16(data, 4); put16(data + 2, 0x2001);
        return 4;
    }
    return -1;
}

static zune_usb_backend_t backend = { .bulk_read = fake_read, .bulk_write = fake_write,
                                      .clear_halt = fake_clear_halt, .class_request = fake_class };

static void reset(void) {
    abort_flag = 0; abort_after_writes = 0; writes = 0; data_bytes_written = 0;
    cancels = status_polls = busy_polls = halt_count = 0; stalled_ep = 0; pending_in = 0;
    recv_total = recv_sent = 0;
    memset(cancel_payload, 0, sizeof(cancel_payload));
}

static ptp_session_t session(zune_usb_handle_t *usb) {
    ptp_session_t s;
    ptp_session_init(&s, usb);
    s.split_header_data = 1;
    s.transaction_id = 40;
    s.abort_flag = &abort_flag;
    return s;
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    zune_usb_handle_t usb = { .backend = &backend, .bulk_in_ep = 0x81, .bulk_out_ep = 0x02,
                              .bulk_in_maxpacket = 512, .bulk_out_maxpacket = 512 };
    static uint8_t payload[4 * 1048576];
    uint16_t rc = 0;

    reset();
    ptp_session_t s = session(&usb);
    abort_after_writes = 3;            /* command, data header, first chunk; then abort */
    busy_polls = 2; stalled_ep = 0x02; pending_in = 3;
    int ret = ptp_transaction(&s, 0x100D, NULL, 0, PTP_DP_SENDDATA, payload, sizeof(payload),
                              NULL, NULL, &rc, NULL, NULL);
    check(ret == -1 && data_bytes_written < (int)sizeof(payload), "an aborted upload stops sending payload");
    check(cancels == 1 && cancel_payload[0] == 0x01 && cancel_payload[1] == 0x40 &&
          cancel_payload[2] == 41 && cancel_payload[3] == 0 && cancel_payload[4] == 0 && cancel_payload[5] == 0,
          "Cancel Request carries code 0x4001 and the aborted transaction's ID");
    check(status_polls == 3, "Get Device Status is polled through busy until OK");
    check(halt_count == 1 && halts[0] == 0x02, "an endpoint the device reports stalled is cleared");
    check(pending_in == 0, "data the device had queued is discarded, leaving the pipe empty");
    check(s.last_response == PTP_RC_TransactionCancelled, "the aborted transaction reports TransactionCancelled");

    reset();
    s = session(&usb);
    recv_total = 12 + 4 * 1048576; recv_opcode = 0x1009; recv_tid = 41;
    ret = ptp_transaction(&s, 0x1009, NULL, 0, PTP_DP_GETDATA, NULL, 0, NULL, NULL, &rc, NULL, NULL);
    check(ret == -1 && cancels == 1 && status_polls >= 1, "an aborted download cancels the transaction too");
    check(s.last_response == PTP_RC_TransactionCancelled, "and reports TransactionCancelled");

    reset();
    s = session(&usb);
    backend.class_request = NULL;      /* a platform without class requests */
    abort_after_writes = 3;
    ret = ptp_transaction(&s, 0x100D, NULL, 0, PTP_DP_SENDDATA, payload, sizeof(payload),
                          NULL, NULL, &rc, NULL, NULL);
    check(ret == -1 && s.last_response == PTP_ERROR_IO, "without class requests the abort fails as a transport error");
    backend.class_request = fake_class;

    reset();
    s = session(&usb);
    abort_after_writes = 3;
    busy_polls = 1000;                 /* never becomes ready */
    ret = ptp_transaction(&s, 0x100D, NULL, 0, PTP_DP_SENDDATA, payload, sizeof(payload),
                          NULL, NULL, &rc, NULL, NULL);
    check(ret == -1 && status_polls == 100 && s.last_response == PTP_ERROR_IO,
          "a device that stays busy is given up on after a bounded number of polls");

    reset();
    s = session(&usb);
    abort_after_writes = 3;
    busy_polls = 1000;
    slow_status = 1;                   /* every status request takes 1 s */
    long long started = virtual_ms;
    ret = ptp_transaction(&s, 0x100D, NULL, 0, PTP_DP_SENDDATA, payload, sizeof(payload),
                          NULL, NULL, &rc, NULL, NULL);
    check(ret == -1 && status_polls <= 10 && virtual_ms - started <= 11500,
          "status requests that each take 1 s are given up on after about 10 s, not 100 polls");
    slow_status = 0;

    printf("PTP cancel: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
