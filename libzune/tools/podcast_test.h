/* Explicit, isolated hardware gate. Included after connect_or_die in zunetool.
 * Never accepts existing object IDs; cleanup is restricted to this invocation. */
#ifndef ZUNE_PODCAST_TEST_H
#define ZUNE_PODCAST_TEST_H

static volatile sig_atomic_t podcast_test_interrupted;
static void podcast_test_interrupt(int sig) { (void)sig; podcast_test_interrupted = 1; }

static int podcast_test_new_id(uint32_t id, const uint32_t *before, int count,
                                const uint32_t *created, int created_count)
{
    if (!id || id == UINT32_MAX) return 0;
    for (int i = 0; i < count; ++i) if (before[i] == id) return 0;
    for (int i = 0; i < created_count; ++i) if (created[i] == id) return 0;
    return 1;
}

static int podcast_test_same_file(const char *a, const char *b)
{
    FILE *left = fopen(a, "rb"), *right = fopen(b, "rb");
    int same = left && right;
    uint8_t x[8192], y[8192];
    while (same) {
        size_t nx = fread(x, 1, sizeof(x), left), ny = fread(y, 1, sizeof(y), right);
        if (nx != ny || memcmp(x, y, nx) || ferror(left) || ferror(right)) same = 0;
        if (!same || nx < sizeof(x)) break;
    }
    if (left) fclose(left);
    if (right) fclose(right);
    return same;
}

