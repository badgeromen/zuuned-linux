/* Real video/metadata/MTP code, with the PTP transaction boundary replaced.
 * No USB backend is linked. Including zmdb.c gives the fixture access to its
 * actual record parser; unreferenced device operations are linker-discarded. */
#include "../src/zune_internal.h"
#include "../src/zmdb.c"
#include <assert.h>

enum { ITEM = 42 };
typedef struct { uint16_t code; uint8_t *data; uint32_t len; } Property;
static Property properties[16];
static int property_count, creates, uploads, deletes, writes, exists;
static uint16_t fail_opcode, fail_property;
static uint8_t *object_info;
static uint32_t object_info_len;
static char last_error[512];
static int checks;
static const char payload[] = "fixture video bytes";
static const uint8_t poster[] = {0xff, 0xd8, 0xff, 0xd9};

static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
    printf("PASS: %s\n", name);
    checks++;
}

static Property *property(uint16_t code) {
    for (int i = 0; i < property_count; i++)
        if (properties[i].code == code) return &properties[i];
    return NULL;
}

static void put_property(uint16_t code, const uint8_t *data, uint32_t len) {
    Property *p = property(code);
    if (!p) {
        assert(property_count < 16);
        p = &properties[property_count++];
        p->code = code;
    }
    free(p->data);
    p->data = malloc(len);
    assert(p->data);
    memcpy(p->data, data, len);
    p->len = len;
}

static void put_string(uint16_t code, const char *value) {
    uint32_t len = 0;
    uint8_t *wire = mtp_string_to_ucs2(value, &len);
    assert(wire);
    put_property(code, wire, len);
    free(wire);
}

static int string_equals(uint16_t code, const char *expected) {
    Property *p = property(code);
    if (!p) return 0;
    char *actual = mtp_ucs2_to_string(p->data, p->len);
    int same = actual && strcmp(actual, expected) == 0;
    free(actual);
    return same;
}

static int number_equals(uint16_t code, uint32_t expected, uint32_t bytes) {
    Property *p = property(code);
    if (!p || p->len != bytes) return 0;
    return (bytes == 2 ? rd16(p->data) : rd32(p->data)) == expected;
}

static void reset_transport(void) {
    for (int i = 0; i < property_count; i++) free(properties[i].data);
    memset(properties, 0, sizeof(properties));
    property_count = creates = uploads = deletes = writes = exists = 0;
    fail_opcode = fail_property = 0;
    free(object_info);
    object_info = NULL;
    object_info_len = 0;
    last_error[0] = 0;
}

static void copy_reply(const uint8_t *data, uint32_t len,
                       uint8_t **out, uint32_t *out_len) {
    assert(out && out_len);
    *out = malloc(len);
    assert(*out);
    memcpy(*out, data, len);
    *out_len = len;
}

