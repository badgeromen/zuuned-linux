#pragma once

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "SyncQueueModel.h"
#include "../library/AlbumArtService.h"
#include "../library/MusicIdentity.h"

class DeviceService;
class DeviceWorker;
class LibraryService;

// Sync orchestrator. Runs on the UI thread as a signal-driven state machine;
// synchronous device calls and media preparation run on their own workers.
// The music path is:
//
//   startSync → canonicalizeAlbumArtists (verbatim mac semantics)
//            → shared MusicIdentity resolution (including disc/track confidence)
//            → per entry, sequentially:
//                 sniff magic bytes (NEVER the extension — real libraries
//                 contain WMA files named .mp3; the mac's extension check
//                 corrupts those)
//                 MPEG  → zuuned_retag_mp3_with_disc (ID3v2.3)
//                 other → zuuned_transcode_audio_with_disc (progress maps to ×0.5)
//                 → workerSendTrack (DeviceWorker, queued) → sendDone
//                 → bucket (albumartist\talbum) + art path resolve
//                 → 200ms spacing (firmware breathing room)
//            → workerForgeAlbums (artist objects + album objects + links
//              + representative-sample art, on the device thread)
//            → syncCompleted = true + DeviceService awaiting-disconnect.
//              NO finalize here — the eject flow finalizes (CleanDataStore
//              re-index), exactly like the mac.
//
// Transcode/retag run on QtConcurrent (never the device thread — they
// would serialize behind USB traffic). Cancel requests zune_abort via the
// worker (mid-transfer abort) and normalizes every in-flight status —
// fixing the mac's stale-".syncing" known issue.
//
// INTEGRATION (main.cpp, same pattern as the position-save hook):
//   engine->attachDevice(deviceService, deviceService->worker());
//   connect(engine, &SyncEngine::workerSendTrack,
//           worker, &DeviceWorker::doSendTrack, Qt::QueuedConnection);
//   connect(engine, &SyncEngine::workerForgeAlbums,
//           worker, &DeviceWorker::doForgeAlbums, Qt::QueuedConnection);
//   connect(worker, &DeviceWorker::sendProgress, engine, &SyncEngine::onSendProgress);
//   connect(worker, &DeviceWorker::sendDone,     engine, &SyncEngine::onSendDone);
//   connect(worker, &DeviceWorker::forgeProgress,engine, &SyncEngine::onForgeProgress);
//   connect(worker, &DeviceWorker::forgeDone,    engine, &SyncEngine::onForgeDone);
// On completion the engine invokes DeviceService "setAwaitingDisconnect"
// by name (QMetaObject) so this file has no compile dependency on the
// in-flight DeviceService additions; switch to a direct call at
// integration if preferred. Device-disconnect mid-sync: call
// engine->cancelSync().
class SyncEngine : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(SyncQueueModel *queue READ queue CONSTANT)
    Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY syncStateChanged)
    Q_PROPERTY(bool syncCompleted READ syncCompleted NOTIFY syncStateChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY progressChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY progressChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY progressChanged)
    // W2: which PHASE the current item is in, so a long transcode
    // reads as "transcoding", not a freeze. "" | transcoding |
    // sending | organizing | finalizing.
    Q_PROPERTY(QString syncPhase READ syncPhase NOTIFY progressChanged)
    Q_PROPERTY(double overallProgress READ overallProgress NOTIFY progressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY syncStateChanged)
    // B2: title of a send that was in flight when the app last died —
    // the device may hold a truncated object that crashes firmware on
    // playback. Non-empty until dismissed.
    Q_PROPERTY(QString interruptedSend READ interruptedSend
                   NOTIFY interruptedSendChanged)
    // B3: the device the queue was built against, and whether the
    // currently connected one differs (the panel shows a note; dedup
    // re-runs against the LIVE device at sync start regardless).
    Q_PROPERTY(QString queueBuiltFor READ queueBuiltFor
                   NOTIFY queueDeviceChanged)
    Q_PROPERTY(bool queueDeviceMismatch READ queueDeviceMismatch
                   NOTIFY queueDeviceChanged)
    // D1: outcome of the most recent completed sync (persisted):
    // {when (epoch s), tracks, videos, photos, skipped, failed, device}
    Q_PROPERTY(QVariantMap lastSync READ lastSync NOTIFY lastSyncChanged)

