#include "DeviceService.h"
#include "TrackImportPath.h"
#include "DevicePlaylistState.h"
#include "UsbWatcher.h"
#include "UdevSetup.h"
#include "library/VideoIdentity.h"

#include <QDir>
#include <QProcess>
#include <QFile>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

DeviceService::DeviceService(QObject *parent) : QObject(parent) {
    m_artCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                    + QStringLiteral("/art");
    QDir().mkpath(m_artCacheDir);

    m_worker = new DeviceWorker;
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    // UI → worker (queued across threads)
    connect(this, &DeviceService::workerBreach, m_worker, &DeviceWorker::doBreach);
    connect(this, &DeviceService::workerSever, m_worker, &DeviceWorker::doSever);
    connect(this, &DeviceService::workerGrabArt, m_worker, &DeviceWorker::doGrabArt);
    connect(this, &DeviceService::workerGrabPhotoThumb, m_worker, &DeviceWorker::doGrabPhotoThumb);

    // worker → UI
    connect(m_worker, &DeviceWorker::breachDone, this, &DeviceService::onBreachDone);
    connect(m_worker, &DeviceWorker::severDone, this, &DeviceService::onSeverDone);
    connect(m_worker, &DeviceWorker::artDone, this, &DeviceService::onArtDone);
    connect(m_worker, &DeviceWorker::stage, this, &DeviceService::onWorkerStage);
    connect(this, &DeviceService::workerFinalize, m_worker, &DeviceWorker::doFinalize);
    connect(this, &DeviceService::workerPurgeItems, m_worker, &DeviceWorker::doPurgeItems);
    connect(this, &DeviceService::workerPurgeInterruptedVideo, m_worker, &DeviceWorker::doPurgeInterruptedVideo);
    connect(m_worker, &DeviceWorker::interruptedVideoPurged, this, &DeviceService::interruptedVideoPurged);
    connect(this, &DeviceService::workerPurgeInterruptedObject, m_worker, &DeviceWorker::doPurgeInterruptedObject);
    connect(m_worker, &DeviceWorker::interruptedObjectPurged, this, &DeviceService::interruptedObjectPurged);
    connect(this, &DeviceService::workerExtractVideo, m_worker, &DeviceWorker::doExtractVideo);
    connect(m_worker, &DeviceWorker::extractVideoDone, this,
            [this](quint32, const QString &destPath, bool ok) {
        emit videoSaved(QFileInfo(destPath).fileName(), ok);
        notePullDone(ok);
    });
    connect(this, &DeviceService::workerExtractPhoto, m_worker, &DeviceWorker::doExtractPhoto);
    connect(this, &DeviceService::workerExtractTrack, m_worker, &DeviceWorker::doExtractTrack);
    connect(m_worker, &DeviceWorker::extractPhotoDone, this,
            [this](quint32 itemId, const QString &destPath, bool ok) {
        // Cache-dir extracts serve the gallery resolver; imports-dir
        // extracts are Save to Library.
        if (destPath.startsWith(m_artCacheDir)) {
            m_photoFullPending.remove(itemId);
            if (ok)
                emit photoFullReady(itemId,
                                    QUrl::fromLocalFile(destPath).toString());
        } else {
            emit photoSaved(QFileInfo(destPath).fileName(), ok);
            notePullDone(ok);   // imports-dir extract = a Save to Library
        }
    });
    connect(m_worker, &DeviceWorker::extractTrackDone, this,
            [this](quint32 itemId, const QString &destPath, bool ok) {
        m_pendingTrackDestinations.remove(TrackImportPath::key(destPath));
        emit trackSaved(QFileInfo(destPath).fileName(), ok);
        const auto batch = m_trackImportBatch.finished(itemId, ok);
        if (batch && batch->hasSuccessfulTrack) {
            for (const auto &album : batch->albums)
                requestImportedAlbumArt(album.itemId, album.artist,
                                        album.title);
            emit musicImportsReady(batch->artists);
        }
        notePullDone(ok);
    });
    connect(this, &DeviceService::workerForgePlaylist,
            m_worker, &DeviceWorker::doForgePlaylist);
    connect(m_worker, &DeviceWorker::forgePlaylistDone, this,
            [this](const QString &name, quint32 playlistId, bool ok) {
        if (ok) {
            m_playlistsList.append(QVariantMap{{"itemId", playlistId},
                                               {"name", name},
                                               {"count", 0},
                                               {"trackIds", QVariantList{}}});
            emit stateChanged();
        }
        emit playlistCreated(name, ok);
    });
    connect(this, &DeviceService::workerRenameItem, m_worker, &DeviceWorker::doRenameItem);
    connect(this, &DeviceService::workerRenameDevice, m_worker, &DeviceWorker::doRenameDevice);
    connect(this, &DeviceService::workerRefreshStorage, m_worker, &DeviceWorker::doRefreshStorage);
    connect(m_worker, &DeviceWorker::storageRefreshed, this,
            [this](double capacityGB, double freeGB) {
        m_capacityGB = capacityGB;
        m_freeGB = freeGB;
        emit stateChanged();
    });
    connect(m_worker, &DeviceWorker::renameDeviceDone, this,
            [this](const QString &newName, bool ok) {
        if (ok) {
            m_name = newName;
            emit stateChanged();
        }
        emit renameFinished(0, newName, ok);
    });
    connect(m_worker, &DeviceWorker::renameItemDone, this,
            [this](quint32 itemId, const QString &newName, bool ok) {
        if (ok) {
            // Mirror into the browse lists so the page updates without
            // a re-breach. (The Name prop is what the device shows;
            // our lists carry the filename — for videos the rename is
            // the more meaningful display, so adopt it.)
            for (int i = 0; i < m_videosList.size(); i++) {
                QVariantMap e = m_videosList[i].toMap();
                if (e.value(QStringLiteral("itemId")).toUInt() == itemId) {
                    e[QStringLiteral("name")] = newName;
                    e[QStringLiteral("title")] = newName;
                    if (e.contains(QStringLiteral("episodeTitle")))
                        e[QStringLiteral("episodeTitle")] = newName;
                    m_videosList[i] = e;
                    break;
                }
            }
            emit stateChanged();
        }
        emit renameFinished(itemId, newName, ok);
    });
    connect(m_worker, &DeviceWorker::purgeProgress, this,
            [this](int current, int total) {
        m_purgeCurrent = current;
        m_purgeTotal = total;
        m_status = QStringLiteral("Deleting %1 of %2…").arg(current).arg(total);
        emit purgeChanged();
        emit stateChanged();
    });
    connect(m_worker, &DeviceWorker::purgeDone, this, &DeviceService::onPurgeDone);
    connect(m_worker, &DeviceWorker::finalizeDone, this, &DeviceService::onFinalizeDone);

    m_workerThread.start();

    m_watcher = new UsbWatcher(this);
    connect(m_watcher, &UsbWatcher::zuneArrived, this, &DeviceService::onZuneArrived);
    connect(m_watcher, &UsbWatcher::zuneLeft, this, &DeviceService::onZuneLeft);
    m_watcher->start();
}

