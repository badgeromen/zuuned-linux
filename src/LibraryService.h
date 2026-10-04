#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QThread>
#include <QtQml/qqmlregistration.h>

#include "library/LibraryTypes.h"
#include "library/TrackIdentityIndex.h"

class LibraryScanner;
class AlbumArtService;
class ArtistImageService;
class OnlineAlbumArtService;
class LibraryDb;
class LocalTrackModel;
class VideoMatcher;
class QFileSystemWatcher;
class QTimer;

// UI-thread facade over the local library — the AppState/LibraryService
// pair of this app. Mirrors DeviceService's shape so the music browser
// can target either source:
//   tracks (model) · artistsList/albumsList/genresList (QVariantList of
//   {name, subtitle?, count, ...}) · artPaths (key → file:// url)
//
// Local specifics: album identity is COALESCE(albumartist, artist);
// albumsList entries carry {name, subtitle=albumartist, count,
// artArtist, artAlbum} so QML can request art; artPaths is keyed
// "artist\nalbum" lowercased (the art cache key input, NOT hashed —
// QML never needs the MD5).
class LibraryService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool empty READ empty NOTIFY libraryChanged)          // drives onboarding
    Q_PROPERTY(int trackCount READ trackCount NOTIFY libraryChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY libraryChanged)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY videosChanged)
    // W4: split counts for the onboarding summary (were hardcoded 0).
    Q_PROPERTY(int movieCount READ movieCount NOTIFY videosChanged)
    Q_PROPERTY(int tvEpisodeCount READ tvEpisodeCount NOTIFY videosChanged)
    Q_PROPERTY(QObject *tracks READ tracksModel CONSTANT)
    Q_PROPERTY(QVariantList artistsList READ artistsList NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList albumsList READ albumsList NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList genresList READ genresList NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList watchFolders READ watchFolders NOTIFY foldersChanged)
    Q_PROPERTY(QVariantMap artPaths READ artPaths NOTIFY artChanged)
    Q_PROPERTY(QVariantMap albumArtworkStatus READ albumArtworkStatus NOTIFY artworkStatusChanged)
    Q_PROPERTY(QVariantMap artistArtworkStatus READ artistArtworkStatus NOTIFY artworkStatusChanged)
    // lowercase(artist name) → file:// url of the cached artist photo
    Q_PROPERTY(QVariantMap artistImages READ artistImages NOTIFY artistImagesChanged)
    // Reactive dependency for device-row membership without exposing or
    // copying a library-sized QVariantMap into each QML delegate.
    Q_PROPERTY(quint64 localTrackIdentityRevision READ localTrackIdentityRevision NOTIFY localTrackIdentitiesChanged)

    // Scan state (onboarding step 3 + Settings + toasts)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(QString scanStage READ scanStage NOTIFY scanChanged)
    Q_PROPERTY(QString scanCurrentFile READ scanCurrentFile NOTIFY scanChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanChanged)
    Q_PROPERTY(QVariantList musicProbeFailures READ musicProbeFailures NOTIFY musicProbeFailuresChanged)

