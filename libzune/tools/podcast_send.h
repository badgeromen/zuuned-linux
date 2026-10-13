/* Explicit developer upload, retaining the episode for device-screen testing.
 * Requires a locally prepared MP3; no fetching, transcoding or existing-item
 * deletion. Included after podcast_test.h for its byte comparison helper. */
#ifndef ZUNE_PODCAST_SEND_H
#define ZUNE_PODCAST_SEND_H

static int cmd_podcast_send(int argc, char **argv)
{
    struct stat st;
    if (argc != 9 || stat(argv[2], &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 4 || st.st_size > 16 * 1024 * 1024) {
        fprintf(stderr, "usage: zunetool podcast-send <prepared-mp3> <feed-url> <show> <title> <author> <episode-url> <YYYYMMDDTHHMMSSZ>\n"
                        "Retains content for playback testing. Experimental 16 MiB limit.\n");
        return 2;
    }
    unsigned char signature[3] = {0};
    FILE *input = fopen(argv[2], "rb");
    int readable = input && fread(signature, 1, 3, input) == 3;
    if (input) fclose(input);
    ZuneMetadata metadata = {0};
    if (!readable || (memcmp(signature, "ID3", 3) &&
        !(signature[0] == 0xff && (signature[1] & 0xe6) == 0xe2)) ||
        zune_probe(argv[2], &metadata) != 0 || !metadata.duration_ms) {
        zune_free_metadata(&metadata);
        fprintf(stderr, "[podcast-send] Prepared MP3 required; no connection made\n");
        return 2;
    }
    uint32_t duration = metadata.duration_ms;
    zune_free_metadata(&metadata);
    char download[] = "/tmp/libzune-podcast-send-readback-XXXXXX";
    int fd = mkstemp(download);
    if (fd < 0) return 1;
    close(fd);
    ZuneDevice *dev = zune_breach();
    if (!dev || !dev->mtpz_authenticated) {
        fprintf(stderr, "[podcast-send] Authentication incomplete; no writes performed\n");
        if (dev) zune_sever(dev);
        unlink(download);
        return 1;
    }
    int ok = 0, count = 0, matches = 0;
    uint32_t folder = 0, show = 0, episode = 0, repeat = 0;
    ZunePodcastSeries *series = NULL;
    if (zune_get_podcast_series(dev, &series, &count) != 0) goto done;
    for (int i = 0; i < count; ++i) {
        if (!strcmp(series[i].feed_url, argv[3])) { show = series[i].item_id; ++matches; }
    }
    zune_free_podcast_series(series, count);
    if (matches > 1) { zune_set_error("Ambiguous feed identity; no writes performed"); goto done; }
    if (show) {
        mtp_object_info_t info = {0};
        int result = mtp_get_object_info(&dev->ptp, show, &info);
        folder = info.parent_object;
        mtp_free_object_info(&info);
        if (result || !folder) { zune_set_error("Cannot establish existing show folder"); goto done; }
    } else {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        char name[128];
        snprintf(name, sizeof(name), "Zuuned Podcasts %ld-%ld-%ld", (long)now.tv_sec, (long)now.tv_nsec, (long)getpid());
        folder = zune_forge_folder(dev, name, UINT32_MAX);
        if (!folder || folder == UINT32_MAX) { folder = 0; goto done; }
    }
    fprintf(stderr, "[podcast-send] folder=%u\n", folder);
    if (zune_forge_podcast_series(dev, folder, "Podcast.ser", argv[4], argv[6], argv[3], &show) != 0) goto done;
    fprintf(stderr, "[podcast-send] series=%u\n", show);
    if (zune_forge_podcast_series(dev, folder, "Podcast.ser", argv[4], argv[6], argv[3], &repeat) != 0 || repeat != show) {
        zune_set_error("Repeated series lookup did not reuse the same ID"); goto done;
    }
    ZunePodcastEpisodeInput ep = {
        .filename = "Episode.mp3", .title = argv[5], .author = argv[6],
        .series_name = argv[4], .source_url = argv[7], .description = "",
        .date_authored = argv[8], .series_id = show, .duration_ms = duration
    };
    if (zune_smuggle_podcast_episode(dev, folder, argv[2], &ep, &episode) != 0) goto done;
    fprintf(stderr, "[podcast-send] episode=%u\n", episode);
    if (zune_smuggle_podcast_episode(dev, folder, argv[2], &ep, &repeat) != 0 || repeat != episode) {
        zune_set_error("Repeated episode lookup did not reuse the same ID"); goto done;
    }
    fprintf(stderr, "[podcast-send] PASS repeated show and episode reuse the same IDs\n");
    if (zune_extract_track(dev, episode, download) != 0) goto done;
    if (!podcast_test_same_file(argv[2], download)) {
        zune_set_error("Extracted podcast bytes differ from prepared source"); goto done;
    }
    fprintf(stderr, "[podcast-send] PASS extracted audio matches source byte for byte\n");
    /* Use the confirmed reindex operation and report its actual status. */
    if (mtp_vendor_operation(&dev->ptp, 0x9108, NULL, 0, NULL, NULL) != 0) {
        zune_set_error("Podcast retained, but CleanDataStore failed"); goto done;
    }
    fprintf(stderr, "[podcast-send] PASS CleanDataStore accepted\n");
    ok = 1;
done:
    if (!ok) fprintf(stderr, "[podcast-send] FAIL: %s (autopsy=0x%04X)\n",
                     zune_get_error() ? zune_get_error() : "operation failed", zune_autopsy(dev));
    fprintf(stderr, "[podcast-send] Retained handles: folder=%u series=%u episode=%u; no automatic CLI cleanup\n",
            folder, show, episode);
    unlink(download);
    zune_sever(dev);
    fprintf(stderr, "[podcast-send] %s; verify category/playback on the unplugged device\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
#endif
