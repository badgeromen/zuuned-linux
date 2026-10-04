/*
 * transcodetool — CLI harness for the Zuuned transcode layer (Phase 2 gate)
 *
 * Exercises every exported function against real media without the app:
 *
 *   transcodetool audio <in>                 → MP3 320 CBR (prints path)
 *   transcodetool video <in> <profile 0-3>   → Zune video (prints path)
 *   transcodetool probe <in>                 → duration + resolution
 *   transcodetool art   <in> <out.jpg>       → embedded album art
 *   transcodetool frame <in> <sec> <out.jpg> → video frame grab
 *   transcodetool retag <in.mp3> <albumartist> → ID3v2.3 rewrite (prints path)
 */

#include "libav_transcode.h"
#include "id3_rewrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void progress_cb(float fraction, void *userdata)
{
    (void)userdata;
    static int last_pct = -1;
    int pct = (int)(fraction * 100.0f);
    if (pct / 10 != last_pct / 10 || pct >= 100) {
        fprintf(stderr, "[progress] %d%%\n", pct);
        last_pct = pct;
    }
}

static int usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  transcodetool audio <in>\n"
        "  transcodetool video <in> <profile 0-3>\n"
        "  transcodetool probe <in>\n"
        "  transcodetool art <in> <out.jpg>\n"
        "  transcodetool frame <in> <seconds> <out.jpg>\n"
        "  transcodetool retag <in.mp3> <albumartist>\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    const char *in = argv[2];

    if (strcmp(cmd, "audio") == 0) {
        char *out = zuuned_transcode_audio(in, NULL, progress_cb, NULL);
        if (!out) { fprintf(stderr, "FAIL: audio transcode\n"); return 1; }
        printf("%s\n", out);
        free(out);
        return 0;
    }

    if (strcmp(cmd, "video") == 0) {
        if (argc < 4) return usage();
        int profile = atoi(argv[3]);
        char *out = zuuned_transcode_video(in, profile, NULL, 0, 0, NULL,
                                           progress_cb, NULL);
        if (!out) { fprintf(stderr, "FAIL: video transcode (profile %d)\n", profile); return 1; }
        printf("%s\n", out);
        free(out);
        return 0;
    }

    if (strcmp(cmd, "probe") == 0) {
        double dur = 0;
        int w = 0, h = 0;
        int vi = zuuned_probe_video_info(in, &dur, &w, &h);
        double d2 = 0;
        int di = zuuned_probe_duration(in, &d2);
        if (vi != 0 && di != 0) { fprintf(stderr, "FAIL: probe\n"); return 1; }
        printf("duration=%.3f\n", dur > 0 ? dur : d2);
        if (w > 0) printf("video=%dx%d\n", w, h);
        return 0;
    }

    if (strcmp(cmd, "art") == 0) {
        if (argc < 4) return usage();
        if (zuuned_extract_art(in, argv[3], 400) != 0) {
            fprintf(stderr, "FAIL: no embedded art\n");
            return 1;
        }
        printf("%s\n", argv[3]);
        return 0;
    }

    if (strcmp(cmd, "frame") == 0) {
        if (argc < 5) return usage();
        if (zuuned_extract_frame(in, atof(argv[3]), argv[4], 400) != 0) {
            fprintf(stderr, "FAIL: frame extract\n");
            return 1;
        }
        printf("%s\n", argv[4]);
        return 0;
    }

    if (strcmp(cmd, "retag") == 0) {
        if (argc < 4) return usage();
        char *out = zuuned_retag_mp3(in, argv[3]);
        if (!out) { fprintf(stderr, "FAIL: retag\n"); return 1; }
        printf("%s\n", out);
        free(out);
        return 0;
    }

    return usage();
}
