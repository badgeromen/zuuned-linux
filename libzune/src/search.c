/*
 * search.c — Duplicate detection via cached ZMDB library data.
 *
 * Searches the in-memory ZMDB library (from zune_infiltrate) to find
 * existing content on the device by metadata match. Zero MTP calls —
 * all searches are pure in-memory string comparisons.
 */

#include "zune_internal.h"
#include <strings.h>  /* strcasecmp */

/* ---- Track search by title + artist + album ---- */

uint32_t zune_find_track(ZuneDevice *dev,
                          const char *title, const char *artist,
                          const char *album) {
    if (!dev || !dev->cached_library || !title)
        return 0;

    ZuneDBLibrary *lib = dev->cached_library;
    const char *a = artist ? artist : "";
    const char *al = album ? album : "";

    for (int i = 0; i < lib->track_count; i++) {
        ZuneTrack *t = &lib->tracks[i];
        if (!t->title) continue;

        if (strcasecmp(t->title, title) == 0 &&
            strcasecmp(t->artist ? t->artist : "", a) == 0 &&
            strcasecmp(t->album ? t->album : "", al) == 0) {
            return t->item_id;
        }
    }
    return 0;
}

/* ---- Video search by filename ---- */

uint32_t zune_find_video(ZuneDevice *dev, const char *filename) {
    if (!dev || !dev->cached_library || !filename)
        return 0;

    ZuneDBLibrary *lib = dev->cached_library;

    for (int i = 0; i < lib->video_count; i++) {
        ZuneVideoFile *v = &lib->videos[i];
        if (v->filename && strcasecmp(v->filename, filename) == 0)
            return v->item_id;
    }
    return 0;
}

/* ---- Photo search by filename ---- */

uint32_t zune_find_photo(ZuneDevice *dev, const char *filename) {
    if (!dev || !dev->cached_library || !filename)
        return 0;

    ZuneDBLibrary *lib = dev->cached_library;

    for (int i = 0; i < lib->photo_count; i++) {
        ZunePhotoFile *p = &lib->photos[i];
        if (p->filename && strcasecmp(p->filename, filename) == 0)
            return p->item_id;
    }
    return 0;
}
