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
#include "media_capabilities.h"
#include "hd_artist_probe.h"
#include "hd_tunnel_probe.h"
#include "hd_network_probe.h"
#include "hd_art_serve.h"
#include "hd_http_init.h"
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

static int cmd_media_capabilities(int argc, char **argv) {
    uint16_t format = 0;
    if (argc != 3 || media_probe_format(argv[2], &format) != 0) {
        fprintf(stderr, "usage: zunetool media-capabilities <format-code, e.g. 0xBA0B>\n");
        return 2;
    }
    ZuneDeviceHandle dev = connect_or_die();
    int result = media_probe_report(dev, format, stdout);
    zune_sever(dev);
    return result == 0 ? 0 : 1;
}

static int cmd_hd_art_probe(int argc, char **argv) {
    unsigned samples = 3;
    if (argc < 2 || argc > 3 || (argc == 3 && hd_art_sample_count(argv[2], &samples))) {
        fprintf(stderr, "usage: zunetool hd-art-probe [sample-count 1..8]\n");
        return 2;
    }
    ZuneDeviceHandle dev = connect_or_die();
    int result = hd_artist_probe_report(dev, samples, stdout);
    zune_sever(dev);
    return result == 0 ? 0 : 1;
}

#include "hd_art_binding_cli.h"

static volatile sig_atomic_t hd_tunnel_stop;
static void hd_tunnel_interrupt(int sig) { (void)sig; hd_tunnel_stop = 1; }
static int cmd_hd_tunnel_probe(int argc, char **argv) {
    int ack = argc == 3 && !strcmp(argv[2], "--ack-lcp");
    if (argc != 2 && !ack) {
        fprintf(stderr, "usage: zunetool hd-tunnel-probe [--ack-lcp]\n");
        return 2;
    }
    ZuneDeviceHandle dev = connect_or_die();
    hd_tunnel_stop = 0;
    void (*old_int)(int) = signal(SIGINT, hd_tunnel_interrupt);
    void (*old_term)(int) = signal(SIGTERM, hd_tunnel_interrupt);
    int result = hd_tunnel_probe_report(dev, ack, stdout, &hd_tunnel_stop);
    if (old_int != SIG_ERR) signal(SIGINT, old_int);
    if (old_term != SIG_ERR) signal(SIGTERM, old_term);
    zune_sever(dev);
    return result == 0 ? 0 : 1;
}

