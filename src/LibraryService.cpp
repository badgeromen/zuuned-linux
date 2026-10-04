#include "LibraryService.h"

#include "library/AlbumArtService.h"
#include "library/OnlineAlbumArtService.h"
#include "library/ArtistImageService.h"
#include "library/ArtworkHttp.h"
#include "library/LibraryDb.h"
#include "library/LibraryScanner.h"
#include "library/LocalTrackModel.h"
#include "library/MusicIdentityClient.h"
#include "library/TmdbClient.h"
#include "library/VideoMatcher.h"
#include "library/VideoNaming.h"
#include "sync/SyncEngine.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStorageInfo>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

extern "C" {
#include "libav_transcode.h"
#include "id3_rewrite.h"
}

LibraryService::LibraryService(QObject *parent) : QObject(parent) {
    // Queued invoke into the matcher thread carries the batch by value.
    qRegisterMetaType<QVector<LibVideo>>("QVector<LibVideo>");

    m_db = new LibraryDb;
    m_db->open();

    // W24: migrate legacy pending notes into ordered DB slots. The first
    // reloadFromDb resolves any tracks that arrived before this launch.
    loadPendingPlaylistAdds();

    m_tracks = new LocalTrackModel(this);

    m_onlineAlbumArt = new OnlineAlbumArtService(this);
    connect(m_onlineAlbumArt, &OnlineAlbumArtService::discoveryResult, this,
        [this](const QString &artist, const QString &album, const QVariantMap &result) {
            if (m_albumArtSources.contains(artKey(artist, album)))
                recordArtworkResult(QStringLiteral("album"), {{"artist", artist}, {"album", album}}, result);
        });
    connect(m_onlineAlbumArt, &OnlineAlbumArtService::finished, this,
            [this](const QString &artist, const QString &album, const QString &path, bool ok) {
        const QString key = artKey(artist, album);
        if (!ok || !m_albumArtSources.contains(key)) return;
        // Apply already publishes a versioned URL; leave that authoritative.
        if (!m_artPaths.value(key).toString().contains(QStringLiteral("?v="))) {
            m_artPaths.insert(key, QUrl::fromLocalFile(path).toString());
            emit artChanged();
        }
    });
    m_artworkDiscovery = new QTimer(this);
    m_artworkDiscovery->setSingleShot(true);
    m_artworkDiscovery->setInterval(750);
    connect(m_artworkDiscovery, &QTimer::timeout, this, &LibraryService::discoverMissingArtwork);
    m_art = new AlbumArtService(this);
    connect(m_art, &AlbumArtService::artReady, this,
            [this](const QString &artist, const QString &album,
                   const QString &path, bool ok) {
        if (!ok) {
            if (m_albumArtSources.contains(artKey(artist, album)) && !m_art->isResolving(artist, album)) {
                const QVariantMap context{{"artist", artist}, {"album", album}};
                const auto input = artworkContext(QStringLiteral("album"), context);
                if (!restoreArtworkResult(QStringLiteral("album"), context, input))
                    m_onlineAlbumArt->request(artist, album, input);
            }
            return;
        }
        recordArtworkResult(QStringLiteral("album"), {{"artist", artist}, {"album", album}}, {{"status", "ready"}});
        const QString url = QUrl::fromLocalFile(path).toString();
        if (m_artPaths.value(artKey(artist, album)).toString()
                .startsWith(url + QStringLiteral("?v=")))
            return; // A later Apply already published this path with a fresh cache key.
        m_artPaths.insert(artKey(artist, album),
                          url);
        emit artChanged();
    });

    m_scanner = new LibraryScanner;
    m_scanner->moveToThread(&m_scanThread);
    connect(&m_scanThread, &QThread::finished, m_scanner, &QObject::deleteLater);
    connect(m_scanner, &LibraryScanner::musicProbeFailuresChanged,
            this, &LibraryService::musicProbeFailuresChanged);

    connect(m_scanner, &LibraryScanner::progress, this,
            [this](const QString &stage, int current, int total,
                   const QString &file) {
        m_scanStage = stage;
        m_scanCurrent = current;
        m_scanTotal = total;
        m_scanCurrentFile = file;
        emit scanChanged();
    });
    connect(m_scanner, &LibraryScanner::chunkCommitted, this,
            [this] {
        m_scanCommittedChunk = true;   // real content landed this scan
        reloadFromDb();
        reloadVideos();
        pokeVideoConsumers();
    });
    connect(m_scanner, &LibraryScanner::finished, this,
            [this](int tracksAdded, int tracksUpdated, int photosAdded) {
        m_scanning = false;
        m_scanStage.clear();
        m_scanCurrentFile.clear();
        emit scanChanged();
        // Settings → Library "Duplicate formats": every scan ends with
        // a resolve pass, so flipping back to keepBoth + rescan
        // resurrects the dropped format.
        const QString dupPref = QSettings().value(
            QStringLiteral("duplicateFormats"),
            QStringLiteral("keepBoth")).toString();
        const int dupDropped = m_db->resolveDuplicateFormats(dupPref);
        if (dupDropped > 0)
            fprintf(stderr, "[library] duplicate formats: dropped %d %s"
                            " twins\n", dupDropped,
                    qPrintable(dupPref == QLatin1String("flac")
                               ? QStringLiteral("mp3")
                               : QStringLiteral("flac")));
        // Only reset the views when the scan ACTUALLY changed the
        // library. A no-op periodic sweep (nothing added/updated/
        // dropped, no chunk committed) must not reload — that reload
        // was resetting every grid and snapping scroll to top every
        // 5 minutes (2026-09-08).
        const bool changed = tracksAdded > 0 || tracksUpdated > 0
                             || photosAdded > 0 || dupDropped > 0
                             || m_scanCommittedChunk;
        if (changed) {
            reloadFromDb();
            reloadVideos();
        }
        m_scanCommittedChunk = false;
        pokeVideoConsumers();
        if (tracksAdded > 0 || photosAdded > 0)
            emit scanFoundNew(tracksAdded, photosAdded);
        // Folders added during this pass queued a follow-up.
        if (m_rescanPending) {
            m_rescanPending = false;
            rescan();
        }
    });
    connect(m_scanner, &LibraryScanner::failed, this,
            [this](const QString &err) {
        m_scanning = false;
        m_scanStage = err;
        emit scanChanged();
        if (m_rescanPending) {
            m_rescanPending = false;
            rescan();
        }
    });

    m_artistImagesSvc = new ArtistImageService(this);
    connect(m_artistImagesSvc, &ArtistImageService::discoveryResult, this,
        [this](const QString &name, const QVariantMap &result) {
            bool exists = false;
            for (const auto &row : std::as_const(m_artistsList))
                exists |= row.toMap().value("name").toString().compare(name, Qt::CaseInsensitive) == 0;
            if (exists) recordArtworkResult(QStringLiteral("artist"), {{"name", name}}, result);
        });
    connect(m_artistImagesSvc, &ArtistImageService::imageReady, this,
            [this](const QString &name, const QString &path, bool ok) {
        if (!ok)
            return;
        const QString url = QUrl::fromLocalFile(path).toString();
        if (m_artistImages.value(name.toLower()).toString()
                .startsWith(url + QStringLiteral("?v=")))
            return;
        m_artistImages.insert(name.toLower(),
                              url);
        emit artistImagesChanged();
    });

    m_scanThread.start();

    // ── Video matcher (own thread — blocking TMDB client) ──
    m_matcher = new VideoMatcher(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library.db"));
    m_matcher->moveToThread(&m_matcherThread);
    connect(&m_matcherThread, &QThread::finished, m_matcher, &QObject::deleteLater);
    connect(m_matcher, &VideoMatcher::updatesCommitted, this,
            [this](int) { reloadVideos(); });
    connect(m_matcher, &VideoMatcher::matchBatchFinished, this, [this] {
        m_matchBusy = false;
        // 500ms breather between rounds; backoff prevents re-picking the
        // same failed rows immediately.
        QTimer::singleShot(500, this, [this] { pokeVideoConsumers(); });
    });
    m_matcherThread.start();

    // Idle re-check keeps consumers draining while a scan streams new
    // rows in (the scanner emits chunkCommitted, but a chunk can be all
    // fast-path skips).
    m_consumerTimer = new QTimer(this);
    m_consumerTimer->setInterval(1500);
    connect(m_consumerTimer, &QTimer::timeout, this,
            [this] { pokeVideoConsumers(); });

    reloadFolders();
    ensureGvfsMounts();
    reloadFromDb();
    reloadVideos();
    // Drain any backlog left from a previous run (unprobed rows, rows
    // whose lookup was interrupted).
    QTimer::singleShot(2000, this, [this] { pokeVideoConsumers(); });

    // ── The library keeps itself (fresh-run feedback) ──
    // 1. Launch rescan: files added while the app was closed are found
    //    automatically — incremental scans ride the mtime fast-path,
    //    so this is near-free (measured instant at 40k tracks).
    QTimer::singleShot(3500, this, [this] {
        if (!m_watchFolders.isEmpty() && !scanning()) {
            fprintf(stderr, "[library] launch sweep: looking for files "
                            "added while we were away\n");
            rescan();
        }
    });
    // 2. Live watching: the watch-folder ROOTS react instantly to
    //    top-level changes; deep additions are caught by the sweep.
    m_watchDebounce = new QTimer(this);
    m_watchDebounce->setSingleShot(true);
    m_watchDebounce->setInterval(5000);   // let copies finish landing
    connect(m_watchDebounce, &QTimer::timeout, this, [this] {
        if (!scanning()) {
            fprintf(stderr, "[library] folder change detected — "
                            "incremental rescan\n");
            rescan();
        }
    });
    m_fsWatcher = new QFileSystemWatcher(this);
    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { m_watchDebounce->start(); });
    rearmFolderWatcher();
    // 3. Periodic sweep for changes deep in the tree (every 5 min,
    //    incremental — effectively free when nothing changed).
    m_periodicSweep = new QTimer(this);
    m_periodicSweep->setInterval(5 * 60 * 1000);
    connect(m_periodicSweep, &QTimer::timeout, this, [this] {
        if (!m_watchFolders.isEmpty() && !scanning())
            rescan();
    });
    m_periodicSweep->start();
}

void LibraryService::rearmFolderWatcher() {
    if (!m_fsWatcher)
        return;
    const QStringList old = m_fsWatcher->directories();
    if (!old.isEmpty())
        m_fsWatcher->removePaths(old);
    QStringList roots;
    for (const QVariant &fv : m_watchFolders) {
        const QString p = fv.toMap().value(QStringLiteral("path")).toString();
        if (QFileInfo::exists(p))
            roots << p;
    }
    if (!roots.isEmpty())
        m_fsWatcher->addPaths(roots);
}

void LibraryService::requestArtistImage(const QString &name) {
    const QVariantMap context{{"name", name}};
    const auto input = artworkContext(QStringLiteral("artist"), context);
    if (m_artistImagesSvc->cachedPath(name).isEmpty()
        && restoreArtworkResult(QStringLiteral("artist"), context, input)) return;
    m_artistImagesSvc->requestImage(name, input);
}

namespace {
bool hasManualCollectionChoice(const QVariantMap &choice) {
    auto identity = choice.value("identity").toMap();
    identity.remove("_artworkDiscovery");
    identity.remove("_artworkChoiceRevision");
    return !identity.isEmpty() || !choice.value("artPath").toString().isEmpty();
}
QString artworkStorageKey(const QString &kind, const QVariantMap &context) {
    return kind == QLatin1String("album")
        ? AlbumArtService::cacheKey(context.value("artist").toString(), context.value("album").toString())
        : ArtistImageService::cacheKey(context.value("name").toString());
}
QString artworkDisplayKey(const QString &kind, const QVariantMap &context) {
    return kind == QLatin1String("album")
        ? LibraryService::artKey(context.value("artist").toString(), context.value("album").toString())
        : context.value("name").toString().toLower();
}
}

QVariantMap LibraryService::artworkContext(const QString &kind, const QVariantMap &context) {
    const auto saved = m_db->collectionCustomization(kind, artworkStorageKey(kind, context));
    auto identity = saved.value("identity").toMap();
    const auto discovery = identity.take("_artworkDiscovery").toMap();
    const auto chosen = identity.take("_artworkMatch").toMap();
    const auto choiceRevision = identity.take("_artworkChoiceRevision");
    if (!chosen.isEmpty()) identity = chosen;
    if (kind == QLatin1String("album")) {
        const auto evidence = m_albumMatchingEvidence.value(artworkDisplayKey(kind, context));
        for (auto it = evidence.cbegin(); it != evidence.cend(); ++it) identity.insert(it.key(), it.value());
        auto artistChoice = m_db->collectionCustomization(QStringLiteral("artist"),
            ArtistImageService::cacheKey(context.value("artist").toString())).value("identity").toMap();
        const auto portraitMatch = artistChoice.take("_artworkMatch").toMap();
        artistChoice.remove("_artworkDiscovery");
        // An explicitly borrowed portrait is not evidence about album credits.
        if (artistChoice.value("providerId").toString().isEmpty() && portraitMatch.isEmpty())
            artistChoice = artworkContext(QStringLiteral("artist"), {{"name", context.value("artist")}});
        if (artistChoice.value("provider").toString() == QLatin1String("musicbrainz"))
            identity.insert("artistMbid", artistChoice.value("providerId", artistChoice.value("mbid")));
    } else {
        QStringList albums;
        for (const auto &value : std::as_const(m_albumsList)) {
            const auto row = value.toMap();
            if (row.value("artArtist").toString().compare(context.value("name").toString(), Qt::CaseInsensitive) == 0)
                albums.append(row.value("artAlbum").toString());
        }
        albums.removeDuplicates(); albums.sort(Qt::CaseInsensitive);
        identity.insert("_libraryAlbums", albums);
    }
    if (choiceRevision.isValid()) identity.insert("_artworkChoiceRevision", choiceRevision);
    const auto fingerprint = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (chosen.isEmpty() && identity.value("providerId").toString().isEmpty()
        && discovery.value("fingerprint").toString() == fingerprint) {
        const auto resolved = discovery.value("identity").toMap();
        for (auto it = resolved.cbegin(); it != resolved.cend(); ++it) identity.insert(it.key(), it.value());
    }
    identity.insert("_evidenceFingerprint", fingerprint);
    return identity;
}

bool LibraryService::restoreArtworkResult(const QString &kind, const QVariantMap &context, const QVariantMap &input) {
    const QString storageKey = artworkStorageKey(kind, context);
    if (m_forceArtworkRetry.contains(kind + storageKey)) return false;
    const auto saved = m_db->collectionCustomization(kind, storageKey).value("identity").toMap()
        .value("_artworkDiscovery").toMap();
    const QString status = saved.value("status").toString();
    if (saved.value("fingerprint") != input.value("_evidenceFingerprint")
        || (status != QLatin1String("needsMatch") && status != QLatin1String("noArtwork"))) return false;
    auto &states = kind == QLatin1String("album") ? m_albumArtworkStatus : m_artistArtworkStatus;
    const QString key = artworkDisplayKey(kind, context);
    if (states.value(key).toMap() != saved) { states.insert(key, saved); emit artworkStatusChanged(); }
    return true;
}

void LibraryService::recordArtworkResult(const QString &kind, const QVariantMap &context, const QVariantMap &result) {
    auto &states = kind == QLatin1String("album") ? m_albumArtworkStatus : m_artistArtworkStatus;
    const QString key = artworkDisplayKey(kind, context);
    if (states.value(key).toMap() != result) { states.insert(key, result); emit artworkStatusChanged(); }
    const QString status = result.value("status").toString();
    if (status == QLatin1String("lookingUp") || status == QLatin1String("checkingLocal") || status.isEmpty()) return;
    const QString storageKey = artworkStorageKey(kind, context);
    m_forceArtworkRetry.remove(kind + storageKey);
    // A cache hit without matching evidence cannot replace saved provenance.
    if (status == QLatin1String("ready") && result.value("identity").toMap().isEmpty()) return;
    auto stored = result;
    stored.insert("fingerprint", artworkContext(kind, context).value("_evidenceFingerprint"));
    const auto expected = m_db->collectionCustomization(kind, storageKey);
    auto prior = expected.value("identity").toMap().value("_artworkDiscovery").toMap();
    prior.remove("recordedAt"); prior.remove("revision");
    if (prior == stored) return;
    stored.insert("recordedAt", QDateTime::currentMSecsSinceEpoch());
    stored.insert("revision", QVariant::fromValue(++m_artworkResultRevision));
    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library.db");
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [watcher] {
        if (!watcher->result()) fprintf(stderr, "[artwork] discovery state not saved; it will be checked again next session\n");
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([dbPath, kind, storageKey, expected, stored] {
        LibraryDb db;
        if (!db.open(dbPath) || !db.begin()) return false;
        const auto current = db.collectionCustomization(kind, storageKey);
        // Concurrent discovery records can advance, but a manual Apply wins.
        auto expectedIdentity = expected.value("identity").toMap();
        expectedIdentity.remove("_artworkDiscovery");
        auto identity = current.value("identity").toMap();
        const auto latest = identity.take("_artworkDiscovery").toMap();
        const auto newer = latest.value("recordedAt").toLongLong() > stored.value("recordedAt").toLongLong()
            || (latest.value("recordedAt") == stored.value("recordedAt")
                && latest.value("revision").toULongLong() > stored.value("revision").toULongLong());
        if (identity != expectedIdentity || current.value("artPath").toString() != expected.value("artPath").toString() || newer) {
            db.rollback(); return true;
        }
        identity.insert("_artworkDiscovery", stored);
        if (!db.setCollectionCustomization(kind, storageKey, identity, current.value("artPath").toString())) {
            db.rollback(); return false;
        }
        return db.commit();
    }));
}

