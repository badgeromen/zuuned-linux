/* Experimental local image routing, not a catalog/MusicBrainz identity claim.
 * Caller MUST persist a private durable snapshot and intended GUID before apply.
 * Single serialized device owner required. No creation, deletion or retry. */
#ifndef ZUNETOOL_HD_ARTIST_BINDING_H
#define ZUNETOOL_HD_ARTIST_BINDING_H
#include "hd_artist_probe.h"

#define HD_ART_BIND_VERSION 1u
#define HD_ART_BIND_STRING_MAX 511u
#define HD_ART_BIND_SERIAL_MAX 256u

typedef enum {
    HD_ART_BIND_CONFIRMED = 0,
    HD_ART_BIND_REFUSED = 1,  /* Explicit refusal AND original state read back. */
    HD_ART_BIND_UNCERTAIN = 2, /* Write attempted, outcome not verified. Keep recovery. */
    HD_ART_BIND_REJECTED = 3   /* No write attempted: invalid/preflight/read failure. */
} HdArtistBindingResult;

typedef struct {
    uint32_t version, handle, storage_id;
    uint16_t format, name_len, filename_len;
    uint8_t name_wire[HD_ART_BIND_STRING_MAX];
    uint8_t filename_wire[HD_ART_BIND_STRING_MAX];
    uint8_t persistent_id[16], da99[16], original_da97[16];
    char serial[HD_ART_BIND_SERIAL_MAX];
} HdArtistBindingSnapshot;

static int hd_bind_nonzero(const uint8_t p[16]) {
    unsigned value = 0;
    for (unsigned i = 0; i < 16; ++i) value |= p[i];
    return value != 0;
}
static int hd_bind_ready(ZuneDevice *dev) {
    return dev && dev->device_family == ZUNE_FAMILY_PAVO && dev->mtpz_authenticated &&
           !hd_art_stopped(dev) && dev->serial && dev->serial[0] &&
           strnlen(dev->serial, HD_ART_BIND_SERIAL_MAX) < HD_ART_BIND_SERIAL_MAX &&
           dev->storage_id && dev->storage_id != UINT32_MAX;
}
static int hd_bind_wire_string(const uint8_t *p, size_t n) {
    size_t used = 0;
    return n >= 5 && n <= HD_ART_BIND_STRING_MAX && !hd_art_string(p, n, &used) && used == n;
}
/* Strict UTF-16 -> UTF-8 comparison. Existing generic UCS-2 helpers replace
 * supplementary characters, so they must not decide an exact artist match. */
