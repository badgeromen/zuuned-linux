/* T3.7: the split functions and consolidated helpers keep their behavior.
 * zune_decode_filename (now one scanner plus a pattern table) must return
 * exactly what the 0.3.0 seven-block implementation returned for every name
 * below: the table was produced by that implementation. Also checks the
 * shared little-endian and file-name helpers, and that season and episode
 * numbers too large for an int are refused instead of overflowing (the
 * scanner's signed overflow aborts this UBSan build on the old code).
 * Real util.c; no USB. */
#include "../src/zune_internal.h"

static int checks, failures;
static void check(int ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", name); }
}

static const struct {
    const char *path;
    int ret;
    const char *series;
    int season, episode;
} golden[] = {
    { "Show Name - S01E05 - Episode Title.mkv", 1, "Show Name", 1, 5 },
    { "show.name.s02e10.720p.mkv", 1, "show.name.", 2, 10 },
    { "The Office S9E23.mp4", 1, "The Office", 9, 23 },
    { "Breaking Bad S05E16 Felina.mkv", 1, "Breaking Bad", 5, 16 },
    { "Show - S01E00 - Pilot.mkv", 1, "Show", 1, 0 },
    { "/media/TV/That 70s Show/Season 1/S01E01.mkv", 1, "That 70s Show", 1, 1 },
    { "/media/TV/That 70s Show/Season 1/s01e02 - Title.mkv", 1, "That 70s Show", 1, 2 },
    { "Season 1/S01E03.mkv", 1, "Unknown Series", 1, 3 },
    { "S01E04.mkv", 1, "Unknown Series", 1, 4 },
    { "/S01E05.mkv", 1, "Unknown Series", 1, 5 },
    { "/tv/S01E06.mkv", 1, "Unknown Series", 1, 6 },
    { "./S01E07.mkv", 1, "Unknown Series", 1, 7 },
    { "Show (2005) - S01E01 - Pilot.mkv", 1, "Show", 1, 1 },
    { "Show (2005) S02E03.mkv", 1, "Show", 2, 3 },
    { "Show - Extras - S00E01.mkv", 1, "Show", 0, 1 },
    { "Show Extras S01E02.mkv", 1, "Show", 0, 2 },
    { "Doctor Who (2005) - Extras S01E05.mkv", 1, "Doctor Who (2005)", 0, 5 },
    { "S10E100 Big Season.mkv", 1, "Unknown Series", 10, 100 },
    { "Show S01E01E02 Double.mkv", 1, "Show", 1, 1 },
    { "Show S1 E1.mkv", 1, "Show S1", 1, 1 },
    { "Show Season 1 Episode 2.mkv", 1, "Show Season 1", 1, 2 },
    { "SomethingS01E05.mkv", 1, "Something", 1, 5 },
    { "Mass Effect S3.mkv", 0, NULL, 1, 0 },
    { "Show Name Episode 12.mkv", 1, "Show Name", 1, 12 },
    { "Show Name episode 7.avi", 1, "Show Name", 1, 7 },
    { "Show Name - Episode 03 - The Third.mkv", 1, "Show Name", 1, 3 },
    { "One Piece EP88.mp4", 1, "One Piece", 1, 88 },
    { "One.Piece.EP1167.1080p.mkv", 1, "One.Piece", 1, 1167 },
    { "Show Ep. 5.mkv", 1, "Show", 1, 5 },
    { "Show Ep 6.mkv", 1, "Show", 1, 6 },
    { "Show ep.7.mkv", 1, "Show", 1, 7 },
    { "Show EP 0.mkv", 0, "Show", 1, 0 },
    { "Show Episode 0.mkv", 0, "Show", 1, 0 },
    { "Episode 9.mkv", 1, "Unknown Series", 1, 9 },
    { "Deep6.mkv", 0, NULL, 1, 0 },
    { "Step2 Movie.mkv", 0, NULL, 1, 0 },
    { "Keep 5.mkv", 0, NULL, 1, 0 },
    { "Prep 10 Course.mkv", 0, NULL, 1, 0 },
    { "Show - 05 - Title.mkv", 1, "Show", 1, 5 },
    { "Show-05-Title.mkv", 1, "Show", 1, 5 },
    { "Show - 123 - Title.mkv", 1, "Show", 1, 123 },
    { "Show - 1234 - Title.mkv", 0, NULL, 1, 0 },
    { "Show - 00 - Zero.mkv", 0, "Show", 1, 0 },
    { " - 07 - Leading.mkv", 1, "Unknown Series", 1, 7 },
    { "Naruto  001 - Enter Naruto Uzumaki.mkv", 1, "Naruto", 1, 1 },
    { "Naruto   002 - Second.mkv", 1, "Naruto", 1, 2 },
    { "Airplane 2.mkv", 0, NULL, 1, 0 },
    { "Airplane 2 - The Sequel.mkv", 0, NULL, 1, 0 },
    { "Fullmetal Alchemist Brotherhood E01.mkv", 1, "Fullmetal Alchemist Brotherhood", 1, 1 },
    { "Fullmetal Alchemist Brotherhood E01 Title.mkv", 1, "Fullmetal Alchemist Brotherhood", 1, 1 },
    { "Gate E12.mkv", 1, "Gate", 1, 12 },
    { "Show E1000.mkv", 0, NULL, 1, 0 },
    { "Show E0.mkv", 0, "Show", 1, 0 },
    { "[Naruto-Kun.Hu] Naruto - 116 [1080p].mkv", 1, "[Naruto-Kun.Hu] Naruto", 1, 116 },
    { "[uP] One Piece - 0542B (FHD 1080p).mkv", 1, "[uP] One Piece", 1, 542 },
    { "Show - 116.mkv", 1, "Show", 1, 116 },
    { "Show - 0116.mkv", 1, "Show", 1, 116 },
    { "Show - 12345.mkv", 0, NULL, 1, 0 },
    { "Spider-Man - 2 (2004).mkv", 1, "Spider-Man", 1, 2 },
    { "Spider-Man 2.mkv", 0, NULL, 1, 0 },
    { "Show - 7E (x).mkv", 0, NULL, 1, 0 },
    { "Show - 7d [tag].mkv", 1, "Show", 1, 7 },
    { "11-Tsuzumi Mansion [C12B68BD].mkv", 1, "", 1, 11 },
    { "1-Pilot.mkv", 1, "", 1, 1 },
    { "001-Start.mkv", 1, "", 1, 1 },
    { "2001-A Space Odyssey.mkv", 0, NULL, 1, 0 },
    { "12-34 Numbers.mkv", 0, NULL, 1, 0 },
    { "0-Zero Start.mkv", 0, "", 1, 0 },
    { "Spaceballs_1.mkv", 0, NULL, 1, 0 },
    { "Uncle Buck_100.mkv", 0, NULL, 1, 0 },
    { "The Matrix (1999).mkv", 0, NULL, 1, 0 },
    { "Movie.mkv", 0, NULL, 1, 0 },
    { "movie", 0, NULL, 1, 0 },
    { ".hidden.mkv", 0, NULL, 1, 0 },
    { ".hidden", 0, NULL, 1, 0 },
    { "name.with.dots.S01E02.mkv", 1, "name.with.dots.", 1, 2 },
    { "name.with.dots.mkv", 0, NULL, 1, 0 },
    { "noext", 0, NULL, 1, 0 },
    { "/path/with.dot/file", 0, NULL, 1, 0 },
    { "/path/with.dot/S01E09", 1, "path", 1, 9 },
    { "a.b.c.d", 0, NULL, 1, 0 },
    { "S01E01", 1, "Unknown Series", 1, 1 },
    { "s1e1.x", 1, "Unknown Series", 1, 1 },
    { "E01.mkv", 0, NULL, 1, 0 },
    { " E01.mkv", 1, "Unknown Series", 1, 1 },
    { "EP1.mkv", 1, "Unknown Series", 1, 1 },
    { "ep 1.mkv", 1, "Unknown Series", 1, 1 },
    { "Episode 1 - Title - S02E03.mkv", 1, "Episode 1 - Title", 2, 3 },
    { "Show - 05 - S01E06.mkv", 1, "Show - 05", 1, 6 },
    { "Show EP5 - 06 - x.mkv", 1, "Show", 1, 5 },
    { "Show Episode 0 - 07 - x.mkv", 1, "Show Episode 0", 1, 7 },
    { "Show Episode 0  008 - y.mkv", 1, "Show Episode 0", 1, 8 },
    { "Show EP0 E09 z.mkv", 1, "Show EP0", 1, 9 },
    { "Show EP0 - 010 [x].mkv", 1, "Show EP0", 1, 10 },
    { "Show ep0.mkv", 0, "Show", 1, 0 },
    { "11-Tsuzumi Episode 0.mkv", 1, "", 1, 11 },
    { "Show - 0 - 0 - x.mkv", 0, "Show", 1, 0 },
    { "Show - 00 - 05 - x.mkv", 0, "Show", 1, 0 },
    { "Ünïcödé Shöw S01E02.mkv", 1, "Ünïcödé Shöw", 1, 2 },
    { "日本語 S01E03.mkv", 1, "日本語", 1, 3 },
    { "Show\tTab\tS01E04.mkv", 1, "Show\tTab\t", 1, 4 },
    { "Show\t-\t05\t-\tTabbed.mkv", 1, "Show", 1, 5 },
    { "Show  \t 006 - Mixed.mkv", 1, "Show", 1, 6 },
    { "Show E01\ttab.mkv", 1, "Show", 1, 1 },
    { "Show - 07\t[t].mkv", 1, "Show", 1, 7 },
    { "UPPER EPISODE 4.MKV", 1, "UPPER", 1, 4 },
    { "mixed EpIsOdE 5.mkv", 1, "mixed", 1, 5 },
    { "/", 0, NULL, 1, 0 },
    { ".", 0, NULL, 1, 0 },
    { "..", 0, NULL, 1, 0 },
    { "/a/b/", 0, NULL, 1, 0 },
};

