/* T3.7: the shared MTP property helpers (mtp.h), against a fake MTP layer:
 * the descriptor preflight used by podcast and audiobook uploads, and the
 * string-property reader used by track, video, album and playlist code. */
#include "../src/zune_internal.h"

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", name); }
    else printf("PASS: %s\n", name);
}

static int fail_next;
static uint8_t reply[32];
static uint32_t reply_len;

int mtp_get_prop_desc(ptp_session_t *s, uint16_t prop_code, uint16_t format, uint8_t **data, uint32_t *len) {
    (void)s; (void)prop_code; (void)format;
    *data = NULL; *len = 0;
    if (fail_next) return -1;
    *data = malloc(reply_len ? reply_len : 1);
    memcpy(*data, reply, reply_len);
    *len = reply_len;
    return 0;
}

int mtp_get_object_prop_value(ptp_session_t *s, uint32_t handle, uint16_t prop_code, uint8_t **data,
                              uint32_t *len) {
    (void)s; (void)handle; (void)prop_code;
    return mtp_get_prop_desc(NULL, 0, 0, data, len);
}

char *mtp_ucs2_to_string(const uint8_t *data, uint32_t len) {
    /* Enough of the real decoder for ASCII: count, UCS-2 units, NUL. */
    if (!len || !data[0]) return strdup("");
    char *out = calloc(data[0], 1);
    for (uint32_t i = 0; i + 1 < data[0] && 2 + 2 * i < len; i++) out[i] = (char)data[1 + 2 * i];
    return out;
}

static void desc(uint16_t prop, uint16_t type, uint8_t getset, uint32_t len) {
    fail_next = 0;
    zune_put_le16(reply, prop);
    zune_put_le16(reply + 2, type);
    reply[4] = getset;
    reply_len = len;
}

int main(void) {
    desc(0xDC44, PTP_DTC_STR, 1, 5);
    check(mtp_prop_desc_writable(NULL, 0xDC44, MTP_OFC_MP3, PTP_DTC_STR), "a writable descriptor of the right type passes");
    desc(0xDC46, PTP_DTC_STR, 1, 5);
    check(!mtp_prop_desc_writable(NULL, 0xDC44, MTP_OFC_MP3, PTP_DTC_STR), "a descriptor for another property fails");
    desc(0xDC44, PTP_DTC_UINT16, 1, 5);
    check(!mtp_prop_desc_writable(NULL, 0xDC44, MTP_OFC_MP3, PTP_DTC_STR), "a descriptor of another type fails");
    desc(0xDC44, PTP_DTC_STR, 0, 5);
    check(!mtp_prop_desc_writable(NULL, 0xDC44, MTP_OFC_MP3, PTP_DTC_STR), "a read-only descriptor fails");
    desc(0xDC44, PTP_DTC_STR, 1, 4);
    check(!mtp_prop_desc_writable(NULL, 0xDC44, MTP_OFC_MP3, PTP_DTC_STR), "a truncated descriptor fails");
    fail_next = 1;
    check(!mtp_prop_desc_writable(NULL, 0xDC44, MTP_OFC_MP3, PTP_DTC_STR), "an unanswered descriptor request fails");

    const uint8_t hi[] = { 3, 'h', 0, 'i', 0, 0, 0 };
    fail_next = 0; memcpy(reply, hi, sizeof(hi)); reply_len = sizeof(hi);
    char *text = mtp_read_string_prop(NULL, 1, 0xDC44);
    check(text && !strcmp(text, "hi"), "a string property is decoded");
    free(text);
    reply_len = 0;
    check(mtp_read_string_prop(NULL, 1, 0xDC44) == NULL, "an empty reply reads as NULL and its buffer is freed");
    fail_next = 1;
    check(mtp_read_string_prop(NULL, 1, 0xDC44) == NULL, "a failed read is NULL");
    printf("MTP helpers: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
