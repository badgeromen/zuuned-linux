/*
 * ptp.c — PTP (Picture Transfer Protocol) over USB implementation
 *
 * Clean, Zune-focused PTP transport layer. Implements the command/data/response
 * transaction model over USB bulk endpoints using the container format defined
 * in the PTP/MTP specification.
 *
 * All multi-byte values are little-endian on the wire. Container header is
 * always 12 bytes: uint32 length, uint16 type, uint16 code, uint32 trans_id.
 * Command containers carry up to 5 uint32 parameters. Data containers carry
 * variable-length payloads. Response containers carry a status code and up
 * to 5 uint32 parameters.
 *
 * Part of the native PTP/MTP stack that replaces vendored libmtp.
 *
 * Copyright (c) 2026 BadgerOmens
 */

#include "ptp.h"
#include "bytes.h"
#include "wiretap.h"

#include <time.h>   /* clock_gettime — wire-throughput telemetry */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

/* ===================================================================
 * Constants
 * =================================================================== */

/* Block size for chunked data transfers.
 * 256KB → 1MB (2026-08: fewer submit round-trips per file; multiple of
 * the 512-byte bulk maxpacket, so the WMP alignment logic below is
 * unaffected). Gated by a zunetool torture run on real Keel hardware —
 * revert to 262144 if a device regresses. */
#define CONTEXT_BLOCK_SIZE  1048576

/* USB timeouts in milliseconds.
 * Command/response: 10s (small data, should be fast)
 * Data transfer: 60s (large files through USB 2.0 need time) */
#define PTP_USB_TIMEOUT_MS       10000
#define PTP_USB_DATA_TIMEOUT_MS  60000

/* PTP over USB always uses little-endian byte order; the packing helpers
 * (zune_le16, zune_put_le32, ...) come from bytes.h. */


/* ===================================================================
 * USB I/O Helpers
 *
 * Thin wrappers around the USB backend that add error logging.
 * =================================================================== */

/*
 * ptp_usb_write — Send data on the bulk OUT endpoint.
 *
 * Returns 0 on success, -1 on failure.
 */
static int ptp_usb_write_timeout(ptp_session_t *s, const uint8_t *data,
                                  uint32_t len, uint32_t timeout_ms)
{
    uint32_t actual = 0;
    int ret = s->usb->backend->bulk_write(
        s->usb, s->usb->bulk_out_ep, data, len, &actual, timeout_ms);

    if (ret != ZUNE_USB_OK) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "bulk_write failed: requested %u bytes", len);
        return -1;
    }
    if (actual != len) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "bulk_write short: wrote %u of %u bytes",
                actual, len);
        return -1;
    }
    return 0;
}

static int ptp_usb_write(ptp_session_t *s, const uint8_t *data, uint32_t len)
{
    return ptp_usb_write_timeout(s, data, len, PTP_USB_TIMEOUT_MS);
}

/*
 * ptp_usb_read — Receive data from the bulk IN endpoint.
 *
 * Reads up to `len` bytes. Sets *actual_out to the number of bytes
 * actually received.
 *
 * Returns 0 on success, -1 on failure.
 */
static int ptp_usb_read_timeout(ptp_session_t *s, uint8_t *data, uint32_t len,
                                 uint32_t *actual_out, uint32_t timeout_ms)
{
    uint32_t actual = 0;
    int ret = s->usb->backend->bulk_read(
        s->usb, s->usb->bulk_in_ep, data, len, &actual, timeout_ms);

    if (ret != ZUNE_USB_OK) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "bulk_read failed: requested %u bytes (timeout %u ms)",
                len, timeout_ms);
        if (actual_out) *actual_out = 0;
        return -1;
    }
    if (actual_out) *actual_out = actual;
    return 0;
}

static int ptp_usb_read(ptp_session_t *s, uint8_t *data, uint32_t len,
                         uint32_t *actual_out)
{
    return ptp_usb_read_timeout(s, data, len, actual_out, PTP_USB_TIMEOUT_MS);
}

/* ---- Transaction cancellation ---- */

/* The host stopped a data phase partway (zune_abort). The device still waits
 * for, or still sends, the rest of the declared length, and would take the
 * next command as payload: the session wedges until a USB reset. Cancel the
 * transaction the PTP-over-USB way: Cancel Request (0x64; code 0x4001 and the
 * transaction ID), then Get Device Status (0x67) until the device is ready,
 * clearing any stalled endpoints it lists, then discard data it had already
 * queued. A Zune HD reports busy for about 0.4 s, then OK, and drops the
 * partly sent object. Records TransactionCancelled on success. */