DeviceService::~DeviceService() {
    m_watcher->stop();
    if (m_connected)
        emit workerSever();
    m_workerThread.quit();
    m_workerThread.wait(5000);
}

void DeviceService::onZuneArrived() {
    m_devicePresent = true;
    if (m_awaitingDisconnect) {
        // Post-sync: the device may be re-indexing — do NOT breach into
        // it (mac connectDevice guards on awaitingDisconnect).
        m_status = QStringLiteral("Sync complete — unplug the Zune to apply changes");
        emit stateChanged();
        return;
    }
    if (m_connected || m_busy) {
        emit stateChanged();
        return;
    }
    m_status = QStringLiteral("Zune detected…");
    emit stateChanged();
    // Let enumeration/udev settle before opening — a breach in the first
    // moments after plug-in races the device's own MTP bring-up.
    QTimer::singleShot(1500, this, [this] {
        if (m_devicePresent && !m_connected && !m_busy)
            breach();
    });
}

void DeviceService::onZuneLeft() {
    m_devicePresent = false;
    m_awaitingDisconnect = false;  // physical unplug completes the cycle
    if (m_connected) {
        // Device is gone — sever frees state; CloseSession fails fast
        // (NO_DEVICE), which is expected and logged, not an error.
        m_connected = false;
        emit workerSever();
    }
    clearDeviceState();
    m_status = QStringLiteral("Zune unplugged");
    emit stateChanged();
}

void DeviceService::clearDeviceState() {
    m_moodColor = QColor();
    m_connectionStage.clear();
    m_connectionProgress = 0;
    m_name.clear();
    m_model.clear();
    m_battery = 0;
    m_capacityGB = m_freeGB = 0;
    m_musicGB = m_videoGB = m_photoGB = 0;
    m_trackCount = m_albumCount = m_artistCount = 0;
    m_artistsList.clear();
    m_albumsList.clear();
    m_genresList.clear();
    m_videosList.clear();
    m_photosList.clear();
    m_playlistsList.clear();
    m_photoAlbumsList.clear();
    m_photoFullPending.clear();
    m_artRequested.clear();
    m_artAliases.clear();
    m_importedAlbumArtTargets.clear();
    m_trackToAlbumArt.clear();
    m_trackToAlbumArtist.clear();
    m_tracks.setRows({});
}