public:
    explicit LibraryService(QObject *parent = nullptr);
    ~LibraryService() override;

    bool empty() const;
    QVariantList musicProbeFailures() const;
    int trackCount() const;
    int photoCount() const;
    // ── Photos (Phase 8) — folder-albums, drill-down like videos ──
    // Albums: [{name, path, count, onZune, covers: [≤4 file:// urls]}],
    // sorted by name. Album id IS the folder path. onDeviceStems (from
    // DeviceService.photoOnDeviceKeys) drives the per-album onZune
    // count for badges.
    Q_INVOKABLE QVariantList photoAlbums(
        const QStringList &onDeviceStems = QStringList());
    // Photos of one folder-album: [{id, filename, url, filesize, mtime}]
    // sorted by filename (natural shoot order for camera dumps).
    Q_INVOKABLE QVariantList photosInAlbum(const QString &path);
    // Virtual albums: direct membership only; all local operations work offline.
    Q_INVOKABLE QVariantList customPhotoAlbums(const QStringList &onDeviceStems = QStringList());
    Q_INVOKABLE QVariantList customAlbumPhotos(double id);
    Q_INVOKABLE QVariantMap createPhotoAlbum(const QString &name, double parentId = 0,
                                             const QVariantList &photoIds = QVariantList());
    Q_INVOKABLE QVariantMap renamePhotoAlbum(double id, const QString &name);
    Q_INVOKABLE QVariantMap movePhotoAlbum(double id, double parentId);
    Q_INVOKABLE QVariantMap deletePhotoAlbum(double id);
    Q_INVOKABLE QVariantMap addPhotosToAlbum(double id, const QVariantList &photoIds);
    Q_INVOKABLE QVariantMap removePhotosFromAlbum(double id, const QVariantList &photoIds);
    Q_INVOKABLE QVariantMap setPhotoAlbumOrder(double id, const QVariantList &photoIds);
    Q_INVOKABLE QVariantMap photoAlbumSnapshot(double id);
    Q_INVOKABLE QVariantMap savePhotoAlbumDraft(double id, const QString &name, double parentId,
                                               const QVariantList &photoIds,
                                               const QVariantMap &originalSnapshot);
    Q_INVOKABLE void deletePhotos(const QVariantList &ids);
    QObject *tracksModel() const;
    QVariantList artistsList() const { return m_artistsList; }
    QVariantList albumsList() const { return m_albumsList; }
    QVariantList genresList() const { return m_genresList; }
    QVariantList watchFolders() const { return m_watchFolders; }
    QVariantMap artPaths() const { return m_artPaths; }
    QVariantMap albumArtworkStatus() const { return m_albumArtworkStatus; }
    QVariantMap artistArtworkStatus() const { return m_artistArtworkStatus; }
    Q_INVOKABLE void retryArtwork(const QString &kind, const QVariantMap &context);
    QVariantMap artistImages() const { return m_artistImages; }
    quint64 localTrackIdentityRevision() const { return m_localTrackIdentityRevision; }
    bool scanning() const { return m_scanning; }
    QString scanStage() const { return m_scanStage; }
    QString scanCurrentFile() const { return m_scanCurrentFile; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }

    Q_INVOKABLE void addWatchFolder(const QString &path, const QString &type);
    // Batch add — each entry is {path, type}. Adds them all, then kicks a
    // SINGLE scan (onboarding commits every chosen folder at once so the
    // finishing screen shows one clean pass, not one per folder).
    Q_INVOKABLE void addWatchFolders(const QVariantList &folders);
    Q_INVOKABLE void removeWatchFolder(double id);
    Q_INVOKABLE void setWatchFolderType(double id, const QString &type);
    // {tracks, videos, photos} indexed under a watch-folder root — the
    // Settings rows' count subline (QML keeps it live with void reads
    // of the library counts).
    Q_INVOKABLE QVariantMap folderCounts(const QString &path) const;
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void retryMissingArtwork();
    // Settings → Library "Duplicate formats" changed: un-exclude the
    // resolver's losers and rescan (the post-scan pass re-applies the
    // new preference).
    Q_INVOKABLE void applyDuplicateFormats();
    // Art for a local album; lands in artPaths under key
    // lowercase(artist)+"\n"+lowercase(album).
    Q_INVOKABLE void requestArt(const QString &artist, const QString &album,
                                const QString &trackFilepath);
    void importDeviceAlbumArt(const QString &artist, const QString &album,
                              const QString &sourcePath);
    Q_INVOKABLE static QString artKey(const QString &artist, const QString &album);
    // Drag device→library: does the library already hold this track?
    // Case-insensitive match on title + album + (artist or albumartist),
    // so a re-import of something we already have is caught and reported
    // instead of duplicated on disk.
    Q_INVOKABLE bool hasTrack(const QString &artist, const QString &album,
                              const QString &title, int discNumber = 0,
                              int trackNumber = 0, bool discReliable = true,
                              bool trackReliable = true, const QVariantList &sourcePeers = {}) const;
    // Device-browser badge only: all fields must be present. Call after
    // reading localTrackIdentityRevision in the QML binding to stay live.
    // Metadata equivalence is not proof of identical bytes or available
    // files; this never stats paths or performs a database query.
    Q_INVOKABLE bool hasLocalTrack(const QString &artist, const QString &album,
                                  const QString &title, int discNumber = 0,
                                  int trackNumber = 0, bool discReliable = true,
                                  bool trackReliable = true, const QVariantList &sourcePeers = {}) const;
    Q_INVOKABLE QVariantList musicIdentityPeers(const QVariantMap &track) const;
    // Drag device→library for videos/photos. Videos match on series +
    // season + episode when it's a TV episode, else on filename; photos
    // match on filename. Both case-insensitive.
    Q_INVOKABLE bool hasVideo(const QString &filename, const QString &series,
                              int season, int episode) const;
    Q_INVOKABLE bool hasPhoto(const QString &filename) const;
    // Reserve every imported occurrence, in device order, before pulling
    // files. Result: {success,id,here,pending,itemsToPull,error}; repeated
    // occurrences stay in the playlist but each device file is pulled once.
    Q_INVOKABLE QVariantMap importDevicePlaylist(const QString &name, const QVariantList &items,
                                                 const QVariantList &sourcePeers = {});
    // Compatibility entry point: append one resolved or pending slot.
    Q_INVOKABLE bool importPlaylistTrack(double playlistId,
                                         const QString &artist,
                                         const QString &album,
                                         const QString &title);
    // Tracks still waiting to auto-join this playlist (pulled but not
    // yet scanned in) — Settings' "N not in library" subline. Changes
    // ride playlistsChanged.
    Q_INVOKABLE int pendingPlaylistAddCount(double playlistId) const;
    // Artist photo (Deezer fallback chain); lands in artistImages keyed
    // by lowercase(name).
    Q_INVOKABLE void requestArtistImage(const QString &name);

    // ── Music customize (W13, album/artist parity with video) ──
    // Custom art: copy a local image (file:// or path) into the SAME
    // md5-keyed cache the auto-fetch and device art sync use, downscaled,
    // and pin it so it wins and survives rescans. Album keys on
    // (artist, album); artist keys on name.
    Q_INVOKABLE void setAlbumCustomArt(const QString &artist,
                                       const QString &album,
                                       const QString &source);
    Q_INVOKABLE void setArtistCustomArt(const QString &name,
                                        const QString &source);
    // Manual identity: rewrite the shared fields across every track in
    // the group (DB authoritative + user_edited, ID3 in place for MPEG),
    // exactly the path editTrackMetadata uses. Album fields map keys:
    // album, albumartist, year, genre. Artist fields map key: artist.
    Q_INVOKABLE void setManualAlbumIdentity(const QString &artist,
                                            const QString &album,
                                            const QVariantMap &fields);
    Q_INVOKABLE void setManualArtistIdentity(const QString &name,
                                             const QVariantMap &fields);

    // Sleeve: browsing never mutates the library. Apply prepares all
    // network/image work before committing the complete draft once.
    Q_INVOKABLE void applyCustomization(const QString &requestId,
                                        const QString &kind,
                                        const QVariantMap &context,
                                        const QVariantMap &draft);
    Q_INVOKABLE void requestCustomizeArt(const QString &requestId,
                                         const QString &kind,
                                         const QString &source,
                                         const QVariantMap &context);
    Q_INVOKABLE void searchCustomizeIdentity(const QString &requestId,
                                             const QString &query, bool tv);
    Q_INVOKABLE QVariantMap customizeContext(const QString &kind,
                                             const QVariantMap &context) const;
    Q_INVOKABLE void searchCustomizeMusicIdentity(const QString &requestId, const QString &kind,
                                                   const QString &query, const QString &provider,
                                                   const QString &artistHint = QString());
    Q_PROPERTY(int collectionArtRevision READ collectionArtRevision NOTIFY collectionArtChanged)
    int collectionArtRevision() const { return m_collectionArtRevision; }
    Q_INVOKABLE QString collectionArt(const QString &kind, const QString &key) const;

    // ── Online art galleries (W13 finish) ──
    // Async candidate fetch → emits artCandidatesReady(kind, source, urls).
    // kind: movie|series|album|artist. source: tmdb|fanart (video),
    // caa (album), deezer (artist). ctx per kind — video: {tmdbId, tv,
    // title}; album: {artist, album}; artist: {name}.
    Q_INVOKABLE void fetchArtCandidates(const QString &kind,
                                        const QString &source,
                                        const QVariantMap &ctx);
    // Frame grabs sampled from the video file itself (no network) →
    // emits frameGrabsReady(urls) of file:// paths.
    Q_INVOKABLE void fetchFrameGrabs(double videoId, const QString &filepath,
                                     int durationMs);
    // Commit a chosen remote image url as custom art (downloads → pins).
    Q_INVOKABLE void pickVideoArtUrl(const QVariantList &ids, const QString &url);
    Q_INVOKABLE void pickAlbumArtUrl(const QString &artist,
                                     const QString &album, const QString &url);
    Q_INVOKABLE void pickArtistArtUrl(const QString &name, const QString &url);
    // Player position persistence (PlayerService hook target)
    void savePosition(qint64 libraryId, int positionMs);
    // Video position persistence (VideoPlayerService hook target).
    // Applies the mac 90% rule: position >= 90% of duration marks the
    // video watched.
    void saveVideoPosition(qint64 libraryId, int positionMs, int durationMs);

    // ── Videos ──
    // Full row set for VideoBrowserModel (C++ side only — QML reads the
    // browser model's precomputed lists, never this).
    const QVector<LibVideo> &videos() const { return m_videos; }
    int videoCount() const { return m_videos.size(); }
    int movieCount() const {
        int n = 0;
        for (const LibVideo &v : m_videos) if (v.isMovie()) n++;
        return n;
    }
    int tvEpisodeCount() const {
        int n = 0;
        for (const LibVideo &v : m_videos) if (v.isTV()) n++;
        return n;
    }
    QString stillsCacheDir() const;

    // NEVER deletes the actual file — removes the row and excludes the
    // path from future scans (mac semantics).
    Q_INVOKABLE void deleteVideo(double id);
    Q_INVOKABLE void deleteVideos(const QVariantList &ids);
    Q_INVOKABLE void setVideosWatched(const QVariantList &ids, bool watched);
    Q_INVOKABLE void setVideoCategory(double id, const QString &category);
    // Customize (W13). Manual identity: hand-entered fields for content
    // no database knows (fields map: category/title/year/genres/
    // overview/series/season/episode/episodeTitle). Custom art: copy a
    // local image (file:// or path) into the cache and pin it. Both are
    // sticky and refresh surgically (one tile, no reset).
    Q_INVOKABLE void setManualVideoIdentity(const QVariantList &ids,
                                            const QVariantMap &fields);
    Q_INVOKABLE void setVideoCustomArt(const QVariantList &ids,
                                       const QString &source);
    Q_INVOKABLE void reclassifyVideos(const QVariantList &ids,
                                      const QString &category);
    // Clear tmdb state so the lookup consumer retries the rows.
    Q_INVOKABLE void requeueForMatch(const QVariantList &ids);
    // Settings "re-run failed lookups": every cached-but-unmatched row.
    Q_INVOKABLE void requeueAllUnmatched();
    Q_PROPERTY(int unmatchedVideoCount READ unmatchedVideoCount NOTIFY videosChanged)
    int unmatchedVideoCount() const;
    // Manual series/episode edit — marks user_edited (sticky).
    Q_INVOKABLE void updateVideoSeries(double id, const QString &series,
                                       int season, int episode,
                                       const QString &episodeTitle);
    // Manual TMDB search (lookup sheet). Results arrive via
    // tmdbSearchResults: [{tmdbId, title, year, overview, posterUrl}].
    Q_INVOKABLE void tmdbSearch(const QString &query, bool tv);
    // Apply one TMDB result to all ids (bulk fix-match). Authoritative:
    // marks user_edited so rescans and the auto-matcher never overwrite.
    Q_INVOKABLE void assignTmdbResult(const QVariantList &ids, int tmdbId,
                                      bool tv);

    // Dominant vivid color of an image (saturation-weighted average,
    // then punched up) — the vinyl rim samples its accent from the
    // album cover. Cached per path; falls back to brand pink.
    Q_INVOKABLE QColor dominantColor(const QString &fileUrl);

    // ── Metadata editing (UX-2 Edit Info) ──
    // fields: {title, artist, albumartist, album, genre, trackNumber,
    // year} — all optional, missing keys keep current values. Updates
    // the library row (marked user_edited: scans never clobber it) and,
    // for MP3 files, rewrites the ID3 tags in place. Returns
    // {success, fileWritten, error}; a library failure is distinct from
    // a successful library save whose media-file tags were not changed.
    Q_INVOKABLE QVariantMap editTrackMetadata(double id, const QVariantMap &fields);

    // ── Playlists (UX-2) — library-first: playlists live HERE; the
    //    device side only mirrors them at sync time. ──
    // [{id, name, count}] sorted by name.
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    QVariantList playlists() const { return m_playlists; }
    // Returns the new playlist id (as double for QML), or -1.
    Q_INVOKABLE double createPlaylist(const QString &name,
                                      const QVariantList &trackIds = {});
    Q_INVOKABLE void renamePlaylist(double id, const QString &name);
    Q_INVOKABLE void deletePlaylist(double id);
    // Replace the full ordered list (tray save).
    Q_INVOKABLE void setPlaylistTracks(double id, const QVariantList &trackIds);
    // Stable occurrence IDs plus the editor's initial snapshot preserve
    // unseen pending/new arrivals while applying explicit reorder/removal.
    Q_INVOKABLE bool editPlaylistEntries(double id, const QString &name,
                                         const QVariantList &tracks,
                                         const QVariantList &originalEntryIds);
    // Append (Add to Playlist menu; dedups against existing members).
    Q_INVOKABLE void addToPlaylist(double id, const QVariantList &trackIds);
    // Resolved rows for the tray/detail view, in playlist order:
    // [{id, playlistEntryId, title, artist, album, durationMs, trackNumber, filepath}].
    // Tracks deleted from the library since are silently skipped.
    Q_INVOKABLE QVariantList playlistTracks(double id);
    // A3 escape hatch: mixtapes are never hostage to our schema.
    // Export writes one .m3u8 per playlist (EXTM3U with absolute
    // paths) and returns the folder; import reads one back, matching
    // rows by filepath (missing files are skipped, count reported).
    // D2: first-run health — evaluates what C++ can see (watch-folder
    // reachability, transcode temp space). QML composes these with the
    // udev and TMDB checks it owns. Rows: {id, ok, label, detail}.
    Q_INVOKABLE QVariantList healthChecks() const;
    Q_INVOKABLE QString exportPlaylistsM3U();
    Q_INVOKABLE QVariantMap importPlaylistM3U(const QString &fileUrl);

    // Health tab — transcode scratch space (GB free where temp files land)
    Q_INVOKABLE double tempFreeGB() const;
    // Copy library.db to a timestamped backup; returns a human label like
    // "today 14:32", or "" on failure. lastBackupLabel() reads the newest.
    Q_INVOKABLE QString backupDatabase();
    Q_INVOKABLE QString lastBackupLabel() const;