static int ptp_cancel_transaction(ptp_session_t *s)
{
    const zune_usb_backend_t *be = s->usb->backend;
    s->last_response = PTP_ERROR_IO;
    if (!be->class_request) {
        ZLOG(ZUNE_CHATTER_SHOUT, "ptp", "cannot cancel transaction %u on this platform",
             s->transaction_id);
        return -1;
    }
    uint8_t cancel[6] = { 0x01, 0x40 };           /* CancellationCode 0x4001 */
    zune_put_le32(cancel + 2, s->transaction_id);
    if (be->class_request(s->usb, 0x21, 0x64, cancel, sizeof(cancel), 2000) < 0) {
        ZLOG(ZUNE_CHATTER_SHOUT, "ptp", "cancel request for transaction %u failed",
             s->transaction_id);
        return -1;
    }
    /* At most 100 polls 100 ms apart, and at most 10 s in all: a status
     * request can itself wait 1 s for an answer, so the count alone would
     * allow almost two minutes. */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int poll = 0; poll < 100; poll++) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000 >= 10000)
            break;
        uint8_t status[32] = { 0 };
        int n = be->class_request(s->usb, 0xA1, 0x67, status, sizeof(status), 1000);
        if (n >= 4) {
            uint32_t length = zune_le16(status);
            for (uint32_t k = 4; k + 4 <= length && k + 4 <= (uint32_t)n; k += 4)
                be->clear_halt(s->usb, status[k]);
            if (zune_le16(status + 2) == PTP_RC_OK) {
                uint8_t sink[16384];
                uint32_t got = 0;
                for (int reads = 0; reads < 256; reads++)
                    if (be->bulk_read(s->usb, s->usb->bulk_in_ep, sink, sizeof(sink), &got, 200) != 0)
                        break;
                s->last_response = PTP_RC_TransactionCancelled;
                ZLOG(ZUNE_CHATTER_TALK, "ptp", "transaction %u cancelled", s->transaction_id);
                return 0;
            }
        }
        usleep(100000);
    }
    ZLOG(ZUNE_CHATTER_SHOUT, "ptp", "device still busy after cancelling transaction %u",
         s->transaction_id);
    return -1;
}

/* ===================================================================
 * Command Phase — Build and send a command container
 * =================================================================== */

/*
 * ptp_send_command — Build and send a command container.
 *
 * Container layout (little-endian):
 *   [0..3]   uint32  length      = 12 + nparams*4
 *   [4..5]   uint16  type        = PTP_CT_COMMAND (1)
 *   [6..7]   uint16  code        = opcode
 *   [8..11]  uint32  trans_id
 *   [12..]   uint32  params[0..4]
 *
 * Returns 0 on success, -1 on failure.
 */
static int ptp_send_command(ptp_session_t *s, uint16_t opcode,
                            uint32_t *params, int nparams)
{
    uint32_t length;
    uint8_t buf[PTP_CONTAINER_HDR_SIZE + PTP_MAX_PARAMS * 4];

    if (nparams < 0) nparams = 0;
    if (nparams > PTP_MAX_PARAMS) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "too many params: %d (max %d)",
                nparams, PTP_MAX_PARAMS);
        return -1;
    }

    length = PTP_CONTAINER_HDR_SIZE + (uint32_t)(nparams * 4);

    /* Pack header */
    zune_put_le32(buf + 0, length);
    zune_put_le16(buf + 4, PTP_CT_COMMAND);
    zune_put_le16(buf + 6, opcode);
    zune_put_le32(buf + 8, s->transaction_id);

    /* Pack parameters */
    for (int i = 0; i < nparams; i++) {
        zune_put_le32(buf + PTP_CONTAINER_HDR_SIZE + i * 4,
                  params ? params[i] : 0);
    }

    /* Debug: dump command container — disabled (very noisy). Re-enable for protocol debugging. */
#if 0
    {
        char hex[2 * sizeof(buf) + 1];
        for (uint32_t i = 0; i < length; i++) snprintf(hex + 2 * i, 3, "%02x", buf[i]);
        ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "CMD: %s (%u bytes)", hex, length);
    }
#endif

    return ptp_usb_write(s, buf, length);
}

/* ===================================================================
 * Data Phase — Send data to device
 * =================================================================== */

/* Payload of a send data phase: a buffer, or a file streamed chunk by chunk
 * so that a multi-GiB upload never needs a file-sized allocation. */
typedef struct {
    const uint8_t *data;    /* buffer source, or NULL for a file */
    int fd;                 /* file source when data is NULL (-1: none) */
    uint8_t *chunk;         /* CONTEXT_BLOCK_SIZE staging buffer for file reads */
} ptp_source_t;

/* Payload bytes [offset, offset + len), or NULL if they cannot be read. */
static const uint8_t *ptp_source_bytes(ptp_source_t *src, uint64_t offset, uint32_t len)
{
    if (src->data) return src->data + offset;
    if (src->fd < 0 || !src->chunk || len > CONTEXT_BLOCK_SIZE) return NULL;
    uint32_t done = 0;
    while (done < len) {
        ssize_t n = pread(src->fd, src->chunk + done, len - done, (off_t)(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "send_data: cannot read the payload at offset %llu (%s)",
                 (unsigned long long)(offset + done), n < 0 ? strerror(errno) : "file ended early");
            return NULL;
        }
        done += (uint32_t)n;
    }
    return src->chunk;
}

/*
 * ptp_send_data — Send a data container to the device.
 *
 * The first USB write includes the 12-byte data container header
 * followed by the beginning of the payload. Subsequent writes
 * continue the payload in CONTEXT_BLOCK_SIZE chunks.
 *
 * If the total transfer length (header + data) is a multiple of
 * the bulk OUT max packet size, a zero-length packet is sent to
 * signal end of transfer.
 *
 * Returns 0 on success, -1 on failure.
 */
