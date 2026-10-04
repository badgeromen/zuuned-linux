/*
 * librarytool — CLI gate for the library core.
 *
 *   librarytool scan <folder> <tmpdb>
 *     Registers <folder> as a music watch folder in <tmpdb>, scans it
 *     (synchronously, on this thread), prints per-chunk progress, final
 *     counts, and 5 sample rows.
 *
 *   librarytool videoscan <folder> <tmpdb> [type]
 *     Phase 6 gate: registers <folder> as a video watch folder
 *     (type: movies|tv|anime, default tv), scans, then runs the TMDB
 *     matcher SYNCHRONOUSLY on every unmatched row and prints what
 *     resolved — series, TMDB titles, poster paths.
 *
 *   librarytool names
 *     VideoNaming self-test: the parsing cases the mac fixed one bug at
 *     a time, frozen as expectations.
 */
#include "LibraryDb.h"
#include "LibraryScanner.h"
#include "VideoMatcher.h"
#include "VideoNaming.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QFile>
#include <sqlite3.h>

#include <cstdio>

static int runNamesSelfTest() {
    struct Case {
        const char *what;
        QString got;
        QString expect;
    };
    const QVector<Case> cases = {
        {"normalize: dots→spaces",
         VideoNaming::normalizeSeriesName(QStringLiteral("SpongeBob.SquarePants")),
         QStringLiteral("SpongeBob SquarePants")},
        {"normalize: tracker prefix",
         VideoNaming::normalizeSeriesName(QStringLiteral("www.UIndex.org    -    ONE PIECE")),
         QStringLiteral("ONE PIECE")},
        {"normalize: [group] prefix",
         VideoNaming::normalizeSeriesName(QStringLiteral("[SubsPlease] Frieren")),
         QStringLiteral("Frieren")},
        {"folder junk truncation",
         VideoNaming::cleanFolderSeriesName(
             QStringLiteral("Adventure Time S01 1080p HMAX WEBRip DD2.0 x265-Kappa")),
         QStringLiteral("Adventure Time")},
        {"folder junk keeps clean names",
         VideoNaming::cleanFolderSeriesName(QStringLiteral("The Office (US) (2005)")),
         QStringLiteral("The Office (US) (2005)")},
        {"derive: season container",
         VideoNaming::deriveSeriesFromPath(
             QStringLiteral("/tv/Breaking Bad/Season 1/Breaking.Bad.S01E01.mkv")),
         QStringLiteral("Breaking Bad")},
        {"derive: episode-dir layout",
         VideoNaming::deriveSeriesFromPath(
             QStringLiteral("/tv/The Wire/The.Wire.S02E03.720p/The.Wire.S02E03.mkv")),
         QStringLiteral("The Wire")},
        {"derive: specials",
         VideoNaming::deriveSeriesFromPath(
             QStringLiteral("/tv/Doctor Who/Specials/xmas.mkv")),
         QStringLiteral("Doctor Who")},
        {"clean: scene movie name",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Die.Hard.1988.1080p.BluRay.x264-SARTRE.mkv")),
         QStringLiteral("Die Hard 1988")},
        {"clean: keeps real subtitle",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Mission Impossible - Fallout.mkv")),
         QStringLiteral("Mission Impossible Fallout")},
        {"clean: paren year preserved",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Hackers (1995) [1080p].mp4")),
         QStringLiteral("Hackers 1995")},
        {"clean: trailing disc idx after year",
         VideoNaming::cleanFilenameForSearch(
             QStringLiteral("Back To The Future Part III 1990 1.mkv")),
         QStringLiteral("Back To The Future Part III 1990")},
        {"clean: Apollo 13 stays intact",
         VideoNaming::cleanFilenameForSearch(QStringLiteral("Apollo 13.mkv")),
         QStringLiteral("Apollo 13")},
        {"squash equality",
         VideoNaming::squash(QStringLiteral("Bob's Burgers")),
         QStringLiteral("bobsburgers")},
    };

    int failed = 0;
    for (const Case &c : cases) {
        const bool ok = c.got == c.expect;
        if (!ok)
            failed++;
        printf("  %s %s: got '%s'%s\n", ok ? "PASS" : "FAIL", c.what,
               c.got.toUtf8().constData(),
               ok ? "" : (QStringLiteral(" expected '") + c.expect
                          + QLatin1Char('\'')).toUtf8().constData());
    }

    // Scoring sanity: exact squash match wins; junk stays below threshold.
    const double exact = VideoNaming::scoreCandidate(
        QStringLiteral("bobs burgers"), QString(),
        QStringLiteral("Bob's Burgers"), QStringLiteral("2011"), 100);
    const double junk = VideoNaming::scoreCandidate(
        QStringLiteral("some totally different show"), QString(),
        QStringLiteral("Bob's Burgers"), QStringLiteral("2011"), 100);
    printf("  %s score exact-squash (%.2f >= 1.0)\n",
           exact >= 1.0 ? "PASS" : "FAIL", exact);
    printf("  %s score junk rejected (%.2f < %.2f)\n",
           junk < VideoNaming::kAcceptThreshold ? "PASS" : "FAIL", junk,
           VideoNaming::kAcceptThreshold);
    if (exact < 1.0 || junk >= VideoNaming::kAcceptThreshold)
        failed++;

    // Absolute anime mapping: 3 seasons of 10 → ep 25 = S3E5.
    QHash<int, int> counts{{1, 10}, {2, 10}, {3, 10}};
    int s = 0, e = 0;
    const bool mapped = VideoMatcher::mapAbsoluteEpisode(25, counts, &s, &e);
    const bool mapOk = mapped && s == 3 && e == 5;
    printf("  %s absolute episode 25 → S%dE%d\n", mapOk ? "PASS" : "FAIL", s, e);
    if (!mapOk)
        failed++;

    printf("names self-test: %s (%d failures)\n",
           failed == 0 ? "ALL PASS" : "FAILED", failed);
    return failed == 0 ? 0 : 1;
}