void DeviceService::breach() {
    if (m_busy || m_connected)
        return;
    m_busy = true;
    m_status = QStringLiteral("Connecting (breach + MTPZ auth)…");
    emit stateChanged();
    emit workerBreach();
}

void DeviceService::onWorkerStage(const QString &label, double progress) {
    m_connectionStage = label;
    m_connectionProgress = progress;
    emit stateChanged();
}

// Port of ZuneColors.genreColor + deviceMoodColor: bias color by the
// device's dominant genre, 75% genre pick / 25% random surprise. The
// palette deliberately excludes brand pink/orange (the device is a
// visitor).
QColor DeviceService::pickMoodColor(const QVariantList &genres) {
    static const QColor palette[] = {
        QColor::fromRgbF(0.20, 0.50, 1.00), // electric blue
        QColor::fromRgbF(0.45, 0.95, 0.30), // acid green
        QColor::fromRgbF(0.55, 0.20, 0.90), // deep purple
        QColor::fromRgbF(0.95, 0.15, 0.20), // hot red
        QColor::fromRgbF(0.10, 0.85, 0.85), // cyan
        QColor::fromRgbF(0.95, 0.78, 0.20), // gold
        QColor::fromRgbF(0.95, 0.40, 0.70), // electric pink
    };
    const int n = int(std::size(palette));
    const QColor random = palette[QRandomGenerator::global()->bounded(n)];

    QString dominant;
    int best = -1;
    for (const QVariant &v : genres) {
        const QVariantMap m = v.toMap();
        if (m.value("count").toInt() > best) {
            best = m.value("count").toInt();
            dominant = m.value("name").toString();
        }
    }
    const QString g = dominant.toLower();
    QColor bias = palette[QRandomGenerator::global()->bounded(n)];
    if (g.contains("rock") || g.contains("metal") || g.contains("punk")
        || g.contains("hardcore") || g.contains("classical")
        || g.contains("jazz") || g.contains("blues") || g.contains("soul"))
        bias = palette[2]; // deep purple
    else if (g.contains("pop"))
        bias = palette[6]; // electric pink
    else if (g.contains("electro") || g.contains("edm") || g.contains("dance")
             || g.contains("house") || g.contains("techno"))
        bias = palette[4]; // cyan
    else if (g.contains("hip") || g.contains("hop") || g.contains("rap")
             || g.contains("r&b") || g.contains("rnb"))
        bias = palette[5]; // gold
    else if (g.contains("indie") || g.contains("alternative") || g.contains("folk"))
        bias = palette[1]; // acid green
    else if (g.contains("soundtrack") || g.contains("score") || g.contains("game"))
        bias = palette[0]; // electric blue

    const bool useGenre = QRandomGenerator::global()->bounded(100) < 75;
    return useGenre ? bias : random;
}

void DeviceService::onBreachDone(const DeviceWorker::BreachData &r) {
    m_busy = false;
    m_connectionStage.clear();
    m_connectionProgress = r.ok ? 1.0 : 0.0;
    if (!r.ok) {
        m_status = r.error;
        m_connectionError = r.error;
    } else {
        m_connectionError.clear();
        m_connected = true;
        m_name = r.name;
        m_model = r.model;
        m_serial = r.serial;
        m_family = r.family;
        m_battery = r.battery;
        m_capacityGB = r.capacityGB;
        m_freeGB = r.freeGB;
        m_trackCount = r.tracks.size();
        m_albumCount = r.albumCount;
        m_artistCount = r.artistCount;
        m_artistsList = r.artists;
        m_albumsList = r.albums;
        m_genresList = r.genres;
        m_videosList = r.videos;
        m_photosList = r.photos;
        m_photoAlbumsList = r.photoAlbums;
        m_playlistsList = r.playlists;
        m_trackToAlbumArt = r.trackToAlbumArt;
        m_trackToAlbumArtist = r.trackToAlbumArtist;
        m_tracks.setRows(r.tracks);
        // Storage breakdown for the stats tab — HD (Pavo) ONLY. Every
        // other generation's ZMDB carries no sizes at these offsets
        // (B1, verified on the 120's dump: track +16 is DURATION,
        // photo +12 is a FILETIME — summing those "sizes" produced
        // petabytes of phantom photos). Classic families show counts.
        // TODO(B1): batched GetObjectPropList ObjectSize sweep would
        // light real sizes up on every family.
        if (r.family == 0x06) {
            qint64 musicBytes = 0;
            for (const TrackRow &t : r.tracks)
                musicBytes += t.filesize;
            double videoMB = 0, photoMB = 0;
            for (const QVariant &v : std::as_const(m_videosList))
                videoMB += v.toMap().value(QStringLiteral("sizeMB")).toDouble();
            for (const QVariant &v : std::as_const(m_photosList))
                photoMB += v.toMap().value(QStringLiteral("sizeMB")).toDouble();
            m_musicGB = musicBytes / 1e9;
            m_videoGB = videoMB / 1e3;
            m_photoGB = photoMB / 1e3;
            fprintf(stderr, "[device] storage: %.1f/%.1f GB (used/total), "
                    "attributed: music %.2f + video %.2f + photo %.2f GB\n",
                    m_capacityGB - m_freeGB, m_capacityGB,
                    m_musicGB, m_videoGB, m_photoGB);
        } else {
            m_musicGB = m_videoGB = m_photoGB = 0;
            fprintf(stderr, "[device] storage: %.1f/%.1f GB (used/total) — "
                    "family 0x%02x ZMDB carries no per-item sizes; "
                    "stats show counts\n",
                    m_capacityGB - m_freeGB, m_capacityGB, r.family);
        }
        rebuildOnDeviceKeys();
        m_moodColor = pickMoodColor(r.genres);
        m_status = QStringLiteral("Connected");
    }
    emit stateChanged();
}