static int ptp_send_data(ptp_session_t *s, uint16_t opcode,
                          ptp_source_t *src, uint64_t data_len)
{
    uint8_t *first_chunk;
    uint64_t total_container_len = PTP_CONTAINER_HDR_SIZE + data_len;
    uint32_t first_write_len;
    uint64_t data_offset;
    uint64_t bytes_remaining;

    /*
     * Split header/data mode (Zune/WMP compatibility):
     * Send the 12-byte data container header as a SEPARATE USB write
     * from the payload. The Zune firmware is sensitive to USB transfer
     * framing and rejects combined header+data writes.
     */
    if (s->split_header_data) {
        /* Send header only (12 bytes) */
        uint8_t hdr[PTP_CONTAINER_HDR_SIZE];
        zune_put_le32(hdr + 0, (uint32_t)total_container_len);
        zune_put_le16(hdr + 4, PTP_CT_DATA);
        zune_put_le16(hdr + 6, opcode);
        zune_put_le32(hdr + 8, s->transaction_id);

#if 0
        {
            char hex[2 * PTP_CONTAINER_HDR_SIZE + 1];
            for (int i = 0; i < PTP_CONTAINER_HDR_SIZE; i++) snprintf(hex + 2 * i, 3, "%02x", hdr[i]);
            ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "DATA hdr (split): %s (total %llu bytes)",
                 hex, (unsigned long long)total_container_len);
        }
#endif

        if (ptp_usb_write(s, hdr, PTP_CONTAINER_HDR_SIZE) < 0)
            return -1;

        /* Send payload data in chunks */
        uint64_t remaining = data_len;
        uint64_t offset = 0;
        uint16_t maxpkt = s->usb->bulk_out_maxpacket;

        /* Wire-throughput telemetry for large payloads (>8MB) — makes
         * chunk-size experiments measurable from any app log. */
        struct timespec xfer_t0;
        clock_gettime(CLOCK_MONOTONIC, &xfer_t0);

        while (remaining > 0) {
            if (s->abort_flag && ZUNE_FLAG_GET(*s->abort_flag)) {
                ZLOG(ZUNE_CHATTER_TALK, "ptp", "send_data: aborted at offset %llu of %llu",
                        (unsigned long long)offset, (unsigned long long)data_len);
                ptp_cancel_transaction(s);
                return -1;
            }
            uint32_t chunk = CONTEXT_BLOCK_SIZE;
            if (remaining < (uint64_t)chunk)
                chunk = (uint32_t)remaining;
            /* WMP alignment */
            if (maxpkt > 0 && chunk > maxpkt && chunk % maxpkt != 0)
                chunk -= chunk % maxpkt;

            const uint8_t *bytes = ptp_source_bytes(src, offset, chunk);
            /* A data phase the host cannot finish leaves the device waiting
             * for the rest: cancel it, as for an abort. */
            if (!bytes || ptp_usb_write_timeout(s, bytes, chunk, PTP_USB_DATA_TIMEOUT_MS) < 0) {
                ptp_cancel_transaction(s);
                return -1;
            }
            offset += chunk;
            remaining -= chunk;
        }

        if (data_len > 8u * 1024 * 1024) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double secs = (double)(t1.tv_sec - xfer_t0.tv_sec)
                + (double)(t1.tv_nsec - xfer_t0.tv_nsec) / 1e9;
            if (secs > 0.001)
                ZLOG(ZUNE_CHATTER_TALK, "ptp", "sent %llu MB in %.1fs (%.1f MB/s)",
                        (unsigned long long)(data_len / 1000000), secs,
                        (double)data_len / 1e6 / secs);
        }

        /* ZLP if the PAYLOAD transfer ends on a packet boundary. The 12-byte
         * header went out as its own transfer above, so the length that
         * matters here is data_len — NOT total_container_len. Wire-verified
         * against the Windows 8 client (docs/WIRE_CAPTURE_FINDINGS.md §1):
         * its one ZLP follows a 16,384-byte payload (16384 % 512 == 0),
         * which the old container-length test would have skipped.
         *
         * data_len == 0 (abstract-object SendObject: album/artist
         * creation) must NOT ZLP: the bare 12-byte header is already a
         * short packet terminating the transfer; a stray ZLP after it
         * desyncs the pipe and the NEXT transaction dies with 0x2002.
         * Hardware-observed: album forging failed on every sync until
         * this guard. */
        if (data_len > 0 && s->usb->bulk_out_maxpacket > 0 &&
            (data_len % s->usb->bulk_out_maxpacket) == 0) {
            uint32_t actual = 0;
            s->usb->backend->bulk_write(s->usb, s->usb->bulk_out_ep,
                                        NULL, 0, &actual, PTP_USB_TIMEOUT_MS);
        }
        return 0;
    }

    /*
     * Normal mode: header + data combined in first write.
     */
    first_write_len = CONTEXT_BLOCK_SIZE;
    if (total_container_len < (uint64_t)first_write_len)
        first_write_len = (uint32_t)total_container_len;

    /* WMP-compatible write alignment */
    uint16_t maxpacket = s->usb->bulk_out_maxpacket;
    if (maxpacket > 0 && first_write_len > maxpacket &&
        first_write_len % maxpacket != 0) {
        first_write_len -= first_write_len % maxpacket;
    }

    first_chunk = (uint8_t *)malloc(first_write_len);
    if (!first_chunk) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "malloc failed for data container");
        return -1;
    }

    /* Pack data container header */
    zune_put_le32(first_chunk + 0, (uint32_t)total_container_len);
    zune_put_le16(first_chunk + 4, PTP_CT_DATA);
    zune_put_le16(first_chunk + 6, opcode);
    zune_put_le32(first_chunk + 8, s->transaction_id);

    /* Copy as much data as fits in the first chunk */
    uint32_t first_data_len = first_write_len - PTP_CONTAINER_HDR_SIZE;
    if (first_data_len > 0) {
        const uint8_t *bytes = ptp_source_bytes(src, 0, first_data_len);
        if (!bytes) {
            free(first_chunk);
            return -1;
        }
        memcpy(first_chunk + PTP_CONTAINER_HDR_SIZE, bytes, first_data_len);
    }

    /* Debug: dump data container header — disabled (very noisy). */