public:
    explicit SyncEngine(QObject *parent = nullptr);

    QString interruptedSend() const { return m_interruptedSend; }
    Q_PROPERTY(QString interruptedRecoveryStatus READ interruptedRecoveryStatus NOTIFY interruptedSendChanged)
    QString interruptedRecoveryStatus() const { return m_interruptedRecoveryStatus; }
    Q_INVOKABLE void dismissInterruptedSend();

    QString queueBuiltFor() const { return m_queueDevice; }
    bool queueDeviceMismatch() const;
    QVariantMap lastSync() const { return m_lastSync; }

    SyncQueueModel *queue() { return &m_queue; }
    bool isSyncing() const { return m_isSyncing; }
    bool syncCompleted() const { return m_syncCompleted; }
    int currentIndex() const { return m_currentIndex; }
    int totalCount() const { return m_totalCount; }
    QString currentName() const { return m_currentName; }
    QString syncPhase() const { return m_syncPhase; }
    double overallProgress() const;
    QString lastError() const { return m_lastError; }

    // Integration wiring (main.cpp, post engine-load).
    void attachDevice(DeviceService *device, DeviceWorker *worker);
    void attachLibrary(LibraryService *library) { m_library = library; }

    // tracks: list of {filepath, title, artist, albumartist?, album,
    // genre?, trackNumber?, discNumber?, year?, durationMs?, libraryId?}
    // Requires a connected Zune; offline calls leave the queue unchanged.
    // Returns {added, rejected, duplicates, error?} so callers can toast
    // the truth instead of assuming everything got in.
    Q_INVOKABLE QVariantMap addTracks(const QVariantList &tracks);
    // videos: list of {filepath, title, description?, posterPath?,
    // series?, season?, episode?, category?, durationMs?, filesize?,
    // libraryId?}. Same result map. Queue a whole series in ONE call —
    // per-item calls each pop their own capacity modal.
    Q_INVOKABLE QVariantMap addVideos(const QVariantList &videos);
    // photos: list of {filepath, filename?, album?, filesize?,
    // libraryId?}. album = device folder-album name. Same result map.
    Q_INVOKABLE QVariantMap addPhotos(const QVariantList &photos);
    // UX-2 ride-along playlist sync: queues the member tracks that
    // aren't on the device yet (normal addTracks path) PLUS a playlist
    // entry; after the video phase the engine resolves members to
    // device itemIds (pre-existing + freshly synced) and forges the
    // playlist via 0x9808. tracks: same shape as addTracks, in
    // playlist order. Result map gains {playlistQueued: bool}.
    Q_INVOKABLE QVariantMap addPlaylist(const QString &name,
                                        const QVariantList &tracks);
    Q_INVOKABLE void startSync();
    Q_INVOKABLE void cancelSync();
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void removeEntry(const QString &entryId);
    // B2: hunt the possibly-truncated object named by the in-flight
    // marker on the connected device and purge it. Returns how many
    // matching items were sent to the purge queue (0 = not found —
    // maybe it never committed, maybe a different device is plugged).
    Q_INVOKABLE int purgeInterruptedSend();

    // ── Pure logic, unit-testable ──
    // iTunes-style album canonicalization (SyncManager.swift semantics,
    // verbatim): buckets by (lowercased+trimmed album, disc-normalized
    // parent folder); per bucket ≥2 tracks: unique mode wins → ≥3
    // distinct values with tied top = "Various Artists" → 2-way tie =
    // shortest → all empty = "Unknown Artist"; overwrites albumartist
    // AND artist. Entry maps need: album, filepath, artist, albumartist,
    // title.
    static QVector<QVariantMap> canonicalizeAlbumArtists(QVector<QVariantMap> entries);
    static QString normalizeDiscFolder(const QString &folder);
    // Magic-byte sniff: true = MPEG audio ("ID3" prefix or frame sync
    // 0xFF Ex/Fx) → retag path; false (ASF GUID 30 26 B2 75 or anything
    // else) → transcode path.
    static bool sniffIsMpeg(const QString &path);
    static QString videoDisplayTitle(const QString &title, const QString &series,
                                     int season, int episode);
    // ObjectFileName: TV → "Series - S02E05[ - Title]",
    // else the display name; FAT32-illegal chars sanitized; extension by
    // profile (.wmv for Zune 30 profile 0, else .mp4).
    static QString videoWireName(const QString &displayName, const QString &series,
                                 int season, int episode, int profile);
    // Category → MetaGenre (0x25 movie / 0x26 tv / 0x23 music video /
    // 0x21 other); series+episode presence forces TV.
    static int videoMetagenre(const QString &category, const QString &series,
                              int episode);
    // Post-transcode size estimates (capacity gate at queue time).
    // Video: profile bitrate × duration (+5% mux); duration unknown →
    // source size (conservative — WMV/H.264 SD outputs shrink 1080p
    // sources). Track: 320kbps MP3 × duration; fallback source size.
    static qint64 estimateVideoBytes(int profile, int durationMs,
                                     qint64 sourceBytes);
    static qint64 estimateTrackBytes(int durationMs, qint64 sourceBytes);