void LibraryService::retryArtwork(const QString &kind, const QVariantMap &context) {
    if (kind != QLatin1String("album") && kind != QLatin1String("artist")) return;
    m_forceArtworkRetry.insert(kind + artworkStorageKey(kind, context));
    if (kind == QLatin1String("album")) {
        const QString artist = context.value("artist").toString(), album = context.value("album").toString();
        m_onlineAlbumArt->invalidate(artist, album);
        m_art->forgetFailure(artist, album);
        requestArt(artist, album, {});
    } else {
        const QString name = context.value("name").toString();
        m_artistImagesSvc->forgetFailure(name);
        requestArtistImage(name);
    }
}

void LibraryService::savePosition(qint64 libraryId, int positionMs) {
    if (libraryId >= 0 && m_db)
        m_db->updateTrackPosition(libraryId, positionMs);
}

// ── Photos (Phase 8) ──

QVariantList LibraryService::photoAlbums(const QStringList &onDeviceStems) {
    QVariantList out;
    if (!m_db)
        return out;
    const QSet<QString> stems(onDeviceStems.begin(), onDeviceStems.end());
    // Group by parent directory — the mac's folder-album rule.
    QMap<QString, QVariantList> byDir;  // path -> photo urls (sorted keys)
    QMap<QString, int> counts, zuneCounts;
    const auto photos = m_db->allPhotos();
    for (const LibPhoto &p : photos) {
        const QString dir = QFileInfo(p.filepath).absolutePath();
        counts[dir]++;
        const int dot = p.filename.lastIndexOf(QLatin1Char('.'));
        const QString stem =
            (dot > 0 ? p.filename.left(dot) : p.filename).toLower();
        if (stems.contains(stem))
            zuneCounts[dir]++;
        if (byDir[dir].size() < 4)
            byDir[dir].append(QUrl::fromLocalFile(p.filepath).toString());
    }
    for (auto it = byDir.constBegin(); it != byDir.constEnd(); ++it) {
        QVariantMap a;
        a[QStringLiteral("name")] = QFileInfo(it.key()).fileName();
        a[QStringLiteral("path")] = it.key();
        a[QStringLiteral("count")] = counts[it.key()];
        a[QStringLiteral("onZune")] = zuneCounts.value(it.key(), 0);
        a[QStringLiteral("covers")] = it.value();
        out.append(a);
    }
    return out;
}

QVariantList LibraryService::photosInAlbum(const QString &path) {
    QVariantList out;
    if (!m_db)
        return out;
    const auto photos = m_db->allPhotos();
    QVector<LibPhoto> hits;
    for (const LibPhoto &p : photos)
        if (QFileInfo(p.filepath).absolutePath() == path)
            hits.append(p);
    std::sort(hits.begin(), hits.end(),
              [](const LibPhoto &a, const LibPhoto &b) {
                  return QString::compare(a.filename, b.filename,
                                          Qt::CaseInsensitive) < 0;
              });
    for (const LibPhoto &p : hits) {
        QVariantMap m;
        m[QStringLiteral("id")] = double(p.id);
        m[QStringLiteral("filename")] = p.filename;
        m[QStringLiteral("url")] = QUrl::fromLocalFile(p.filepath).toString();
        m[QStringLiteral("filesize")] = double(p.filesize);
        m[QStringLiteral("mtime")] = double(p.mtime);
        out.append(m);
    }
    return out;
}

namespace {
bool validPhotoId(double id, bool root = false) {
    return std::isfinite(id) && id >= (root ? 0 : 1)
        && id <= 9007199254740991.0 && std::floor(id) == id;
}

QVariantMap photoRow(const LibPhoto &p) {
    return {{QStringLiteral("id"), double(p.id)},
            {QStringLiteral("filename"), p.filename},
            {QStringLiteral("url"), QUrl::fromLocalFile(p.filepath).toString()},
            {QStringLiteral("filesize"), double(p.filesize)},
            {QStringLiteral("mtime"), double(p.mtime)}};
}

bool photoIdsFromVariants(const QVariantList &values, QVector<qint64> *ids) {
    for (const auto &value : values) {
        bool ok = false;
        const double id = value.toDouble(&ok);
        if (!ok || !validPhotoId(id)) return false;
        ids->append(qint64(id));
    }
    return true;
}

QVariantMap invalidPhotoAlbumRequest() {
    return {{QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("The album or photo selection is invalid.")}};
}
}

QVariantList LibraryService::customPhotoAlbums(const QStringList &onDeviceStems) {
    if (!m_db || !m_db->isOpen()) return {};
    QVariantList albums = m_db->allPhotoAlbums();
    QHash<qint64, QVariantMap> byId;
    QHash<qint64, LibPhoto> photos;
    const QSet<QString> stems(onDeviceStems.begin(), onDeviceStems.end());
    for (const auto &row : albums) {
        const auto album = row.toMap();
        byId.insert(album.value(QStringLiteral("id")).toLongLong(), album);
    }
    for (const auto &photo : m_db->allPhotos()) photos.insert(photo.id, photo);
    for (auto &row : albums) {
        auto album = row.toMap();
        const qint64 id = album.value(QStringLiteral("id")).toLongLong();
        QStringList path;
        QSet<qint64> visited;
        qint64 cursor = id;
        while (cursor && byId.contains(cursor) && !visited.contains(cursor)) {
            visited.insert(cursor);
            const auto ancestor = byId.value(cursor);
            path.prepend(ancestor.value(QStringLiteral("name")).toString());
            cursor = ancestor.value(QStringLiteral("parentId")).toLongLong();
        }
        album.insert(QStringLiteral("pathLabel"), path.join(QStringLiteral(" / ")));
        QVariantList covers;
        int onZune = 0;
        const auto ids = m_db->photoAlbumPhotoIds(id);
        for (const auto photoId : ids) {
            if (!photos.contains(photoId)) continue;
            const auto &photo = photos[photoId];
            if (covers.size() < 4)
                covers.append(QUrl::fromLocalFile(photo.filepath).toString());
            const int dot = photo.filename.lastIndexOf(QLatin1Char('.'));
            const auto stem = (dot > 0 ? photo.filename.left(dot) : photo.filename).toLower();
            if (stems.contains(stem)) ++onZune;
        }
        album.insert(QStringLiteral("covers"), covers);
        album.insert(QStringLiteral("onZune"), onZune);
        row = album;
    }
    return albums;
}

QVariantList LibraryService::customAlbumPhotos(double id) {
    if (!m_db || !m_db->isOpen() || !validPhotoId(id)) return {};
    QHash<qint64, LibPhoto> photos;
    for (const auto &photo : m_db->allPhotos()) photos.insert(photo.id, photo);
    QVariantList result;
    for (const qint64 photoId : m_db->photoAlbumPhotoIds(qint64(id)))
        if (photos.contains(photoId)) result.append(photoRow(photos.value(photoId)));
    return result;
}

QVariantMap LibraryService::photoAlbumResult(bool success) {
    if (success) emit libraryChanged();
    QString error;
    if (!success) {
        error = m_db ? m_db->lastError() : QString();
        if (error.isEmpty()) error = QStringLiteral("The photo album could not be saved.");
    }
    return {{QStringLiteral("success"), success}, {QStringLiteral("error"), error}};
}

QVariantMap LibraryService::createPhotoAlbum(const QString &name, double parentId,
                                            const QVariantList &photoIds) {
    QVector<qint64> ids;
    if (!validPhotoId(parentId, true) || !photoIdsFromVariants(photoIds, &ids))
        return invalidPhotoAlbumRequest();
    if (!m_db || !m_db->isOpen() || !m_db->begin()) return photoAlbumResult(false);
    const qint64 id = m_db->createPhotoAlbum(name, qint64(parentId));
    const bool success = id > 0 && m_db->addPhotoAlbumPhotos(id, ids) && m_db->commit();
    if (!success) {
        // Preserve the actual failure before rollback can replace diagnostics.
        auto result = photoAlbumResult(false);
        m_db->rollback();
        result.insert(QStringLiteral("id"), -1);
        return result;
    }
    auto result = photoAlbumResult(true);
    result.insert(QStringLiteral("id"), double(id));
    return result;
}

QVariantMap LibraryService::renamePhotoAlbum(double id, const QString &name) {
    if (!validPhotoId(id)) return invalidPhotoAlbumRequest();
    return photoAlbumResult(m_db && m_db->isOpen() && m_db->renamePhotoAlbum(qint64(id), name));
}

QVariantMap LibraryService::photoAlbumSnapshot(double id) {
    if (!m_db || !m_db->isOpen() || !validPhotoId(id)) return {};
    for (const auto &row : m_db->allPhotoAlbums()) {
        const auto album = row.toMap();
        if (album.value(QStringLiteral("id")).toLongLong() != qint64(id)) continue;
        QVariantList photoIds;
        for (const auto photoId : m_db->photoAlbumPhotoIds(qint64(id))) photoIds.append(double(photoId));
        return {{QStringLiteral("id"), id}, {QStringLiteral("name"), album.value(QStringLiteral("name"))},
                {QStringLiteral("parentId"), double(album.value(QStringLiteral("parentId")).toLongLong())},
                {QStringLiteral("photoIds"), photoIds}};
    }
    return {};
}

QVariantMap LibraryService::savePhotoAlbumDraft(double id, const QString &name, double parentId,
                                               const QVariantList &photoIds,
                                               const QVariantMap &originalSnapshot) {
    QVector<qint64> ids;
    if ((id != -1 && !validPhotoId(id)) || !validPhotoId(parentId, true)
        || !photoIdsFromVariants(photoIds, &ids)
        || QSet<qint64>(ids.begin(), ids.end()).size() != ids.size()) {
        auto result = invalidPhotoAlbumRequest();
        result.insert(QStringLiteral("id"), id);
        return result;
    }
    if (!m_db || !m_db->isOpen() || !m_db->begin()) {
        auto result = photoAlbumResult(false);
        result.insert(QStringLiteral("id"), id);
        return result;
    }
    auto fail = [&](const QString &error = QString()) {
        auto result = photoAlbumResult(false);
        if (!error.isEmpty()) result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("id"), id);
        m_db->rollback();
        return result;
    };
    qint64 savedId = qint64(id);
    QVector<qint64> previous;
    if (id != -1) {
        const auto current = photoAlbumSnapshot(id);
        QVector<qint64> originalIds;
        bool validOriginalId = false, validOriginalParent = false;
        const double originalId = originalSnapshot.value(QStringLiteral("id")).toDouble(&validOriginalId);
        const double originalParent = originalSnapshot.value(QStringLiteral("parentId")).toDouble(&validOriginalParent);
        if (current.isEmpty() || !validOriginalId || originalId != id || !validOriginalParent
            || !validPhotoId(originalParent, true)
            || originalSnapshot.value(QStringLiteral("photoIds")).metaType().id() != QMetaType::QVariantList
            || !photoIdsFromVariants(originalSnapshot.value(QStringLiteral("photoIds")).toList(), &originalIds)
            || current.value(QStringLiteral("name")).toString() != originalSnapshot.value(QStringLiteral("name")).toString()
            || current.value(QStringLiteral("parentId")).toDouble() != originalParent
            || !photoIdsFromVariants(current.value(QStringLiteral("photoIds")).toList(), &previous)
            || originalIds != previous)
            return fail(QStringLiteral("This album changed or was deleted while editing. Reopen it before saving."));
        if (!m_db->renamePhotoAlbum(savedId, name) || !m_db->movePhotoAlbum(savedId, qint64(parentId))) return fail();
    } else {
        savedId = m_db->createPhotoAlbum(name, qint64(parentId));
        if (savedId <= 0) return fail();
    }
    const QSet<qint64> wanted(ids.begin(), ids.end());
    QVector<qint64> removed;
    for (const auto photoId : previous) if (!wanted.contains(photoId)) removed.append(photoId);
    if (!m_db->removePhotoAlbumPhotos(savedId, removed) || !m_db->addPhotoAlbumPhotos(savedId, ids)
        || !m_db->reorderPhotoAlbumPhotos(savedId, ids) || !m_db->commit()) return fail();
    auto result = photoAlbumResult(true);
    result.insert(QStringLiteral("id"), double(savedId));
    return result;
}

QVariantMap LibraryService::movePhotoAlbum(double id, double parentId) {
    if (!validPhotoId(id) || !validPhotoId(parentId, true)) return invalidPhotoAlbumRequest();
    return photoAlbumResult(m_db && m_db->isOpen()
                           && m_db->movePhotoAlbum(qint64(id), qint64(parentId)));
}

QVariantMap LibraryService::deletePhotoAlbum(double id) {
    if (!validPhotoId(id)) return invalidPhotoAlbumRequest();
    return photoAlbumResult(m_db && m_db->isOpen() && m_db->deletePhotoAlbum(qint64(id)));
}

QVariantMap LibraryService::addPhotosToAlbum(double id, const QVariantList &photoIds) {
    QVector<qint64> ids;
    if (!validPhotoId(id) || !photoIdsFromVariants(photoIds, &ids)) return invalidPhotoAlbumRequest();
    return photoAlbumResult(m_db && m_db->isOpen() && m_db->addPhotoAlbumPhotos(qint64(id), ids));
}

QVariantMap LibraryService::removePhotosFromAlbum(double id, const QVariantList &photoIds) {
    QVector<qint64> ids;
    if (!validPhotoId(id) || !photoIdsFromVariants(photoIds, &ids)) return invalidPhotoAlbumRequest();
    return photoAlbumResult(m_db && m_db->isOpen() && m_db->removePhotoAlbumPhotos(qint64(id), ids));
}

QVariantMap LibraryService::setPhotoAlbumOrder(double id, const QVariantList &photoIds) {
    QVector<qint64> ids;
    if (!validPhotoId(id) || !photoIdsFromVariants(photoIds, &ids)) return invalidPhotoAlbumRequest();
    return photoAlbumResult(m_db && m_db->isOpen() && m_db->reorderPhotoAlbumPhotos(qint64(id), ids));
}

void LibraryService::deletePhotos(const QVariantList &ids) {
    if (!m_db)
        return;
    for (const QVariant &v : ids)
        m_db->removePhoto(qint64(v.toDouble()), /*exclude=*/true);
    m_photoCount = m_db->allPhotos().size();
    emit libraryChanged();
}

void LibraryService::saveVideoPosition(qint64 libraryId, int positionMs,
                                       int durationMs) {
    if (libraryId < 0 || !m_db)
        return;
    m_db->updateVideoPosition(libraryId, positionMs);
    if (durationMs > 0 && positionMs >= durationMs / 10 * 9)
        m_db->setVideosWatched({libraryId}, true);
}

// gvfs mounts (SMB/NFS shares picked via the file chooser) vanish at
// logout. Remount any watch folder living under the gvfs FUSE dir so
// the library doesn't silently disappear after a reboot. Anonymous
// first — matches how the share was mounted when it was added.
void LibraryService::ensureGvfsMounts() {
    static const QRegularExpression re(
        QStringLiteral("^/run/user/\\d+/gvfs/smb-share:server=([^,]+),share=([^/]+)"));
    for (const QVariant &v : std::as_const(m_watchFolders)) {
        const QString path = v.toMap().value("path").toString();
        const auto m = re.match(path);
        if (!m.hasMatch() || QDir(path).exists())
            continue;
        const QString url = QStringLiteral("smb://%1/%2/")
                            .arg(m.captured(1), m.captured(2));
        fprintf(stderr, "[library] remounting gvfs share for watch folder: %s\n",
                qPrintable(url));
        QProcess::startDetached(QStringLiteral("gio"),
                                {QStringLiteral("mount"), QStringLiteral("-a"), url});
    }
}

LibraryService::~LibraryService() {
    m_scanner->cancel();
    m_scanThread.quit();
    m_scanThread.wait(5000);
    m_matcherThread.quit();
    m_matcherThread.wait(5000);
    delete m_db;
}

bool LibraryService::empty() const {
    // Mac onboarding rule: empty = no watch folders AND no content
    return m_watchFolders.isEmpty() && trackCount() == 0 && m_photoCount == 0;
}

