/* External tool invocation: real util/photo/thumbnail/transcode code, fake
 * tools. run-tool-exec.sh puts recording fakes first on PATH. Each fake writes
 * its argv (NUL-separated) to $ZUNE_TOOL_LOG, its own path to
 * $ZUNE_TOOL_LOG.who, and creates its last argument unless FAKE_TOOL_FAIL is
 * set (fake ffprobe prints flat-format metadata instead). No USB backend is
 * linked; the two MTP calls the photo-thumbnail fallback makes are fixtures. */
#include "../src/zune_internal.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>

static int checks;
static char last_error[1024];
static char workdir[256];
static char fixture[200];

void zune_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, ap);
    va_end(ap);
}
void zune_clear_error(void) { last_error[0] = 0; }
const char *zune_get_error(void) { return last_error[0] ? last_error : NULL; }

/* Photo thumbnail fixtures: no representative sample, full download works. */
int mtp_get_representative_sample(ptp_session_t *s, uint32_t handle,
                                  uint8_t **data, uint32_t *len) {
    (void)s; (void)handle;
    if (data) *data = NULL;
    if (len) *len = 0;
    return -1;
}
static char downloaded[4096];
int mtp_get_object_to_file(ptp_session_t *s, uint32_t handle, const char *path) {
    (void)s; (void)handle;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    fputs("full photo bytes", fp);
    fclose(fp);
    snprintf(downloaded, sizeof(downloaded), "%s", path);
    return 0;
}

static void check(int ok, const char *name) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s (error: %s)\n", name, last_error);
        exit(1);
    }
    printf("PASS: %s\n", name);
    checks++;
}

/* Read the fake tool's argv log. Returns argument count, -1 if absent. */
static int read_log(char *buf, size_t size, const char **args, int max_args) {
    const char *log = getenv("ZUNE_TOOL_LOG");
    FILE *fp = log ? fopen(log, "rb") : NULL;
    if (!fp) return -1;
    size_t n = fread(buf, 1, size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    int count = 0;
    for (size_t i = 0; i < n && count < max_args;) {
        args[count++] = buf + i;
        i += strlen(buf + i) + 1;
    }
    return count;
}

static void clear_log(void) {
    const char *log = getenv("ZUNE_TOOL_LOG");
    char who[512];
    if (!log) return;
    unlink(log);
    snprintf(who, sizeof(who), "%s.who", log);
    unlink(who);
}

/* 1 if the last fake run was the executable at `expected`. */
static int who_is(const char *expected) {
    char path[512], buf[1024];
    snprintf(path, sizeof(path), "%s.who", getenv("ZUNE_TOOL_LOG"));
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (expected) fprintf(stderr, "  no tool ran, expected [%s]\n", expected);
        return expected == NULL;
    }
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    if (!expected || strcmp(buf, expected) != 0) {
        fprintf(stderr, "  ran [%s], expected [%s]\n", buf, expected ? expected : "(none)");
        return 0;
    }
    return 1;
}

/* 1 if the recorded arguments (argv[1..]) equal expected exactly. */
static int argv_is(const char *const *expected) {
    static char buf[65536];
    const char *args[64];
    int count = read_log(buf, sizeof(buf), args, 64);
    int want = 0;
    while (expected[want]) want++;
    if (count != want) {
        fprintf(stderr, "  argc %d, expected %d\n", count, want);
        return 0;
    }
    for (int i = 0; i < want; i++) {
        if (strcmp(args[i], expected[i]) != 0) {
            fprintf(stderr, "  arg %d: got [%s]\n  expected [%s]\n", i, args[i], expected[i]);
            return 0;
        }
    }
    return 1;
}

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

/* Injection attempts write marker files into the working directory. */
static int injected(void) {
    const char *markers[] = { "INJECTED_DOLLAR", "INJECTED_TICK", "INJECTED_SEMI",
                              "INJECTED_QUOTE", NULL };
    char path[512];
    for (int i = 0; markers[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", workdir, markers[i]);
        if (exists(path) || exists(markers[i])) return 1;
    }
    return 0;
}

static void write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    assert(fp);
    fputs(text, fp);
    fclose(fp);
}

