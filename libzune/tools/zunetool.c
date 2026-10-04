/*
 * zunetool — libzune command-line harness
 *
 * A minimal CLI for exercising libzune against a real Zune WITHOUT the
 * macOS app or DEXT. On Linux it drives the device through the libusb
 * backend directly, which makes it the test rig for the wire-protocol
 * fixes (ZLP framing, autopsy codes, retry triage, timeouts).
 *
 * Build: `make zunetool` (Linux needs libusb-1.0 + libgcrypt).
 * Run as root or with a udev rule granting access to VID 0x045e.
 *
 *   zunetool info                 connect, print device + storage, disconnect
 *   zunetool list                 list tracks on device
 *   zunetool send <file> [title] [artist] [album] [genre]
 *   zunetool purge <item_id>
 *   zunetool torture <file>       send the same file 20x — flushes out the
 *                                 size-dependent ZLP failure and prints the
 *                                 autopsy code on any failure
 *
 * Every failure prints zune_autopsy()/_name() so we see WHY, not just that.
 */

#include "zune_internal.h"  /* bounded fixture reads raw properties by known ID */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <strings.h>

static void die(ZuneDeviceHandle dev, const char *what) {
    uint16_t rc = zune_autopsy(dev);
    fprintf(stderr, "FAIL: %s\n", what);
    fprintf(stderr, "  error : %s\n", zune_get_error() ? zune_get_error() : "(none)");
    fprintf(stderr, "  autopsy: %s (0x%04X)\n", zune_autopsy_name(rc), rc);
    if (dev) zune_sever(dev);
    exit(1);
}

static ZuneDeviceHandle connect_or_die(void) {
    fprintf(stderr, "Connecting (breach + MTPZ auth)...\n");
    ZuneDeviceHandle dev = zune_breach();
    if (!dev) {
        fprintf(stderr, "FAIL: zune_breach — %s\n",
                zune_get_error() ? zune_get_error() : "(no device / auth failed)");
        exit(1);
    }
    fprintf(stderr, "Connected: %s (%s), battery %d%%\n",
            zune_get_name(dev), zune_get_model(dev), zune_get_battery(dev));
    return dev;
}

static int cmd_info(void) {
    ZuneDeviceHandle dev = connect_or_die();
    uint64_t cap = zune_get_capacity(dev);
    uint64_t free = zune_get_headroom(dev);
    printf("name     : %s\n", zune_get_name(dev));
    printf("model    : %s\n", zune_get_model(dev));
    printf("serial   : %s\n", zune_get_serial(dev));
    printf("battery  : %d%%\n", zune_get_battery(dev));
    printf("capacity : %.2f GB\n", cap / 1e9);
    printf("free     : %.2f GB (%.1f%%)\n", free / 1e9,
           cap ? 100.0 * free / cap : 0.0);
    zune_sever(dev);
    return 0;
}

static int cmd_list(void) {
    ZuneDeviceHandle dev = connect_or_die();
    int n = 0;
    ZuneTrack *tracks = zune_get_tracks(dev, &n);
    printf("%d tracks:\n", n);
    for (int i = 0; i < n; i++) {
        printf("  [%u] %-32s %-24s plays=%u\n",
               tracks[i].item_id,
               tracks[i].title  ? tracks[i].title  : "(untitled)",
               tracks[i].artist ? tracks[i].artist : "(no artist)",
               tracks[i].playcount);
    }
    zune_free_tracks(tracks, n);
    zune_sever(dev);
    return 0;
}

static int cmd_send(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: zunetool smuggle <file> [title] [artist] [album] [genre]\n"); return 2; }
    const char *file   = argv[2];
    const char *title  = argc > 3 ? argv[3] : "Test Track";
    const char *artist = argc > 4 ? argv[4] : "Test Artist";
    const char *album  = argc > 5 ? argv[5] : "Test Album";
    const char *genre  = argc > 6 ? argv[6] : "Test";

    ZuneDeviceHandle dev = connect_or_die();
    zune_clear_abort(dev);
    uint32_t id = 0;
    fprintf(stderr, "Smuggling %s ...\n", file);
    int ret = zune_smuggle_track_tagged(dev, file, title, artist, album, genre,
                                        1, 0, &id);
    if (ret != 0) die(dev, "zune_smuggle_track_tagged");
    printf("OK: sent as item_id=%u\n", id);
    fprintf(stderr, "Finalizing...\n");
    zune_finalize(dev);
    zune_sever(dev);
    return 0;
}