int LibraryService::trackCount() const { return m_tracks->rowCount(); }
int LibraryService::photoCount() const { return m_photoCount; }
QObject *LibraryService::tracksModel() const { return m_tracks; }

void LibraryService::addWatchFolder(const QString &path, const QString &type) {
    const QString local = QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path;
    if (local.isEmpty())
        return;
    m_db->addWatchFolder(local, type.isEmpty() ? QStringLiteral("all") : type);
    reloadFolders();
    rescan();
}

void LibraryService::addWatchFolders(const QVariantList &folders) {
    bool any = false;
    for (const QVariant &fv : folders) {
        const QVariantMap m = fv.toMap();
        const QString raw = m.value(QStringLiteral("path")).toString();
        const QString local = QUrl(raw).isLocalFile() ? QUrl(raw).toLocalFile() : raw;
        if (local.isEmpty())
            continue;
        const QString type = m.value(QStringLiteral("type")).toString();
        m_db->addWatchFolder(local, type.isEmpty() ? QStringLiteral("all") : type);
        any = true;
    }
    if (!any)
        return;
    reloadFolders();
    rescan();  // one pass for the whole batch
}

void LibraryService::removeWatchFolder(double id) {
    // W1: releasing a folder must take its media with it — otherwise
    // the "removed" folder's tracks/videos/photos linger orphaned in
    // the library forever. Purge by path BEFORE dropping the row (need
    // the path), then refresh every model so the UI reflects it now.
    QString path;
    for (const QVariant &fv : std::as_const(m_watchFolders)) {
        const QVariantMap m = fv.toMap();
        if (qint64(m.value(QStringLiteral("id")).toDouble()) == qint64(id)) {
            path = m.value(QStringLiteral("path")).toString();
            break;
        }
    }
    if (!path.isEmpty()) {
        const int removed = m_db->purgeFolder(path);
        fprintf(stderr, "[library] released '%s' — purged %d items\n",
                qPrintable(path), removed);
    }
    m_db->removeWatchFolder(qint64(id));
    reloadFolders();
    reloadFromDb();     // music lists + photo count + libraryChanged
    reloadVideos();     // video models + videosChanged
    rearmFolderWatcher();
}

void LibraryService::setWatchFolderType(double id, const QString &type) {
    m_db->setWatchFolderType(qint64(id), type);
    reloadFolders();
    rescan();
}

QVariantMap LibraryService::folderCounts(const QString &path) const {
    int tracks = 0, videos = 0, photos = 0;
    if (m_db)
        m_db->countsUnder(path, &tracks, &videos, &photos);
    return {{QStringLiteral("tracks"), tracks},
            {QStringLiteral("videos"), videos},
            {QStringLiteral("photos"), photos}};
}

void LibraryService::applyDuplicateFormats() {
    const int lifted = m_db->clearFormatExclusions();
    if (lifted > 0)
        fprintf(stderr, "[library] duplicate formats: lifted %d"
                        " exclusions\n", lifted);
    rescan();
}

void LibraryService::rescan() {
    if (m_scanning) {
        // Fold this request into a follow-up pass once the current scan
        // ends — scan-as-you-go adds folders while a scan is running.
        m_rescanPending = true;
        return;
    }
    m_scanning = true;
    m_scanCommittedChunk = false;   // track whether this pass changes anything
    m_scanStage = QStringLiteral("starting");
    m_scanCurrent = m_scanTotal = 0;
    emit scanChanged();
    QMetaObject::invokeMethod(m_scanner, &LibraryScanner::scan,
                              Qt::QueuedConnection);
}

QString LibraryService::artKey(const QString &artist, const QString &album) {
    return artist.toLower() + QLatin1Char('\n') + album.toLower();
}

qint64 LibraryService::matchTrackId(const QString &artist, const QString &album,
                                    const QString &title, int discNumber, int trackNumber,
                                    const QVariantList &sourcePeers) const {
    QVector<MusicIdentity::Track> peers;
    peers.reserve(sourcePeers.size());
    for (const auto &value : sourcePeers) peers.append(MusicIdentity::fromMap(value.toMap()));
    const auto result=m_localTrackIdentities.resolve({artist,album,title,discNumber,trackNumber},peers);
    return result.matched() ? result.itemId : -1;
}

bool LibraryService::hasTrack(const QString &artist, const QString &album,
                              const QString &title, int discNumber, int trackNumber,
                              bool discReliable, bool trackReliable, const QVariantList &sourcePeers) const {
    return matchTrackId(artist, album, title, discReliable ? discNumber : 0,
                        trackReliable ? trackNumber : 0,sourcePeers) >= 0;
}

bool LibraryService::hasLocalTrack(const QString &artist, const QString &album,
                                  const QString &title, int discNumber, int trackNumber,
                                  bool discReliable, bool trackReliable, const QVariantList &sourcePeers) const {
    return matchTrackId(artist,album,title,discReliable ? discNumber : 0,
        trackReliable ? trackNumber : 0,sourcePeers) >= 0;
}

QVariantList LibraryService::musicIdentityPeers(const QVariantMap &track) const {
    return m_localTrackIdentities.peers(track);
}

QVariantMap LibraryService::importDevicePlaylist(const QString &name, const QVariantList &items,
                                                  const QVariantList &sourcePeers) {
    MusicIdentity::Index peers;
    qint64 peerId=0;
    for (const auto &value : items) peers.add(MusicIdentity::fromMap(value.toMap()),++peerId);
    for (const auto &value : sourcePeers) peers.add(MusicIdentity::fromMap(value.toMap()),++peerId);
    QVector<LibraryDb::PlaylistEntry> entries;
    QVariantList toPull;
    QSet<quint32> requested;
    int here = 0;
    for (const QVariant &value : items) {
        const auto item = value.toMap();
        LibraryDb::PlaylistEntry entry;
        entry.artist = item.value(QStringLiteral("artist")).toString();
        entry.album = item.value(QStringLiteral("album")).toString();
        entry.title = item.value(QStringLiteral("title")).toString();
        const auto identity = MusicIdentity::fromMap(item);
        entry.discNumber = identity.discNumber;
        entry.trackNumber = identity.trackNumber;
        QVariantList matchingPeers;
        for (const auto &peer : peers.identitiesFor(identity)) matchingPeers.append(MusicIdentity::toMap(peer));
        entry.trackId = matchTrackId(entry.artist, entry.album, entry.title,
                                    entry.discNumber, entry.trackNumber, matchingPeers);
        entries.append(entry);
        if (entry.trackId >= 0) ++here;
        else {
            const quint32 itemId = item.value(QStringLiteral("itemId")).toUInt();
            if (itemId > 0 && !requested.contains(itemId)) {
                requested.insert(itemId); toPull.append(item);
            }
        }
    }
    const qint64 id = m_db->createPlaylistEntries(name.trimmed(), entries);
    if (id < 0)
        return {{"success", false}, {"id", -1}, {"error", QStringLiteral("Could not save this playlist. Try again.")}};
    reloadPlaylists();
    return {{"success", true}, {"id", double(id)}, {"here", here},
        {"pending", int(entries.size()) - here}, {"itemsToPull", toPull}, {"error", QString()}};
}

bool LibraryService::importPlaylistTrack(double playlistId,
                                         const QString &artist,
                                         const QString &album,
                                         const QString &title) {
    const qint64 id = matchTrackId(artist, album, title);
    LibraryDb::PlaylistEntry entry;
    entry.trackId = id; entry.artist = artist; entry.album = album; entry.title = title;
    if (!m_db->appendPlaylistEntries(qint64(playlistId), {entry})) return false;
    reloadPlaylists();
    return id >= 0;
}

int LibraryService::pendingPlaylistAddCount(double playlistId) const {
    return m_pendingPlaylistCounts.value(qint64(playlistId));
}

void LibraryService::resolvePendingPlaylistAdds() {
    const auto entries=m_db->pendingPlaylistEntries();
    MusicIdentity::Index peers;
    for (const auto &entry : entries)
        peers.add({entry.artist,entry.album,entry.title,entry.discNumber,entry.trackNumber},entry.id);
    for (const auto &entry : entries) {
        QVariantList matchingPeers;
        for (const auto &peer : peers.identitiesFor({entry.artist,entry.album,entry.title,entry.discNumber,entry.trackNumber}))
            matchingPeers.append(MusicIdentity::toMap(peer));
        const qint64 id=matchTrackId(entry.artist,entry.album,entry.title,entry.discNumber,entry.trackNumber,matchingPeers);
        if (id>=0) m_db->resolvePlaylistEntry(entry.id,id);
    }
    // Snapshot all source peers before resolving any slots. A first match may
    // not erase the ambiguity protecting its sibling later in this pass.
}

void LibraryService::loadPendingPlaylistAdds() {
    QVector<LibraryDb::PlaylistEntry> pending;
    const QVariantList list =
        QSettings().value(QStringLiteral("pendingPlaylistAdds")).toList();
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        LibraryDb::PlaylistEntry entry;
        entry.playlistId = qint64(m.value(QStringLiteral("pl")).toDouble());
        entry.artist = m.value(QStringLiteral("artist")).toString();
        entry.album = m.value(QStringLiteral("album")).toString();
        entry.title = m.value(QStringLiteral("title")).toString();
        pending.append(entry);
    }
    // The SQLite marker and slots commit together. A crash before clearing
    // QSettings cannot duplicate imported occurrences on the next launch.
    if (m_db->migratePendingPlaylistEntries(pending))
        QSettings().remove(QStringLiteral("pendingPlaylistAdds"));
}

bool LibraryService::hasVideo(const QString &filename, const QString &series,
                              int season, int episode) const {
    const bool tv = !series.isEmpty() && episode > 0;
    const QString fn = filename.toLower();
    const QString se = series.toLower();
    for (const LibVideo &v : m_videos) {
        if (tv) {
            // A TV episode is the same episode regardless of the file's
            // name (we rename on import), so identity is series+S+E.
            if (v.series.toLower() == se && v.season == season
                && v.episode == episode)
                return true;
        }
        if (!fn.isEmpty() && v.filename.toLower() == fn)
            return true;
    }
    return false;
}

bool LibraryService::hasPhoto(const QString &filename) const {
    if (!m_db || filename.isEmpty())
        return false;
    const QString fn = filename.toLower();
    for (const LibPhoto &p : m_db->allPhotos())
        if (p.filename.toLower() == fn)
            return true;
    return false;
}

void LibraryService::requestArt(const QString &artist, const QString &album,
                                const QString &trackFilepath) {
    const QString key = artKey(artist, album);
    if (m_artPaths.contains(key))
        return;
    // Disk-cache hit publishes IMMEDIATELY — the async service treats
    // already-cached as a no-op, which left every cached cover invisible
    // after a restart.
    const QString cached = m_art->cachedArtPath(artist, album);
    if (!cached.isEmpty()) {
        m_artPaths.insert(key, QUrl::fromLocalFile(cached).toString());
        recordArtworkResult(QStringLiteral("album"), {{"artist", artist}, {"album", album}}, {{"status", "ready"}});
        emit artChanged();
        return;
    }
    QStringList sources = m_albumArtSources.value(key);
    if (!trackFilepath.isEmpty() && !sources.contains(trackFilepath)) sources.prepend(trackFilepath);
    if (!m_albumArtworkStatus.contains(key)) {
        m_albumArtworkStatus.insert(key, QVariantMap{{"status", "checkingLocal"}});
        emit artworkStatusChanged();
    }
    m_art->requestSources(artist, album, sources);
}

void LibraryService::discoverMissingArtwork() {
    // Both setup and later imports use this path. Local extraction is queued
    // first; only an exhausted local album request can start online lookup.
    for (const auto &value : std::as_const(m_albumsList)) {
        const auto row = value.toMap();
        requestArt(row.value(QStringLiteral("artArtist")).toString(),
                   row.value(QStringLiteral("artAlbum")).toString(),
                   row.value(QStringLiteral("firstTrackPath")).toString());
    }
    for (const auto &value : std::as_const(m_artistsList))
        requestArtistImage(value.toMap().value(QStringLiteral("name")).toString());
}

void LibraryService::retryMissingArtwork() {
    m_onlineAlbumArt->retryFailures();
    for (const auto &value : std::as_const(m_albumsList)) {
        const auto row = value.toMap();
        const QString artist = row.value(QStringLiteral("artArtist")).toString();
        const QString album = row.value(QStringLiteral("artAlbum")).toString();
        if (!m_art->cachedArtPath(artist, album).isEmpty()) continue;
        m_forceArtworkRetry.insert(QStringLiteral("album") + AlbumArtService::cacheKey(artist, album));
        m_onlineAlbumArt->invalidate(artist, album);
        m_art->forgetFailure(artist, album);
    }
    for (const auto &value : std::as_const(m_artistsList)) {
        const QString name = value.toMap().value(QStringLiteral("name")).toString();
        m_forceArtworkRetry.insert(QStringLiteral("artist") + ArtistImageService::cacheKey(name));
        m_artistImagesSvc->forgetFailure(name);
    }
    discoverMissingArtwork();
}

void LibraryService::importDeviceAlbumArt(const QString &artist,
                                          const QString &album,
                                          const QString &sourcePath) {
    m_art->importDeviceArt(artist, album, sourcePath);
}

QVariantList LibraryService::musicProbeFailures() const {
    return m_db ? m_db->musicProbeFailures() : QVariantList();
}

void LibraryService::reloadFolders() {
    m_watchFolders.clear();
    for (const WatchFolder &f : m_db->watchFolders()) {
        m_watchFolders.append(QVariantMap{
            {"id", double(f.id)},
            {"path", f.path},
            {"type", f.type},
        });
    }
    emit foldersChanged();
    emit libraryChanged(); // empty() depends on folders
    rearmFolderWatcher();  // watch the new folder set's roots
}

void LibraryService::reloadFromDb() {
    const QVector<LibTrack> rows = m_db->allTracks();
    const bool identitiesChanged = m_localTrackIdentities.replaceRows(rows);
    m_tracks->setRows(rows);
    if (m_tracks->displayRows().size() < rows.size())
        fprintf(stderr, "[library] verified music copies: %lld source rows, %lld displayed songs\n",
                static_cast<long long>(rows.size()),
                static_cast<long long>(m_tracks->displayRows().size()));
    QSet<qint64> visibleIds;
    QHash<qint64, const LibTrack *> visibleTracks;
    for (const auto &track : m_tracks->displayRows()) {
        visibleIds.insert(track.id);
        visibleTracks.insert(track.id, &track);
    }

    // Grouped lists — same shapes DeviceWorker produces, with the LOCAL
    // album identity: COALESCE(albumartist, artist).
    QMap<QString, int> artistCounts, genreCounts;
    struct AlbumAgg {
        QString name, albumartist, firstTrackPath;
        int count = 0;
    };
    QMap<QString, AlbumAgg> albumAgg;
    const auto previousArtSources = m_albumArtSources;
    m_albumArtSources.clear();
    const auto previousEvidence = m_albumMatchingEvidence;
    m_albumMatchingEvidence.clear();

    for (const LibTrack &t : rows) {
        const LibTrack &logical = *visibleTracks.value(m_tracks->representativeId(t.id), &t);
        const QString artist =
            logical.artist.isEmpty() ? QStringLiteral("Unknown Artist") : logical.artist;
        const QString albumArtist = logical.albumartist.isEmpty() ? artist : logical.albumartist;
        // Artists pivot groups by ALBUMARTIST — the album's owner.
        // Featured/helper artists on individual tracks don't become
        // library artists (they'd explode a real library into 1000s).
        const bool visible = visibleIds.contains(t.id);
        if (visible) artistCounts[albumArtist]++;
        if (!logical.album.isEmpty()) {
            const QString key = albumArtist.toLower() + QLatin1Char('\n')
                              + logical.album.toLower();
            m_albumArtSources[artKey(albumArtist, logical.album)].append(t.filepath);
            if (visible) {
                auto &evidence = m_albumMatchingEvidence[key];
                auto tracks = evidence.value("_libraryTracks").toList();
                const QVariantMap track{{"title", t.title}, {"track", t.trackNumber}, {"disc", t.discNumber}, {"duration", t.durationMs}};
                if (!tracks.contains(track)) tracks.append(track);
                evidence.insert("_libraryTracks", tracks);
                if (t.year > 0 && !evidence.contains("_libraryYear")) evidence.insert("_libraryYear", t.year);
                AlbumAgg &agg = albumAgg[key];
                if (agg.count == 0) {
                    agg.name = t.album;
                    agg.albumartist = albumArtist;
                    agg.firstTrackPath = t.filepath;
                }
                agg.count++;
            }
        }
        if (visible && !t.genre.isEmpty())
            genreCounts[t.genre]++;
    }

    for (auto it = previousArtSources.cbegin(); it != previousArtSources.cend(); ++it) {
        if (m_albumArtSources.value(it.key()) == it.value()
            && m_albumMatchingEvidence.value(it.key()) == previousEvidence.value(it.key())) continue;
        m_albumArtworkStatus.remove(it.key());
        const qsizetype split = it.key().indexOf('\n');
        m_onlineAlbumArt->invalidate(it.key().left(split), it.key().mid(split + 1));
        m_artistImagesSvc->invalidate(it.key().left(split));
        m_artistArtworkStatus.remove(it.key().left(split));
    }
    m_artworkDiscovery->start();
    m_artistsList.clear();
    for (auto it = artistCounts.cbegin(); it != artistCounts.cend(); ++it)
        m_artistsList.append(QVariantMap{{"name", it.key()},
                                         {"count", it.value()}});
    m_albumsList.clear();
    for (auto it = albumAgg.cbegin(); it != albumAgg.cend(); ++it)
        m_albumsList.append(QVariantMap{
            {"name", it->name},
            {"subtitle", it->albumartist},
            {"count", it->count},
            {"artArtist", it->albumartist},
            {"artAlbum", it->name},
            {"firstTrackPath", it->firstTrackPath}});
    m_genresList.clear();
    for (auto it = genreCounts.cbegin(); it != genreCounts.cend(); ++it)
        m_genresList.append(QVariantMap{{"name", it.key()},
                                        {"count", it.value()}});

    m_photoCount = m_db->allPhotos().size();
    if (identitiesChanged) {
        ++m_localTrackIdentityRevision;
        emit localTrackIdentitiesChanged();
    }
    // W24: pulled playlist tracks that just scanned in can now join the
    // playlist they were imported for.
    resolvePendingPlaylistAdds();
    emit libraryChanged();
    // Track deletions change playlist counts (dangling ids are skipped
    // on resolve), so refresh the playlist list alongside.
    reloadPlaylists();
}