/* A path of at least `length` bytes made of nested directories. */
static void long_path(char *out, size_t size, size_t length, const char *leaf) {
    size_t used = (size_t)snprintf(out, size, "%s", workdir);
    while (used + 1 + strlen(leaf) < length) {
        out[used++] = '/';
        memset(out + used, 'd', 200);
        used += 200;
        out[used] = '\0';
        int made = mkdir(out, 0700);
        assert(made == 0 || errno == EEXIST);
    }
    snprintf(out + used, size - used, "/%s", leaf);
}

static void test_snap_thumb(void) {
    const char *names[] = {
        "it's \"quoted\" $(touch INJECTED_DOLLAR) `touch INJECTED_TICK`; touch INJECTED_SEMI; v.mp4",
        "Don't Stop.mp4",
        "x' ; touch INJECTED_QUOTE ; echo '.mp4",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        char video[1024], output[1024];
        snprintf(video, sizeof(video), "%s/%s", workdir, names[i]);
        snprintf(output, sizeof(output), "%s/thumb '%d' $(x) `y`; z.jpg", workdir, i);
        write_file(video, "video bytes");
        clear_log();
        int rc = zune_snap_thumb(video, output);
        const char *expected[] = { "-y", "-ss", "10", "-i", video, "-vf", "scale=200:-1",
                                   "-frames:v", "1", "-q:v", "2", output, NULL };
        check(rc == 0 && argv_is(expected) && exists(output) && !injected(),
              names[i]);
        unlink(video);
        unlink(output);
    }

    char video[4096], output[4096];
    long_path(video, sizeof(video), 3000, "long name video.mp4");
    long_path(output, sizeof(output), 3000, "long name thumb.jpg");
    check(strlen(video) >= 3000 && strlen(output) >= 3000, "fixture paths exceed 3000 bytes");
    write_file(video, "video bytes");
    clear_log();
    int rc = zune_snap_thumb(video, output);
    const char *expected[] = { "-y", "-ss", "10", "-i", video, "-vf", "scale=200:-1",
                               "-frames:v", "1", "-q:v", "2", output, NULL };
    check(rc == 0 && argv_is(expected) && exists(output),
          "3000-byte input and output paths arrive intact");
    unlink(video);
    unlink(output);

    /* No frame at 10 s (a shorter clip): retry from the start, and on a
     * second failure leave no file that could pass for a thumbnail. */
    snprintf(video, sizeof(video), "%s/fails.mp4", workdir);
    snprintf(output, sizeof(output), "%s/fails.jpg", workdir);
    write_file(video, "video bytes");
    /* The fake logs only its latest run: the retry from the start. */
    const char *retry[] = { "-y", "-ss", "0", "-i", video, "-vf", "scale=200:-1",
                            "-frames:v", "1", "-q:v", "2", output, NULL };
    setenv("FAKE_TOOL_FAIL", "1", 1);
    write_file(output, "stale thumbnail");
    clear_log();
    rc = zune_snap_thumb(video, output);
    check(rc == -1 && argv_is(retry) && !exists(output),
          "nonzero tool exit retries from the start, then fails without leaving the output");
    unsetenv("FAKE_TOOL_FAIL");

    /* Older ffmpeg exits 0 with nothing written past the end of a clip. */
    setenv("FAKE_TOOL_EMPTY", "1", 1);
    write_file(output, "");
    clear_log();
    rc = zune_snap_thumb(video, output);
    check(rc == -1 && argv_is(retry) && !exists(output),
          "an exit-0 empty thumbnail is a failure after the retry, and is removed");
    /* ...and an older thumbnail already at the output path is not this one. */
    write_file(output, "stale thumbnail from another video");
    clear_log();
    rc = zune_snap_thumb(video, output);
    check(rc == -1 && argv_is(retry) && !exists(output),
          "an exit-0 run that writes nothing never passes off an older file as its thumbnail");
    unsetenv("FAKE_TOOL_EMPTY");
    unlink(video);

    /* Reject aliases before removing output or running ffmpeg. An output
     * derived incorrectly from the source name must never erase the source,
     * even when the converter is unavailable. stat follows symlinks and
     * catches hard links and alternate spellings of the same path too. */
    snprintf(video, sizeof(video), "%s/source.mp4", workdir);
    const char *aliases[] = { "source.mp4", "./source.mp4", "source-link.mp4",
                              "source-hardlink.mp4" };
    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        write_file(video, "original source bytes");
        snprintf(output, sizeof(output), "%s/%s", workdir, aliases[i]);
        if (i == 2) assert(symlink(video, output) == 0);
        if (i == 3) assert(link(video, output) == 0);
        clear_log();
        if (i == 0) setenv("ZUNE_FFMPEG", "/nonexistent/review-ffmpeg", 1);
        rc = zune_snap_thumb(video, output);
        char original[64] = {0};
        FILE *fp = fopen(video, "rb");
        if (fp) { fread(original, 1, sizeof(original) - 1, fp); fclose(fp); }
        check(rc == -1 && strcmp(original, "original source bytes") == 0
                  && exists(output) && who_is(NULL)
                  && zune_get_error() && strstr(zune_get_error(), "same file"),
              aliases[i]);
        if (i == 0) unsetenv("ZUNE_FFMPEG");
        if (i >= 2) unlink(output);
        unlink(video);
    }
}