#if 0
    {
        char hex[2 * PTP_CONTAINER_HDR_SIZE + 1] = "";
        for (uint32_t i = 0; i < (PTP_CONTAINER_HDR_SIZE < first_write_len ? PTP_CONTAINER_HDR_SIZE : first_write_len); i++)
            snprintf(hex + 2 * i, 3, "%02x", first_chunk[i]);
        ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "DATA hdr: %s (total %llu bytes)",
             hex, (unsigned long long)total_container_len);
    }
#endif

    /* Send first chunk */
    if (ptp_usb_write(s, first_chunk, first_write_len) < 0) {
        free(first_chunk);
        return -1;
    }
    free(first_chunk);

    /* Send remaining data in CONTEXT_BLOCK_SIZE chunks */
    data_offset = first_data_len;
    bytes_remaining = data_len - first_data_len;

    while (bytes_remaining > 0) {
        if (s->abort_flag && ZUNE_FLAG_GET(*s->abort_flag)) {
            ZLOG(ZUNE_CHATTER_TALK, "ptp", "send_data: aborted at offset %llu of %llu",
                    (unsigned long long)data_offset, (unsigned long long)data_len);
            ptp_cancel_transaction(s);
            return -1;
        }
        uint32_t chunk_len = CONTEXT_BLOCK_SIZE;
        if (bytes_remaining < (uint64_t)chunk_len) {
            chunk_len = (uint32_t)bytes_remaining;
            /* WMP-compatible: align to max packet boundary */
            if (maxpacket > 0 && chunk_len > maxpacket &&
                chunk_len % maxpacket != 0) {
                chunk_len -= chunk_len % maxpacket;
            }
        }

        const uint8_t *bytes = ptp_source_bytes(src, data_offset, chunk_len);
        if (!bytes || ptp_usb_write_timeout(s, bytes, chunk_len, PTP_USB_DATA_TIMEOUT_MS) < 0) {
            ptp_cancel_transaction(s);
            return -1;
        }

        data_offset += chunk_len;
        bytes_remaining -= chunk_len;
    }

    /*
     * If the total transfer length is a multiple of the max packet size,
     * send a zero-length packet to signal end of transfer.
     */
    if (s->usb->bulk_out_maxpacket > 0 &&
        (total_container_len % s->usb->bulk_out_maxpacket) == 0) {
        uint32_t actual = 0;
        s->usb->backend->bulk_write(s->usb, s->usb->bulk_out_ep,
                                    NULL, 0, &actual, PTP_USB_TIMEOUT_MS);
    }

    return 0;
}

/* ===================================================================
 * Data Phase — Receive data from device
 * =================================================================== */

/*
 * ptp_recv_data — Receive a data container from the device.
 *
 * Reads the first packet to get the container header, then reads
 * the remaining payload in chunks. Allocates the receive buffer.
 *
 * On success, *out_data points to the data payload (caller frees)
 * and *out_len is the payload length (excluding the 12-byte header).
 *
 * Returns 0 on success, -1 on failure.
 */