#include "hd_art_first_recovery.h"
#include "hd_art_graph_recovery.h"
#include "hd_art_album_recovery.h"
static int cmd_hd_art_album_inspect(int argc,char **argv) {
    if(argc!=4){fprintf(stderr,"usage: zunetool hd-art-album-inspect plan.tsv journal\n");return 2;}
    HdAlbumSync album={0};album.fd=album.wire.fd=-1;
    if(hd_album_local(&album,argv[2])||hd_album_recovery_load(&album,argv[3])){
        hd_album_release(&album);fprintf(stderr,"Album plan or recovery journal rejected; no device accessed.\n");return 2;
    }
    ZuneDeviceHandle dev=connect_or_die();hd_tunnel_stop=0;
    void (*old_int)(int)=signal(SIGINT,hd_tunnel_interrupt);
    void (*old_term)(int)=signal(SIGTERM,hd_tunnel_interrupt);
    album.dev=dev;album.out=stdout;album.stop=&hd_tunnel_stop;album.now_fn=hd_art_ready_now;
    album.deadline=album.now_fn()+120000;
    int result=hd_album_recovery_inspect(&album);
    if(old_int!=SIG_ERR)signal(SIGINT,old_int);
    if(old_term!=SIG_ERR)signal(SIGTERM,old_term);
    hd_cli_sever(dev,album.uncertain);hd_album_release(&album);return result?1:0;
}
static int cmd_hd_art_first_inspect(int argc,char **argv) {
    if(argc!=4){fprintf(stderr,"usage: zunetool hd-art-first-inspect manifest fixture-mp3\n");return 2;}
    HdFirstSync s,local;
    if(hd_first_graph_load(&s,argv[2]))return 2;
    if(hd_first_local(&local,argv[3])){hd_first_release(&s);return 2;}
    if(local.mp3_size!=s.mp3_size){hd_first_release(&local);hd_first_release(&s);return 2;}
    s.mp3=local.mp3;local.mp3=NULL;hd_first_release(&local);
    ZuneDeviceHandle dev=connect_or_die();s.dev=dev;s.out=stdout;s.now_fn=hd_art_ready_now;s.deadline=s.now_fn()+120000;
    int rc=hd_first_graph_diagnose(&s);hd_cli_sever(dev,s.uncertain);hd_first_release(&s);return rc?1:0;
}
static int cmd_hd_art_recover_graph(int argc,char **argv) {
    int remove=argc==6&&!strcmp(argv[5],"--delete"),retain=argc==6&&!strcmp(argv[5],"--retain");
    if(argc!=5&&!remove&&!retain){fprintf(stderr,"usage: zunetool hd-art-recover-graph original-manifest fixture-mp3 new-proof [--delete|--retain]\n");return 2;}
    HdFirstSync s,local;
    if(hd_first_graph_recovery_load(&s,argv[2])){fprintf(stderr,"Graph manifest rejected; no device accessed.\n");return 2;}
    if(hd_first_local(&local,argv[3])){hd_first_release(&s);return 2;}
    if(local.mp3_size!=s.mp3_size){hd_first_release(&local);hd_first_release(&s);return 2;}
    s.mp3=local.mp3;local.mp3=NULL;hd_first_release(&local);
    ZuneDeviceHandle dev=connect_or_die();hd_tunnel_stop=0;
    void (*old_int)(int)=signal(SIGINT,hd_tunnel_interrupt);void (*old_term)(int)=signal(SIGTERM,hd_tunnel_interrupt);
    s.dev=dev;s.out=stdout;s.stop=&hd_tunnel_stop;s.now_fn=hd_art_ready_now;s.deadline=s.now_fn()+600000;
    int rc=hd_first_graph_recovery_inspect(&s);
    if(!rc)rc=hd_first_recovery_proof(&s,argv[4]);
    if(!rc)rc=hd_first_graph_extra_record(&s);
    if(!rc&&remove)rc=hd_first_graph_recovery_delete(&s);
    if(!rc&&retain)rc=hd_first_graph_recovery_retain(&s);
    if(!rc&&!remove&&!retain)fprintf(stdout,"first-sync-graph-recovery\tinspection only; no device mutation\n");
    if(old_int!=SIG_ERR)signal(SIGINT,old_int);
    if(old_term!=SIG_ERR)signal(SIGTERM,old_term);
    hd_cli_sever(dev,s.uncertain);hd_first_release(&s);return rc?1:0;
}
static int cmd_hd_art_recover_music(int argc,char **argv) {
    int remove=argc==5&&!strcmp(argv[4],"--delete");
    if((argc!=4&&!remove)||!argv[2][0]||!argv[3][0]) {
        fprintf(stderr,"usage: zunetool hd-art-recover-music original-manifest new-proof-file [--delete]\n");return 2;
    }
    HdFirstSync s;
    if(hd_first_recovery_load(&s,argv[2])) {
        fprintf(stderr,"Recovery manifest rejected; no device accessed.\n");return 2;
    }
    ZuneDeviceHandle dev=connect_or_die();hd_tunnel_stop=0;
    void (*old_int)(int)=signal(SIGINT,hd_tunnel_interrupt);
    void (*old_term)(int)=signal(SIGTERM,hd_tunnel_interrupt);
    s.dev=dev;s.out=stdout;s.stop=&hd_tunnel_stop;s.now_fn=hd_art_ready_now;s.deadline=s.now_fn()+120000;
    int rc=hd_first_recovery_inspect(&s);
    if(!rc)rc=hd_first_recovery_proof(&s,argv[3]);
    if(!rc&&remove)rc=hd_first_recovery_delete(&s);
    if(!rc&&!remove)fprintf(stdout,"first-sync-recovery\tinspection only; no device mutation\n");
    if(rc)fprintf(stderr,"Recovery refused or incomplete; preserve manifests.\n");
    if(old_int!=SIG_ERR)signal(SIGINT,old_int);
    if(old_term!=SIG_ERR)signal(SIGTERM,old_term);
    hd_cli_sever(dev,s.uncertain);hd_first_release(&s);return rc?1:0;
}

