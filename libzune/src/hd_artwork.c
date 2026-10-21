/* Public native HD artist content. All USB belongs to the caller's serial owner. */
#include "zune_internal.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "hd/hd_network_probe.h"
#include "hd/hd_http_init.h"
#include "hd/hd_art_gallery.h"
#include "hd/hd_artist_binding.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static int hd_guid(const char *text, uint8_t wire[16]) {
    uint8_t raw[16]; size_t n = 0;
    if (!text || strnlen(text, 37) != 36) return -1;
    for (unsigned i = 0; i < 36;) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i++] != '-') return -1;
            continue;
        }
        unsigned value = 0;
        for (unsigned j = 0; j < 2; j++) {
            unsigned char c = (unsigned char)tolower((unsigned char)text[i++]);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return -1;
            value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
        }
        raw[n++] = (uint8_t)value;
    }
    if (n != 16 || !hd_bind_nonzero(raw)) return -1;
    static const unsigned order[16] = {3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
    for (unsigned i = 0; i < 16; i++) wire[i] = raw[order[i]];
    return 0;
}

static int hd_name(const char *name) {
    char escaped[1537];
    if (!name || !*name || hdis_xml_name(name, escaped)) return -1;
    size_t n = strlen(name), i = 0;
    while (i < n && (unsigned char)name[i] <= 32) i++;
    return i == n ? -1 : 0;
}

static int hd_sink_emit(void *opaque, const uint8_t *bytes, size_t size) {
    (void)opaque; (void)bytes; (void)size; return -1;
}

static int hd_prepare(const ZuneHDArtConfig *c, HdImageServer *s,
                      HdImageEmit emit, void *opaque) {
    uint8_t guid[16]; HdImageAsset assets[HDIS_ASSETS] = {0};
    size_t members[HDIS_GALLERY_MAX] = {2,3,4,5,6,7};
    if (!c || c->version != ZUNE_HD_ART_API_VERSION || hd_name(c->artist_name) ||
        hd_guid(c->catalog_guid, guid) || c->gallery_count > ZUNE_HD_ART_GALLERY_MAX ||
        c->duration_ms < 1000 || c->duration_ms > 360000 ||
        (!c->biography && c->biography_size) ||
        (c->biography && (!c->biography_size || c->biography_size > HDIS_BIO_MAX))) return -1;
    size_t count = c->gallery_count + 2;
    for (size_t i = 0; i < count; i++) {
        const ZuneHDArtImage *image = i == 0 ? &c->background : i == 1 ? &c->thumbnail : &c->gallery[i-2];
        if (hd_guid(image->guid, guid) || hd_art_jpeg_decode(image->jpeg, image->size)) return -1;
        memcpy(assets[i].guid, image->guid, 37);
        assets[i].jpeg = image->jpeg; assets[i].jpeg_n = image->size;
    }
    if (hd_image_server_init(s, 0xc0a83764, c->catalog_guid,
            c->background.jpeg, c->background.size, emit, opaque) ||
        strcasecmp(s->image_guid, c->background.guid) ||
        hd_image_server_set_asset_roles(s, assets, count, 0, 1, members, c->gallery_count) ||
        hd_image_server_set_artist(s, c->artist_name)) return -1;
    if (c->biography) {
        size_t i = 0;
        while (i < c->biography_size && (c->biography[i] == ' ' || c->biography[i] == '\t' ||
               c->biography[i] == '\r' || c->biography[i] == '\n')) i++;
        if (i == c->biography_size || hd_image_server_set_biography(s, c->biography, c->biography_size)) return -1;
    }
    return 0;
}

ZuneHDArtStatus zune_hd_art_validate(const ZuneHDArtConfig *config) {
    zune_clear_error();
    HdImageServer *s = calloc(1, sizeof(*s));
    int failed = !s || hd_prepare(config, s, hd_sink_emit, NULL);
    free(s);
    if (failed) zune_set_error("Invalid HD artist content: check UTF-8, UUIDs, JPEGs and limits");
    return failed ? ZUNE_HD_ART_INVALID : ZUNE_HD_ART_OK;
}

typedef struct {
    ZuneDevice *dev;
    HdImageServer server;
    HdNetworkState *network;
    ZuneHDArtReady ready;
    void *ready_context;
    ZuneHDArtProgress progress;
    void *progress_context;
    ZuneHDArtResult *result;
    int ready_failed;
    int biography_required;
    int64_t last_activity;
    unsigned last_completed, last_requests;
} HdPublicSession;