static int ptp_recv_data(ptp_session_t *s, uint8_t **out_data,
                          uint32_t *out_len, int optional, uint16_t opcode,
                          uint32_t *resp_params, int *resp_nparams)
{
    uint8_t *first_buf = NULL;
    uint32_t first_actual = 0;
    uint32_t container_len;
    uint16_t container_type;
    uint32_t data_total;
    uint8_t *payload;
    uint32_t payload_in_first;
    uint32_t payload_received;

    if (out_data) *out_data = NULL;
    if (out_len) *out_len = 0;

    first_buf = (uint8_t *)malloc(CONTEXT_BLOCK_SIZE);
    if (!first_buf) return -1;

    /* Read first packet from bulk IN */
    if (ptp_usb_read(s, first_buf, CONTEXT_BLOCK_SIZE, &first_actual) < 0) {
        free(first_buf);
        return -1;
    }

    if (first_actual < PTP_CONTAINER_HDR_SIZE) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "recv_data: short read (%u bytes, need %d)",
                first_actual, PTP_CONTAINER_HDR_SIZE);
        free(first_buf);
        return -1;
    }

    /* Parse data container header */
    container_len  = zune_le32(first_buf + 0);
    container_type = zune_le16(first_buf + 4);

    /* Auto-detect split header/data mode (Zune/WMP compatibility).
     * If the device sends the 12-byte header in a separate USB transfer
     * from the payload, we must mirror this behavior when sending data. */
    if (!optional && first_actual == PTP_CONTAINER_HDR_SIZE && container_len > PTP_CONTAINER_HDR_SIZE) {
        if (!s->split_header_data) {
            ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "detected split header/data mode (Zune/WMP)");
            s->split_header_data = 1;
        }
    }

    /* Optional polling is a separate, bounded receive contract. Do not let
     * an idle response be mistaken for failed DATA, or accept stale traffic. */
    if (optional && (container_len < PTP_CONTAINER_HDR_SIZE ||
        first_actual > container_len ||
        zune_le32(first_buf + 8) != s->transaction_id ||
        (container_type == PTP_CT_DATA &&
         (zune_le16(first_buf + 6) != opcode || container_len > 65536u + 12u)))) {
        free(first_buf); s->last_response = PTP_ERROR_IO; return -1;
    }
    if (container_type == PTP_CT_RESPONSE) {
        uint16_t resp_code = zune_le16(first_buf + 6);
        if (optional) {
            if (container_len > 12u + PTP_MAX_PARAMS * 4u ||
                (container_len - 12u) % 4u) {
                free(first_buf); s->last_response = PTP_ERROR_IO; return -1;
            }
            if (first_actual == 12u && container_len > 12u) {
                uint32_t actual = 0;
                if (ptp_usb_read(s, first_buf + 12u, container_len - 12u, &actual) < 0 ||
                    actual != container_len - 12u) {
                    free(first_buf); s->last_response = PTP_ERROR_IO; return -1;
                }
                first_actual += actual;
            }
            if (first_actual != container_len) {
                free(first_buf); s->last_response = PTP_ERROR_IO; return -1;
            }
            int count = (int)(container_len - 12u) / 4;
            if (resp_params) for (int i = 0; i < count; ++i)
                resp_params[i] = zune_le32(first_buf + 12u + 4u*i);
            if (resp_nparams) *resp_nparams = count;
            s->last_response = resp_code;
            free(first_buf);
            return -2; /* Complete response consumed, including parameters. */
        }
        ZLOG(ZUNE_CHATTER_SHOUT, "ptp", "recv_data: got RESPONSE (0x%04X) instead of DATA "
                "(operation may not be supported)", resp_code);
        s->last_response = resp_code;
        if (out_data) *out_data = NULL;
        if (out_len) *out_len = 0;
        free(first_buf);
        return -2;
    }

    if (container_type != PTP_CT_DATA) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "recv_data: expected DATA container (0x%04X), "
                "got type 0x%04X code 0x%04X",
                PTP_CT_DATA, container_type, zune_le16(first_buf + 6));
        free(first_buf);
        return -1;
    }

    if (container_len < PTP_CONTAINER_HDR_SIZE) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "recv_data: invalid container length %u",
                container_len);
        free(first_buf);
        return -1;
    }

    /* Total payload = container length minus header */
    /* Research polling must validate type, opcode, transaction and size before
     * letting a DATA header change the session's outbound framing mode. */
    if (optional && first_actual == PTP_CONTAINER_HDR_SIZE &&
        container_len > PTP_CONTAINER_HDR_SIZE)
        s->split_header_data = 1;
    data_total = container_len - PTP_CONTAINER_HDR_SIZE;

    if (data_total == 0) {
        free(first_buf);
        return 0;
    }

    /* Allocate receive buffer for the full payload */
    payload = (uint8_t *)malloc(data_total);
    if (!payload) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "malloc failed for %u bytes recv buffer",
                data_total);
        free(first_buf);
        return -1;
    }

    /* Copy payload portion from the first read */
    payload_in_first = first_actual - PTP_CONTAINER_HDR_SIZE;
    if (payload_in_first > data_total)
        payload_in_first = data_total;

    memcpy(payload, first_buf + PTP_CONTAINER_HDR_SIZE, payload_in_first);
    free(first_buf);
    first_buf = NULL;
    payload_received = payload_in_first;

    /* Read remaining data in chunks */
    while (payload_received < data_total) {
        if (s->abort_flag && ZUNE_FLAG_GET(*s->abort_flag)) {
            ZLOG(ZUNE_CHATTER_TALK, "ptp", "recv_data: aborted at offset %u of %u",
                    payload_received, data_total);
            free(payload);
            ptp_cancel_transaction(s);
            return -1;
        }
        uint32_t want = data_total - payload_received;
        if (want > CONTEXT_BLOCK_SIZE)
            want = CONTEXT_BLOCK_SIZE;

        uint32_t actual = 0;
        if (ptp_usb_read(s, payload + payload_received, want, &actual) < 0) {
            free(payload);
            ptp_cancel_transaction(s);     /* the device may still be sending */
            return -1;
        }

        if (actual == 0) {
            ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "recv_data: zero-length read at offset %u "
                    "(expected %u total)", payload_received, data_total);
            free(payload);
            return -1;
        }

        payload_received += actual;
    }

    if (out_data) *out_data = payload;
    else free(payload);

    if (out_len) *out_len = data_total;

    return 0;
}