void DeviceService::sever() {
    if (m_busy || !m_connected)
        return;
    m_connected = false;
    clearDeviceState();
    m_status = m_devicePresent ? QStringLiteral("Disconnected (Zune still plugged in)")
                               : QStringLiteral("Waiting for a Zune…");
    emit stateChanged();
    emit workerSever();
}

void DeviceService::onSeverDone() {
    if (m_ejecting) {
        m_ejecting = false;
        m_awaitingDisconnect = false;  // auto-detect may reconnect now (mac)
        m_status = QStringLiteral("Disconnected — the Zune is re-indexing");
        emit stateChanged();
    }
    // Otherwise state was already cleared on request; nothing further.
}

void DeviceService::setAwaitingDisconnect(bool v) {
    if (m_awaitingDisconnect == v)
        return;
    m_awaitingDisconnect = v;
    if (v)
        m_status = QStringLiteral("Sync complete — eject to apply changes");
    emit stateChanged();
}

void DeviceService::eject() {
    emit ejectStarted();
    if (!m_connected || m_ejecting)
        return;
    // Mac disconnectDevice(): (sync forceStop is SyncEngine's duty),
    // finalize → sever → clear awaitingDisconnect.
    m_ejecting = true;
    m_connected = false;
    clearDeviceState();
    m_status = QStringLiteral("Finalizing — device will re-index…");
    emit stateChanged();
    emit workerFinalize();
}

void DeviceService::finalizeAndRelease() {
    if (!m_connected || m_ejecting)
        return;
    m_ejecting = true;  // reuse the finalize → sever chain
    m_connected = false;
    clearDeviceState();
    m_status = QStringLiteral("Sync complete — the Zune is re-indexing…");
    emit stateChanged();
    emit workerFinalize();
}

void DeviceService::onFinalizeDone(bool ok) {
    if (!m_ejecting)
        return;  // finalize during a sync flow — SyncEngine handles it
    if (!ok)
        fprintf(stderr, "[device] finalize reported failure — severing anyway\n");
    emit workerSever();
}

void DeviceService::requestArt(quint32 itemId) {
    if (itemId == 0 || !m_connected || m_artRequested.contains(itemId))
        return;
    m_artRequested.insert(itemId);

    // Track ids carry no art on the Zune — redirect to the MTP album
    // object that owns the track (the mac's getAlbumList() route).
    const quint32 target = m_trackToAlbumArt.value(itemId, itemId);

    const QString key = QString::number(target);
    if (m_artPaths.contains(key)) { // album already fetched — just alias
        m_artPaths.insert(QString::number(itemId), m_artPaths.value(key));
        emit artChanged();
        return;
    }

    m_artAliases[target].append(itemId);
    if (m_artAliases[target].size() > 1)
        return; // fetch already in flight for this album

    const QString path = m_artCacheDir + QStringLiteral("/%1.jpg").arg(target);
    emit workerGrabArt(target, path);
}