static int cmd_purge(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: zunetool purge <item_id>\n"); return 2; }
    uint32_t id = (uint32_t)strtoul(argv[2], NULL, 0);
    ZuneDeviceHandle dev = connect_or_die();
    if (zune_purge_track(dev, id) != 0) die(dev, "zune_purge_track");
    printf("OK: purged item_id=%u\n", id);
    zune_sever(dev);
    return 0;
}

/* Send the same file N times. Since the ZLP bug is size-triggered and
 * deterministic, a file that hits it fails EVERY iteration; a good file
 * succeeds every time. This is the direct before/after test for the fix. */
static int cmd_torture(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: zunetool torture <file>\n"); return 2; }
    const char *file = argv[2];
    ZuneDeviceHandle dev = connect_or_die();
    int ok = 0, fail = 0;
    uint32_t ids[20];
    for (int i = 0; i < 20; i++) {
        zune_clear_abort(dev);
        uint32_t id = 0;
        char title[64];
        snprintf(title, sizeof(title), "Torture %02d", i);
        int ret = zune_smuggle_track_tagged(dev, file, title, "Torture Test",
                                            "Torture Album", "Test", 1, 0, &id);
        if (ret == 0) {
            ids[ok++] = id;
            fprintf(stderr, "  %02d OK (id=%u)\n", i, id);
        } else {
            uint16_t rc = zune_autopsy(dev);
            fprintf(stderr, "  %02d FAIL: %s (0x%04X)\n", i,
                    zune_autopsy_name(rc), rc);
            fail++;
        }
    }
    printf("torture result: %d ok, %d failed\n", ok, fail);
    /* Clean up what we sent so the test is repeatable. */
    for (int i = 0; i < ok; i++) zune_purge_track(dev, ids[i]);
    fprintf(stderr, "cleaned up %d test tracks\n", ok);
    zune_finalize(dev);
    zune_sever(dev);
    return fail == 0 ? 0 : 1;
}

/* Tiny, explicitly requested hardware gate. Never enumerate or rename existing
 * content. Finish the current small operation on SIGINT/SIGTERM, then clean up
 * the IDs created by this invocation before severing the session. */
static volatile sig_atomic_t video_test_interrupted;
static void interrupt_video_test(int signal_number) {
    (void)signal_number;
    video_test_interrupted = 1;
}

static int video_test_string(ZuneDevice *dev, uint32_t id, uint16_t prop,
                              const char *expected) {
    uint8_t *data = NULL;
    uint32_t len = 0;
    char *actual = NULL;
    int ret = mtp_get_object_prop_value(&dev->ptp, id, prop, &data, &len);
    if (ret == 0 && data && len) actual = mtp_ucs2_to_string(data, len);
    int ok = actual && strcmp(actual, expected) == 0;
    fprintf(stderr, "[video-title-test] %s item=%u property=0x%04X expected='%s' actual='%s'\n",
            ok ? "PASS" : "FAIL", id, prop, expected, actual ? actual : "(unreadable)");
    free(data);
    free(actual);
    return ok;
}

static int video_test_number(ZuneDevice *dev, uint32_t id, uint16_t prop,
                              uint32_t expected, uint32_t bytes) {
    uint8_t *data = NULL;
    uint32_t len = 0, actual = 0;
    int ret = mtp_get_object_prop_value(&dev->ptp, id, prop, &data, &len);
    int readable = ret == 0 && data && len == bytes;
    if (readable)
        for (uint32_t i = 0; i < bytes; i++) actual |= (uint32_t)data[i] << (8 * i);
    int ok = readable && actual == expected;
    fprintf(stderr, "[video-title-test] %s item=%u property=0x%04X expected=%u actual=%u readable=%d\n",
            ok ? "PASS" : "FAIL", id, prop, expected, actual, readable);
    free(data);
    return ok;
}

