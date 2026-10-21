/* Experimental private transport gate, independently implemented from our
 * albumsync capture: control frames97/99/101,105; CLIENT frame122;
 * CLIENTSERVER frame3087; LCP request3182 and Ack3203; close633/641/643.
 * XuneSyncLibrary c2a6eeb NetworkManager was consulted for sequence leads;
 * no implementation copied. No catalog, IP or artwork support is implied.
 * Frame/FCS encoding follows standard asynchronous PPP (RFC1662). */
#ifndef ZUNETOOL_HD_TUNNEL_PROBE_H
#define ZUNETOOL_HD_TUNNEL_PROBE_H
#include "zune_internal.h"
#include <signal.h>

#define HD_TUNNEL_FRAME_MAX 512
#define HD_TUNNEL_READ_MAX 4096
#define HD_TUNNEL_POLL_MAX 80
#define HD_TUNNEL_DEADLINE_MS 20000

typedef struct {
    uint8_t frame[HD_TUNNEL_FRAME_MAX];
    size_t length;
    int active, escaped, seen;
    uint8_t request[HD_TUNNEL_FRAME_MAX];
    size_t request_length;
} HdTunnelDecoder;

static uint16_t hd_tunnel_fcs(const uint8_t *p, size_t len) {
    uint16_t fcs = 0xffff;
    for (size_t i = 0; i < len; ++i) {
        fcs ^= p[i];
        for (int b = 0; b < 8; ++b)
            fcs = (fcs & 1) ? (uint16_t)((fcs >> 1) ^ 0x8408) : (uint16_t)(fcs >> 1);
    }
    return fcs;
}
static int hd_tunnel_lcp(HdTunnelDecoder *d) {
    const uint8_t *p = d->frame;
    size_t n = d->length;
    if (n < 10 || hd_tunnel_fcs(p, n) != 0xf0b8 ||
        p[0] != 0xff || p[1] != 3 || p[2] != 0xc0 || p[3] != 0x21 ||
        p[4] != 1 || (((size_t)p[6] << 8) | p[7]) != n-6) return -1;
    unsigned options = 0;
    for (size_t i = 8; i < n-2;) {
        if (n-2-i < 2 || p[i+1] < 2 || p[i+1] > n-2-i) return -1;
        unsigned flag;
        if (p[i] == 2 && p[i+1] == 6 && !p[i+2] && !p[i+3] && !p[i+4] && !p[i+5]) flag = 1;
        else if (p[i] == 7 && p[i+1] == 2) flag = 2;
        else if (p[i] == 8 && p[i+1] == 2) flag = 4;
        else return -1; /* This gate handles only capture-observed options. */
        if (options & flag) return -1;
        options |= flag; i += p[i+1];
    }
    if (options != 7) return -1;
    if (d->seen && (d->request_length != n-2 || memcmp(d->request, p, n-2))) return -1;
    memcpy(d->request, p, n-2); d->request_length = n-2; d->seen = 1;
    return 0;
}
/* Streaming decoder preserves split escape sequences and permits coalesced
 * identical requests. Unexpected frames or trailing partial data cannot pass. */
static int hd_tunnel_feed(HdTunnelDecoder *d, const uint8_t *p, size_t n) {
    if (!d || (!p && n) || n > HD_TUNNEL_READ_MAX) return -1;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c == 0x7e) {
            if (d->escaped) return -1;
            if (d->active && d->length && hd_tunnel_lcp(d)) return -1;
            d->length = 0; d->active = 1;
            continue;
        }
        if (!d->active) return -1;
        if (d->escaped) { c ^= 0x20; d->escaped = 0; }
        else if (c == 0x7d) { d->escaped = 1; continue; }
        if (d->length == sizeof(d->frame)) return -1;
        d->frame[d->length++] = c;
    }
    return 0;
}
static size_t hd_tunnel_ack(const HdTunnelDecoder *d, uint8_t *out, size_t capacity) {
    uint8_t plain[HD_TUNNEL_FRAME_MAX+2];
    if (!d || !d->seen || d->length || d->escaped || !out ||
        d->request_length > HD_TUNNEL_FRAME_MAX || capacity < 2) return 0;
    memcpy(plain, d->request, d->request_length); plain[4] = 2;
    uint16_t fcs = (uint16_t)~hd_tunnel_fcs(plain, d->request_length);
    plain[d->request_length] = (uint8_t)fcs;
    plain[d->request_length+1] = (uint8_t)(fcs >> 8);
    size_t pos = 0; out[pos++] = 0x7e;
    for (size_t i = 0; i < d->request_length+2; ++i) {
        uint8_t c = plain[i];
        int escape = c < 0x20 || c == 0x7e || c == 0x7d;
        if (pos + (escape ? 2u : 1u) + 1 > capacity) return 0;
        if (escape) { out[pos++] = 0x7d; c ^= 0x20; }
        out[pos++] = c;
    }
    out[pos++] = 0x7e; return pos;
}