static void hd_progress(HdPublicSession *s) {
    ZuneHDArtResult *r = s->result;
    r->asset_count = s->server.asset_count;
    r->http_requests = s->server.requests;
    r->biography_requests = s->server.biography_requests;
    r->biography_acknowledged = s->server.biography_completed;
    for (size_t i = 0; i < r->asset_count; i++) {
        memcpy(r->assets[i].guid, s->server.assets[i].guid, 37);
        r->assets[i].requests = s->server.asset_requests[i];
        r->assets[i].acknowledged = s->server.asset_completed[i];
    }
    if (s->progress) s->progress(r, s->progress_context);
}

static int hd_public_emit(void *opaque, const uint8_t *bytes, size_t size) {
    HdPublicSession *s = opaque;
    return s->network ? hd_network_emit(s->network, 0x21, bytes, size) : -1;
}

static int hd_public_ready(HdPublicSession *s, HdNetworkState *network) {
    s->network = network;
    if (!s->result->ready_called) {
        s->result->ready_called = 1;
        int rc = s->ready(s->dev, s->ready_context);
        if (rc || s->dev->hd_transport_uncertain || ZUNE_FLAG_GET(s->dev->disconnecting) || ZUNE_FLAG_GET(s->dev->cancel_requested)) {
            s->ready_failed = 1;
            if (rc == -2 || s->dev->hd_transport_uncertain) network->callback_transport_uncertain = 1;
            return -1;
        }
    }
    return s->ready_failed ? -1 : 0;
}

static int hd_complete(HdPublicSession *s, int64_t now) {
    unsigned done = s->server.image_completed + s->server.biography_completed;
    if (done != s->last_completed || s->server.requests != s->last_requests) {
        s->last_completed = done; s->last_requests = s->server.requests;
        s->last_activity = now; hd_progress(s);
    }
    if (s->server.distinct_assets_completed != s->server.asset_count ||
        (s->biography_required && !s->server.biography_completed)) return 0;
    for (size_t i = 0; i < HDIS_FLOWS; i++) if (s->server.flow[i].active) return 0;
    return now >= s->last_activity && now - s->last_activity >= 3000;
}

static int hd_public_packet(HdNetworkState *network, void *opaque, const uint8_t *bytes, size_t size) {
    HdPublicSession *s = opaque;
    if (hd_public_ready(s, network) || hd_image_server_packet(&s->server, bytes, size)) return -1;
    return hd_complete(s, HD_TUNNEL_NOW_MS());
}

static int hd_public_tick(HdNetworkState *network, void *opaque, int64_t now) {
    HdPublicSession *s = opaque;
    if (now < 0) return -1;
    if (!network->local_ipcp || !network->peer_ipcp || network->frame_length || network->frame_escaped) return 0;
    if (hd_public_ready(s, network) || hd_image_server_tick(&s->server, (uint64_t)now)) return -1;
    return hd_complete(s, now);
}