QColor LibraryService::dominantColor(const QString &fileUrl) {
    QString path = QUrl(fileUrl).toLocalFile();
    if (path.isEmpty())
        path = fileUrl;
    if (m_domColorCache.contains(path))
        return m_domColorCache.value(path);

    const QColor fallback(0xd4, 0x36, 0x7a);   // brand pink
    QImage img(path);
    if (img.isNull()) {
        m_domColorCache.insert(path, fallback);
        return fallback;
    }
    img = img.scaled(24, 24, Qt::IgnoreAspectRatio,
                     Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB32);
    // Saturation×value weighted average — vivid regions dominate,
    // grays barely vote.
    double r = 0, g = 0, b = 0, w = 0;
    for (int y = 0; y < img.height(); y++)
        for (int x = 0; x < img.width(); x++) {
            const QColor c(img.pixel(x, y));
            float h, sat, val;
            c.getHsvF(&h, &sat, &val);
            const double wi = double(sat) * double(val) + 0.02;
            r += c.redF() * wi;
            g += c.greenF() * wi;
            b += c.blueF() * wi;
            w += wi;
        }
    QColor out = w > 0
        ? QColor::fromRgbF(float(r / w), float(g / w), float(b / w))
        : fallback;
    // Punch it up so it reads as spray, not mud
    float h, sat, val;
    out.getHsvF(&h, &sat, &val);
    out = QColor::fromHsvF(h, qMin(1.0f, sat * 1.5f + 0.08f),
                           qMax(val, 0.62f));
    m_domColorCache.insert(path, out);
    return out;
}

// ═══ Metadata editing (UX-2 Edit Info) ═══

QVariantMap LibraryService::editTrackMetadata(double id, const QVariantMap &fields) {
    const qint64 trackId = qint64(id);
    const LibTrack *cur = nullptr;
    for (const LibTrack &t : m_tracks->rows())
        if (t.id == trackId) { cur = &t; break; }
    if (!cur)
        return {{"success", false}, {"fileWritten", false},
                {"error", QStringLiteral("This song is no longer in your library. Your edits have not been saved.")}};

    LibTrack t = *cur;
    if (fields.contains(QStringLiteral("title")))
        t.title = fields.value(QStringLiteral("title")).toString().trimmed();
    if (fields.contains(QStringLiteral("artist")))
        t.artist = fields.value(QStringLiteral("artist")).toString().trimmed();
    if (fields.contains(QStringLiteral("albumartist")))
        t.albumartist =
            fields.value(QStringLiteral("albumartist")).toString().trimmed();
    if (fields.contains(QStringLiteral("album")))
        t.album = fields.value(QStringLiteral("album")).toString().trimmed();
    if (fields.contains(QStringLiteral("genre")))
        t.genre = fields.value(QStringLiteral("genre")).toString().trimmed();
    if (fields.contains(QStringLiteral("trackNumber")))
        t.trackNumber = fields.value(QStringLiteral("trackNumber")).toInt();
    if (fields.contains(QStringLiteral("year")))
        t.year = fields.value(QStringLiteral("year")).toInt();
    if (fields.contains(QStringLiteral("discNumber")))
        t.discNumber = qMax(0, fields.value(QStringLiteral("discNumber")).toInt());

    if (t.title.trimmed().isEmpty())
        return {{"success", false}, {"fileWritten", false},
                {"error", QStringLiteral("Give the song a title before saving.")}};

    // Library first — user_edited makes the DB authoritative even if
    // the file write below can't happen (WMA/FLAC, read-only mounts).
    if (!m_db->updateTrackMetadata(trackId, t, /*markUserEdited=*/true))
        return {{"success", false}, {"fileWritten", false},
                {"error", QStringLiteral("Couldn't save your edits to the library. Try again.")}};

    // ID3 in place for real MPEG files only (magic sniff — a WMA named
    // .mp3 would be corrupted by an MPEG-sync rewrite).
    bool fileWritten = false;
    if (SyncEngine::sniffIsMpeg(t.filepath)) {
        const QByteArray path = t.filepath.toUtf8();
        const QByteArray title = t.title.toUtf8();
        const QByteArray artist = t.artist.toUtf8();
        const QByteArray albumartist = t.albumartist.toUtf8();
        const QByteArray album = t.album.toUtf8();
        const QByteArray genre = t.genre.toUtf8();
        ZuunedId3Edits edits;
        edits.title = title.constData();
        edits.artist = artist.constData();
        edits.album_artist = albumartist.constData();
        edits.album = album.constData();
        edits.genre = genre.constData();
        edits.track = t.trackNumber;
        edits.year = t.year;
        fileWritten = zuuned_edit_mp3_tags(path.constData(), &edits) == 0;
    }

    reloadFromDb();
    return {{"success", true}, {"fileWritten", fileWritten}, {"error", QString()}};
}

// ═══ Playlists (UX-2, library-first) ═══

void LibraryService::reloadPlaylists() {
    QVector<int> counts, pending;
    const auto rows = m_db->allPlaylists(&counts, &pending);
    m_playlists.clear();
    m_pendingPlaylistCounts.clear();
    for (int i = 0; i < rows.size(); ++i) {
        m_pendingPlaylistCounts.insert(rows[i].first, pending.value(i));
        m_playlists.append(QVariantMap{
            {"id", double(rows[i].first)},
            {"name", rows[i].second},
            {"count", counts.value(i)}});
    }
    emit playlistsChanged();
}

double LibraryService::createPlaylist(const QString &name,
                                      const QVariantList &trackIds) {
    QVector<qint64> ids;
    for (const QVariant &v : trackIds) ids.append(qint64(v.toDouble()));
    const qint64 id = m_db->createPlaylist(name.trimmed(), ids);
    if (id >= 0) reloadPlaylists();
    return double(id);
}

void LibraryService::renamePlaylist(double id, const QString &name) {
    if (m_db->renamePlaylist(qint64(id), name.trimmed()))
        reloadPlaylists();
}

void LibraryService::deletePlaylist(double id) {
    if (m_db->deletePlaylist(qint64(id)))
        reloadPlaylists();
}

void LibraryService::setPlaylistTracks(double id, const QVariantList &trackIds) {
    QVector<qint64> ids;
    for (const QVariant &v : trackIds) ids.append(qint64(v.toDouble()));
    if (m_db->setPlaylistTracks(qint64(id), ids))
        reloadPlaylists();
}

bool LibraryService::editPlaylistEntries(double id, const QString &name,
                                         const QVariantList &tracks,
                                         const QVariantList &originalEntryIds) {
    QVector<LibraryDb::PlaylistEntry> entries;
    QVector<qint64> baseline;
    for (const QVariant &value : tracks) {
        const auto row = value.toMap();
        LibraryDb::PlaylistEntry entry;
        entry.id = qint64(row.value(QStringLiteral("playlistEntryId")).toDouble());
        entry.trackId = qint64(row.value(QStringLiteral("id"), -1).toDouble());
        entries.append(entry);
    }
    for (const QVariant &value : originalEntryIds) baseline.append(qint64(value.toDouble()));
    if (!m_db->editPlaylistEntries(qint64(id), name.trimmed(), entries, baseline)) return false;
    reloadPlaylists();
    return true;
}

void LibraryService::addToPlaylist(double id, const QVariantList &trackIds) {
    const QVector<qint64> existing = m_db->playlistTrackIds(qint64(id));
    QSet<qint64> have;
    for (auto track : existing) have.insert(m_tracks->representativeId(track));
    QVector<qint64> ids;
    for (const QVariant &v : trackIds) {
        const qint64 tid = qint64(v.toDouble());
        const qint64 representative = m_tracks->representativeId(tid);
        if (!have.contains(representative)) { ids.append(tid); have.insert(representative); }
    }
    if (!ids.isEmpty() && m_db->appendPlaylistTracks(qint64(id), ids))
        reloadPlaylists();
}

QVariantList LibraryService::playlistTracks(double id) {
    QHash<qint64, const LibTrack *> byId;
    for (const LibTrack &t : m_tracks->rows())
        byId.insert(t.id, &t);
    QVariantList out;
    for (const auto &entry : m_db->playlistEntries(qint64(id))) {
        const LibTrack *t = byId.value(entry.trackId, nullptr);
        if (!t) continue;   // deleted from the library since
        out.append(QVariantMap{
            {"id", double(t->id)},
            {"playlistEntryId", double(entry.id)},
            {"title", t->title},
            {"artist", t->artist},
            {"album", t->album},
            {"durationMs", t->durationMs},
            {"trackNumber", t->trackNumber},
            {"discNumber", t->discNumber},
            {"year", t->year},
            {"albumartist", t->albumartist},
            {"filepath", t->filepath}});
    }
    return out;
}

// ═══ Videos ═══

QString LibraryService::stillsCacheDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/stills");
}

void LibraryService::reloadVideos() {
    m_videos = m_db->allVideos();
    emit videosChanged();
}

void LibraryService::patchVideoRows(const QVector<qint64> &ids) {
    // Refresh only the named rows in place — the read is cheap; the
    // point is to emit a TARGETED signal (videoRowsChanged) instead of
    // videosChanged, so nothing bound to the video lists resets.
    const QVector<LibVideo> fresh = m_db->allVideos();
    QHash<qint64, const LibVideo *> byId;
    for (const LibVideo &v : fresh)
        byId.insert(v.id, &v);
    QVariantList changed;
    for (qint64 id : ids) {
        const LibVideo *nv = byId.value(id, nullptr);
        if (!nv)
            continue;
        for (LibVideo &v : m_videos) {
            if (v.id == id) {
                v = *nv;
                break;
            }
        }
        changed.append(double(id));
    }
    if (!changed.isEmpty())
        emit videoRowsChanged(changed);
}

// Exponential retry backoff for TMDB lookups: 60s after the first
// failure, doubling to a 24h cap. attempts==0 → no wait.
int LibraryService::lookupBackoff(int attempts) {
    if (attempts <= 0)
        return 0;
    return qMin(30 * (1 << qMin(attempts, 12)), 86400);
}

void LibraryService::pokeVideoConsumers() {
    runProbeRound();
    runLookupRound();
    // Keep the idle timer alive only while there is (or may be) work.
    const bool workPossible = m_scanning || m_probeBusy || m_matchBusy;
    if (workPossible && !m_consumerTimer->isActive())
        m_consumerTimer->start();
    else if (!workPossible && m_consumerTimer->isActive())
        m_consumerTimer->stop();
}

void LibraryService::runProbeRound() {
    if (m_probeBusy)
        return;

    // probedAt==0 keeps unprobeable files (offline share, broken
    // container) from spinning forever — one attempt per process run.
    QVector<QPair<qint64, QString>> todo;
    for (const LibVideo &v : std::as_const(m_videos)) {
        if (v.durationMs == 0 && v.probedAt == 0) {
            todo.append({v.id, v.filepath});
            if (todo.size() >= 64)
                break;
        }
    }
    if (todo.isEmpty())
        return;

    m_probeBusy = true;
    const QString dbPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library.db");

    auto future = QtConcurrent::run([todo, dbPath] {
        struct Probe { qint64 id; int durationMs, width, height; };
        // Parallel libav header reads (each 200-500ms over SMB).
        const QList<Probe> results = QtConcurrent::blockingMapped(
            todo, std::function<Probe(const QPair<qint64, QString> &)>(
                [](const QPair<qint64, QString> &item) {
                    double duration = 0;
                    int w = 0, h = 0;
                    zuuned_probe_video_info(item.second.toUtf8().constData(),
                                            &duration, &w, &h);
                    return Probe{item.first, int(duration * 1000.0), w, h};
                }));

        // Background connection — don't contend with the scan's.
        LibraryDb bg;
        if (bg.open(dbPath)) {
            bg.begin();
            for (const Probe &p : results)
                bg.updateVideoProbe(p.id, p.durationMs, p.width, p.height);
            bg.commit();
        }
    });

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_probeBusy = false;
        reloadVideos();
        pokeVideoConsumers(); // next round if rows remain
    });
    watcher->setFuture(future);
}

void LibraryService::runLookupRound() {
    if (m_matchBusy)
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVector<LibVideo> eligible;
    for (const LibVideo &v : std::as_const(m_videos)) {
        if (!v.userEdited && !v.tmdbCached && v.lookupAttempts < 8
            && now - v.lastLookupAt >= lookupBackoff(v.lookupAttempts)) {
            eligible.append(v);
            if (eligible.size() >= 200)
                break;
        }
    }
    if (eligible.isEmpty())
        return;

    m_matchBusy = true;
    const QString posterSource =
        QSettings().value(QStringLiteral("posterSource"),
                          QStringLiteral("tmdb")).toString();
    fprintf(stderr, "[library] TMDB lookup: %d videos\n", int(eligible.size()));
    QMetaObject::invokeMethod(m_matcher, "matchBatch", Qt::QueuedConnection,
                              Q_ARG(QVector<LibVideo>, eligible),
                              Q_ARG(QString, posterSource));
}

void LibraryService::deleteVideo(double id) {
    // NEVER deletes the actual file — row + exclusion only.
    m_db->removeVideo(qint64(id), /*exclude=*/true);
    reloadVideos();
}

void LibraryService::deleteVideos(const QVariantList &ids) {
    for (const QVariant &id : ids)
        m_db->removeVideo(qint64(id.toDouble()), /*exclude=*/true);
    reloadVideos();
}

void LibraryService::setVideosWatched(const QVariantList &ids, bool watched) {
    QVector<qint64> v;
    for (const QVariant &id : ids)
        v.append(qint64(id.toDouble()));
    m_db->setVideosWatched(v, watched);
    reloadVideos();
}

void LibraryService::setVideoCategory(double id, const QString &category) {
    m_db->updateVideoCategory(qint64(id), category);
    reloadVideos();
}

void LibraryService::reclassifyVideos(const QVariantList &ids,
                                      const QString &category) {
    for (const QVariant &id : ids)
        m_db->updateVideoCategory(qint64(id.toDouble()), category);
    reloadVideos();
}

void LibraryService::setManualVideoIdentity(const QVariantList &ids,
                                            const QVariantMap &f) {
    if (ids.isEmpty())
        return;
    const QString type = f.value(QStringLiteral("type"),
                                 QStringLiteral("movie")).toString();
    const bool tv = type == QLatin1String("tv");
    const QString category = tv ? QStringLiteral("tv") : QStringLiteral("movie");
    const QString title = f.value(QStringLiteral("title")).toString();
    const QString year = f.value(QStringLiteral("year")).toString();
    const QString genres = f.value(QStringLiteral("genres")).toString();
    const QString overview = f.value(QStringLiteral("overview")).toString();
    const bool bulk = ids.size() > 1;   // a whole series: many episode rows

    QHash<qint64, const LibVideo *> byId;
    for (const LibVideo &v : std::as_const(m_videos))
        byId.insert(v.id, &v);

    QVector<qint64> changed;
    for (const QVariant &idv : ids) {
        const qint64 id = qint64(idv.toDouble());
        // Series-level fields apply to every episode; per-file
        // season/episode come from the form only for a single file —
        // a bulk series keeps each episode's existing numbering.
        int season = 0, episode = 0;
        QString epTitle;
        if (tv) {
            if (bulk) {
                const LibVideo *v = byId.value(id, nullptr);
                if (v) { season = v->season; episode = v->episode;
                         epTitle = v->episodeTitle; }
            } else {
                season = f.value(QStringLiteral("season")).toInt();
                episode = f.value(QStringLiteral("episode")).toInt();
                epTitle = f.value(QStringLiteral("episodeTitle")).toString();
            }
        }
        m_db->setManualVideoIdentity(id, category, title, year, genres,
                                     overview, tv ? title : QString(),
                                     season, episode, epTitle);
        changed.append(id);
    }
    patchVideoRows(changed);   // surgical — no grid reset
}

