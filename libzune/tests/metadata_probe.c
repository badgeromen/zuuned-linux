/* Compile the actual implementation in this isolated translation unit so the
 * common parsing boundary can also be exercised without synthetic USB stubs. */
#include "../src/util.c"
#include <stddef.h>

static int checks, failures;
static void check(int success, const char *description)
{
    ++checks;
    printf("%s %s\n", success ? "PASS" : "FAIL", description);
    if (!success) ++failures;
}
static int same(const char *left, const char *right)
{
    return left && right ? !strcmp(left, right) : left == right;
}
static int empty(const ZuneMetadata *value)
{
    return !value->title && !value->artist && !value->albumartist && !value->album
        && !value->genre && !value->tracknumber && !value->duration_ms
        && !value->discnumber && !value->year;
}
static int load(const char *root, const char *name, ZuneMetadata *metadata)
{
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path)) return -1;
    return zune_probe(path, metadata);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *root = argv[1];
    const char *formats[] = {"tagged.mp3", "tagged.flac", "tagged.m4a", "tagged.ogg", "tagged.opus"};
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
        ZuneMetadata metadata;
        const int result = load(root, formats[i], &metadata);
        char description[200];
        snprintf(description, sizeof(description), "%s trims real text tags and preserves spelling/punctuation", formats[i]);
        check(result == 0 && same(metadata.title, "Étude \"Glass\" \\ mix")
            && same(metadata.artist, "Featured  Artist") && same(metadata.albumartist, "Album Owner")
            && same(metadata.album, "A  Record") && same(metadata.genre, "Alternative"), description);
        snprintf(description, sizeof(description), "%s reads track/disc totals, release date and duration", formats[i]);
        check(result == 0 && metadata.tracknumber == 7 && metadata.discnumber == 2
            && metadata.year == 2020 && metadata.duration_ms > 50 && metadata.duration_ms < 2000, description);
        zune_free_metadata(&metadata);
        check(empty(&metadata), "free clears all original and appended fields");
        zune_free_metadata(&metadata);
    }
    ZuneMetadata metadata;
    check(load(root, "missing.flac", &metadata) == 0 && !metadata.title && !metadata.artist
        && !metadata.albumartist && !metadata.album && !metadata.genre && !metadata.tracknumber
        && !metadata.discnumber && !metadata.year && metadata.duration_ms > 0,
        "valid untagged media succeeds with missing fields rather than filename guesses");
    zune_free_metadata(&metadata);
    check(load(root, "blank.ogg", &metadata) == 0 && !metadata.title && !metadata.artist
        && !metadata.albumartist && !metadata.album && !metadata.genre && !metadata.tracknumber
        && !metadata.discnumber && !metadata.year, "ASCII and Unicode whitespace-only tags are missing");
    zune_free_metadata(&metadata);
    check(load(root, "invalid.opus", &metadata) == 0 && same(metadata.title, "Valid title")
        && !metadata.tracknumber && !metadata.discnumber && !metadata.year,
        "invalid numeric/date tags do not reject media or destroy its valid title");
    zune_free_metadata(&metadata);
    check(load(root, "selected.mkv", &metadata) == 0 && same(metadata.title, "Container Title")
        && same(metadata.artist, "Selected Artist") && same(metadata.albumartist, "Container Owner")
        && same(metadata.album, "Selected Album") && same(metadata.genre, "Selected Genre")
        && metadata.tracknumber == 11 && metadata.discnumber == 3 && metadata.year == 1998,
        "container fields/aliases win; only selected default audio fills missing tags, never other streams");
    zune_free_metadata(&metadata);
    check(load(root, "first.mka", &metadata) == 0 && same(metadata.artist, "First Artist"),
        "without a default disposition the first audio stream supplies tag fallback");
    zune_free_metadata(&metadata);
    check(load(root, "video-only.mkv", &metadata) == 0 && same(metadata.title, "Silent Video")
        && same(metadata.artist, "Video Stream Artist") && metadata.duration_ms > 0,
        "valid video-only inspection retains duration and selected video tag fallback");
    zune_free_metadata(&metadata);
    check(load(root, "literal ' $(touch SHOULD_NOT_EXIST) `quotes`.opus", &metadata) == 0
        && same(metadata.title, "Étude \"Glass\" \\ mix"),
        "literal shell punctuation in filenames remains a filename");
    zune_free_metadata(&metadata);
    check(load(root, "escaped.flac", &metadata) == 0
        && same(metadata.title, "first\nsecond\t\"quote\" \\ path $literal `tick`"),
        "embedded newlines, tabs, quotes and backslashes survive both transports");
    zune_free_metadata(&metadata);

    const char *bad[] = {"missing-file.mp3", "malformed.flac", "truncated.ogg"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        memset(&metadata, 0x5a, sizeof(metadata));
        check(load(root, bad[i], &metadata) == -1 && empty(&metadata),
            "missing/malformed/truncated input fails with completely zeroed free-safe output");
        zune_free_metadata(&metadata);
    }
    memset(&metadata, 0x5a, sizeof(metadata));
    check(zune_probe(NULL, &metadata) == -1 && empty(&metadata) && zune_probe("file", NULL) == -1,
        "invalid arguments preserve the free-safe failure contract");

    const char *invalid_numbers[] = {"-1", "+1", "0", "65536", "4294967297", "184467440737095516160",
        "7x", "7/", "7/0", "7/6", "7/12x", "7.5", "NaN", "", "7/-2"};
    for (size_t i = 0; i < sizeof(invalid_numbers) / sizeof(*invalid_numbers); ++i)
        check(probe_number(invalid_numbers[i], UINT16_MAX) == 0, "malformed or overflowing track number cannot wrap into a valid index");
    check(probe_number("65535/65535", UINT16_MAX) == 65535 && probe_number("007 / 12", UINT16_MAX) == 7
        && probe_number("2147483647", INT_MAX) == INT_MAX && probe_number("2147483648", INT_MAX) == 0,
        "valid integer boundaries and spaced N/total forms parse safely");
    struct { const char *text; int year; } dates[] = {
        {"2020", 2020}, {"2020-09", 2020}, {"2020-09-08", 2020}, {"2020/09/08", 2020},
        {"2020-02-29", 2020}, {"2020-09-08T12:34:56.789Z", 2020}, {"2020-09-08 12:34:56+05:30", 2020},
        {"0000", 0}, {"20201", 0}, {"2020junk", 0}, {"2019-02-29", 0}, {"2020-00-01", 0},
        {"2020-13", 0}, {"2020-09-31", 0}, {"2020-09-08T99:00:00Z", 0}, {"2020-09-08junk", 0},
        {"2020-09-08T12", 0}, {"2020-09-08T12:34:56+", 0}, {"2020-09-08T12:34:56.abc", 0}
    };
    for (size_t i = 0; i < sizeof(dates) / sizeof(*dates); ++i)
        check(probe_year(dates[i].text) == dates[i].year, "release-year date parsing accepts valid forms and rejects malformed suffixes");
    check(probe_duration(NAN) == 0 && probe_duration(INFINITY) == 0 && probe_duration(-1) == 0
        && probe_duration(1e30) == UINT32_MAX, "invalid and excessive duration cannot overflow");

    ProbeTags container = {0}, stream = {0};
    probe_trim(" \t\xc2\xa0\xe3\x80\x80", &container.value[TAG_ALBUM_ARTIST]);
    probe_trim("  Alias Owner  ", &container.value[TAG_ALBUMARTIST]);
    probe_trim("Wrong Stream Owner", &stream.value[TAG_ALBUM_ARTIST]);
    probe_trim("invalid", &container.value[TAG_TRACK]);
    probe_trim("9/10", &container.value[TAG_TRACKNUMBER]);
    probe_trim("1/10", &stream.value[TAG_TRACK]);
    memset(&metadata, 0, sizeof(metadata));
    check(probe_fill(&metadata, &container, &stream) == 0 && same(metadata.albumartist, "Alias Owner")
        && metadata.tracknumber == 9, "blank/malformed preferred keys do not hide a valid container alias");
    zune_free_metadata(&metadata);
    probe_tags_free(&container);
    probe_tags_free(&stream);
    printf("%d/%d metadata probe checks passed (%s)\n", checks - failures, checks,
#ifdef USE_LIBAV
        "native libav"
#else
        "ffprobe fallback"
#endif
    );
    return failures ? 1 : 0;
}