/* ===================================================================
 * Response Phase — Read response container from device
 * =================================================================== */

/*
 * ptp_recv_response — Read and parse a response container.
 *
 * Response container layout:
 *   [0..3]   uint32  length
 *   [4..5]   uint16  type    = PTP_CT_RESPONSE (3)
 *   [6..7]   uint16  code    = response code (PTP_RC_*)
 *   [8..11]  uint32  trans_id
 *   [12..]   uint32  params[0..4]  (optional)
 *
 * Returns 0 if response code is PTP_RC_OK, -1 otherwise.
 * Sets *out_code to the response code if non-NULL.
 */
static int ptp_recv_response_timeout(ptp_session_t *s, uint16_t *out_code,
                                     uint32_t *out_params, int *out_nparams,
                                     uint32_t timeout_ms, int strict)
{
    uint8_t buf[PTP_CONTAINER_HDR_SIZE + PTP_MAX_PARAMS * 4];
    uint32_t actual = 0;
    uint32_t container_len;
    uint16_t container_type;
    uint16_t code;

    if (out_params && out_nparams) *out_nparams = 0;

    if (ptp_usb_read_timeout(s, buf, sizeof(buf), &actual, timeout_ms) < 0) {
        if (out_code) *out_code = PTP_ERROR_IO;
        s->last_response = PTP_ERROR_IO;
        return -1;
    }

    /* Device→host ZLP tolerance: when the preceding DATA phase was an
     * exact 512-multiple, the device appends a zero-length packet (the
     * same wire rule we honor on sends; wire-confirmed device→host in
     * capture-bulkreading). It surfaces HERE as a 0-byte read where
     * the response belongs — swallow it and read again. Without this,
     * GetObjectHandles dies 'short read (0 bytes)' → 0x02FF → dead
     * pipe whenever the handle count lands on the 512-multiple
     * boundary (deterministic Pavo breach freeze, 2026-08-31). */
    if (actual == 0) {
        ZLOG(ZUNE_CHATTER_WHISPER, "ptp",
                "recv_response: swallowed device ZLP, re-reading");
        if (ptp_usb_read_timeout(s, buf, sizeof(buf), &actual, timeout_ms) < 0) {
            if (out_code) *out_code = PTP_ERROR_IO;
            s->last_response = PTP_ERROR_IO;
            return -1;
        }
    }

    /* Debug: dump response bytes — disabled (very noisy). */
#if 0
    {
        char hex[2 * 20 + 1] = "";
        for (uint32_t i = 0; i < actual && i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", buf[i]);
        ZLOG(ZUNE_CHATTER_WHISPER, "ptp", "RESP: %s (%u bytes)", hex, actual);
    }
#endif

    if (actual < PTP_CONTAINER_HDR_SIZE) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "recv_response: short read (%u bytes)", actual);
        if (out_code) *out_code = PTP_ERROR_IO;
        s->last_response = PTP_ERROR_IO;
        return -1;
    }

    container_len  = zune_le32(buf + 0);
    container_type = zune_le16(buf + 4);
    code           = zune_le16(buf + 6);

    if (container_type != PTP_CT_RESPONSE) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "recv_response: expected RESPONSE container "
                "(0x%04X), got type 0x%04X code 0x%04X",
                PTP_CT_RESPONSE, container_type, code);
        if (out_code) *out_code = PTP_ERROR_IO;
        s->last_response = PTP_ERROR_IO;
        return -1;
    }

    if (strict && (container_len != actual || container_len < 12u ||
        container_len > sizeof(buf) || (container_len - 12u) % 4u ||
        zune_le32(buf + 8) != s->transaction_id)) {
        if (out_code) *out_code = PTP_ERROR_IO;
        s->last_response = PTP_ERROR_IO;
        return -1;
    }

    /* Extract response parameters */
    if (out_params && out_nparams && container_len > PTP_CONTAINER_HDR_SIZE) {
        int nparam = (int)(container_len - PTP_CONTAINER_HDR_SIZE) / 4;
        if (nparam > PTP_MAX_PARAMS) nparam = PTP_MAX_PARAMS;
        for (int i = 0; i < nparam; i++) {
            out_params[i] = zune_le32(buf + PTP_CONTAINER_HDR_SIZE + i * 4);
        }
        *out_nparams = nparam;
    }

    if (out_code) *out_code = code;
    s->last_response = code;

    if (code != PTP_RC_OK) {
        ZLOG(ZUNE_CHATTER_SHOUT, "ptp", "response code 0x%04X for transaction %u",
                code, s->transaction_id);
        return -1;
    }

    return 0;
}

static int ptp_recv_response(ptp_session_t *s, uint16_t *out_code,
                             uint32_t *out_params, int *out_nparams)
{
    return ptp_recv_response_timeout(s, out_code, out_params, out_nparams,
                                     PTP_USB_TIMEOUT_MS, 0);
}

/* ===================================================================
 * Public API
 * =================================================================== */

/*
 * ptp_session_init — Initialize a PTP session struct.
 *
 * Zeros out all fields, stores the USB handle, and sets the byte
 * order to little-endian (the only order used over USB).
 * Call this before ptp_open_session().
 */
