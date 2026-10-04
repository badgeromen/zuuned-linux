/*
 * libzune — transcode.c
 *
 * Audio and video transcoding for Zune-compatible formats.
 * Uses popen() to invoke ffmpeg. Returns malloc'd temp file paths
 * that the caller must free() and unlink().
 *
 * Pure C99. No GLib. No GTK.
 */

#include "zune_internal.h"
#include <ctype.h>

/* ---- Shell escaping helper ----
 *
 * Escape single quotes in a string for safe use inside '...' shell quoting.
 * Returns a malloc'd buffer.  Caller frees.
 */
static char *shell_escape(const char *s)
{
    /* Worst case: every char is a quote → 4x expansion ("'\''") + 1 NUL */
    size_t len = strlen(s);
    size_t alloc = len * 4 + 1;
    char *buf = malloc(alloc);
    if (!buf) return NULL;

    char *out = buf;
    for (const char *p = s; *p; p++) {
        if (*p == '\'') {
            *out++ = '\'';
            *out++ = '\\';
            *out++ = '\'';
            *out++ = '\'';
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
    return buf;
}

/* ---- Filename helper (basename without extension) ---- */

static char *filename_no_ext(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);

    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, base, len);
    result[len] = '\0';
    return result;
}

/* ------------------------------------------------------------------ */
/*  Audio transcode — MP3 320kbps CBR, ID3v2.3                       */
/* ------------------------------------------------------------------ */

char *zune_arm_audio(const char *input_path)
{
    if (!input_path) return NULL;

    /* Build temp output path */
    char *base = filename_no_ext(input_path);
    if (!base) return NULL;

    char output[512];
    snprintf(output, sizeof(output), "/tmp/zunelinux_%s_%d.mp3",
             base, (int)getpid());
    free(base);

    /* Escape paths for shell */
    char *esc_in  = shell_escape(input_path);
    char *esc_out = shell_escape(output);
    if (!esc_in || !esc_out) {
        free(esc_in);
        free(esc_out);
        return NULL;
    }

    /* ffmpeg command:
     *   -codec:a libmp3lame      explicit LAME encoder
     *   -b:a 320k                constant bitrate (Zune handles CBR best)
     *   -map_metadata -1         strip source metadata (MTP tags take precedence)
     *   -id3v2_version 3         Zune supports ID3v2.3, not v2.4
     */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "/opt/homebrew/bin/ffmpeg -y -i '%s' -codec:a libmp3lame -b:a 320k "
        "-map_metadata -1 -id3v2_version 3 '%s' 2>&1",
        esc_in, esc_out);
    free(esc_in);
    free(esc_out);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    /* Drain the pipe (ffmpeg writes progress to stderr, piped via 2>&1) */
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        /* Could parse "time=HH:MM:SS.xx" for progress — not needed in lib */
    }

    int status = pclose(pipe);
    if (status != 0 || access(output, F_OK) != 0) {
        fprintf(stderr, "[libzune] audio transcode FAILED (status=%d)\n", status);
        return NULL;
    }

    fprintf(stderr, "[libzune] audio transcode OK: %s\n", output);
    return strdup(output);
}

/* ------------------------------------------------------------------ */
/*  ID3v2.3 retagging — for MP3 files already in native format        */
/*                                                                    */
/*  The Zune ignores ID3v2.4 tags entirely, showing "Unknown Artist"  */
/*  and "Unknown Album". This does a stream copy (no re-encoding)     */
/*  to downgrade tags to ID3v2.3 + ID3v1.1.                          */
/*  Reference: zune-explorer's _retagToId3v23()                       */
/* ------------------------------------------------------------------ */

char *zune_retag(const char *mp3_path)
{
    if (!mp3_path) return NULL;

    char *base = filename_no_ext(mp3_path);
    if (!base) return NULL;

    char output[512];
    snprintf(output, sizeof(output), "/tmp/zunelinux_retag_%s_%d.mp3",
             base, (int)getpid());
    free(base);

    char *esc_in  = shell_escape(mp3_path);
    char *esc_out = shell_escape(output);
    if (!esc_in || !esc_out) {
        free(esc_in);
        free(esc_out);
        return NULL;
    }

    /* Stream copy — no re-encoding, just retag.
     *   -c copy              no re-encoding (instant)
     *   -map_metadata 0      copy all metadata from input
     *   -id3v2_version 3     downgrade to ID3v2.3
     *   -write_id3v1 1       also write ID3v1.1 tags
     */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "/opt/homebrew/bin/ffmpeg -y -i '%s' -c copy -map_metadata 0 "
        "-id3v2_version 3 -write_id3v1 1 '%s' 2>&1",
        esc_in, esc_out);
    free(esc_in);
    free(esc_out);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    char line[512];
    while (fgets(line, sizeof(line), pipe)) { /* drain */ }

    int status = pclose(pipe);
    if (status != 0 || access(output, F_OK) != 0) {
        fprintf(stderr, "[libzune] ID3v2.3 retag FAILED (status=%d)\n", status);
        return NULL;
    }

    fprintf(stderr, "[libzune] retagged to ID3v2.3: %s\n", output);
    return strdup(output);
}

/* ------------------------------------------------------------------ */
/*  Video profiles — one per Zune model                               */
/* ------------------------------------------------------------------ */

