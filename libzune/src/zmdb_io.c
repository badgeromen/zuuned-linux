/* Complete bounded ZMDB transport, shared by music scans and spoken state.
 * A raw 16-byte bulk request is followed by a 12-byte response header and
 * payload in chunks. This vendor 0x9217 sequence is not a standard PTP
 * transaction, so it uses the backend directly. Caller owns serialization.
 * Reading the database never authorizes mutation. Partial payloads fail closed. */
#include "zune_internal.h"
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int zune_zmdb_read_payload(zune_usb_handle_t *usb, uint8_t **out_payload,
                          uint32_t *out_len) {
    if (out_payload) *out_payload = NULL;
    if (out_len) *out_len = 0;
    if (!usb || !out_payload || !out_len || !usb->backend ||
        !usb->backend->bulk_read || !usb->backend->bulk_write) {
        zune_set_error("Invalid ZMDB transport");
        return -1;
    }
    const zune_usb_backend_t *be = usb->backend;
    uint8_t outep = usb->bulk_out_ep;
    uint8_t inep  = usb->bulk_in_ep;

    fprintf(stderr, "[libzune-zmdb] using endpoints: OUT=0x%02x, IN=0x%02x\n",
            outep, inep);

    /* Step 1: Build 16-byte ZMDB request */
    uint8_t request[16] = {0};
    request[0]  = 0x10;    /* length (16) */
    request[4]  = 0x01;    /* command marker */
    request[6]  = 0x17;    /* opcode high byte */
    request[7]  = 0x92;    /* opcode low byte -> 0x1792 */
    request[8]  = 0x03;    /* music library object ID */
    request[9]  = 0x92;
    request[10] = 0x1f;
    request[12] = 0x01;    /* trailer */

    /* Step 2: Send request via bulk OUT */
    uint32_t transferred = 0;
    int ret = be->bulk_write(usb, outep, request, 16, &transferred, 5000);
    if (ret != 0 || transferred != 16) {
        zune_set_error("ZMDB request send failed (transferred=%u)", transferred);
        return -1;
    }
    fprintf(stderr, "[libzune-zmdb] request sent (16 bytes)\n");

    /* Step 3: Wait 250ms for device to prepare response */
    usleep(250000);

    /* Step 4: Read first chunk (header + initial payload, up to 64KB) */
    uint8_t *first_chunk = malloc(65536);
    if (!first_chunk) {
        zune_set_error("Out of memory for ZMDB first chunk");
        return -1;
    }

    transferred = 0;
    ret = be->bulk_read(usb, inep, first_chunk, 65536, &transferred, 5000);
    if (ret != 0 || transferred < 12 || transferred > 65536) {
        free(first_chunk);
        zune_set_error("ZMDB header read failed (got %u bytes)", transferred);
        return -1;
    }

    /* Step 5: Parse header — totalSize at offset 0 */
    uint32_t total_size = rd32(first_chunk);
    if (total_size <= 12 || total_size > 64 * 1024 * 1024 ||
        transferred > total_size || rd16(first_chunk + 4) != 2 ||
        rd16(first_chunk + 6) != 0x9217) {
        free(first_chunk);
        zune_set_error("ZMDB invalid header or total size: %u", total_size);
        return -1;
    }

    uint32_t payload_size = total_size - 12;
    fprintf(stderr, "[libzune-zmdb] totalSize=%u, payloadSize=%u, first chunk=%u bytes\n",
            total_size, payload_size, transferred);

    /* Step 6: Allocate full payload buffer and copy what we already have */
    uint8_t *payload = malloc(payload_size);
    if (!payload) {
        free(first_chunk);
        zune_set_error("Out of memory for ZMDB payload (%u bytes)", payload_size);
        return -1;
    }

    /* The first chunk contains 12-byte header + initial payload data */
    uint32_t first_payload = transferred - 12;
    /* Header validation above guarantees this fits the declared payload. */
    memcpy(payload, first_chunk + 12, first_payload);
    free(first_chunk);

    /* Step 7: Read remaining payload in 64KB chunks */
    uint32_t received = first_payload;
    while (received < payload_size) {
        uint32_t chunk = payload_size - received;
        if (chunk > 65536) chunk = 65536;

        transferred = 0;
        ret = be->bulk_read(usb, inep, payload + received, chunk,
                            &transferred, 10000);
        if (ret != 0 || transferred > chunk) {
            fprintf(stderr, "[libzune-zmdb] read error at %u/%u\n",
                    received, payload_size);
            break;
        }
        if (transferred == 0) break;
        received += transferred;
    }

    /* Step 8: Drain residual data (race 512-byte read against 100ms timeout).
     * Matches zune-explorer: errors are expected and silently ignored. */
    uint8_t drain[512];
    uint32_t drain_transferred = 0;
    be->bulk_read(usb, inep, drain, sizeof(drain), &drain_transferred, 100);

    fprintf(stderr, "[libzune-zmdb] received %u/%u bytes\n", received, payload_size);

    if (received != payload_size) {
        free(payload);
        zune_set_error("ZMDB read incomplete: got %u of %u bytes", received, payload_size);
        return -1;
    }

    *out_payload = payload;
    *out_len = received;
    return 0;
}