static int video_test_readback(ZuneDevice *dev, uint32_t id,
                                const char *filename, const char *title,
                                const char *series, uint64_t size) {
    int ok = video_test_string(dev, id, MTP_OPC_Name, title);
    ok &= video_test_string(dev, id, MTP_OPC_ObjectFileName, filename);
    ok &= video_test_number(dev, id, MTP_OPC_MetaGenre,
                            series ? ZUNE_METAGENRE_TV_SHOW : ZUNE_METAGENRE_MOVIE, 2);
    if (series) {
        ok &= video_test_string(dev, id, ZUNE_OPC_SeriesName, series);
        ok &= video_test_number(dev, id, ZUNE_OPC_Season, 2, 4);
        ok &= video_test_number(dev, id, ZUNE_OPC_Episode, 3, 4);
    }
    uint64_t actual_size = 0;
    uint16_t format = 0;
    int size_ok = zune_probe_object(dev, id, &actual_size, &format) == 0
                  && actual_size == size && format == MTP_OFC_WMV;
    fprintf(stderr, "[video-title-test] %s item=%u size=%" PRIu64 " expected=%" PRIu64 " format=0x%04X\n",
            size_ok ? "PASS" : "FAIL", id, actual_size, size, format);
    return ok && size_ok;
}

static int cmd_video_title_test(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: zunetool video-title-test <short-wmv> (1 MiB maximum)\n");
        return 2;
    }
    const char *filepath = argv[2];
    const char *extension = strrchr(filepath, '.');
    struct stat st;
    const uint8_t asf_guid[] = {0x30,0x26,0xb2,0x75,0x8e,0x66,0xcf,0x11,
                                0xa6,0xd9,0x00,0xaa,0x00,0x62,0xce,0x6c};
    uint8_t header[sizeof(asf_guid)];
    if (!extension || strcasecmp(extension, ".wmv") != 0
            || stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)
            || st.st_size < (off_t)sizeof(header) || st.st_size > 1024 * 1024) {
        fprintf(stderr, "[video-title-test] Refusing input: use a short WMV file, 16 bytes to 1 MiB. No connection made.\n");
        return 2;
    }
    FILE *input = fopen(filepath, "rb");
    int valid = input && fread(header, 1, sizeof(header), input) == sizeof(header)
                && memcmp(header, asf_guid, sizeof(header)) == 0;
    if (input) fclose(input);
    if (!valid) {
        fprintf(stderr, "[video-title-test] Refusing input: missing ASF/WMV signature. No connection made.\n");
        return 2;
    }

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    char nonce[80], movie_filename[128], episode_filename[160], series[128];
    snprintf(nonce, sizeof(nonce), "%ld-%ld-%ld", (long)now.tv_sec, (long)now.tv_nsec, (long)getpid());
    snprintf(movie_filename, sizeof(movie_filename), "ZUUNED title test %s movie.wmv", nonce);
    snprintf(episode_filename, sizeof(episode_filename), "ZUUNED title test %s - S02E03 - Episode.wmv", nonce);
    snprintf(series, sizeof(series), "ZUUNED title test %s", nonce);
    const char *movie_title = "ZUUNED title test: Movie";
    const char *episode_title = "A clean episode title";
    const char *renamed_title = "A renamed episode: title only";

    ZuneDeviceHandle dev = connect_or_die();
    uint32_t ids[2] = {0, 0};
    int ok = 1;
    video_test_interrupted = 0;
    void (*old_int)(int) = signal(SIGINT, interrupt_video_test);
    void (*old_term)(int) = signal(SIGTERM, interrupt_video_test);
    zune_clear_abort(dev);
    for (int i = 0; i < 2; i++) {
        if (video_test_interrupted) { ok = 0; goto cleanup; }
        int ret = zune_smuggle_video_named(dev, filepath,
                         i ? episode_filename : movie_filename,
                         i ? episode_title : movie_title,
                         i ? ZUNE_METAGENRE_TV_SHOW : ZUNE_METAGENRE_MOVIE,
                         i ? series : NULL, i ? 2 : 0, i ? 3 : 0,
                         "Temporary ZUUNED title verification fixture", NULL, 0, &ids[i]);
        fprintf(stderr, "[video-title-test] created %s handle=%u result=%d\n",
                i ? "episode" : "movie", ids[i], ret);
        if (ret != 0 || ids[i] == 0) {
            fprintf(stderr, "[video-title-test] upload failed/incomplete: %s\n", zune_get_error());
            ok = 0;
            goto cleanup;
        }
        if (!video_test_readback(dev, ids[i], i ? episode_filename : movie_filename,
                                i ? episode_title : movie_title, i ? series : NULL,
                                (uint64_t)st.st_size)) {
            ok = 0;
            goto cleanup;
        }
    }
    if (video_test_interrupted) { ok = 0; goto cleanup; }
    if (zune_rename_item(dev, ids[1], renamed_title) != 0) {
        fprintf(stderr, "[video-title-test] FAIL title-only rename handle=%u\n", ids[1]);
        ok = 0;
        goto cleanup;
    }
    ok &= video_test_readback(dev, ids[1], episode_filename, renamed_title,
                              series, (uint64_t)st.st_size);