signals:
    void musicProbeFailuresChanged();
    void localTrackIdentitiesChanged();
    void customizationFinished(const QString &requestId, bool success,
                               const QString &error);
    void customizeArtReady(const QString &requestId, const QVariantList &urls,
                           const QString &error);
    void customizeIdentityReady(const QString &requestId,
                                const QVariantList &results, const QString &error);
    void libraryChanged();
    void playlistsChanged();
    void foldersChanged();
    void artChanged();
    void artworkStatusChanged();
    void artistImagesChanged();
    void collectionArtChanged();
    void scanChanged();
    void videosChanged();
    // Surgical refresh (2026-09-08): a fix-match / art correction that
    // does NOT change grouping (same category, already-matched) patches
    // just these rows in place and fires this instead of videosChanged,
    // so bound grids/drill lists never reset — only the touched tile
    // repaints, scroll and frame-grab stills untouched.
    void videoRowsChanged(const QVariantList &ids);
    // W4: a scan finished having added new items — the app toasts it
    // so a silent background scan announces its result.
    void scanFoundNew(int tracks, int photos);
    void tmdbSearchResults(const QVariantList &results);
    // Online art gallery candidates (absolute image urls) for the sheet.
    void artCandidatesReady(const QString &kind, const QString &source,
                            const QVariantList &urls);
    // Frame-grab stills (file:// urls) for the video Artwork tab.
    void frameGrabsReady(const QVariantList &urls);

