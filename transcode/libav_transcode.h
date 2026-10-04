/*
 * libav_transcode.h — In-process transcoding via MPVKit's libav* + LAME
 *
 * All transcoding, probing, and art extraction without subprocess overhead.
 * Links against MPVKit's libavformat/libavcodec/libswresample/libswscale
 * plus standalone libmp3lame for MP3 encoding.
 */

#ifndef LIBAV_TRANSCODE_H
#define LIBAV_TRANSCODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declare ZuneMetadata from zune.h (avoid circular include) */
struct ZuneMetadata;

/* ---- Progress callback ---- */

typedef void (*zuuned_progress_cb)(float fraction, void *userdata);

/* ---- Cancellation ----
 * Thread-safe, process-global (one transcode at a time). abort() makes
 * the in-flight audio/video transcode bail within one packet — temp
 * file unlinked, NULL returned. The sync engine clears the flag at sync
 * start so a stale cancel can't kill the next run. */
void zuuned_transcode_abort(void);
void zuuned_transcode_clear_abort(void);

/* ---- Hardware decode mode ----
 * 0 = software only
 * 1 = auto (default): NVDEC, then VAAPI, then software
 * 2 = NVDEC (CUDA) only, software fallback
 * 3 = VAAPI (AMD/Intel) only, software fallback
 * macOS ignores the mode beyond 0/non-0 (VideoToolbox). The sync
 * engine mirrors the Settings pick here before each sync. */
void zuuned_transcode_set_hw_decode(int mode);

/* ---- Audio Transcoding ---- */

/*
 * Transcode any audio format to MP3 320kbps CBR with ID3v2.3 tags.
 * Uses MPVKit's libav* for decoding + libswresample for resampling
 * + standalone libmp3lame for MP3 encoding.
 * Metadata (title, artist, album, etc.) is copied from input.
 *
 * album_artist: if non-NULL and non-empty, overrides the output TPE1
 *               (artist) frame so Zune groups tracks under one artist
 *               instead of splitting on "feat. X" strings. Pass NULL
 *               to keep the input's artist tag.
 *
 * Returns: path to temp MP3 file (caller frees + unlinks). NULL on failure.
 */
char *zuuned_transcode_audio(const char *input_path,
                              const char *album_artist,
                              zuuned_progress_cb progress, void *userdata);

/* Positive disc_number explicitly overrides source disc metadata. Zero/negative
 * preserves a valid source disc or discnumber (including its total). */
char *zuuned_transcode_audio_with_disc(const char *input_path, const char *album_artist,
                                      int disc_number, zuuned_progress_cb progress, void *userdata);

/* ---- Video Transcoding ---- */

/*
 * Video transcode profiles (matches VideoProfile Swift enum):
 *   0 = Zune 30:      WMV2 320x240, 800k video, WMA 128k (requires forked MPVKit)
 *   1 = Zune 4-120:   H.264 Baseline 2.1, 320x240, 768k, AAC 128k
 *   2 = Zune HD:       H.264 Baseline 3.1, 480x272, 2500k, AAC 192k
 *   3 = Zune HD 720p: H.264 Baseline 3.1, 1280x720, 8000k, AAC 192k
 */
#define ZUUNED_PROFILE_ZUNE30       0
#define ZUUNED_PROFILE_ZUNE4_120    1
#define ZUUNED_PROFILE_ZUNEHD       2
#define ZUUNED_PROFILE_ZUNEHD_720P  3

/*
 * Transcode video to a Zune-compatible format.
 * Profiles 1-3: h264_videotoolbox (HW!) + AAC → MP4
 * Profile 0:    wmv2 + wmav2 → ASF (requires forked MPVKit with wmv2 encoder)
 *
 * series/season/episode: optional TV metadata (pass NULL/0 to skip).
 * audio_lang: preferred audio language (e.g., "eng"). NULL for default.
 *
 * Returns: path to temp video file (caller frees + unlinks). NULL on failure.
 */
char *zuuned_transcode_video(const char *input_path, int profile,
                              const char *series, int season, int episode,
                              const char *audio_lang,
                              zuuned_progress_cb progress, void *userdata);

/* ---- Probing ---- */

/*
 * Probe file duration in seconds. Returns 0 on success, -1 on failure.
 */
int zuuned_probe_duration(const char *filepath, double *duration_out);

/*
 * Probe duration (seconds) AND video resolution in one header read.
 * Returns 0 when at least one of duration/resolution was found, -1 otherwise.
 */
int zuuned_probe_video_info(const char *filepath, double *duration_out,
                            int *width_out, int *height_out);

/*
 * Find audio stream index matching a language tag (e.g., "eng", "jpn").
 * Returns 0 on success (stream_idx_out set), -1 if not found.
 */
int zuuned_probe_audio_lang(const char *filepath, const char *lang, int *stream_idx_out);

/*
 * Probe full metadata from any media file.
 * Populates a ZuneMetadata struct (title, artist, album, genre, track#, duration).
 * Returns 0 on success, -1 on failure.
 */
int zuuned_probe_metadata(const char *filepath, struct ZuneMetadata *out);

/* ---- Album Art Extraction ---- */

/*
 * Extract embedded album art from a media file, save as JPEG.
 * Scales to max_dimension x max_dimension (preserving aspect ratio).
 * Returns 0 on success, -1 if no art found or extraction failed.
 */
int zuuned_extract_art(const char *filepath, const char *output_jpeg_path,
                        int max_dimension);

/*
 * Decode one video frame at at_seconds (clamped to the file's duration),
 * scale to max_width (preserving aspect ratio), save as JPEG.
 * Used for episode-thumbnail fallbacks when no TMDB still exists.
 * Returns 0 on success, -1 on failure.
 */
int zuuned_extract_frame(const char *filepath, double at_seconds,
                         const char *output_jpeg_path, int max_width);

#ifdef __cplusplus
}
#endif

#endif /* LIBAV_TRANSCODE_H */