void DeviceService::requestImportedAlbumArt(quint32 itemId,
                                            const QString &artist,
                                            const QString &album) {
    if (itemId == 0 || album.trimmed().isEmpty() || !m_connected)
        return;
    const quint32 target = m_trackToAlbumArt.value(itemId, itemId);
    const QString targetKey = QString::number(target);
    const QString existingUrl = m_artPaths.value(targetKey).toString();
    if (!existingUrl.isEmpty()) {
        const QString path = QUrl(existingUrl).toLocalFile();
        if (!path.isEmpty())
            emit musicImportAlbumArtReady(artist, album, path);
        return;
    }

    QVariantList &waiting = m_importedAlbumArtTargets[target];
    const QString identity = artist.toCaseFolded() + QLatin1Char('\n')
        + album.toCaseFolded();
    bool alreadyWaiting = false;
    for (const QVariant &value : std::as_const(waiting)) {
        if (value.toMap().value(QStringLiteral("identity")).toString()
                == identity) {
            alreadyWaiting = true;
            break;
        }
    }
    if (!alreadyWaiting) {
        waiting.append(QVariantMap{{QStringLiteral("identity"), identity},
                                   {QStringLiteral("artist"), artist},
                                   {QStringLiteral("album"), album}});
    }

    if (m_artAliases.contains(target))
        return; // a device-browser request is already fetching this album

    // A prior browse miss must not suppress the import's one fresh attempt.
    m_artRequested.remove(itemId);
    requestArt(itemId);
}

QVariantList DeviceService::musicIdentityPeers(const QVariantMap &track) const {
    QVariantList out;
    for (const auto &peer : m_deviceMusicIndex.identitiesFor(MusicIdentity::fromMap(track))) out.append(MusicIdentity::toMap(peer));
    return out;
}

bool DeviceService::hasDeviceTrack(const QVariantMap &track, const QVariantList &sourcePeers) const {
    if (!m_connected) return false;
    QVector<MusicIdentity::Track> peers;
    peers.reserve(sourcePeers.size());
    for (const auto &peer : sourcePeers) peers.append(MusicIdentity::fromMap(peer.toMap()));
    return m_deviceMusicIndex.resolve(MusicIdentity::fromMap(track), peers).matched();
}

void DeviceService::rebuildOnDeviceKeys() {
    m_onDeviceKeys.clear();
    m_deviceMusicIndex.clear();
    const auto rows = m_tracks.rowsSnapshot();
    for (const auto &value : rows) {
        const auto row = value.toMap();
        m_deviceMusicIndex.add(MusicIdentity::fromMap(row), row.value(QStringLiteral("itemId")).toLongLong());
    }
    ++m_musicIdentityRevision;
    // Compatibility map only. New consumers use hasDeviceTrack with source
    // peers, so unknown readback cannot hide another disc with the same title.
    for (const auto &value : rows) {
        auto identity = MusicIdentity::fromMap(value.toMap());
        identity.discNumber = identity.trackNumber = 0;
        if (m_deviceMusicIndex.resolve(identity).matched())
            m_onDeviceKeys.insert(identity.artist + QLatin1Char('\t') + identity.album
                + QLatin1Char('\t') + identity.title, true);
    }
    m_photoOnDeviceKeys.clear();
    for (const QVariant &v : std::as_const(m_photosList))
        m_photoOnDeviceKeys.insert(
            photoStem(v.toMap().value(QStringLiteral("name")).toString()),
            true);
    if (!m_photoOnDeviceKeys.isEmpty())
        fprintf(stderr, "[device] %d photo stems on device, e.g. '%s'\n",
                int(m_photoOnDeviceKeys.size()),
                qPrintable(m_photoOnDeviceKeys.constBegin().key()));
}

void DeviceService::noteSyncedPhoto(const QString &name) {
    m_photosList.append(QVariantMap{{"itemId", 0u},
                                    {"name", name},
                                    {"parentId", 0u},
                                    {"sizeMB", 0.0}});
    m_photoOnDeviceKeys.insert(photoStem(name), true);
    emit stateChanged();
}

void DeviceService::noteSyncedTrack(const QString &title, const QString &artist,
                                    const QString &album, quint32 itemId, int discNumber,
                                    int trackNumber, int durationMs) {
    TrackRow row;
    row.itemId = itemId;
    row.title = title;
    row.artist = artist;
    row.album = album;
    row.discNumber = qMax(0,discNumber);
    row.trackNumber = qMax(0,trackNumber);
    row.durationMs = qMax(0,durationMs);
    row.trackNumberReliable = true;
    row.identityFromReadback = false;
    m_tracks.appendRow(row);
    m_trackCount = m_tracks.rowCount();
    rebuildOnDeviceKeys();
    emit stateChanged();
}

void DeviceService::renameItem(quint32 itemId, const QString &newName) {
    if (!m_connected || itemId == 0 || newName.trimmed().isEmpty())
        return;
    emit workerRenameItem(itemId, newName.trimmed());
}

void DeviceService::renameDevice(const QString &newName) {
    if (!m_connected || newName.trimmed().isEmpty())
        return;
    emit workerRenameDevice(newName.trimmed());
}

