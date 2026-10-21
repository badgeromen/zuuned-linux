/*
 * libzune — transcode.c
 *
 * Audio and video transcoding for Zune-compatible formats.
 * Runs ffmpeg directly (argv, no shell). Returns malloc'd temp file paths
 * that the caller must free() and unlink().
 *
 * Pure C99. No GLib. No GTK.
 */

#include "zune_internal.h"
#include <ctype.h>

/* Unique private output named after the input, for example
 * $TMPDIR/zunelinux_vid_Show_Ab12Cd.mp4. Caller unlinks and frees. */
static char *temp_output(const char *kind, const char *input_path,
                         const char *suffix)
{
    char *base = zune_filename_no_ext(input_path);
    if (!base) return NULL;
    size_t length = strlen(kind) + strlen(base) + 1;
    char *label = malloc(length);
    if (label) snprintf(label, length, "%s%s", kind, base);
    free(base);
    char *path = label ? zune_mktemp(label, suffix) : NULL;
    free(label);
    return path;
}

/* Keep a finished output, or remove a failed one. */
static char *finish_output(char *output, int status, const char *what)
{
    if (status == 0 && zune_tool_output_ok(output)) {
        ZLOG(ZUNE_CHATTER_TALK, "libzune", "%s OK: %s", what, output);
        return output;
    }
    ZLOG(ZUNE_CHATTER_SCREAM, "libzune", "%s FAILED (status=%d)", what, status);
    if (status > 0)
        zune_set_error("ffmpeg %s failed (exit %d)", what, status);
    else if (status == 0)
        zune_set_error("ffmpeg %s produced no output", what);
    unlink(output);
    free(output);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Audio transcode — MP3 320kbps CBR, ID3v2.3                       */
/* ------------------------------------------------------------------ */

char *zune_arm_audio(const char *input_path)
{
    zune_clear_error();
    if (!input_path) return NULL;

    char *output = temp_output("", input_path, ".mp3");
    if (!output) return NULL;

    /* ffmpeg arguments:
     *   -codec:a libmp3lame      explicit LAME encoder
     *   -b:a 320k                constant bitrate (Zune handles CBR best)
     *   -map_metadata -1         strip source metadata (MTP tags take precedence)
     *   -id3v2_version 3         Zune supports ID3v2.3, not v2.4
     */
    const char *const argv[] = {
        "ffmpeg", "-y", "-i", input_path, "-codec:a", "libmp3lame",
        "-b:a", "320k", "-map_metadata", "-1", "-id3v2_version", "3",
        output, NULL
    };
    return finish_output(output, zune_run_tool("ffmpeg", argv, -1),
                         "audio transcode");
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
    zune_clear_error();
    if (!mp3_path) return NULL;

    char *output = temp_output("retag_", mp3_path, ".mp3");
    if (!output) return NULL;

    /* Stream copy — no re-encoding, just retag.
     *   -c copy              no re-encoding (instant)
     *   -map_metadata 0      copy all metadata from input
     *   -id3v2_version 3     downgrade to ID3v2.3
     *   -write_id3v1 1       also write ID3v1.1 tags
     */
    const char *const argv[] = {
        "ffmpeg", "-y", "-i", mp3_path, "-c", "copy", "-map_metadata", "0",
        "-id3v2_version", "3", "-write_id3v1", "1", output, NULL
    };
    return finish_output(output, zune_run_tool("ffmpeg", argv, -1),
                         "ID3v2.3 retag");
}

/* ------------------------------------------------------------------ */
/*  Video profiles — one per Zune model                               */
/*  Encoder arguments between "-i input" and the optional metadata.   */
/*  Every profile forces 8-bit 4:2:0: the Zune decoders need it, and  */
/*  x264's baseline profile refuses 4:4:4/4:2:2 input outright.       */
/* ------------------------------------------------------------------ */

/* 0: Zune 30 — WMV2 320x240 in WMV container */
static const char *const PROFILE_ZUNE30[] = {
    "-c:v", "wmv2", "-b:v", "800k",
    "-vf", "scale=320:240:force_original_aspect_ratio=decrease,"
           "pad=320:240:(ow-iw)/2:(oh-ih)/2", "-pix_fmt", "yuv420p",
    "-c:a", "wmav2", "-b:a", "128k", "-ar", "44100", "-ac", "2", NULL
};

/* 1: Zune 4/8/16/80/120 — H.264 Baseline Level 2.1, 320x240 */
static const char *const PROFILE_ZUNE80[] = {
    "-c:v", "libx264", "-profile:v", "baseline", "-level", "2.1",
    "-b:v", "768k", "-maxrate", "1500k", "-bufsize", "1500k", "-refs", "1",
    "-vf", "scale=320:240:force_original_aspect_ratio=decrease,"
           "pad=320:240:(ow-iw)/2:(oh-ih)/2", "-pix_fmt", "yuv420p",
    "-c:a", "aac", "-b:a", "128k", "-ar", "44100", "-ac", "2",
    "-movflags", "+faststart", NULL
};

/* 2: Zune HD — H.264 Baseline Level 3.1, 480x272 */
static const char *const PROFILE_ZUNEHD[] = {
    "-c:v", "libx264", "-profile:v", "baseline", "-level", "3.1",
    "-b:v", "2500k",
    "-vf", "scale=480:272:force_original_aspect_ratio=decrease,"
           "pad=480:272:(ow-iw)/2:(oh-ih)/2", "-pix_fmt", "yuv420p",
    "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", NULL
};

/* 3: Zune HD 720p — H.264 Baseline Level 3.1, 1280x720 */
static const char *const PROFILE_ZUNEHD_720P[] = {
    "-c:v", "libx264", "-profile:v", "baseline", "-level", "3.1",
    "-b:v", "8000k",
    "-vf", "scale=1280:720:force_original_aspect_ratio=decrease,"
           "pad=1280:720:(ow-iw)/2:(oh-ih)/2", "-pix_fmt", "yuv420p",
    "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", NULL
};

static const char *const *const VIDEO_PROFILES[] = {
    PROFILE_ZUNE30, PROFILE_ZUNE80, PROFILE_ZUNEHD, PROFILE_ZUNEHD_720P
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

/* "key=value" for one -metadata argument. Caller frees. */
static char *metadata_arg(const char *key, const char *value)
{
    size_t length = strlen(key) + 1 + strlen(value) + 1;
    char *arg = malloc(length);
    if (arg) snprintf(arg, length, "%s=%s", key, value);
    return arg;
}

/* Shared implementation for video and TV episode transcoding.
 * If series is non-NULL and episode > 0, TV metadata is embedded. */
static char *transcode_video_impl(const char *input_path, ZuneModel model,
                                  const char *series, int season, int episode)
{
    if (!input_path) return NULL;

    int profile = model_to_profile(model);
    const char *out_ext = (profile == 0) ? ".wmv" : ".mp4";

    /* TV episode metadata. For MP4 ffmpeg embeds iTunes-style atoms that
     * the Zune reads; for WMV (profile 0) they land in the ASF container.
     * Values are whole arguments, so series names need no quoting. */
    char *show = NULL, *season_arg = NULL, *episode_arg = NULL;
    if (series && series[0] && episode > 0) {
        char number[16];
        show = metadata_arg("show", series);
        snprintf(number, sizeof(number), "%d", season);
        season_arg = metadata_arg("season_number", number);
        snprintf(number, sizeof(number), "%d", episode);
        episode_arg = metadata_arg("episode_sort", number);
        if (!show || !season_arg || !episode_arg) {
            free(show); free(season_arg); free(episode_arg);
            zune_set_error("Out of memory preparing episode metadata");
            return NULL;
        }
    }

    char *output = temp_output("vid_", input_path, out_ext);
    if (!output) {
        free(show); free(season_arg); free(episode_arg);
        return NULL;
    }

    /* ffmpeg -y -i input <profile arguments> [metadata] output */
    const char *argv[48];
    size_t argc = 0;
    argv[argc++] = "ffmpeg";
    argv[argc++] = "-y";
    argv[argc++] = "-i";
    argv[argc++] = input_path;
    for (const char *const *arg = VIDEO_PROFILES[profile]; *arg; arg++)
        argv[argc++] = *arg;
    if (show) {
        argv[argc++] = "-metadata"; argv[argc++] = show;
        argv[argc++] = "-metadata"; argv[argc++] = season_arg;
        argv[argc++] = "-metadata"; argv[argc++] = episode_arg;
    }
    argv[argc++] = output;
    argv[argc] = NULL;

    ZLOG(ZUNE_CHATTER_TALK, "libzune", "video transcode profile=%d: %s", profile, input_path);

    int status = zune_run_tool("ffmpeg", argv, -1);
    free(show); free(season_arg); free(episode_arg);
    return finish_output(output, status, "video transcode");
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

char *zune_arm_video(const char *input_path, ZuneModel model)
{
    zune_clear_error();
    return transcode_video_impl(input_path, model, NULL, 0, 0);
}

char *zune_arm_episode(const char *input_path, ZuneModel model,
                                const char *series, int season, int episode)
{
    zune_clear_error();
    return transcode_video_impl(input_path, model, series, season, episode);
}