ZuneHDArtStatus zune_hd_art_serve(ZuneDevice *dev, const ZuneHDArtConfig *config,
    ZuneHDArtReady ready, void *ready_context, ZuneHDArtProgress progress,
    void *progress_context, ZuneHDArtResult *result) {
    zune_clear_error();
    if (!result) return ZUNE_HD_ART_INVALID;
    memset(result, 0, sizeof(*result)); result->status = ZUNE_HD_ART_INVALID;
    result->safe_to_continue = dev && !dev->hd_transport_uncertain && !ZUNE_FLAG_GET(dev->disconnecting);
    if (!dev || !ready) return result->status;
    if (dev->hd_art_active || dev->hd_transport_uncertain || ZUNE_FLAG_GET(dev->disconnecting)) {
        result->safe_to_continue = 0;
        result->transport_uncertain = dev->hd_transport_uncertain;
        return result->status;
    }
    if (dev->device_family != ZUNE_FAMILY_PAVO) return result->status = ZUNE_HD_ART_UNSUPPORTED;
    if (ZUNE_FLAG_GET(dev->cancel_requested)) return result->status = ZUNE_HD_ART_CANCELLED;
    if (!dev->mtpz_authenticated) return result->status;
    HdPublicSession *s = calloc(1, sizeof(*s));
    if (!s) return result->status;
    s->dev = dev; s->ready = ready; s->ready_context = ready_context;
    s->progress = progress; s->progress_context = progress_context; s->result = result;
    if (hd_prepare(config, &s->server, hd_public_emit, s)) {
        zune_set_error("Invalid HD artist content"); free(s); return result->status;
    }
    s->biography_required = config->biography != NULL;
    s->last_activity = HD_TUNNEL_NOW_MS();
    if (s->last_activity < 0) { free(s); return result->status; }
    s->server.now = (uint64_t)s->last_activity;
    /* Protocol diagnostics contain no auth material; private payloads never logged.
     * /dev/null avoids growing temp files or exposing artists via default stdout. */
    FILE *out = fopen("/dev/null", "w");
    if (!out) { free(s); return result->status = ZUNE_HD_ART_IO_ERROR; }
    dev->hd_art_active = 1;
    result->status = ZUNE_HD_ART_PARTIAL; result->safe_to_continue = 0;
    hd_progress(s);
    int uncertain = 0, clean = 0;
    if (hd_http_init_full_ex(dev, out, NULL, &uncertain)) {
        result->status = ZUNE_FLAG_GET(dev->cancel_requested) ? ZUNE_HD_ART_CANCELLED : ZUNE_HD_ART_IO_ERROR;
        result->safe_to_continue = !uncertain && !ZUNE_FLAG_GET(dev->disconnecting) && !ZUNE_FLAG_GET(dev->cancel_requested);
    } else {
        const char *failure = "not-started";
        int rc = hd_network_probe_run_detail(dev, out, NULL, hd_public_packet, hd_public_tick,
                s, 1, config->duration_ms, &uncertain, &clean, &failure);
        result->network_closed = clean;
        result->safe_to_continue = clean && !uncertain && !ZUNE_FLAG_GET(dev->disconnecting) && !ZUNE_FLAG_GET(dev->cancel_requested);
        if (ZUNE_FLAG_GET(dev->cancel_requested)) result->status = ZUNE_HD_ART_CANCELLED;
        else if (s->ready_failed || uncertain || !clean || (rc && strcmp(failure, "deadline") &&
                 strcmp(failure, "observation-incomplete"))) result->status = ZUNE_HD_ART_IO_ERROR;
        else if (s->server.distinct_assets_completed == s->server.asset_count &&
                 (!s->biography_required || s->server.biography_completed)) result->status = ZUNE_HD_ART_OK;
        else result->status = ZUNE_HD_ART_PARTIAL;
    }
    if (!result->safe_to_continue) dev->hd_transport_uncertain = 1;
    result->transport_uncertain = uncertain || dev->hd_transport_uncertain;
    dev->hd_art_active = 0;
    hd_progress(s); fclose(out); free(s);
    if (result->status == ZUNE_HD_ART_IO_ERROR) zune_set_error("HD artwork session failed; inspect transport and asset results before continuing");
    return result->status;
}

static int hd_equal_name(const char *a, const char *b) {
    while (*a && (unsigned char)*a <= 32) a++;
    while (*b && (unsigned char)*b <= 32) b++;
    size_t na = strlen(a), nb = strlen(b);
    while (na && (unsigned char)a[na-1] <= 32) na--;
    while (nb && (unsigned char)b[nb-1] <= 32) nb--;
    if (na != nb) return 0;
    for (size_t i = 0; i < na; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a'-'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a'-'A';
        if (ca != cb) return 0;
    }
    return 1;
}

/* Strict conversion preserves supplementary Unicode rather than normalizing an
 * identity through the legacy UCS-2 replacement behavior. */