#ifndef __APPLE__
static void test_arm_photo(void) {
    char input[1024];
    snprintf(input, sizeof(input), "%s/photo 'one' \"two\" $(touch INJECTED_DOLLAR).png", workdir);
    write_file(input, "png bytes");
    clear_log();
    char *prepared = zune_arm_photo(input);
    const char *expected[] = { "-y", "-i", input, "-vf",
        "scale='min(480,iw)':'min(480,ih)':force_original_aspect_ratio=decrease",
        "-frames:v", "1", "-q:v", "5", prepared ? prepared : "(null)", NULL };
    check(prepared && argv_is(expected) && !injected(),
          "photo preparation keeps the scale filter as one argument");
    if (prepared) { unlink(prepared); free(prepared); }
    unlink(input);
}

static void test_photo_thumb(void) {
    char cache[1024];
    snprintf(cache, sizeof(cache), "%s/cache 'thumb' $(touch INJECTED_DOLLAR).jpg", workdir);
    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));
    clear_log();
    downloaded[0] = '\0';
    int rc = zune_grab_photo_thumb(&dev, 7, cache);
    const char *expected[] = { "-y", "-i", downloaded, "-vf",
        "scale=200:200:force_original_aspect_ratio=decrease", cache, NULL };
    check(rc == 0 && downloaded[0] && argv_is(expected) && exists(cache) && !injected(),
          "photo thumbnail fallback passes download and cache paths intact");
    check(!exists(downloaded), "downloaded full photo is removed after resizing");
    unlink(cache);
}
#endif

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

static void release(char *prepared) {
    if (!prepared) return;
    unlink(prepared);
    free(prepared);
}

/* Regular files left in $TMPDIR. */
static int temp_files(void) {
    DIR *dir = opendir(getenv("TMPDIR"));
    assert(dir);
    int count = 0;
    for (struct dirent *entry; (entry = readdir(dir));)
        if (entry->d_name[0] != '.') count++;
    closedir(dir);
    return count;
}

/* Structural UTF-8 check: lead bytes followed by the right continuations. */
static int valid_utf8(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        int tail = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2
                 : (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (tail < 0) return 0;
        for (p++; tail--; p++)
            if ((*p & 0xC0) != 0x80) return 0;
    }
    return 1;
}