static void decode_golden(void) {
    int same = 0;
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); i++) {
        char *series = (char *)1;
        int season = -7, episode = -7;
        int ret = zune_decode_filename(golden[i].path, &series, &season, &episode);
        int ok = ret == golden[i].ret && season == golden[i].season && episode == golden[i].episode &&
                 (golden[i].series ? series && !strcmp(series, golden[i].series) : !series);
        if (!ok)
            printf("FAIL: \"%s\" -> %d|%s|%d|%d, expected %d|%s|%d|%d\n", golden[i].path, ret,
                   series ? series : "(null)", season, episode, golden[i].ret,
                   golden[i].series ? golden[i].series : "(null)", golden[i].season, golden[i].episode);
        same += ok;
        free(series);
    }
    char label[96];
    snprintf(label, sizeof(label), "zune_decode_filename matches the original on %d names",
             (int)(sizeof(golden) / sizeof(golden[0])));
    check(same == (int)(sizeof(golden) / sizeof(golden[0])), label);

    char *series = (char *)1;
    int season = 5, episode = 5;
    check(zune_decode_filename(NULL, &series, &season, &episode) == 0 && series == (char *)1 &&
          season == 5 && episode == 5, "a NULL path is rejected without touching the outputs");
    check(zune_decode_filename("x", NULL, &season, &episode) == 0, "a NULL output is rejected");
}