static char *hd_read_name(const uint8_t *p, size_t n) {
    size_t used = 0;
    if (hd_art_string(p, n, &used) || used != n || n < 5) return NULL;
    char *text = calloc(1, 1021); if (!text) return NULL;
    size_t out = 0;
    for (size_t i = 0; i+1 < p[0]; i++) {
        uint32_t cp = hd_art_u16(p+1+2*i);
        if (cp >= 0xd800 && cp <= 0xdbff) cp = 0x10000 + ((cp-0xd800)<<10) + (hd_art_u16(p+1+2*++i)-0xdc00);
        if (cp < 0x80) text[out++] = (char)cp;
        else if (cp < 0x800) { text[out++] = (char)(0xc0|(cp>>6)); text[out++] = (char)(0x80|(cp&63)); }
        else if (cp < 0x10000) { text[out++] = (char)(0xe0|(cp>>12)); text[out++] = (char)(0x80|((cp>>6)&63)); text[out++] = (char)(0x80|(cp&63)); }
        else { text[out++] = (char)(0xf0|(cp>>18)); text[out++] = (char)(0x80|((cp>>12)&63)); text[out++] = (char)(0x80|((cp>>6)&63)); text[out++] = (char)(0x80|(cp&63)); }
    }
    return text;
}

static int hd_name_wire(const char *text, uint8_t wire[511], size_t *size) {
    size_t at = 1, count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p;) {
        uint32_t cp = *p++; unsigned tail = 0;
        if (cp >= 0xf0) { cp &= 7; tail = 3; }
        else if (cp >= 0xe0) { cp &= 15; tail = 2; }
        else if (cp >= 0xc0) { cp &= 31; tail = 1; }
        while (tail--) cp = (cp<<6) | (*p++ & 63);
        if (count + (cp >= 0x10000 ? 2 : 1) > 254) return -1;
        if (cp >= 0x10000) {
            cp -= 0x10000; uint16_t high = (uint16_t)(0xd800 | (cp>>10));
            wire[at++] = (uint8_t)high; wire[at++] = (uint8_t)(high>>8); count++;
            cp = 0xdc00 | (cp & 1023);
        }
        wire[at++] = (uint8_t)cp; wire[at++] = (uint8_t)(cp>>8); count++;
    }
    wire[at++] = 0; wire[at++] = 0; wire[0] = (uint8_t)(count+1); *size = at; return 0;
}

static void hd_prop(uint8_t *data, size_t *pos, uint16_t prop, uint16_t type, const void *bytes, size_t size) {
    memset(data+*pos, 0, 4); *pos += 4;
    data[(*pos)++] = (uint8_t)prop; data[(*pos)++] = (uint8_t)(prop>>8);
    data[(*pos)++] = (uint8_t)type; data[(*pos)++] = (uint8_t)(type>>8);
    memcpy(data+*pos, bytes, size); *pos += size;
}

/* The proven HD graph places abstract artists below the existing Artists
 * association, not storage root. Never create or guess this shared folder. */
static int hd_artist_parent(ZuneDevice *dev, uint32_t *parent) {
    uint32_t *ids = NULL; int count = 0; *parent = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_Association, 0, &ids, &count) ||
        count < 0 || count > 65536 || (count && !ids)) { free(ids); return -1; }
    for (int i = 0; i < count; i++) {
        uint8_t *data = NULL; uint32_t n = 0;
        if (!ids[i] || ids[i] == UINT32_MAX || ZUNE_FLAG_GET(dev->cancel_requested) ||
            mtp_get_object_prop_value(&dev->ptp, ids[i], MTP_OPC_ObjectFileName, &data, &n)) { free(data); free(ids); return -1; }
        char *name = hd_read_name(data, n); free(data); data = NULL;
        if (!name) { free(ids); return -1; }
        int match = !strcmp(name, "Artists"); free(name);
        if (!match) continue;
        if (mtp_get_object_prop_value(&dev->ptp, ids[i], MTP_OPC_ParentObject, &data, &n) || !data || n != 4) { free(data); free(ids); return -1; }
        int root = !hd_art_u32(data); free(data);
        if (root) { if (*parent) { free(ids); return -1; } *parent = ids[i]; }
    }
    free(ids);
    if (!*parent) return -1;
    const uint16_t props[2] = {MTP_OPC_StorageID, MTP_OPC_ObjectFormat};
    for (unsigned i = 0; i < 2; i++) {
        uint8_t *data = NULL; uint32_t n = 0;
        int rc = mtp_get_object_prop_value(&dev->ptp, *parent, props[i], &data, &n);
        int valid = !rc && data && (i ? n == 2 && hd_art_u16(data) == MTP_OFC_Association : n == 4 && hd_art_u32(data) == dev->storage_id);
        free(data); if (!valid) return -1;
    }
    return 0;
}