// Exercise the actual commit SQL with an old matcher snapshot; no HTTP.
struct VideoMatcherCustomizationTest {
    static void writeStaleResult(VideoMatcher &matcher, const LibVideo &old) {
        VideoMatcher::SeriesResolution meta;
        meta.tmdbId = 99; meta.title = QStringLiteral("Automatic title");
        meta.posterLocalPath = QStringLiteral("/fixture/automatic.jpg");
        matcher.writeMovieRow(old.id, meta);
        matcher.writeTvRow(old, meta, nullptr, {}, -1, -1);
    }
};

static int runCustomizeSelfTest() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir scratch;
    LibraryDb db;
    if (!scratch.isValid() || !db.open(scratch.filePath(QStringLiteral("library.db"))))
        return 1;
    int failed = 0;
    auto check = [&failed](bool ok, const char *label) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failed;
    };
    LibVideo seed;
    seed.filepath = QStringLiteral("/fixture/series/episode.mkv");
    seed.filename = QStringLiteral("episode.mkv");
    seed.category = QStringLiteral("tv"); seed.series = QStringLiteral("Original");
    seed.season = 2; seed.episode = 7; seed.durationMs = 1800000;
    check(db.insertVideo(seed), "video fixture inserted");
    LibVideo video = db.allVideos().first();
    db.updateVideoPosition(video.id, 42000);
    db.setVideosWatched({video.id}, true);
    video.tmdbId = -1; video.tmdbTitle = QStringLiteral("My fan edit");
    video.tmdbPoster = QStringLiteral("/fixture/picked-cover.jpg");
    video.customPoster = true;
    video.tmdbCached = true; video.userEdited = true;
    check(db.begin() && db.updateVideoCustomization(video) && db.commit(),
          "manual identity and chosen art commit together");
    video = db.allVideos().first();
    check(video.tmdbId == -1 && video.userEdited && video.tmdbCached
          && video.tmdbPoster == QStringLiteral("/fixture/picked-cover.jpg"),
          "manual identity and artwork remain pinned");
    check(video.season == 2 && video.episode == 7 && video.watched
          && video.lastPositionMs == 42000,
          "customizing preserves episode numbering and playback state");
    VideoMatcher matcher(scratch.filePath(QStringLiteral("library.db")));
    VideoMatcherCustomizationTest::writeStaleResult(matcher, video);
    check(db.allVideos().first().tmdbTitle == QStringLiteral("My fan edit")
          && db.allVideos().first().tmdbPoster == QStringLiteral("/fixture/picked-cover.jpg"),
          "already-running matcher cannot overwrite a manual identity or poster");

    db.begin();
    video.tmdbTitle = QStringLiteral("Must roll back");
    check(db.updateVideoCustomization(video), "first row staged in transaction");
    LibVideo missing = video; missing.id += 1000;
    check(!db.updateVideoCustomization(missing), "missing target rejects the batch");
    check(db.rollback() && db.allVideos().first().tmdbTitle == QStringLiteral("My fan edit"),
          "failed batch restores earlier row edits");

    video = db.allVideos().first();
    video.tmdbId = 0; video.tmdbCached = false; video.userEdited = false;
    video.tmdbTitle.clear(); video.tmdbPoster.clear();
    video.customPoster = false;
    check(db.begin() && db.updateVideoCustomization(video) && db.commit(),
          "reset commits automatic state");
    video = db.allVideos().first();
    check(video.tmdbId == 0 && !video.tmdbCached && !video.userEdited
          && video.tmdbPoster.isEmpty(), "reset releases the matcher user-edit guard");
    // Reset identity, then choose artwork: identity can match while the
    // separately pinned image survives both movie and TV matcher paths.
    video.tmdbPoster = QStringLiteral("/fixture/independent.jpg"); video.customPoster = true;
    check(db.begin() && db.updateVideoCustomization(video) && db.commit(),
          "reset identity plus chosen art commits independently");
    VideoMatcherCustomizationTest::writeStaleResult(matcher, video);
    video = db.allVideos().first();
    check(video.tmdbId == 99 && video.tmdbTitle == QStringLiteral("Automatic title")
          && video.tmdbPoster == QStringLiteral("/fixture/independent.jpg") && video.customPoster,
          "automatic identity lookup preserves independent custom artwork");
    const auto parsed = VideoNaming::parseIdentity(
        QStringLiteral("/tv/Original Show/Season 2/Original.Show.S02E07.mkv"), QStringLiteral("tv"));
    check(parsed.series == QStringLiteral("Original Show") && parsed.season == 2 && parsed.episode == 7,
          "reset uses scanner folder truth and filename episode numbers");

    LibTrack track;
    track.filepath = QStringLiteral("/fixture/song.flac");
    track.title = QStringLiteral("Song"); track.artist = QStringLiteral("Performer");
    track.albumartist = QStringLiteral("Owner"); track.album = QStringLiteral("Old sleeve");
    check(db.upsertTrack(track), "music fixture inserted");
    LibTrack edited = db.allTracks().first();
    edited.albumartist = QStringLiteral("New owner"); edited.album = QStringLiteral("New sleeve");
    check(db.begin() && db.updateTrackMetadata(edited.id, edited, true) && db.commit(),
          "album identity commits with canonical owner");
    track.mtime = 12345;
    check(db.upsertTrack(track), "scanner revisits the original file metadata");
    edited = db.allTracks().first();
    check(edited.albumartist == QStringLiteral("New owner")
          && edited.album == QStringLiteral("New sleeve") && edited.userEdited
          && edited.mtime == 12345, "rescan refreshes file stats and preserves chosen identity");
    // Reconstruct the actual pre-Customize schema, including pre-slot
    // playlist tables. Merely lowering user_version on a current DB
    // leaves future tables behind and does not represent an old library.
    const QByteArray legacySchema =
        "ALTER TABLE videos DROP COLUMN custom_poster;"
        "DROP TABLE collection_customizations;"
        "DROP TABLE playlist_tracks; DROP TABLE playlists;"
        "CREATE TABLE playlists(id INTEGER PRIMARY KEY,name TEXT NOT NULL);"
        "CREATE TABLE playlist_tracks(id INTEGER PRIMARY KEY,playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,position INTEGER NOT NULL);"
        "CREATE INDEX idx_plt ON playlist_tracks(playlist_id,position);";
    const QString legacyPath = scratch.filePath(QStringLiteral("legacy.db"));
    LibraryDb legacy;
    check(legacy.open(legacyPath) && legacy.insertVideo(seed), "migration fixture created");
    auto legacyVideo = legacy.allVideos().first();
    legacyVideo.tmdbPoster = QStringLiteral("/cache/vidthumbs/custom_1.jpg");
    legacy.updateVideoCustomization(legacyVideo);
    legacy.close();
    sqlite3 *raw = nullptr;
    bool prepared = sqlite3_open(legacyPath.toUtf8().constData(), &raw) == SQLITE_OK;
    if (prepared)
        prepared = sqlite3_exec(raw, (legacySchema + "PRAGMA user_version=2;").constData(),
                                  nullptr, nullptr, nullptr) == SQLITE_OK;
    if (raw) sqlite3_close(raw);
    check(prepared && legacy.open(legacyPath), "v2 library upgrades to separate artwork pins");
    check(!legacy.allVideos().isEmpty() && legacy.allVideos().first().customPoster
          && QFile::exists(legacyPath + QStringLiteral(".v2-backup")),
          "migration retains old custom covers and creates the upgrade backup");
    legacy.close();
    raw = nullptr;
    prepared = sqlite3_open(legacyPath.toUtf8().constData(), &raw) == SQLITE_OK;
    if (prepared)
        prepared = sqlite3_exec(raw, (legacySchema + "PRAGMA user_version=0;").constData(),
                                  nullptr, nullptr, nullptr) == SQLITE_OK;
    if (raw) sqlite3_close(raw);
    check(prepared && legacy.open(legacyPath) && !legacy.allVideos().isEmpty()
          && legacy.allVideos().first().customPoster,
          "historical unversioned libraries receive the artwork-pin migration too");
    printf("customize self-test: %s (%d failures)\n", failed ? "FAILED" : "ALL PASS", failed);
    return failed ? 1 : 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QString mode = argc >= 2 ? QString::fromUtf8(argv[1]) : QString();

    if (mode == QLatin1String("names"))
        return runNamesSelfTest();
    if (mode == QLatin1String("customize"))
        return runCustomizeSelfTest();

    const bool videoMode = mode == QLatin1String("videoscan");
    if (argc < 4 || (mode != QLatin1String("scan") && !videoMode)) {
        fprintf(stderr,
                "librarytool — library core gate\n"
                "  librarytool scan <folder> <tmpdb>\n"
                "  librarytool videoscan <folder> <tmpdb> [movies|tv|anime]\n"
                "  librarytool names\n"
                "  librarytool customize\n");
        return 2;
    }
    const QString folder = QString::fromUtf8(argv[2]);
    const QString dbPath = QString::fromUtf8(argv[3]);
    const QString folderType = videoMode
        ? (argc >= 5 ? QString::fromUtf8(argv[4]) : QStringLiteral("tv"))
        : QStringLiteral("music");

    {
        LibraryDb db;
        if (!db.open(dbPath)) {
            fprintf(stderr, "FAIL: db open: %s\n",
                    db.lastError().toUtf8().constData());
            return 1;
        }
        db.addWatchFolder(folder, folderType);
    }

    LibraryScanner scanner;
    scanner.setDbPath(dbPath);

    QObject::connect(&scanner, &LibraryScanner::progress,
        [](const QString &stage, int cur, int total, const QString &file) {
            fprintf(stderr, "[scan] %s %d/%d  %s\n",
                    stage.toUtf8().constData(), cur, total,
                    file.toUtf8().constData());
        });
    int added = -1, updated = -1, photos = -1;
    QObject::connect(&scanner, &LibraryScanner::finished,
        [&](int a, int u, int p) { added = a; updated = u; photos = p; });
    QObject::connect(&scanner, &LibraryScanner::failed,
        [](const QString &err) {
            fprintf(stderr, "FAIL: %s\n", err.toUtf8().constData());
        });

    scanner.scan(); // synchronous on this thread

    if (added < 0)
        return 1;

    printf("scan done: %d tracks added, %d updated, %d photos\n",
           added, updated, photos);

    LibraryDb db;
    if (!db.open(dbPath))
        return 1;

    if (!videoMode) {
        const auto tracks = db.allTracks();
        printf("total tracks in db: %d\n", int(tracks.size()));
        const int n = qMin(5, int(tracks.size()));
        for (int i = 0; i < n; i++) {
            const LibTrack &t = tracks[i];
            printf("  [%lld] '%s' | artist='%s' | albumartist='%s' | album='%s'"
                   " | genre='%s' | track=%d | dur=%dms\n",
                   (long long)t.id, t.title.toUtf8().constData(),
                   t.artist.toUtf8().constData(),
                   t.albumartist.toUtf8().constData(),
                   t.album.toUtf8().constData(), t.genre.toUtf8().constData(),
                   t.trackNumber, t.durationMs);
        }
        return 0;
    }

    // ── Phase 6 gate: run the matcher synchronously on the scan output ──
    auto videos = db.allVideos();
    printf("total videos in db: %d\n", int(videos.size()));
    for (const LibVideo &v : videos)
        printf("  [%lld] %s | series='%s' S%02dE%02d cat=%s\n",
               (long long)v.id, v.filename.toUtf8().constData(),
               v.series.toUtf8().constData(), v.season, v.episode,
               v.category.toUtf8().constData());

    QVector<LibVideo> uncached;
    for (const LibVideo &v : videos)
        if (!v.tmdbCached)
            uncached.append(v);

    if (!uncached.isEmpty()) {
        printf("matching %d videos against TMDB...\n", int(uncached.size()));
        VideoMatcher matcher(dbPath);
        QObject::connect(&matcher, &VideoMatcher::updatesCommitted,
            [](int n) { fprintf(stderr, "[match] %d rows committed\n", n); });
        matcher.matchBatch(uncached, QStringLiteral("tmdb")); // synchronous here
    }

    videos = db.allVideos();
    int matched = 0, needsMatch = 0, posters = 0;
    for (const LibVideo &v : videos) {
        if (v.tmdbId > 0) matched++;
        else if (v.tmdbCached) needsMatch++;
        if (!v.tmdbPoster.isEmpty()) posters++;
        printf("  [%lld] %s\n        → tmdb='%s' (%s) S%02dE%02d ep='%s'"
               " poster=%s still=%s\n",
               (long long)v.id, v.filename.toUtf8().constData(),
               v.tmdbTitle.toUtf8().constData(), v.tmdbYear.toUtf8().constData(),
               v.season, v.episode, v.episodeTitle.toUtf8().constData(),
               v.tmdbPoster.isEmpty() ? "NO" : "yes",
               v.episodeStill.isEmpty() ? "no" : "yes");
    }
    printf("videoscan done: %d matched, %d needs-match, %d with posters\n",
           matched, needsMatch, posters);
    return 0;
}
