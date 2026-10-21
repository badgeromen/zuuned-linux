/* SPDX-License-Identifier: GPL-3.0-or-later
 * Minimal public API client. Validation is offline; --serve explicitly writes
 * a fresh artist when absent. A real sync client also links its albums/tracks
 * inside the READY callback before catalog requests are serviced. */
#include "zune.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    ZuneHDArtConfig config;
    ZuneHDArtistResult artist;
    ZuneHDArtStatus artist_status;
} HdExample;

static uint8_t *hd_example_load(const char *path, size_t limit, size_t *size) {
    FILE *file = fopen(path, "rb"); if (!file) return NULL;
    uint8_t *bytes = malloc(limit+1);
    size_t n = bytes ? fread(bytes, 1, limit+1, file) : 0;
    int bad = !bytes || !n || n > limit || ferror(file);
    fclose(file);
    if (bad) { free(bytes); return NULL; }
    *size = n; return bytes;
}

static int hd_example_ready(struct ZuneDevice *device, void *opaque) {
    HdExample *e = opaque;
    e->artist_status = zune_hd_art_ensure_artist(device, e->config.artist_name,
        e->config.catalog_guid, &e->artist);
    printf("artist status=%d handle=%u created=%d write_attempted=%d\n",
        e->artist_status, e->artist.handle, e->artist.created, e->artist.write_attempted);
    return e->artist_status == ZUNE_HD_ART_OK ? 0 : e->artist.transport_uncertain ? -2 : -1;
}

int main(int argc, char **argv) {
    if (argc < 7 || (strcmp(argv[1], "--validate") && strcmp(argv[1], "--serve")) ||
        (argc-7)%2 || (argc-7)/2 > ZUNE_HD_ART_GALLERY_MAX) {
        fprintf(stderr, "usage: %s --validate|--serve ARTIST CATALOG_UUID BACKGROUND.jpg THUMBNAIL.jpg BIOGRAPHY.txt|- [GALLERY_UUID IMAGE.jpg]...\n", argv[0]);
        return 2;
    }
    int serve = !strcmp(argv[1], "--serve"), exit_code = 1;
    HdExample e = {0};
    e.config.version = ZUNE_HD_ART_API_VERSION; e.config.artist_name = argv[2];
    e.config.catalog_guid = argv[3]; e.config.duration_ms = 240000;
    if (strlen(argv[3]) != 36) return 2;
    char background_id[37], thumbnail_id[37];
    memcpy(background_id, argv[3], 37); memcpy(thumbnail_id, argv[3], 37);
    unsigned char last = (unsigned char)tolower((unsigned char)argv[3][35]);
    if (!isxdigit(last)) return 2;
    unsigned nibble = last <= '9' ? last-'0' : last-'a'+10;
    background_id[35] = "0123456789abcdef"[nibble^1];
    thumbnail_id[35] = "0123456789abcdef"[nibble^2];
    e.config.background.guid = background_id; e.config.thumbnail.guid = thumbnail_id;
    e.config.background.jpeg = hd_example_load(argv[4], 128*1024, &e.config.background.size);
    e.config.thumbnail.jpeg = hd_example_load(argv[5], 128*1024, &e.config.thumbnail.size);
    if (strcmp(argv[6], "-")) {
        e.config.biography = (char *)hd_example_load(argv[6], 4096, &e.config.biography_size);
        if (!e.config.biography) goto done;
    }
    e.config.gallery_count = (size_t)(argc-7)/2;
    for (size_t i = 0; i < e.config.gallery_count; i++) {
        e.config.gallery[i].guid = argv[7+2*i];
        e.config.gallery[i].jpeg = hd_example_load(argv[8+2*i],128*1024,&e.config.gallery[i].size);
    }
    if (zune_hd_art_validate(&e.config) != ZUNE_HD_ART_OK) {
        fprintf(stderr, "validation failed: %s\n", zune_get_error()); goto done;
    }
    if (!serve) { puts("HD artist content validated; no device opened"); exit_code = 0; goto done; }
    struct ZuneDevice *device = zune_breach();
    if (!device) goto done;
    ZuneHDArtResult result;
    zune_hd_art_serve(device, &e.config, hd_example_ready, &e, NULL, NULL, &result);
    printf("status=%d closed=%d safe_to_continue=%d biography_requests=%u biography_ACKs=%u\n",
        result.status, result.network_closed, result.safe_to_continue,
        result.biography_requests, result.biography_acknowledged);
    for (size_t i = 0; i < result.asset_count; i++) printf("asset=%s requests=%u ACKs=%u\n",
        result.assets[i].guid, result.assets[i].requests, result.assets[i].acknowledged);
    /* No finalize or retry here. sever safely drops a wounded HD connection. */
    zune_sever(device);
    exit_code = result.status == ZUNE_HD_ART_OK ? 0 : result.status == ZUNE_HD_ART_PARTIAL ? 3 : 1;
done:
    free((void *)e.config.background.jpeg); free((void *)e.config.thumbnail.jpeg);
    free((void *)e.config.biography);
    for (size_t i = 0; i < e.config.gallery_count; i++) free((void *)e.config.gallery[i].jpeg);
    return exit_code;
}