static int private_temp(const char *path) {
    struct stat st;
    const char *dir = getenv("TMPDIR");
    return path && strncmp(path, dir, strlen(dir)) == 0 && path[strlen(dir)] == '/'
        && lstat(path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600;
}

static void test_temp_files(void) {
    char input[512];
    snprintf(input, sizeof(input), "%s/Same Second.flac", workdir);
    write_file(input, "flac bytes");
    check(temp_files() == 0, "temporary directory starts empty");

    char *first = zune_arm_audio(input);
    char *second = zune_arm_audio(input);
    check(first && second && strcmp(first, second) != 0,
          "two preparations of one input in the same second get different names");
    check(private_temp(first) && private_temp(second),
          "outputs are regular 0600 files inside $TMPDIR");
    check(strstr(first, "zunelinux_Same Second_") != NULL, "names keep a readable input label");
    release(first);
    release(second);

    /* One ASCII byte, then 100 two-byte characters: a 96-byte cut would split
     * a character, so the label must back up one byte. */
    char long_name[400] = "x", long_input[1024];
    for (int i = 0; i < 100; i++) strcat(long_name, "\xc3\xa9");
    strcat(long_name, ".flac");
    snprintf(long_input, sizeof(long_input), "%s/%s", workdir, long_name);
    write_file(long_input, "flac bytes");
    char *labelled = zune_arm_audio(long_input);
    const char *name = labelled ? strrchr(labelled, '/') + 1 : "";
    check(labelled && strlen(name) < 255 && valid_utf8(name) && strstr(name, "x\xc3\xa9"),
          "a 201-byte UTF-8 stem is cut on a character boundary within NAME_MAX");
    release(labelled);
    unlink(long_input);

    setenv("FAKE_TOOL_FAIL", "1", 1);
    char *failed[4] = { zune_arm_audio(input), zune_retag(input),
                        zune_arm_video(input, ZUNE_MODEL_HD), zune_arm_photo(input) };
    unsetenv("FAKE_TOOL_FAIL");
    check(!failed[0] && !failed[1] && !failed[2] && !failed[3] && temp_files() == 0,
          "failed audio, retag, video and photo preparation leave no file behind");

    setenv("FAKE_TOOL_EMPTY", "1", 1);
    char *empty[2] = { zune_arm_audio(input), zune_arm_photo(input) };
    unsetenv("FAKE_TOOL_EMPTY");
    check(!empty[0] && !empty[1] && temp_files() == 0,
          "a tool that exits 0 without output is a failure, and its file is removed");

#ifndef __APPLE__
    char cache[512];
    snprintf(cache, sizeof(cache), "%s/failed-cache.jpg", workdir);
    ZuneDevice dev;
    memset(&dev, 0, sizeof(dev));
    downloaded[0] = '\0';
    setenv("FAKE_TOOL_FAIL", "1", 1);
    int rc = zune_grab_photo_thumb(&dev, 9, cache);
    unsetenv("FAKE_TOOL_FAIL");
    check(rc == -1 && private_temp(downloaded) == 0 && !exists(downloaded) && !exists(cache)
          && temp_files() == 0,
          "failed photo resize removes the download and leaves no cache file");
    check(strncmp(downloaded, getenv("TMPDIR"), strlen(getenv("TMPDIR"))) == 0,
          "the photo download used a private temporary file");
#endif
    unlink(input);
}

static void test_lookup(void) {
    char input[512], bin_ffmpeg[512], bin_ffprobe[512], custom[512];
    char probe_custom[512], noexec[512], empty[512], alt_path[8192];
    char *saved_path = strdup(getenv("PATH"));
    assert(saved_path);
    snprintf(bin_ffmpeg, sizeof(bin_ffmpeg), "%s/bin/ffmpeg", fixture);
    snprintf(bin_ffprobe, sizeof(bin_ffprobe), "%s/bin/ffprobe", fixture);
    snprintf(custom, sizeof(custom), "%s/alt/ffmpeg-custom", fixture);
    snprintf(probe_custom, sizeof(probe_custom), "%s/alt/ffprobe-custom", fixture);
    snprintf(noexec, sizeof(noexec), "%s/alt/not-executable", fixture);
    snprintf(empty, sizeof(empty), "%s/empty", fixture);
    snprintf(input, sizeof(input), "%s/Song 'quoted' $(touch INJECTED_DOLLAR).flac", workdir);
    write_file(input, "flac bytes");

    clear_log();
    char *out = zune_arm_audio(input);
    const char *audio[] = { "-y", "-i", input, "-codec:a", "libmp3lame", "-b:a", "320k",
        "-map_metadata", "-1", "-id3v2_version", "3", out ? out : "(null)", NULL };
    check(out && argv_is(audio) && who_is(bin_ffmpeg) && !injected(),
          "audio transcode finds ffmpeg on PATH, not a Homebrew path");
    release(out);

    clear_log();
    out = zune_retag(input);
    const char *retag[] = { "-y", "-i", input, "-c", "copy", "-map_metadata", "0",
        "-id3v2_version", "3", "-write_id3v1", "1", out ? out : "(null)", NULL };
    check(out && argv_is(retag), "retag passes exact stream-copy arguments");
    release(out);

    char series[1100], show[1200];
    memset(series, 'S', 1000);
    snprintf(series + 1000, sizeof(series) - 1000, " 'quoted' $(touch INJECTED_DOLLAR)");
    snprintf(show, sizeof(show), "show=%s", series);
    clear_log();
    out = zune_arm_episode(input, ZUNE_MODEL_HD, series, 2, 5);
    const char *episode[] = { "-y", "-i", input, "-c:v", "libx264", "-profile:v", "baseline",
        "-level", "3.1", "-b:v", "2500k", "-vf",
        "scale=480:272:force_original_aspect_ratio=decrease,pad=480:272:(ow-iw)/2:(oh-ih)/2",
        "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart",
        "-metadata", show, "-metadata", "season_number=2", "-metadata", "episode_sort=5",
        out ? out : "(null)", NULL };
    check(out && argv_is(episode) && ends_with(out, ".mp4") && !injected(),
          "HD episode keeps a 1000-byte series name as one metadata argument");
    release(out);

    clear_log();
    out = zune_arm_video(input, ZUNE_MODEL_30);
    const char *wmv[] = { "-y", "-i", input, "-c:v", "wmv2", "-b:v", "800k", "-vf",
        "scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2",
        "-pix_fmt", "yuv420p",
        "-c:a", "wmav2", "-b:a", "128k", "-ar", "44100", "-ac", "2", out ? out : "(null)", NULL };
    check(out && argv_is(wmv) && ends_with(out, ".wmv"), "Zune 30 profile writes WMV without metadata");
    release(out);

    setenv("ZUNE_FFMPEG", custom, 1);
    clear_log();
    out = zune_arm_audio(input);
    check(out && who_is(custom), "ZUNE_FFMPEG path overrides PATH");
    release(out);

    snprintf(alt_path, sizeof(alt_path), "%s/alt:%s", fixture, saved_path);
    setenv("PATH", alt_path, 1);
    setenv("ZUNE_FFMPEG", "ffmpeg-custom", 1);
    clear_log();
    out = zune_arm_audio(input);
    check(out && who_is(custom), "ZUNE_FFMPEG name is looked up on PATH");
    release(out);
    setenv("PATH", saved_path, 1);

    setenv("ZUNE_FFMPEG", noexec, 1);
    last_error[0] = '\0';
    clear_log();
    out = zune_arm_audio(input);
    check(!out && who_is(NULL) && strstr(last_error, "ZUNE_FFMPEG")
          && strstr(last_error, "not an executable"),
          "non-executable ZUNE_FFMPEG is refused with a clear error");
    release(out);
    unsetenv("ZUNE_FFMPEG");

    setenv("PATH", empty, 1);
    last_error[0] = '\0';
    clear_log();
    out = zune_arm_audio(input);
    check(!out && who_is(NULL) && strstr(last_error, "ffmpeg not found")
          && strstr(last_error, "ZUNE_FFMPEG"),
          "missing ffmpeg names the tool and its override variable");
    release(out);

    /* A copy of the fake in the working directory must not be found. */
    char local[512], tool_text[4096];
    snprintf(local, sizeof(local), "%s/ffmpeg", workdir);
    FILE *src = fopen(bin_ffmpeg, "rb");
    assert(src);
    size_t n = fread(tool_text, 1, sizeof(tool_text) - 1, src);
    fclose(src);
    tool_text[n] = '\0';
    write_file(local, tool_text);
    assert(chmod(local, 0755) == 0);
    snprintf(alt_path, sizeof(alt_path), ":%s:", empty);
    setenv("PATH", alt_path, 1);
    clear_log();
    out = zune_arm_audio(input);
    check(!out && who_is(NULL), "empty PATH entries never run ./ffmpeg");
    release(out);
    snprintf(alt_path, sizeof(alt_path), ".:%s", empty);
    setenv("PATH", alt_path, 1);
    clear_log();
    out = zune_arm_audio(input);
    check(!out && who_is(NULL), "a \".\" PATH entry never runs ./ffmpeg");
    release(out);
    unlink(local);
    /* A relative directory is resolved against the working directory too. */
    char rel_dir[512], rel_tool[600];
    snprintf(rel_dir, sizeof(rel_dir), "%s/relbin", workdir);
    snprintf(rel_tool, sizeof(rel_tool), "%s/ffmpeg", rel_dir);
    assert(mkdir(rel_dir, 0755) == 0);
    write_file(rel_tool, tool_text);
    assert(chmod(rel_tool, 0755) == 0);
    snprintf(alt_path, sizeof(alt_path), "relbin:%s", empty);
    setenv("PATH", alt_path, 1);
    clear_log();
    out = zune_arm_audio(input);
    check(!out && who_is(NULL), "a relative PATH entry never runs a tool below the working directory");
    release(out);
    unlink(rel_tool);
    rmdir(rel_dir);
    setenv("PATH", saved_path, 1);

    ZuneMetadata meta;
    clear_log();
    int rc = zune_probe(input, &meta);
    check(rc == 0 && meta.title && strcmp(meta.title, "Fake Probe Title") == 0
          && meta.duration_ms == 1500 && who_is(bin_ffprobe),
          "fallback probe finds ffprobe on PATH and parses its output");
    zune_free_metadata(&meta);
    setenv("ZUNE_FFPROBE", probe_custom, 1);
    clear_log();
    rc = zune_probe(input, &meta);
    check(rc == 0 && who_is(probe_custom), "ZUNE_FFPROBE overrides PATH");
    zune_free_metadata(&meta);
    unsetenv("ZUNE_FFPROBE");

    setenv("PATH", empty, 1);
    last_error[0] = '\0';
    rc = zune_probe(input, &meta);
    check(rc == -1 && strstr(last_error, "ZUNE_FFPROBE"), "missing ffprobe names ZUNE_FFPROBE");
    setenv("PATH", saved_path, 1);

    unlink(input);
    free(saved_path);
}

int main(int argc, char **argv) {
    /* The wrapper owns and removes this directory. */
    assert(argc == 2 && strlen(argv[1]) < sizeof(fixture));
    snprintf(fixture, sizeof(fixture), "%s", argv[1]);
    snprintf(workdir, sizeof(workdir), "%s/work", fixture);
    assert(mkdir(workdir, 0700) == 0 && chdir(workdir) == 0);
    test_snap_thumb();
#ifndef __APPLE__
    test_arm_photo();
    test_photo_thumb();
#endif
    test_lookup();
    test_temp_files();
    printf("Tool execution: %d checks passed; no shell, no USB\n", checks);
    return 0;
}