void LibraryService::setVideoCustomArt(const QVariantList &ids,
                                       const QString &source) {
    if (ids.isEmpty())
        return;
    // Copy the chosen image into the cache ONCE (keyed by the first id),
    // downscaled to a poster-sane width, and point every target row at
    // it (sticky). "Your file wins."
    QString src = source;
    if (src.startsWith(QLatin1String("file://")))
        src = QUrl(src).toLocalFile();
    if (src.isEmpty() || !QFile::exists(src)) {
        fprintf(stderr, "[customize] custom art source missing: %s\n",
                qPrintable(src));
        return;
    }
    QImage img(src);
    if (img.isNull()) {
        fprintf(stderr, "[customize] custom art not an image: %s\n",
                qPrintable(src));
        return;
    }
    if (img.width() > 780)
        img = img.scaledToWidth(780, Qt::SmoothTransformation);
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation) + QStringLiteral("/vidthumbs");
    QDir().mkpath(dir);
    const qint64 key = qint64(ids.first().toDouble());
    const QString dest = dir + QStringLiteral("/custom_%1.jpg").arg(key);
    QFile::remove(dest);
    if (!img.save(dest, "JPG", 90)) {
        fprintf(stderr, "[customize] failed to cache custom art\n");
        return;
    }
    QVector<qint64> changed;
    for (const QVariant &idv : ids) {
        const qint64 id = qint64(idv.toDouble());
        m_db->setVideoCustomPoster(id, dest);
        changed.append(id);
    }
    patchVideoRows(changed);
}

// ── Music customize (W13) ───────────────────────────────────────────
namespace {
// Downscale to a poster-sane width and write JPG to dest. Returns the
// on-disk path on success, empty on failure. Mirrors setVideoCustomArt.
QString writeCustomArt(const QString &source, const QString &dest) {
    QString src = source;
    if (src.startsWith(QLatin1String("file://")))
        src = QUrl(src).toLocalFile();
    if (src.isEmpty() || !QFile::exists(src)) {
        fprintf(stderr, "[customize] art source missing: %s\n", qPrintable(src));
        return QString();
    }
    QImage img(src);
    if (img.isNull()) {
        fprintf(stderr, "[customize] art not an image: %s\n", qPrintable(src));
        return QString();
    }
    if (img.width() > 780)
        img = img.scaledToWidth(780, Qt::SmoothTransformation);
    QFile::remove(dest);
    if (!img.save(dest, "JPG", 90)) {
        fprintf(stderr, "[customize] failed to write art: %s\n", qPrintable(dest));
        return QString();
    }
    return dest;
}
// Cache-busting url so a QML Image re-decodes the same path (the query
// participates in Qt's pixmap cache key but is dropped by toLocalFile()).
QString bustedUrl(const QString &path) {
    return QUrl::fromLocalFile(path).toString()
           + QStringLiteral("?v=")
           + QString::number(QDateTime::currentMSecsSinceEpoch());
}
// ID3 in place for MPEG files only — same magic-sniff guard and field
// set as editTrackMetadata.
bool writeId3IfMpeg(const LibTrack &t) {
    if (!SyncEngine::sniffIsMpeg(t.filepath))
        return true; // Library metadata is authoritative for other formats.
    const QByteArray path = t.filepath.toUtf8();
    const QByteArray title = t.title.toUtf8();
    const QByteArray artist = t.artist.toUtf8();
    const QByteArray albumartist = t.albumartist.toUtf8();
    const QByteArray album = t.album.toUtf8();
    const QByteArray genre = t.genre.toUtf8();
    ZuunedId3Edits edits;
    edits.title = title.constData();
    edits.artist = artist.constData();
    edits.album_artist = albumartist.constData();
    edits.album = album.constData();
    edits.genre = genre.constData();
    edits.track = t.trackNumber;
    edits.year = t.year;
    return zuuned_edit_mp3_tags(path.constData(), &edits) == 0;
}
} // namespace

void LibraryService::setAlbumCustomArt(const QString &artist,
                                       const QString &album,
                                       const QString &source) {
    if (album.isEmpty() && artist.isEmpty())
        return;
    const QString dest = writeCustomArt(source, m_art->cachePathFor(artist, album));
    if (dest.isEmpty())
        return;
    m_artPaths.insert(artKey(artist, album), bustedUrl(dest));
    emit artChanged();
}

void LibraryService::setArtistCustomArt(const QString &name,
                                        const QString &source) {
    if (name.isEmpty())
        return;
    const QString dest =
        writeCustomArt(source, m_artistImagesSvc->cachePathFor(name));
    if (dest.isEmpty())
        return;
    m_artistImages.insert(name.toLower(), bustedUrl(dest));
    emit artistImagesChanged();
}

void LibraryService::setManualAlbumIdentity(const QString &artist,
                                            const QString &album,
                                            const QVariantMap &f) {
    const QString oa = artist.toLower();
    const QString ol = album.toLower();
    // Snapshot ids first — updateTrackMetadata/reload can churn rows().
    QVector<qint64> ids;
    for (const LibTrack &t : m_tracks->rows()) {
        if (t.album.toLower() != ol)
            continue;
        if (!oa.isEmpty() && t.artist.toLower() != oa
            && t.albumartist.toLower() != oa)
            continue;
        ids.append(t.id);
    }
    bool any = false;
    for (const qint64 id : std::as_const(ids)) {
        const LibTrack *cur = nullptr;
        for (const LibTrack &t : m_tracks->rows())
            if (t.id == id) { cur = &t; break; }
        if (!cur)
            continue;
        LibTrack t = *cur;
        if (f.contains(QStringLiteral("album")))
            t.album = f.value(QStringLiteral("album")).toString().trimmed();
        if (f.contains(QStringLiteral("albumartist")))
            t.albumartist =
                f.value(QStringLiteral("albumartist")).toString().trimmed();
        if (f.contains(QStringLiteral("genre")))
            t.genre = f.value(QStringLiteral("genre")).toString().trimmed();
        if (f.contains(QStringLiteral("year")))
            t.year = f.value(QStringLiteral("year")).toInt();
        if (m_db->updateTrackMetadata(id, t, /*markUserEdited=*/true)) {
            writeId3IfMpeg(t);
            any = true;
        }
    }
    if (any)
        reloadFromDb();
}

void LibraryService::setManualArtistIdentity(const QString &name,
                                             const QVariantMap &f) {
    const QString on = name.toLower();
    const QString newName = f.value(QStringLiteral("artist")).toString().trimmed();
    if (newName.isEmpty())
        return;
    QVector<qint64> ids;
    for (const LibTrack &t : m_tracks->rows())
        if (t.artist.toLower() == on || t.albumartist.toLower() == on)
            ids.append(t.id);
    bool any = false;
    for (const qint64 id : std::as_const(ids)) {
        const LibTrack *cur = nullptr;
        for (const LibTrack &t : m_tracks->rows())
            if (t.id == id) { cur = &t; break; }
        if (!cur)
            continue;
        LibTrack t = *cur;
        if (t.artist.toLower() == on)
            t.artist = newName;
        if (t.albumartist.toLower() == on)
            t.albumartist = newName;
        if (m_db->updateTrackMetadata(id, t, /*markUserEdited=*/true)) {
            writeId3IfMpeg(t);
            any = true;
        }
    }
    if (any)
        reloadFromDb();
}

// ── Online art galleries (W13 finish) ──────────────────────────────
namespace {
QByteArray httpGet(const QString &url, QString *error = nullptr) {
    const QUrl parsed(url);
    const bool musicBrainz = parsed.host().compare(QStringLiteral("musicbrainz.org"), Qt::CaseInsensitive) == 0;
    return ArtworkHttp::getBody(parsed, musicBrainz, error);
}

// MusicBrainz release-group search → Cover Art Archive front images.
// Share the identity search cache, pacing and outage handling; artwork browsing
// must not issue an uncached copy of the same album search.
QVariantList caaAlbumUrls(const QString &artist, const QString &album,
                         QString *error = nullptr) {
    MusicIdentityClient client;
    const auto groups = client.searchAlbums(album, artist, error);
    QVariantList out;
    for (const auto &group : groups) {
        const QString id = group.toMap().value(QStringLiteral("mbid")).toString();
        out.append(QStringLiteral(
            "https://coverartarchive.org/release-group/%1/front-500").arg(id));
        if (out.size() >= 12)
            break;
    }
    return out;
}

// Deezer artist search → picture_xl candidates (placeholders skipped).
QVariantList deezerArtistUrls(const QString &name, QString *error = nullptr) {
    if (name.isEmpty())
        return {};
    QUrl url(QStringLiteral("https://api.deezer.com/search/artist"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), name);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("12"));
    url.setQuery(q);
    const QByteArray body = httpGet(url.toString(QUrl::FullyEncoded), error);
    QVariantList out;
    const QJsonArray data = QJsonDocument::fromJson(body).object()
        .value(QStringLiteral("data")).toArray();
    for (const QJsonValue &v : data) {
        const QJsonObject a = v.toObject();
        QString pic = a.value(QStringLiteral("picture_xl")).toString();
        if (pic.isEmpty())
            pic = a.value(QStringLiteral("picture_big")).toString();
        if (pic.isEmpty() || pic.contains(QStringLiteral("/artist//")))
            continue;
        out.append(pic);
    }
    return out;
}

// Download a remote image to a temp file; returns path or empty.
QString downloadToTemp(const QString &url) {
    const QByteArray b = httpGet(url);
    if (b.size() < 512)
        return QString();
    const QString dest = QDir::temp().filePath(
        QStringLiteral("zuuned_pick_%1.jpg").arg(qHash(url)));
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(b);
    f.close();
    return dest;
}
} // namespace

void LibraryService::fetchArtCandidates(const QString &kind,
                                        const QString &source,
                                        const QVariantMap &ctx) {
    auto future = QtConcurrent::run([kind, source, ctx]() -> QVariantList {
        if (kind == QLatin1String("movie") || kind == QLatin1String("series")) {
            const bool tv = kind == QLatin1String("series")
                            || ctx.value(QStringLiteral("tv")).toBool();
            int id = ctx.value(QStringLiteral("tmdbId")).toInt();
            TmdbClient tmdb;
            if (id <= 0) {
                const QString title = ctx.value(QStringLiteral("title")).toString();
                bool ok = false;
                const auto res = tv ? tmdb.searchTV(title, &ok)
                                    : tmdb.searchMovie(title, &ok);
                if (!res.isEmpty())
                    id = res.first().id;
            }
            if (id <= 0)
                return {};
            QVariantList out;
            if (source == QLatin1String("fanart")) {
                FanartClient fan(&tmdb);
                const QStringList urls = tv
                    ? fan.tvPosterUrls(tmdb.tvdbIdFor(id))
                    : fan.moviePosterUrls(id);
                for (const QString &u : urls)
                    out.append(u);
            } else {
                const auto posters = tv ? tmdb.tvPosters(id) : tmdb.moviePosters(id);
                for (const TmdbPoster &p : posters)
                    out.append(QStringLiteral("https://image.tmdb.org/t/p/w500")
                               + p.filePath);
            }
            return out;
        }
        if (kind == QLatin1String("album"))
            return caaAlbumUrls(ctx.value(QStringLiteral("artist")).toString(),
                                ctx.value(QStringLiteral("album")).toString());
        if (kind == QLatin1String("artist"))
            return deezerArtistUrls(ctx.value(QStringLiteral("name")).toString());
        return {};
    });
    auto *w = new QFutureWatcher<QVariantList>(this);
    connect(w, &QFutureWatcher<QVariantList>::finished, this,
            [this, w, kind, source] {
        w->deleteLater();
        emit artCandidatesReady(kind, source, w->result());
    });
    w->setFuture(future);
}

void LibraryService::fetchFrameGrabs(double videoId, const QString &filepath,
                                     int durationMs) {
    const qint64 id = qint64(videoId);
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation) + QStringLiteral("/vidthumbs");
    QDir().mkpath(dir);
    QString fp = filepath;
    if (fp.startsWith(QLatin1String("file://")))
        fp = QUrl(fp).toLocalFile();
    int dur = durationMs;
    // Resolve the file + duration from the row when QML doesn't carry them.
    if (fp.isEmpty() || dur <= 0) {
        for (const LibVideo &v : std::as_const(m_videos)) {
            if (v.id == id) {
                if (fp.isEmpty()) fp = v.filepath;
                if (dur <= 0) dur = v.durationMs;
                break;
            }
        }
    }
    if (fp.isEmpty()) {
        emit frameGrabsReady({});
        return;
    }
    const int durationMs2 = dur;
    auto future = QtConcurrent::run([id, fp, durationMs2, dir]() -> QVariantList {
        QVariantList out;
        const double dur = durationMs2 > 0 ? durationMs2 / 1000.0 : 0;
        const double fracs[] = {0.10, 0.30, 0.50, 0.70, 0.90};
        int i = 0;
        for (const double f : fracs) {
            const double secs = dur > 0 ? dur * f : (10.0 + i * 30.0);
            const QString dest =
                dir + QStringLiteral("/grab_%1_%2.jpg").arg(id).arg(i);
            QFile::remove(dest);
            if (zuuned_extract_frame(fp.toUtf8().constData(), secs,
                                     dest.toUtf8().constData(), 500) == 0
                && QFile::exists(dest))
                out.append(QUrl::fromLocalFile(dest).toString());
            i++;
        }
        return out;
    });
    auto *w = new QFutureWatcher<QVariantList>(this);
    connect(w, &QFutureWatcher<QVariantList>::finished, this, [this, w] {
        w->deleteLater();
        emit frameGrabsReady(w->result());
    });
    w->setFuture(future);
}

void LibraryService::pickVideoArtUrl(const QVariantList &ids, const QString &url) {
    auto future = QtConcurrent::run([url] { return downloadToTemp(url); });
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w, ids] {
        w->deleteLater();
        const QString tmp = w->result();
        if (!tmp.isEmpty())
            setVideoCustomArt(ids, tmp);
    });
    w->setFuture(future);
}

void LibraryService::pickAlbumArtUrl(const QString &artist,
                                     const QString &album, const QString &url) {
    auto future = QtConcurrent::run([url] { return downloadToTemp(url); });
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this,
            [this, w, artist, album] {
        w->deleteLater();
        const QString tmp = w->result();
        if (!tmp.isEmpty())
            setAlbumCustomArt(artist, album, tmp);
    });
    w->setFuture(future);
}

void LibraryService::pickArtistArtUrl(const QString &name, const QString &url) {
    auto future = QtConcurrent::run([url] { return downloadToTemp(url); });
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w, name] {
        w->deleteLater();
        const QString tmp = w->result();
        if (!tmp.isEmpty())
            setArtistCustomArt(name, tmp);
    });
    w->setFuture(future);
}

void LibraryService::requeueForMatch(const QVariantList &ids) {
    QVector<qint64> v;
    for (const QVariant &id : ids)
        v.append(qint64(id.toDouble()));
    m_db->requeueVideosForMatch(v);
    reloadVideos();
    pokeVideoConsumers();
}

int LibraryService::unmatchedVideoCount() const {
    int n = 0;
    for (const LibVideo &v : m_videos)
        if (v.tmdbCached && v.tmdbId == 0)
            n++;
    return n;
}

void LibraryService::requeueAllUnmatched() {
    QVector<qint64> ids;
    for (const LibVideo &v : std::as_const(m_videos))
        if (v.tmdbCached && v.tmdbId == 0)
            ids.append(v.id);
    if (ids.isEmpty())
        return;
    m_db->requeueVideosForMatch(ids);
    reloadVideos();
    pokeVideoConsumers();
}

void LibraryService::updateVideoSeries(double id, const QString &series,
                                       int season, int episode,
                                       const QString &episodeTitle) {
    m_db->updateVideoSeries(qint64(id), series, season, episode, episodeTitle);
    m_db->setVideosUserEdited({qint64(id)});
    reloadVideos();
}

