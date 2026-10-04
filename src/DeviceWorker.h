#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>
#include <QVariantList>

#include <atomic>

#include "TrackModel.h"

struct ZuneDevice;

// Everything libzune, on one thread — the C++ equivalent of ZuunedMac's
// `actor MTPService`. The worker OWNS the ZuneDevice handle; the UI-thread
// DeviceService only ever talks to it through queued slots/signals, so
// device operations are serialized by construction: breach, scan, art
// grabs, and (later) the sync queue can never race each other on the USB
// pipe.
class DeviceWorker : public QObject {
    Q_OBJECT

public:
    struct BreachData {
        bool ok = false;
        QString error;
        QString name, model, serial;
        // ZuneDeviceFamily from MTP 0xD21A (0x00=Keel/Zune30, 0x02=Scorpius,
        // 0x03=Draco, 0x06=Pavo/HD, 0xFF=unknown). Drives the video
        // profile auto-detect — the model STRING is just "Zune".
        int family = 0xFF;
        int battery = 0;
        double capacityGB = 0, freeGB = 0;
        QVector<TrackRow> tracks;
        int albumCount = 0, artistCount = 0;
        QVariantList artists, albums, genres;
        QVariantList videos, photos, playlists;
        // Device photo folder-albums: [{itemId, name}] (ZMDB scan)
        QVariantList photoAlbums;
        // Art lives on MTP ALBUM objects, not tracks (the Zune returns
        // "no representative sample" for every track). Map track item →
        // its album object so any art request lands on the right object.
        QHash<quint32, quint32> trackToAlbumArt;
        // Album-object Artist is the best available album owner. Keep it
        // beside the art mapping so imported compilation art uses the same
        // local cache identity as the extracted track metadata when possible.
        QHash<quint32, QString> trackToAlbumArtist;
    };

    // Called from the C progress-callback thunk (worker thread) —
    // signals are protected, so the thunk routes through this.
    void notifySendProgress(const QString &entryId, double frac) {
        emit sendProgress(entryId, frac);
    }

    // Cross-thread abort: zune_abort only flips a volatile flag on the
    // device struct, safe to call from ANY thread (SyncEngine/UI) while
    // the worker is mid-transfer. Everything else on this class is
    // queued-slot only.
    void requestAbort();

public slots:
    void doBreach();
    void doSever();
    // Fetch art for an item into cachePath (skips USB if file exists).
    void doGrabArt(quint32 itemId, const QString &cachePath);
    // Photo thumbnail (representative sample → thumbnail → download+resize).
    void doGrabPhotoThumb(quint32 itemId, const QString &cachePath);