void ptp_session_init(ptp_session_t *s, zune_usb_handle_t *usb)
{
    memset(s, 0, sizeof(*s));
    s->usb = usb;
    s->transaction_id = 0;
    s->session_id = 0;
    s->byteorder = PTP_DL_LE;
}

/*
 * ptp_open_session — Open a PTP session on the device.
 *
 * Sends the OpenSession command with session_id=1. The Zune accepts
 * a single session. If a session is already open (from a previous
 * crash or incomplete close), PTP_RC_SessionAlreadyOpened is treated
 * as success.
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_open_session(ptp_session_t *s)
{
    uint16_t response_code = 0;
    uint32_t param;
    int ret;

    s->session_id = 1;
    s->transaction_id = 0;  /* PTP spec: OpenSession uses transaction_id 0 */

    param = s->session_id;

    /* Send OpenSession command — param1 is the session ID */
    if (ptp_send_command(s, PTP_OC_OpenSession, &param, 1) < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "open_session: failed to send command");
        return -1;
    }

    /* No data phase for OpenSession */

    /* Read response */
    ret = ptp_recv_response(s, &response_code, NULL, NULL);

    if (ret < 0) {
        /* SessionAlreadyOpened is acceptable — the device has a stale session */
        if (response_code == PTP_RC_SessionAlreadyOpened) {
            ZLOG(ZUNE_CHATTER_TALK, "ptp", "session already open, reusing");
            return 0;
        }
        /* InvalidParameter (0x201D) — try closing stale session, then retry */
        if (response_code == 0x201D) {
            ZLOG(ZUNE_CHATTER_SHOUT, "ptp", "InvalidParameter — closing stale session and retrying");
            s->transaction_id++;
            ptp_send_command(s, PTP_OC_CloseSession, NULL, 0);
            ptp_recv_response(s, NULL, NULL, NULL); /* ignore response */

            /* Retry OpenSession */
            s->session_id = 1;
            s->transaction_id = 0;
            param = s->session_id;
            if (ptp_send_command(s, PTP_OC_OpenSession, &param, 1) < 0)
                return -1;
            ret = ptp_recv_response(s, &response_code, NULL, NULL);
            if (ret == 0 || response_code == PTP_RC_SessionAlreadyOpened) {
                ZLOG(ZUNE_CHATTER_TALK, "ptp", "session opened on retry");
                return 0;
            }
        }
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "open_session: failed with response 0x%04X",
                response_code);
        return -1;
    }

    return 0;
}

/*
 * ptp_close_session — Close the PTP session on the device.
 *
 * Sends CloseSession. After this, no further transactions are valid
 * until a new session is opened.
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_close_session(ptp_session_t *s)
{
    uint16_t response_code = 0;
    int ret;

    s->transaction_id++;

    if (ptp_send_command(s, PTP_OC_CloseSession, NULL, 0) < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "close_session: failed to send command");
        return -1;
    }

    ret = ptp_recv_response(s, &response_code, NULL, NULL);
    if (ret < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "close_session: failed with response 0x%04X",
                response_code);
        return -1;
    }

    s->session_id = 0;
    return 0;
}

/*
 * ptp_transaction — Execute a complete PTP transaction.
 *
 * A PTP transaction consists of up to three phases:
 *   1. Command phase (always): host sends command container on bulk OUT
 *   2. Data phase (optional): host sends or receives data container
 *   3. Response phase (always): device sends response container on bulk IN
 *
 * This function handles all three phases, incrementing the transaction ID
 * for each call, building containers, and managing chunked data transfer.
 *
 * Parameters:
 *   s             — initialized and opened PTP session
 *   opcode        — PTP/MTP operation code
 *   params        — array of command parameters (NULL if nparams==0)
 *   nparams       — number of parameters (0..5)
 *   flags         — PTP_DP_NODATA, PTP_DP_SENDDATA, PTP_DP_GETDATA,
 *                   or PTP_DP_GETDATA_OPTIONAL (bounded polling reads)
 *   send_data     — data payload to send (for PTP_DP_SENDDATA)
 *   send_len      — length of send_data
 *   recv_data     — [out] received data (for PTP_DP_GETDATA), caller frees
 *   recv_len      — [out] length of received data
 *   response_code — [out] device's response code (may be NULL)
 *
 * Returns 0 on success (PTP_RC_OK), -1 on transport or protocol error.
 */
