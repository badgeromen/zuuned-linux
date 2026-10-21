/* zune_get_brand_limits against the real thumbnail/MTP code, with
 * ptp_transaction replaced by a device that answers GetObjectInfo and
 * GetObjectPropDesc. Descriptor bytes for MP3 and MP4 are the Zune 30's own
 * (October 20, 2026): 240x240 and 1 MiB for audio, no pixel limit for MP4. */
#include "../src/zune_internal.h"
#include <assert.h>
#include <stdarg.h>

static int checks, failures;
static char last_error[1024];

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
}
void zune_clear_error(void) { last_error[0] = 0; }
const char *zune_get_error(void) { return last_error[0] ? last_error : NULL; }
const char *zune_autopsy_name(uint16_t code) { (void)code; return "fixture"; }
int zune_is_aborted(ZuneDevice *dev) { return dev->cancel_requested; }
int zune_unjam(ZuneDevice *dev) { (void)dev; return 0; }

static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}

/* Zune 30 (Keel) descriptors, captured from GetObjectPropDesc. */
static const uint8_t keel_width[] = { 0x84,0xDC, 0x06,0x00, 0x00, 0,0,0,0, 0,0,0,0, 0x01,
                                      0,0,0,0, 0xF0,0,0,0, 1,0,0,0 };
static const uint8_t keel_height[] = { 0x83,0xDC, 0x06,0x00, 0x00, 0,0,0,0, 0,0,0,0, 0x01,
                                       0,0,0,0, 0xF0,0,0,0, 1,0,0,0 };
static const uint8_t keel_data[] = { 0x86,0xDC, 0x02,0x40, 0x01, 0,0,0,0, 0,0,0,0, 0x06,
                                     0x00,0x00,0x10,0x00 };
static const uint8_t keel_mp4_width[] = { 0x84,0xDC, 0x06,0x00, 0x00, 0,0,0,0, 0,0,0,0, 0x00 };
static const uint8_t keel_mp4_height[] = { 0x83,0xDC, 0x06,0x00, 0x00, 0,0,0,0, 0,0,0,0, 0x00 };

enum Damage { INTACT, TRUNCATED_RANGE, WRONG_PROP, WRONG_TYPE, HUGE_DEFAULT, NO_LENGTH,
              DEFAULT_BYTES, TRANSPORT, INFO_FAILS };
static enum Damage damage;
static int info_queries, desc_queries;

static uint16_t format_of(uint32_t handle) {
    switch (handle >> 24) {
    case 0x12: return MTP_OFC_MP3;
    case 0x0F: return 0xBA0B;               /* podcast series */
    case 0x03: return MTP_OFC_MP4;
    case 0x09: return MTP_OFC_JPEG;         /* answers no artwork properties */
    case 0x0B: return MTP_OFC_WMA;          /* damaged per scenario */
    default: return (uint16_t)(0xB000 + (handle >> 24));   /* generic formats */
    }
}

static uint8_t *copy(const uint8_t *src, uint32_t n, uint32_t *len) {
    uint8_t *d = malloc(n);
    assert(d);
    memcpy(d, src, n);
    *len = n;
    return d;
}

/* A plausible descriptor: range 0..max for UINT32, byte array max for AUINT8. */
static uint8_t *made(uint16_t prop, uint32_t max, uint32_t *len) {
    uint8_t d[26] = { (uint8_t)prop, (uint8_t)(prop >> 8) };
    if (prop == 0xDC86) {
        d[2] = 0x02; d[3] = 0x40; d[4] = 1; d[13] = 0x06;
        memcpy(d + 14, &max, 4);
        return copy(d, 18, len);
    }
    d[2] = 0x06; d[13] = 0x01;
    memcpy(d + 18, &max, 4);
    d[22] = 1;
    return copy(d, 26, len);
}