public slots:
    // DeviceWorker → engine (integration connects)
    void onSendProgress(const QString &entryId, double frac);
    void onSendDone(const QString &entryId, bool ok, quint32 itemId,
                    const QString &autopsyName);
    void onForgeProgress(int current, int total);
    void onForgeDone(int okCount, int failCount);
    void onVideoSendDone(const QString &entryId, bool ok, quint32 itemId,
                         const QString &autopsyName);
    void onPhotoSendDone(const QString &entryId, bool ok, quint32 itemId,
                         const QString &autopsyName);
    // DeviceWorker::forgePlaylistDone (playlist phase)
    void onPlaylistForgeDone(const QString &name, quint32 playlistId, bool ok);

    // Internal: pool thread → UI (queued invokes)
    void onTranscodeProgress(const QString &entryId, double frac);
    void onPrepareFinished(const QString &entryId, const QString &preparedPath,
                           bool transcoded, const QString &error);

signals:
    void syncStateChanged();
    void progressChanged();
    void interruptedSendChanged();
    void queueDeviceChanged();
    void lastSyncChanged();
    // Fired once when a sync COMPLETES (not on cancel/failure-abort):
    // wall-clock duration + per-type success counts for the summary
    // modal. skipped = already-on-device, failed = entries left behind.
    void syncSummary(double elapsedMs, int tracks, int videos, int photos,
                     int skipped, int failed);
    // Anything actually ADDED to the queue — the device panel jumps to
    // its queue tab so you see what's happening (design contract).
    void queueItemsAdded(int count);
    // Capacity gate refused entries: the queue's estimate would overflow
    // the connected Zune (256MB safety margin). rejectedItems:
    // [{title, estGB}] — QML shows a modal listing exactly what didn't
    // fit next to what was added.
    void capacityRejected(const QVariantList &rejectedItems, int addedCount,
                          double queuedGB, double freeGB);

    // engine → DeviceWorker (integration connects, queued)
    void workerSendTrack(const QString &entryId, const QString &filepath,
                         const QString &title, const QString &albumartist,
                         const QString &album, const QString &genre,
                         int trackNumber, int durationMs);
    void workerForgeAlbums(const QVariantList &buckets);
    void workerSendVideo(const QString &entryId, const QString &filepath,
                         const QString &objectFilename, const QString &title, int metagenre,
                         const QString &description, const QString &posterPath,
                         const QString &series, int season, int episode);
    void workerSendPhoto(const QString &entryId, const QString &filepath,
                         const QString &albumName);
    // → DeviceWorker::doForgePlaylist (0x9808 + SetObjectReferences)
    void workerForgePlaylist(const QString &name, const QVariantList &trackIds);
    // 0x922A pre-transfer notify — drives the DEVICE's own sync display.
    // Emitted right before each send; queued to the same worker thread,
    // so it always lands immediately ahead of its transfer.
    void workerSyncNotify(const QString &name, int itemIndex, int totalItems);

