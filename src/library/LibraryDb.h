#pragma once

#include "LibraryTypes.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>

struct sqlite3;

// SQLite layer — the mac's LibraryService storage, same schema (raw
// sqlite3 C API, WAL, foreign keys). One instance per THREAD: the UI
// facade owns one connection, the scanner opens its own (the mac does
// the same). No Qt SQL module.
//
// Semantics ported from the mac:
// - user_edited=1 rows: scans update file stats only, never metadata
// - deletions insert into excluded_files; scans skip excluded paths
// - album identity key: COALESCE(NULLIF(albumartist,''), artist) NOCASE
class LibraryDb {
public:
    LibraryDb() = default;
    ~LibraryDb();

    // Opens/creates at QStandardPaths::AppDataLocation/library.db
    // (or explicit path for tests). Creates schema if missing.
    bool open(const QString &path = QString());
    void close();
    bool isOpen() const { return m_db != nullptr; }
    QString lastError() const { return m_lastError; }

    // ── Tracks ──
    QVector<LibTrack> allTracks();                 // ordered artist/album/disc/track NOCASE
    // Insert or update by filepath. mtime-unchanged rows are the
    // caller's fast-path; user_edited rows only get stats updated.
    bool upsertTrack(const LibTrack &t);
    bool updateAudioFingerprint(const LibTrack &expected, const QString &fingerprint);
    bool updateTrackMetadata(qint64 id, const LibTrack &t, bool markUserEdited);
    bool updateTrackPosition(qint64 id, int lastPositionMs);
    bool removeTrack(qint64 id, bool exclude);      // exclude=true → excluded_files
    // filepath → (id, mtime) for the scanner's unchanged fast-path
    QHash<QString, QPair<qint64, qint64>> trackPathIndex();
    QHash<QString, TrackProbeState> trackProbeIndex();
    bool recordMusicProbeFailure(const QString &filepath, const QString &error, bool *copyInvalidated = nullptr);
    bool clearMusicProbeFailure(const QString &filepath);
    QVariantList musicProbeFailures();

    // ── Videos ──
    QVector<LibVideo> allVideos();                 // ordered series/season/episode
    // filepath → (id, mtime) for the scanner's unchanged fast-path
    QHash<QString, QPair<qint64, qint64>> videoPathIndex();
    // (id, filesize) for changed-content detection
    QHash<qint64, qint64> videoSizeIndex();
    // Scanner inserts: parse fields come from zune_decode_filename +
    // folder-truth; duration/width/height stay 0 for the probe consumer.
    bool insertVideo(const LibVideo &v);
    // Content changed (size differs): reset probe fields for re-probe.
    bool touchVideoChanged(qint64 id, qint64 filesize, qint64 mtime);
    // mtime-only change: keep probed metadata.
    bool touchVideoMtime(qint64 id, qint64 mtime);
    // Rename-in-place repair — never over a user edit.
    bool updateVideoParse(qint64 id, const QString &series, int season,
                          int episode, const QString &category);
    bool updateVideoProbe(qint64 id, int durationMs, int width, int height);
    // Manual TMDB assignment (marks tmdb_cached, sets category).
    bool updateVideoTmdb(qint64 id, int tmdbId, const QString &title,
                         const QString &poster, const QString &cast,
                         const QString &director, double rating,
                         const QString &genres, const QString &year,
                         const QString &category, const QString &description);
    bool updateVideoSeries(qint64 id, const QString &series, int season,
                           int episode, const QString &episodeTitle);
    bool updateVideoCategory(qint64 id, const QString &category);
    // Customize: hand-entered identity (tmdb_id=-1 sentinel, sticky)
    // and a hand-picked cover (local cached path, sticky).
    bool setManualVideoIdentity(qint64 id, const QString &category,
                                const QString &title, const QString &year,
                                const QString &genres, const QString &overview,
                                const QString &series, int season,
                                int episode, const QString &episodeTitle);
    bool setVideoCustomPoster(qint64 id, const QString &localPath);
    // One prepared Customize row. Caller owns the surrounding transaction;
    // playback/probe fields remain untouched. Missing rows are failures.
    bool updateVideoCustomization(const LibVideo &video);
    bool setVideosWatched(const QVector<qint64> &ids, bool watched);
    bool setVideosUserEdited(const QVector<qint64> &ids);
    // Clear tmdb data + attempts so the lookup consumer retries.
    bool requeueVideosForMatch(const QVector<qint64> &ids);
    bool updateVideoPosition(qint64 id, int lastPositionMs);
    bool removeVideo(qint64 id, bool exclude);