int ptp_transaction(ptp_session_t *s, uint16_t opcode, uint32_t *params, int nparams,
                    uint16_t flags, uint8_t *send, uint64_t send_len, uint8_t **data,
                    uint32_t *length, uint16_t *response, uint32_t *response_params,
                    int *response_count) {
    (void)send; (void)send_len; (void)response_params; (void)response_count;
    assert(flags == PTP_DP_GETDATA);
    *data = NULL;
    *length = 0;
    s->last_response = *response = PTP_RC_OK;
    if (opcode == PTP_OC_GetObjectInfo) {
        assert(nparams == 1);
        ++info_queries;
        if (damage == INFO_FAILS) { s->last_response = *response = PTP_RC_InvalidObjectHandle; return -1; }
        uint16_t format = format_of(params[0]);
        *length = 55;
        *data = calloc(1, *length);
        assert(*data);
        (*data)[4] = (uint8_t)format;
        (*data)[5] = (uint8_t)(format >> 8);
        return 0;
    }
    assert(opcode == MTP_OC_GetObjectPropDesc && nparams == 2);
    ++desc_queries;
    uint16_t prop = (uint16_t)params[0], format = (uint16_t)params[1];
    assert(prop == 0xDC83 || prop == 0xDC84 || prop == 0xDC86);
    if (format == MTP_OFC_JPEG) {
        s->last_response = *response = PTP_RC_MTP_ObjectPropNotSupported;
        return -1;
    }
    if (format == MTP_OFC_MP3)
        *data = prop == 0xDC84 ? copy(keel_width, sizeof(keel_width), length)
              : prop == 0xDC83 ? copy(keel_height, sizeof(keel_height), length)
              : copy(keel_data, sizeof(keel_data), length);
    else if (format == MTP_OFC_MP4)
        *data = prop == 0xDC84 ? copy(keel_mp4_width, sizeof(keel_mp4_width), length)
              : prop == 0xDC83 ? copy(keel_mp4_height, sizeof(keel_mp4_height), length)
              : copy(keel_data, sizeof(keel_data), length);
    else if (format == 0xBA0B)
        *data = made(prop, prop == 0xDC84 ? 480 : prop == 0xDC83 ? 272 : 524288, length);
    else if (format == MTP_OFC_WMA) {
        if (damage == TRANSPORT && prop == 0xDC86) {
            s->last_response = *response = 0x02FF;
            return -1;
        }
        *data = made(prop, prop == 0xDC86 ? 65536 : 240, length);
        if (damage == TRUNCATED_RANGE && prop == 0xDC83) *length = 21;
        if (damage == WRONG_PROP && prop == 0xDC84) (*data)[0] = 0x83;
        if (damage == WRONG_TYPE && prop == 0xDC84) (*data)[2] = 0x04;
        if (damage == HUGE_DEFAULT && prop == 0xDC86) memset(*data + 5, 0xFF, 4);
        if (damage == NO_LENGTH && prop == 0xDC86) *length = 14;
        if (damage == DEFAULT_BYTES && prop == 0xDC86) {
            /* A three-byte default value before GroupCode and FormFlag. */
            uint8_t d[21] = { 0x86,0xDC, 0x02,0x40, 0x01, 3,0,0,0, 0xAA,0xBB,0xCC,
                              0,0,0,0, 0x06, 0x00,0x00,0x02,0x00 };
            free(*data);
            *data = copy(d, sizeof(d), length);
        }
    } else
        *data = made(prop, 100 + (format & 0xFF), length);
    return 0;
}

