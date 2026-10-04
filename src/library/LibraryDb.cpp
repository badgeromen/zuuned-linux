#include "LibraryDb.h"
#include <QJsonDocument>
#include <QJsonObject>

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <sqlite3.h>

static const char *kPhotoAlbumSchema = R"sql(
CREATE TABLE IF NOT EXISTS photo_albums (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  parent_id INTEGER REFERENCES photo_albums(id) ON DELETE CASCADE,
  name TEXT NOT NULL CHECK(length(trim(name)) BETWEEN 1 AND 200),
  CHECK(parent_id IS NULL OR parent_id != id)
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_photo_album_name
  ON photo_albums(COALESCE(parent_id,0),name COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_photo_album_parent ON photo_albums(parent_id);
CREATE TABLE IF NOT EXISTS photo_album_photos (
  album_id INTEGER NOT NULL REFERENCES photo_albums(id) ON DELETE CASCADE,
  photo_id INTEGER NOT NULL REFERENCES photos(id) ON DELETE CASCADE,
  position INTEGER NOT NULL,
  PRIMARY KEY(album_id,photo_id)
);
CREATE INDEX IF NOT EXISTS idx_photo_album_order ON photo_album_photos(album_id,position);
CREATE INDEX IF NOT EXISTS idx_photo_album_photo ON photo_album_photos(photo_id);
)sql";

// Schema — ported from LibraryService.swift's inline schema with the
// mac's later ALTER-TABLE migrations baked in (Linux starts clean):
// watch_folders.type, tracks.albumartist/acoustid_scanned/positions.
static const char *kSchema = R"sql(
CREATE TABLE IF NOT EXISTS tracks (
  id INTEGER PRIMARY KEY,
  filepath TEXT UNIQUE NOT NULL,
  title TEXT,
  artist TEXT,
  albumartist TEXT DEFAULT '',
  album TEXT,
  genre TEXT,
  duration INTEGER DEFAULT 0,
  filesize INTEGER DEFAULT 0,
  tracknumber INTEGER DEFAULT 0,
  discnumber INTEGER DEFAULT 0,
  year INTEGER DEFAULT 0,
  mtime INTEGER DEFAULT 0,
  probe_mtime_ns INTEGER DEFAULT 0,
  probe_ctime_ns INTEGER DEFAULT 0,
  probe_version INTEGER DEFAULT 0,
  audio_fingerprint TEXT NOT NULL DEFAULT '',
  user_edited INTEGER DEFAULT 0,
  acoustid_scanned INTEGER DEFAULT 0,
  last_position_ms INTEGER DEFAULT 0,
  last_played_at INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_artist ON tracks(artist COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_albumartist ON tracks(albumartist COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_album ON tracks(album COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS idx_genre ON tracks(genre COLLATE NOCASE);
CREATE TABLE IF NOT EXISTS music_probe_failures (
  filepath TEXT PRIMARY KEY,
  error TEXT NOT NULL,
  last_attempt_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS watch_folders (
  id INTEGER PRIMARY KEY,
  path TEXT UNIQUE NOT NULL,
  type TEXT DEFAULT 'all'
);
CREATE TABLE IF NOT EXISTS playlists (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS playlist_tracks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
  track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE,
  position INTEGER NOT NULL,
  pending_artist TEXT NOT NULL DEFAULT '',
  pending_album TEXT NOT NULL DEFAULT '',
  pending_title TEXT NOT NULL DEFAULT '',
  pending_disc_number INTEGER NOT NULL DEFAULT 0,
  pending_track_number INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_plt ON playlist_tracks(playlist_id, position);
CREATE TABLE IF NOT EXISTS collection_customizations (
  kind TEXT NOT NULL,
  item_key TEXT NOT NULL,
  identity_json TEXT NOT NULL DEFAULT '{}',
  art_path TEXT NOT NULL DEFAULT '',
  PRIMARY KEY(kind, item_key)
);
CREATE TABLE IF NOT EXISTS sync_rules (
  id INTEGER PRIMARY KEY,
  rule_type TEXT NOT NULL,
  rule_value TEXT,
  enabled INTEGER DEFAULT 1
);
CREATE TABLE IF NOT EXISTS sync_state (
  key TEXT PRIMARY KEY,
  value TEXT
);
CREATE TABLE IF NOT EXISTS videos (
  id INTEGER PRIMARY KEY,
  filepath TEXT UNIQUE,
  filename TEXT,
  filesize INTEGER,
  mtime INTEGER,
  duration INTEGER DEFAULT 0,
  width INTEGER DEFAULT 0,
  height INTEGER DEFAULT 0,
  description TEXT DEFAULT '',
  category TEXT DEFAULT '',
  series TEXT DEFAULT '',
  season INTEGER DEFAULT 0,
  episode INTEGER DEFAULT 0,
  episode_title TEXT DEFAULT '',
  tmdb_id INTEGER DEFAULT 0,
  tmdb_title TEXT DEFAULT '',
  tmdb_poster TEXT DEFAULT '',
  custom_poster INTEGER DEFAULT 0,
  tmdb_cast TEXT DEFAULT '',
  tmdb_director TEXT DEFAULT '',
  tmdb_rating REAL DEFAULT 0,
  tmdb_genres TEXT DEFAULT '',
  tmdb_year TEXT DEFAULT '',
  tmdb_cached INTEGER DEFAULT 0,
  last_position_ms INTEGER DEFAULT 0,
  last_played_at INTEGER DEFAULT 0,
  watched INTEGER DEFAULT 0,
  episode_still TEXT DEFAULT '',
  episode_air_date TEXT DEFAULT '',
  episode_rating REAL DEFAULT 0,
  episode_runtime INTEGER DEFAULT 0,
  episode_tmdb_id INTEGER DEFAULT 0,
  user_edited INTEGER DEFAULT 0,
  lookup_attempts INTEGER DEFAULT 0,
  last_lookup_at INTEGER DEFAULT 0,
  probed_at INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_video_series ON videos(series COLLATE NOCASE);
CREATE TABLE IF NOT EXISTS photos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  filepath TEXT UNIQUE,
  filename TEXT,
  filesize INTEGER,
  mtime INTEGER
);
CREATE TABLE IF NOT EXISTS excluded_files (
  filepath TEXT PRIMARY KEY,
  excluded_at INTEGER DEFAULT (strftime('%s','now')),
  reason TEXT DEFAULT ''
);
CREATE TABLE IF NOT EXISTS artists (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  mbid TEXT DEFAULT '',
  image_path TEXT DEFAULT '',
  fetch_attempts INTEGER DEFAULT 0,
  last_fetch_at INTEGER DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_artists_name ON artists(name COLLATE NOCASE);
)sql";

namespace {

// RAII statement wrapper
class Stmt {
public:
    Stmt(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
            m_stmt = nullptr;
    }
    ~Stmt() { if (m_stmt) sqlite3_finalize(m_stmt); }
    bool ok() const { return m_stmt != nullptr; }
    sqlite3_stmt *get() { return m_stmt; }

    void bindText(int i, const QString &s) {
        const QByteArray utf8 = s.toUtf8();
        sqlite3_bind_text(m_stmt, i, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
    }
    void bindInt64(int i, qint64 v) { sqlite3_bind_int64(m_stmt, i, v); }
    void bindInt(int i, int v) { sqlite3_bind_int(m_stmt, i, v); }
    void bindNull(int i) { sqlite3_bind_null(m_stmt, i); }

    int step() { return sqlite3_step(m_stmt); }

    QString colText(int i) {
        const unsigned char *t = sqlite3_column_text(m_stmt, i);
        return t ? QString::fromUtf8(reinterpret_cast<const char *>(t)) : QString();
    }
    qint64 colInt64(int i) { return sqlite3_column_int64(m_stmt, i); }
    int colInt(int i) { return sqlite3_column_int(m_stmt, i); }

private:
    sqlite3_stmt *m_stmt = nullptr;
};

} // namespace

LibraryDb::~LibraryDb() { close(); }

bool LibraryDb::open(const QString &path) {
    close();
    QString dbPath = path;
    if (dbPath.isEmpty()) {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        dbPath = dir + QStringLiteral("/library.db");
    }
    if (sqlite3_open(dbPath.toUtf8().constData(), &m_db) != SQLITE_OK) {
        m_lastError = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db))
                           : QStringLiteral("sqlite3_open failed");
        close();
        return false;
    }

    // A2: schema versioning. user_version 0 = fresh DB or the
    // pre-versioning era. Infer existing-table versions below so a
    // historical DB receives new columns too. A DB from
    // a NEWER app refuses politely instead of corrupting; migrations
    // append to kMigrations and bump kSchemaVersion.
    static const int kSchemaVersion = 9;
    static const QByteArray photoAlbumMigration = QByteArray(
        "CREATE TABLE photos_v7 (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "filepath TEXT UNIQUE,filename TEXT,filesize INTEGER,mtime INTEGER);"
        "INSERT INTO photos_v7 SELECT id,filepath,filename,filesize,mtime FROM photos;"
        "DROP TABLE photos; ALTER TABLE photos_v7 RENAME TO photos;") + kPhotoAlbumSchema;
    struct Migration { int to; const char *sql; };
    static const Migration kMigrations[] = {
        // v2: tag WHY a file is excluded — the duplicate-formats
        // resolver marks its losers 'dup-format' so a preference flip
        // can un-exclude exactly those and nothing the user deleted.
        { 2, "ALTER TABLE excluded_files ADD COLUMN reason TEXT DEFAULT ''" },
        // v3: picking art must not freeze an unidentified item's metadata.
        // Existing custom cache filenames let us retain earlier art pins.
        { 3, "ALTER TABLE videos ADD COLUMN custom_poster INTEGER DEFAULT 0;"
             "UPDATE videos SET custom_poster=1 WHERE tmdb_poster GLOB '*/custom_*.jpg';"
             "UPDATE videos SET user_edited=0 WHERE custom_poster=1 AND COALESCE(tmdb_id,0)=0;" },
        // v4: unresolved imports occupy the same ordered row that will
        // hold the track. Neither playlist nor occurrence IDs are reused.
        { 4, "CREATE TABLE playlists_v4 (id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL);"
             "INSERT INTO playlists_v4 SELECT id,name FROM playlists;"
             "CREATE TABLE playlist_tracks_v4 (id INTEGER PRIMARY KEY AUTOINCREMENT,"
             "playlist_id INTEGER NOT NULL REFERENCES playlists_v4(id) ON DELETE CASCADE,"
             "track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE,position INTEGER NOT NULL,"
             "pending_artist TEXT NOT NULL DEFAULT '',pending_album TEXT NOT NULL DEFAULT '',"
             "pending_title TEXT NOT NULL DEFAULT '');"
             "INSERT INTO playlist_tracks_v4(id,playlist_id,track_id,position)"
             " SELECT pt.id,pt.playlist_id,pt.track_id,pt.position FROM playlist_tracks pt"
             " JOIN playlists p ON p.id=pt.playlist_id JOIN tracks t ON t.id=pt.track_id;"
             "DROP TABLE playlist_tracks;DROP TABLE playlists;"
             "ALTER TABLE playlists_v4 RENAME TO playlists;"
             "ALTER TABLE playlist_tracks_v4 RENAME TO playlist_tracks;"
             "CREATE INDEX idx_plt ON playlist_tracks(playlist_id,position);" },
        // v5: shared persistent identity/artwork overrides for collections.
        { 5, "CREATE TABLE collection_customizations (kind TEXT NOT NULL,item_key TEXT NOT NULL,"
             "identity_json TEXT NOT NULL DEFAULT '{}',art_path TEXT NOT NULL DEFAULT '',"
             "PRIMARY KEY(kind,item_key));" },
        // v6: a fingerprint is committed only with a successful current-parser
        // probe. Version0 schedules existing rows for one safe repair scan.
        { 6, "ALTER TABLE tracks ADD COLUMN discnumber INTEGER DEFAULT 0;"
             "ALTER TABLE tracks ADD COLUMN probe_mtime_ns INTEGER DEFAULT 0;"
             "ALTER TABLE tracks ADD COLUMN probe_ctime_ns INTEGER DEFAULT 0;"
             "ALTER TABLE tracks ADD COLUMN probe_version INTEGER DEFAULT 0;"
             "CREATE TABLE music_probe_failures (filepath TEXT PRIMARY KEY,"
             "error TEXT NOT NULL,last_attempt_at INTEGER NOT NULL);" },
        { 7, photoAlbumMigration.constData() },
        // v8: unresolved playlist slots retain the music identity needed
        // to distinguish same-title songs on different discs/tracks.
        { 8, "ALTER TABLE playlist_tracks ADD COLUMN pending_disc_number INTEGER NOT NULL DEFAULT 0;"
             "ALTER TABLE playlist_tracks ADD COLUMN pending_track_number INTEGER NOT NULL DEFAULT 0;" },
        { 9, "ALTER TABLE tracks ADD COLUMN audio_fingerprint TEXT NOT NULL DEFAULT '';" },
        { 0, nullptr }
    };
    int userVersion = 0;
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(m_db, "PRAGMA user_version", -1, &st,
                               nullptr) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW)
            userVersion = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (userVersion == 0) {
        Stmt tables(m_db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='videos'");
        if (tables.ok() && tables.step() == SQLITE_ROW) {
            bool hasArtPin = false, hasExclusionReason = false;
            Stmt videoColumns(m_db, "PRAGMA table_info(videos)");
            while (videoColumns.ok() && videoColumns.step() == SQLITE_ROW)
                hasArtPin |= videoColumns.colText(1) == QLatin1String("custom_poster");
            Stmt exclusionColumns(m_db, "PRAGMA table_info(excluded_files)");
            while (exclusionColumns.ok() && exclusionColumns.step() == SQLITE_ROW)
                hasExclusionReason |= exclusionColumns.colText(1) == QLatin1String("reason");
            userVersion = hasArtPin ? 3 : hasExclusionReason ? 2 : 1;
            Stmt playlistColumns(m_db, "PRAGMA table_info(playlist_tracks)");
            while (playlistColumns.ok() && playlistColumns.step() == SQLITE_ROW)
                if (playlistColumns.colText(1) == QLatin1String("pending_title")) userVersion = 4;
            Stmt collections(m_db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='collection_customizations'");
            if (userVersion == 4 && collections.ok() && collections.step() == SQLITE_ROW) userVersion = 5;
            Stmt trackColumns(m_db, "PRAGMA table_info(tracks)");
            while (trackColumns.ok() && trackColumns.step() == SQLITE_ROW)
                if (userVersion == 5 && trackColumns.colText(1) == QLatin1String("probe_version")) userVersion = 6;
            Stmt photoAlbums(m_db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='photo_albums'");
            if (userVersion == 6 && photoAlbums.ok() && photoAlbums.step() == SQLITE_ROW) userVersion = 7;
            Stmt pendingColumns(m_db, "PRAGMA table_info(playlist_tracks)");
            while (pendingColumns.ok() && pendingColumns.step() == SQLITE_ROW)
                if (userVersion == 7 && pendingColumns.colText(1) == QLatin1String("pending_track_number")) userVersion = 8;
            if (userVersion == 8) {
                Stmt fingerprintColumns(m_db, "PRAGMA table_info(tracks)");
                while (fingerprintColumns.ok() && fingerprintColumns.step() == SQLITE_ROW)
                    if (fingerprintColumns.colText(1) == QLatin1String("audio_fingerprint")) userVersion = 9;
            }
        }
    }
    if (userVersion > kSchemaVersion) {
        m_lastError = QStringLiteral(
            "library.db was written by a NEWER Zuuned (schema v%1, this "
            "app speaks v%2) — refusing to touch it. Update the app.")
            .arg(userVersion).arg(kSchemaVersion);
        fprintf(stderr, "[librarydb] %s\n", qPrintable(m_lastError));
        close();
        return false;
    }
    if (userVersion < kSchemaVersion && userVersion > 0) {
        // A3: a real upgrade of a real library — snapshot first.
        const QString backup = dbPath
            + QStringLiteral(".v%1-backup").arg(userVersion);
        if (!QFile::exists(backup)) {
            // A file copy omits committed pages still in the WAL. SQLite's
            // backup API snapshots the actual library, including those pages.
            QTemporaryFile snapshot(backup + QStringLiteral(".XXXXXX"));
            sqlite3 *destination = nullptr;
            bool saved = snapshot.open();
            const QString temporaryPath = snapshot.fileName();
            snapshot.close();
            if (saved) saved = sqlite3_open(temporaryPath.toUtf8().constData(), &destination) == SQLITE_OK;
            if (saved) {
                sqlite3_backup *copy = sqlite3_backup_init(destination, "main", m_db, "main");
                saved = copy && sqlite3_backup_step(copy, -1) == SQLITE_DONE;
                if (copy) saved = sqlite3_backup_finish(copy) == SQLITE_OK && saved;
            }
            if (destination) sqlite3_close(destination);
            saved = saved && QFile::rename(temporaryPath, backup);
            if (!saved) {
                m_lastError = QStringLiteral("Could not snapshot the library before migration.");
                close();
                return false;
            }
            fprintf(stderr, "[librarydb] pre-migration backup: %s\n", qPrintable(backup));
        }
        for (const Migration &m : kMigrations) {
            if (!m.sql || m.to <= userVersion)
                continue;
            const QByteArray stamp = QByteArray("PRAGMA user_version=") + QByteArray::number(m.to);
            if (!exec("BEGIN") || !exec(m.sql) || !exec(stamp.constData()) || !exec("COMMIT")) {
                exec("ROLLBACK");
                fprintf(stderr, "[librarydb] migration to v%d FAILED: %s\n",
                        m.to, qPrintable(m_lastError));
                close();
                return false;
            }
            fprintf(stderr, "[librarydb] migrated schema to v%d\n", m.to);
        }
    }

    if (!exec(kSchema) || !exec(kPhotoAlbumSchema))
        return false;
    {
        const QByteArray stamp =
            QByteArray("PRAGMA user_version=")
            + QByteArray::number(kSchemaVersion) + ";";
        exec(stamp.constData());
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    return true;
}

void LibraryDb::close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool LibraryDb::exec(const char *sql) {
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        m_lastError = err ? QString::fromUtf8(err) : QStringLiteral("exec failed");
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

// ── Tracks ──

QVector<LibTrack> LibraryDb::allTracks() {
    QVector<LibTrack> out;
    if (!m_db) return out;
    Stmt s(m_db,
           "SELECT id, filepath, title, artist, albumartist, album, genre,"
           " duration, filesize, tracknumber, year, mtime, user_edited,"
           " last_position_ms, discnumber, probe_mtime_ns, probe_ctime_ns, probe_version, audio_fingerprint FROM tracks"
           " ORDER BY COALESCE(NULLIF(albumartist,''), artist) COLLATE NOCASE,"
           " album COLLATE NOCASE, CASE WHEN discnumber>0 THEN discnumber ELSE 1 END,"
           " tracknumber, title COLLATE NOCASE, filepath");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW) {
        LibTrack t;
        t.id = s.colInt64(0);
        t.filepath = s.colText(1);
        t.title = s.colText(2);
        t.artist = s.colText(3);
        t.albumartist = s.colText(4);
        t.album = s.colText(5);
        t.genre = s.colText(6);
        t.durationMs = s.colInt(7);
        t.filesize = s.colInt64(8);
        t.trackNumber = s.colInt(9);
        t.year = s.colInt(10);
        t.mtime = s.colInt64(11);
        t.userEdited = s.colInt(12) != 0;
        t.lastPositionMs = s.colInt(13);
        t.discNumber = s.colInt(14);
        t.probeMtimeNs = s.colInt64(15);
        t.probeCtimeNs = s.colInt64(16);
        t.probeVersion = s.colInt(17);
        t.audioFingerprint = s.colText(18);
        out.append(t);
    }
    return out;
}

bool LibraryDb::upsertTrack(const LibTrack &incoming) {
    if (!m_db) return false;
    LibTrack t = incoming;
    if (t.probeVersion > 0) {
        Stmt old(m_db, "SELECT artist,albumartist,album FROM tracks WHERE filepath=?");
        if (!old.ok()) return false;
        old.bindText(1, t.filepath);
        if (old.step() == SQLITE_ROW) {
            const QString oldArtist = old.colText(0), oldAlbum = old.colText(2);
            const QString owner = old.colText(1).isEmpty() ? oldArtist : old.colText(1);
            const auto key = [](const QString &name) {
                return QString::fromLatin1(QCryptographicHash::hash(name.toLower().toUtf8(), QCryptographicHash::Md5).toHex());
            };
            const auto hasChoice = [this](const QString &kind, const QString &identity) {
                const auto choice = collectionCustomization(kind, identity);
                auto metadataIdentity = choice.value(QStringLiteral("identity")).toMap();
                // Discovery state and artwork-only provider matches describe
                // images, not user choices about the collection's metadata.
                // Their persistence must not prevent a later file-tag repair.
                metadataIdentity.remove(QStringLiteral("_artworkDiscovery"));
                metadataIdentity.remove(QStringLiteral("_artworkMatch"));
                metadataIdentity.remove(QStringLiteral("_artworkChoiceRevision"));
                return !choice.value(QStringLiteral("artPath")).toString().isEmpty()
                    || !metadataIdentity.isEmpty();
            };
            // Re-probing is automatic. Keep collection identities attached to
            // pinned artwork/online choices; a user can rename them in Sleeve.
            if (hasChoice(QStringLiteral("album"), key(owner + QLatin1Char('\n') + oldAlbum))) {
                t.albumartist = owner;
                t.album = oldAlbum;
            }
            if (hasChoice(QStringLiteral("artist"), key(owner))) {
                t.albumartist = owner;
                if (oldArtist == owner) t.artist = oldArtist;
            }
        }
    }
    // user_edited rows keep their metadata — only file stats update
    // (mac semantics).
    Stmt s(m_db,
        "INSERT INTO tracks (filepath, title, artist, albumartist, album,"
        " genre, duration, filesize, tracknumber, year, mtime, discnumber,"
        " probe_mtime_ns, probe_ctime_ns, probe_version)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(filepath) DO UPDATE SET"
        " title=CASE WHEN tracks.user_edited=1 THEN tracks.title ELSE excluded.title END,"
        " artist=CASE WHEN tracks.user_edited=1 THEN tracks.artist ELSE excluded.artist END,"
        " albumartist=CASE WHEN tracks.user_edited=1 THEN tracks.albumartist ELSE excluded.albumartist END,"
        " album=CASE WHEN tracks.user_edited=1 THEN tracks.album ELSE excluded.album END,"
        " genre=CASE WHEN tracks.user_edited=1 THEN tracks.genre ELSE excluded.genre END,"
        " tracknumber=CASE WHEN tracks.user_edited=1 THEN tracks.tracknumber ELSE excluded.tracknumber END,"
        " discnumber=CASE WHEN tracks.user_edited=1 AND tracks.discnumber>0"
        " THEN tracks.discnumber ELSE excluded.discnumber END,"
        " year=CASE WHEN tracks.user_edited=1 THEN tracks.year ELSE excluded.year END,"
        " duration=excluded.duration,"
        " filesize=excluded.filesize,"
        " mtime=excluded.mtime,"
        " probe_mtime_ns=excluded.probe_mtime_ns,"
        " probe_ctime_ns=excluded.probe_ctime_ns,"
        " audio_fingerprint=CASE WHEN tracks.filesize=excluded.filesize AND tracks.probe_mtime_ns=excluded.probe_mtime_ns"
        " AND tracks.probe_ctime_ns=excluded.probe_ctime_ns THEN tracks.audio_fingerprint ELSE '' END,"
        " probe_version=excluded.probe_version");
    if (!s.ok()) { m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db)); return false; }
    s.bindText(1, t.filepath);
    s.bindText(2, t.title);
    s.bindText(3, t.artist);
    s.bindText(4, t.albumartist);
    s.bindText(5, t.album);
    s.bindText(6, t.genre);
    s.bindInt(7, t.durationMs);
    s.bindInt64(8, t.filesize);
    s.bindInt(9, t.trackNumber);
    s.bindInt(10, t.year);
    s.bindInt64(11, t.mtime);
    s.bindInt(12, t.discNumber);
    s.bindInt64(13, t.probeMtimeNs);
    s.bindInt64(14, t.probeCtimeNs);
    s.bindInt(15, t.probeVersion);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::updateAudioFingerprint(const LibTrack &expected, const QString &fingerprint) {
    if (!m_db) return false;
    Stmt s(m_db, "UPDATE tracks SET audio_fingerprint=? WHERE id=? AND filepath=?"
                " AND filesize=? AND probe_mtime_ns=? AND probe_ctime_ns=?");
    if (!s.ok()) return false;
    s.bindText(1, fingerprint);
    s.bindInt64(2, expected.id);
    s.bindText(3, expected.filepath);
    s.bindInt64(4, expected.filesize);
    s.bindInt64(5, expected.probeMtimeNs);
    s.bindInt64(6, expected.probeCtimeNs);
    return s.step() == SQLITE_DONE && sqlite3_changes(m_db) == 1;
}

bool LibraryDb::updateTrackMetadata(qint64 id, const LibTrack &t, bool markUserEdited) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE tracks SET title=?, artist=?, albumartist=?, album=?, genre=?,"
        " tracknumber=?, year=?, discnumber=?, user_edited=CASE WHEN ?=1 THEN 1 ELSE user_edited END"
        " WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, t.title);
    s.bindText(2, t.artist);
    s.bindText(3, t.albumartist);
    s.bindText(4, t.album);
    s.bindText(5, t.genre);
    s.bindInt(6, t.trackNumber);
    s.bindInt(7, t.year);
    s.bindInt(8, t.discNumber);
    s.bindInt(9, markUserEdited ? 1 : 0);
    s.bindInt64(10, id);
    // A scan can delete the row before the UI snapshot refreshes. An
    // UPDATE with no matching row must not authorize writing file tags.
    return s.step() == SQLITE_DONE && sqlite3_changes(m_db) == 1;
}

bool LibraryDb::updateTrackPosition(qint64 id, int lastPositionMs) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE tracks SET last_position_ms=?,"
        " last_played_at=strftime('%s','now') WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt(1, lastPositionMs);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::removeTrack(qint64 id, bool exclude) {
    if (!m_db) return false;
    Stmt failure(m_db, "DELETE FROM music_probe_failures WHERE filepath=(SELECT filepath FROM tracks WHERE id=?)");
    if (failure.ok()) { failure.bindInt64(1, id); failure.step(); }
    if (exclude) {
        Stmt sel(m_db, "SELECT filepath FROM tracks WHERE id=?");
        if (sel.ok()) {
            sel.bindInt64(1, id);
            if (sel.step() == SQLITE_ROW) {
                Stmt ins(m_db,
                    "INSERT OR IGNORE INTO excluded_files (filepath) VALUES (?)");
                if (ins.ok()) {
                    ins.bindText(1, sel.colText(0));
                    ins.step();
                }
            }
        }
    }
    Stmt del(m_db, "DELETE FROM tracks WHERE id=?");
    if (!del.ok()) return false;
    del.bindInt64(1, id);
    return del.step() == SQLITE_DONE;
}

QHash<QString, QPair<qint64, qint64>> LibraryDb::trackPathIndex() {
    QHash<QString, QPair<qint64, qint64>> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT filepath, id, mtime FROM tracks");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW)
        out.insert(s.colText(0), {s.colInt64(1), s.colInt64(2)});
    return out;
}

QHash<QString, TrackProbeState> LibraryDb::trackProbeIndex() {
    QHash<QString, TrackProbeState> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT filepath,id,filesize,probe_mtime_ns,probe_ctime_ns,probe_version FROM tracks");
    while (s.ok() && s.step() == SQLITE_ROW)
        out.insert(s.colText(0), {s.colInt64(1), s.colInt64(2), s.colInt64(3), s.colInt64(4), s.colInt(5)});
    return out;
}

bool LibraryDb::recordMusicProbeFailure(const QString &filepath, const QString &error, bool *copyInvalidated) {
    if (copyInvalidated) *copyInvalidated = false;
    if (!m_db) return false;
    // A changed or unreadable file no longer has trustworthy cached copy evidence.
    Stmt invalidate(m_db, "UPDATE tracks SET audio_fingerprint='' WHERE filepath=? AND audio_fingerprint<>''");
    if (!invalidate.ok()) return false;
    invalidate.bindText(1, filepath);
    if (invalidate.step() != SQLITE_DONE) return false;
    if (copyInvalidated) *copyInvalidated = sqlite3_changes(m_db) > 0;
    Stmt s(m_db, "INSERT INTO music_probe_failures(filepath,error,last_attempt_at)"
                " VALUES(?,?,strftime('%s','now')) ON CONFLICT(filepath) DO UPDATE SET"
                " error=excluded.error,last_attempt_at=excluded.last_attempt_at");
    if (!s.ok()) return false;
    s.bindText(1, filepath); s.bindText(2, error);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::clearMusicProbeFailure(const QString &filepath) {
    if (!m_db) return false;
    Stmt s(m_db, "DELETE FROM music_probe_failures WHERE filepath=?");
    if (!s.ok()) return false;
    s.bindText(1, filepath);
    return s.step() == SQLITE_DONE;
}

QVariantList LibraryDb::musicProbeFailures() {
    QVariantList out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT filepath,error,last_attempt_at FROM music_probe_failures ORDER BY filepath");
    while (s.ok() && s.step() == SQLITE_ROW)
        out.append(QVariantMap{{"filepath", s.colText(0)}, {"error", s.colText(1)},
                              {"lastAttemptAt", s.colInt64(2)}});
    return out;
}

// ── Videos ──

QVector<LibVideo> LibraryDb::allVideos() {
    QVector<LibVideo> out;
    if (!m_db) return out;
    Stmt s(m_db,
        "SELECT id, filepath, filename, filesize, mtime, duration, width, height,"
        " description, category, series, season, episode, episode_title,"
        " tmdb_id, tmdb_title, tmdb_poster, tmdb_cast, tmdb_director,"
        " tmdb_rating, tmdb_genres, tmdb_year, tmdb_cached,"
        " last_position_ms, last_played_at, watched, episode_still,"
        " episode_air_date, episode_rating, episode_runtime, episode_tmdb_id,"
        " COALESCE(user_edited,0), COALESCE(lookup_attempts,0),"
        " COALESCE(last_lookup_at,0), COALESCE(probed_at,0), COALESCE(custom_poster,0)"
        " FROM videos"
        " ORDER BY series COLLATE NOCASE, season, episode, filename COLLATE NOCASE");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW) {
        LibVideo v;
        v.id = s.colInt64(0);
        v.filepath = s.colText(1);
        v.filename = s.colText(2);
        v.filesize = s.colInt64(3);
        v.mtime = s.colInt64(4);
        v.durationMs = s.colInt(5);
        v.width = s.colInt(6);
        v.height = s.colInt(7);
        v.description = s.colText(8);
        v.category = s.colText(9);
        v.series = s.colText(10);
        v.season = s.colInt(11);
        v.episode = s.colInt(12);
        v.episodeTitle = s.colText(13);
        v.tmdbId = s.colInt(14);
        v.tmdbTitle = s.colText(15);
        v.tmdbPoster = s.colText(16);
        v.tmdbCast = s.colText(17);
        v.tmdbDirector = s.colText(18);
        v.tmdbRating = sqlite3_column_double(s.get(), 19);
        v.tmdbGenres = s.colText(20);
        v.tmdbYear = s.colText(21);
        v.tmdbCached = s.colInt(22) != 0;
        v.lastPositionMs = s.colInt(23);
        v.lastPlayedAt = s.colInt64(24);
        v.watched = s.colInt(25) != 0;
        v.episodeStill = s.colText(26);
        v.episodeAirDate = s.colText(27);
        v.episodeRating = sqlite3_column_double(s.get(), 28);
        v.episodeRuntime = s.colInt(29);
        v.episodeTmdbId = s.colInt(30);
        v.userEdited = s.colInt(31) != 0;
        v.lookupAttempts = s.colInt(32);
        v.lastLookupAt = s.colInt64(33);
        v.probedAt = s.colInt64(34);
        v.customPoster = s.colInt(35) != 0;
        out.append(v);
    }
    return out;
}

QHash<QString, QPair<qint64, qint64>> LibraryDb::videoPathIndex() {
    QHash<QString, QPair<qint64, qint64>> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT filepath, id, mtime FROM videos");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW)
        out.insert(s.colText(0), {s.colInt64(1), s.colInt64(2)});
    return out;
}

QHash<qint64, qint64> LibraryDb::videoSizeIndex() {
    QHash<qint64, qint64> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT id, filesize FROM videos");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW)
        out.insert(s.colInt64(0), s.colInt64(1));
    return out;
}

bool LibraryDb::insertVideo(const LibVideo &v) {
    if (!m_db) return false;
    Stmt s(m_db,
        "INSERT INTO videos (filepath, filename, filesize, mtime, duration,"
        " width, height, series, season, episode, category)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    if (!s.ok()) { m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db)); return false; }
    s.bindText(1, v.filepath);
    s.bindText(2, v.filename);
    s.bindInt64(3, v.filesize);
    s.bindInt64(4, v.mtime);
    s.bindInt(5, v.durationMs);
    s.bindInt(6, v.width);
    s.bindInt(7, v.height);
    s.bindText(8, v.series);
    s.bindInt(9, v.season);
    s.bindInt(10, v.episode);
    s.bindText(11, v.category);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::touchVideoChanged(qint64 id, qint64 filesize, qint64 mtime) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET filesize=?, mtime=?, duration=0, width=0,"
        " height=0, probed_at=0 WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt64(1, filesize);
    s.bindInt64(2, mtime);
    s.bindInt64(3, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::touchVideoMtime(qint64 id, qint64 mtime) {
    if (!m_db) return false;
    Stmt s(m_db, "UPDATE videos SET mtime=? WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt64(1, mtime);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::updateVideoParse(qint64 id, const QString &series, int season,
                                 int episode, const QString &category) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET series=?, season=?, episode=?, category=?"
        " WHERE id=? AND COALESCE(user_edited,0)=0");
    if (!s.ok()) return false;
    s.bindText(1, series);
    s.bindInt(2, season);
    s.bindInt(3, episode);
    s.bindText(4, category);
    s.bindInt64(5, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::updateVideoProbe(qint64 id, int durationMs, int width, int height) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET duration=?, width=?, height=?,"
        " probed_at=strftime('%s','now') WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt(1, durationMs);
    s.bindInt(2, width);
    s.bindInt(3, height);
    s.bindInt64(4, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::updateVideoTmdb(qint64 id, int tmdbId, const QString &title,
                                const QString &poster, const QString &cast,
                                const QString &director, double rating,
                                const QString &genres, const QString &year,
                                const QString &category, const QString &description) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET tmdb_id=?, tmdb_title=?, tmdb_poster=?, tmdb_cast=?,"
        " tmdb_director=?, tmdb_rating=?, tmdb_genres=?, tmdb_year=?,"
        " tmdb_cached=1, category=?, description=?,"
        " lookup_attempts=lookup_attempts+1, last_lookup_at=strftime('%s','now')"
        " WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt(1, tmdbId);
    s.bindText(2, title);
    s.bindText(3, poster);
    s.bindText(4, cast);
    s.bindText(5, director);
    sqlite3_bind_double(s.get(), 6, rating);
    s.bindText(7, genres);
    s.bindText(8, year);
    s.bindText(9, category);
    s.bindText(10, description);
    s.bindInt64(11, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::updateVideoSeries(qint64 id, const QString &series, int season,
                                  int episode, const QString &episodeTitle) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET series=?, season=?, episode=?, episode_title=?"
        " WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, series);
    s.bindInt(2, season);
    s.bindInt(3, episode);
    s.bindText(4, episodeTitle);
    s.bindInt64(5, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::setManualVideoIdentity(qint64 id, const QString &category,
                                       const QString &title, const QString &year,
                                       const QString &genres,
                                       const QString &overview,
                                       const QString &series, int season,
                                       int episode, const QString &episodeTitle) {
    if (!m_db) return false;
    // tmdb_id = -1 is the MANUAL sentinel: not 0 (so it's never "Needs
    // Match") and not a real id (so nothing tries to re-fetch it).
    // tmdb_cached=1 keeps it out of the lookup queue; user_edited=1
    // makes it stick across rescans.
    Stmt s(m_db,
        "UPDATE videos SET tmdb_id=-1, tmdb_cached=1, user_edited=1,"
        " category=?, tmdb_title=?, tmdb_year=?, tmdb_genres=?,"
        " description=?, series=?, season=?, episode=?, episode_title=?"
        " WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, category);
    s.bindText(2, title);
    s.bindText(3, year);
    s.bindText(4, genres);
    s.bindText(5, overview);
    s.bindText(6, series);
    s.bindInt(7, season);
    s.bindInt(8, episode);
    s.bindText(9, episodeTitle);
    s.bindInt64(10, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::setVideoCustomPoster(qint64 id, const QString &localPath) {
    if (!m_db) return false;
    // A hand-picked cover is sticky like a manual match.
    Stmt s(m_db,
        "UPDATE videos SET tmdb_poster=?, custom_poster=1 WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, localPath);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::updateVideoCustomization(const LibVideo &v) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET category=?, series=?, season=?, episode=?,"
        " episode_title=?, tmdb_id=?, tmdb_title=?, tmdb_poster=?,"
        " tmdb_cast=?, tmdb_director=?, tmdb_rating=?, tmdb_genres=?,"
        " tmdb_year=?, tmdb_cached=?, description=?, user_edited=?,"
        " lookup_attempts=?, last_lookup_at=?, custom_poster=? WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, v.category); s.bindText(2, v.series);
    s.bindInt(3, v.season); s.bindInt(4, v.episode);
    s.bindText(5, v.episodeTitle); s.bindInt(6, v.tmdbId);
    s.bindText(7, v.tmdbTitle); s.bindText(8, v.tmdbPoster);
    s.bindText(9, v.tmdbCast); s.bindText(10, v.tmdbDirector);
    sqlite3_bind_double(s.get(), 11, v.tmdbRating);
    s.bindText(12, v.tmdbGenres); s.bindText(13, v.tmdbYear);
    s.bindInt(14, v.tmdbCached); s.bindText(15, v.description);
    s.bindInt(16, v.userEdited); s.bindInt(17, v.lookupAttempts);
    s.bindInt64(18, v.lastLookupAt); s.bindInt(19, v.customPoster); s.bindInt64(20, v.id);
    if (s.step() != SQLITE_DONE || sqlite3_changes(m_db) != 1) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    return true;
}

bool LibraryDb::updateVideoCategory(qint64 id, const QString &category) {
    if (!m_db) return false;
    Stmt s(m_db, "UPDATE videos SET category=? WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, category);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::setVideosWatched(const QVector<qint64> &ids, bool watched) {
    if (!m_db) return false;
    Stmt s(m_db, "UPDATE videos SET watched=? WHERE id=?");
    if (!s.ok()) return false;
    exec("BEGIN;");
    for (qint64 id : ids) {
        sqlite3_reset(s.get());
        s.bindInt(1, watched ? 1 : 0);
        s.bindInt64(2, id);
        s.step();
    }
    return exec("COMMIT;");
}

bool LibraryDb::setVideosUserEdited(const QVector<qint64> &ids) {
    if (!m_db) return false;
    Stmt s(m_db, "UPDATE videos SET user_edited=1 WHERE id=?");
    if (!s.ok()) return false;
    exec("BEGIN;");
    for (qint64 id : ids) {
        sqlite3_reset(s.get());
        s.bindInt64(1, id);
        s.step();
    }
    return exec("COMMIT;");
}

bool LibraryDb::requeueVideosForMatch(const QVector<qint64> &ids) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET tmdb_cached=0, tmdb_id=0, user_edited=0, lookup_attempts=0,"
        " last_lookup_at=0 WHERE id=?");
    if (!s.ok()) return false;
    exec("BEGIN;");
    for (qint64 id : ids) {
        sqlite3_reset(s.get());
        s.bindInt64(1, id);
        s.step();
    }
    return exec("COMMIT;");
}

bool LibraryDb::updateVideoPosition(qint64 id, int lastPositionMs) {
    if (!m_db) return false;
    Stmt s(m_db,
        "UPDATE videos SET last_position_ms=?,"
        " last_played_at=strftime('%s','now') WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt(1, lastPositionMs);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::removeVideo(qint64 id, bool exclude) {
    if (!m_db) return false;
    if (exclude) {
        Stmt sel(m_db, "SELECT filepath FROM videos WHERE id=?");
        if (sel.ok()) {
            sel.bindInt64(1, id);
            if (sel.step() == SQLITE_ROW) {
                Stmt ins(m_db,
                    "INSERT OR IGNORE INTO excluded_files (filepath) VALUES (?)");
                if (ins.ok()) {
                    ins.bindText(1, sel.colText(0));
                    ins.step();
                }
            }
        }
    }
    Stmt del(m_db, "DELETE FROM videos WHERE id=?");
    if (!del.ok()) return false;
    del.bindInt64(1, id);
    return del.step() == SQLITE_DONE;
}

// ── Playlists (UX-2, library-first) ──

namespace {
// SAVEPOINT composes with callers' transactions and nested playlist writes.
class PlaylistWrite {
public:
    explicit PlaylistWrite(sqlite3 *db) : m_db(db) {
        m_open = db && sqlite3_exec(db, "SAVEPOINT playlist_write", nullptr, nullptr, nullptr) == SQLITE_OK;
    }
    ~PlaylistWrite() {
        if (m_open) sqlite3_exec(m_db, "ROLLBACK TO playlist_write; RELEASE playlist_write", nullptr, nullptr, nullptr);
    }
    bool ok() const { return m_open; }
    bool finish() {
        if (!m_open || sqlite3_exec(m_db, "RELEASE playlist_write", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        m_open = false; return true;
    }
private:
    sqlite3 *m_db = nullptr;
    bool m_open = false;
};

bool playlistExists(sqlite3 *db, qint64 id) {
    if (!db) return false;
    Stmt row(db, "SELECT 1 FROM playlists WHERE id=?");
    if (!row.ok()) return false;
    row.bindInt64(1, id);
    return row.step() == SQLITE_ROW;
}

LibraryDb::PlaylistEntry readPlaylistEntry(Stmt &row) {
    LibraryDb::PlaylistEntry result;
    result.id = row.colInt64(0); result.playlistId = row.colInt64(1);
    result.trackId = sqlite3_column_type(row.get(), 2) == SQLITE_NULL ? -1 : row.colInt64(2);
    result.position = row.colInt(3); result.artist = row.colText(4);
    result.album = row.colText(5); result.title = row.colText(6);
    result.discNumber = row.colInt(7); result.trackNumber = row.colInt(8);
    return result;
}
}

qint64 LibraryDb::createPlaylist(const QString &name,
                                 const QVector<qint64> &trackIds) {
    QVector<PlaylistEntry> entries;
    for (qint64 id : trackIds) { PlaylistEntry entry; entry.trackId = id; entries.append(entry); }
    return createPlaylistEntries(name, entries);
}

qint64 LibraryDb::createPlaylistEntries(const QString &name, const QVector<PlaylistEntry> &entries) {
    if (!m_db || name.trimmed().isEmpty()) return -1;
    PlaylistWrite write(m_db);
    if (!write.ok()) return -1;
    Stmt ins(m_db, "INSERT INTO playlists (name) VALUES (?)");
    if (!ins.ok()) return -1;
    ins.bindText(1, name.trimmed());
    if (ins.step() != SQLITE_DONE) return -1;
    const qint64 id = sqlite3_last_insert_rowid(m_db);
    if (!appendPlaylistEntries(id, entries)) return -1;
    return write.finish() ? id : -1;
}

QVector<QPair<qint64, QString>> LibraryDb::allPlaylists(QVector<int> *counts, QVector<int> *pendingCounts) {
    QVector<QPair<qint64, QString>> out;
    if (counts) counts->clear();
    if (pendingCounts) pendingCounts->clear();
    if (!m_db) return out;
    Stmt s(m_db,
        "SELECT p.id, p.name, COUNT(pt.track_id),"
        " SUM(CASE WHEN pt.id IS NOT NULL AND pt.track_id IS NULL THEN 1 ELSE 0 END) FROM playlists p"
        " LEFT JOIN playlist_tracks pt ON pt.playlist_id = p.id"
        " GROUP BY p.id ORDER BY p.name COLLATE NOCASE");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW) {
        out.append({s.colInt64(0), s.colText(1)});
        if (counts) counts->append(s.colInt(2));
        if (pendingCounts) pendingCounts->append(s.colInt(3));
    }
    return out;
}

QVector<qint64> LibraryDb::playlistTrackIds(qint64 playlistId) {
    QVector<qint64> out;
    if (!m_db) return out;
    Stmt s(m_db,
        "SELECT track_id FROM playlist_tracks WHERE playlist_id=? AND track_id IS NOT NULL"
        " ORDER BY position,id");
    if (!s.ok()) return out;
    s.bindInt64(1, playlistId);
    while (s.step() == SQLITE_ROW)
        out.append(s.colInt64(0));
    return out;
}

bool LibraryDb::renamePlaylist(qint64 id, const QString &name) {
    if (!m_db || name.isEmpty()) return false;
    Stmt s(m_db, "UPDATE playlists SET name=? WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, name);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE && sqlite3_changes(m_db) == 1;
}

bool LibraryDb::deletePlaylist(qint64 id) {
    if (!playlistExists(m_db, id)) return false;
    PlaylistWrite write(m_db);
    if (!write.ok()) return false;
    // Explicit member delete — CASCADE only fires when foreign_keys=ON
    // was set for THIS connection, and the scanner thread's isn't.
    Stmt del(m_db, "DELETE FROM playlist_tracks WHERE playlist_id=?");
    if (!del.ok()) return false;
    del.bindInt64(1, id);
    if (del.step() != SQLITE_DONE) return false;
    Stmt art(m_db, "DELETE FROM collection_customizations WHERE kind='mixtape' AND item_key=?");
    if (!art.ok()) return false;
    art.bindText(1, QString::number(id));
    if (art.step() != SQLITE_DONE) return false;
    Stmt s(m_db, "DELETE FROM playlists WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt64(1, id);
    return s.step() == SQLITE_DONE && write.finish();
}

bool LibraryDb::setPlaylistTracks(qint64 id, const QVector<qint64> &trackIds) {
    const auto current = playlistEntries(id);
    QHash<qint64, QVector<PlaylistEntry>> occurrences;
    QVector<qint64> baseline;
    for (const auto &entry : current) if (entry.trackId >= 0) {
        occurrences[entry.trackId].append(entry); baseline.append(entry.id);
    }
    QVector<PlaylistEntry> desired;
    for (qint64 trackId : trackIds) {
        auto &available = occurrences[trackId];
        if (!available.isEmpty()) desired.append(available.takeFirst());
        else { PlaylistEntry entry; entry.trackId = trackId; desired.append(entry); }
    }
    return editPlaylistEntries(id, QString(), desired, baseline);
}

bool LibraryDb::appendPlaylistTracks(qint64 id,
                                     const QVector<qint64> &trackIds) {
    QVector<PlaylistEntry> entries;
    for (qint64 trackId : trackIds) { PlaylistEntry entry; entry.trackId = trackId; entries.append(entry); }
    return appendPlaylistEntries(id, entries);
}

QVector<LibraryDb::PlaylistEntry> LibraryDb::playlistEntries(qint64 playlistId) {
    QVector<PlaylistEntry> result;
    if (!m_db) return result;
    Stmt rows(m_db, "SELECT id,playlist_id,track_id,position,pending_artist,pending_album,pending_title,pending_disc_number,pending_track_number"
                    " FROM playlist_tracks WHERE playlist_id=? ORDER BY position,id");
    if (!rows.ok()) return result;
    rows.bindInt64(1, playlistId);
    while (rows.step() == SQLITE_ROW) result.append(readPlaylistEntry(rows));
    return result;
}

QVector<LibraryDb::PlaylistEntry> LibraryDb::pendingPlaylistEntries() {
    QVector<PlaylistEntry> result;
    if (!m_db) return result;
    Stmt rows(m_db, "SELECT id,playlist_id,track_id,position,pending_artist,pending_album,pending_title,pending_disc_number,pending_track_number"
                    " FROM playlist_tracks WHERE track_id IS NULL ORDER BY playlist_id,position,id");
    if (!rows.ok()) return result;
    while (rows.step() == SQLITE_ROW) result.append(readPlaylistEntry(rows));
    return result;
}

bool LibraryDb::appendPlaylistEntries(qint64 id, const QVector<PlaylistEntry> &entries) {
    if (!playlistExists(m_db, id)) return false;
    PlaylistWrite write(m_db);
    if (!write.ok()) return false;
    int pos = 0;
    {
        Stmt mx(m_db,
            "SELECT COALESCE(MAX(position)+1,0) FROM playlist_tracks"
            " WHERE playlist_id=?");
        if (mx.ok()) {
            mx.bindInt64(1, id);
            if (mx.step() == SQLITE_ROW) pos = mx.colInt(0);
        }
    }
    for (const auto &entry : entries) {
        Stmt ins(m_db,
            "INSERT INTO playlist_tracks (playlist_id,track_id,position,pending_artist,pending_album,pending_title,pending_disc_number,pending_track_number)"
            " VALUES (?,?,?,?,?,?,?,?)");
        if (!ins.ok()) return false;
        ins.bindInt64(1, id);
        if (entry.trackId >= 0) ins.bindInt64(2, entry.trackId); else ins.bindNull(2);
        ins.bindInt(3, pos++);
        ins.bindText(4, entry.artist); ins.bindText(5, entry.album); ins.bindText(6, entry.title);
        ins.bindInt(7, qMax(0,entry.discNumber)); ins.bindInt(8, qMax(0,entry.trackNumber));
        if (ins.step() != SQLITE_DONE) return false;
    }
    return write.finish();
}

bool LibraryDb::resolvePlaylistEntry(qint64 entryId, qint64 trackId) {
    if (!m_db || trackId < 0) return false;
    Stmt update(m_db, "UPDATE playlist_tracks SET track_id=?,pending_artist='',pending_album='',pending_title='',pending_disc_number=0,pending_track_number=0"
                      " WHERE id=? AND track_id IS NULL");
    if (!update.ok()) return false;
    update.bindInt64(1, trackId); update.bindInt64(2, entryId);
    return update.step() == SQLITE_DONE && sqlite3_changes(m_db) == 1;
}

bool LibraryDb::editPlaylistEntries(qint64 id, const QString &name,
                                    const QVector<PlaylistEntry> &entries,
                                    const QVector<qint64> &originalEntryIds) {
    if (!playlistExists(m_db, id) || (!name.isNull() && name.trimmed().isEmpty())) return false;
    PlaylistWrite write(m_db);
    if (!write.ok()) return false;
    const auto current = playlistEntries(id);
    QHash<qint64, PlaylistEntry> byId;
    for (const auto &entry : current) byId.insert(entry.id, entry);
    const QSet<qint64> baseline(originalEntryIds.cbegin(), originalEntryIds.cend());
    QSet<qint64> selected;
    QVector<PlaylistEntry> orderedExisting, newBeforeNext;
    QHash<qint64, QVector<PlaylistEntry>> insertBefore;
    for (const auto &entry : entries) {
        if (entry.trackId < 0) return false;
        if (entry.id <= 0) { newBeforeNext.append(entry); continue; }
        if (!baseline.contains(entry.id) || selected.contains(entry.id) || !byId.contains(entry.id)
            || byId.value(entry.id).trackId != entry.trackId) return false;
        selected.insert(entry.id);
        orderedExisting.append(byId.value(entry.id));
        insertBefore.insert(entry.id, newBeforeNext); newBeforeNext.clear();
    }
    QVector<PlaylistEntry> merged;
    int next = 0;
    for (const auto &entry : current) {
        if (!baseline.contains(entry.id)) { merged.append(entry); continue; }
        if (selected.contains(entry.id)) {
            const auto replacement = orderedExisting[next++];
            merged += insertBefore.value(replacement.id);
            merged.append(replacement);
        } else {
            Stmt remove(m_db, "DELETE FROM playlist_tracks WHERE id=? AND playlist_id=?");
            if (!remove.ok()) return false;
            remove.bindInt64(1, entry.id); remove.bindInt64(2, id);
            if (remove.step() != SQLITE_DONE) return false;
        }
    }
    merged += newBeforeNext;
    for (int position = 0; position < merged.size(); ++position) {
        const auto &entry = merged[position];
        if (entry.id > 0) {
            Stmt update(m_db, "UPDATE playlist_tracks SET position=? WHERE id=? AND playlist_id=?");
            if (!update.ok()) return false;
            update.bindInt(1, position); update.bindInt64(2, entry.id); update.bindInt64(3, id);
            if (update.step() != SQLITE_DONE || sqlite3_changes(m_db) != 1) return false;
        } else {
            Stmt insert(m_db, "INSERT INTO playlist_tracks(playlist_id,track_id,position) VALUES(?,?,?)");
            if (!insert.ok()) return false;
            insert.bindInt64(1, id); insert.bindInt64(2, entry.trackId); insert.bindInt(3, position);
            if (insert.step() != SQLITE_DONE) return false;
        }
    }
    if (!name.isNull() && !renamePlaylist(id, name.trimmed())) return false;
    return write.finish();
}

bool LibraryDb::migratePendingPlaylistEntries(const QVector<PlaylistEntry> &entries) {
    if (!m_db) return false;
    PlaylistWrite write(m_db);
    if (!write.ok()) return false;
    Stmt done(m_db, "SELECT 1 FROM sync_state WHERE key='playlist_pending_slots_v1'");
    if (!done.ok()) return false;
    if (done.step() == SQLITE_ROW) return write.finish();
    for (const auto &entry : entries) {
        // Old versions retained notes for deleted playlists. Never create
        // a playlist just to satisfy one of those orphaned notes.
        if (!playlistExists(m_db, entry.playlistId)) continue;
        if (!appendPlaylistEntries(entry.playlistId, {entry})) return false;
    }
    Stmt marker(m_db, "INSERT INTO sync_state(key,value) VALUES('playlist_pending_slots_v1','1')");
    if (!marker.ok() || marker.step() != SQLITE_DONE) return false;
    return write.finish();
}

// ── Photos ──

QVector<LibPhoto> LibraryDb::allPhotos() {
    QVector<LibPhoto> out;
    if (!m_db) return out;
    Stmt s(m_db,
        "SELECT id, filepath, filename, filesize, mtime FROM photos"
        " ORDER BY filepath COLLATE NOCASE");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW) {
        LibPhoto p;
        p.id = s.colInt64(0);
        p.filepath = s.colText(1);
        p.filename = s.colText(2);
        p.filesize = s.colInt64(3);
        p.mtime = s.colInt64(4);
        out.append(p);
    }
    return out;
}

bool LibraryDb::upsertPhoto(const LibPhoto &p) {
    if (!m_db) return false;
    Stmt s(m_db,
        "INSERT INTO photos (filepath, filename, filesize, mtime)"
        " VALUES (?,?,?,?)"
        " ON CONFLICT(filepath) DO UPDATE SET"
        " filename=excluded.filename, filesize=excluded.filesize,"
        " mtime=excluded.mtime");
    if (!s.ok()) return false;
    s.bindText(1, p.filepath);
    s.bindText(2, p.filename);
    s.bindInt64(3, p.filesize);
    s.bindInt64(4, p.mtime);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::removePhoto(qint64 id, bool exclude) {
    if (!m_db) return false;
    if (exclude) {
        Stmt sel(m_db, "SELECT filepath FROM photos WHERE id=?");
        if (sel.ok()) {
            sel.bindInt64(1, id);
            if (sel.step() == SQLITE_ROW) {
                Stmt ins(m_db,
                    "INSERT OR IGNORE INTO excluded_files (filepath) VALUES (?)");
                if (ins.ok()) {
                    ins.bindText(1, sel.colText(0));
                    ins.step();
                }
            }
        }
    }
    Stmt del(m_db, "DELETE FROM photos WHERE id=?");
    if (!del.ok()) return false;
    del.bindInt64(1, id);
    return del.step() == SQLITE_DONE;
}

QHash<QString, QPair<qint64, qint64>> LibraryDb::photoPathIndex() {
    QHash<QString, QPair<qint64, qint64>> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT filepath, id, mtime FROM photos");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW)
        out.insert(s.colText(0), {s.colInt64(1), s.colInt64(2)});
    return out;
}

// ── Watch folders ──

QVector<WatchFolder> LibraryDb::watchFolders() {
    QVector<WatchFolder> out;
    if (!m_db) return out;
    Stmt s(m_db, "SELECT id, path, type FROM watch_folders ORDER BY path");
    if (!s.ok()) return out;
    while (s.step() == SQLITE_ROW) {
        WatchFolder f;
        f.id = s.colInt64(0);
        f.path = s.colText(1);
        f.type = s.colText(2);
        out.append(f);
    }
    return out;
}

bool LibraryDb::addWatchFolder(const QString &path, const QString &type) {
    if (!m_db) return false;
    Stmt s(m_db,
        "INSERT INTO watch_folders (path, type) VALUES (?,?)"
        " ON CONFLICT(path) DO UPDATE SET type=excluded.type");
    if (!s.ok()) return false;
    s.bindText(1, path);
    s.bindText(2, type);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::removeWatchFolder(qint64 id) {
    if (!m_db) return false;
    Stmt s(m_db, "DELETE FROM watch_folders WHERE id=?");
    if (!s.ok()) return false;
    s.bindInt64(1, id);
    return s.step() == SQLITE_DONE;
}

int LibraryDb::purgeFolder(const QString &path) {
    if (!m_db || path.isEmpty()) return 0;
    // Match the folder itself and anything beneath "<path>/". Exact
    // prefix via substr so no LIKE/GLOB metacharacters in the path
    // (underscore, [ ]) can over-match.
    const QByteArray prefix = (path + QLatin1Char('/')).toUtf8();
    const int plen = prefix.size();
    int removed = 0;
    for (const char *tbl : {"tracks", "videos", "photos", "music_probe_failures"}) {
        const QByteArray sql =
            QByteArray("DELETE FROM ") + tbl
            + " WHERE filepath=? OR substr(filepath,1,?)=?";
        Stmt del(m_db, sql.constData());
        if (!del.ok())
            continue;
        del.bindText(1, path);
        del.bindInt64(2, plen);
        del.bindText(3, QString::fromUtf8(prefix));
        if (del.step() == SQLITE_DONE && QByteArray(tbl) != "music_probe_failures")
            removed += sqlite3_changes(m_db);
    }
    return removed;
}

void LibraryDb::countsUnder(const QString &path, int *tracks, int *videos,
                            int *photos) {
    *tracks = *videos = *photos = 0;
    if (!m_db || path.isEmpty()) return;
    // Exact prefix via substr, as in purgeFolder — no LIKE/GLOB
    // metacharacter surprises from _ and [ ] in paths.
    const QByteArray prefix = (path + QLatin1Char('/')).toUtf8();
    const int plen = prefix.size();
    const struct { const char *tbl; int *out; } q[] = {
        {"tracks", tracks}, {"videos", videos}, {"photos", photos}};
    for (const auto &e : q) {
        const QByteArray sql =
            QByteArray("SELECT COUNT(*) FROM ") + e.tbl
            + " WHERE substr(filepath,1,?)=?";
        Stmt s(m_db, sql.constData());
        if (!s.ok())
            continue;
        s.bindInt64(1, plen);
        s.bindText(2, QString::fromUtf8(prefix));
        if (s.step() == SQLITE_ROW)
            *e.out = s.colInt(0);
    }
}

bool LibraryDb::setWatchFolderType(qint64 id, const QString &type) {
    if (!m_db) return false;
    Stmt s(m_db, "UPDATE watch_folders SET type=? WHERE id=?");
    if (!s.ok()) return false;
    s.bindText(1, type);
    s.bindInt64(2, id);
    return s.step() == SQLITE_DONE;
}

int LibraryDb::resolveDuplicateFormats(const QString &prefer) {
    if (!m_db) return 0;
    const char *loser, *keeper;
    if (prefer == QLatin1String("flac")) {
        loser = "lower(substr(t.filepath,-4))='.mp3'";
        keeper = "lower(substr(o.filepath,-5))='.flac'";
    } else if (prefer == QLatin1String("mp3")) {
        loser = "lower(substr(t.filepath,-5))='.flac'";
        keeper = "lower(substr(o.filepath,-4))='.mp3'";
    } else {
        return 0;
    }
    const QByteArray twinJoin = QByteArray(
        " FROM tracks t JOIN tracks o ON o.id != t.id"
        "  AND o.artist = t.artist COLLATE NOCASE"
        "  AND o.album = t.album COLLATE NOCASE"
        "  AND o.title = t.title COLLATE NOCASE"
        "  AND o.tracknumber = t.tracknumber"
        "  AND COALESCE(NULLIF(o.discnumber,0),1) = COALESCE(NULLIF(t.discnumber,0),1)"
        " WHERE ") + loser + " AND " + keeper;
    // Exclude first (tagged) so the scanner won't re-add the files on
    // the next sweep — un-deleted-un-excluded losers churned forever:
    // every scan re-added them, toasted "added N tracks", and dropped
    // them again (2026-09-07).
    {
        const QByteArray sql = QByteArray(
            "INSERT OR IGNORE INTO excluded_files (filepath, reason)"
            " SELECT DISTINCT t.filepath, 'dup-format'") + twinJoin;
        Stmt ex(m_db, sql.constData());
        if (!ex.ok() || ex.step() != SQLITE_DONE)
            return 0;
    }
    const QByteArray sql = QByteArray(
        "DELETE FROM tracks WHERE id IN (SELECT t.id") + twinJoin + ")";
    Stmt s(m_db, sql.constData());
    if (!s.ok() || s.step() != SQLITE_DONE)
        return 0;
    return sqlite3_changes(m_db);
}

// Preference flip: lift ONLY the resolver's exclusions (user deletes
// keep theirs), so a rescan resurrects the dropped format.
int LibraryDb::clearFormatExclusions() {
    if (!m_db) return 0;
    Stmt s(m_db, "DELETE FROM excluded_files WHERE reason='dup-format'");
    if (!s.ok() || s.step() != SQLITE_DONE)
        return 0;
    return sqlite3_changes(m_db);
}

// ── Exclusions ──

bool LibraryDb::isExcluded(const QString &filepath) {
    if (!m_db) return false;
    Stmt s(m_db, "SELECT 1 FROM excluded_files WHERE filepath=?");
    if (!s.ok()) return false;
    s.bindText(1, filepath);
    return s.step() == SQLITE_ROW;
}

// ── Transactions ──

bool LibraryDb::begin() { return exec("BEGIN IMMEDIATE;"); }
bool LibraryDb::commit() { return exec("COMMIT;"); }
bool LibraryDb::rollback() { return exec("ROLLBACK;"); }

QVariantMap LibraryDb::collectionCustomization(const QString &kind, const QString &key) {
    if (!m_db) return {};
    Stmt s(m_db, "SELECT identity_json, art_path FROM collection_customizations WHERE kind=? AND item_key=?");
    if (!s.ok()) return {};
    s.bindText(1, kind); s.bindText(2, key);
    if (s.step() != SQLITE_ROW) return {};
    return {{QStringLiteral("identity"), QJsonDocument::fromJson(s.colText(0).toUtf8()).object().toVariantMap()},
            {QStringLiteral("artPath"), s.colText(1)}};
}

bool LibraryDb::setCollectionCustomization(const QString &kind, const QString &key,
                                           const QVariantMap &identity, const QString &artPath) {
    if (!m_db || kind.isEmpty() || key.isEmpty()) return false;
    Stmt s(m_db, "INSERT INTO collection_customizations(kind,item_key,identity_json,art_path) VALUES(?,?,?,?)"
                " ON CONFLICT(kind,item_key) DO UPDATE SET identity_json=excluded.identity_json,art_path=excluded.art_path");
    if (!s.ok()) return false;
    s.bindText(1, kind); s.bindText(2, key);
    s.bindText(3, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(identity)).toJson(QJsonDocument::Compact)));
    s.bindText(4, artPath);
    return s.step() == SQLITE_DONE;
}

bool LibraryDb::removeCollectionCustomization(const QString &kind, const QString &key) {
    if (!m_db) return false;
    Stmt s(m_db, "DELETE FROM collection_customizations WHERE kind=? AND item_key=?");
    if (!s.ok()) return false;
    s.bindText(1, kind); s.bindText(2, key);
    return s.step() == SQLITE_DONE;
}

// Virtual photo collections. Savepoints also allow these edits inside a caller's
// larger transaction; failed validation never leaves a partial membership edit.
namespace {
class PhotoEdit {
public:
    PhotoEdit(sqlite3 *db, QString &error) : db(db), error(error) {
        error.clear();
        active = db && sqlite3_exec(db, "SAVEPOINT photo_edit", nullptr, nullptr, nullptr) == SQLITE_OK;
        if (!active) fail("Could not start photo album edit.");
    }
    ~PhotoEdit() {
        if (active) {
            sqlite3_exec(db, "ROLLBACK TO photo_edit", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "RELEASE photo_edit", nullptr, nullptr, nullptr);
        }
    }
    bool fail(const QString &message) { error = message; return false; }
    bool done(Stmt &stmt) {
        if (!stmt.ok() || stmt.step() != SQLITE_DONE) {
            if (sqlite3_extended_errcode(db) == SQLITE_CONSTRAINT_UNIQUE)
                return fail("An album with that name already exists here.");
            return fail(QString::fromUtf8(sqlite3_errmsg(db)));
        }
        return true;
    }
    bool finish() {
        if (!active || sqlite3_exec(db, "RELEASE photo_edit", nullptr, nullptr, nullptr) != SQLITE_OK)
            return fail("Could not save photo album edit.");
        active = false;
        return true;
    }
    bool exists(qint64 id, const char *table = "photo_albums") {
        const QByteArray query = QByteArray("SELECT 1 FROM ") + table + " WHERE id=?";
        Stmt stmt(db, query.constData());
        if (!stmt.ok()) return fail("Could not read photo album.");
        stmt.bindInt64(1, id);
        return stmt.step() == SQLITE_ROW || fail("Photo or album no longer exists.");
    }
    bool depthFits(qint64 id, qint64 parentId) {
        // Keep hierarchy edits well below SQLite's cascade recursion limit.
        Stmt depth(db, "WITH RECURSIVE ancestors(id,parent_id,depth) AS ("
                      "SELECT id,parent_id,1 FROM photo_albums WHERE id=? UNION ALL "
                      "SELECT a.id,a.parent_id,p.depth+1 FROM photo_albums a JOIN ancestors p ON a.id=p.parent_id),"
                      "descendants(id,depth) AS (SELECT ?,1 UNION ALL "
                      "SELECT a.id,p.depth+1 FROM photo_albums a JOIN descendants p ON a.parent_id=p.id) "
                      "SELECT COALESCE((SELECT MAX(depth) FROM ancestors),0)+(SELECT MAX(depth) FROM descendants)");
        if (!depth.ok()) return fail("Could not check album depth.");
        depth.bindInt64(1, parentId); depth.bindInt64(2, id);
        return (depth.step() == SQLITE_ROW && depth.colInt(0) <= 32)
               || fail("Albums support up to 32 nested levels.");
    }
    bool active = false;
private:
    sqlite3 *db;
    QString &error;
};
}

QVariantList LibraryDb::allPhotoAlbums() {
    QVariantList rows;
    if (!m_db) return rows;
    Stmt s(m_db, "SELECT a.id,COALESCE(a.parent_id,0),a.name,"
                 "(SELECT COUNT(*) FROM photo_album_photos p WHERE p.album_id=a.id)"
                 " FROM photo_albums a ORDER BY a.name COLLATE NOCASE,a.id");
    while (s.ok() && s.step() == SQLITE_ROW)
        rows.append(QVariantMap{{"id", s.colInt64(0)}, {"parentId", s.colInt64(1)},
                                {"name", s.colText(2)}, {"count", s.colInt(3)}});
    return rows;
}

qint64 LibraryDb::createPhotoAlbum(const QString &name, qint64 parentId) {
    PhotoEdit edit(m_db, m_lastError);
    const QString clean = name.trimmed();
    if (!edit.active) return -1;
    if (clean.isEmpty() || clean.size() > 200 || parentId < 0) {
        edit.fail("Use an album name between 1 and 200 characters and a valid parent."); return -1;
    }
    if (parentId && !edit.exists(parentId)) return -1;
    if (!edit.depthFits(-1, parentId)) return -1;
    Stmt s(m_db, "INSERT INTO photo_albums(name,parent_id) VALUES(?,?)");
    if (!s.ok()) return -1;
    s.bindText(1, clean);
    if (parentId) s.bindInt64(2, parentId); else s.bindNull(2);
    if (!edit.done(s)) return -1;
    const qint64 id = sqlite3_last_insert_rowid(m_db);
    return edit.finish() ? id : -1;
}

bool LibraryDb::renamePhotoAlbum(qint64 id, const QString &name) {
    PhotoEdit edit(m_db, m_lastError);
    const QString clean = name.trimmed();
    if (!edit.active) return false;
    if (clean.isEmpty() || clean.size() > 200) return edit.fail("Use an album name between 1 and 200 characters.");
    if (!edit.exists(id)) return false;
    Stmt s(m_db, "UPDATE photo_albums SET name=? WHERE id=?");
    if (!s.ok()) return edit.fail("Could not rename album.");
    s.bindText(1, clean); s.bindInt64(2, id);
    return edit.done(s) && edit.finish();
}

bool LibraryDb::movePhotoAlbum(qint64 id, qint64 parentId) {
    PhotoEdit edit(m_db, m_lastError);
    if (!edit.active || !edit.exists(id)) return false;
    if (parentId < 0) return edit.fail("Invalid parent album.");
    if (parentId && !edit.exists(parentId)) return false;
    Stmt cycle(m_db, "WITH RECURSIVE descendants(id) AS (SELECT ? UNION SELECT a.id FROM photo_albums a"
                    " JOIN descendants d ON a.parent_id=d.id) SELECT 1 FROM descendants WHERE id=?");
    if (!cycle.ok()) return edit.fail("Could not check album ancestry.");
    cycle.bindInt64(1, id); cycle.bindInt64(2, parentId);
    const int cycleResult = cycle.step();
    if (cycleResult == SQLITE_ROW) return edit.fail("An album cannot be moved inside itself or a nested album.");
    if (cycleResult != SQLITE_DONE) return edit.fail("Could not check album ancestry.");
    if (!edit.depthFits(id, parentId)) return false;
    Stmt s(m_db, "UPDATE photo_albums SET parent_id=? WHERE id=?");
    if (!s.ok()) return edit.fail("Could not move album.");
    if (parentId) s.bindInt64(1, parentId); else s.bindNull(1);
    s.bindInt64(2, id);
    return edit.done(s) && edit.finish();
}

bool LibraryDb::deletePhotoAlbum(qint64 id) {
    PhotoEdit edit(m_db, m_lastError);
    if (!edit.active || !edit.exists(id)) return false;
    Stmt s(m_db, "DELETE FROM photo_albums WHERE id=?");
    if (!s.ok()) return edit.fail("Could not delete album.");
    s.bindInt64(1, id);
    return edit.done(s) && edit.finish();
}

QVector<qint64> LibraryDb::photoAlbumPhotoIds(qint64 id) {
    QVector<qint64> ids;
    if (!m_db) return ids;
    Stmt s(m_db, "SELECT photo_id FROM photo_album_photos WHERE album_id=? ORDER BY position,photo_id");
    if (!s.ok()) return ids;
    s.bindInt64(1, id);
    while (s.step() == SQLITE_ROW) ids.append(s.colInt64(0));
    return ids;
}

bool LibraryDb::addPhotoAlbumPhotos(qint64 id, const QVector<qint64> &photoIds) {
    PhotoEdit edit(m_db, m_lastError);
    if (!edit.active || !edit.exists(id)) return false;
    for (qint64 photoId : photoIds) {
        if (!edit.exists(photoId, "photos")) return false;
        Stmt s(m_db, "INSERT INTO photo_album_photos(album_id,photo_id,position)"
                     " SELECT ?,?,COALESCE(MAX(position)+1,0) FROM photo_album_photos WHERE album_id=?"
                     " ON CONFLICT(album_id,photo_id) DO NOTHING");
        if (!s.ok()) return edit.fail("Could not add photo to album.");
        s.bindInt64(1, id); s.bindInt64(2, photoId); s.bindInt64(3, id);
        if (!edit.done(s)) return false;
    }
    return edit.finish();
}

bool LibraryDb::removePhotoAlbumPhotos(qint64 id, const QVector<qint64> &photoIds) {
    PhotoEdit edit(m_db, m_lastError);
    if (!edit.active || !edit.exists(id)) return false;
    for (qint64 photoId : photoIds) {
        Stmt s(m_db, "DELETE FROM photo_album_photos WHERE album_id=? AND photo_id=?");
        if (!s.ok()) return edit.fail("Could not remove photo from album.");
        s.bindInt64(1, id); s.bindInt64(2, photoId);
        if (!edit.done(s)) return false;
    }
    return edit.finish();
}

bool LibraryDb::reorderPhotoAlbumPhotos(qint64 id, const QVector<qint64> &photoIds) {
    PhotoEdit edit(m_db, m_lastError);
    if (!edit.active || !edit.exists(id)) return false;
    QVector<qint64> current;
    Stmt membership(m_db, "SELECT photo_id FROM photo_album_photos WHERE album_id=?");
    if (!membership.ok()) return edit.fail("Could not read album contents.");
    membership.bindInt64(1, id);
    int result;
    while ((result = membership.step()) == SQLITE_ROW) current.append(membership.colInt64(0));
    if (result != SQLITE_DONE) return edit.fail("Could not read album contents.");
    const QSet<qint64> requested(photoIds.begin(), photoIds.end());
    if (requested.size() != photoIds.size() || requested != QSet<qint64>(current.begin(), current.end()))
        return edit.fail("Album contents changed. Refresh the album before reordering.");
    for (qsizetype i = 0; i < photoIds.size(); ++i) {
        Stmt s(m_db, "UPDATE photo_album_photos SET position=? WHERE album_id=? AND photo_id=?");
        if (!s.ok()) return edit.fail("Could not reorder album.");
        s.bindInt64(1, i); s.bindInt64(2, id); s.bindInt64(3, photoIds[i]);
        if (!edit.done(s)) return false;
    }
    return edit.finish();
}
