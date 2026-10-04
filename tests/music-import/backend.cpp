#include "library/LibraryDb.h"
#include "library/AudioCopyFingerprint.h"
#include "library/LibraryScanner.h"
#include "library/LocalTrackModel.h"
#include "sync/SyncQueueModel.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>

static bool sql(sqlite3 *db, const char *query) {
    char *error = nullptr;
    const bool ok = sqlite3_exec(db, query, nullptr, nullptr, &error) == SQLITE_OK;
    if (!ok) fprintf(stderr, "SQL failed: %s\n", error);
    sqlite3_free(error);
    return ok;
}
static qint64 scalar(const QString &path, const char *query) {
    sqlite3 *db = nullptr;
    sqlite3_stmt *st = nullptr;
    qint64 out = -1;
    if (sqlite3_open(path.toUtf8().constData(), &db) == SQLITE_OK
        && sqlite3_prepare_v2(db, query, -1, &st, nullptr) == SQLITE_OK
        && sqlite3_step(st) == SQLITE_ROW) out = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st); sqlite3_close(db);
    return out;
}
static void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("fixture write failed");
}
static QString digest(const QString &value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZuunedMusicImportTest");
    app.setApplicationName("Isolated");
    QTemporaryDir fixture("/tmp/zuuned-music-import.XXXXXX");
    if (!fixture.isValid()) return 2;
    const QString music = fixture.path() + "/music", path = fixture.path() + "/library.db";
    QDir().mkpath(music);
    auto audio = [&](const QString &relative, QVariantMap tags = {}) {
        const QString file = music + '/' + relative;
        QDir().mkpath(QFileInfo(file).absolutePath());
        QStringList args{"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                         "anullsrc=r=48000:cl=stereo", "-t", "0.1"};
        for (auto it = tags.cbegin(); it != tags.cend(); ++it) args << "-metadata" << it.key() + '=' + it.value().toString();
        args << "-y" << file;
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", args);
        if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0)
            qFatal("fixture audio failed: %s", ffmpeg.readAllStandardError().constData());
        return file;
    };
    const auto base = [](QString title, QString album = "Tagged Album") {
        return QVariantMap{{"title", title}, {"artist", "Tagged Artist"}, {"album_artist", "Tagged Owner"},
                           {"album", album}, {"track", "2/10"}, {"date", "2021-09-08"}, {"genre", "Pop"}};
    };
    const QString ownerFile = audio("Downloads/21/song.flac", {{"artist", " Adele "}, {"album", "21"}, {"title", " Fire "}});
    const QString worldFile = audio("Owner/World 2/world.flac", base("World", "World 2"));
    const QString trimFile = audio("Owner/Album/trim.flac", {{"artist", " Spaced Artist "}, {"album_artist", " Tagged Owner "}, {"album", " Album "}, {"title", " Title "}, {"genre", " Pop "}});
    const QString blankFile = audio("Folder Artist/Folder Album/01 - Song.flac", {{"artist", "  "}, {"title", "  "}, {"album", "  "}, {"album_artist", "  "}});
    const QString repairFile = audio("Owner/Album/repair.flac", base("Original Title"));
    const QString editedFile = audio("Owner/Album/edited.flac", base("Tagged edit"));
    const QString pinnedFile = audio("Owner/Album/pinned.flac", base("Tagged pin"));
    const QString retagFile = audio("Owner/Album/retag.flac", base("Before Retag"));
    for (const auto &ext : {QStringLiteral("flac"), QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("ogg"), QStringLiteral("opus")})
        audio("Codec/Album/tags." + ext, base("Codec " + ext));
    for (int disc : {2, 1}) for (int track : {2, 1}) {
        auto tags = base(QStringLiteral("%1 disc%2 track%3").arg(disc == 1 ? "Z" : "A").arg(disc).arg(track), QStringLiteral("Double (CD %1)").arg(disc));
        tags["disc"] = QStringLiteral("%1/2").arg(disc);
        tags["track"] = QStringLiteral("%1/2").arg(track);
        audio(QStringLiteral("Owner/Double/CD%1/%2.flac").arg(disc).arg(track), tags);
    }
    const QString folderDisc = audio("Owner/Folder Disc/CD3/track.flac", base("Folder disc", "Folder Disc"));
    const QString suffixDisc = audio("Owner/Suffix Disc/track.flac", base("Suffix disc", "Suffix Disc [Disk 4]"));
    auto priority = base("Priority", "Priority (CD 5)"); priority["disc"] = "2/5";
    const QString tagDisc = audio("Owner/Priority/CD3/track.flac", priority);
    const QString brokenFile = music + "/broken.flac";
    write(brokenFile, "unreadable first import");

    LibraryDb db;
    if (!db.open(path) || !db.addWatchFolder(music, "music")) return 2;
    LibraryScanner scanner;
    scanner.setDbPath(path);
    int added = 0, updated = 0, committed = 0;
    QObject::connect(&scanner, &LibraryScanner::finished, [&](int a, int u, int) { added = a; updated = u; });
    QObject::connect(&scanner, &LibraryScanner::chunkCommitted, [&] { ++committed; });
    QObject::connect(&scanner, &LibraryScanner::failed, [&](const QString &error) { qFatal("scan failed: %s", qPrintable(error)); });
    auto rows = [&] {
        QHash<QString, LibTrack> result;
        for (const auto &track : db.allTracks()) result.insert(track.filepath, track);
        return result;
    };
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks; if (!ok) ++failures;
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    };
    scanner.scan();
    auto first = rows();
    check(first.size() == 20 && !first.contains(brokenFile) && db.musicProbeFailures().size() == 1,
          "failed new probes stay out of the library and remain diagnosed for retry");
    check(first[ownerFile].artist == "Adele" && first[ownerFile].albumartist == "Adele" && first[ownerFile].title == "Fire",
          "trimmed tagged performer outranks the Downloads folder owner");
    check(first[trimFile].artist == "Spaced Artist" && first[trimFile].albumartist == "Tagged Owner"
          && first[trimFile].title == "Title" && first[trimFile].album == "Album" && first[trimFile].genre == "Pop",
          "all tag boundaries trim without changing a real compilation owner");
    check(first[blankFile].artist == "Folder Artist" && first[blankFile].albumartist == "Folder Artist"
          && first[blankFile].album == "Folder Album" && first[blankFile].title == "Song",
          "whitespace-only tags permit folder and filename fallback");
    check(first[worldFile].album == "World 2" && first[worldFile].discNumber == 0,
          "legitimate World 2 title is never stripped as a disc suffix");
    bool codecs = true;
    for (const auto &track : first) if (track.filepath.contains("/Codec/"))
        codecs &= track.albumartist == "Tagged Owner" && track.artist == "Tagged Artist"
            && track.album == "Tagged Album" && track.year == 2021 && track.trackNumber == 2;
    check(codecs, "native FLAC MP3 M4A Ogg and Opus imports retain owner year and track tags");
    check(first[folderDisc].discNumber == 3 && first[suffixDisc].discNumber == 4
          && first[suffixDisc].album == "Suffix Disc" && first[tagDisc].discNumber == 2,
          "disc tags outrank folder inference and separated suffixes preserve their disc number");

    QVector<LibTrack> doubleAlbum;
    QStringList titles;
    for (const auto &track : db.allTracks()) if (track.album == "Double") { doubleAlbum << track; titles << track.title; }
    check(titles == QStringList{"Z disc1 track1", "Z disc1 track2", "A disc2 track1", "A disc2 track2"},
          "database album order is disc then track despite contrary titles and import order");
    LocalTrackModel model;
    model.setRows(doubleAlbum);
    const auto snapshots = model.rowsSnapshot();
    check(snapshots.size() == 4 && snapshots[2].toMap()["discNumber"].toInt() == 2
          && model.data(model.index(2), LocalTrackModel::DiscNumberRole).toInt() == 2
          && snapshots[2].toMap()["year"].toInt() == 2021,
          "local model roles and QML snapshots carry disc and year");
    QFile scriptFile("qml/MusicIdentity.js");
    if (!scriptFile.open(QIODevice::ReadOnly)) return 2;
    QString script = QString::fromUtf8(scriptFile.readAll()); script.remove(".pragma library");
    QJSEngine engine;
    engine.evaluate(script);
    QVariantList reverse = snapshots;
    std::reverse(reverse.begin(), reverse.end());
    engine.globalObject().setProperty("tracks", engine.toScriptValue(reverse));
    const auto sorted = engine.evaluate("tracks.sort(compareAlbumTracks).map(t => t.title)").toVariant().toStringList();
    check(sorted == titles, "production QML album comparator keeps multidisc playback order");
    SyncQueueModel queue;
    for (const auto &entry : reverse) {
        const auto track = entry.toMap();
        queue.addTrack(track["filepath"].toString(), track["title"].toString(), track["artist"].toString(),
                       track["albumartist"].toString(), track["album"].toString(), {}, track["trackNumber"].toInt(),
                       100, track["libraryId"].toLongLong(), 0, track["discNumber"].toInt(), track["year"].toInt());
    }
    QStringList queueTitles;
    for (const auto &entry : queue.entries()) queueTitles << entry.title;
    check(queueTitles == titles && queue.entries()[2].discNumber == 2 && queue.entries()[2].year == 2021,
          "transfer queue groups carry disc/year and order scrambled album additions naturally");
    const auto repeat1 = queue.addTrack("/test/repeat1.flac", "Repeated song", "A", "A", "Live", {}, 1, 100, 901, 0, 1);
    const auto repeat2 = queue.addTrack("/test/repeat2.flac", "Repeated song", "A", "A", "Live", {}, 1, 100, 902, 0, 2);
    check(!repeat1.isEmpty() && !repeat2.isEmpty(), "different library songs on separate discs are not collapsed by title");

    const auto good = first[repairFile];
    write(repairFile, "partial unreadable changed audio");
    committed = 0;
    scanner.scan();
    auto failed = rows()[repairFile];
    check(failed.title == good.title && failed.artist == good.artist && failed.albumartist == good.albumartist
          && failed.album == good.album && failed.durationMs == good.durationMs && failed.filesize == good.filesize
          && failed.mtime == good.mtime && failed.probeMtimeNs == good.probeMtimeNs && failed.probeCtimeNs == good.probeCtimeNs
          && failed.probeVersion == good.probeVersion && added == 0 && updated == 0
          && failed.audioFingerprint.isEmpty() && committed > 0,
          "failed reprobe preserves metadata and probe stats but clears stale copy evidence and refreshes");
    check(db.musicProbeFailures().size() == 2, "new and previously imported failures remain visible as retry diagnostics");
    audio("Owner/Album/repair.flac", base("Repaired Title"));
    scanner.scan();
    check(rows()[repairFile].title == "Repaired Title" && db.musicProbeFailures().size() == 1 && updated == 1,
          "next scan retries failed files and clears a recovered diagnostic");

    struct stat beforeRetag {};
    ::stat(QFile::encodeName(retagFile).constData(), &beforeRetag);
    audio("Owner/Album/retag.flac", base("After Retag!")); // same-size text; restore the exact old mtime
    const timespec times[] = {beforeRetag.st_atim, beforeRetag.st_mtim};
    if (::utimensat(AT_FDCWD, QFile::encodeName(retagFile).constData(), times, 0) != 0) return 2;
    scanner.scan();
    check(rows()[retagFile].title == "After Retag!" && QFileInfo(retagFile).size() == beforeRetag.st_size,
          "ctime fingerprint detects a same-size retag with exact nanosecond mtime restored");

    auto edited = rows()[editedFile];
    edited.title = "My title"; edited.artist = "My artist"; edited.albumartist = "My owner";
    edited.album = "My album"; edited.genre = "My genre"; edited.year = 1999; edited.trackNumber = 8; edited.discNumber = 7;
    if (!db.updateTrackMetadata(edited.id, edited, true) || !db.updateTrackPosition(edited.id, 321)) return 2;
    sqlite3 *raw = nullptr; sqlite3_open(path.toUtf8().constData(), &raw);
    sql(raw, "UPDATE tracks SET probe_version=0;");
    // A deliberately wrong old automatic row must repair without a stat change.
    const auto wrongSql = QStringLiteral("UPDATE tracks SET title='Old parser title',album='Old parser album' WHERE id=%1;").arg(first[worldFile].id);
    sql(raw, wrongSql.toUtf8().constData());
    const auto pinSql = QStringLiteral("UPDATE tracks SET albumartist='Pinned owner',album='Pinned album' WHERE id=%1;").arg(first[pinnedFile].id);
    sql(raw, pinSql.toUtf8().constData()); sqlite3_close(raw);
    const QString artPath = fixture.path() + "/pinned-art.jpg";
    write(artPath, "original custom art bytes");
    const QString artKey = digest("Pinned owner\nPinned album");
    db.setCollectionCustomization("album", artKey, {}, artPath);
    scanner.scan();
    const auto repaired = rows();
    check(repaired[worldFile].title == "World" && repaired[worldFile].album == "World 2" && repaired[worldFile].probeVersion == 1,
          "parser-version repair benefits existing automatic metadata without requiring changed files");
    const auto preserved = repaired[editedFile];
    check(preserved.title == "My title" && preserved.artist == "My artist" && preserved.albumartist == "My owner"
          && preserved.album == "My album" && preserved.genre == "My genre" && preserved.year == 1999
          && preserved.trackNumber == 8 && preserved.discNumber == 7 && preserved.lastPositionMs == 321 && preserved.userEdited,
          "versioned re-probe preserves manual metadata disc/year playback position and user-edited state");
    QFile art(artPath); art.open(QIODevice::ReadOnly);
    check(repaired[pinnedFile].albumartist == "Pinned owner" && repaired[pinnedFile].album == "Pinned album"
          && db.collectionCustomization("album", artKey)["artPath"] == artPath && art.readAll() == "original custom art bytes",
          "automatic repair cannot detach keyed collection artwork or alter its original bytes");
    scanner.scan();
    check(added == 0 && updated == 0, "current successful fingerprints take the no-op path on following scans");

    const auto failurePaths = [&] {
        QStringList paths;
        for (const auto &failure : db.musicProbeFailures()) paths << failure.toMap()["filepath"].toString();
        return paths;
    };
    QFile::remove(brokenFile);
    committed = 0;
    scanner.scan();
    check(db.musicProbeFailures().isEmpty() && committed == 0,
          "completed accessible scans clear deleted new-file failures without refreshing galleries");
    write(brokenFile, "corrupt file before rename");
    scanner.scan();
    const QString renamedBroken = music + "/renamed.flac";
    if (!QFile::rename(brokenFile, renamedBroken)) return 2;
    scanner.scan();
    check(failurePaths() == QStringList{renamedBroken},
          "renamed corrupt files retain only the new path retry diagnostic");
    sqlite3_open(path.toUtf8().constData(), &raw);
    const auto excludeSql = QStringLiteral("INSERT INTO excluded_files(filepath) VALUES('%1')").arg(renamedBroken);
    sql(raw, excludeSql.toUtf8().constData()); sqlite3_close(raw);
    scanner.scan();
    check(db.musicProbeFailures().isEmpty(), "completed accessible scans clear failures for excluded paths");

    const QString offlineRoot = music + "/external", offlineFile = offlineRoot + "/broken.flac";
    QDir().mkpath(offlineRoot);
    write(offlineFile, "unreadable external song");
    db.addWatchFolder(offlineRoot, "music");
    scanner.scan();
    const QString detachedRoot = fixture.path() + "/detached";
    if (!QDir().rename(offlineRoot, detachedRoot)) return 2;
    scanner.scan();
    check(failurePaths().contains(offlineFile),
          "unavailable nested watched roots preserve failures despite completed readable parent walks");
    if (!QDir().rename(detachedRoot, offlineRoot)) return 2;
    if (::chmod(QFile::encodeName(offlineRoot).constData(), 0) != 0) return 2;
    scanner.scan();
    if (::chmod(QFile::encodeName(offlineRoot).constData(), 0700) != 0) return 2;
    check(failurePaths().contains(offlineFile), "unreadable watched roots preserve existing retry diagnostics");

    const QString blockedDir = offlineRoot + "/blocked", blockedFile = blockedDir + "/gone.flac";
    QDir().mkpath(blockedDir);
    write(blockedFile, "unreadable child song");
    scanner.scan();
    QFile::remove(blockedFile);
    if (::chmod(QFile::encodeName(blockedDir).constData(), 0100) != 0) return 2;
    scanner.scan();
    if (::chmod(QFile::encodeName(blockedDir).constData(), 0700) != 0) return 2;
    check(failurePaths().contains(blockedFile), "partial walks through unreadable child directories do not prune absent failures");
    const auto cancellation = QObject::connect(&scanner, &LibraryScanner::progress,
                                               [&](const QString &, int, int, const QString &) { scanner.cancel(); });
    scanner.scan();
    QObject::disconnect(cancellation);
    check(failurePaths().contains(blockedFile), "cancelled scans never clear missing-file diagnostics");
    scanner.scan();
    check(!failurePaths().contains(blockedFile) && failurePaths().contains(offlineFile),
          "following completed scan clears the missing file while preserving real ongoing failures");
    bool rootReplaced = false;
    const auto replacement = QObject::connect(&scanner, &LibraryScanner::progress,
        [&](const QString &, int, int, const QString &) {
            if (!rootReplaced) {
                if (!QDir().rename(offlineRoot, detachedRoot) || !QDir().mkpath(offlineRoot))
                    qFatal("fixture root replacement failed");
                rootReplaced = true;
            }
        });
    scanner.scan();
    QObject::disconnect(replacement);
    check(rootReplaced && failurePaths().contains(offlineFile),
          "replaced or unmounted roots cannot authorize pruning from a different directory identity");
    if (!QDir().rmdir(offlineRoot) || !QDir().rename(detachedRoot, offlineRoot)) return 2;

    LibTrack formatA;
    formatA.filepath = music + "/format-disc1.flac";
    formatA.title = "Same song"; formatA.artist = "Format Artist"; formatA.album = "Format Album";
    formatA.trackNumber = 1; formatA.discNumber = 1;
    LibTrack formatB = formatA; formatB.filepath = music + "/format-disc2.mp3"; formatB.discNumber = 2;
    db.upsertTrack(formatA); db.upsertTrack(formatB);
    check(db.resolveDuplicateFormats("flac") == 0,
          "duplicate-format cleanup cannot collapse different discs with equal titles and track numbers");
    LibTrack twin = formatA; twin.filepath = music + "/format-disc1.mp3";
    db.upsertTrack(twin);
    check(db.resolveDuplicateFormats("flac") == 1 && rows().contains(formatB.filepath) && !rows().contains(twin.filepath),
          "duplicate-format cleanup still removes a real same-disc format twin");

    const QString legacy = fixture.path() + "/legacy.db";
    LibraryDb migration;
    if (!migration.open(legacy) || !migration.upsertTrack(edited)) return 2;
    migration.close();
    sqlite3_open(legacy.toUtf8().constData(), &raw);
    if (!sql(raw, "PRAGMA journal_mode=WAL;PRAGMA wal_autocheckpoint=0;"
             "ALTER TABLE tracks DROP COLUMN audio_fingerprint;ALTER TABLE tracks DROP COLUMN discnumber;ALTER TABLE tracks DROP COLUMN probe_mtime_ns;"
             "ALTER TABLE tracks DROP COLUMN probe_ctime_ns;ALTER TABLE tracks DROP COLUMN probe_version;"
             "ALTER TABLE playlist_tracks DROP COLUMN pending_disc_number;ALTER TABLE playlist_tracks DROP COLUMN pending_track_number;"
             "DROP TABLE music_probe_failures;PRAGMA user_version=5;"
             "UPDATE tracks SET title='Committed in WAL',user_edited=1;")) return 2;
    check(migration.open(legacy) && scalar(legacy, "PRAGMA user_version") == 9
          && migration.allTracks().first().title == "Committed in WAL"
          && migration.allTracks().first().userEdited && migration.allTracks().first().probeVersion == 0,
          "schema v5 migrates existing rows without erasing edits and schedules safe re-probe");
    check(scalar(legacy + ".v5-backup", "SELECT COUNT(*) FROM tracks WHERE title='Committed in WAL' AND user_edited=1") == 1
          && scalar(legacy + ".v5-backup", "PRAGMA user_version") == 5,
          "pre-migration SQLite snapshot includes committed WAL content and original schema");
    sqlite3_close(raw);
    // Audio packet evidence ignores metadata containers but verifies payload.
    std::atomic_bool cancel{false};
    const QString original = audio("copy-tests/a.flac", base("Copy"));
    const QString retagged = audio("copy-tests/b.flac", base("Different tags"));
    const QString originalHash = AudioCopyFingerprint::read(original, cancel);
    check(!originalHash.isEmpty() && originalHash == AudioCopyFingerprint::read(retagged, cancel),
          "full audio fingerprint ignores differing container tags");
    const QString cover = fixture.path() + "/cover.bmp";
    QImage coverImage(8, 8, QImage::Format_RGB32); coverImage.fill(Qt::red);
    if (!coverImage.save(cover)) return 2;
    const QString illustrated = fixture.path() + "/illustrated.flac";
    QProcess addArt;
    addArt.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-i", original,
        "-i", cover, "-map", "0:a", "-map", "1:v", "-c:a", "copy", "-c:v", "mjpeg",
        "-disposition:v", "attached_pic", "-y", illustrated});
    check(addArt.waitForFinished(30000) && addArt.exitCode() == 0
          && originalHash == AudioCopyFingerprint::read(illustrated, cancel),
          "attached album artwork does not change verified audio copy identity");
    const QString multiple = fixture.path() + "/multiple.mka";
    QProcess multiAudio;
    multiAudio.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-i", original,
        "-map", "0:a", "-map", "0:a", "-c", "copy", "-y", multiple});
    check(multiAudio.waitForFinished(30000) && multiAudio.exitCode() == 0
          && AudioCopyFingerprint::read(multiple, cancel).isEmpty(),
          "multiple audio streams cannot establish a single-track copy");
    cancel = true;
    check(AudioCopyFingerprint::read(original, cancel).isEmpty(), "cancelled audio reads provide no copy evidence");
    cancel = false;
    check(AudioCopyFingerprint::read(brokenFile, cancel).isEmpty()
          && AudioCopyFingerprint::read(music + "/absent.flac", cancel).isEmpty(),
          "corrupt and absent files provide no copy evidence");
    QProcess changedAudio;
    changedAudio.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
        "sine=frequency=880:sample_rate=48000", "-t", "0.1", "-y", retagged});
    check(changedAudio.waitForFinished(30000) && changedAudio.exitCode() == 0
          && originalHash != AudioCopyFingerprint::read(retagged, cancel), "changed audio is not a copy");
    const QString duplicateA = audio("copies-one/song.flac", base("Verified copy"));
    const QString duplicateB = audio("copies-two/song.flac", base("Verified copy"));
    const QString untaggedCopy = audio("untagged-copy.flac");
    const QString conflictingCopy = audio("other-tags/song.flac", {
        {"title", "Different title"}, {"artist", "Different artist"},
        {"album", "Different album"}});
    QVector<QPair<int, int>> copyProgress;

    QObject::connect(&scanner, &LibraryScanner::progress,
        [&](const QString &phase, int done, int total, const QString &) {
            if (phase == "checking copies") copyProgress.append({done, total});
        });
    scanner.scan();
    check(!copyProgress.isEmpty() && copyProgress.first().first == 0
          && copyProgress.last().first == copyProgress.last().second
          && copyProgress.last().second >= 2,
          "copy checking reports all indexed source progress through completion");
    const auto copies = rows();
    check(!copies[duplicateA].audioFingerprint.isEmpty()
          && copies[duplicateA].audioFingerprint == copies[duplicateB].audioFingerprint,
          "scanner persists evidence for metadata twins while retaining both source rows");
    check(copyProgress.last().second == copies.size(), "copy checking includes the entire indexed library");
    check(!copies[ownerFile].audioFingerprint.isEmpty(), "unique metadata never prevents payload verification");
    check(copies[untaggedCopy].artist == "Unknown Artist"
          && copies[untaggedCopy].album == "Unknown Album"
          && copies[untaggedCopy].discNumber == 0 && copies[untaggedCopy].trackNumber == 0
          && !copies[untaggedCopy].audioFingerprint.isEmpty(),
          "unknown artist and album with missing positions still receive copy evidence");
    check(copies[untaggedCopy].audioFingerprint == copies[duplicateA].audioFingerprint
          && copies[conflictingCopy].audioFingerprint == copies[duplicateA].audioFingerprint,
          "identical payloads receive identical evidence despite absent or conflicting metadata");

    scanner.scan();
    check(added == 0 && updated == 0 && rows()[duplicateA].audioFingerprint == copies[duplicateA].audioFingerprint,
          "unchanged rescans reuse persisted copy evidence");
    auto staleCopy = copies[duplicateA];
    staleCopy.filesize++;
    check(!db.updateAudioFingerprint(staleCopy, "stale"), "fingerprint publication rejects changed source stats");
    auto changedCopy = copies[duplicateA];
    changedCopy.probeMtimeNs++;
    check(db.upsertTrack(changedCopy) && rows()[duplicateA].audioFingerprint.isEmpty(),
          "changed source metadata invalidates cached copy evidence");
    // Start with a fully grouped pair, then force only failure work. The UI
    // must receive a refresh even when no successful metadata probe commits.
    scanner.scan();
    check(!rows()[duplicateA].audioFingerprint.isEmpty(), "copy evidence restored after a successful re-probe");
    write(duplicateA, "broken replacement");
    committed = 0;
    scanner.scan();
    check(rows()[duplicateA].audioFingerprint.isEmpty() && !rows()[duplicateB].audioFingerprint.isEmpty()
          && committed > 0, "failed re-probe clears prior copy evidence and refreshes the library");
    audio("copies-one/song.flac", base("Verified copy"));
    scanner.scan();
    check(!rows()[duplicateA].audioFingerprint.isEmpty(), "repaired source regains verified copy evidence");
    if (!QFile::remove(duplicateA)) return 2;
    committed = 0;
    scanner.scan();
    check(rows()[duplicateA].audioFingerprint.isEmpty() && !rows()[duplicateB].audioFingerprint.isEmpty()
          && committed > 0, "missing preferred source loses copy evidence and refreshes the library");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