QString DeviceService::importsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
        + QStringLiteral("/Zuuned Imports");
}

void DeviceService::saveVideoToLibrary(quint32 itemId, const QString &filename,
                                       const QString &subdir) {
    if (!m_connected || itemId == 0)
        return;
    QString dir = importsDir();
    if (!subdir.isEmpty()) {
        QString safe = subdir;
        safe.replace(QLatin1Char('/'), QLatin1Char('_'));
        dir += QLatin1Char('/') + safe;
    }
    QDir().mkpath(dir);
    QVariantMap video{{"itemId", itemId}, {"name", filename}};
    for (const QVariant &entry : std::as_const(m_videosList)) {
        if (entry.toMap().value(QStringLiteral("itemId")).toUInt() == itemId) {
            video = entry.toMap();
            break;
        }
    }
    const QString fname = VideoIdentity::importFilename(video);
    emit workerExtractVideo(itemId, dir + QLatin1Char('/') + fname);
}

QString DeviceService::photoImportsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
        + QStringLiteral("/Zuuned Imports");
}

QString DeviceService::musicImportsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
        + QStringLiteral("/Zuuned Imports");
}

void DeviceService::savePhotoToLibrary(quint32 itemId, const QString &filename,
                                       const QString &subdir) {
    if (!m_connected || itemId == 0)
        return;
    QString dir = photoImportsDir();
    if (!subdir.isEmpty()) {
        QString safe = subdir;
        safe.replace(QLatin1Char('/'), QLatin1Char('_'));
        dir += QLatin1Char('/') + safe;
    }
    QDir().mkpath(dir);
    const QString fname = filename.isEmpty()
        ? QStringLiteral("photo_%1.jpg").arg(itemId) : filename;
    emit workerExtractPhoto(itemId, dir + QLatin1Char('/') + fname);
}

void DeviceService::saveTrackToLibrary(quint32 itemId, const QString &filename,
                                       const QString &artist,
                                       const QString &album) {
    if (!m_connected || itemId == 0)
        return;
    QString dir = musicImportsDir();
    for (const QString &part : {artist, album}) {
        if (part.isEmpty())
            continue;
        QString safe = part;
        safe.replace(QLatin1Char('/'), QLatin1Char('_'));
        dir += QLatin1Char('/') + safe;
    }
    QDir().mkpath(dir);
    QString fname = filename.isEmpty()
        ? QStringLiteral("track_%1.mp3").arg(itemId) : filename;
    fname.replace(QLatin1Char('/'), QLatin1Char('_'));
    // Device names carry no extension — the Zune stores MP3/WMA; MP3 is
    // what we smuggle, so default to it when the name is bare.
    if (!fname.contains(QLatin1Char('.')))
        fname += QStringLiteral(".mp3");
    const QString destPath = TrackImportPath::available(
        dir + QLatin1Char('/') + fname, m_pendingTrackDestinations);
    m_pendingTrackDestinations.insert(TrackImportPath::key(destPath));
    m_trackImportBatch.started(itemId, artist, album,
                               m_trackToAlbumArtist.value(itemId, artist));
    emit workerExtractTrack(itemId, destPath);
}

void DeviceService::beginPull(const QString &noun, int total, int existing) {
    m_pullNoun = noun.isEmpty() ? QStringLiteral("item") : noun;
    m_pullExisting = qMax(0, existing);
    m_pullFailed = 0;
    m_pullDone = 0;
    m_pullTotal = qMax(0, total);
    m_pulling = m_pullTotal > 0;
    if (m_pulling) {
        m_status = QStringLiteral("Pulling %1 %2%3 from your Zune…")
                       .arg(m_pullTotal)
                       .arg(m_pullNoun, m_pullTotal == 1 ? QString()
                                                         : QStringLiteral("s"));
    } else if (m_pullExisting > 0) {
        // Nothing to extract — everything dropped was already owned.
        m_status = m_pullExisting == 1
            ? QStringLiteral("That %1's already in your library").arg(m_pullNoun)
            : QStringLiteral("%1 %2s already in your library")
                  .arg(m_pullExisting).arg(m_pullNoun);
        emit pullToast(m_status);
    }
    emit pullChanged();
    emit stateChanged();
}