static int ptp_transaction_with(ptp_session_t *s,
                                uint16_t opcode,
                                uint32_t *params, int nparams,
                                uint16_t flags,
                                ptp_source_t *src, uint64_t send_len,
                                uint8_t **recv_data, uint32_t *recv_len,
                                uint16_t *response_code,
                                uint32_t *resp_params, int *resp_nparams)
{
    uint16_t resp_code = 0;
    int ret;

    /* Clear outputs */
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    if (response_code) *response_code = 0;
    if (resp_nparams) *resp_nparams = 0;
    s->last_response = 0;

    /* Assign transaction ID */
    s->transaction_id++;

    /* ---- Phase 1: Command ---- */
    if (ptp_send_command(s, opcode, params, nparams) < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "transaction 0x%04X: command phase failed",
                opcode);
        return -1;
    }

    /* ---- Phase 2: Data (optional) ---- */
    if (flags & PTP_DP_SENDDATA) {
        if (ptp_send_data(s, opcode, src, send_len) < 0) {
            ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "transaction 0x%04X: send data phase failed",
                    opcode);
            return -1;
        }
    }

    const int optional = flags == PTP_DP_GETDATA_OPTIONAL;

    if (flags & PTP_DP_GETDATA) {
        uint8_t *data = NULL;
        uint32_t len = 0;

        int recv_ret = ptp_recv_data(s, &data, &len, optional, opcode,
                                     resp_params, resp_nparams);
        if (recv_ret == -2) {
            /* Device sent RESPONSE instead of DATA — data phase was skipped.
             * The response was already consumed by recv_data (which recorded
             * the REAL code in s->last_response), so we must NOT try to read
             * another response. Surface the actual code, not a synthetic one. */
            if (response_code)
                *response_code = s->last_response ? s->last_response
                                                  : PTP_RC_GeneralError;
            return optional && s->last_response == PTP_RC_OK ? 0 : -1;
        } else if (recv_ret < 0) {
            ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "transaction 0x%04X: recv data phase failed",
                    opcode);
            return -1;
        }

        if (recv_data) *recv_data = data;
        else free(data);

        if (recv_len) *recv_len = len;
    }

    /* ---- Phase 3: Response ---- */
    /* After a data-send phase, the device may need real time to commit the
     * payload (flash write / HDD spin-up) before it answers — wire captures
     * show 3.5 s for a 44.8 MB payload, scaling with size. Use the data
     * timeout there; the 10 s command timeout stays for everything else. */
    ret = ptp_recv_response_timeout(s, &resp_code, resp_params, resp_nparams,
                                    (flags & PTP_DP_SENDDATA)
                                        ? PTP_USB_DATA_TIMEOUT_MS
                                        : PTP_USB_TIMEOUT_MS, optional);
    if (response_code) *response_code = resp_code;

    if (ret < 0 && resp_code != PTP_RC_OK) {
        return -1;
    }

    return 0;
}

int ptp_transaction(ptp_session_t *s,
                    uint16_t opcode,
                    uint32_t *params, int nparams,
                    uint16_t flags,
                    uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams)
{
    ptp_source_t src = { .data = send_data, .fd = -1 };
    return ptp_transaction_with(s, opcode, params, nparams, flags, &src, send_len,
                                recv_data, recv_len, response_code, resp_params, resp_nparams);
}

int ptp_transaction_file(ptp_session_t *s, uint16_t opcode,
                         uint32_t *params, int nparams,
                         int fd, uint64_t send_len,
                         uint16_t *response_code)
{
    ptp_source_t src = { .data = NULL, .fd = fd, .chunk = malloc(CONTEXT_BLOCK_SIZE) };
    if (!src.chunk) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "cannot allocate the upload buffer");
        if (response_code) *response_code = 0;
        return -1;
    }
    int ret = ptp_transaction_with(s, opcode, params, nparams, PTP_DP_SENDDATA, &src, send_len,
                                   NULL, NULL, response_code, NULL, NULL);
    free(src.chunk);
    return ret;
}

/* ===================================================================
 * Convenience Wrappers
 * =================================================================== */

/*
 * ptp_get_device_prop_value — Read a device property value.
 *
 * Sends GetDevicePropValue (0x1015) with the property code as param1.
 * The device responds with a data container holding the raw property
 * value (format depends on the property).
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_get_device_prop_value(ptp_session_t *s, uint16_t propcode,
                              uint8_t **data, uint32_t *len)
{
    uint32_t param = (uint32_t)propcode;
    uint16_t resp = 0;
    int ret;

    ret = ptp_transaction(s, PTP_OC_GetDevicePropValue,
                          &param, 1,
                          PTP_DP_GETDATA,
                          NULL, 0,
                          data, len,
                          &resp, NULL, NULL);

    if (ret < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "get_device_prop_value 0x%04X: "
                "failed with response 0x%04X", propcode, resp);
        /* A data phase that completed before the failed response: release
         * it, so a failure never hands the caller a buffer. */
        if (data) {
            free(*data);
            *data = NULL;
        }
        if (len) *len = 0;
    }
    return ret;
}

/*
 * ptp_set_device_prop_value — Write a device property value.
 *
 * Sends SetDevicePropValue (0x1016) with the property code as param1
 * and the raw value as the data phase payload.
 *
 * Returns 0 on success, -1 on error.
 */
int ptp_set_device_prop_value(ptp_session_t *s, uint16_t propcode,
                              uint8_t *data, uint32_t len)
{
    uint32_t param = (uint32_t)propcode;
    uint16_t resp = 0;
    int ret;

    ret = ptp_transaction(s, PTP_OC_SetDevicePropValue,
                          &param, 1,
                          PTP_DP_SENDDATA,
                          data, (uint64_t)len,
                          NULL, NULL,
                          &resp, NULL, NULL);

    if (ret < 0) {
        ZLOG(ZUNE_CHATTER_SCREAM, "ptp", "set_device_prop_value 0x%04X: "
                "failed with response 0x%04X", propcode, resp);
    }
    return ret;
}