#ifndef HD_TUNNEL_NOW_MS
static int64_t hd_tunnel_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}
#define HD_TUNNEL_NOW_MS hd_tunnel_now_ms
#endif
#ifndef HD_TUNNEL_PAUSE
static void hd_tunnel_pause(void) {
    struct timespec delay = {0, 250000000};
    nanosleep(&delay, NULL);
}
#define HD_TUNNEL_PAUSE hd_tunnel_pause
#endif

typedef struct {
    ZuneDevice *dev;
    FILE *out;
    volatile sig_atomic_t *stop;
    int64_t deadline;
    int wounded;
    uint16_t failed_response;
    const char *failure;
} HdTunnelProbe;
static int hd_tunnel_ready(HdTunnelProbe *p) {
    if ((p->stop && *p->stop) || ZUNE_FLAG_GET(p->dev->cancel_requested) || ZUNE_FLAG_GET(p->dev->disconnecting)) {
        p->failure = "cancelled"; return 0;
    }
    int64_t now = HD_TUNNEL_NOW_MS();
    if (now < 0 || now >= p->deadline) { p->failure = "deadline"; return 0; }
    return 1;
}
/* All accepted response shapes are from the capture. Stream3 queue size may
 * vary in send responses; accepted byte count must exactly match the send. */
static int hd_tunnel_transaction(HdTunnelProbe *p, uint16_t op, uint32_t *params,
        int count, uint16_t flags, uint8_t *send, size_t send_len,
        uint8_t **receive, uint32_t *receive_len, int cleanup) {
    uint16_t rc = 0; uint32_t reply[5] = {0}; int reply_count = 0;
    if (!cleanup && !hd_tunnel_ready(p)) return -1;
    int ret = ptp_transaction(&p->dev->ptp, op, params, count, flags, send, send_len,
                              receive, receive_len, &rc, reply, &reply_count);
    if (ret || rc != PTP_RC_OK) {
        p->failed_response = rc; p->failure = "transaction-failed";
        /* A non-OK complete response allows orderly close. A lost response
         * leaves framing unknown; never issue follow-up commands or retry. */
        if (!rc || rc == PTP_ERROR_IO || (ret && rc == PTP_RC_OK)) p->wounded = 1;
        return -1;
    }
    int valid = 0;
    if (op == 0x922b) valid = reply_count == 3 && reply[0] == 0 && reply[1] == 0 && reply[2] == 2;
    else if (op == 0x9230) valid = reply_count == 0;
    else if (op == 0x922d) valid = reply_count == 3 && reply[0] == 0 && reply[1] == 3 && reply[2] == 0;
    else if (op == 0x922c) valid = reply_count == 4 && reply[0] == 0 && reply[1] == 3 && reply[3] == send_len;
    if (!valid) { p->failed_response = rc; p->failure = "unexpected-response-parameters"; return -1; }
    return 0;
}

/* Returns success only after a validated LCP request (and optional ACK), plus
 * successful mode-end and stream-close. No IP configuration or host sockets.
 * The scheduling deadline does not preempt an in-progress PTP USB timeout. */
