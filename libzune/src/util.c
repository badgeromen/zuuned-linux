/*
 * libzune — util.c
 *
 * Utility functions: video filename parsing and thumbnail generation.
 * Uses POSIX regex (regex.h) instead of GLib GRegex.
 *
 * Pure C99. No GLib. No GTK.
 */

#include "zune_internal.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <regex.h>
#include <strings.h>
#ifndef USE_LIBAV
#include <fcntl.h>
#include <sys/wait.h>
#endif

/* ------------------------------------------------------------------ */
/*  Path helpers (replacing GLib g_path_get_*)                        */
/* ------------------------------------------------------------------ */

/* Return the directory component of a path. Caller frees. */
static char *path_get_dirname(const char *path)
{
    if (!path || !path[0]) return strdup(".");

    const char *last_slash = strrchr(path, '/');
    if (!last_slash) return strdup(".");

    /* Handle root "/" */
    if (last_slash == path) return strdup("/");

    size_t len = (size_t)(last_slash - path);
    char *dir = malloc(len + 1);
    if (!dir) return NULL;
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

/* Return the base filename component of a path. Caller frees. */
static char *path_get_basename(const char *path)
{
    if (!path || !path[0]) return strdup(".");

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (!base[0]) return strdup("/");
    return strdup(base);
}

/* ------------------------------------------------------------------ */
/*  String helpers                                                     */
/* ------------------------------------------------------------------ */

/* Strip leading/trailing whitespace, dashes, and underscores from a
 * substring [s, s+len).  Returns malloc'd string or NULL if empty. */
static char *strip_trim(const char *s, size_t len)
{
    while (len > 0 && (s[0] == ' ' || s[0] == '-' || s[0] == '_')) {
        s++;
        len--;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '-'
                        || s[len - 1] == '_')) {
        len--;
    }
    if (len == 0) return NULL;

    char *r = malloc(len + 1);
    if (!r) return NULL;
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

/* Strip trailing " (YYYY)" from series name using POSIX regex.
 * Also strip trailing " - Extras" / " Extras" and set season=0. */
static void clean_series_name(char **series, int *season)
{
    if (!series || !*series) return;

    /* Remove trailing (YYYY) year */
    regex_t re;
    regmatch_t match[1];
    if (regcomp(&re, "[ \t]*\\([0-9][0-9][0-9][0-9]\\)[ \t]*$",
                REG_EXTENDED) == 0) {
        if (regexec(&re, *series, 1, match, 0) == 0) {
            (*series)[match[0].rm_so] = '\0';
            /* Trim trailing whitespace after removal */
            size_t len = strlen(*series);
            while (len > 0 && (*series)[len - 1] == ' ') {
                (*series)[--len] = '\0';
            }
        }
        regfree(&re);
    }

    /* Handle " - Extras" or " Extras" → season 0 */
    char *p = strcasestr(*series, " - Extras");
    if (!p) p = strcasestr(*series, " Extras");
    if (p) {
        *p = '\0';
        if (season) *season = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  zune_decode_filename                                         */
/*                                                                    */
/*  Detect TV series metadata from a video filename.                  */
/*  Returns 0 if no pattern (movie), 1 if TV episode detected.       */
/* ------------------------------------------------------------------ */

int zune_decode_filename(const char *filepath,
                              char **out_series, int *out_season,
                              int *out_episode)
{
    if (!filepath || !out_series || !out_season || !out_episode)
        return 0;

    *out_series  = NULL;
    *out_season  = 1;
    *out_episode = 0;

    /* Get filename without path and extension */
    const char *base = strrchr(filepath, '/');
    base = base ? base + 1 : filepath;
    const char *dot = strrchr(base, '.');
    size_t namelen = dot ? (size_t)(dot - base) : strlen(base);

    char *name = malloc(namelen + 1);
    if (!name) return 0;
    memcpy(name, base, namelen);
    name[namelen] = '\0';

    /* ---- Pattern 1: SxxExx (case insensitive) ----
     * e.g. "Show Name - S01E05 - Episode Title" */
    {
        const char *p = name;
        while (*p) {
            if ((*p == 'S' || *p == 's') && isdigit((unsigned char)p[1])) {
                int s = 0, e = 0;
                const char *sp = p + 1;
                while (isdigit((unsigned char)*sp)) {
                    s = s * 10 + (*sp - '0');
                    sp++;
                }
                if ((*sp == 'E' || *sp == 'e')
                    && isdigit((unsigned char)sp[1])) {
                    sp++;
                    while (isdigit((unsigned char)*sp)) {
                        e = e * 10 + (*sp - '0');
                        sp++;
                    }
                    *out_season  = s;
                    *out_episode = e;
                    *out_series  = strip_trim(name, (size_t)(p - name));

                    /* If no series name from filename, try grandparent dir
                     * e.g. .../That 70s Show/Season 1/S01E01.mkv */
                    if (!*out_series) {
                        char *dir = path_get_dirname(filepath);
                        char *gp  = dir ? path_get_dirname(dir) : NULL;
                        if (gp) {
                            char *folder = path_get_basename(gp);
                            if (folder && strcmp(folder, ".") != 0
                                && strcmp(folder, "/") != 0) {
                                *out_series = folder;
                            } else {
                                free(folder);
                            }
                            free(gp);
                        }
                        free(dir);
                    }
                    if (!*out_series)
                        *out_series = strdup("Unknown Series");

                    free(name);
                    clean_series_name(out_series, out_season);
                    return 1;
                }
            }
            p++;
        }
    }

    /* ---- Pattern 2: "Episode XX" / "EP XX" / "EP88" / "Ep. XX" ----
     * Uses POSIX regex (case insensitive). The keyword may be glued to the
     * digits ("One Piece EP88", "One.Piece.EP1167") — common in web rips —
     * so the separator is optional. POSIX ERE has no \b, so a leading
     * non-letter guard keeps "Deep6" / "Step2" from matching their "ep".
     * matches[1] = guard, matches[2] = keyword, matches[3] = digits */
    {
        regex_t re;
        regmatch_t matches[4];
        if (regcomp(&re,
                    "(^|[^A-Za-z])(episode|ep\\.?)[ \t]*([0-9]+)",
                    REG_EXTENDED | REG_ICASE) == 0) {
            if (regexec(&re, name, 4, matches, 0) == 0) {
                *out_episode = atoi(name + matches[3].rm_so);
                *out_series  = strip_trim(name, (size_t)matches[0].rm_so);
                if (!*out_series)
                    *out_series = strdup("Unknown Series");
            }
            regfree(&re);

            if (*out_episode > 0) {
                free(name);
                clean_series_name(out_series, out_season);
                return 1;
            }
        }
    }

    /* ---- Pattern 3: " - XX - " (number between dashes) ---- */
    {
        regex_t re;
        regmatch_t matches[2];
        if (regcomp(&re, "[ \t]*-[ \t]*([0-9]{1,3})[ \t]*-[ \t]*",
                    REG_EXTENDED) == 0) {
            if (regexec(&re, name, 2, matches, 0) == 0) {
                *out_episode = atoi(name + matches[1].rm_so);
                *out_series  = strip_trim(name, (size_t)matches[0].rm_so);
                if (!*out_series)
                    *out_series = strdup("Unknown Series");
            }
            regfree(&re);

            if (*out_episode > 0) {
                free(name);
                clean_series_name(out_series, out_season);
                return 1;
            }
        }
    }

    /* ---- Pattern 3b: "  NNN - " (anime: double-space + number + dash) ----
     * e.g., "Naruto  001 - Enter Naruto Uzumaki"
     * Requires 2+ whitespace chars before the number to avoid false positives
     * on movies that have year or sequel numbers (e.g., "Airplane 2"). */
    {
        regex_t re;
        regmatch_t matches[2];
        if (regcomp(&re, "[ \t]{2,}([0-9]{1,3})[ \t]*-[ \t]*",
                    REG_EXTENDED) == 0) {
            if (regexec(&re, name, 2, matches, 0) == 0) {
                *out_episode = atoi(name + matches[1].rm_so);
                *out_series  = strip_trim(name, (size_t)matches[0].rm_so);
                if (!*out_series)
                    *out_series = strdup("Unknown Series");
            }
            regfree(&re);

            if (*out_episode > 0) {
                free(name);
                clean_series_name(out_series, out_season);
                return 1;
            }
        }
    }

    /* ---- Pattern 3c: " E## " or trailing " E##" (episode-only, no season) ----
     * e.g., "Fullmetal Alchemist Brotherhood E01"
     * Requires a leading whitespace so "Episode 1" (already handled in Pat 2)
     * and things like "Gate E12" aren't confused with real "E## " suffixes.
     * Uses word-boundary-ish check: must be preceded by whitespace. */
    {
        regex_t re;
        regmatch_t matches[2];
        if (regcomp(&re, "[ \t]+[Ee]([0-9]{1,3})([ \t]|$)",
                    REG_EXTENDED) == 0) {
            if (regexec(&re, name, 2, matches, 0) == 0) {
                *out_episode = atoi(name + matches[1].rm_so);
                *out_series  = strip_trim(name, (size_t)matches[0].rm_so);
                if (!*out_series)
                    *out_series = strdup("Unknown Series");
            }
            regfree(&re);

            if (*out_episode > 0) {
                free(name);
                clean_series_name(out_series, out_season);
                return 1;
            }
        }
    }

    /* ---- Pattern 3d: " - NNN [" / " - NNN (" / trailing " - NNN" ----
     * e.g., "[Naruto-Kun.Hu] Naruto - 116 [1080p]"
     *       "[uP] One Piece - 0542B (FHD 1080p ...)"   (part letter eaten)
     *       "Show - 116"
     * Pattern 3 requires a SECOND dash after the number; release names end
     * the number with a bracketed tag or nothing instead. Spaces around the
     * dash are required so hyphenated titles ("Spider-Man") don't trigger.
     * Up to 4 digits for absolute-numbered anime; an optional single part
     * letter (A-D) is consumed with the number. */
    {
        regex_t re;
        regmatch_t matches[2];
        if (regcomp(&re, "[ \t]+-[ \t]+([0-9]{1,4})[A-Da-d]?[ \t]*([([]|$)",
                    REG_EXTENDED) == 0) {
            if (regexec(&re, name, 2, matches, 0) == 0) {
                *out_episode = atoi(name + matches[1].rm_so);
                *out_series  = strip_trim(name, (size_t)matches[0].rm_so);
                if (!*out_series)
                    *out_series = strdup("Unknown Series");
            }
            regfree(&re);

            if (*out_episode > 0) {
                free(name);
                clean_series_name(out_series, out_season);
                return 1;
            }
        }
    }

    /* ---- Pattern 3e: leading "NN-Title" ----
     * e.g., "11-Tsuzumi Mansion [C12B68BD]" inside a show/season folder.
     * The filename carries no series at all, so series is returned EMPTY
     * (not "Unknown Series") — callers with folder context (the macOS
     * scanner's deriveSeriesFromPath) fill it from the directory structure,
     * and an empty string keeps that path reachable. 1-3 digits so years
     * ("2001-A Space Odyssey") can't match; the dash must be immediately
     * followed by a letter (a title), not another number or separator. */
    {
        regex_t re;
        regmatch_t matches[2];
        if (regcomp(&re, "^([0-9]{1,3})-[A-Za-z]",
                    REG_EXTENDED) == 0) {
            if (regexec(&re, name, 2, matches, 0) == 0) {
                *out_episode = atoi(name + matches[1].rm_so);
                *out_series  = strdup("");
            }
            regfree(&re);

            if (*out_episode > 0) {
                free(name);
                clean_series_name(out_series, out_season);
                return 1;
            }
        }
    }

    /* Pattern 4 (trailing number) REMOVED — too aggressive.
     * It matched movie names with numbers (Spaceballs_1, Uncle Buck_100,
     * DVD rip title numbers) as TV episodes. If it's a real TV show,
     * patterns 1-3 (SxxExx, "Episode XX", " - XX - ") will catch it. */

    /* ---- Fallback: no TV pattern detected — it's a movie ---- */
    free(name);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  zune_snap_thumb                                           */
/*                                                                    */
/*  Grab a single frame at 10 seconds via ffmpeg, scale to 200px     */
/*  wide (preserving aspect ratio), save as JPEG.                     */
/* ------------------------------------------------------------------ */

int zune_snap_thumb(const char *video_path, const char *output_path)
{
    if (!video_path || !output_path) return -1;

    /* Build shell command with escaped paths.
     * -ss 10        seek 10s in (skip black intro frames)
     * -vf scale=200:-1   200px wide, auto height
     * -frames:v 1   one frame only
     * -q:v 2        JPEG quality */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -ss 10 -i '%s' -vf \"scale=200:-1\" "
        "-frames:v 1 -q:v 2 '%s' 2>/dev/null",
        video_path, output_path);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[libzune] thumbnail generation failed for: %s\n",
                video_path);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Metadata extraction: shared normalization, native/fallback parity  */
/* ------------------------------------------------------------------ */

enum ProbeTag {
    TAG_TITLE, TAG_ARTIST, TAG_ALBUM_ARTIST, TAG_ALBUMARTIST, TAG_ALBUM,
    TAG_GENRE, TAG_TRACK, TAG_TRACKNUMBER, TAG_DISC, TAG_DISCNUMBER,
    TAG_DATE, TAG_YEAR, TAG_COUNT
};
static const char *const probe_tag_names[TAG_COUNT] = {
    "title", "artist", "album_artist", "albumartist", "album", "genre",
    "track", "tracknumber", "disc", "discnumber", "date", "year"
};
typedef struct { char *value[TAG_COUNT]; } ProbeTags;

/* UTF-8 whitespace only: preserve punctuation, accents, internal spacing and
 * all non-whitespace bytes. This matches the app's trimmed/blank semantics. */
static size_t probe_space(const unsigned char *text, size_t remaining)
{
    if (!remaining) return 0;
    if (text[0] == ' ' || (text[0] >= '\t' && text[0] <= '\r')) return 1;
    if (remaining >= 2 && text[0] == 0xc2 && (text[1] == 0x85 || text[1] == 0xa0)) return 2;
    if (remaining >= 3) {
        if (text[0] == 0xe1 && text[1] == 0x9a && text[2] == 0x80) return 3;
        if (text[0] == 0xe2 && text[1] == 0x80
            && ((text[2] >= 0x80 && text[2] <= 0x8a) || text[2] == 0xa8
                || text[2] == 0xa9 || text[2] == 0xaf)) return 3;
        if (text[0] == 0xe2 && text[1] == 0x81 && text[2] == 0x9f) return 3;
        if (text[0] == 0xe3 && text[1] == 0x80 && text[2] == 0x80) return 3;
    }
    return 0;
}

static int probe_trim(const char *text, char **out)
{
    *out = NULL;
    if (!text) return 0;
    size_t length = strlen(text), start = 0, end = 0;
    while (start < length) {
        size_t space = probe_space((const unsigned char *)text + start, length - start);
        if (!space) break;
        start += space;
    }
    for (size_t i = start; i < length;) {
        size_t space = probe_space((const unsigned char *)text + i, length - i);
        if (space) i += space;
        else end = ++i;
    }
    if (end <= start) return 0;
    *out = malloc(end - start + 1);
    if (!*out) return -1;
    memcpy(*out, text + start, end - start);
    (*out)[end - start] = '\0';
    return 0;
}

static void probe_tags_free(ProbeTags *tags)
{
    for (int i = 0; i < TAG_COUNT; ++i) free(tags->value[i]);
    memset(tags, 0, sizeof(*tags));
}

static int probe_number(const char *text, unsigned long maximum)
{
    if (!text || !isdigit((unsigned char)*text)) return 0;
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || !value || value > maximum) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (*end == '/') {
        const char *total_text = end + 1;
        while (isspace((unsigned char)*total_text)) ++total_text;
        if (!isdigit((unsigned char)*total_text)) return 0;
        errno = 0;
        unsigned long total = strtoul(total_text, &end, 10);
        if (errno || total < value || total > INT_MAX) return 0;
        while (isspace((unsigned char)*end)) ++end;
    }
    return *end ? 0 : (int)value;
}

static int probe_two_digits(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    if (!p[0] || !p[1] || !isdigit(p[0]) || !isdigit(p[1])) return -1;
    *cursor += 2;
    return (p[0] - '0') * 10 + p[1] - '0';
}

/* YYYY, YYYY-MM, YYYY-MM-DD (also slash dates), optionally an ISO timestamp.
 * Reject numeric prefixes of garbage and impossible calendar components. */
static int probe_year(const char *text)
{
    if (!text || strlen(text) < 4) return 0;
    int year = 0;
    for (int i = 0; i < 4; ++i) {
        if (!isdigit((unsigned char)text[i])) return 0;
        year = year * 10 + text[i] - '0';
    }
    if (!year) return 0;
    const char *p = text + 4;
    if (!*p) return year;
    char separator = *p++;
    if (separator != '-' && separator != '/') return 0;
    int month = probe_two_digits(&p);
    if (month < 1 || month > 12) return 0;
    if (!*p) return year;
    if (*p++ != separator) return 0;
    int day = probe_two_digits(&p);
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int maximum = days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day < 1 || day > maximum) return 0;
    if (!*p) return year;
    if (*p != 'T' && *p != 't' && *p != ' ') return 0;
    ++p;
    int hour = probe_two_digits(&p);
    if (hour < 0 || hour > 23 || *p++ != ':') return 0;
    int minute = probe_two_digits(&p);
    if (minute < 0 || minute > 59) return 0;
    if (*p == ':') {
        ++p;
        int second = probe_two_digits(&p);
        if (second < 0 || second > 60) return 0;
        if (*p == '.') {
            ++p;
            if (!isdigit((unsigned char)*p)) return 0;
            while (isdigit((unsigned char)*p)) ++p;
        }
    }
    if (*p == 'Z' || *p == 'z') ++p;
    else if (*p == '+' || *p == '-') {
        ++p;
        int offset_hour = probe_two_digits(&p);
        if (offset_hour < 0 || offset_hour > 23) return 0;
        if (*p == ':') ++p;
        int offset_minute = probe_two_digits(&p);
        if (offset_minute < 0 || offset_minute > 59) return 0;
    }
    return *p ? 0 : year;
}

static const char *probe_text(const ProbeTags *container, const ProbeTags *stream, int tag, int alias)
{
    const ProbeTags *sources[] = { container, stream };
    for (int i = 0; i < 2; ++i) {
        if (sources[i]->value[tag]) return sources[i]->value[tag];
        if (alias >= 0 && sources[i]->value[alias]) return sources[i]->value[alias];
    }
    return NULL;
}

static int probe_numeric(const ProbeTags *container, const ProbeTags *stream, int tag, int alias, unsigned long maximum)
{
    const ProbeTags *sources[] = { container, stream };
    for (int i = 0; i < 2; ++i) {
        int value = probe_number(sources[i]->value[tag], maximum);
        if (!value) value = probe_number(sources[i]->value[alias], maximum);
        if (value) return value;
    }
    return 0;
}

static int probe_fill(ZuneMetadata *out, const ProbeTags *container, const ProbeTags *stream)
{
    char **targets[] = { &out->title, &out->artist, &out->albumartist, &out->album, &out->genre };
    const int tags[] = { TAG_TITLE, TAG_ARTIST, TAG_ALBUM_ARTIST, TAG_ALBUM, TAG_GENRE };
    for (int i = 0; i < 5; ++i) {
        const char *value = probe_text(container, stream, tags[i], i == 2 ? TAG_ALBUMARTIST : -1);
        if (value && !(*targets[i] = strdup(value))) return -1;
    }
    out->tracknumber = (uint16_t)probe_numeric(container, stream, TAG_TRACK, TAG_TRACKNUMBER, UINT16_MAX);
    out->discnumber = probe_numeric(container, stream, TAG_DISC, TAG_DISCNUMBER, INT_MAX);
    const ProbeTags *sources[] = { container, stream };
    for (int i = 0; i < 2 && !out->year; ++i) {
        out->year = probe_year(sources[i]->value[TAG_DATE]);
        if (!out->year) out->year = probe_year(sources[i]->value[TAG_YEAR]);
    }
    return 0;
}

static uint32_t probe_duration(double seconds)
{
    if (!isfinite(seconds) || seconds <= 0) return 0;
    return seconds >= UINT32_MAX / 1000.0 ? UINT32_MAX : (uint32_t)(seconds * 1000.0);
}

#ifdef USE_LIBAV
#include <libavformat/avformat.h>
#include <libavcodec/version.h>
#include <libavutil/dict.h>

static int probe_dictionary(AVDictionary *dictionary, ProbeTags *tags)
{
    for (int i = 0; i < TAG_COUNT; ++i) {
        AVDictionaryEntry *entry = NULL;
        while ((entry = av_dict_get(dictionary, probe_tag_names[i], entry, 0))) {
            if (probe_trim(entry->value, &tags->value[i]) < 0) return -1;
            if (tags->value[i]) break;
        }
    }
    return 0;
}

static AVStream *probe_selected_stream(AVFormatContext *format)
{
    const enum AVMediaType types[] = { AVMEDIA_TYPE_AUDIO, AVMEDIA_TYPE_VIDEO };
    for (int type = 0; type < 2; ++type) {
        AVStream *first = NULL;
        for (unsigned int i = 0; i < format->nb_streams; ++i) {
            AVStream *stream = format->streams[i];
            if (stream->codecpar->codec_type != types[type]) continue;
            if (stream->codecpar->codec_id == AV_CODEC_ID_NONE) continue;
            if (types[type] == AVMEDIA_TYPE_AUDIO) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 24, 100)
                int channels = stream->codecpar->ch_layout.nb_channels;
#else
                int channels = stream->codecpar->channels;
#endif
                if (stream->codecpar->sample_rate <= 0 || channels <= 0) continue;
            } else if (stream->codecpar->width <= 0 || stream->codecpar->height <= 0) continue;
            if (!first) first = stream;
            if (stream->disposition & AV_DISPOSITION_DEFAULT) return stream;
        }
        if (first) return first;
    }
    return NULL;
}

