/* libFuzzer: arbitrary bytes as a ZMDB database, through both ZMDB parsers
 * and the podcast-state parser. zmdb.c is compiled into this translation
 * unit so its static parsers are reachable; transport is a stub. */
#include "../../src/zmdb.c"

void zune_set_error(const char *fmt, ...) { (void)fmt; }
void zune_clear_error(void) {}
const char *zune_get_error(void) { return NULL; }
int zune_zmdb_read_payload(zune_usb_handle_t *usb, uint8_t **payload, uint32_t *length) {
    (void)usb; (void)payload; (void)length;
    return -1;
}
int zune_zmdb_read_response(zune_usb_handle_t *usb, uint8_t header[12], uint8_t **payload, uint32_t *length) {
    (void)usb; (void)header; (void)payload; (void)length;
    return -1;
}
ZunePlaylist *zune_get_playlists(ZuneDevice *dev, int *count) {
    (void)dev;
    *count = 0;
    return NULL;
}
void zune_free_videos(ZuneVideoFile *videos, int count) {
    if (!videos) return;
    for (int i = 0; i < count; i++) {
        free(videos[i].filename);
        free(videos[i].title);
        free(videos[i].object_filename);
    }
    free(videos);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    zune_free_scan(zmdb_parse_full(data, size));
    zune_free_library(zmdb_parse(data, size));
    ZunePodcastState *states = NULL;
    int count = 0;
    if (zune_parse_podcast_states(data, size, &states, &count) == 0)
        zune_free_podcast_states(states, count);
    return 0;
}