/* Numbers that do not fit in an int are not seasons or episodes. */
static const struct {
    const char *path;
    int ret;
    const char *series;
    int season, episode;
} bounds[] = {
    { "Show S99999999999E1.mkv", 0, NULL, 1, 0 },
    { "Show S1E99999999999.mkv", 0, NULL, 1, 0 },
    { "Show S2147483648E1.mkv", 0, NULL, 1, 0 },
    { "Show S2147483647E2147483647.mkv", 1, "Show", 2147483647, 2147483647 },
    { "Show Episode 99999999999999999999.mkv", 0, "Show", 1, 0 },
    { "Show Episode 4294967296.mkv", 0, "Show", 1, 0 },
    { "Show Episode 2147483648.mkv", 0, "Show", 1, 0 },
    { "Show Episode 2147483647.mkv", 1, "Show", 1, 2147483647 },
    { "Show - 99999999999 - x.mkv", 0, NULL, 1, 0 },
};

static void decode_bounds(void) {
    int same = 0;
    for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); i++) {
        char *series = NULL;
        int season = -7, episode = -7;
        int ret = zune_decode_filename(bounds[i].path, &series, &season, &episode);
        int ok = ret == bounds[i].ret && season == bounds[i].season && episode == bounds[i].episode &&
                 (bounds[i].series ? series && !strcmp(series, bounds[i].series) : !series);
        if (!ok)
            printf("FAIL: \"%s\" -> %d|%s|%d|%d\n", bounds[i].path, ret, series ? series : "(null)",
                   season, episode);
        same += ok;
        free(series);
    }
    check(same == (int)(sizeof(bounds) / sizeof(bounds[0])),
          "season and episode numbers beyond an int are refused, INT_MAX still decodes");
}