private:
    QVariantMap photoAlbumResult(bool success);
    // Patch the given rows in m_videos in place from the DB and emit
    // videoRowsChanged (no global reset). Used by field-only edits.
    void patchVideoRows(const QVector<qint64> &ids);
    void reloadFromDb();       // repopulates model + grouped lists
    void reloadPlaylists();    // m_playlists ← DB, emits playlistsChanged
    void reloadFolders();
    // W24 playlist import: match a device track to a library row id
    // (case-insensitive title+album+artist), -1 if none.
    qint64 matchTrackId(const QString &artist, const QString &album,
                        const QString &title, int discNumber = 0,
                        int trackNumber = 0, const QVariantList &sourcePeers = {}) const;
    // Retry pending playlist adds after a scan brings new tracks in.
    void resolvePendingPlaylistAdds();
    QHash<qint64, int> m_pendingPlaylistCounts;
    // One-time QSettings → ordered SQLite slots migration.
    void loadPendingPlaylistAdds();
    void ensureGvfsMounts();   // remount SMB watch folders after reboot

    // ── Video consumers (probe + TMDB lookup) ──
    void reloadVideos();       // m_videos ← DB, emits videosChanged
    void pokeVideoConsumers(); // start/continue both background drains
    void runProbeRound();      // ≤64 rows, parallel libav header reads
    void runLookupRound();     // ≤200 eligible rows → matcher thread
    static int lookupBackoff(int attempts);

    QThread m_scanThread;
    QThread m_matcherThread;
    VideoMatcher *m_matcher = nullptr;
    QTimer *m_consumerTimer = nullptr;   // idle re-check while scanning
    // The library keeps itself: root watcher + debounce + periodic sweep
    QFileSystemWatcher *m_fsWatcher = nullptr;
    QTimer *m_watchDebounce = nullptr;
    QTimer *m_periodicSweep = nullptr;
    void rearmFolderWatcher();
    QVector<LibVideo> m_videos;
    bool m_probeBusy = false;
    bool m_matchBusy = false;
    bool m_customizationBusy = false;
    LibraryScanner *m_scanner = nullptr;
    AlbumArtService *m_art = nullptr;
    ArtistImageService *m_artistImagesSvc = nullptr;
    OnlineAlbumArtService *m_onlineAlbumArt = nullptr;
    QTimer *m_artworkDiscovery = nullptr;
    void discoverMissingArtwork();
    QVariantMap m_artistImages;
    QVariantMap m_albumArtworkStatus, m_artistArtworkStatus;
    QHash<QString, QVariantMap> m_albumMatchingEvidence;
    QVariantMap artworkContext(const QString &kind, const QVariantMap &context);
    void recordArtworkResult(const QString &kind, const QVariantMap &context, const QVariantMap &result);
    bool restoreArtworkResult(const QString &kind, const QVariantMap &context, const QVariantMap &input);
    QSet<QString> m_forceArtworkRetry;
    quint64 m_artworkResultRevision = 0;
    int m_collectionArtRevision = 0;
    LibraryDb *m_db = nullptr;             // UI-thread connection
    LocalTrackModel *m_tracks = nullptr;
    TrackIdentityIndex m_localTrackIdentities;
    quint64 m_localTrackIdentityRevision = 0;
    QVariantList m_artistsList, m_albumsList, m_genresList, m_watchFolders;
    QVariantList m_playlists;
    QVariantMap m_artPaths;
    QHash<QString, QStringList> m_albumArtSources;
    QHash<QString, QColor> m_domColorCache;
    int m_photoCount = 0;
    bool m_scanning = false;
    // W15 scan-as-you-go: a folder added mid-scan sets this so a fresh
    // pass runs when the current one finishes (the mtime fast-path makes
    // the re-walk of already-scanned folders cheap).
    bool m_rescanPending = false;
    // True once a scan pass commits real content — gates the
    // finish-time reload so a no-op sweep never resets the views.
    bool m_scanCommittedChunk = false;
    QString m_scanStage, m_scanCurrentFile;
    int m_scanCurrent = 0, m_scanTotal = 0;
};