void DeviceService::notePullDone(bool ok) {
    if (!m_pulling)
        return;
    if (!ok)
        m_pullFailed++;
    m_pullDone++;
    if (m_pullDone < m_pullTotal) {
        emit pullChanged();
        return;
    }
    // Batch complete — an honest summary: pulled, failed, already-had.
    m_pulling = false;
    const int okCount = m_pullTotal - m_pullFailed;
    QString msg = QStringLiteral("Pulled %1 %2%3 to library")
                      .arg(okCount)
                      .arg(m_pullNoun, okCount == 1 ? QString()
                                                    : QStringLiteral("s"));
    if (m_pullFailed > 0)
        msg += QStringLiteral(" · %1 failed").arg(m_pullFailed);
    if (m_pullExisting > 0)
        msg += QStringLiteral(" · %1 already had").arg(m_pullExisting);
    m_status = msg;
    emit pullToast(msg);
    emit pullChanged();
    emit stateChanged();
}

QVariantMap DeviceService::trackInfo(quint32 itemId) const {
    for (int i = 0; i < m_tracks.rowCount(); ++i) {
        const auto index = m_tracks.index(i, 0);
        if (m_tracks.data(index, TrackModel::ItemIdRole).toUInt() != itemId) continue;
        return {{"itemId",itemId}, {"title",m_tracks.data(index,TrackModel::TitleRole)},
            {"artist",m_tracks.data(index,TrackModel::ArtistRole)},
            {"album",m_tracks.data(index,TrackModel::AlbumRole)},
            {"durationMs",m_tracks.data(index,TrackModel::DurationMsRole)},
            {"trackNumber",m_tracks.data(index,TrackModel::TrackNumberRole)},
            {"discNumber",m_tracks.data(index,TrackModel::DiscNumberRole)},
            {"trackNumberReliable",m_tracks.data(index,TrackModel::TrackNumberReliableRole)},
            {"discNumberReliable",m_tracks.data(index,TrackModel::DiscNumberReliableRole)},
            {"identityFromReadback",m_tracks.data(index,TrackModel::IdentityFromReadbackRole)}};
    }
    return {};
}

QVariantList DeviceService::deviceTrackRows() const { return m_tracks.rowsSnapshot(); }

void DeviceService::createPlaylist(const QString &name,
                                   const QVariantList &trackIds) {
    if (!m_connected || name.isEmpty())
        return;
    emit workerForgePlaylist(name, trackIds);
}

void DeviceService::requestPhotoFull(quint32 itemId) {
    if (!m_connected || itemId == 0 || m_photoFullPending.contains(itemId))
        return;
    const QString path =
        m_artCacheDir + QStringLiteral("/photofull_%1.jpg").arg(itemId);
    if (QFile::exists(path)) {
        emit photoFullReady(itemId, QUrl::fromLocalFile(path).toString());
        return;
    }
    m_photoFullPending.insert(itemId);
    emit workerExtractPhoto(itemId, path);
}

void DeviceService::noteSyncedVideo(const QVariantMap &video) {
    m_videosList.append(video);
    emit stateChanged();
}

bool DeviceService::udevRuleOk() const {
    return UdevSetup::installed(UdevSetup::rules());
}

QString DeviceService::udevInstallCommand() const {
    const QString script = UdevSetup::installScript(UdevSetup::rules());
    return script.isEmpty() ? QString() : QStringLiteral("sudo sh -c ") + UdevSetup::shellQuote(script);
}

void DeviceService::installUdevRule() {
    const QString script = UdevSetup::installScript(UdevSetup::rules());
    if (script.isEmpty()) {
        emit udevRuleInstalled(false, QStringLiteral("USB rule is missing from this build"));
        return;
    }

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        const bool ok = (code == 0) && udevRuleOk();
        if (ok) {
            emit udevRuleChanged();
            emit udevRuleInstalled(true,
                QStringLiteral("USB rule installed — replug the Zune if it "
                               "doesn't connect"));
        } else {
            // 126/127 = pkexec dismissed / no auth agent.
            emit udevRuleInstalled(false,
                (code == 126 || code == 127)
                    ? QStringLiteral("cancelled — run the command shown instead")
                    : QStringLiteral("couldn't install the rule (exit %1)")
                          .arg(code));
        }
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        emit udevRuleInstalled(false,
            QStringLiteral("pkexec not available — run the command shown"));
        proc->deleteLater();
    });
    proc->start(QStringLiteral("pkexec"),
                {QStringLiteral("sh"), QStringLiteral("-c"),
                 script});
}

bool DeviceService::purgeInterruptedVideo(const QString &filename) {
    if (!m_connected || m_busy || m_purging || m_pulling || filename.isEmpty()) return false;
    m_purging = true;
    m_purgeCurrent = 0;
    m_purgeTotal = 1;
    emit purgeChanged();
    emit workerPurgeInterruptedVideo(filename);
    return true;
}