ZuneHDArtStatus zune_hd_art_ensure_artist(ZuneDevice *dev, const char *name,
    const char *catalog_guid, ZuneHDArtistResult *result) {
    zune_clear_error();
    uint8_t guid[16], name_wire[511]; size_t name_size = 0;
    if (!result) return ZUNE_HD_ART_INVALID;
    memset(result, 0, sizeof(*result));
    if (!dev || hd_name(name) || hd_guid(catalog_guid, guid) || hd_name_wire(name, name_wire, &name_size)) return ZUNE_HD_ART_INVALID;
    if (dev->device_family != ZUNE_FAMILY_PAVO) return ZUNE_HD_ART_UNSUPPORTED;
    if (ZUNE_FLAG_GET(dev->cancel_requested)) return ZUNE_HD_ART_CANCELLED;
    if (!dev->mtpz_authenticated || dev->hd_transport_uncertain || ZUNE_FLAG_GET(dev->disconnecting) || !dev->storage_id || !dev->serial || !*dev->serial) return ZUNE_HD_ART_INVALID;
    uint32_t *ids = NULL; int count = 0; uint32_t found = 0; int conflict = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_AbstractArtist, 0, &ids, &count) ||
        count < 0 || count > 65536 || (count && !ids)) { free(ids); goto failure; }
    for (int i = 0; i < count; i++) {
        if (!ids[i] || ids[i] == UINT32_MAX) { free(ids); goto failure; }
        for (int j = 0; j < i; j++) if (ids[j] == ids[i]) { free(ids); goto failure; }
        uint8_t *data = NULL; uint32_t length = 0;
        if (ZUNE_FLAG_GET(dev->cancel_requested) || mtp_get_object_prop_value(&dev->ptp, ids[i], MTP_OPC_Name, &data, &length)) { free(data); free(ids); goto failure; }
        char *candidate = hd_read_name(data, length); free(data); data = NULL;
        if (!candidate || !*candidate) { free(candidate); free(ids); goto failure; }
        int same = hd_equal_name(candidate, name); free(candidate);
        if (mtp_get_object_prop_value(&dev->ptp, ids[i], 0xda97, &data, &length) || !data || length != 16) { free(data); free(ids); goto failure; }
        int same_guid = !memcmp(data, guid, 16); free(data);
        if (same) {
            if (found || !same_guid) conflict = 1;
            found = ids[i];
        } else if (same_guid) conflict = 1;
    }
    free(ids);
    if (conflict) { zune_set_error("Artist catalog identity conflicts with existing device objects; no changes made"); return ZUNE_HD_ART_IDENTITY_CONFLICT; }
    if (ZUNE_FLAG_GET(dev->cancel_requested)) return ZUNE_HD_ART_CANCELLED;
    if (found) { result->handle = found; return ZUNE_HD_ART_OK; }
    uint8_t props[1200] = {4,0,0,0}, filename[511], zero = 0;
    uint32_t parent = 0;
    if (hd_artist_parent(dev, &parent)) goto failure;
    char filename_text[261]; size_t pos = 4, filename_size = 0;
    snprintf(filename_text, sizeof(filename_text), "%s.art", name);
    if (hd_name_wire(filename_text, filename, &filename_size)) return ZUNE_HD_ART_INVALID;
    hd_prop(props, &pos, 0xdab0, PTP_DTC_UINT8, &zero, 1);
    hd_prop(props, &pos, MTP_OPC_ObjectFileName, PTP_DTC_STR, filename, filename_size);
    hd_prop(props, &pos, MTP_OPC_Name, PTP_DTC_STR, name_wire, name_size);
    hd_prop(props, &pos, 0xda97, 0x000a, guid, 16);
    uint32_t params[5] = {dev->storage_id,parent,MTP_OFC_AbstractArtist,0,0}, reply[5] = {0};
    int n = 0; uint16_t response = 0;
    if (ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting)) return ZUNE_HD_ART_CANCELLED;
    result->write_attempted = 1;
    int rc = ptp_transaction(&dev->ptp, MTP_OC_SendObjectPropList, params, 5,
            PTP_DP_SENDDATA, props, pos, NULL, NULL, &response, reply, &n);
    if (n >= 3 && n <= 5) result->handle = reply[2];
    if (rc || response != PTP_RC_OK || (n < 3 || n > 5) || reply[0] != dev->storage_id || reply[1] != parent || !reply[2] || reply[2] == UINT32_MAX) {
        result->transport_uncertain = hd_cli_transaction_uncertain(rc, response) || (response == PTP_RC_OK);
        goto failure;
    }
    result->created = 1;
    if (ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting)) { result->transport_uncertain = 1; goto failure; }
    if (mtp_send_object(&dev->ptp, NULL, 0)) goto failure;
    /* Exact readback is required. Never delete or retry an ambiguous creation. */
    uint8_t *data = NULL; uint32_t length = 0;
    if (mtp_get_object_prop_value(&dev->ptp, result->handle, MTP_OPC_Name, &data, &length) ||
        length != name_size || !data || memcmp(data, name_wire, name_size)) { free(data); goto failure; }
    free(data); data = NULL;
    if (mtp_get_object_prop_value(&dev->ptp, result->handle, 0xda97, &data, &length) ||
        length != 16 || !data || memcmp(data, guid, 16)) { free(data); goto failure; }
    free(data);
    if (ZUNE_FLAG_GET(dev->cancel_requested)) return ZUNE_HD_ART_CANCELLED;
    return ZUNE_HD_ART_OK;