static int cmd_hd_art_serve(int argc,char **argv) {
    int initialize=0,explicit_artist=0,first_preflight=0,album_preflight=0,retain=0,duration_explicit=0;unsigned seconds=60,fixture_ms=3000;const char *artist="Local artist";
    const char *bind_handle=NULL,*bind_backup=NULL,*first_mp3=NULL,*first_manifest=NULL;uint32_t bind_id=0;
    HdFirstSync first={0};first.fd=-1;HdArtGallery gallery={0};
    HdAlbumSync album={0};album.fd=album.wire.fd=-1;const char *album_plan=NULL,*album_journal=NULL,*album_previous=NULL;
    const char *gallery_ids[HDIS_GALLERY_MAX],*gallery_paths[HDIS_GALLERY_MAX],*primary=NULL;size_t gallery_count=0;
    const char *thumbnail_id=NULL,*thumbnail_path=NULL,*biography_path=NULL;
    char biography_text[HDIS_BIO_MAX+1];size_t biography_n=0;
    if(argc<4)goto usage;
    for(int i=4;i<argc;++i) {
        if(!strcmp(argv[i],"--init-9231")&&!initialize)initialize=1;
        else if(!strcmp(argv[i],"--init-full")&&!initialize)initialize=2;
        else if(!strcmp(argv[i],"--artist-name")&&i+1<argc&&!explicit_artist){artist=argv[++i];explicit_artist=1;}
        else if(!strcmp(argv[i],"--bind-on-ready")&&i+2<argc&&!bind_handle){bind_handle=argv[++i];bind_backup=argv[++i];}
        else if(!strcmp(argv[i],"--first-sync")&&i+2<argc&&!first_mp3){first_mp3=argv[++i];first_manifest=argv[++i];}
        else if(!strcmp(argv[i],"--album-sync")&&i+2<argc&&!album_plan){album_plan=argv[++i];album_journal=argv[++i];}
        else if(!strcmp(argv[i],"--album-resume")&&i+3<argc&&!album_plan){album_plan=argv[++i];album_previous=argv[++i];album_journal=argv[++i];}
        else if(!strcmp(argv[i],"--first-sync-preflight")&&!first_preflight)first_preflight=1;
        else if(!strcmp(argv[i],"--album-preflight")&&!album_preflight)album_preflight=1;
        else if(!strcmp(argv[i],"--retain")&&!retain)retain=1;
        else if(!strcmp(argv[i],"--gallery")&&i+2<argc&&gallery_count<HDIS_GALLERY_MAX){gallery_ids[gallery_count]=argv[++i];gallery_paths[gallery_count++]=argv[++i];}
        else if(!strcmp(argv[i],"--primary")&&i+1<argc&&!primary)primary=argv[++i];
        else if(!strcmp(argv[i],"--biography")&&i+1<argc&&!biography_path)biography_path=argv[++i];
        else if(!strcmp(argv[i],"--thumbnail")&&i+2<argc&&!thumbnail_id){thumbnail_id=argv[++i];thumbnail_path=argv[++i];}
        else if(!strcmp(argv[i],"--first-sync-duration-ms")&&i+1<argc&&!duration_explicit){
            char *end=NULL;errno=0;const char *value=argv[++i];unsigned long parsed=strtoul(value,&end,10);
            if(errno||!*value||*end||value[0]<'0'||value[0]>'9'||parsed<1000||parsed>60000)goto usage;
            fixture_ms=(unsigned)parsed;duration_explicit=1;
        }
        else if(!strcmp(argv[i],"--seconds")&&i+1<argc) {
            char *end=NULL;errno=0;const char *value=argv[++i];
            unsigned long parsed=strtoul(value,&end,10);
            if(errno||!*value||*end||value[0]<'0'||value[0]>'9'||parsed<1||parsed>360)goto usage;
            seconds=(unsigned)parsed;
        } else goto usage;
    }
    if(biography_path && hd_art_load_biography(biography_path,biography_text,&biography_n)) {
        fprintf(stderr,"Invalid biography file (1..4096 UTF-8 bytes required); no device accessed.\n");return 2;
    }
    if(album_preflight&&!album_plan)goto usage;
    if(album_previous&&(!*album_previous||!strcmp(album_previous,album_journal)))goto usage;
    if(album_plan&&(first_mp3||bind_handle||retain||first_preflight||duration_explicit||explicit_artist||!album_journal||!*album_journal))goto usage;
    if(primary&&!gallery_count)goto usage;
    if(thumbnail_id ? (!gallery_count||primary) : gallery_count>=HDIS_LEGACY_ASSETS)goto usage;
    if(gallery_count&&seconds<5)goto usage;
    if((first_preflight||retain||duration_explicit)&&!first_mp3)goto usage;
    if(first_preflight&&retain)goto usage;
    if(first_mp3&&(bind_handle||strcmp(argv[2],hd_art_test_guid)||!first_manifest||!*first_manifest))goto usage;
    if(first_mp3){
        if(explicit_artist&&!hd_first_probe_name(artist))goto usage;
        if(hd_first_local(&first,first_mp3)){fprintf(stderr,"Invalid bounded MP3 fixture; no device accessed.\n");return 2;}
        if(explicit_artist)snprintf(first.name,sizeof(first.name),"%s",artist);
        artist=first.name;first.retain_requested=retain;first.fixture_duration_ms=fixture_ms;
    }
    if(bind_handle&&hd_art_ready_options(bind_handle,bind_backup,argv[2],artist,explicit_artist,&bind_id))goto usage;
    if(album_plan){
        if(hd_album_local(&album,album_plan)){fprintf(stderr,"Invalid bounded album plan or local files; no device accessed.\n");return 2;}
        if(hd_album_set_guid(&album,argv[2])){hd_album_release(&album);fprintf(stderr,"Invalid album routing GUID; no device accessed.\n");return 2;}
        if(album_previous&&(hd_album_recovery_load(&album,album_previous)||strcasecmp(album.guid,argv[2]))){
            hd_album_release(&album);fprintf(stderr,"Album recovery journal or matching routing GUID rejected; no device accessed.\n");return 2;
        }
        if(album.cover&&hd_art_jpeg_decode(album.cover,album.cover_size)){hd_album_release(&album);fprintf(stderr,"Invalid decoded album cover; no device accessed.\n");return 2;}
        artist=album.artist;
    }
    size_t size=0;uint8_t *image=hd_art_load_jpeg(argv[3],&size);
    HdArtServing check={0};
    if(!image || (album_plan&&hd_art_jpeg_decode(image,size)) || hd_image_server_init(&check.server,0xc0a83764,argv[2],image,size,hd_art_emit,&check)||
       (gallery_count&&((thumbnail_id?hd_art_gallery_load_roles(&gallery,&check.server,gallery_ids,gallery_paths,gallery_count,thumbnail_id,thumbnail_path,hd_art_load_jpeg):
        hd_art_gallery_load(&gallery,&check.server,gallery_ids,gallery_paths,gallery_count,primary,hd_art_load_jpeg))||
        hd_art_gallery_configure(&check.server,&gallery)))||
       hd_image_server_set_artist(&check.server,artist)||
       (biography_path&&hd_image_server_set_biography(&check.server,biography_text,biography_n))) {
        hd_art_gallery_release(&gallery);free(image);hd_first_release(&first);hd_album_release(&album);fprintf(stderr,"Invalid GUID, artist name, biography, gallery role or JPEG (maximum 128 KiB each; six legacy assets or six gallery images plus background and thumbnail). No device accessed.\n");return 2;
    }
    ZuneDeviceHandle dev=connect_or_die();hd_tunnel_stop=0;
    void (*old_int)(int)=signal(SIGINT,hd_tunnel_interrupt);
    void (*old_term)(int)=signal(SIGTERM,hd_tunnel_interrupt);
    int uncertain=0,result=0;HdArtReadyBinding binding;
    hd_art_ready_init(&binding,dev,stdout,&hd_tunnel_stop);
    if(bind_handle){
        result=hd_art_ready_prepare(&binding,bind_id,artist,bind_backup);
        uncertain=binding.uncertain;
    }
    if(first_mp3){
        first.dev=dev;first.out=stdout;first.stop=&hd_tunnel_stop;first.storage=dev->storage_id;first.now_fn=hd_art_ready_now;
        first.deadline=first.now_fn()+600000;
        if(!dev->serial||strlen(dev->serial)>=sizeof(first.serial))result=-1;
        else{strcpy(first.serial,dev->serial);result=hd_first_prepare(&first,first_manifest);}
        uncertain=first.uncertain;
    }
    if(!result&&album_plan){
        album.dev=dev;album.out=stdout;album.stop=&hd_tunnel_stop;album.now_fn=hd_art_ready_now;
        album.deadline=album.now_fn()+600000;
        if(album_previous){
            result=hd_album_recovery_inspect(&album);
            if(!result&&!album_preflight)result=hd_album_recovery_prepare(&album,album_journal);
        }else result=hd_album_prepare(&album,album_journal);
        uncertain=album.uncertain;
    }
    if(!result&&!first_preflight&&!album_preflight&&initialize==2)result=hd_http_init_full_ex(dev,stdout,&hd_tunnel_stop,&uncertain);
    else if(!result&&!first_preflight&&!album_preflight&&initialize==1){
        HdHttpInitResult initial;
        result=hd_http_init_run(dev,stdout,&hd_tunnel_stop,&initial);
        uncertain=initial.transport_uncertain;
    }
    if(!result&&!first_preflight&&!album_preflight)result=hd_art_serve_experiment_content(dev,argv[2],artist,image,size,seconds*1000,&hd_tunnel_stop,stdout,&uncertain,bind_handle?&binding:NULL,first_mp3?&first:NULL,gallery_count?&gallery:NULL,album_plan?&album:NULL,biography_path?biography_text:NULL,biography_n);
    if(old_int!=SIG_ERR)signal(SIGINT,old_int);
    if(old_term!=SIG_ERR)signal(SIGTERM,old_term);
    hd_cli_sever(dev,uncertain);hd_art_gallery_release(&gallery);free(image);hd_first_release(&first);hd_album_release(&album);return result?1:0;
 usage:
    fprintf(stderr,"usage: zunetool hd-art-serve <routing-guid> <jpeg> [--artist-name name] [--init-9231|--init-full] [--seconds 1..360] [--bind-on-ready handle new-backup-file | --first-sync short-mp3 new-manifest | --album-sync plan.tsv new-journal | --album-resume plan.tsv old-journal new-journal] [--first-sync-preflight|--album-preflight] [--retain] [--first-sync-duration-ms 1000..60000] [--gallery image-uuid jpeg (up to 5 legacy, 6 with thumbnail)] [--primary image-uuid | --thumbnail image-uuid jpeg] [--biography utf8.txt]\n");return 2;
}

