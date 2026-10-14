/* Exercise production audiobook operations and MTP encoding with a fake PTP
 * transport. No USB backend, device session or user library is involved. */
#include "../src/zune_internal.h"
#include <assert.h>

typedef struct { uint16_t code, type; uint8_t *data; uint32_t size; } Property;
typedef struct {
    uint32_t id, parent, size;
    uint16_t format;
    char filename[128];
    int count;
    Property props[8];
} Object;
static Object objects[16];
static int used, creates, payloads, folders, calls, checks, wildcard_reads;
static uint16_t fail_op, fail_prop, corrupt_prop;
static int zero_category, wrong_schema, wrong_descriptor, bad_handles;
static char error_text[512];
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t read16(const uint8_t *p) { return p[0] | p[1] << 8; }
static void put32(uint8_t *p, uint32_t value) { for (int i = 0; i < 4; ++i) p[i] = value >> (8 * i); }
void zune_set_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(error_text, sizeof(error_text), fmt, ap); va_end(ap);
}
const char *zune_get_error(void) { return error_text; }
static void check(int ok, const char *label) {
    if (!ok) { fprintf(stderr, "FAIL: %s (%s)\n", label, error_text); exit(1); }
    ++checks; printf("PASS: %s\n", label);
}
static Object *object(uint32_t id) {
    for (int i = 0; i < used; ++i) if (objects[i].id == id) return &objects[i];
    return NULL;
}
static void duplicate(uint32_t id) {
    Object *source = object(id); assert(source && used < 16);
    Object *copy = &objects[used++]; *copy = *source;
    copy->id = 0x12000000u | (uint32_t)used;
    for (int i = 0; i < copy->count; ++i) {
        copy->props[i].data = malloc(source->props[i].size); assert(copy->props[i].data);
        memcpy(copy->props[i].data, source->props[i].data, source->props[i].size);
    }
}
static Property *property(Object *o, uint16_t code) {
    if (o) for (int i = 0; i < o->count; ++i) if (o->props[i].code == code) return &o->props[i];
    return NULL;
}
static void reply(const void *data, uint32_t size, uint8_t **out, uint32_t *length) {
    *out = malloc(size ? size : 1); assert(*out);
    if (size) memcpy(*out, data, size);
    *length = size;
}
static void reset(void) {
    for (int i = 0; i < used; ++i)
        for (int j = 0; j < objects[i].count; ++j) free(objects[i].props[j].data);
    memset(objects, 0, sizeof(objects));
    used = creates = payloads = folders = calls = wildcard_reads = 0;
    fail_op = fail_prop = corrupt_prop = 0;
    zero_category = wrong_schema = wrong_descriptor = bad_handles = 0;
    error_text[0] = 0;
}
/* Folder creation is a separate public API. Model its observable result without
 * linking a USB backend; the production sender chooses its exact parent/name. */