int zune_probe(const char *filepath, ZuneMetadata *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!filepath || !*filepath) return -1;
    AVFormatContext *fmt = NULL;
    ProbeTags container = {0}, selected = {0};
    int result = -1;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) goto done;
    if (avformat_find_stream_info(fmt, NULL) < 0) goto done;
    AVStream *stream = probe_selected_stream(fmt);
    if (!stream || probe_dictionary(fmt->metadata, &container) < 0
        || probe_dictionary(stream->metadata, &selected) < 0) goto done;
    if (probe_fill(out, &container, &selected) < 0) goto done;
    if (fmt->duration > 0) {
        int64_t milliseconds = fmt->duration / (AV_TIME_BASE / 1000);
        out->duration_ms = milliseconds > UINT32_MAX ? UINT32_MAX : (uint32_t)milliseconds;
    }
    if (!out->duration_ms && stream->duration > 0)
        out->duration_ms = probe_duration(stream->duration * av_q2d(stream->time_base));
    result = 0;
done:
    probe_tags_free(&container);
    probe_tags_free(&selected);
    avformat_close_input(&fmt);
    if (result < 0) zune_free_metadata(out);
    return result;
}

#else

typedef struct {
    ProbeTags tags;
    int type; /* 1 audio, 2 video */
    int is_default;
    int has_codec;
    int sample_rate, channels, width, height;
    uint32_t duration;
} ProbeStream;