static int cmd_hd_network_probe(int argc) {
    if (argc != 2) { fprintf(stderr, "usage: zunetool hd-network-probe\n"); return 2; }
    ZuneDeviceHandle dev = connect_or_die();
    hd_tunnel_stop = 0;
    void (*old_int)(int) = signal(SIGINT, hd_tunnel_interrupt);
    void (*old_term)(int) = signal(SIGTERM, hd_tunnel_interrupt);
    int uncertain=0;
    int result = hd_network_probe_run_ex_status(dev,stdout,&hd_tunnel_stop,NULL,NULL,NULL,0,60000,&uncertain);
    if (old_int != SIG_ERR) signal(SIGINT, old_int);
    if (old_term != SIG_ERR) signal(SIGTERM, old_term);
    hd_cli_sever(dev,uncertain);
    return result == 0 ? 0 : 1;
}

#include "podcast_test.h"
#include "podcast_send.h"
#include "audiobook_test.h"

static int cmd_podcasts(int argc) {
    if (argc != 2) { fprintf(stderr, "usage: zunetool podcasts\n"); return 2; }
    ZuneDeviceHandle dev = connect_or_die();
    ZunePodcastSeries *series = NULL;
    int count = 0;
    if (zune_get_podcast_series(dev, &series, &count) != 0) die(dev, "podcast series inventory");
    printf("%d podcast series:\n", count);
    for (int i = 0; i < count; ++i)
        printf("  [%u] %s | %s\n", series[i].item_id, series[i].name, series[i].author);
    zune_free_podcast_series(series, count);
    zune_sever(dev);
    return 0;
}