uint32_t zune_forge_folder(ZuneDevice *d, const char *name, uint32_t parent) {
    (void)d; assert(used < 16);
    Object *o = &objects[used++];
    o->id = 0x07000000u | (uint32_t)used;
    o->format = MTP_OFC_Association; o->parent = parent;
    snprintf(o->filename, sizeof(o->filename), "%s", name);
    ++folders; return o->id;
}
static uint16_t type_for(uint16_t code) {
    if (code == 0xDC8B || code == 0xDC95) return PTP_DTC_UINT16;
    if (code == 0xDC89) return PTP_DTC_UINT32;
    return PTP_DTC_STR;
}
int ptp_transaction(ptp_session_t *s, uint16_t op, uint32_t *params, int nparams,
                    uint16_t flags, uint8_t *send, uint64_t send_len,
                    uint8_t **recv, uint32_t *recv_len, uint16_t *response,
                    uint32_t *rp, int *rn) {
    ++calls;
    if (recv) *recv = NULL;
    if (recv_len) *recv_len = 0;
    s->last_response = (op == fail_op || (op == MTP_OC_GetObjectPropValue && params[1] == fail_prop))
        ? 0x2002 : PTP_RC_OK;
    if (response) *response = s->last_response;
    if (s->last_response != PTP_RC_OK) return -1;
    switch (op) {
    case PTP_OC_GetObjectHandles: {
        assert(nparams == 3 && flags == PTP_DP_GETDATA);
        if (!params[1]) ++wildcard_reads;
        uint8_t bytes[68] = {0}; uint32_t count = 0;
        if (bad_handles) { put32(bytes, 3); reply(bytes, 4, recv, recv_len); return 0; }
        for (int i = 0; i < used; ++i) {
            /* Reproduce Pavo's omission from MP3-filtered inventory. */
            if (params[1] == MTP_OFC_MP3) continue;
            if ((!params[1] || params[1] == objects[i].format)
                && (!params[2] || objects[i].parent == params[2]))
                put32(bytes + 4 + 4 * count++, objects[i].id);
        }
        put32(bytes, count); reply(bytes, 4 + 4 * count, recv, recv_len); return 0;
    }
    case PTP_OC_GetObjectInfo: {
        Object *o = object(params[0]); assert(o);
        uint32_t length = 0;
        uint8_t *name = mtp_string_to_ucs2(o->filename, &length); assert(name);
        uint8_t bytes[1024] = {0}; assert(length + 55 < sizeof(bytes));
        put32(bytes, 1); bytes[4] = o->format; bytes[5] = o->format >> 8;
        put32(bytes + 8, o->size); put32(bytes + 38, o->parent);
        memcpy(bytes + 52, name, length); free(name);
        reply(bytes, 55 + length, recv, recv_len); return 0;
    }
    case MTP_OC_GetObjectPropDesc: {
        uint16_t type = type_for(params[0]);
        uint8_t bytes[16] = {params[0] & 255, params[0] >> 8, type & 255, type >> 8, !wrong_descriptor};
        reply(bytes, sizeof(bytes), recv, recv_len); return 0;
    }
    case MTP_OC_SendObjectPropList: {
        assert(nparams == 5 && flags == PTP_DP_SENDDATA && used < 16 && params[3] == 0);
        Object *o = &objects[used++];
        o->id = (wrong_schema ? 0x01000000u : 0x12000000u) | (uint32_t)used;
        o->parent = params[1]; o->format = params[2]; o->size = params[4]; o->count = read32(send);
        assert(o->count == 7);
        uint64_t at = 4;
        for (int i = 0; i < o->count; ++i) {
            assert(at + 8 <= send_len && read32(send + at) == 0);
            Property *p = &o->props[i]; p->code = read16(send + at + 4); p->type = read16(send + at + 6); at += 8;
            assert(at < send_len);
            p->size = p->type == PTP_DTC_STR ? 1 + 2 * send[at] : p->type == PTP_DTC_UINT16 ? 2 : 4;
            assert(at + p->size <= send_len);
            p->data = malloc(p->size); assert(p->data); memcpy(p->data, send + at, p->size); at += p->size;
            if (p->code == 0xDC07) {
                char *name = mtp_ucs2_to_string(p->data, p->size); assert(name);
                snprintf(o->filename, sizeof(o->filename), "%s", name); free(name);
            }
        }
        assert(at == send_len); ++creates;
        rp[0] = 1; rp[1] = o->parent; rp[2] = o->id; *rn = 3; return 0;
    }
    case PTP_OC_SendObject:
        assert(used && send_len == objects[used - 1].size); ++payloads; return 0;
    case MTP_OC_GetObjectPropValue: {
        Property *p = property(object(params[0]), params[1]); assert(p);
        reply(p->data, p->size, recv, recv_len);
        if (zero_category && params[1] == 0xDC95) memset(*recv, 0, p->size);
        if (corrupt_prop == params[1]) (*recv)[p->type == PTP_DTC_STR ? 1 : 0] ^= 1;
        return 0;
    }
    default: fprintf(stderr, "Unexpected operation 0x%04X\n", op); abort();
    }
}
int main(void) {
    char path[] = "/tmp/libzune-audiobook-protocol.XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    const char audio[] = "prepared MP3 fixture";
    assert(write(fd, audio, sizeof(audio)) == sizeof(audio)); close(fd);
    ZuneDevice dev = {0}; dev.storage_id = 1; dev.mtpz_authenticated = 1;
    ZuneAudiobookPartInput part = {
        .book_key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .book_title = "A Book", .title = "Chapter One", .author = "An Author",
        .part_number = 1, .duration_ms = 60000
    };
    const ZuneDeviceFamily families[] = {ZUNE_FAMILY_KEEL, ZUNE_FAMILY_PAVO, ZUNE_FAMILY_DRACO};
    for (size_t f = 0; f < sizeof(families) / sizeof(*families); ++f) {
        reset(); dev.device_family = families[f]; zero_category = 1;
        uint32_t id = 0, folder = 0, found = 0;
        check(!zune_find_audiobook_part(&dev, &part, &folder, &found) && !folder && !found && !creates && !folders,
              "supported family empty lookup succeeds without writes");
        check(!zune_smuggle_audiobook_part(&dev, path, &part, &id) && id >> 24 == 0x12 && creates == 1 && payloads == 1 && folders == 2,
              "supported family sends native book with firmware zero category");
        check(!zune_find_audiobook_part(&dev, &part, &folder, &found) && found == id && folder == object(id)->parent && wildcard_reads > 0,
              "lookup uses wildcard inventory and exact book part identity");
        uint32_t again = 0;
        check(!zune_smuggle_audiobook_part(&dev, path, &part, &again) && again == id && creates == 1 && payloads == 1 && folders == 2,
              "repeat send reuses verified part without folder or payload writes");
        zero_category = 0;
        check(!zune_find_audiobook_part(&dev, &part, &folder, &found) && found == id,
              "declared audiobook category 18 remains accepted");
        const uint16_t metadata[] = {0xDA9A, 0xDC44, 0xDC46, 0xDC89, 0xDC95};
        for (size_t m = 0; m < sizeof(metadata) / sizeof(*metadata); ++m) {
            corrupt_prop = metadata[m]; again = 99;
            check(zune_smuggle_audiobook_part(&dev, path, &part, &again) < 0 && !again && creates == 1 && payloads == 1 && folders == 2,
                  "existing mismatched book, chapter, author, duration or category prevents writes");
        }
        corrupt_prop = 0;
        for (size_t m = 0; m < sizeof(metadata) / sizeof(*metadata); ++m) {
            fail_prop = metadata[m]; again = 99;
            check(zune_smuggle_audiobook_part(&dev, path, &part, &again) < 0 && !again && creates == 1 && payloads == 1 && folders == 2,
                  "unreadable existing metadata cannot be treated as absent");
        }
        fail_prop = 0; object(id)->size++;
        check(zune_smuggle_audiobook_part(&dev, path, &part, &again) < 0 && !again && creates == 1 && payloads == 1,
              "existing byte-size mismatch blocks reuse without another upload");
        object(id)->size--;
        const uint16_t failures[] = {PTP_OC_GetObjectHandles, PTP_OC_GetObjectInfo};
        for (size_t m = 0; m < sizeof(failures) / sizeof(*failures); ++m) {
            fail_op = failures[m]; again = 99;
            check(zune_smuggle_audiobook_part(&dev, path, &part, &again) < 0 && !again && creates == 1 && payloads == 1 && folders == 2,
                  "inventory and object-info failure cannot create a replacement");
        }
        fail_op = 0;
        ZuneAudiobookPartInput second = part;
        second.part_number = 2; second.title = "Chapter Two";
        uint32_t next = 0;
        check(!zune_smuggle_audiobook_part(&dev, path, &second, &next)
            && next != id && object(next)->parent == object(id)->parent
            && creates == 2 && payloads == 2 && folders == 2,
              "second chapter keeps the same book folder and distinct part identity");
        check(!zune_find_audiobook_part(&dev, &part, &folder, &found) && found == id
            && !zune_find_audiobook_part(&dev, &second, &folder, &found) && found == next,
              "multipart lookup resolves each chapter by its ordered identity");
        duplicate(id); again = 99;
        check(zune_smuggle_audiobook_part(&dev, path, &part, &again) < 0 && !again
            && creates == 2 && payloads == 2 && folders == 2,
              "ambiguous duplicate part IDs block reuse and replacement writes");
    }
    dev.device_family = ZUNE_FAMILY_DRACO;
    for (int mode = 0; mode < 4; ++mode) {
        reset(); uint32_t id = 99;
        if (mode == 0) wrong_schema = 1;
        if (mode == 1) corrupt_prop = 0xDC95;
        if (mode == 2) corrupt_prop = 0xDC44;
        if (mode == 3) fail_op = PTP_OC_SendObject;
        check(zune_smuggle_audiobook_part(&dev, path, &part, &id) < 0 && id && object(id) && creates == 1,
              "failed new schema, category, title or payload retains exact recovery ID and never deletes");
    }
    for (int mode = 0; mode < 3; ++mode) {
        reset(); uint32_t id = 99;
        if (mode == 0) wrong_descriptor = 1;
        if (mode == 1) bad_handles = 1;
        if (mode == 2) fail_op = PTP_OC_GetObjectHandles;
        check(zune_smuggle_audiobook_part(&dev, path, &part, &id) < 0 && !id && !creates && !payloads && !folders,
              "failed preflight or incomplete inventory has no device writes");
    }
    for (int mode = 0; mode < 4; ++mode) {
        reset(); uint32_t id = 99;
        dev.device_family = mode == 0 ? ZUNE_FAMILY_UNKNOWN : ZUNE_FAMILY_DRACO;
        dev.mtpz_authenticated = mode != 1; dev.cancel_requested = mode == 2; dev.disconnecting = mode == 3;
        check(zune_smuggle_audiobook_part(&dev, path, &part, &id) < 0 && !id && !calls && !folders,
              "unknown family, missing auth, cancellation and disconnect reject before I/O");
    }
    unlink(path); reset(); printf("Audiobook protocol: %d checks passed; no USB\n", checks);
    return 0;
}