void DeviceService::purgeItems(const QVariantList &itemIds) {
    if (!m_connected || itemIds.isEmpty())
        return;
    m_purging = true;
    m_purgeCurrent = 0;
    m_purgeTotal = itemIds.size();
    m_status = QStringLiteral("Deleting %1 item(s)…").arg(itemIds.size());
    emit purgeChanged();
    emit stateChanged();
    emit workerPurgeItems(itemIds);
}

bool DeviceService::purgeInterruptedObject(const QVariantMap &record) {
    if (!m_connected || m_busy || m_purging || record.value("serial").toString() != deviceSerial()) return false;
    m_purging = true;
    m_purgeCurrent = 0;
    m_purgeTotal = 1;
    emit purgeChanged();
    emit workerPurgeInterruptedObject(record);
    return true;
}

void DeviceService::onPurgeDone(int okCount, int failCount,
                                const QVariantList &purgedIds) {
    QSet<quint32> ids;
    for (const QVariant &v : purgedIds)
        ids.insert(v.toUInt());
    // W3: sum the freed bytes BEFORE the rows are dropped, then credit
    // them back so the capacity gate reflects the deletion immediately
    // (the device's own number won't update until the eject re-index).
    qint64 freed = m_tracks.sumFilesizeForIds(ids);
    for (const QVariant &v : std::as_const(m_videosList)) {
        const QVariantMap m = v.toMap();
        if (ids.contains(m.value(QStringLiteral("itemId")).toUInt()))
            freed += qint64(m.value(QStringLiteral("sizeMB")).toDouble() * 1e6);
    }
    for (const QVariant &v : std::as_const(m_photosList)) {
        const QVariantMap m = v.toMap();
        if (ids.contains(m.value(QStringLiteral("itemId")).toUInt()))
            freed += qint64(m.value(QStringLiteral("sizeMB")).toDouble() * 1e6);
    }
    noteBytesFreed(freed);
    m_tracks.removeByItemIds(ids);
    m_trackCount = m_tracks.rowCount();
    // Videos/photos live in flat lists — drop purged rows there too so
    // the device browse pages update without a re-breach.
    const auto dropPurged = [&ids](QVariantList &list) {
        for (int i = list.size() - 1; i >= 0; i--)
            if (ids.contains(list[i].toMap()
                                 .value(QStringLiteral("itemId")).toUInt()))
                list.removeAt(i);
    };
    dropPurged(m_videosList);
    dropPurged(m_photosList);
    pruneDevicePlaylists(m_playlistsList, ids);
    rebuildOnDeviceKeys();
    // Do NOT re-read the device here: right after DeleteObject the Zune
    // still reports its PRE-delete free space (it doesn't re-index until
    // the eject-time CleanDataStore), so a refresh would clobber the
    // noteBytesFreed credit above and the gauge would snap back to
    // "full". The optimistic credit IS the accurate number until the
    // next breach re-reads ground truth.
    m_purging = false;
    m_purgeCurrent = m_purgeTotal = 0;
    const QString freedStr = freed >= 1000000000LL
        ? QStringLiteral("%1 GB").arg(freed / 1e9, 0, 'f', 1)
        : QStringLiteral("%1 MB").arg(qRound(freed / 1e6));
    m_status = failCount == 0
        ? (freed > 0 ? QStringLiteral("Deleted %1 item(s) — freed %2")
                           .arg(okCount).arg(freedStr)
                     : QStringLiteral("Deleted %1 item(s)").arg(okCount))
        : QStringLiteral("Deleted %1, failed %2").arg(okCount).arg(failCount);
    emit purgeChanged();
    emit stateChanged();
}

void DeviceService::requestPhotoThumb(quint32 itemId) {
    if (itemId == 0 || !m_connected || m_artRequested.contains(itemId))
        return;
    m_artRequested.insert(itemId);
    const QString path = m_artCacheDir + QStringLiteral("/photo_%1.jpg").arg(itemId);
    emit workerGrabPhotoThumb(itemId, path);
}

void DeviceService::onArtDone(quint32 itemId, const QString &path, bool ok) {
    const QList<quint32> requesters = m_artAliases.take(itemId);
    const QVariantList importedAlbums = m_importedAlbumArtTargets.take(itemId);
    if (!ok)
        return; // no art — QML keeps the fallback rendering
    const QString url = QUrl::fromLocalFile(path).toString();
    m_artPaths.insert(QString::number(itemId), url);
    for (quint32 rid : requesters)
        m_artPaths.insert(QString::number(rid), url);
    emit artChanged();
    for (const QVariant &value : importedAlbums) {
        const QVariantMap album = value.toMap();
        emit musicImportAlbumArtReady(
            album.value(QStringLiteral("artist")).toString(),
            album.value(QStringLiteral("album")).toString(), path);
    }
}