/* The supported catalog command is the same public-only C example client. */
#define main hd_art_public_main
#include "../examples/hd_artwork.c"
#undef main

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "zunetool — libzune test harness\n"
            "  zunetool info\n"
            "  zunetool media-capabilities <format-code>   read-only descriptor probe\n"
            "  zunetool hd-art-probe [1..8]   read-only HD artist identities and capabilities\n"
            "  zunetool hd-tunnel-probe [--ack-lcp]   experimental HD USB tunnel/LCP gate\n"
            "  zunetool hd-network-probe   experimental HD PPP/IP observation gate\n"
            "  zunetool hd-art-recover-music <original-manifest> <new-proof-file> [--delete]\n"
            "  zunetool hd-art-album-inspect plan.tsv journal   read-only interrupted album inspection\n"
            "  zunetool hd-art-publish --validate|--serve ARTIST UUID BACKGROUND THUMBNAIL BIO|- [GALLERY_UUID JPEG]...\n"
            "  zunetool hd-art-serve <guid> <jpeg> [--artist-name name] [--init-9231|--init-full] [--seconds 1..360] [--bind-on-ready handle new-backup-file | --first-sync short-mp3 new-manifest | --album-sync plan.tsv new-journal | --album-resume plan.tsv old-journal new-journal] [--first-sync-preflight|--album-preflight] [--retain] [--first-sync-duration-ms 1000..60000] [--gallery image-uuid jpeg (up to 5 legacy, 6 with thumbnail)] [--primary image-uuid | --thumbnail image-uuid jpeg] [--biography utf8.txt]\n"
            "  zunetool hd-art-bind <handle> <exact-name> <backup>   experimental DA97 mapping\n"
            "  zunetool hd-art-restore <backup>   restore exact snapshotted artist mapping\n"
            "  zunetool podcasts     read-only experimental series inventory\n"
            "  zunetool audiobook-test <part1.mp3> <part2.mp3> <cover.jpg> [--retain]   isolated native book gate\n"
            "  zunetool podcast-test <short-mp3> [title]   isolated upload/readback/cleanup gate\n"
            "  zunetool podcast-send <mp3> <feed-url> <show> <title> <author> <episode-url> <date>   retain for playback testing\n"
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
    else if (!strcmp(cmd, "media-capabilities")) return cmd_media_capabilities(argc, argv);
    else if (!strcmp(cmd, "hd-art-probe")) return cmd_hd_art_probe(argc, argv);
    else if (!strcmp(cmd, "hd-tunnel-probe")) return cmd_hd_tunnel_probe(argc, argv);
    else if (!strcmp(cmd, "hd-art-bind")) return cmd_hd_art_bind(argc, argv);
    else if (!strcmp(cmd, "hd-art-restore")) return cmd_hd_art_restore(argc, argv);
    else if (!strcmp(cmd, "hd-art-recover-graph")) return cmd_hd_art_recover_graph(argc, argv);
    else if (!strcmp(cmd, "hd-art-album-inspect")) return cmd_hd_art_album_inspect(argc, argv);
    else if (!strcmp(cmd, "hd-art-first-inspect")) return cmd_hd_art_first_inspect(argc, argv);
    else if (!strcmp(cmd, "hd-art-recover-music")) return cmd_hd_art_recover_music(argc, argv);
    else if (!strcmp(cmd, "hd-art-publish")) return hd_art_public_main(argc-1, argv+1);
    else if (!strcmp(cmd, "hd-art-serve")) return cmd_hd_art_serve(argc, argv);
    else if (!strcmp(cmd, "hd-network-probe")) return cmd_hd_network_probe(argc);
    else if (!strcmp(cmd, "audiobook-test")) return cmd_audiobook_test(argc, argv);
    else if (!strcmp(cmd, "podcasts")) return cmd_podcasts(argc);
    else if (!strcmp(cmd, "podcast-test")) return cmd_podcast_test(argc, argv);
    else if (!strcmp(cmd, "podcast-send")) return cmd_podcast_send(argc, argv);
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