void LibraryService::tmdbSearch(const QString &query, bool tv) {
    if (query.isEmpty()) {
        emit tmdbSearchResults({});
        return;
    }
    auto future = QtConcurrent::run([query, tv] {
        // Per-call client: QNetworkAccessManager gets pool-thread
        // affinity; the event loop runs fine there.
        TmdbClient tmdb;
        return tv ? tmdb.searchTV(query) : tmdb.searchMovie(query);
    });
    auto *watcher = new QFutureWatcher<QVector<TmdbResult>>(this);
    connect(watcher, &QFutureWatcher<QVector<TmdbResult>>::finished, this,
            [this, watcher] {
        watcher->deleteLater();
        QVariantList out;
        for (const TmdbResult &r : watcher->result()) {
            out.append(QVariantMap{
                {"tmdbId", r.id},
                {"title", r.title},
                {"year", r.year},
                {"overview", r.overview},
                // Remote w92 thumb for the sheet — same as the mac's
                // AsyncImage rows.
                {"posterUrl", r.posterPath.isEmpty()
                    ? QString()
                    : QStringLiteral("https://image.tmdb.org/t/p/w92") + r.posterPath},
            });
        }
        emit tmdbSearchResults(out);
    });
    watcher->setFuture(future);
}

void LibraryService::assignTmdbResult(const QVariantList &ids, int tmdbId,
                                      bool tv) {
    QVector<qint64> videoIds;
    for (const QVariant &id : ids)
        videoIds.append(qint64(id.toDouble()));
    if (videoIds.isEmpty())
        return;

    const QString thumbsDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/vidthumbs");

    auto future = QtConcurrent::run([tmdbId, tv, thumbsDir] {
        TmdbClient tmdb;
        TmdbDetails details;
        const bool ok = tv ? tmdb.tvDetails(tmdbId, &details)
                           : tmdb.movieDetails(tmdbId, &details);
        QString posterLocal;
        if (ok && !details.posterPath.isEmpty()) {
            const QString dest = thumbsDir
                + (tv ? QStringLiteral("/tv_%1.jpg") : QStringLiteral("/%1.jpg"))
                      .arg(tmdbId);
            if (tmdb.downloadPoster(details.posterPath, dest))
                posterLocal = dest;
        }
        return QPair<bool, QPair<TmdbDetails, QString>>(
            ok, {details, posterLocal});
    });
    auto *watcher =
        new QFutureWatcher<QPair<bool, QPair<TmdbDetails, QString>>>(this);
    connect(watcher,
            &QFutureWatcher<QPair<bool, QPair<TmdbDetails, QString>>>::finished,
            this, [this, watcher, videoIds, tv] {
        watcher->deleteLater();
        const auto result = watcher->result();
        if (!result.first) {
            fprintf(stderr, "[tmdb] manual assign fetch failed\n");
            return;
        }
        const TmdbDetails &details = result.second.first;
        const QString &poster = result.second.second;
        const QString category = tv ? QStringLiteral("tv") : QStringLiteral("movie");
        fprintf(stderr, "[tmdb] bulk assigning %d videos to '%s'\n",
                int(videoIds.size()), qPrintable(details.title));

        // Season/episode preserved from the current rows.
        QHash<qint64, const LibVideo *> byId;
        for (const LibVideo &v : std::as_const(m_videos))
            byId.insert(v.id, &v);

        // Membership-stable? A correction of an ALREADY-matched item to
        // the same category/series only changes fields — patch in place
        // (no reset). A newly-matched item (was tmdbId 0), or one that
        // changes category/series, moves between tabs/groups → full
        // rebuild. All ids must be stable to take the surgical path.
        bool membershipStable = true;
        for (qint64 id : videoIds) {
            const LibVideo *v = byId.value(id, nullptr);
            if (!v || v->tmdbId == 0 || v->category != category
                || (tv && v->series != details.title)) {
                membershipStable = false;
                break;
            }
        }

        for (qint64 id : videoIds) {
            m_db->updateVideoTmdb(id, details.id, details.title, poster,
                                  details.cast, details.director,
                                  details.rating, details.genres, details.year,
                                  category, details.overview);
            if (tv) {
                // Write the series fields so the rows group under one
                // tile — assigning without them left episodes stranded
                // outside the TV tab (mac bug, fixed there too).
                const LibVideo *v = byId.value(id, nullptr);
                m_db->updateVideoSeries(id, details.title,
                                        v ? qMax(v->season, 1) : 1,
                                        v ? v->episode : 0,
                                        v ? v->episodeTitle : QString());
            }
        }
        // A manual match is authoritative: sticky across rescans.
        m_db->setVideosUserEdited(videoIds);
        if (membershipStable)
            patchVideoRows(videoIds);   // surgical — no reset
        else
            reloadVideos();             // item moved groups/tabs
    });
    watcher->setFuture(future);
}

QString LibraryService::exportPlaylistsM3U() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
        + QStringLiteral("/Zuuned Playlists");
    QDir().mkpath(dir);
    int written = 0;
    for (const QVariant &pv : std::as_const(m_playlists)) {
        const QVariantMap p = pv.toMap();
        QString name = p.value(QStringLiteral("name")).toString();
        name.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")),
                     QStringLiteral("_"));
        if (name.isEmpty())
            name = QStringLiteral("playlist");
        QFile f(dir + QLatin1Char('/') + name + QStringLiteral(".m3u8"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            continue;
        f.write("#EXTM3U\n");
        for (const QVariant &tv :
             playlistTracks(p.value(QStringLiteral("id")).toDouble())) {
            const QVariantMap t = tv.toMap();
            f.write(QStringLiteral("#EXTINF:%1,%2 - %3\n")
                        .arg(t.value(QStringLiteral("durationMs")).toInt() / 1000)
                        .arg(t.value(QStringLiteral("artist")).toString(),
                             t.value(QStringLiteral("title")).toString())
                        .toUtf8());
            f.write(t.value(QStringLiteral("filepath")).toString().toUtf8());
            f.write("\n");
        }
        written++;
    }
    fprintf(stderr, "[library] exported %d playlists to %s\n",
            written, qPrintable(dir));
    return dir;
}

QVariantMap LibraryService::importPlaylistM3U(const QString &fileUrl) {
    QString path = fileUrl;
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(fileUrl).toLocalFile();
    QFile f(path);
    QVariantMap res{{QStringLiteral("added"), 0},
                    {QStringLiteral("missing"), 0}};
    if (!f.open(QIODevice::ReadOnly))
        return res;

    // filepath → library id
    QHash<QString, qint64> byPath;
    for (const LibTrack &t : m_db->allTracks())
        byPath.insert(t.filepath, t.id);

    QVariantList ids;
    int missing = 0;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const auto it = byPath.constFind(line);
        if (it != byPath.constEnd())
            ids.append(double(it.value()));
        else
            missing++;
    }
    if (!ids.isEmpty()) {
        QString name = QFileInfo(path).completeBaseName();
        const double pid = createPlaylist(name, ids);
        if (pid >= 0) res[QStringLiteral("added")] = int(ids.size());
    }
    res[QStringLiteral("missing")] = missing;
    return res;
}

QVariantList LibraryService::healthChecks() const {
    QVariantList out;
    for (const QVariant &fv : m_watchFolders) {
        const QString path = fv.toMap().value(QStringLiteral("path")).toString();
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isReadable())
            out.append(QVariantMap{
                {QStringLiteral("id"), QStringLiteral("folder")},
                {QStringLiteral("ok"), false},
                {QStringLiteral("label"),
                 QStringLiteral("watch folder unreachable")},
                {QStringLiteral("detail"),
                 path + QStringLiteral(" — network mount offline? Fix or"
                                       " remove it; scans skip it.")}});
    }
    const QStorageInfo tmp(QDir::tempPath());
    if (tmp.isValid() && tmp.bytesAvailable() >= 0
        && tmp.bytesAvailable() < qint64(2) * 1000 * 1000 * 1000)
        out.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("tempspace")},
            {QStringLiteral("ok"), false},
            {QStringLiteral("label"),
             QStringLiteral("low disk space for transcodes")},
            {QStringLiteral("detail"),
             QStringLiteral("%1 free in %2 — video syncs buffer ~500MB of"
                            " temp files there.")
                 .arg(QString::number(tmp.bytesAvailable() / 1e9, 'f', 1)
                      + QStringLiteral(" GB"), QDir::tempPath())}});
    return out;
}

double LibraryService::tempFreeGB() const {
    const QStorageInfo tmp(QDir::tempPath());
    if (!tmp.isValid() || tmp.bytesAvailable() < 0)
        return -1.0;
    return tmp.bytesAvailable() / 1e9;
}

// "today 14:32" / "yesterday 09:05" / "3 Sep 14:32" — matches the mock.
static QString humanWhen(const QDateTime &dt) {
    if (!dt.isValid())
        return QString();
    const QDate d = dt.date();
    const QDate today = QDate::currentDate();
    const QString t = dt.toString(QStringLiteral("HH:mm"));
    if (d == today)
        return QStringLiteral("today ") + t;
    if (d == today.addDays(-1))
        return QStringLiteral("yesterday ") + t;
    return dt.toString(QStringLiteral("d MMM HH:mm"));
}

static QString backupDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/backups");
}

QString LibraryService::backupDatabase() {
    const QString dbPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library.db");
    if (!QFileInfo::exists(dbPath))
        return QString();
    QDir().mkpath(backupDir());
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString dest =
        backupDir() + QStringLiteral("/library-%1.db").arg(stamp);
    if (!QFile::copy(dbPath, dest))
        return QString();
    return humanWhen(QDateTime::currentDateTime());
}

QString LibraryService::lastBackupLabel() const {
    QDir dir(backupDir());
    if (!dir.exists())
        return QString();
    const auto files = dir.entryInfoList(
        {QStringLiteral("library-*.db")}, QDir::Files, QDir::Time);
    if (files.isEmpty())
        return QString();
    return humanWhen(files.first().lastModified());
}