private:
    friend struct SyncIdentityTestAccess;
    friend struct RecoveryTestAccess;
    void onInterruptedObjectPurged(const QString &token, bool ok);
    void advance();                       // next entry or forge phase
    void prepareEntry(int idx);           // sniff + retag/transcode (pool)
    void beginForge();
    // ── Video phase (mac SyncManager phase 4, after the album forge) ──
    // PIPELINED: up to 2 transcodes run ahead of the (strictly serial,
    // one-file-at-a-time) USB sends, buffering ≤3 prepared WMVs in /tmp
    // (~500MB). The wire never waits for an encode once the pipeline
    // fills; the device thread ordering is unchanged.
    struct PreparedVideo {
        int idx = -1;
        QString entryId;
        QString path;      // empty = terminal transcode failure (skip)
    };
    // ── Photo phase (mac SyncManager phase 3, between forge and video) ──
    // Serial sends; libzune arms (480px JPEG) inside smuggle_photo.
    void beginPhotoPhase();
    void advancePhoto();
    void beginVideoPhase();
    // ── Playlist phase (mac SyncManager phase 5, the last wire work
    // before finalize) — forge each queued playlist against the now-
    // complete device track set. ──
    struct PendingPlaylist {
        QString entryId;
        QString name;
        QVariantList members; // Full intended identities, in playlist order.
    };
    void beginPlaylistPhase();
    void advancePlaylist();
    void pumpVideoPipeline();             // launch prepares + dispatch send
    void dispatchVideoSend(const PreparedVideo &pv);
    void prepareVideoEntry(int idx, bool isRetry);
    void onVideoPrepareFinished(const QString &entryId, const QString &path,
                                const QString &error, bool wasRetry);
    int resolveVideoProfile() const;      // setting; -1=auto from device model
    qint64 capacityBudget() const;        // free − margin − queued estimate
    void emitCapacityRejected(const QVariantList &rejectedItems, int addedCount);
    void finishSync(bool completed);
    void normalizeStatuses(const QString &note);
    void markEntryTerminal(const QString &entryId, const QString &status,
                           const QString &note);
    MusicIdentity::Index deviceDedupIndex() const;
    QVector<MusicIdentity::Track> identityPeers(const QVariantMap &track,
                                              const MusicIdentity::Index &batch) const;
    void cleanupCurrentTemp(const QString &originalPath);

    // ── A1 queue persistence (JSON in AppData, debounced) ──
    QString queueStorePath() const;
    void schedulePersist();
    void persistQueue();
    void restoreQueue();
    // ── B2 in-flight send marker (killed-sync orphan detection) ──
    QString inflightPath() const;
    bool markInflight(const QString &type, const QVariantMap &identity);
    void retainFailedInflight(quint32 itemId);
    void markInflightVideo(const QString &filename, const QString &title);
    void clearInflight();

    SyncQueueModel m_queue;
    AlbumArtService m_artCache;           // read-only cache-path lookups
    DeviceService *m_device = nullptr;
    DeviceWorker *m_worker = nullptr;
    LibraryService *m_library = nullptr;

    bool m_isSyncing = false;
    bool m_syncCompleted = false;
    bool m_cancelled = false;
    int m_currentIndex = 0;
    int m_totalCount = 0;
    QString m_currentName;
    QString m_syncPhase;
    QString m_lastError;

    QVector<QVariantMap> m_entries;       // canonicalized snapshot
    int m_entryPos = -1;
    QVector<QVariantMap> m_videoEntries;  // video snapshot (phase after forge)
    QVector<QVariantMap> m_photoEntries;  // photo snapshot (phase 3)
    int m_photoPos = -1;
    bool m_inPhotoPhase = false;
    bool m_inVideoPhase = false;
    // Playlist phase state. m_pendingPlaylists persists across syncs
    // (entries queue before the sync starts); resolution maps are
    // rebuilt at phase start.
    QVector<PendingPlaylist> m_pendingPlaylists;
    QVector<int> m_playlistRound;         // indexes into m_pendingPlaylists
    int m_playlistPos = -1;
    bool m_inPlaylistPhase = false;
    MusicIdentity::Index m_playlistIdentities;
    QHash<QString, quint32> m_sentTrackIds; // Exact IDs returned for this sync's files.
    QHash<QString, MusicIdentity::Track> m_sentTrackIdentities; // Intent before artist canonicalization.
    int m_videoProfile = 2;
    // Pipeline state
    QVector<PreparedVideo> m_preparedBuf;
    int m_activePrepares = 0;
    int m_nextPrepareIdx = 0;
    int m_nextSendIdx = 0;
    bool m_videoSending = false;
    QString m_currentSendPath;            // temp of the file ON THE WIRE
    qint64 m_syncStartMs = 0;             // epoch ms at startSync (summary)
    int m_completedCount = 0;             // terminal entries (any outcome)
    QSet<QString> m_videoWarningsThisRun;
    double m_currentFraction = 0;         // current entry, 0..1
    bool m_forging = false;
    double m_forgeFraction = 0;

    QString m_currentEntryId;
    QString m_currentTempPath;            // retag/transcode temp (cleanup)
    bool m_currentTranscoded = false;

    // "albumartist\talbum" → {albumartist, artist, album, genre,
    // trackIds, artJpegPath}
    QMap<QString, QVariantMap> m_buckets;

    QTimer m_spacing;                     // 200ms between sends
    QTimer m_persistTimer;                // A1 debounce
    QString m_interruptedSend;            // B2 startup finding
    QString m_interruptedVideoFile;       // exact ObjectFileName, never display Name
    QVariantMap m_recoveryRecord;
    QVariantMap m_activeRecoveryRecord;
    QString m_pendingRecoveryToken;
    QString m_interruptedRecoveryStatus;
    QString m_queueDevice;                // B3 stamp
    QVariantMap m_lastSync;               // D1 persisted outcome
    QString currentDeviceIdent() const;
    void stampQueueDevice();
};