cleanup:
    /* IDs come only from the two uploads above. No name search, no supplied
     * handle, and no library-wide operation is permitted in this cleanup. */
    zune_clear_abort(dev);
    for (int i = 1; i >= 0; i--) {
        if (!ids[i]) continue;
        int ret = zune_purge_video(dev, ids[i]);
        fprintf(stderr, "[video-title-test] %s delete created handle=%u\n",
                ret == 0 ? "PASS" : "FAIL", ids[i]);
        if (ret != 0) {
            ok = 0;
            fprintf(stderr, "[video-title-test] CLEANUP REQUIRED: newly created handle=%u remains unconfirmed.\n", ids[i]);
        }
    }
    fprintf(stderr, "[video-title-test] finalizing before sever\n");
    if (zune_finalize(dev) != 0) ok = 0;
    zune_sever(dev);
    if (old_int != SIG_ERR) signal(SIGINT, old_int);
    if (old_term != SIG_ERR) signal(SIGTERM, old_term);
    if (video_test_interrupted) ok = 0;
    fprintf(stderr, "[video-title-test] %s; created handles were %u, %u\n",
            ok ? "PASS" : "FAIL", ids[0], ids[1]);
    return ok ? 0 : 1;
}

/* Cross-check every ZMDB row against the real MTP object. Read-only.
 * Flags the stranded/half-written entries that make the firmware
 * crash-loop at playback: rows whose object is missing, size-mismatched,
 * zero-sized, or (tracks) zero-duration. */
