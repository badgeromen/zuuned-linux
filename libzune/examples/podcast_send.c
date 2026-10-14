/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common.h"
#include <time.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    if ((argc != 9 && argc != 10) || strcmp(argv[1], "--send")) {
        puts("Usage: podcast_send --send prepared.mp3 feed-url stable-source-url show author title duration-ms [cover.jpg]\nWrites a podcast; does not fetch feeds, convert audio or delete existing content.");
        return argc == 2 && !strcmp(argv[1], "--help") ? 0 : 2;
    }
    uint32_t duration;
    uint8_t *cover = NULL; size_t cover_size = 0;
    if (!*argv[3] || !*argv[4] || !*argv[5] || !*argv[7] || positive_u32(argv[8], &duration) ||
        !prepared_file(argv[2]) || load_cover(argc == 10 ? argv[9] : NULL, &cover, &cover_size)) {
        fputs("Invalid input: supply nonempty identities/title, a positive 32-bit duration, a nonempty prepared MP3 under 4 GiB, and an optional JPEG up to 1 MiB.\n", stderr);
        return 2;
    }
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) { free(cover); return failure(NULL, "Connect"); }
    uint32_t folder = 0, show = 0, id = 0;
    ZunePodcastSeries *shows = NULL; int count = 0, result = 1;
    if (zune_find_podcast_episode(dev, argv[3], argv[4], &folder, &show, &id)) {
        failure(dev, "Lookup failed; no writes"); goto done;
    }
    if (!show) {
        /* Authenticated lookup succeeded. Parent ownership remains the caller's
         * responsibility if subsequent series/episode creation fails. */
        char folder_name[96];
        snprintf(folder_name, sizeof(folder_name), "libzune podcast %lld-%ld",
                 (long long)time(NULL), (long)getpid());
        folder = zune_forge_folder(dev, folder_name, UINT32_MAX);
        if (!folder || folder == UINT32_MAX) { failure(dev, "Folder creation failed"); goto done; }
        printf("Created parent folder: %" PRIu32 "\n", folder);
        int status = zune_forge_podcast_series(dev, folder, "Podcast.ser", argv[5], argv[6], argv[3], &show);
        if (status) {
            failure(dev, "Series creation failed");
            fprintf(stderr, "status=%d serial=%s series=%" PRIu32 " parent=%" PRIu32 "\n",
                    status, text(zune_get_serial(dev)), show, folder);
            if (status == ZUNE_PODCAST_FAILED_CLEANED) fputs("New series deletion confirmed; parent may remain.\n", stderr);
            else fputs("State may be uncertain even with zero ID. Inspect before retrying.\n", stderr);
            goto done;
        }
        /* Forge can reuse an existing feed in a different parent. Re-read its
         * actual location instead of guessing from our requested folder. */
        if (zune_find_podcast_episode(dev, argv[3], argv[4], &folder, &show, &id) || !show) {
            failure(dev, "Live series lookup failed after forge; inspect retained objects"); goto done;
        }
    }
    if (zune_get_podcast_series(dev, &shows, &count)) { failure(dev, "Live show name unavailable"); goto done; }
    const char *live_name = NULL;
    for (int i = 0; i < count; ++i)
        if (shows[i].item_id == show && !strcmp(text(shows[i].feed_url), argv[3])) live_name = shows[i].name;
    if (!live_name) { fputs("Live show identity missing; no episode sent.\n", stderr); goto done; }
    ZunePodcastEpisodeInput episode = {.filename="Episode.mp3", .title=argv[7],
        .author=argv[6], .series_name=live_name, .source_url=argv[4],
        .description="", .date_authored=NULL, .series_id=show, .duration_ms=duration};
    /* The sender validates metadata and byte size for reuse. Lookup alone is
     * admission information, not proof that this payload matches. */
    id = 0;
    int status = zune_smuggle_podcast_episode(dev, folder, argv[2], &episode, &id);
    if (status) {
        failure(dev, "Episode send failed; no automatic retry");
        fprintf(stderr, "status=%d serial=%s episode=%" PRIu32 "\n", status, text(zune_get_serial(dev)), id);
        if (status == ZUNE_PODCAST_FAILED_CLEANED) fputs("New episode deletion confirmed; show/folder remain.\n", stderr);
        else fputs("Inspect retained/uncertain state even with zero ID.\n", stderr);
        goto done;
    }
    printf("Episode transferred or verified reusable: %" PRIu32 "\n", id);
    result = 0;
    if (cover && zune_brand(dev, show, cover, cover_size)) result = failure(dev, "Episode retained; show cover failed");
    if (zune_finalize(dev)) result = failure(dev, "Episode retained; finalization failed");
    puts("Finalization requested; check presentation/playback on the device.");
done:
    zune_free_podcast_series(shows, count);
    zune_sever(dev); free(cover);
    return result;
}