int ptp_transaction(ptp_session_t *s, uint16_t opcode,
                    uint32_t *params, int nparams, uint16_t flags,
                    uint8_t *send_data, uint64_t send_len,
                    uint8_t **recv_data, uint32_t *recv_len,
                    uint16_t *response_code,
                    uint32_t *resp_params, int *resp_nparams) {
    (void)flags;
    if (recv_data) *recv_data = NULL;
    if (recv_len) *recv_len = 0;
    int fail = opcode == fail_opcode
        || (opcode == MTP_OC_SetObjectPropValue && params[1] == fail_property);
    s->last_response = fail ? 0x200f : PTP_RC_OK;
    if (response_code) *response_code = s->last_response;
    if (fail) return -1;

    switch (opcode) {
    case PTP_OC_SendObjectInfo:
        assert(nparams == 2 && send_len >= 55);
        creates++;
        exists = 1;
        free(object_info);
        object_info = malloc(send_len);
        assert(object_info);
        memcpy(object_info, send_data, send_len);
        object_info_len = (uint32_t)send_len;
        /* Capture the actual packed ObjectFileName, not an API argument. */
        put_property(MTP_OPC_ObjectFileName, send_data + 52,
                     1u + 2u * send_data[52]);
        assert(resp_params && resp_nparams);
        resp_params[0] = params[0]; resp_params[1] = params[1]; resp_params[2] = ITEM;
        *resp_nparams = 3;
        return 0;
    case PTP_OC_SendObject:
        assert(exists && send_len == sizeof(payload));
        assert(memcmp(send_data, payload, sizeof(payload)) == 0);
        uploads++;
        return 0;
    case MTP_OC_SetObjectPropValue:
        assert(nparams == 2 && params[0] == ITEM && exists);
        writes++;
        put_property((uint16_t)params[1], send_data, (uint32_t)send_len);
        return 0;
    case MTP_OC_GetObjectPropValue: {
        assert(nparams == 2 && params[0] == ITEM && exists);
        Property *p = property((uint16_t)params[1]);
        if (!p) return -1;
        copy_reply(p->data, p->len, recv_data, recv_len);
        return 0;
    }
    case PTP_OC_GetObjectHandles: {
        const uint8_t handles[] = {1,0,0,0, ITEM,0,0,0};
        assert(exists);
        copy_reply(handles, sizeof(handles), recv_data, recv_len);
        return 0;
    }
    case PTP_OC_GetObjectInfo:
        assert(nparams == 1 && params[0] == ITEM && exists);
        copy_reply(object_info, object_info_len, recv_data, recv_len);
        return 0;
    case PTP_OC_DeleteObject:
        assert(params[0] == ITEM);
        deletes++;
        exists = 0;
        return 0;
    default:
        fprintf(stderr, "Unexpected PTP operation: 0x%04x\n", opcode);
        abort();
    }
}

/* Connection/error state only. The production USB/device code is not linked. */
int zune_is_aborted(ZuneDevice *dev) { return dev->cancel_requested; }
int zune_unjam(ZuneDevice *dev) { (void)dev; abort(); }
const char *zune_autopsy_name(uint16_t code) { (void)code; return "fixture refusal"; }
void zune_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, args);
    va_end(args);
}
const char *zune_get_error(void) { return last_error; }

static int send_named(ZuneDevice *dev, const char *path, uint16_t genre,
                       const char *filename, const char *title,
                       const char *series, int season, int episode, uint32_t *id) {
    return zune_smuggle_video_named(dev, path, filename, title, genre,
                                    series, season, episode, "A description",
                                    poster, sizeof(poster), id);
}