static const char *VIDEO_PROFILES[] = {
    /* 0: Zune 30 — WMV2 320x240 in WMV container */
    "/opt/homebrew/bin/ffmpeg -y -i '%s' -c:v wmv2 -b:v 800k "
    "-vf \"scale=320:240:force_original_aspect_ratio=decrease,"
    "pad=320:240:(ow-iw)/2:(oh-ih)/2\" "
    "-c:a wmav2 -b:a 128k -ar 44100 -ac 2 "
    "%s"                          /* metadata placeholder */
    "'%s' 2>&1",

    /* 1: Zune 4/8/16/80/120 — H.264 Baseline Level 2.1, 320x240 */
    "/opt/homebrew/bin/ffmpeg -y -i '%s' -c:v libx264 -profile:v baseline -level 2.1 "
    "-b:v 768k -maxrate 1500k -bufsize 1500k -refs 1 "
    "-vf \"scale=320:240:force_original_aspect_ratio=decrease,"
    "pad=320:240:(ow-iw)/2:(oh-ih)/2\" "
    "-c:a aac -b:a 128k -ar 44100 -ac 2 "
    "-movflags +faststart "
    "%s"                          /* metadata placeholder */
    "'%s' 2>&1",

    /* 2: Zune HD — H.264 Baseline Level 3.1, 480x272 */
    "/opt/homebrew/bin/ffmpeg -y -i '%s' -c:v libx264 -profile:v baseline -level 3.1 "
    "-b:v 2500k "
    "-vf \"scale=480:272:force_original_aspect_ratio=decrease,"
    "pad=480:272:(ow-iw)/2:(oh-ih)/2\" "
    "-c:a aac -b:a 192k "
    "-movflags +faststart "
    "%s"                          /* metadata placeholder */
    "'%s' 2>&1",

    /* 3: Zune HD 720p — H.264 Baseline Level 3.1, 1280x720 */
    "/opt/homebrew/bin/ffmpeg -y -i '%s' -c:v libx264 -profile:v baseline -level 3.1 "
    "-b:v 8000k "
    "-vf \"scale=1280:720:force_original_aspect_ratio=decrease,"
    "pad=1280:720:(ow-iw)/2:(oh-ih)/2\" "
    "-c:a aac -b:a 192k "
    "-movflags +faststart "
    "%s"                          /* metadata placeholder */
    "'%s' 2>&1",
};

/* Map ZuneModel enum to profile index (0-3) */
static int model_to_profile(ZuneModel model)
{
    switch (model) {
    case ZUNE_MODEL_30:      return 0;
    case ZUNE_MODEL_80:      return 1;
    case ZUNE_MODEL_HD:      return 2;
    case ZUNE_MODEL_HD_720P: return 3;
    default:                 return 2;  /* safe default: Zune HD */
    }
}

/* Shared implementation for video and TV episode transcoding.
 * If series is non-NULL and episode > 0, TV metadata is embedded. */
static char *transcode_video_impl(const char *input_path, ZuneModel model,
                                  const char *series, int season, int episode)
{
    if (!input_path) return NULL;

    int profile = model_to_profile(model);
    const char *out_ext = (profile == 0) ? "wmv" : "mp4";

    /* Build temp output path */
    char *base = filename_no_ext(input_path);
    if (!base) return NULL;

    char output[512];
    snprintf(output, sizeof(output), "/tmp/zunelinux_vid_%s_%d.%s",
             base, (int)getpid(), out_ext);
    free(base);

    /* Escape paths */
    char *esc_in  = shell_escape(input_path);
    char *esc_out = shell_escape(output);
    if (!esc_in || !esc_out) {
        free(esc_in);
        free(esc_out);
        return NULL;
    }

    /* Build metadata flags for TV episodes.
     * For MP4: ffmpeg embeds iTunes-style atoms that the Zune reads.
     * For WMV (profile 0): metadata flags get embedded in ASF container. */
    char meta[512] = "";
    if (series && series[0] && episode > 0) {
        char *esc_series = shell_escape(series);
        if (esc_series) {
            snprintf(meta, sizeof(meta),
                     "-metadata show='%s' -metadata season_number='%d' "
                     "-metadata episode_sort='%d' ",
                     esc_series, season, episode);
            free(esc_series);
        }
    }

    /* Build full command from profile template.
     * Each profile has 3 format specifiers: %s (input), %s (meta), %s (output) */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), VIDEO_PROFILES[profile],
             esc_in, meta, esc_out);
    free(esc_in);
    free(esc_out);

    fprintf(stderr, "[libzune] video transcode profile=%d: %s\n", profile, cmd);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        /* Drain pipe */
    }

    int status = pclose(pipe);
    if (status != 0 || access(output, F_OK) != 0) {
        fprintf(stderr, "[libzune] video transcode FAILED (status=%d)\n", status);
        return NULL;
    }

    fprintf(stderr, "[libzune] video transcode OK: %s\n", output);
    return strdup(output);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

char *zune_arm_video(const char *input_path, ZuneModel model)
{
    return transcode_video_impl(input_path, model, NULL, 0, 0);
}

char *zune_arm_episode(const char *input_path, ZuneModel model,
                                const char *series, int season, int episode)
{
    return transcode_video_impl(input_path, model, series, season, episode);
}