static int cmd_audit(void) {
    ZuneDeviceHandle dev = connect_or_die();
    ZuneDBLibrary *lib = NULL;
    if (zune_infiltrate(dev, &lib) != 0 || !lib) die(dev, "zune_infiltrate");
    printf("auditing %d tracks + %d videos against MTP objects...\n",
           lib->track_count, lib->video_count);

    int bad = 0, checked = 0;
    for (int i = 0; i < lib->track_count; i++) {
        ZuneTrack *t = &lib->tracks[i];
        uint64_t sz = 0; uint16_t fmt = 0;
        int missing = zune_probe_object(dev, t->item_id, &sz, &fmt) != 0;
        checked++;
        const char *why = NULL;
        if (missing)                  why = "OBJECT MISSING";
        else if (sz == 0)             why = "ZERO-BYTE OBJECT";
        /* Classic ZMDB filesize parse is unreliable (returns the
         * tracknumber field — parser bug, 2026-08-29 audit); only
         * compare when the ZMDB value is a plausible byte count. */
        else if (t->filesize > (1u << 20) && sz != t->filesize)
            why = "SIZE MISMATCH";
        else if (t->duration_ms == 0) why = "ZERO DURATION";
        if (why) {
            bad++;
            printf("  TRACK [%u] %s — '%s' / '%s' zmdb=%" PRIu64
                   " mtp=%" PRIu64 " dur=%ums fmt=0x%04X\n",
                   t->item_id, why,
                   t->title ? t->title : "(untitled)",
                   t->artist ? t->artist : "(no artist)",
                   t->filesize, sz, t->duration_ms, fmt);
        }
        if (checked % 100 == 0)
            fprintf(stderr, "  ...%d checked\n", checked);
    }
    for (int i = 0; i < lib->video_count; i++) {
        ZuneVideoFile *v = &lib->videos[i];
        uint64_t sz = 0; uint16_t fmt = 0;
        int missing = zune_probe_object(dev, v->item_id, &sz, &fmt) != 0;
        checked++;
        const char *why = NULL;
        if (missing)      why = "OBJECT MISSING";
        else if (sz == 0) why = "ZERO-BYTE OBJECT";
        else if (v->filesize > (1u << 20) && sz != v->filesize)
            why = "SIZE MISMATCH";
        if (why) {
            bad++;
            printf("  VIDEO [%u] %s — '%s' zmdb=%" PRIu64 " mtp=%" PRIu64
                   " fmt=0x%04X\n",
                   v->item_id, why,
                   v->filename ? v->filename : "(unnamed)",
                   v->filesize, sz, fmt);
        }
        if (checked % 100 == 0)
            fprintf(stderr, "  ...%d checked\n", checked);
    }
    /* Playlists: every referenced track id must exist in the ZMDB —
     * dangling refs (tracks purged without rewiring playlists) are a
     * classic playback-crash source on old firmware. */
    for (int i = 0; i < lib->playlist_count; i++) {
        ZunePlaylist *p = &lib->playlists[i];
        for (uint32_t j = 0; j < p->track_count; j++) {
            uint32_t tid = p->track_ids[j];
            int found = 0;
            for (int k = 0; k < lib->track_count && !found; k++)
                if (lib->tracks[k].item_id == tid) found = 1;
            checked++;
            if (!found) {
                bad++;
                printf("  PLAYLIST [%u] '%s' DANGLING REF — slot %u -> "
                       "track %u not in ZMDB\n",
                       p->playlist_id, p->name ? p->name : "(unnamed)", j, tid);
            }
        }
    }
    printf("audit: %d entries checked, %d suspicious\n", checked, bad);
    if (bad)
        printf("purge suspicious ids with: zunetool purge <item_id>, "
               "then eject cleanly so the device re-indexes\n");
    zune_free_scan(lib);
    zune_sever(dev);
    return bad == 0 ? 0 : 1;
}

/* Dump an object's GetObjectReferences list and flag refs that don't
 * exist in the ZMDB (dangling — playback-crash suspects). */
static int cmd_refs(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: zunetool refs <item_id>\n"); return 2; }
    uint32_t id = (uint32_t)strtoul(argv[2], NULL, 0);
    ZuneDeviceHandle dev = connect_or_die();
    ZuneDBLibrary *lib = NULL;
    if (zune_infiltrate(dev, &lib) != 0 || !lib) die(dev, "zune_infiltrate");
    uint32_t *refs = NULL; int n = 0;
    if (zune_get_item_refs(dev, id, &refs, &n) != 0) die(dev, "zune_get_item_refs");
    printf("object 0x%08X: %d references\n", id, n);
    int dangling = 0;
    for (int i = 0; i < n; i++) {
        const char *title = NULL;
        for (int k = 0; k < lib->track_count && !title; k++)
            if (lib->tracks[k].item_id == refs[i]) title = lib->tracks[k].title;
        for (int k = 0; k < lib->video_count && !title; k++)
            if (lib->videos[k].item_id == refs[i]) title = lib->videos[k].filename;
        if (!title) dangling++;
        printf("  [%02d] 0x%08X %s\n", i, refs[i],
               title ? title : "** DANGLING (not in ZMDB) **");
    }
    printf("%d dangling of %d\n", dangling, n);
    free(refs);
    zune_free_scan(lib);
    zune_sever(dev);
    return dangling ? 1 : 0;
}

/* ── Phase 9 research: playlists through every lens ── */