static int cmd_podcast_test(int argc, char **argv)
{
    struct stat st;
    if ((argc != 3 && argc != 4) || stat(argv[2], &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 4 || st.st_size > 1024 * 1024) {
        fprintf(stderr, "usage: zunetool podcast-test <prepared-mp3> [episode-title] (4 bytes to 1 MiB; creates then removes a test show/episode)\n");
        return 2;
    }
    unsigned char signature[3] = {0};
    FILE *input = fopen(argv[2], "rb");
    int readable = input && fread(signature, 1, 3, input) == 3;
    if (input) fclose(input);
    int mp3 = !memcmp(signature, "ID3", 3) ||
              (signature[0] == 0xff && (signature[1] & 0xe6) == 0xe2);
    ZuneMetadata metadata = {0};
    if (!readable || !mp3 || zune_probe(argv[2], &metadata) != 0 ||
        !metadata.duration_ms || metadata.duration_ms > 30000) {
        zune_free_metadata(&metadata);
        fprintf(stderr, "[podcast-test] Refusing input: use a prepared MP3 of at most 30 seconds. No connection made.\n");
        return 2;
    }
    uint32_t duration = metadata.duration_ms;
    zune_free_metadata(&metadata);
    char download[] = "/tmp/libzune-podcast-readback-XXXXXX";
    int fd = mkstemp(download);
    if (fd < 0) { fprintf(stderr, "[podcast-test] Cannot prepare local readback file\n"); return 1; }
    close(fd);

    ZuneDevice *dev = zune_breach();
    if (!dev || !dev->mtpz_authenticated) {
        fprintf(stderr, "[podcast-test] Authentication did not complete; no media writes performed\n");
        if (dev) zune_sever(dev);
        unlink(download);
        return 1;
    }
    uint32_t *before = NULL; int before_count = 0;
    if (mtp_get_object_handles(&dev->ptp, dev->storage_id, 0, 0, &before, &before_count) != 0) {
        free(before); unlink(download); zune_sever(dev);
        fprintf(stderr, "[podcast-test] Baseline inventory failed; no media writes performed\n"); return 1;
    }
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    char folder[128], feed[192], url[224];
    snprintf(folder, sizeof(folder), "ZUUNED podcast test %ld-%ld-%ld", (long)now.tv_sec, (long)now.tv_nsec, (long)getpid());
    snprintf(feed, sizeof(feed), "https://example.invalid/zuuned-test/%ld-%ld-%ld/feed", (long)now.tv_sec, (long)now.tv_nsec, (long)getpid());
    snprintf(url, sizeof(url), "%s/episode.mp3", feed);
    uint32_t created[3] = {0}, id = 0, repeat = 0;
    int ok = 0;
    podcast_test_interrupted = 0;
    void (*old_int)(int) = signal(SIGINT, podcast_test_interrupt);
    void (*old_term)(int) = signal(SIGTERM, podcast_test_interrupt);
    created[0] = zune_forge_folder(dev, folder, 0xffffffff);
    if (!podcast_test_new_id(created[0], before, before_count, created, 0)) { created[0] = 0; goto cleanup; }
    fprintf(stderr, "[podcast-test] created isolated folder=%u\n", created[0]);
    if (podcast_test_interrupted) goto cleanup;
    int result = zune_forge_podcast_series(dev, created[0], "Test show.ser", "ZUUNED podcast check",
                                          "ZUUNED", feed, &id);
    if (podcast_test_new_id(id, before, before_count, created, 1)) created[1] = id;
    fprintf(stderr, "[podcast-test] series result=%d id=%u\n", result, id);
    if (result != 0 || !created[1] || podcast_test_interrupted) goto cleanup;
    if (zune_forge_podcast_series(dev, created[0], "Test show.ser", "ZUUNED podcast check",
                                  "ZUUNED", feed, &repeat) != 0 || repeat != created[1]) goto cleanup;
    fprintf(stderr, "[podcast-test] PASS repeated series reuses id=%u\n", repeat);
    ZunePodcastEpisodeInput ep = {
        .filename="Test episode.mp3", .title=argc == 4 ? argv[3] : "ZUUNED podcast test episode", .author="ZUUNED",
        .series_name="ZUUNED podcast check", .source_url=url,
        .description="Generated tone for isolated podcast protocol verification. Removed after testing.",
        .date_authored="20261008T120000Z", .series_id=created[1], .duration_ms=duration
    };
    if (podcast_test_interrupted) goto cleanup;
    result = zune_smuggle_podcast_episode(dev, created[0], argv[2], &ep, &id);
    if (podcast_test_new_id(id, before, before_count, created, 2)) created[2] = id;
    fprintf(stderr, "[podcast-test] episode result=%d id=%u\n", result, id);
    if (result != 0 || !created[2] || podcast_test_interrupted) goto cleanup;
    if (zune_smuggle_podcast_episode(dev, created[0], argv[2], &ep, &repeat) != 0 || repeat != created[2]) goto cleanup;
    fprintf(stderr, "[podcast-test] PASS repeated episode reuses id=%u\n", repeat);
    if (zune_extract_track(dev, created[2], download) != 0 || !podcast_test_same_file(argv[2], download)) goto cleanup;
    fprintf(stderr, "[podcast-test] PASS extracted episode bytes match generated source\n");
    uint32_t *after = NULL; int after_count = 0;
    result = mtp_get_object_handles(&dev->ptp, dev->storage_id, 0, 0, &after, &after_count);
    fprintf(stderr, "[podcast-test] wildcard inventory before=%d after=%d (not a podcast-specific inventory)\n", before_count, after_count);
    int preserved = result == 0;
    for (int i = 0; preserved && i < before_count; ++i) {
        int found = 0;
        for (int j = 0; j < after_count; ++j) if (after[j] == before[i]) { found = 1; break; }
        preserved = found;
    }
    free(after);
    /* Firmware need not expose podcast/abstract objects in its format-zero
     * inventory. The typed API reuse checks above validate their exact IDs;
     * here require every pre-existing wildcard-visible object to survive. */
    if (!preserved) { zune_set_error("Original wildcard inventory changed during the podcast test"); goto cleanup; }
    ok = 1;
cleanup:
    if (!ok) fprintf(stderr, "[podcast-test] FAIL: %s (autopsy=0x%04X)\n", zune_get_error() ? zune_get_error() : "test assertion failed", zune_autopsy(dev));
    int children_removed = 1;
    for (int i = 2; i >= 0; --i) {
        if (!created[i]) continue;
        if (i == 0 && !children_removed) {
            fprintf(stderr, "[podcast-test] CLEANUP REQUIRED: preserving test folder=%u because child cleanup failed\n", created[i]);
            ok = 0; continue;
        }
        int removed = zune_purge_track(dev, created[i]) == 0;
        fprintf(stderr, "[podcast-test] %s remove created id=%u\n", removed ? "PASS" : "FAIL", created[i]);
        if (!removed) { ok = 0; children_removed = 0; }
    }
    if (created[0]) {
        uint32_t *remaining = NULL; int remaining_count = 0;
        int read = mtp_get_object_handles(&dev->ptp, dev->storage_id, 0, 0, &remaining, &remaining_count);
        int restored = !read && remaining_count == before_count;
        for (int i = 0; restored && i < before_count; ++i) {
            int found = 0;
            for (int j = 0; j < remaining_count; ++j) if (remaining[j] == before[i]) { found = 1; break; }
            restored = found;
        }
        free(remaining);
        fprintf(stderr, "[podcast-test] %s original handle inventory restored\n", restored ? "PASS" : "FAIL");
        if (!restored) ok = 0;
        zune_finalize(dev); // Best effort reindex, not a playback/persistence claim.
    }
    free(before); unlink(download); zune_sever(dev);
    if (old_int != SIG_ERR) signal(SIGINT, old_int);
    if (old_term != SIG_ERR) signal(SIGTERM, old_term);
    if (podcast_test_interrupted) ok = 0;
    fprintf(stderr, "[podcast-test] %s; playback/category after eject remains a separate gate\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
#endif
