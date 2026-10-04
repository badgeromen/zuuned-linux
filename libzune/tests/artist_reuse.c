/* Real artist creation policy, with an in-memory MTP boundary. No USB. */
#include "../src/zune_internal.h"
#include <assert.h>
static const char *names[8];
static uint32_t ids[8];
static int count, creates, commits, deletes, enumeration_fail, name_fail, commit_fail;
void zune_set_error(const char *fmt, ...) {}
const char *zune_autopsy_name(uint16_t code) { return "fixture"; }
int mtp_get_props_supported(ptp_session_t *s, uint16_t f, uint16_t **p, int *n) {
    *p = malloc(sizeof(uint16_t)); **p = MTP_OPC_Name; *n = 1; return 0;
}
int mtp_get_object_handles(ptp_session_t *s, uint32_t storage, uint32_t format,
                          uint32_t parent, uint32_t **h, int *n) {
    assert(format == MTP_OFC_AbstractArtist);
    if (enumeration_fail) return -1;
    *n = count; *h = malloc((count + 1) * sizeof(uint32_t));
    memcpy(*h, ids, count * sizeof(uint32_t)); return 0;
}
int mtp_get_object_prop_value(ptp_session_t *s, uint32_t h, uint16_t p, uint8_t **d, uint32_t *n) {
    assert(p == MTP_OPC_Name);
    if (name_fail) return -1;
    for (int i = 0; i < count; i++) if (ids[i] == h) {
        *d = (uint8_t *)strdup(names[i]); *n = strlen(names[i]) + 1; return 0;
    }
    return -1;
}
/* Boundary fixture supplies decoded UTF-8; UCS-2 parsing is not under test. */
char *mtp_ucs2_to_string(const uint8_t *d, uint32_t n) { return strdup((const char *)d); }
int mtp_send_object_prop_list(ptp_session_t *s, uint32_t storage, uint32_t parent,
    uint16_t format, uint64_t size, mtp_prop_entry_t *p, int n, uint32_t *h) {
    assert(format == MTP_OFC_AbstractArtist && size == 0);
    creates++; *h = 100 + count;
    for (int i = 0; i < n; i++) if (p[i].prop_code == MTP_OPC_Name) {
        names[count] = strdup(p[i].value.str); ids[count++] = *h; return 0;
    }
    abort();
}
int mtp_send_object(ptp_session_t *s, const uint8_t *d, uint32_t n) {
    assert(n == 0); commits++; return commit_fail ? -1 : 0;
}
int mtp_delete_object(ptp_session_t *s, uint32_t h) { deletes++; return 0; }
static int checks;
static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", what); exit(1); }
    printf("PASS %s\n", what); checks++;
}
int main(void) {
    ZuneDevice dev; memset(&dev, 0, sizeof(dev)); dev.storage_id = 1;
    names[0] = "Selena Gomez"; ids[0] = 22;
    names[1] = "Zweihänder"; ids[1] = 17; count = 2;
    check(zune_forge_artist(&dev, "Selena Gomez") == 22 && !creates, "existing Selena artist is reused");
    check(zune_forge_artist(&dev, "Zweihänder") == 17 && !creates, "UTF-8 artist is reused without accent loss");
    check(zune_forge_artist(&dev, " selena GOMEZ ") == 22 && !creates, "ASCII case and outer whitespace do not duplicate artist");
    names[2] = "Selena Gomez"; ids[2] = 12; count++;
    check(zune_forge_artist(&dev, "Selena Gomez") == 12 && !deletes, "existing duplicates select lowest handle without deleting records");
    enumeration_fail = 1;
    check(zune_forge_artist(&dev, "New Artist") == 0 && !creates, "failed enumeration cannot cause creation");
    enumeration_fail = 0; name_fail = 1;
    check(zune_forge_artist(&dev, "New Artist") == 0 && !creates, "unreadable artist name cannot cause creation");
    name_fail = 0; dev.cancel_requested = 1;
    check(zune_forge_artist(&dev, "New Artist") == 0 && !creates, "cancelled lookup cannot cause creation");
    dev.cancel_requested = 0;
    uint32_t fresh = zune_forge_artist(&dev, "New Artist");
    check(fresh != 0 && creates == 1 && commits == 1, "absent artist is created with zero-byte commit");
    check(zune_forge_artist(&dev, "New Artist") == fresh && creates == 1, "second sync reuses newly created artist");
    commit_fail = 1;
    check(zune_forge_artist(&dev, "Commit failure") == 0 && deletes == 1, "failed new artist commit still cleans its orphan");
    for (int i = 3; i < count; i++) free((void *)names[i]);
    printf("Artist reuse: %d checks passed; no USB\n", checks);
}