/* MTP enumeration (the mac's route — bypasses the ZMDB parse). */
static int cmd_playlists(void) {
    ZuneDeviceHandle dev = connect_or_die();
    int n = 0;
    ZunePlaylist *pls = zune_get_playlists(dev, &n);
    printf("MTP enumeration: %d playlists\n", n);
    for (int i = 0; i < n; i++) {
        printf("  [%u] '%s' — %u tracks:", pls[i].playlist_id,
               pls[i].name ? pls[i].name : "(unnamed)", pls[i].track_count);
        for (uint32_t j = 0; j < pls[i].track_count; j++)
            printf(" %u", pls[i].track_ids[j]);
        printf("\n");
    }
    zune_free_playlists(pls, n);
    zune_sever(dev);
    return 0;
}

static int cmd_mkplaylist(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: zunetool forge <name> <trackid> [trackid...]\n");
        return 2;
    }
    ZuneDeviceHandle dev = connect_or_die();
    int n = argc - 3;
    uint32_t *ids = calloc(n, sizeof(uint32_t));
    for (int i = 0; i < n; i++)
        ids[i] = (uint32_t)strtoul(argv[3 + i], NULL, 0);
    uint32_t plid = zune_forge_playlist(dev, argv[2], ids, n);
    free(ids);
    if (plid == 0) die(dev, "zune_forge_playlist");
    printf("OK: playlist '%s' created as 0x%08X (%d tracks)\n", argv[2], plid, n);
    zune_finalize(dev);
    zune_sever(dev);
    return 0;
}

static int cmd_zmdbdump(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: zunetool zmdbdump <outfile>\n"); return 2; }
    ZuneDeviceHandle dev = connect_or_die();
    if (zune_dump_raw(dev, argv[2]) != 0) die(dev, "zune_dump_raw");
    printf("OK: ZMDB dumped to %s\n", argv[2]);
    zune_sever(dev);
    return 0;
}

static int cmd_extract(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: zunetool extract <item_id> <dest>\n"); return 2; }
    uint32_t id = (uint32_t)strtoul(argv[2], NULL, 0);
    ZuneDeviceHandle dev = connect_or_die();
    if (zune_extract_track(dev, id, argv[3]) != 0) die(dev, "zune_extract_track");
    printf("OK: %u -> %s\n", id, argv[3]);
    zune_sever(dev);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "zunetool — libzune test harness\n"
            "  zunetool info\n"
            "  zunetool list\n"
            "  zunetool send <file> [title] [artist] [album] [genre]\n"
            "  zunetool purge <item_id>\n"
            "  zunetool torture <file>\n"
            "  zunetool video-title-test <short-wmv>   isolated <=1 MiB title gate\n"
            "  zunetool audit          cross-check ZMDB rows vs real objects\n"
            "  zunetool extract <item_id> <dest>   download a track\n");
        return 2;
    }
    const char *cmd = argv[1];
    if      (!strcmp(cmd, "info"))    return cmd_info();
    else if (!strcmp(cmd, "list"))    return cmd_list();
    else if (!strcmp(cmd, "smuggle")) return cmd_send(argc, argv);
    else if (!strcmp(cmd, "send"))    return cmd_send(argc, argv);   /* civilian alias */
    else if (!strcmp(cmd, "purge"))   return cmd_purge(argc, argv);
    else if (!strcmp(cmd, "torture")) return cmd_torture(argc, argv);
    else if (!strcmp(cmd, "video-title-test")) return cmd_video_title_test(argc, argv);
    else if (!strcmp(cmd, "audit"))   return cmd_audit();
    else if (!strcmp(cmd, "extract")) return cmd_extract(argc, argv);
    else if (!strcmp(cmd, "refs"))    return cmd_refs(argc, argv);
    else if (!strcmp(cmd, "playlists"))  return cmd_playlists();
    else if (!strcmp(cmd, "forge"))      return cmd_mkplaylist(argc, argv);
    else if (!strcmp(cmd, "mkplaylist")) return cmd_mkplaylist(argc, argv); /* civilian alias */
    else if (!strcmp(cmd, "zmdbdump"))   return cmd_zmdbdump(argc, argv);
    fprintf(stderr, "unknown command: %s\n", cmd);
    return 2;
}