int main(void) {
    char path[] = "/tmp/libzune-video-fixture-XXXXXX.wmv";
    int fd = mkstemps(path, 4);
    assert(fd >= 0);
    assert(write(fd, payload, sizeof(payload)) == sizeof(payload));
    close(fd);
    ZuneDevice dev = {0};
    dev.storage_id = 1;
    uint32_t id = 0;

    const char *movie = "Amélie: le fabuleux destin";
    int ret = send_named(&dev, path, ZUNE_METAGENRE_MOVIE,
                          "Amelie - le fabuleux destin.wmv", movie, NULL, 0, 0, &id);
    check(ret == 0 && id == ITEM && creates == 1 && uploads == 1
          && string_equals(MTP_OPC_Name, movie)
          && string_equals(MTP_OPC_ObjectFileName, "Amelie - le fabuleux destin.wmv")
          && number_equals(MTP_OPC_MetaGenre, ZUNE_METAGENRE_MOVIE, 2)
          && !property(ZUNE_OPC_SeriesName), "movie Name stays exact; filename and category remain separate");

    int count = 0;
    ZuneVideoFile *videos = zune_get_videos(&dev, &count);
    check(videos && count == 1 && strcmp(videos[0].title, movie) == 0
          && strcmp(videos[0].object_filename, "Amelie - le fabuleux destin.wmv") == 0
          && strcmp(videos[0].filename, videos[0].object_filename) == 0
          && videos[0].object_format == MTP_OFC_WMV,
          "MTP readback exposes actual Unicode Name, filename, and object format");
    zune_free_videos(videos, count);
    put_string(MTP_OPC_Name, "");
    videos = zune_get_videos(&dev, &count);
    check(videos && !videos[0].title[0] && videos[0].object_filename[0],
          "unavailable Name remains empty rather than pretending filename is title");
    zune_free_videos(videos, count);

    reset_transport();
    const char *filename = "Cyberpunk Edgerunners - S01E01 - Let You Down.mp4";
    ret = send_named(&dev, path, ZUNE_METAGENRE_TV_SHOW,
                     filename, "Let You Down", "Cyberpunk Edgerunners", 1, 1, &id);
    check(ret == 0 && string_equals(MTP_OPC_ObjectFileName, filename)
          && string_equals(MTP_OPC_Name, "Let You Down")
          && string_equals(ZUNE_OPC_SeriesName, "Cyberpunk Edgerunners")
          && number_equals(ZUNE_OPC_Season, 1, 4) && number_equals(ZUNE_OPC_Episode, 1, 4)
          && number_equals(MTP_OPC_MetaGenre, ZUNE_METAGENRE_TV_SHOW, 2),
          "episode matches captured Windows Name/FileName/series property split");
    Property *description = property(MTP_OPC_Description);
    Property *art = property(MTP_OPC_RepresentativeSampleData);
    check(description && rd32(description->data) == strlen("A description")
          && description->len == 4 + 2 * strlen("A description")
          && art && rd32(art->data) == sizeof(poster)
          && memcmp(art->data + 4, poster, sizeof(poster)) == 0,
          "description remains AUINT16 and poster remains AUINT8");

    char *series = NULL, *title = NULL;
    int season = -1, episode = -1;
    ret = zune_get_series_info(&dev, ITEM, &series, &season, &episode, &title);
    check(ret == 0 && strcmp(title, "Let You Down") == 0
          && strcmp(series, "Cyberpunk Edgerunners") == 0 && season == 1 && episode == 1,
          "series readback returns clean Name and independent episode metadata");
    free(series); free(title);
    int writes_before = writes;
    ret = zune_rename_item(&dev, ITEM, "Stay: don't let go");
    check(ret == 0 && writes == writes_before + 1 && uploads == 1 && deletes == 0
          && string_equals(MTP_OPC_Name, "Stay: don't let go")
          && string_equals(MTP_OPC_ObjectFileName, filename)
          && string_equals(ZUNE_OPC_SeriesName, "Cyberpunk Edgerunners")
          && number_equals(ZUNE_OPC_Season, 1, 4) && number_equals(ZUNE_OPC_Episode, 1, 4),
          "title-only rename preserves media, filename, series, and numbering");
    fail_property = MTP_OPC_Name;
    ret = zune_rename_item(&dev, ITEM, "Rejected");
    check(ret == -1 && string_equals(MTP_OPC_Name, "Stay: don't let go"),
          "title-only rename reports actual setter failure");
    fail_property = 0;
    put_string(MTP_OPC_Name, "");
    ret = zune_get_series_info(&dev, ITEM, NULL, NULL, NULL, &title);
    check(ret == 0 && title && strcmp(title, filename) == 0,
          "series title falls back to filename when Name is empty; optional outputs safe");
    free(title);

    reset_transport();
    ret = send_named(&dev, path, ZUNE_METAGENRE_TV_SHOW,
                     "Special.wmv", "Special", "The Show", 0, 0, &id);
    check(ret == 0 && string_equals(MTP_OPC_Name, "Special")
          && number_equals(ZUNE_OPC_Season, 0, 4) && number_equals(ZUNE_OPC_Episode, 0, 4),
          "TV specials retain zero season/episode without changing the title");

    const uint16_t genres[] = {ZUNE_METAGENRE_MUSIC_VIDEO, ZUNE_METAGENRE_OTHER};
    for (unsigned i = 0; i < sizeof(genres) / sizeof(genres[0]); i++) {
        reset_transport();
        ret = send_named(&dev, path, genres[i], "Clip.wmv", "Clip: live", "Unused", 4, 8, &id);
        check(ret == 0 && string_equals(MTP_OPC_Name, "Clip: live")
              && number_equals(MTP_OPC_MetaGenre, genres[i], 2) && !property(ZUNE_OPC_SeriesName),
              genres[i] == ZUNE_METAGENRE_OTHER ? "Other category retains its independent title"
                                               : "music-video category retains its independent title");
    }

    const struct { uint16_t code; const char *label; } failures[] = {
        {MTP_OPC_Name, "title"}, {MTP_OPC_MetaGenre, "category"},
        {MTP_OPC_Description, "description"}, {ZUNE_OPC_SeriesName, "series"},
        {ZUNE_OPC_Season, "season"}, {ZUNE_OPC_Episode, "episode"},
        {MTP_OPC_RepresentativeSampleData, "poster"}
    };
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        reset_transport();
        fail_property = failures[i].code;
        ret = send_named(&dev, path, ZUNE_METAGENRE_TV_SHOW,
                         filename, "Let You Down", "Cyberpunk Edgerunners", 1, 1, &id);
        char label[160];
        snprintf(label, sizeof(label), "failed %s returns incomplete with actual ID; upload is not repeated/deleted",
                 failures[i].label);
        check(ret == ZUNE_VIDEO_METADATA_INCOMPLETE && id == ITEM && exists
              && creates == 1 && uploads == 1 && deletes == 0
              && strstr(zune_get_error(), failures[i].label), label);
    }

    reset_transport();
    fail_opcode = PTP_OC_SendObject;
    id = 999;
    ret = send_named(&dev, path, ZUNE_METAGENRE_MOVIE, "Movie.wmv", "Movie", NULL, 0, 0, &id);
    check(ret == -1 && id == 0 && creates == 1 && uploads == 0 && deletes == 1 && !exists
          && writes == 0, "failed media transfer has no completed ID and removes its partial object");
    reset_transport();
    fail_opcode = PTP_OC_SendObjectInfo;
    id = 999;
    ret = send_named(&dev, path, ZUNE_METAGENRE_MOVIE, "Movie.wmv", "Movie", NULL, 0, 0, &id);
    check(ret == -1 && id == 0 && creates == 0 && uploads == 0 && writes == 0,
          "failed object creation has no completed ID or metadata writes");
    reset_transport();
    id = 999;
    ret = send_named(&dev, path, ZUNE_METAGENRE_MOVIE, "Movie.wmv", "", NULL, 0, 0, &id);
    check(ret == -1 && id == 0 && creates == 0,
          "invalid empty title fails before touching transport and clears stale output ID");

    reset_transport();
    ret = zune_smuggle_movie(&dev, path, "Legacy.wmv", NULL, NULL, 0, &id);
    check(ret == 0 && string_equals(MTP_OPC_Name, "Legacy.wmv")
          && string_equals(MTP_OPC_ObjectFileName, "Legacy.wmv"),
          "legacy movie API keeps its previous source behavior");
    reset_transport();
    ret = zune_smuggle_episode(&dev, path, "Legacy.wmv", "Legacy Series", 2, 5,
                               NULL, NULL, 0, &id);
    check(ret == 0 && string_equals(MTP_OPC_Name, "Legacy Series - S02E05")
          && string_equals(MTP_OPC_ObjectFileName, "Legacy.wmv"),
          "legacy episode API remains compatible while the named API opts into clean titles");

    uint8_t record[128] = {0};
    record[0] = 1; record[16] = sizeof(payload); record[38] = 4;
    memcpy(record + 40, movie, strlen(movie) + 1);
    const uint16_t formats[] = {MTP_OFC_WMV, MTP_OFC_MP4};
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        record[32] = formats[i] & 0xff; record[33] = formats[i] >> 8;
        ZuneDBLibrary *scan = calloc(1, sizeof(*scan));
        scan->videos = calloc(1, sizeof(*scan->videos));
        scan->video_count = 1;
        ret = zmdb_parse_video(NULL, record, sizeof(record), ITEM, scan->videos);
        check(ret == 0 && strcmp(scan->videos[0].title, movie) == 0
              && strcmp(scan->videos[0].filename, movie) == 0
              && !scan->videos[0].object_filename[0]
              && scan->videos[0].object_format == formats[i],
              formats[i] == MTP_OFC_WMV ? "ZMDB WMV Name is known; filename remains explicitly unknown"
                                        : "ZMDB MP4 retains format when the clean title has no extension");
        zune_free_scan(scan);
    }

    reset_transport();
    unlink(path);
    printf("video_named: %d checks passed; no USB backend linked\n", checks);
    return 0;
}