static int limits(ZuneDevice *dev, uint32_t item, uint32_t *w, uint32_t *h, uint32_t *b) {
    last_error[0] = 0;
    *w = *h = *b = 7;                       /* must be overwritten on every path */
    return zune_get_brand_limits(dev, item, w, h, b);
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    ZuneDevice *dev = calloc(1, sizeof(*dev));
    assert(dev);
    uint32_t w, h, b;

    int rc = limits(dev, 0x12000001, &w, &h, &b);
    check(rc == 0 && w == 240 && h == 240 && b == 1048576 && desc_queries == 3,
          "Zune 30 audio declares 240x240 and 1 MiB");
    desc_queries = info_queries = 0;
    rc = limits(dev, 0x12000002, &w, &h, &b);
    check(rc == 0 && w == 240 && h == 240 && b == 1048576 && desc_queries == 0 && info_queries == 1,
          "another audio item uses the cached answer");

    desc_queries = 0;
    rc = limits(dev, 0x0F000001, &w, &h, &b);
    check(rc == 0 && w == 480 && h == 272 && b == 524288 && desc_queries == 3,
          "each format is read separately (width 0xDC84, height 0xDC83)");

    rc = limits(dev, 0x03000001, &w, &h, &b);
    check(rc == 0 && w == 0 && h == 0 && b == 1048576, "MP4 declares bytes but no pixel limit");

    rc = limits(dev, 0x09000001, &w, &h, &b);
    check(rc == 0 && w == 0 && h == 0 && b == 0, "unsupported artwork properties declare nothing");

    damage = TRANSPORT;
    desc_queries = 0;
    rc = limits(dev, 0x0B000001, &w, &h, &b);
    check(rc == -1 && w == 0 && h == 0 && b == 0 && strstr(last_error, "0x02FF"),
          "a transport failure fails with zeroed outputs and the code");
    damage = INTACT;
    rc = limits(dev, 0x0B000001, &w, &h, &b);
    check(rc == 0 && w == 240 && b == 65536 && desc_queries == 6, "a failed answer is not cached");

    static const struct { enum Damage damage; const char *name; } broken[] = {
        { TRUNCATED_RANGE, "a truncated range form is refused" },
        { WRONG_PROP, "a descriptor for another property is refused" },
        { WRONG_TYPE, "a width that is not UINT32 is refused" },
        { HUGE_DEFAULT, "a default array longer than the descriptor is refused" },
        { NO_LENGTH, "a byte-array form without its length is refused" },
    };
    for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); i++) {
        ZuneDevice *fresh = calloc(1, sizeof(*fresh));
        assert(fresh);
        damage = broken[i].damage;
        rc = limits(fresh, 0x0B000001, &w, &h, &b);
        check(rc == -1 && w == 0 && h == 0 && b == 0 && strstr(last_error, "Malformed"), broken[i].name);
        free(fresh);
    }

    ZuneDevice *fresh = calloc(1, sizeof(*fresh));
    assert(fresh);
    damage = DEFAULT_BYTES;
    rc = limits(fresh, 0x0B000001, &w, &h, &b);
    check(rc == 0 && b == 131072, "a nonempty default value is skipped before the form");
    damage = INFO_FAILS;
    desc_queries = 0;
    rc = limits(fresh, 0x0B000001, &w, &h, &b);
    check(rc == -1 && w == 0 && b == 0 && desc_queries == 0 && strstr(last_error, "0x2009"),
          "an unreadable item fails before any descriptor query");
    damage = INTACT;
    free(fresh);

    check(zune_get_brand_limits(dev, 0x12000001, &w, NULL, &b) == -1 && w == 0 && b == 0,
          "missing output pointers are refused");
    check(zune_get_brand_limits(NULL, 0x12000001, &w, &h, &b) == -1 && h == 0, "no device is refused");

    /* dev already caches MP3, series, MP4, JPEG and WMA: three more fill it. */
    for (uint32_t f = 0x20; f < 0x23; f++) {
        rc = limits(dev, f << 24, &w, &h, &b);
        assert(rc == 0 && w == 100 + f);
    }
    desc_queries = 0;
    rc = limits(dev, 0x30000000, &w, &h, &b);
    int first = desc_queries;
    rc |= limits(dev, 0x30000000, &w, &h, &b);
    check(rc == 0 && w == 0x30 + 100 && first == 3 && desc_queries == 6 && dev->brand_limits_count == 8,
          "beyond eight formats the answer is still correct, just not cached");

    free(dev);
    printf("Brand limits: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