static int hd_bind_name_matches(const uint8_t *p, size_t n, const char *expected) {
    char text[1021]; size_t out = 0;
    if (!expected || !expected[0] || strnlen(expected, sizeof(text)) == sizeof(text) ||
        !hd_bind_wire_string(p, n)) return 0;
    for (size_t i = 0; i + 1 < p[0]; ++i) {
        uint32_t cp = hd_art_u16(p + 1 + 2*i);
        if (cp >= 0xd800 && cp <= 0xdbff) {
            uint32_t low = hd_art_u16(p + 1 + 2*++i);
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        }
        if (cp < 0x80) text[out++] = (char)cp;
        else if (cp < 0x800) {
            text[out++] = (char)(0xc0 | (cp >> 6));
            text[out++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            text[out++] = (char)(0xe0 | (cp >> 12));
            text[out++] = (char)(0x80 | ((cp >> 6) & 63));
            text[out++] = (char)(0x80 | (cp & 63));
        } else {
            text[out++] = (char)(0xf0 | (cp >> 18));
            text[out++] = (char)(0x80 | ((cp >> 12) & 63));
            text[out++] = (char)(0x80 | ((cp >> 6) & 63));
            text[out++] = (char)(0x80 | (cp & 63));
        }
    }
    text[out] = '\0';
    return !strcmp(text, expected);
}
static int hd_bind_valid_snapshot(const HdArtistBindingSnapshot *s) {
    return s && s->version == HD_ART_BIND_VERSION && s->handle && s->handle != UINT32_MAX &&
           s->storage_id && s->storage_id != UINT32_MAX && s->format == MTP_OFC_AbstractArtist &&
           s->serial[0] && memchr(s->serial, 0, sizeof(s->serial)) &&
           hd_bind_wire_string(s->name_wire, s->name_len) &&
           hd_bind_wire_string(s->filename_wire, s->filename_len) &&
           hd_bind_nonzero(s->persistent_id) && !hd_bind_nonzero(s->original_da97);
}
static int hd_bind_same_device(ZuneDevice *dev, const HdArtistBindingSnapshot *s) {
    return hd_bind_ready(dev) && dev->storage_id == s->storage_id && !strcmp(dev->serial, s->serial);
}
static int hd_bind_get(ZuneDevice *dev, uint32_t handle, uint16_t prop,
                       uint8_t *out, uint32_t exact, uint16_t *string_len) {
    uint8_t *data = NULL; uint32_t length = 0, params[2] = {handle, prop};
    int ok = hd_bind_ready(dev) &&
        !hd_art_read(dev, MTP_OC_GetObjectPropValue, params, 2, &data, &length) && data &&
        (string_len ? hd_bind_wire_string(data, length) : length == exact);
    if (ok) {
        memcpy(out, data, length);
        if (string_len) *string_len = (uint16_t)length;
    }
    free(data);
    return ok ? 0 : -1;
}
static int hd_bind_descriptor(ZuneDevice *dev) {
    uint8_t *data = NULL; uint32_t length = 0, params[2] = {0xda97, MTP_OFC_AbstractArtist};
    /* Observed UINT128 descriptor: code/type/access/default16/group4/form0.
     * Other forms fail closed until their permitted values are understood. */
    int ok = hd_bind_ready(dev) &&
        !hd_art_read(dev, MTP_OC_GetObjectPropDesc, params, 2, &data, &length) &&
        data && length == 26 && hd_art_u16(data) == 0xda97 &&
        hd_art_u16(data+2) == HD_ART_UINT128 && data[4] == 1 && data[25] == 0;
    free(data);
    return ok ? 0 : -1;
}
/* Reads current state, allowing either zero or assigned DA97 for verification. */
static int hd_bind_read_state(ZuneDevice *dev, uint32_t handle, HdArtistBindingSnapshot *state) {
    uint8_t value[4];
    memset(state, 0, sizeof(*state));
    if (!hd_bind_ready(dev) || !handle || handle == UINT32_MAX) return -1;
    state->version = HD_ART_BIND_VERSION; state->handle = handle;
    state->storage_id = dev->storage_id;
    memcpy(state->serial, dev->serial, strlen(dev->serial)+1);
    if (hd_bind_get(dev, handle, MTP_OPC_ObjectFormat, value, 2, NULL)) return -1;
    state->format = hd_art_u16(value);
    if (state->format != MTP_OFC_AbstractArtist ||
        hd_bind_get(dev, handle, MTP_OPC_StorageID, value, 4, NULL) ||
        hd_art_u32(value) != state->storage_id || hd_bind_descriptor(dev) ||
        hd_bind_get(dev, handle, 0xdc44, state->name_wire, 0, &state->name_len) ||
        hd_bind_get(dev, handle, 0xdc07, state->filename_wire, 0, &state->filename_len) ||
        hd_bind_get(dev, handle, 0xdc41, state->persistent_id, 16, NULL) ||
        !hd_bind_nonzero(state->persistent_id) ||
        hd_bind_get(dev, handle, 0xda99, state->da99, 16, NULL) ||
        hd_bind_get(dev, handle, 0xda97, state->original_da97, 16, NULL) ||
        !hd_bind_same_device(dev, state)) return -1;
    return 0;
}
static int hd_bind_matches(const HdArtistBindingSnapshot *expected,
                            const HdArtistBindingSnapshot *current, const uint8_t guid[16]) {
    return current->version == expected->version && current->handle == expected->handle &&
           current->storage_id == expected->storage_id && current->format == expected->format &&
           !strcmp(current->serial, expected->serial) &&
           current->name_len == expected->name_len && current->filename_len == expected->filename_len &&
           !memcmp(current->name_wire, expected->name_wire, expected->name_len) &&
           !memcmp(current->filename_wire, expected->filename_wire, expected->filename_len) &&
           !memcmp(current->persistent_id, expected->persistent_id, 16) &&
           !memcmp(current->da99, expected->da99, 16) &&
           !memcmp(current->original_da97, guid, 16);
}
static HdArtistBindingResult hd_artist_binding_snapshot(ZuneDevice *dev, uint32_t handle,
                       const char *expected_name, HdArtistBindingSnapshot *out) {
    HdArtistBindingSnapshot state;
    if (!out) return HD_ART_BIND_REJECTED;
    memset(out, 0, sizeof(*out));
    if (!expected_name || !expected_name[0] ||
        hd_bind_read_state(dev, handle, &state) || !hd_bind_valid_snapshot(&state) ||
        !hd_bind_name_matches(state.name_wire, state.name_len, expected_name)) return HD_ART_BIND_REJECTED;
    *out = state;
    return HD_ART_BIND_CONFIRMED;
}
static int hd_bind_explicit_refusal(uint16_t code) {
    /* GeneralError/IncompleteTransfer/transport failures can follow a write. */
    return code == 0x2005 || code == 0x2006 || code == 0x2009 || code == 0x200f ||
           code == 0x2016 || code == 0xa801 || code == 0xa802 || code == 0xa803;
}
static HdArtistBindingResult hd_bind_write_guarded(ZuneDevice *dev, const HdArtistBindingSnapshot *snapshot,
                              const uint8_t before[16], const uint8_t after[16],
                              int (*write_allowed)(void *),void *context) {
    HdArtistBindingSnapshot current;
    if (!hd_bind_valid_snapshot(snapshot) || !hd_bind_same_device(dev, snapshot) ||
        hd_bind_read_state(dev, snapshot->handle, &current) ||
        !hd_bind_matches(snapshot, &current, before) || !hd_bind_same_device(dev, snapshot))
        return HD_ART_BIND_REJECTED;
    if(write_allowed&&!write_allowed(context))return HD_ART_BIND_REJECTED;
    uint32_t params[2] = {snapshot->handle, 0xda97};
    uint16_t response = 0;
    int result = ptp_transaction(&dev->ptp, MTP_OC_SetObjectPropValue, params, 2,
                                 PTP_DP_SENDDATA, (uint8_t *)after, 16,
                                 NULL, NULL, &response, NULL, NULL);
    if (result || response != PTP_RC_OK) {
        if (hd_bind_explicit_refusal(response) &&
            !hd_bind_read_state(dev, snapshot->handle, &current) &&
            hd_bind_matches(snapshot, &current, before)) {
            dev->ptp.last_response = response;
            return HD_ART_BIND_REFUSED;
        }
        dev->ptp.last_response = response;
        return HD_ART_BIND_UNCERTAIN;
    }
    if (hd_bind_read_state(dev, snapshot->handle, &current) ||
        !hd_bind_matches(snapshot, &current, after)) return HD_ART_BIND_UNCERTAIN;
    return HD_ART_BIND_CONFIRMED;
}
static HdArtistBindingResult hd_bind_write(ZuneDevice *dev,const HdArtistBindingSnapshot *snapshot,
                              const uint8_t before[16],const uint8_t after[16]) {
    return hd_bind_write_guarded(dev,snapshot,before,after,NULL,NULL);
}
static HdArtistBindingResult hd_artist_binding_apply_guarded(ZuneDevice *dev,
                      const HdArtistBindingSnapshot *snapshot,const uint8_t routing_guid[16],
                      int (*write_allowed)(void *),void *context) {
    if(!routing_guid||!hd_bind_nonzero(routing_guid)||!hd_bind_valid_snapshot(snapshot)||
       (write_allowed&&!write_allowed(context)))return HD_ART_BIND_REJECTED;
    return hd_bind_write_guarded(dev,snapshot,snapshot->original_da97,routing_guid,write_allowed,context);
}
static HdArtistBindingResult hd_artist_binding_apply(ZuneDevice *dev,
                      const HdArtistBindingSnapshot *snapshot, const uint8_t routing_guid[16]) {
    if (!routing_guid || !hd_bind_nonzero(routing_guid) || !hd_bind_valid_snapshot(snapshot))
        return HD_ART_BIND_REJECTED;
    return hd_bind_write(dev, snapshot, snapshot->original_da97, routing_guid);
}
static HdArtistBindingResult hd_artist_binding_restore(ZuneDevice *dev,
                      const HdArtistBindingSnapshot *snapshot, const uint8_t routing_guid[16]) {
    if (!routing_guid || !hd_bind_nonzero(routing_guid) || !hd_bind_valid_snapshot(snapshot))
        return HD_ART_BIND_REJECTED;
    return hd_bind_write(dev, snapshot, routing_guid, snapshot->original_da97);
}
#endif
