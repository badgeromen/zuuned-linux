#include "library/LibraryDb.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <cstdio>

static bool sql(const QString &path, const char *query) {
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK) return false;
    char *error = nullptr;
    const bool ok = sqlite3_exec(db, query, nullptr, nullptr, &error) == SQLITE_OK;
    if (!ok) fprintf(stderr, "%s\n", error ? error : "SQL failed");
    sqlite3_free(error); sqlite3_close(db); return ok;
}

static qint64 scalar(const QString &path, const char *query) {
    sqlite3 *db = nullptr; sqlite3_stmt *stmt = nullptr; qint64 value = -1;
    if (sqlite3_open(path.toUtf8().constData(), &db) == SQLITE_OK
        && sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK
        && sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt); sqlite3_close(db); return value;
}

using Entry = LibraryDb::PlaylistEntry;
static Entry resolved(qint64 id) { Entry entry; entry.trackId = id; return entry; }
static Entry pending(QString title) { Entry entry; entry.artist = "Artist"; entry.album = "Album"; entry.title = title; return entry; }
static QVector<qint64> entryIds(const QVector<Entry> &entries) {
    QVector<qint64> ids; for (const auto &entry : entries) if (entry.trackId >= 0) ids.append(entry.id); return ids;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root("/tmp/zuuned-playlist-order-db-XXXXXX");
    if (!root.isValid()) return 2;
    const QString path = root.path() + "/library.db";
    LibraryDb db;
    if (!db.open(path)) return 2;
    for (const QString &title : {"A", "B", "C", "D", "E"}) {
        LibTrack track; track.filepath = "/offline/" + title + ".flac";
        track.title = title; track.artist = "Artist"; track.album = "Album";
        if (!db.upsertTrack(track)) return 2;
    }
    QHash<QString, qint64> tracks;
    for (const auto &track : db.allTracks()) tracks.insert(track.title, track.id);
    const auto A = tracks["A"], B = tracks["B"], C = tracks["C"], D = tracks["D"], E = tracks["E"];
    db.close();
    const QString legacy = QStringLiteral(
        "DROP TABLE playlist_tracks;DROP TABLE playlists;DROP TABLE collection_customizations;"
        "DROP TABLE music_probe_failures;ALTER TABLE tracks DROP COLUMN audio_fingerprint;ALTER TABLE tracks DROP COLUMN discnumber;"
        "ALTER TABLE tracks DROP COLUMN probe_mtime_ns;ALTER TABLE tracks DROP COLUMN probe_ctime_ns;"
        "ALTER TABLE tracks DROP COLUMN probe_version;"
        "CREATE TABLE playlists(id INTEGER PRIMARY KEY,name TEXT NOT NULL);"
        "CREATE TABLE playlist_tracks(id INTEGER PRIMARY KEY,playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,position INTEGER NOT NULL);"
        "CREATE INDEX idx_plt ON playlist_tracks(playlist_id,position);"
        "INSERT INTO playlists VALUES(55,'Legacy');"
        "INSERT INTO playlist_tracks VALUES(501,55,%1,0),(502,55,%2,1),(503,55,%1,2);"
        "PRAGMA user_version=3;").arg(A).arg(C);
    if (!sql(path, legacy.toUtf8().constData()) || !db.open(path)) return 2;
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char *label) { ++checks; if (!ok) ++failed;
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label); };
    check(scalar(path, "PRAGMA user_version") == 9 && QFile::exists(path + ".v3-backup")
          && scalar(path, "SELECT COUNT(*) FROM collection_customizations") == 0,
          "v3 upgrades through durable slots and collection customizations with a backup");
    check(db.playlistTrackIds(55) == QVector<qint64>{A, C, A}
          && db.playlistEntries(55).first().id == 501,
          "migration retains playlist IDs, occurrence IDs, order and repeated tracks");
    check(scalar(path, "SELECT COUNT(*) FROM pragma_foreign_key_check") == 0,
          "rebuilt playlist foreign keys reference the final table names");

    const auto imported = db.createPlaylistEntries("Out of order", {resolved(A), pending("B"), resolved(C), pending("D"), pending("B")});
    const auto importedEntries = db.playlistEntries(imported);
    check(imported > 55 && importedEntries.size() == 5 && db.playlistTrackIds(imported) == QVector<qint64>{A, C},
          "every imported occurrence immediately owns a durable ordered slot");
    QVector<int> counts, missing;
    const auto playlists = db.allPlaylists(&counts, &missing);
    int index = -1; for (int i = 0; i < playlists.size(); ++i) if (playlists[i].first == imported) index = i;
    check(index >= 0 && counts[index] == 2 && missing[index] == 3,
          "resolved and pending counts remain distinct");
    check(db.resolvePlaylistEntry(importedEntries[3].id, D)
          && db.playlistTrackIds(imported) == QVector<qint64>{A, C, D},
          "a later download fills its own slot without moving earlier tracks");
    db.close();
    check(db.open(path) && db.playlistEntries(imported)[1].id == importedEntries[1].id
          && db.playlistEntries(imported)[1].trackId < 0,
          "pending slot identity and order survive a database reopen");
    check(db.resolvePlaylistEntry(importedEntries[4].id, B) && db.resolvePlaylistEntry(importedEntries[1].id, B)
          && db.playlistTrackIds(imported) == QVector<qint64>{A, B, C, D, B},
          "reverse completion preserves the original sequence and intentional repeats");

    const auto editable = db.createPlaylistEntries("Edit me", {resolved(A), pending("B"), resolved(C)});
    const auto baseline = db.playlistEntries(editable);
    check(db.appendPlaylistTracks(editable, {D}) && db.resolvePlaylistEntry(baseline[1].id, B)
          && db.editPlaylistEntries(editable, "Edited", {baseline[2], resolved(E), baseline[0]}, entryIds(baseline))
          && db.playlistTrackIds(editable) == QVector<qint64>{C, B, E, A, D},
          "editor reorder and insertion preserve unseen resolution and an external append");

    const auto repeated = db.createPlaylistEntries("Repeats", {resolved(A), pending("B"), resolved(A)});
    const auto repeatedSlots = db.playlistEntries(repeated);
    check(db.editPlaylistEntries(repeated, QString(), {repeatedSlots[2]}, entryIds(repeatedSlots))
          && db.playlistEntries(repeated).size() == 2
          && db.playlistEntries(repeated)[1].id == repeatedSlots[2].id,
          "removing one repeated occurrence keeps the exact occurrence the user chose");
    check(db.resolvePlaylistEntry(repeatedSlots[1].id, B)
          && db.playlistTrackIds(repeated) == QVector<qint64>{B, A},
          "late resolution respects a deliberate removal around its slot");
    const auto clearing = db.createPlaylistEntries("Clear", {resolved(A), pending("B"), resolved(C)});
    check(db.setPlaylistTracks(clearing, {}) && db.playlistEntries(clearing).size() == 1
          && db.resolvePlaylistEntry(db.playlistEntries(clearing).first().id, B)
          && db.playlistTrackIds(clearing) == QVector<qint64>{B},
          "clearing observed tracks does not discard hidden pending imports");

    const auto rollbackSlots = db.playlistEntries(editable);
    const auto rollbackTracks = db.playlistTrackIds(editable);
    check(!db.editPlaylistEntries(editable, "Should roll back", {resolved(999999)}, entryIds(rollbackSlots))
          && db.playlistTrackIds(editable) == rollbackTracks,
          "a failed edit rolls back removals and new entries together");
    bool originalName = false;
    for (const auto &playlist : db.allPlaylists()) if (playlist.first == editable) originalName = playlist.second == "Edited";
    check(originalName, "failed membership writes cannot commit a new playlist name");
    const int oldCount = db.allPlaylists().size();
    check(db.createPlaylist("Bad", {A, 999999}) < 0 && db.allPlaylists().size() == oldCount,
          "a failed batch import cannot leave a partial playlist behind");

    const auto deleted = db.createPlaylistEntries("Delete", {pending("B")});
    const qint64 deletedSlot = db.playlistEntries(deleted).first().id;
    if (!sql(path, QStringLiteral("INSERT INTO collection_customizations(kind,item_key,art_path) VALUES('mixtape','%1','/art.jpg')")
             .arg(deleted).toUtf8().constData())) return 2;
    check(db.deletePlaylist(deleted) && !db.resolvePlaylistEntry(deletedSlot, B)
          && db.playlistEntries(deleted).isEmpty()
          && scalar(path, "SELECT COUNT(*) FROM collection_customizations") == 0,
          "playlist deletion atomically removes pending slots and its artwork override");
    const auto replacement = db.createPlaylist("Replacement", {A});
    check(replacement > deleted && db.playlistEntries(replacement).first().id > deletedSlot,
          "deleted playlist and occurrence IDs cannot attach old state to a new playlist");

    Entry legacyB = pending("B"); legacyB.playlistId = 55;
    Entry legacyD = pending("D"); legacyD.playlistId = 55;
    Entry orphan = pending("E"); orphan.playlistId = 999999;
    check(db.migratePendingPlaylistEntries({legacyB, legacyD, legacyB, orphan})
          && db.playlistEntries(55).size() == 6
          && db.playlistEntries(55)[3].title == "B" && db.playlistEntries(55)[4].title == "D",
          "legacy notes retain their recorded order and repeats; deleted-playlist notes are dropped");
    check(db.migratePendingPlaylistEntries({legacyB, legacyD, legacyB}) && db.playlistEntries(55).size() == 6,
          "the migration marker prevents duplicates after a crash before QSettings cleanup");
    printf("Playlist order database: %d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