// ── Sleeve: correlated reads and a single prepared commit ───────────
namespace {
struct CustomizeLookup {
    QVariantList items;
    QString error;
};
QVariantList customizeUrls(const QStringList &urls) {
    QVariantList out;
    for (const auto &url : urls) if (!out.contains(url)) out.append(url);
    return out;
}

QByteArray prepareCustomizeImage(const QString &source, QString *error) {
    QByteArray bytes;
    const QUrl url(source);
    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) {
        bytes = httpGet(source, error);
        if (!error->isEmpty()) return {};
    } else {
        const QString path = url.isLocalFile() ? url.toLocalFile() : source;
        QFile input(path);
        if ((!url.scheme().isEmpty() && !url.isLocalFile())
            || !input.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("That image is no longer available. Choose it again.");
            return {};
        }
        if (input.size() > 32 * 1024 * 1024) {
            *error = QStringLiteral("Choose an image smaller than 32 MB.");
            return {};
        }
        bytes = input.readAll();
    }
    QBuffer input(&bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() > 100000000) {
        *error = QStringLiteral("That image is too large to decode. Choose a smaller copy.");
        return {};
    }
    QImage image = reader.read();
    if (image.isNull()) {
        *error = QStringLiteral("That file could not be read as an image.");
        return {};
    }
    if (image.width() > 780 || image.height() > 1170)
        image = image.scaled(780, 1170, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray encoded;
    QBuffer output(&encoded);
    output.open(QIODevice::WriteOnly);
    if (!image.save(&output, "JPG", 90)) {
        *error = QStringLiteral("Could not prepare that image for the library.");
        return {};
    }
    return encoded;
}

QString albumOwner(const LibTrack &track) {
    const QString owner = track.albumartist.isEmpty() ? track.artist : track.albumartist;
    return owner.isEmpty() ? QStringLiteral("Unknown Artist") : owner;
}
QString albumCache(const QString &cache, const QString &artist, const QString &album) {
    return cache + QStringLiteral("/art/")
        + AlbumArtService::cacheKey(artist.isEmpty() ? QStringLiteral("Unknown Artist") : artist,
                                   album.isEmpty() ? QStringLiteral("Unknown Album") : album)
        + QStringLiteral(".jpg");
}
QString artistCache(const QString &cache, const QString &artist) {
    return cache + QStringLiteral("/artistart/") + ArtistImageService::cacheKey(artist)
        + QStringLiteral(".jpg");
}

struct CustomizeFile {
    QString path;
    QByteArray bytes, original;
    bool remove = false, existed = false;
};
bool writeCustomizeFile(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
struct CustomizeCommit {
    bool success = false, regroup = false, tracksChanged = false;
    QString error;
    QVector<qint64> videoIds;
    QVector<QPair<QString, QString>> albums;
    QStringList artists;
};
} // namespace

QString LibraryService::collectionArt(const QString &kind, const QString &key) const {
    if (!m_db || (kind != QLatin1String("genre") && kind != QLatin1String("mixtape"))) return {};
    const QString path = m_db->collectionCustomization(kind, key).value(QStringLiteral("artPath")).toString();
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString()
        + QStringLiteral("?v=%1").arg(m_collectionArtRevision) : QString();
}

void LibraryService::searchCustomizeMusicIdentity(const QString &requestId, const QString &kind,
                                                  const QString &query, const QString &provider,
                                                  const QString &artistHint) {
    auto *watcher = new QFutureWatcher<CustomizeLookup>(this);
    connect(watcher, &QFutureWatcher<CustomizeLookup>::finished, this, [this, watcher, requestId] {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit customizeIdentityReady(requestId, result.items, result.error);
    });
    watcher->setFuture(QtConcurrent::run([kind, query, provider, artistHint] {
        CustomizeLookup out;
        if (query.trimmed().isEmpty()) return out;
        MusicIdentityClient client;
        if (kind == QLatin1String("album")) out.items = client.searchAlbums(query, artistHint, &out.error);
        else if (kind == QLatin1String("artist")) out.items = client.searchArtists(query, provider, &out.error);
        else out.error = QStringLiteral("This item has no music identity to match.");
        return out;
    }));
}

void LibraryService::searchCustomizeIdentity(const QString &requestId,
                                              const QString &query, bool tv) {
    auto *watcher = new QFutureWatcher<CustomizeLookup>(this);
    connect(watcher, &QFutureWatcher<CustomizeLookup>::finished, this, [this, watcher, requestId] {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit customizeIdentityReady(requestId, result.items, result.error);
    });
    watcher->setFuture(QtConcurrent::run([query, tv] {
        CustomizeLookup out;
        if (query.trimmed().isEmpty()) return out;
        TmdbClient tmdb;
        bool ok = false;
        const auto results = tv ? tmdb.searchTV(query.trimmed(), &ok)
                                : tmdb.searchMovie(query.trimmed(), &ok);
        if (!ok) out.error = QStringLiteral("TMDB could not be reached. Try again.");
        for (const auto &r : results)
            out.items.append(QVariantMap{{"tmdbId", r.id}, {"title", r.title}, {"year", r.year},
                {"overview", r.overview}, {"posterUrl", r.posterPath.isEmpty() ? QString()
                    : QStringLiteral("https://image.tmdb.org/t/p/w185") + r.posterPath}});
        return out;
    }));
}

QVariantMap LibraryService::customizeContext(const QString &kind,
                                             const QVariantMap &context) const {
    QVariantMap out = context;
    if (kind == QLatin1String("genre") || kind == QLatin1String("mixtape")) {
        const QString key = kind == QLatin1String("genre") ? context.value(QStringLiteral("name")).toString()
            : QString::number(context.value(QStringLiteral("id")).toLongLong());
        if (kind == QLatin1String("mixtape")) {
            for (const auto &playlist : m_db->allPlaylists())
                if (QString::number(playlist.first) == key) out.insert(QStringLiteral("title"), playlist.second);
        } else out.insert(QStringLiteral("title"), key);
        out.insert(QStringLiteral("poster"), collectionArt(kind, key));
        out.insert(QStringLiteral("customArt"), !out.value(QStringLiteral("poster")).toString().isEmpty());
        return out;
    }
    if (kind == QLatin1String("movie") || kind == QLatin1String("series")) {
        const auto ids = context.value(QStringLiteral("ids")).toList();
        if (ids.isEmpty()) return out;
        for (const auto &video : m_videos) {
            if (video.id != ids.first().toLongLong()) continue;
            out.insert(QStringLiteral("title"), video.tmdbTitle.isEmpty()
                ? (video.isTV() && !video.series.isEmpty() ? video.series : video.filename) : video.tmdbTitle);
            out.insert(QStringLiteral("type"), video.isTV() ? QStringLiteral("tv") : QStringLiteral("movie"));
            out.insert(QStringLiteral("tv"), video.isTV());
            out.insert(QStringLiteral("tmdbId"), video.tmdbId);
            out.insert(QStringLiteral("year"), video.tmdbYear);
            out.insert(QStringLiteral("genres"), video.tmdbGenres);
            out.insert(QStringLiteral("overview"), video.description);
            out.insert(QStringLiteral("season"), video.season);
            out.insert(QStringLiteral("episode"), video.episode);
            out.insert(QStringLiteral("episodeTitle"), video.episodeTitle);
            out.insert(QStringLiteral("userEdited"), video.userEdited);
            out.insert(QStringLiteral("customArt"), video.customPoster);
            out.insert(QStringLiteral("filepath"), video.filepath);
            out.insert(QStringLiteral("durationMs"), video.durationMs);
            if (!video.tmdbPoster.isEmpty())
                out.insert(QStringLiteral("poster"), QUrl::fromLocalFile(video.tmdbPoster).toString());
            break;
        }
    } else if (kind == QLatin1String("album")) {
        const QString artist = context.value(QStringLiteral("artist")).toString();
        const QString album = context.value(QStringLiteral("album")).toString();
        for (const auto &track : m_tracks->rows()) {
            if (albumOwner(track).compare(artist, Qt::CaseInsensitive) != 0
                || track.album.compare(album, Qt::CaseInsensitive) != 0) continue;
            out.insert(QStringLiteral("album"), track.album);
            out.insert(QStringLiteral("title"), track.album);
            out.insert(QStringLiteral("artist"), albumOwner(track));
            out.insert(QStringLiteral("albumartist"), track.albumartist);
            out.insert(QStringLiteral("year"), track.year);
            out.insert(QStringLiteral("genre"), track.genre);
            out.insert(QStringLiteral("userEdited"), track.userEdited);
            break;
        }
    }
    if (kind == QLatin1String("album") || kind == QLatin1String("artist")) {
        const QString key = kind == QLatin1String("album")
            ? AlbumArtService::cacheKey(out.value(QStringLiteral("artist")).toString(), out.value(QStringLiteral("album")).toString())
            : ArtistImageService::cacheKey(out.value(QStringLiteral("name")).toString());
        const auto saved = m_db->collectionCustomization(kind, key);
        const auto identity = saved.value(QStringLiteral("identity")).toMap();
        out.insert(QStringLiteral("musicIdentity"), identity);
        out.insert(QStringLiteral("artworkMatch"), identity.value("_artworkMatch"));
        out.insert(QStringLiteral("artworkDiscovery"), kind == QLatin1String("album")
            ? m_albumArtworkStatus.value(artKey(context.value("artist").toString(), context.value("album").toString()))
            : m_artistArtworkStatus.value(context.value("name").toString().toLower()));
        out.insert(QStringLiteral("overview"), identity.value(QStringLiteral("overview")));
        out.insert(QStringLiteral("customArt"), !saved.value(QStringLiteral("artPath")).toString().isEmpty());
    }
    return out;
}

void LibraryService::requestCustomizeArt(const QString &requestId, const QString &kind,
                                         const QString &source, const QVariantMap &context) {
    QVariantMap ctx = context;
    if (source == QLatin1String("grab")) {
        const auto ids = ctx.value(QStringLiteral("ids")).toList();
        const qint64 id = ids.isEmpty() ? 0 : ids.first().toLongLong();
        for (const auto &video : std::as_const(m_videos)) {
            if (video.id != id) continue;
            ctx.insert(QStringLiteral("filepath"), video.filepath);
            ctx.insert(QStringLiteral("durationMs"), video.durationMs);
            break;
        }
    }
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    auto *watcher = new QFutureWatcher<CustomizeLookup>(this);
    connect(watcher, &QFutureWatcher<CustomizeLookup>::finished, this, [this, watcher, requestId] {
        const auto result = watcher->result();
        watcher->deleteLater();
        emit customizeArtReady(requestId, result.items, result.error);
    });
    watcher->setFuture(QtConcurrent::run([kind, source, ctx, cache] {
        CustomizeLookup out;
        if (source == QLatin1String("grab")) {
            QString path = ctx.value(QStringLiteral("filepath")).toString();
            if (QUrl(path).isLocalFile()) path = QUrl(path).toLocalFile();
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                out.error = QStringLiteral("The video file is unavailable for frame grabs.");
                return out;
            }
            const QString dir = cache + QStringLiteral("/vidthumbs");
            QDir().mkpath(dir);
            const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            const double duration = ctx.value(QStringLiteral("durationMs")).toDouble() / 1000.0;
            for (int i = 0; i < 5; ++i) {
                const QString dest = dir + QStringLiteral("/grab_%1_%2.jpg").arg(token).arg(i);
                const double seconds = duration > 0 ? duration * (0.1 + i * 0.2) : 10.0 + i * 30.0;
                if (zuuned_extract_frame(path.toUtf8().constData(), seconds,
                                          dest.toUtf8().constData(), 500) == 0
                    && QFileInfo::exists(dest))
                    out.items.append(QUrl::fromLocalFile(dest).toString());
            }
            if (out.items.isEmpty()) out.error = QStringLiteral("No frames could be extracted from this video.");
            return out;
        }
        if (kind == QLatin1String("album")) {
            MusicIdentityClient client;
            const auto chosen = ctx.value(QStringLiteral("musicIdentity")).toMap();
            const QString mbid = chosen.value("releaseGroupId", chosen.value("providerId", chosen.value("mbid"))).toString();
            const QString releaseId = chosen.value("releaseId").toString();
            if (!releaseId.isEmpty()) {
                // A chosen edition must never browse general art as if exact.
                out.items = customizeUrls(client.caaReleaseUrls(releaseId, &out.error));
                return out;
            }
            if (source == QLatin1String("fanart")) {
                QString id = mbid;
                if (id.isEmpty()) {
                    const auto matches = client.searchAlbums(ctx.value(QStringLiteral("album")).toString(),
                        ctx.value(QStringLiteral("artist")).toString(), &out.error);
                    if (!matches.isEmpty()) id = matches.first().toMap().value(QStringLiteral("mbid")).toString();
                }
                if (!id.isEmpty()) out.items = customizeUrls(client.fanartAlbumUrls(id, &out.error));
                return out;
            }
            if (!mbid.isEmpty()) {
                out.items = customizeUrls(client.caaAlbumUrls(mbid, &out.error));
                return out;
            }
            out.items = caaAlbumUrls(ctx.value(QStringLiteral("artist")).toString(),
                                     ctx.value(QStringLiteral("album")).toString(), &out.error);
            return out;
        }
        if (kind == QLatin1String("artist")) {
            MusicIdentityClient client;
            const auto identity = ctx.value(QStringLiteral("musicIdentity")).toMap();
            if (source == QLatin1String("fanart")) {
                QString mbid = identity.value("artistMbid", identity.value("mbid", identity.value("providerId"))).toString();
                if (mbid.isEmpty()) {
                    out.items = customizeUrls(client.searchFanartArtistUrls(ctx.value(QStringLiteral("name")).toString(), &out.error));
                } else out.items = customizeUrls(client.fanartArtistUrls(mbid, &out.error));
                return out;
            }
            if (identity.value(QStringLiteral("provider")).toString() == QLatin1String("deezer")
                && !identity.value(QStringLiteral("providerId")).toString().isEmpty()) {
                const auto details = client.artistDetails(QStringLiteral("deezer"), identity.value(QStringLiteral("providerId")).toString(), &out.error);
                const QString url = details.value(QStringLiteral("posterUrl")).toString();
                if (!url.isEmpty()) out.items.append(url);
                return out;
            }
            if (identity.value("provider").toString() == QLatin1String("musicbrainz")
                && !identity.value("providerId", identity.value("mbid")).toString().isEmpty()) {
                const auto result = client.resolveArtistArtwork(ctx.value("name").toString(), identity);
                if (!result.imageUrl.isEmpty()) out.items.append(result.imageUrl);
                else if (result.status == QLatin1String("providerUnavailable")) out.error = result.reason;
                return out;
            }
            out.items = deezerArtistUrls(ctx.value(QStringLiteral("name")).toString(), &out.error);
            return out;
        }
        if (kind != QLatin1String("movie") && kind != QLatin1String("series")) {
            out.error = QStringLiteral("Artwork is unavailable for this item type.");
            return out;
        }
        TmdbClient tmdb;
        const bool tv = ctx.value(QStringLiteral("tv"), kind == QLatin1String("series")).toBool();
        int id = ctx.value(QStringLiteral("tmdbId")).toInt();
        bool ok = false;
        if (id <= 0) {
            const QString query = ctx.value(QStringLiteral("title")).toString().trimmed();
            if (query.isEmpty()) return out;
            const auto matches = tv ? tmdb.searchTV(query, &ok) : tmdb.searchMovie(query, &ok);
            if (!ok) out.error = QStringLiteral("TMDB could not be reached. Try again.");
            if (matches.isEmpty()) return out;
            id = matches.first().id;
        }
        if (source == QLatin1String("fanart")) {
            FanartClient fan(&tmdb);
            const int providerId = tv ? tmdb.tvdbIdFor(id) : id;
            if (providerId <= 0) return out;
            const auto urls = tv ? fan.tvPosterUrls(providerId, &ok) : fan.moviePosterUrls(id, &ok);
            for (const auto &url : urls) out.items.append(url);
        } else {
            const auto posters = tv ? tmdb.tvPosters(id, &ok) : tmdb.moviePosters(id, &ok);
            for (const auto &poster : posters)
                out.items.append(QStringLiteral("https://image.tmdb.org/t/p/w500") + poster.filePath);
        }
        if (!ok) out.error = QStringLiteral("The artwork provider could not be reached. Try again.");
        return out;
    }));
}