    // ── Sync (Phase 5) ──
    // Send one audio file (already MP3/WMA — transcode happens on the
    // engine's pool BEFORE this). artist arg on the wire = albumartist
    // (mac rule). Emits sendProgress during transfer, then sendDone.
    void doSendTrack(const QString &entryId, const QString &filepath,
                     const QString &title, const QString &albumartist,
                     const QString &album, const QString &genre,
                     int trackNumber, int durationMs);
    // ── Sync (Phase 6) ──
    // Send one already-transcoded video. Routing (mac MTPService rule):
    // series+episode → smuggle_episode (vendor props 0xDA9A/0xDAB5/0xDAB6),
    // else metagenre 0x23 → clip, 0x21 → other, default → movie.
    // posterPath: local JPEG sent as representative sample ("" = none).
    void doSendVideo(const QString &entryId, const QString &filepath,
                     const QString &objectFilename, const QString &title, int metagenre,
                     const QString &description, const QString &posterPath,
                     const QString &series, int season, int episode);
    // Mac sync phase 2 — buckets: {albumartist, artist, album, genre,
    // trackIds: QVariantList<uint>, artJpegPath: QString?}. Forges
    // artists, merges/creates album objects, links artist to album AND
    // to each track, brands album art.
    void doForgeAlbums(const QVariantList &buckets);
    // ── Phase 6: device video browsing ──
    // Download one video off the device (Save to Library). Serialized
    // with everything else on this thread — saves queue behind syncs.
    void doExtractVideo(quint32 itemId, const QString &destPath);
    // ── Phase 8: photos both directions ──
    // Send one photo (arm/resize happens inside libzune) into a named
    // device album ("" = root).
    void doSendPhoto(const QString &entryId, const QString &filepath,
                     const QString &albumName);
    // Download one photo / one track off the device (Save to Library +
    // the gallery's full-res resolver).
    void doExtractPhoto(quint32 itemId, const QString &destPath);
    void doExtractTrack(quint32 itemId, const QString &destPath);
    // ── Phase 9: playlists ──
    // Create a playlist on the device (0x9808 path — survives
    // re-index, Keel+Pavo verified). trackIds are DEVICE item ids.
    void doForgePlaylist(const QString &name, const QVariantList &trackIds);
    // Rename an item's display Name (0xDC44) — Zune HD test feature.
    void doRenameItem(quint32 itemId, const QString &newName);
    // Rename the DEVICE (Friendly Name prop 0xD402) — "rename zune".
    void doRenameDevice(const QString &newName);
    // 0x922A pre-transfer notification: item name + batch progress for
    // the device's own sync display. Queued right before each send on
    // this thread, so ordering is inherent. Best-effort.
    void doSyncNotify(const QString &name, int itemIndex, int totalItems);
    // Re-read capacity/free from the DEVICE (GetStorageInfo) — the
    // ground truth after purges, when local accounting has gone stale.
    void doRefreshStorage();
    // Vendor finalize (CleanDataStore → device re-index).
    void doFinalize();
    // Delete objects (tracks/albums/photos — MTP DeleteObject) by id.
    void doPurgeItems(const QVariantList &itemIds);
    void doPurgeInterruptedVideo(const QString &filename);
    void doPurgeInterruptedObject(const QVariantMap &record);

signals:
    void breachDone(const DeviceWorker::BreachData &data);
    void severDone();
    void artDone(quint32 itemId, const QString &path, bool ok);
    // Staged connection progress ("MTPZ authentication…", 0.4) for the
    // panel's connection bar — mirrors mac connectionStage/Progress.
    void stage(const QString &label, double progress);

    // ── Sync (Phase 5) ──
    void sendProgress(const QString &entryId, double frac);
    void sendDone(const QString &entryId, bool ok, quint32 itemId,
                  const QString &autopsyName);
    void sendVideoDone(const QString &entryId, bool ok, quint32 itemId,
                       const QString &autopsyName);
    void extractVideoDone(quint32 itemId, const QString &destPath, bool ok);
    void sendPhotoDone(const QString &entryId, bool ok, quint32 itemId,
                       const QString &autopsyName);
    void extractPhotoDone(quint32 itemId, const QString &destPath, bool ok);
    void extractTrackDone(quint32 itemId, const QString &destPath, bool ok);
    void renameItemDone(quint32 itemId, const QString &newName, bool ok);
    void forgePlaylistDone(const QString &name, quint32 playlistId, bool ok);
    void renameDeviceDone(const QString &newName, bool ok);
    void storageRefreshed(double capacityGB, double freeGB);
    void forgeProgress(int current, int total);
    void forgeDone(int okCount, int failCount);
    void finalizeDone(bool ok);
    void purgeProgress(int current, int total);
    void purgeDone(int okCount, int failCount, const QVariantList &purgedIds);
    void interruptedVideoPurged(const QString &filename, bool ok);
    void interruptedObjectPurged(const QString &token, bool ok);

private:
    ZuneDevice *m_dev = nullptr;
    // Mirror of m_dev for the cross-thread requestAbort() read.
    std::atomic<ZuneDevice *> m_devShared{nullptr};
};

Q_DECLARE_METATYPE(DeviceWorker::BreachData)