failure:
    if (hd_cli_transaction_uncertain(-1, dev->ptp.last_response)) result->transport_uncertain = 1;
    if (result->transport_uncertain) dev->hd_transport_uncertain = 1;
    zune_set_error("HD artist lookup/create could not be verified; preserve returned handle and do not blindly retry");
    return ZUNE_FLAG_GET(dev->cancel_requested) ? ZUNE_HD_ART_CANCELLED : ZUNE_HD_ART_IO_ERROR;
}

/* Identity-sensitive consumers need a complete snapshot, not the legacy
 * best-effort browser inventory. This operation never mutates device state. */
int zune_get_albums_checked(ZuneDevice *dev, ZuneAlbumObject **out, int *count) {
    zune_clear_error();
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!dev || !out || !count || ZUNE_FLAG_GET(dev->disconnecting) || ZUNE_FLAG_GET(dev->cancel_requested) || dev->hd_transport_uncertain) return ZUNE_BLOWN;
    uint32_t *ids = NULL; int n = 0; ZuneAlbumObject *albums = NULL;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, MTP_OFC_AbstractAudioAlbum, 0, &ids, &n) ||
        n < 0 || n > 65536 || (n && !ids)) goto fail;
    if (!n) { free(ids); return ZUNE_OK; }
    albums = calloc((size_t)n, sizeof(*albums));
    if (!albums) goto fail;
    for (int i = 0; i < n; i++) {
        if (!ids[i] || ids[i] == UINT32_MAX || ZUNE_FLAG_GET(dev->cancel_requested)) goto fail;
        for (int j = 0; j < i; j++) if (ids[j] == ids[i]) goto fail;
        albums[i].album_id = ids[i];
        const uint16_t props[2] = {MTP_OPC_Name, MTP_OPC_Artist};
        for (unsigned j = 0; j < 2; j++) {
            uint8_t *data = NULL; uint32_t size = 0;
            int rc = mtp_get_object_prop_value(&dev->ptp, ids[i], props[j], &data, &size);
            char *text = !rc ? hd_read_name(data, size) : NULL;
            free(data);
            if (!text || !*text) { free(text); goto fail; }
            if (!j) albums[i].name = text; else albums[i].artist = text;
        }
        uint32_t *refs = NULL; int refs_count = 0;
        if (mtp_get_object_references(&dev->ptp, ids[i], &refs, &refs_count) ||
            refs_count < 0 || refs_count > 1000000 || (refs_count && !refs)) { free(refs); goto fail; }
        for (int j = 0; j < refs_count; j++) if (!refs[j] || refs[j] == UINT32_MAX) { free(refs); goto fail; }
        albums[i].track_ids = refs; albums[i].track_count = (uint32_t)refs_count;
    }
    free(ids);
    if (ZUNE_FLAG_GET(dev->cancel_requested) || ZUNE_FLAG_GET(dev->disconnecting)) { zune_free_albums(albums, n); return ZUNE_BLOWN; }
    *out = albums; *count = n; return ZUNE_OK;
fail:
    free(ids); zune_free_albums(albums, n > 0 && n <= 65536 ? n : 0);
    zune_set_error("Could not read complete album identities and references; creation must be deferred");
    return ZUNE_BLOWN;
}