static void bytes(void) {
    uint8_t b[8];
    zune_put_le16(b, 0xBEEF);
    check(b[0] == 0xEF && b[1] == 0xBE && zune_le16(b) == 0xBEEF, "16-bit little-endian round trip");
    zune_put_le32(b, 0xFEDCBA98u);
    check(b[0] == 0x98 && b[3] == 0xFE && zune_le32(b) == 0xFEDCBA98u,
          "32-bit round trip, top byte above 0x7F (no signed shift)");
    zune_put_le64(b, 0x8877665544332211ull);
    check(b[0] == 0x11 && b[7] == 0x88 && zune_le64(b) == 0x8877665544332211ull, "64-bit round trip");
    const uint8_t wire[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    check(zune_le16(wire) == 0x0201 && zune_le32(wire) == 0x04030201u &&
          zune_le64(wire) == 0x0807060504030201ull, "decodes read the first byte as the lowest");
}

static void names(void) {
    static const struct { const char *path, *stem; } cases[] = {
        { "/music/Artist/Song.mp3", "Song" }, { "Song", "Song" }, { "a.b.c", "a.b" },
        { "/dir.with.dot/file", "file" }, { ".hidden", ".hidden" }, { ".hidden.mp3", ".hidden" },
        { "name.", "name" }, { "", "" }, { "/trailing/", "" },
    };
    int same = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *stem = zune_filename_no_ext(cases[i].path);
        int ok = stem && !strcmp(stem, cases[i].stem);
        if (!ok) printf("FAIL: zune_filename_no_ext(\"%s\") = \"%s\"\n", cases[i].path, stem ? stem : "(null)");
        same += ok;
        free(stem);
    }
    check(same == (int)(sizeof(cases) / sizeof(cases[0])), "zune_filename_no_ext strips directory and extension");
    const uint16_t list[] = { 0xDC44, 0xDC46, 0xDC8B };
    check(zune_prop_listed(0xDC46, list, 3) && !zune_prop_listed(0xDC07, list, 3) &&
          !zune_prop_listed(0xDC44, list, 0), "zune_prop_listed finds only listed properties");
}

int main(void) {
    zune_set_chatter(ZUNE_CHATTER_SILENT);
    decode_golden();
    decode_bounds();
    bytes();
    names();
    printf("Shrink giants: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
