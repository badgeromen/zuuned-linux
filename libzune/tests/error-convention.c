/* T3.5: the error convention. Every public function is called with NULL or
 * zero arguments (none reaches USB; zune_breach is the one that would) after
 * an earlier call's error was planted: a call that can fail must clear it on
 * entry, a call that cannot fail must leave it, and status functions must
 * fail with ZUNE_BLOWN. tests/run-error-convention.sh adds the source and
 * deprecation checks. This file calls the deprecated functions on purpose. */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "../src/zune_internal.h"

static int checks, failures;
static const char sentinel[] = "error left by an earlier call";

static void check(int ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    if (!ok) printf("FAIL: %s\n", name);
}

static void plant(void) { zune_set_error("%s", sentinel); }
static int planted(void) {
    const char *e = zune_get_error();
    return e && !strcmp(e, sentinel);
}

#define KEEPS(call) do { plant(); (void)(call); check(planted(), #call " leaves the error"); } while (0)
#define KEEPS_VOID(call) do { plant(); call; check(planted(), #call " leaves the error"); } while (0)
#define CLEARS(call) do { plant(); (void)(call); check(!planted(), #call " clears the error"); } while (0)
#define CLEARS_BLOWN(call) do { plant(); int rc_ = (call); \
    check(!planted() && rc_ == ZUNE_BLOWN, #call " clears the error and returns ZUNE_BLOWN"); } while (0)
#define CLEARS_ZERO(call) do { plant(); uint32_t id_ = (call); \
    check(!planted() && id_ == 0, #call " clears the error and returns 0"); } while (0)
#define CLEARS_NULL(call) do { plant(); const void *p_ = (const void *)(call); \
    check(!planted() && !p_, #call " clears the error and returns NULL"); } while (0)
#define CLEARS_NEGATIVE(call) do { plant(); int rc_ = (int)(call); \
    check(!planted() && rc_ < 0, #call " clears the error and fails"); } while (0)

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    check(ZUNE_OK == 0 && ZUNE_BLOWN == -1, "ZUNE_OK is 0 and ZUNE_BLOWN is -1");

    /* The error describes the most recent call that can fail. */
    plant();
    check(zune_get_folders(NULL, NULL) == ZUNE_BLOWN && zune_get_error() &&
          strstr(zune_get_error(), "Invalid device"), "a failing call replaces the earlier error with its own");
    (void)zune_autopsy(NULL);
    (void)zune_autopsy_name(0x2002);
    zune_free_folders(NULL, 0);
    check(zune_get_error() && strstr(zune_get_error(), "Invalid device"),
          "autopsy and free calls made while reporting keep the failure's error");
    char *series = NULL;
    int season = 0, episode = 0;
    check(zune_decode_filename("Show - S02E05 - Title.mkv", &series, &season, &episode) == 1 &&
          !zune_get_error(), "a call that succeeds after a failure leaves no error");
    free(series);

    /* Every public function, in header order (zune_breach and zune_get_error excepted). */
    CLEARS_NEGATIVE(zune_hd_art_validate(NULL));
    CLEARS_NEGATIVE(zune_hd_art_serve(NULL, NULL, NULL, NULL, NULL, NULL, NULL));
    CLEARS_NEGATIVE(zune_hd_art_ensure_artist(NULL, NULL, NULL, NULL));
    KEEPS(zune_dossier());
    KEEPS_VOID(zune_wiretap(NULL, NULL));
    KEEPS_VOID(zune_set_chatter(ZUNE_CHATTER_SILENT));
    KEEPS_VOID(zune_sever(NULL));
    KEEPS_VOID(zune_go_dark());
    KEEPS(zune_get_name(NULL));
    KEEPS(zune_get_model(NULL));
    KEEPS(zune_get_serial(NULL));
    KEEPS(zune_get_battery(NULL));
    KEEPS(zune_get_capacity(NULL));
    KEEPS(zune_get_headroom(NULL));
    CLEARS_BLOWN(zune_refresh_storage(NULL));
    KEEPS(zune_identify(NULL));
    KEEPS(zune_get_family(NULL));
    KEEPS(zune_is_hdd(NULL));
    CLEARS_BLOWN(zune_rename(NULL, NULL));
    KEEPS(zune_is_live(NULL));
    KEEPS_VOID(zune_abort(NULL));
    KEEPS(zune_is_aborted(NULL));
    KEEPS_VOID(zune_clear_abort(NULL));
    KEEPS(zune_autopsy(NULL));
    KEEPS(zune_autopsy_name(0));
    CLEARS_BLOWN(zune_unjam(NULL));
    CLEARS_ZERO(zune_forge_folder(NULL, NULL, 0));
    CLEARS_BLOWN(zune_get_folders(NULL, NULL));
    KEEPS_VOID(zune_free_folders(NULL, 0));
    CLEARS_NULL(zune_get_tracks(NULL, NULL));
    KEEPS_VOID(zune_free_tracks(NULL, 0));
    CLEARS_BLOWN(zune_smuggle_track(NULL, NULL, NULL));
    CLEARS_BLOWN(zune_smuggle_track_tagged(NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, NULL));
    CLEARS_BLOWN(zune_purge_track(NULL, 0));
    CLEARS_BLOWN(zune_extract_track(NULL, 0, NULL));
    CLEARS_BLOWN(zune_get_track_state(NULL, 0, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_set_track_state(NULL, 0, 0, 0));
    CLEARS_BLOWN(zune_verify(NULL, 0, NULL, NULL, 0));
    CLEARS_BLOWN(zune_rename_item(NULL, 0, NULL));
    CLEARS_BLOWN(zune_sync_notify(NULL, NULL, 0, 0, 0, 0, 0));
    CLEARS_BLOWN(zune_probe_object(NULL, 0, NULL, NULL));
    CLEARS_BLOWN(zune_probe_object_named(NULL, 0, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_get_item_refs(NULL, 0, NULL, NULL));
    CLEARS_ZERO(zune_find_track(NULL, NULL, NULL, NULL));
    CLEARS_ZERO(zune_find_video(NULL, NULL));
    CLEARS_ZERO(zune_find_photo(NULL, NULL));
    CLEARS_NULL(zune_get_videos(NULL, NULL));
    KEEPS_VOID(zune_free_videos(NULL, 0));
    CLEARS_BLOWN(zune_smuggle_video_named(NULL, NULL, NULL, NULL, 0, NULL, 0, 0, NULL, NULL, 0, NULL));
    CLEARS_BLOWN(zune_smuggle_movie(NULL, NULL, NULL, NULL, NULL, 0, NULL));
    CLEARS_BLOWN(zune_get_series_info(NULL, 0, NULL, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_smuggle_episode(NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, 0, NULL));
    CLEARS_BLOWN(zune_smuggle_clip(NULL, NULL, NULL, NULL, 0, NULL));
    CLEARS_BLOWN(zune_smuggle_other(NULL, NULL, NULL, NULL, NULL, 0, NULL));
    CLEARS_BLOWN(zune_purge_video(NULL, 0));
    CLEARS_BLOWN(zune_extract_video(NULL, 0, NULL));
    CLEARS_NULL(zune_get_photos(NULL, NULL));
    KEEPS_VOID(zune_free_photos(NULL, 0));
    CLEARS_BLOWN(zune_get_dimensions(NULL, 0, NULL, NULL));
    CLEARS_NULL(zune_get_photo_albums(NULL, NULL, 0, NULL));
    KEEPS_VOID(zune_free_photo_albums(NULL, 0));
    CLEARS_NULL(zune_arm_photo(NULL));
    CLEARS_BLOWN(zune_smuggle_photo(NULL, NULL, NULL));
    CLEARS_BLOWN(zune_purge_photo(NULL, 0));
    CLEARS_BLOWN(zune_extract_photo(NULL, 0, NULL));
    CLEARS_NULL(zune_get_playlists(NULL, NULL));
    KEEPS_VOID(zune_free_playlists(NULL, 0));
    CLEARS_ZERO(zune_forge_playlist(NULL, NULL, NULL, 0));
    CLEARS_BLOWN(zune_rewire_playlist(NULL, 0, NULL, NULL, 0));
    CLEARS_BLOWN(zune_purge_playlist(NULL, 0));
    CLEARS_ZERO(zune_forge_album(NULL, NULL, NULL, NULL, NULL, 0));
    CLEARS_BLOWN(zune_rewire_album(NULL, 0, NULL, NULL, NULL, 0));
    CLEARS_ZERO(zune_forge_artist(NULL, NULL));
    CLEARS_BLOWN(zune_link_artist(NULL, 0, 0));
    CLEARS_NULL(zune_get_albums(NULL, NULL));
    CLEARS_BLOWN(zune_get_albums_checked(NULL, NULL, NULL));
    KEEPS_VOID(zune_free_albums(NULL, 0));
    CLEARS_BLOWN(zune_get_podcast_series(NULL, NULL, NULL));
    KEEPS_VOID(zune_free_podcast_series(NULL, 0));
    CLEARS_BLOWN(zune_get_podcast_episodes(NULL, NULL, NULL));
    KEEPS_VOID(zune_free_podcast_episodes(NULL, 0));
    CLEARS_BLOWN(zune_get_podcast_episode_sources(NULL, NULL, NULL));
    KEEPS_VOID(zune_free_podcast_episode_sources(NULL, 0));
    CLEARS_BLOWN(zune_get_podcast_states(NULL, NULL, NULL));
    KEEPS_VOID(zune_free_podcast_states(NULL, 0));
    CLEARS_BLOWN(zune_forge_podcast_series(NULL, 0, NULL, NULL, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_smuggle_podcast_episode(NULL, 0, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_find_podcast_episode(NULL, NULL, NULL, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_get_audiobook_parts(NULL, NULL, NULL));
    KEEPS_VOID(zune_free_audiobook_parts(NULL, 0));
    CLEARS_BLOWN(zune_find_audiobook_part(NULL, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_smuggle_audiobook_part(NULL, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_brand(NULL, 0, NULL, 0));
    CLEARS_BLOWN(zune_get_brand_limits(NULL, 0, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_grab_thumb(NULL, 0, NULL));
    CLEARS_BLOWN(zune_grab_photo_thumb(NULL, 0, NULL));
    CLEARS_NULL(zune_arm_audio(NULL));
    CLEARS_NULL(zune_retag(NULL));
    CLEARS_NULL(zune_arm_video(NULL, (ZuneModel)0));
    CLEARS_NULL(zune_arm_episode(NULL, (ZuneModel)0, NULL, 0, 0));
    CLEARS_BLOWN(zune_infiltrate_legacy(NULL, NULL));
    KEEPS_VOID(zune_free_library(NULL));
    CLEARS_BLOWN(zune_finalize(NULL));
    CLEARS(zune_decode_filename(NULL, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_snap_thumb(NULL, NULL));
    CLEARS_BLOWN(zune_probe(NULL, NULL));
    KEEPS_VOID(zune_free_metadata(NULL));
    CLEARS_BLOWN(zune_smuggle_track_ex(NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL));
    CLEARS_BLOWN(zune_infiltrate(NULL, NULL));
    KEEPS_VOID(zune_free_scan(NULL));
    CLEARS_BLOWN(zune_dump_raw(NULL, NULL));
    CLEARS_BLOWN(zune_infiltrate_deep(NULL));

    zune_set_chatter(ZUNE_CHATTER_TALK);
    printf("Error convention: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