static int hd_tunnel_probe_report(ZuneDevice *dev, int acknowledge, FILE *out,
                                 volatile sig_atomic_t *stop) {
    if (!dev || !out || (acknowledge != 0 && acknowledge != 1)) return -1;
    HdTunnelProbe p = {dev, out, stop, 0, 0, 0, "not-started"};
    int opened = 0, passed = 0, cleanup_ok = 1;
    uint8_t *data = NULL; uint32_t length = 0;
    fprintf(out, "hd-tunnel-probe-v1\nscope\texperimental USB PPP LCP only; no catalog or artwork\n");
    if (dev->device_family != ZUNE_FAMILY_PAVO) { p.failure = "not-zune-hd"; goto done; }
    if (!dev->mtpz_authenticated) { p.failure = "not-authenticated"; goto done; }
    int64_t start = HD_TUNNEL_NOW_MS();
    if (start < 0) { p.failure = "clock-failed"; goto done; }
    p.deadline = start + HD_TUNNEL_DEADLINE_MS;
    if (!hd_tunnel_ready(&p)) goto done;
    /* Captured 260-byte open control. Interpretation of prefix is not proven;
     * reproduce the first HD capture's exact opaque control bytes. */
    uint8_t control[260] = {0x02, 0x1d, 0xc3, 0x00};
    uint32_t param[3] = {3, 1, 0};
    opened = 1; /* Even a lost open response can leave a live stream. */
    if (hd_tunnel_transaction(&p, 0x922b, param, 3, PTP_DP_SENDDATA, control, sizeof(control), NULL, NULL, 0)) goto done;
    param[0] = 1;
    if (hd_tunnel_transaction(&p, 0x9230, param, 1, PTP_DP_NODATA, NULL, 0, NULL, NULL, 0)) goto done;
    size_t client_bytes = 0; int replied = 0;
    HdTunnelDecoder decoder = {0};
    for (unsigned poll = 0; poll < HD_TUNNEL_POLL_MAX; ++poll) {
        param[0] = 3; param[1] = 3;
        if (hd_tunnel_transaction(&p, 0x922d, param, 2, PTP_DP_GETDATA_OPTIONAL,
                                  NULL, 0, &data, &length, 0)) goto done;
        if (!hd_tunnel_ready(&p)) goto done;
        if (length > HD_TUNNEL_READ_MAX || (length && !data)) { p.failure = "invalid-poll-data"; goto done; }
        if (!replied) {
            if (length > 6-client_bytes || (length && memcmp(data, &"CLIENT"[client_bytes], length))) {
                p.failure = "unexpected-client-signal"; goto done;
            }
            client_bytes += length;
            if (client_bytes == 6) {
                uint8_t response[] = "CLIENTSERVER";
                if (hd_tunnel_transaction(&p, 0x922c, param, 2, PTP_DP_SENDDATA,
                                          response, 12, NULL, NULL, 0)) goto done;
                replied = 1; fprintf(out, "client-handshake\tcomplete\n");
            }
        } else {
            if (hd_tunnel_feed(&decoder, data, length)) { p.failure = "invalid-or-unsupported-lcp"; goto done; }
            if (decoder.seen && !decoder.length && !decoder.escaped) {
                fprintf(out, "lcp-request\tvalid\nidentifier\t%u\noptions\tACCM-zero,PFC,ACFC\n", decoder.request[5]);
                if (acknowledge) {
                    uint8_t ack[2*HD_TUNNEL_FRAME_MAX+8];
                    size_t ack_len = hd_tunnel_ack(&decoder, ack, sizeof(ack));
                    if (!ack_len) { p.failure = "ack-encoding-failed"; goto done; }
                    if (hd_tunnel_transaction(&p, 0x922c, param, 2, PTP_DP_SENDDATA,
                                              ack, ack_len, NULL, NULL, 0)) goto done;
                    fprintf(out, "lcp-ack\tsent; full negotiation not attempted\n");
                } else fprintf(out, "lcp-ack\tnot-requested\n");
                if (!hd_tunnel_ready(&p)) goto done;
                passed = 1; break;
            }
        }
        free(data); data = NULL; length = 0;
        if (!hd_tunnel_ready(&p)) goto done;
        HD_TUNNEL_PAUSE();
    }
    if (!passed && hd_tunnel_ready(&p)) p.failure = "poll-limit-or-incomplete-frame";
done:
    free(data);
    const char *primary_failure = p.failure;
    uint16_t primary_response = p.failed_response;
    if (opened) {
        if (p.wounded || ZUNE_FLAG_GET(dev->disconnecting) || ZUNE_FLAG_GET(dev->cancel_requested)) {
            cleanup_ok = 0;
            fprintf(out, "cleanup\tnot-sent; transport or device cancellation prevents safe close\n");
        } else {
            uint32_t end = 2, close[3] = {3, 2, 0};
            if (hd_tunnel_transaction(&p, 0x9230, &end, 1, PTP_DP_NODATA, NULL, 0, NULL, NULL, 1)) cleanup_ok = 0;
            /* Never use a framing-uncertain pipe after a failed cleanup. */
            if (p.wounded || hd_tunnel_transaction(&p, 0x922b, close, 3, PTP_DP_SENDDATA,
                                                  NULL, 0, NULL, NULL, 1)) cleanup_ok = 0;
            fprintf(out, "cleanup\t%s\n", cleanup_ok ? "mode-ended-and-stream-closed" : "failed; device state uncertain");
            if (!cleanup_ok) fprintf(out, "cleanup-response\t0x%04X\n", p.failed_response);
        }
    } else fprintf(out, "cleanup\tnot-needed\n");
    if (passed && cleanup_ok) fprintf(out, "status\tcomplete\n");
    else fprintf(out, "status\t%s\nresponse\t0x%04X\n", passed ? "cleanup-failed" : primary_failure, primary_response);
    return fflush(out) || ferror(out) || !passed || !cleanup_ok ? -1 : 0;
}
#endif