    // ── Playlists (UX-2: library-first — the library is truth, the
    //    zune is a mirror) ──
    struct PlaylistEntry {
        qint64 id = 0;
        qint64 playlistId = 0;
        qint64 trackId = -1; // NULL in SQLite until an imported track resolves
        int position = 0;
        QString artist, album, title;
        int discNumber = 0, trackNumber = 0; // unknown remains zero for pending imports
    };
    // Returns the new playlist id, or -1.
    qint64 createPlaylist(const QString &name, const QVector<qint64> &trackIds);
    // [{id, name, count}]
    QVector<QPair<qint64, QString>> allPlaylists(QVector<int> *counts = nullptr,
                                                QVector<int> *pendingCounts = nullptr);
    // Ordered track ids of a playlist.
    QVector<qint64> playlistTrackIds(qint64 playlistId);
    bool renamePlaylist(qint64 id, const QString &name);
    bool deletePlaylist(qint64 id);
    // Replace the full ordered track list.
    bool setPlaylistTracks(qint64 id, const QVector<qint64> &trackIds);
    bool appendPlaylistTracks(qint64 id, const QVector<qint64> &trackIds);
    qint64 createPlaylistEntries(const QString &name, const QVector<PlaylistEntry> &entries);
    QVector<PlaylistEntry> playlistEntries(qint64 playlistId);
    QVector<PlaylistEntry> pendingPlaylistEntries();
    bool appendPlaylistEntries(qint64 playlistId, const QVector<PlaylistEntry> &entries);
    bool resolvePlaylistEntry(qint64 entryId, qint64 trackId);
    // Apply only the editor's observed entries. Unknown/new/pending slots
    // survive; stable entry IDs distinguish repeated occurrences.
    bool editPlaylistEntries(qint64 playlistId, const QString &name,
                             const QVector<PlaylistEntry> &entries,
                             const QVector<qint64> &originalEntryIds);
    bool migratePendingPlaylistEntries(const QVector<PlaylistEntry> &entries);

    // Collection identity/art choices live beside the library and survive
    // rescans. Callers can include these writes in their Apply transaction.
    QVariantMap collectionCustomization(const QString &kind, const QString &key);
    bool setCollectionCustomization(const QString &kind, const QString &key,
                                     const QVariantMap &identity, const QString &artPath);
    bool removeCollectionCustomization(const QString &kind, const QString &key);

    // ── Photos ──
    QVector<LibPhoto> allPhotos();
    bool upsertPhoto(const LibPhoto &p);
    bool removePhoto(qint64 id, bool exclude);
    QHash<QString, QPair<qint64, qint64>> photoPathIndex();
    // Virtual albums never move/delete photo files. parentId=0 is the root.
    QVariantList allPhotoAlbums(); // id, parentId, name, direct count
    qint64 createPhotoAlbum(const QString &name, qint64 parentId = 0);
    bool renamePhotoAlbum(qint64 id, const QString &name);
    bool movePhotoAlbum(qint64 id, qint64 parentId);
    bool deletePhotoAlbum(qint64 id); // subtree and memberships only
    QVector<qint64> photoAlbumPhotoIds(qint64 id);
    bool addPhotoAlbumPhotos(qint64 id, const QVector<qint64> &photoIds);
    bool removePhotoAlbumPhotos(qint64 id, const QVector<qint64> &photoIds);
    bool reorderPhotoAlbumPhotos(qint64 id, const QVector<qint64> &photoIds);

    // ── Watch folders ──
    QVector<WatchFolder> watchFolders();
    bool addWatchFolder(const QString &path, const QString &type);
    bool removeWatchFolder(qint64 id);
    // W1: cascade-purge every track/video/photo living under a folder
    // path (exact prefix, NOT a LIKE — paths contain _ and [] which
    // are wildcards). No exclusion: re-adding the folder rescans
    // fresh. Returns total rows removed.
    int purgeFolder(const QString &path);
    // Indexed items under a folder (same exact-prefix match as
    // purgeFolder) per table — the Settings rows' count subline.
    void countsUnder(const QString &path, int *tracks, int *videos,
                     int *photos);
    bool setWatchFolderType(qint64 id, const QString &type);
    // Settings → Library "Duplicate formats": drop the losing format
    // where the same (artist, album, title, tracknumber) exists as both
    // FLAC and MP3. prefer: "flac" | "mp3" (anything else is a no-op).
    // Rows only leave the index — files stay; a keepBoth rescan
    // re-adds them. Returns rows removed.
    int resolveDuplicateFormats(const QString &prefer);
    // Lift the resolver's tagged exclusions (preference change)
    int clearFormatExclusions();

    // ── Exclusions ──
    bool isExcluded(const QString &filepath);

    // ── Transactions (scanner batches) ──
    bool begin();
    bool commit();
    bool rollback();

private:
    bool exec(const char *sql);
    sqlite3 *m_db = nullptr;
    QString m_lastError;
};