void LibraryService::applyCustomization(const QString &requestId, const QString &kind,
                                        const QVariantMap &context, const QVariantMap &draft) {
    if (m_customizationBusy) {
        emit customizationFinished(requestId, false, QStringLiteral("Another customization is still saving."));
        return;
    }
    const bool music = kind == QLatin1String("album") || kind == QLatin1String("artist");
    const bool collection = kind == QLatin1String("genre") || kind == QLatin1String("mixtape");
    if (!music && !collection && kind != QLatin1String("movie") && kind != QLatin1String("series")) {
        emit customizationFinished(requestId, false, QStringLiteral("This item type cannot be customized yet."));
        return;
    }
    // Stable target IDs, using the same canonical album-owner grouping as
    // reloadFromDb (a featured performer must not pull in unrelated albums).
    QVector<qint64> ids;
    if (music) {
        const QString owner = context.value(kind == QLatin1String("album")
            ? QStringLiteral("artist") : QStringLiteral("name")).toString();
        const QString album = context.value(QStringLiteral("album")).toString();
        for (const auto &track : m_tracks->rows()) {
            if (albumOwner(track).compare(owner, Qt::CaseInsensitive) != 0) continue;
            if (kind == QLatin1String("album") && track.album.compare(album, Qt::CaseInsensitive) != 0) continue;
            ids.append(track.id);
        }
    } else {
        for (const auto &id : context.value(QStringLiteral("ids")).toList())
            if (!ids.contains(id.toLongLong())) ids.append(id.toLongLong());
    }
    if (ids.isEmpty() && !collection) {
        emit customizationFinished(requestId, false, QStringLiteral("This item is no longer in the library."));
        return;
    }
    const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/library.db");
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_customizationBusy = true;
    if (music) m_onlineAlbumArt->setPaused(true);
    if (kind == QLatin1String("artist")) {
        m_artistImagesSvc->setCustomizationPaused(true);
    }
    auto *watcher = new QFutureWatcher<CustomizeCommit>(this);
    connect(watcher, &QFutureWatcher<CustomizeCommit>::finished, this,
            [this, watcher, requestId, music, collection, kind] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (result.success) {
            for (const auto &pair : result.albums) {
                m_onlineAlbumArt->invalidate(pair.first, pair.second);
                const QString path = m_art->cachedArtPath(pair.first, pair.second);
                const QString key = artKey(pair.first, pair.second);
                if (path.isEmpty()) {
                    m_artPaths.remove(key);
                    m_art->forgetFailure(pair.first, pair.second);
                } else {
                    m_artPaths.insert(key, bustedUrl(path));
                    m_domColorCache.remove(path);
                }
            }
            for (const auto &name : result.artists) {
                m_artistImagesSvc->invalidate(name);
                const QString path = m_artistImagesSvc->cachedPath(name);
                if (path.isEmpty()) {
                    m_artistImages.remove(name.toLower());
                    m_artistImagesSvc->forgetFailure(name);
                    requestArtistImage(name);
                } else {
                    m_artistImages.insert(name.toLower(), bustedUrl(path));
                    m_domColorCache.remove(path);
                }
            }
            if (collection) {
                ++m_collectionArtRevision;
                emit collectionArtChanged();
            } else if (music) {
                if (result.tracksChanged) reloadFromDb();
                if (!result.albums.isEmpty()) emit artChanged();
                if (!result.artists.isEmpty()) emit artistImagesChanged();
                for (const auto &pair : result.albums) {
                    if (!m_art->cachedArtPath(pair.first, pair.second).isEmpty()) continue;
                    for (const auto &track : m_tracks->rows()) {
                        if (albumOwner(track).compare(pair.first, Qt::CaseInsensitive) == 0
                            && track.album.compare(pair.second, Qt::CaseInsensitive) == 0) {
                            requestArt(pair.first, pair.second, track.filepath);
                            break;
                        }
                    }
                }
            } else {
                if (result.regroup) reloadVideos();
                else patchVideoRows(result.videoIds);
                pokeVideoConsumers();
            }
        }
        if (kind == QLatin1String("artist")) m_artistImagesSvc->setCustomizationPaused(false);
        if (music) {
            m_onlineAlbumArt->setPaused(false);
            m_artworkDiscovery->start();
        }
        m_customizationBusy = false;
        emit customizationFinished(requestId, result.success, result.error);
    });
    watcher->setFuture(QtConcurrent::run([kind, context, draft, music, collection, ids, dbPath, cache] {
        CustomizeCommit out;
        const QString mode = draft.value(QStringLiteral("identityMode"), QStringLiteral("keep")).toString();
        QVariantMap fields = draft.value(QStringLiteral("fields")).toMap();
        const bool resetArt = draft.value(QStringLiteral("resetArt")).toBool();
        const QString artUrl = draft.value(QStringLiteral("artUrl")).toString();
        const bool manual = mode == QLatin1String("manual");
        const bool match = mode == QLatin1String("match");
        const bool reset = mode == QLatin1String("reset");
        if (mode != QLatin1String("keep") && !manual && !match && !reset) {
            out.error = QStringLiteral("Unknown identity choice."); return out;
        }
        if (collection && mode != QLatin1String("keep")) {
            out.error = QStringLiteral("Only artwork is edited here."); return out;
        }
        QVariantMap artworkMatch = draft.value(QStringLiteral("artworkMatch")).toMap();
        if (!artworkMatch.isEmpty()) {
            const QString provider = artworkMatch.value("provider").toString();
            QString id = artworkMatch.value("providerId", artworkMatch.value("mbid")).toString();
            if (id.isEmpty()) id = artworkMatch.value("releaseGroupId").toString();
            const QString releaseId = artworkMatch.value("releaseId").toString();
            const QString scope = artworkMatch.value("scope").toString();
            const bool valid = music && mode == QLatin1String("keep")
                && ((provider == QLatin1String("musicbrainz") && MusicIdentityClient::validMbid(id)
                    && (releaseId.isEmpty() || MusicIdentityClient::validMbid(releaseId)))
                    || (kind == QLatin1String("artist") && provider == QLatin1String("deezer")
                        && QRegularExpression(QStringLiteral("^[1-9][0-9]*$")).match(id).hasMatch()))
                && ((kind == QLatin1String("artist") && scope == QLatin1String("artist"))
                    || (kind == QLatin1String("album") && (scope == QLatin1String("general")
                        || (scope == QLatin1String("exact") && !releaseId.isEmpty()))));
            if (!valid) { out.error = QStringLiteral("Choose a valid artwork match without changing identity at the same time."); return out; }
            artworkMatch.insert("providerId", id);
            if (kind == QLatin1String("album")) artworkMatch.insert("releaseGroupId", id);
            artworkMatch.insert("choice", "manual");
        }
        QVariantMap musicIdentity;
        if (music && match) {
            MusicIdentityClient client;
            const auto chosen = draft.value(QStringLiteral("musicIdentity")).toMap();
            musicIdentity = kind == QLatin1String("album")
                ? client.albumDetails(chosen.value(QStringLiteral("mbid")).toString(), &out.error)
                : client.artistDetails(chosen.value(QStringLiteral("provider")).toString(), chosen.value(QStringLiteral("providerId")).toString(), &out.error);
            if (!out.error.isEmpty() || musicIdentity.value(QStringLiteral("title")).toString().isEmpty()) {
                if (out.error.isEmpty()) out.error = QStringLiteral("Could not load that music match. Your changes have not been saved.");
                return out;
            }
            fields.insert(QStringLiteral("title"), musicIdentity.value(QStringLiteral("title")));
            fields.insert(kind == QLatin1String("album") ? QStringLiteral("album") : QStringLiteral("artist"), musicIdentity.value(QStringLiteral("title")));
            if (kind == QLatin1String("album")) {
                if (!musicIdentity.value(QStringLiteral("artist")).toString().isEmpty()) fields.insert(QStringLiteral("albumartist"), musicIdentity.value(QStringLiteral("artist")));
                if (!musicIdentity.value(QStringLiteral("year")).toString().isEmpty()) fields.insert(QStringLiteral("year"), musicIdentity.value(QStringLiteral("year")));
                if (!musicIdentity.value(QStringLiteral("genre")).toString().isEmpty()) fields.insert(QStringLiteral("genre"), musicIdentity.value(QStringLiteral("genre")));
            }
        }
        const QString nameField = !music ? QStringLiteral("title")
            : kind == QLatin1String("album") ? QStringLiteral("album") : QStringLiteral("artist");
        if (manual && fields.value(nameField).toString().trimmed().isEmpty()) {
            out.error = QStringLiteral("Give this item a name before saving."); return out;
        }
        for (const QString &key : {QStringLiteral("year"), QStringLiteral("season"), QStringLiteral("episode")}) {
            if (!manual || !fields.contains(key) || fields.value(key).toString().trimmed().isEmpty()) continue;
            bool valid = false;
            const int value = fields.value(key).toString().toInt(&valid);
            if (!valid || value < 0 || value > 9999) {
                out.error = QStringLiteral("%1 must be a number between 0 and 9999.").arg(key); return out;
            }
        }
        QByteArray art;
        if (!artUrl.isEmpty()) {
            art = prepareCustomizeImage(artUrl, &out.error);
            if (!out.error.isEmpty()) return out;
        }
        TmdbDetails details;
        const bool tv = draft.value(QStringLiteral("tv"), kind == QLatin1String("series")).toBool();
        if (match && !music && !collection) {
            TmdbClient tmdb;
            const int id = draft.value(QStringLiteral("tmdbId")).toInt();
            if (id <= 0 || !(tv ? tmdb.tvDetails(id, &details) : tmdb.movieDetails(id, &details))
                || details.id <= 0 || details.title.isEmpty()) {
                out.error = QStringLiteral("Could not load that match. Your changes have not been saved."); return out;
            }
        }
        LibraryDb db;
        if (!db.open(dbPath) || !db.begin()) {
            out.error = QStringLiteral("Could not open the library for saving: %1").arg(db.lastError()); return out;
        }
        QVector<LibTrack> changedTracks;
        QVector<LibVideo> changedVideos;
        QVector<CustomizeFile> files;
        auto stageFile = [&files](const QString &path, const QByteArray &bytes, bool remove = false) {
            for (auto &file : files) {
                if (file.path != path) continue;
                file.bytes = bytes; file.remove = remove; return;
            }
            CustomizeFile file;
            file.path = path; file.bytes = bytes; file.remove = remove;
            files.append(file);
        };
        auto carryArt = [&stageFile, &out](const QString &from, const QString &to) {
            if (from == to || !QFileInfo::exists(from)) return;
            QFile input(from);
            if (!input.open(QIODevice::ReadOnly)) {
                out.error = QStringLiteral("Could not preserve the existing artwork."); return;
            }
            stageFile(to, input.readAll());
        };
        if (collection) {
            const QString key = kind == QLatin1String("genre") ? context.value(QStringLiteral("name")).toString()
                : QString::number(context.value(QStringLiteral("id")).toLongLong());
            bool exists = false;
            if (kind == QLatin1String("mixtape")) {
                for (const auto &playlist : db.allPlaylists()) if (QString::number(playlist.first) == key) exists = true;
            } else {
                for (const auto &track : db.allTracks())
                    if ((track.genre.isEmpty() ? QStringLiteral("Unknown Genre") : track.genre) == key) exists = true;
            }
            if (!exists) { db.rollback(); out.error = QStringLiteral("This collection is no longer in the library."); return out; }
            const QString oldPath = db.collectionCustomization(kind, key).value(QStringLiteral("artPath")).toString();
            QString path = oldPath;
            if (!art.isEmpty()) {
                path = QFileInfo(dbPath).absolutePath() + QStringLiteral("/custom-art/%1_%2.jpg")
                    .arg(kind, QUuid::createUuid().toString(QUuid::WithoutBraces));
                stageFile(path, art);
            } else if (resetArt) path.clear();
            if (!db.setCollectionCustomization(kind, key, {}, path)) out.error = QStringLiteral("Could not save this collection’s artwork.");
            if (oldPath != path && !oldPath.isEmpty()) stageFile(oldPath, {}, true);
        } else if (music) {
            const bool editMusic = manual || match;
            QSet<QString> movedAlbumKeys;
            const auto tracks = db.allTracks();
            for (const auto &old : tracks) {
                if (!ids.contains(old.id)) continue;
                LibTrack track = old;
                if (editMusic && kind == QLatin1String("album")) {
                    track.album = fields.value(QStringLiteral("album")).toString().trimmed();
                    if (fields.contains(QStringLiteral("albumartist")))
                        track.albumartist = fields.value(QStringLiteral("albumartist")).toString().trimmed();
                    if (fields.contains(QStringLiteral("genre"))) track.genre = fields.value(QStringLiteral("genre")).toString().trimmed();
                    if (fields.contains(QStringLiteral("year"))) track.year = fields.value(QStringLiteral("year")).toInt();
                } else if (editMusic) {
                    const QString oldName = context.value(QStringLiteral("name")).toString();
                    const QString newName = fields.value(QStringLiteral("artist")).toString().trimmed();
                    if (track.artist.compare(oldName, Qt::CaseInsensitive) == 0) track.artist = newName;
                    if (track.albumartist.compare(oldName, Qt::CaseInsensitive) == 0) track.albumartist = newName;
                    if (old.artist.isEmpty() && old.albumartist.isEmpty()) track.artist = newName;
                }
                const QString newOwner = albumOwner(track);
                const auto pair = qMakePair(newOwner, track.album);
                if (editMusic && (albumOwner(old) != newOwner || old.album != track.album)) {
                    carryArt(albumCache(cache, albumOwner(old), old.album), albumCache(cache, newOwner, track.album));
                    if (!out.albums.contains(pair)) out.albums.append(pair);
                    if (kind == QLatin1String("artist")) {
                        const QString fromKey = AlbumArtService::cacheKey(albumOwner(old), old.album);
                        const QString toKey = AlbumArtService::cacheKey(newOwner, track.album);
                        if (fromKey != toKey && !movedAlbumKeys.contains(fromKey)) {
                            movedAlbumKeys.insert(fromKey);
                            const auto albumChoice = db.collectionCustomization(QStringLiteral("album"), fromKey);
                            const auto destination = db.collectionCustomization(QStringLiteral("album"), toKey);
                            if (hasManualCollectionChoice(destination)) {
                                out.error = QStringLiteral("An album under that artist already has custom choices. Choose a different artist name to keep both.");
                                continue;
                            }
                            if (!albumChoice.isEmpty()) {
                                const QString pinnedPath = albumChoice.value(QStringLiteral("artPath")).toString().isEmpty() ? QString()
                                    : albumCache(cache, newOwner, track.album);
                                if (!db.setCollectionCustomization(QStringLiteral("album"), toKey, albumChoice.value(QStringLiteral("identity")).toMap(), pinnedPath)
                                    || !db.removeCollectionCustomization(QStringLiteral("album"), fromKey))
                                    out.error = QStringLiteral("Could not preserve an album’s customization during the artist change.");
                            }
                        }
                    }
                }
                if (kind == QLatin1String("album")) {
                    const QString path = albumCache(cache, newOwner, track.album);
                    if (!art.isEmpty()) stageFile(path, art);
                    else if (resetArt || reset) stageFile(path, {}, true);
                    if ((!art.isEmpty() || resetArt || reset) && !out.albums.contains(pair)) out.albums.append(pair);
                }
                changedTracks.append(track);
            }
            if (kind == QLatin1String("artist")) {
                const QString oldName = context.value(QStringLiteral("name")).toString();
                const QString name = editMusic ? fields.value(QStringLiteral("artist")).toString().trimmed() : oldName;
                const QString path = artistCache(cache, name);
                if (editMusic) carryArt(artistCache(cache, oldName), path);
                if (!art.isEmpty()) stageFile(path, art);
                else if (resetArt || reset) stageFile(path, {}, true);
                if (editMusic || !art.isEmpty() || resetArt || reset) out.artists.append(name);
            }
            if (changedTracks.size() != ids.size() && out.error.isEmpty()) out.error = QStringLiteral("Some tracks left the library while this sheet was open.");
            if (!changedTracks.isEmpty()) {
                const auto &first = changedTracks.first();
                const QString oldKey = kind == QLatin1String("album") ? AlbumArtService::cacheKey(context.value(QStringLiteral("artist")).toString(), context.value(QStringLiteral("album")).toString())
                    : ArtistImageService::cacheKey(context.value(QStringLiteral("name")).toString());
                const QString newKey = kind == QLatin1String("album") ? AlbumArtService::cacheKey(albumOwner(first), first.album)
                    : ArtistImageService::cacheKey(editMusic ? fields.value(QStringLiteral("artist")).toString().trimmed() : context.value(QStringLiteral("name")).toString());
                const auto saved = db.collectionCustomization(kind, oldKey);
                const auto destination = oldKey == newKey ? QVariantMap() : db.collectionCustomization(kind, newKey);
                if (hasManualCollectionChoice(destination)) {
                    out.error = QStringLiteral("That name already belongs to a customized item. Choose a different name to keep both.");
                    db.rollback(); return out;
                }
                if (manual) musicIdentity = {{QStringLiteral("provider"), QStringLiteral("manual")}, {QStringLiteral("overview"), fields.value(QStringLiteral("overview"))}};
                else if (!match) musicIdentity = saved.value(QStringLiteral("identity")).toMap();
                // An artwork-only match is a separate staged choice. It never
                // changes canonical names, tags or the collection's identity.
                if (!artworkMatch.isEmpty()) {
                    musicIdentity.insert("_artworkMatch", artworkMatch);
                    musicIdentity.remove("_artworkDiscovery");
                    if (kind == QLatin1String("album")) {
                        const auto pair = qMakePair(albumOwner(first), first.album);
                        if (!out.albums.contains(pair)) out.albums.append(pair);
                    } else {
                        const QString name = context.value("name").toString();
                        if (!out.artists.contains(name)) out.artists.append(name);
                    }
                }
                if (!art.isEmpty() || resetArt || reset) musicIdentity.remove("_artworkDiscovery");
                // Even a reset of an empty image invalidates queued discovery saves.
                musicIdentity.insert("_artworkChoiceRevision", QUuid::createUuid().toString(QUuid::WithoutBraces));
                QString artPath = saved.value(QStringLiteral("artPath")).toString();
                if (!art.isEmpty() || (!artPath.isEmpty() && editMusic)) artPath = kind == QLatin1String("album")
                    ? albumCache(cache, albumOwner(first), first.album) : artistCache(cache, editMusic ? fields.value(QStringLiteral("artist")).toString().trimmed() : context.value(QStringLiteral("name")).toString());
                if (art.isEmpty() && (resetArt || reset)) artPath.clear();
                if (!db.setCollectionCustomization(kind, newKey, musicIdentity, artPath)
                    || (oldKey != newKey && !db.removeCollectionCustomization(kind, oldKey))) out.error = QStringLiteral("Could not save the chosen music identity.");
            }
        } else {
            const auto videos = db.allVideos();
            const auto folders = reset ? db.watchFolders() : QVector<WatchFolder>();
            QString poster;
            if (!art.isEmpty()) {
                poster = cache + QStringLiteral("/vidthumbs/custom_%1.jpg")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
                stageFile(poster, art);
            }
            for (const auto &old : videos) {
                if (!ids.contains(old.id)) continue;
                LibVideo video = old;
                if (reset) {
                    video.tmdbId = 0; video.tmdbCached = false; video.userEdited = false;
                    video.customPoster = false;
                    video.tmdbTitle.clear(); video.tmdbPoster.clear(); video.tmdbYear.clear();
                    video.tmdbCast.clear(); video.tmdbDirector.clear(); video.tmdbGenres.clear();
                    video.tmdbRating = 0; video.description.clear();
                    video.lookupAttempts = 0; video.lastLookupAt = 0;
                    QString folderType;
                    qsizetype longestRoot = -1;
                    for (const auto &folder : folders) {
                        QString root = QDir::cleanPath(folder.path);
                        if (QUrl(root).isLocalFile()) root = QUrl(root).toLocalFile();
                        if (!root.endsWith(QLatin1Char('/'))) root += QLatin1Char('/');
                        if (video.filepath.startsWith(root) && root.size() > longestRoot) {
                            folderType = folder.type; longestRoot = root.size();
                        }
                    }
                    const auto parsed = VideoNaming::parseIdentity(video.filepath, folderType);
                    video.category = parsed.category; video.series = parsed.series;
                    video.season = parsed.season; video.episode = parsed.episode;
                    video.episodeTitle.clear();
                } else if (manual) {
                    const bool manualTv = fields.value(QStringLiteral("type")).toString() == QLatin1String("tv");
                    video.tmdbId = -1; video.tmdbCached = true; video.userEdited = true;
                    video.category = manualTv ? QStringLiteral("tv") : QStringLiteral("movie");
                    video.tmdbTitle = fields.value(QStringLiteral("title")).toString().trimmed();
                    video.tmdbYear = fields.value(QStringLiteral("year")).toString().trimmed();
                    video.tmdbGenres = fields.value(QStringLiteral("genres")).toString().trimmed();
                    video.description = fields.value(QStringLiteral("overview")).toString().trimmed();
                    video.tmdbCast.clear(); video.tmdbDirector.clear(); video.tmdbRating = 0;
                    video.series = manualTv ? video.tmdbTitle : QString();
                    if (!manualTv) { video.season = 0; video.episode = 0; video.episodeTitle.clear(); }
                    else if (ids.size() == 1) {
                        video.season = fields.value(QStringLiteral("season")).toInt();
                        video.episode = fields.value(QStringLiteral("episode")).toInt();
                        video.episodeTitle = fields.value(QStringLiteral("episodeTitle")).toString().trimmed();
                    }
                } else if (match) {
                    video.tmdbId = details.id; video.tmdbCached = true; video.userEdited = true;
                    video.category = tv ? QStringLiteral("tv") : QStringLiteral("movie");
                    video.tmdbTitle = details.title; video.tmdbYear = details.year;
                    video.tmdbGenres = details.genres; video.description = details.overview;
                    video.tmdbCast = details.cast; video.tmdbDirector = details.director; video.tmdbRating = details.rating;
                    video.series = tv ? details.title : QString();
                    if (!tv) { video.season = 0; video.episode = 0; video.episodeTitle.clear(); }
                }
                if (resetArt && !reset) { video.tmdbPoster.clear(); video.customPoster = false; }
                if (!poster.isEmpty()) { video.tmdbPoster = poster; video.customPoster = true; }
                out.regroup |= (old.tmdbId == 0) != (video.tmdbId == 0)
                    || old.category != video.category || old.series != video.series;
                out.videoIds.append(video.id);
                changedVideos.append(video);
            }
            if (changedVideos.size() != ids.size()) out.error = QStringLiteral("Some videos left the library while this sheet was open.");
        }
        // Prepare rollback copies before changing either the cache or rows.
        for (auto &file : files) {
            file.existed = QFileInfo::exists(file.path);
            if (!file.existed) continue;
            QFile input(file.path);
            if (!input.open(QIODevice::ReadOnly)) {
                out.error = QStringLiteral("The existing artwork could not be opened for saving."); break;
            }
            file.original = input.readAll();
        }
        if (!out.error.isEmpty()) { db.rollback(); return out; }
        bool ok = true;
        if ((manual || match) && music) {
            for (const auto &track : changedTracks)
                if (!(ok = db.updateTrackMetadata(track.id, track, true))) break;
            out.tracksChanged = true;
        }
        if (ok)
            for (const auto &video : changedVideos)
                if (!(ok = db.updateVideoCustomization(video))) break;
        int installed = 0;
        if (ok) {
            for (const auto &file : files) {
                ok = file.remove ? (!QFileInfo::exists(file.path) || QFile::remove(file.path))
                                 : writeCustomizeFile(file.path, file.bytes);
                if (!ok) break;
                ++installed;
            }
        }
        if (ok) ok = db.commit();
        if (!ok) {
            db.rollback();
            bool restored = true;
            for (int i = installed - 1; i >= 0; --i) {
                const auto &file = files.at(i);
                if (file.existed) restored &= writeCustomizeFile(file.path, file.original);
                else if (QFileInfo::exists(file.path)) restored &= QFile::remove(file.path);
            }
            out.error = restored ? QStringLiteral("Could not save the customization. Your previous choices were restored.")
                                 : QStringLiteral("Saving failed and an artwork cache file could not be restored. Your metadata was not changed.");
            return out;
        }
        // Tags are an export of the authoritative library. Rewrites can
        // copy large media files, so stay on this worker until they finish.
        int tagFailures = 0;
        if (out.tracksChanged)
            for (const auto &track : changedTracks)
                if (!writeId3IfMpeg(track)) ++tagFailures;
        out.success = true;
        if (tagFailures > 0)
            out.error = QStringLiteral("Saved to your library. Tags could not be written to %1 media file(s).").arg(tagFailures);
        return out;
    }));
}