static char *probe_flat_value(const char *text)
{
    size_t length = strlen(text);
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r')) --length;
    if (length && *text == '"') {
        if (length < 2 || text[length - 1] != '"') return NULL;
        ++text;
        length -= 2;
    }
    char *value = malloc(length + 1);
    if (!value) return NULL;
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (c == '\\' && i + 1 < length) {
            c = text[++i];
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case '\\': case '"': case '\'': case '`': case '$': break;
                default: value[used++] = '\\'; break;
            }
        }
        value[used++] = c;
    }
    value[used] = '\0';
    return value;
}

static int probe_flat_tag(ProbeTags *tags, const char *key, const char *value)
{
    for (int i = 0; i < TAG_COUNT; ++i) {
        if (strcasecmp(key, probe_tag_names[i]) || tags->value[i]) continue;
        return probe_trim(value, &tags->value[i]);
    }
    return 0;
}

static uint32_t probe_flat_duration(const char *value)
{
    char *end;
    errno = 0;
    double seconds = strtod(value, &end);
    return errno || end == value || *end ? 0 : probe_duration(seconds);
}

int zune_probe(const char *filepath, ZuneMetadata *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!filepath || !*filepath) return -1;
    const char *ffprobe = NULL;
    if (access("/opt/homebrew/bin/ffprobe", X_OK) == 0)
        ffprobe = "/opt/homebrew/bin/ffprobe";
    else if (access("/usr/local/bin/ffprobe", X_OK) == 0)
        ffprobe = "/usr/local/bin/ffprobe";
    else if (access("/usr/bin/ffprobe", X_OK) == 0)
        ffprobe = "/usr/bin/ffprobe";
    else return -1;

    /* No shell expansion, filename truncation or fixed-size tag lines. */
    const char *entries = "stream=codec_name,codec_type,duration,sample_rate,channels,width,height:stream_disposition=default:"
        "stream_tags=title,artist,album_artist,albumartist,album,genre,track,tracknumber,disc,discnumber,date,year:"
        "format=duration:format_tags=title,artist,album_artist,albumartist,album,genre,track,tracknumber,disc,discnumber,date,year";
    int descriptors[2];
    if (pipe(descriptors) < 0) return -1;
    pid_t child = fork();
    if (child < 0) { close(descriptors[0]); close(descriptors[1]); return -1; }
    if (!child) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(127);
        close(descriptors[1]);
        int quiet = open("/dev/null", O_RDWR);
        if (quiet >= 0) {
            dup2(quiet, STDERR_FILENO);
            dup2(quiet, STDIN_FILENO);
            if (quiet > STDERR_FILENO) close(quiet);
        }
        execl(ffprobe, ffprobe, "-v", "error", "-show_entries", entries, "-of", "flat", "-i", filepath, (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    FILE *output = fdopen(descriptors[0], "r");
    ProbeTags container = {0};
    ProbeStream *streams = NULL;
    size_t count = 0, line_capacity = 0;
    char *line = NULL;
    uint32_t duration = 0;
    int result = -1, parse_error = !output;
    if (!output) close(descriptors[0]);
    while (output && getline(&line, &line_capacity, output) >= 0) {
        char *equals = strchr(line, '=');
        if (!equals) { parse_error = 1; continue; }
        *equals = '\0';
        char *value = probe_flat_value(equals + 1);
        if (!value) { parse_error = 1; continue; }
        if (!strncmp(line, "format.tags.", 12)) {
            if (probe_flat_tag(&container, line + 12, value) < 0) parse_error = 1;
        } else if (!strcmp(line, "format.duration")) duration = probe_flat_duration(value);
        else {
            unsigned int index;
            int offset = 0;
            if (sscanf(line, "streams.stream.%u.%n", &index, &offset) == 1 && offset > 0) {
                if (index >= 1024) { free(value); parse_error = 1; continue; }
                if (index >= count) {
                    ProbeStream *larger = realloc(streams, (index + 1) * sizeof(*streams));
                    if (!larger) { free(value); parse_error = 1; continue; }
                    streams = larger;
                    memset(streams + count, 0, (index + 1 - count) * sizeof(*streams));
                    count = index + 1;
                }
                ProbeStream *stream = &streams[index];
                const char *key = line + offset;
                if (!strcmp(key, "codec_type")) stream->type = !strcmp(value, "audio") ? 1 : !strcmp(value, "video") ? 2 : 0;
                else if (!strcmp(key, "codec_name")) stream->has_codec = *value && strcmp(value, "unknown");
                else if (!strcmp(key, "sample_rate")) stream->sample_rate = probe_number(value, INT_MAX);
                else if (!strcmp(key, "channels")) stream->channels = probe_number(value, INT_MAX);
                else if (!strcmp(key, "width")) stream->width = probe_number(value, INT_MAX);
                else if (!strcmp(key, "height")) stream->height = probe_number(value, INT_MAX);
                else if (!strcmp(key, "disposition.default")) stream->is_default = !strcmp(value, "1");
                else if (!strcmp(key, "duration")) stream->duration = probe_flat_duration(value);
                else if (!strncmp(key, "tags.", 5) && probe_flat_tag(&stream->tags, key + 5, value) < 0) parse_error = 1;
            }
        }
        free(value);
    }
    if (output) { if (ferror(output)) parse_error = 1; fclose(output); }
    free(line);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (!parse_error && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        ProbeStream *selected = NULL;
        for (int type = 1; type <= 2 && !selected; ++type) {
            for (size_t i = 0; i < count; ++i) {
                if (streams[i].type != type) continue;
                if (!streams[i].has_codec || (type == 1 && (!streams[i].sample_rate || !streams[i].channels))
                    || (type == 2 && (!streams[i].width || !streams[i].height))) continue;
                if (!selected) selected = &streams[i];
                if (streams[i].is_default) { selected = &streams[i]; break; }
            }
        }
        if (selected && probe_fill(out, &container, &selected->tags) == 0) {
            out->duration_ms = duration ? duration : selected->duration;
            result = 0;
        }
    }
    probe_tags_free(&container);
    for (size_t i = 0; i < count; ++i) probe_tags_free(&streams[i].tags);
    free(streams);
    if (result < 0) zune_free_metadata(out);
    return result;
}
#endif

void zune_free_metadata(ZuneMetadata *meta) {
    if (!meta) return;
    free(meta->title); free(meta->artist); free(meta->albumartist);
    free(meta->album); free(meta->genre);
    memset(meta, 0, sizeof(ZuneMetadata));
}
