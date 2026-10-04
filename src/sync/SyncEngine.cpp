#include "SyncEngine.h"
#include "RecoveryIdentity.h"

#include "../DeviceService.h"
#include "../LibraryService.h"
#include "../library/VideoBrowserModel.h"
#include "../library/VideoNaming.h"
#include "../library/VideoIdentity.h"
#include "../DeviceWorker.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>

#include <cstdio>
#include <cstdlib>
#include <limits>

extern "C" {
#include "libav_transcode.h"
#include "id3_rewrite.h"
}

// ════════════════════════════════════════════════════════════════════
//  Pure logic (unit-testable, no Qt object state)
// ════════════════════════════════════════════════════════════════════

// Port of SyncManager.normalizeDiscFolder: two-step disc-marker
// normalization so multi-disc albums share one bucket key.
QString SyncEngine::normalizeDiscFolder(const QString &folder) {
    const QString name = QFileInfo(folder).fileName();

    // Pattern A: the folder IS a disc marker ("CD 1", "Disc 2", …)
    static const QRegularExpression discOnly(
        QStringLiteral("^(?:cd|disc|disk|d)\\s*\\d+$"),
        QRegularExpression::CaseInsensitiveOption);
    if (discOnly.match(name).hasMatch())
        return QFileInfo(folder).path();   // grandparent-of-file

    // Pattern B: trailing disc suffix ("Halo 3 (Disc 1)" → "Halo 3")
    static const QRegularExpression trailing(
        QStringLiteral("\\s*[\\(\\[]*(?:cd|disc|disk|d)\\s*\\d+[\\)\\]]*\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    QString stripped = name;
    stripped.remove(trailing);
    stripped = stripped.trimmed();
    if (stripped != name && !stripped.isEmpty())
        return QFileInfo(folder).path() + QLatin1Char('/') + stripped;

    return folder;
}

QVector<QVariantMap> SyncEngine::canonicalizeAlbumArtists(QVector<QVariantMap> entries) {
    // [sync-canon] entry snapshot — raw inputs before bucketing.
    for (const QVariantMap &e : std::as_const(entries)) {
        if (e.value(QStringLiteral("type"), QStringLiteral("track")).toString()
                != QLatin1String("track"))
            continue;
        const QString folder = QFileInfo(e.value(QStringLiteral("filepath"))
                                             .toString()).path();
        fprintf(stderr,
                "[sync-canon] entry title='%s' artist='%s' albumartist='%s' "
                "album='%s' folder='%s'\n",
                qPrintable(e.value(QStringLiteral("title")).toString()),
                qPrintable(e.value(QStringLiteral("artist")).toString()),
                qPrintable(e.value(QStringLiteral("albumartist")).toString()),
                qPrintable(e.value(QStringLiteral("album")).toString()),
                qPrintable(folder));
    }

    // Bucket by (album lowercased+trimmed, disc-normalized parent folder).
    QMap<QString, QVector<int>> buckets;   // "album\nfolder" → indices
    QMap<QString, QString> bucketFolder;   // for the trace line
    for (int idx = 0; idx < entries.size(); idx++) {
        const QVariantMap &e = entries[idx];
        if (e.value(QStringLiteral("type"), QStringLiteral("track")).toString()
                != QLatin1String("track"))
            continue;
        const QString album =
            e.value(QStringLiteral("album")).toString().trimmed().toLower();
        if (album.isEmpty())
            continue;
        const QString rawFolder =
            QFileInfo(e.value(QStringLiteral("filepath")).toString()).path();
        const QString folder = normalizeDiscFolder(rawFolder);
        const QString key = album + QLatin1Char('\n') + folder;
        buckets[key].append(idx);
        bucketFolder[key] = folder;
    }

    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
        const QVector<int> &idxs = it.value();
        if (idxs.size() <= 1)
            continue;   // singles don't need canonicalizing

        // Tally non-empty albumartist (falling back to artist).
        QMap<QString, int> counts;
        for (int i : idxs) {
            const QVariantMap &e = entries[i];
            QString candidate = e.value(QStringLiteral("albumartist")).toString();
            if (candidate.isEmpty())
                candidate = e.value(QStringLiteral("artist")).toString();
            candidate = candidate.trimmed();
            if (!candidate.isEmpty())
                counts[candidate]++;
        }

        QString canonical;
        QString strategy;
        if (counts.isEmpty()) {
            canonical = QStringLiteral("Unknown Artist");
            strategy = QStringLiteral("all-empty");
        } else {
            // Sort by count desc (mac's sorted { $0.value > $1.value }).
            QVector<QPair<QString, int>> sorted;
            for (auto c = counts.cbegin(); c != counts.cend(); ++c)
                sorted.append({c.key(), c.value()});
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            const int topVotes = sorted[0].second;
            QVector<QString> tiedAtTop;
            for (const auto &p : sorted)
                if (p.second == topVotes)
                    tiedAtTop.append(p.first);

            if (tiedAtTop.size() == 1) {
                canonical = sorted[0].first;
                strategy = QStringLiteral("unique-mode");
            } else if (counts.size() >= 3) {
                canonical = QStringLiteral("Various Artists");
                strategy = QStringLiteral("3+-distinct-various");
            } else {
                // Tie → shortest (strips "feat. X" in practice); equal
                // lengths break lexicographically for determinism.
                canonical = *std::min_element(
                    tiedAtTop.cbegin(), tiedAtTop.cend(),
                    [](const QString &a, const QString &b) {
                        if (a.size() != b.size()) return a.size() < b.size();
                        return a < b;
                    });
                strategy = QStringLiteral("tie-shortest");
            }
        }

        const QString originalAlbum =
            entries[idxs.first()].value(QStringLiteral("album")).toString();
        fprintf(stderr, "[sync-canon] bucket '%s' @ '%s' : %d tracks\n",
                qPrintable(originalAlbum), qPrintable(bucketFolder[it.key()]),
                int(idxs.size()));
        QStringList votes;
        {
            QVector<QPair<QString, int>> sorted;
            for (auto c = counts.cbegin(); c != counts.cend(); ++c)
                sorted.append({c.key(), c.value()});
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto &a, const auto &b) { return a.second > b.second; });
            for (const auto &p : sorted)
                votes << QStringLiteral("'%1'×%2").arg(p.first).arg(p.second);
        }
        fprintf(stderr, "[sync-canon]   votes: %s\n",
                votes.isEmpty() ? "(none)" : qPrintable(votes.join(QStringLiteral(", "))));
        fprintf(stderr, "[sync-canon]   → canonical '%s' (strategy: %s)\n",
                qPrintable(canonical), qPrintable(strategy));

        for (int i : idxs) {
            entries[i][QStringLiteral("albumartist")] = canonical;
            entries[i][QStringLiteral("artist")] = canonical;
        }
        fprintf(stderr,
                "[sync-canon]   applied to %d entries (albumartist + artist overwritten)\n",
                int(idxs.size()));
        fprintf(stderr,
                "[sync] canonicalized album '%s' (folder=%s) → '%s' (%d tracks, %d distinct sources)\n",
                qPrintable(originalAlbum), qPrintable(bucketFolder[it.key()]),
                qPrintable(canonical), int(idxs.size()), int(counts.size()));
    }

    return entries;
}

QString SyncEngine::videoDisplayTitle(const QString &title, const QString &series,
                                      int season, int episode) {
    return VideoIdentity::displayTitle(title, series, season, episode);
}

QString SyncEngine::videoWireName(const QString &displayName, const QString &series,
                                  int season, int episode, int profile) {
    QString raw;
    if (!series.isEmpty() && episode > 0) {
        raw = QStringLiteral("%1 - S%2E%3")
                  .arg(series)
                  .arg(season, 2, 10, QLatin1Char('0'))
                  .arg(episode, 2, 10, QLatin1Char('0'));
        // Skip a redundant title: untitled episodes carry "SxxEyy" as
        // their display fallback — "Series - S04E12 - S04E12" is noise.
        static const QRegularExpression epLabel(
            QStringLiteral("^S\\d{1,2}E\\d{1,3}$"),
            QRegularExpression::CaseInsensitiveOption);
        if (!displayName.isEmpty() && !epLabel.match(displayName).hasMatch())
            raw += QStringLiteral(" - ") + displayName;
    } else {
        raw = displayName;
    }
    // FAT32-illegal characters (mac sanitize rules)
    raw.replace(QLatin1Char(':'), QStringLiteral(" -"));
    raw.remove(QLatin1Char('?'));
    raw.remove(QLatin1Char('"'));
    raw.remove(QLatin1Char('*'));
    raw.remove(QLatin1Char('/'));
    raw.remove(QLatin1Char('\\'));
    raw.remove(QRegularExpression(QStringLiteral("[<>|\\x{0000}-\\x{001f}]")));
    raw = raw.trimmed();
    while (raw.endsWith(QLatin1Char('.'))) raw.chop(1);
    if (raw.isEmpty()) raw = QStringLiteral("video");
    // The Zune needs the extension to identify playable content.
    return raw + (profile == 0 ? QStringLiteral(".wmv") : QStringLiteral(".mp4"));
}

int SyncEngine::videoMetagenre(const QString &category, const QString &series,
                               int episode) {
    if (episode > 0 && !series.isEmpty())
        return 0x26;   // TV episode (vendor-prop path in doSendVideo)
    if (category == QLatin1String("tv"))
        return 0x26;
    if (category == QLatin1String("music_video"))
        return 0x23;
    if (category == QLatin1String("other"))
        return 0x21;
    if (category == QLatin1String("movie"))
        return 0x25;
    return series.isEmpty() ? 0x25 : 0x26;
}

qint64 SyncEngine::estimateVideoBytes(int profile, int durationMs,
                                      qint64 sourceBytes) {
    if (durationMs <= 0)
        return sourceBytes > 0 ? sourceBytes : qint64(300) * 1000 * 1000;
    // video+audio kbps per profile (libav_transcode.h)
    int kbps;
    switch (profile) {
    case 0:  kbps = 800 + 128;  break;   // Zune 30: WMV2 + WMA
    case 2:  kbps = 2500 + 192; break;   // HD 480x272
    case 3:  kbps = 8000 + 192; break;   // HD 720p
    default: kbps = 768 + 128;  break;   // classic H.264
    }
    const double secs = durationMs / 1000.0;
    return qint64(secs * kbps * 1000.0 / 8.0 * 1.05);   // +5% mux overhead
}

qint64 SyncEngine::estimateTrackBytes(int durationMs, qint64 sourceBytes) {
    if (durationMs <= 0)
        return sourceBytes > 0 ? sourceBytes : qint64(10) * 1000 * 1000;
    // 320kbps CBR MP3 + ~1MB tag/art headroom. MPEG passthrough keeps
    // the source size, but 320kbps is the ceiling either way.
    return qint64(durationMs / 1000.0 * 40000.0) + 1000 * 1000;
}

bool SyncEngine::sniffIsMpeg(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray h = f.read(16);
    if (h.size() < 4)
        return false;
    // ID3v2 container — MPEG family, the retagger handles it natively.
    if (h.startsWith("ID3"))
        return true;
    // Bare MPEG frame sync: 11 set bits.
    const uchar b0 = uchar(h[0]), b1 = uchar(h[1]);
    if (b0 == 0xFF && (b1 & 0xE0) == 0xE0)
        return true;
    // ASF GUID (WMA/WMV — 30 26 B2 75 …) and everything else: transcode.
    return false;
}

// ════════════════════════════════════════════════════════════════════
//  Engine
// ════════════════════════════════════════════════════════════════════

SyncEngine::SyncEngine(QObject *parent) : QObject(parent) {
    m_spacing.setSingleShot(true);
    m_spacing.setInterval(200);   // Zune firmware breathing room (mac)
    connect(&m_spacing, &QTimer::timeout, this, [this] {
        if (m_inVideoPhase)
            pumpVideoPipeline();
        else
            advance();
    });

    // A1: the queue survives restarts — debounced JSON snapshot on any
    // queue mutation, restored (and re-verified) at launch.
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(800);
    connect(&m_persistTimer, &QTimer::timeout, this,
            [this] { persistQueue(); });
    restoreQueue();
    {
        QFile f(queueStorePath() + QStringLiteral(".lastsync"));
        if (f.exists() && f.open(QIODevice::ReadOnly))
            m_lastSync = QJsonDocument::fromJson(f.readAll())
                             .object().toVariantMap();
    }
    connect(&m_queue, &SyncQueueModel::countChanged, this, [this] {
        schedulePersist();
        // B3: the queue belongs to the device it was built against;
        // an emptied queue belongs to nobody.
        if (m_queue.count() == 0) {
            if (!m_queueDevice.isEmpty()) {
                m_queueDevice.clear();
                emit queueDeviceChanged();
            }
        } else {
            stampQueueDevice();
        }
    });
    connect(&m_queue, &SyncQueueModel::estimateChanged, this,
            [this] { schedulePersist(); });
    connect(&m_queue, &QAbstractItemModel::dataChanged, this,
            [this] { schedulePersist(); });

    // B2: a marker file left behind means the app died mid-send — the
    // device may hold a truncated object (firmware-crash hazard).
    QFile inf(inflightPath());
    if (inf.exists() && inf.open(QIODevice::ReadOnly)) {
        const QByteArray marker = inf.readAll();
        const QJsonObject object = QJsonDocument::fromJson(marker).object();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("video")) {
            m_interruptedVideoFile = object.value(QStringLiteral("filename")).toString();
            m_interruptedSend = object.value(QStringLiteral("title")).toString();
            if (m_interruptedSend.isEmpty()) m_interruptedSend = m_interruptedVideoFile;
        } else if (object.value(QStringLiteral("type")).toString() == QLatin1String("music")
                   || object.value(QStringLiteral("type")).toString() == QLatin1String("photo")) {
            m_recoveryRecord = object.toVariantMap();
            m_interruptedSend = m_recoveryRecord.value("identity").toMap().value("title").toString();
            if (m_interruptedSend.isEmpty()) m_interruptedSend = QStringLiteral("interrupted media transfer");
        } else {
            m_interruptedSend = QString::fromUtf8(marker).trimmed();
            // Previous builds stored video filenames as plain text.
            if (VideoIdentity::fileStem(m_interruptedSend) != m_interruptedSend)
                m_interruptedVideoFile = m_interruptedSend;
        }
        inf.close();
        if (!m_interruptedSend.isEmpty())
            fprintf(stderr, "[sync] WARNING: previous run died mid-send: "
                    "'%s' may be truncated on the device\n",
                    qPrintable(m_interruptedSend));
    }
}

// D1: autopsy codes for humans. The raw name still lands in the log;
// the queue row gets a sentence a stranger can act on.
static QString humanAutopsy(const QString &a) {
    if (a.contains(QLatin1String("StoreFull")))
        return QStringLiteral("the zune is full");
    if (a.contains(QLatin1String("OperationNotSupported")))
        return QStringLiteral("the zune refused this operation — don't retry");
    if (a.contains(QLatin1String("InvalidObjectFormatCode")))
        return QStringLiteral("the zune doesn't accept this file format");
    if (a.contains(QLatin1String("IncompleteTransfer")))
        return QStringLiteral("the transfer broke off — check the cable and retry");
    if (a.contains(QLatin1String("DeviceBusy")))
        return QStringLiteral("the zune is busy — wait a moment and retry");
    if (a.contains(QLatin1String("AccessDenied")))
        return QStringLiteral("the zune denied access to this item");
    if (a.contains(QLatin1String("NoResponse"))
        || a.contains(QLatin1String("Transport")))
        return QStringLiteral("the USB connection faltered — replug and retry");
    if (a.contains(QLatin1String("SessionNotOpen")))
        return QStringLiteral("the connection dropped — reconnect the zune");
    return a.isEmpty() ? QStringLiteral("failed — see the log") : a;
}

void SyncEngine::attachDevice(DeviceService *device, DeviceWorker *worker) {
    m_device = device;
    m_worker = worker;
    // B3: mismatch is relative to the LIVE device — recompute on every
    // device state change.
    connect(device, &DeviceService::stateChanged, this,
            [this] { emit queueDeviceChanged(); });
    connect(device, &DeviceService::interruptedVideoPurged, this,
            [this](const QString &filename, bool ok) {
        if (ok && filename == m_interruptedVideoFile) dismissInterruptedSend();
    });
    connect(device, &DeviceService::interruptedObjectPurged, this,
            &SyncEngine::onInterruptedObjectPurged);
}

void SyncEngine::onInterruptedObjectPurged(const QString &token, bool ok) {
    if (token.isEmpty() || token != m_pendingRecoveryToken
        || token != m_recoveryRecord.value("token").toString()) return;
    m_pendingRecoveryToken.clear();
    if (ok) dismissInterruptedSend();
    else {
        m_interruptedRecoveryStatus = QStringLiteral("The exact object could not be verified or deleted. Recovery is retained; review the device library.");
        emit interruptedSendChanged();
    }
}

QString SyncEngine::currentDeviceIdent() const {
    if (!m_device || !m_device->connected())
        return QString();
    const QString n = m_device->name();
    return (n.isEmpty() ? m_device->model() : n)
        + QLatin1Char('|') + m_device->model();
}

void SyncEngine::stampQueueDevice() {
    if (!m_queueDevice.isEmpty())
        return;
    const QString ident = currentDeviceIdent();
    if (ident.isEmpty())
        return;
    m_queueDevice = ident;
    emit queueDeviceChanged();
    schedulePersist();
}

bool SyncEngine::queueDeviceMismatch() const {
    if (m_queueDevice.isEmpty() || !m_device || !m_device->connected())
        return false;
    return m_queueDevice != currentDeviceIdent();
}

// ── A1 queue persistence ──

QString SyncEngine::queueStorePath() const {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/syncqueue.json");
}

void SyncEngine::schedulePersist() { m_persistTimer.start(); }

void SyncEngine::persistQueue() {
    QJsonArray entries;
    for (const SyncQueueEntry &e : m_queue.entries()) {
        // Persist the re-arm set only — verified/skipped rows are done.
        if (e.status != QLatin1String("pending")
            && e.status != QLatin1String("failed")
            && e.status != QLatin1String("sent"))
            continue;
        QJsonObject o;
        o["entryId"] = e.entryId;
        o["status"] = e.status;
        o["statusNote"] = e.statusNote;
        o["type"] = e.type;
        o["filepath"] = e.filepath;
        o["title"] = e.title;
        o["artist"] = e.artist;
        o["albumartist"] = e.albumartist;
        o["album"] = e.album;
        o["genre"] = e.genre;
        o["trackNumber"] = e.trackNumber;
        o["discNumber"] = e.discNumber;
        o["year"] = e.year;
        o["durationMs"] = e.durationMs;
        o["libraryId"] = double(e.libraryId);
        o["videoSeries"] = e.videoSeries;
        o["videoSeason"] = e.videoSeason;
        o["videoEpisode"] = e.videoEpisode;
        o["videoDescription"] = e.videoDescription;
        o["videoPosterPath"] = e.videoPosterPath;
        o["videoCategory"] = e.videoCategory;
        o["estimatedBytes"] = double(e.estimatedBytes);
        entries.append(o);
    }
    QJsonArray playlists;
    for (const PendingPlaylist &p : m_pendingPlaylists) {
        QJsonObject o;
        o["entryId"] = p.entryId;
        o["name"] = p.name;
        o["members"] = QJsonArray::fromVariantList(p.members);
        playlists.append(o);
    }
    QJsonObject root;
    root["entries"] = entries;
    root["playlists"] = playlists;
    root["device"] = m_queueDevice;
    root["identityVersion"] = 2;
    QSaveFile f(queueStorePath());
    if (f.open(QIODevice::WriteOnly)) {
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (f.write(bytes) == bytes.size()) f.commit();
    }
}

void SyncEngine::restoreQueue() {
    QFile f(queueStorePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonArray entries = root["entries"].toArray();
    if (entries.isEmpty())
        return;
    m_queueDevice = root["device"].toString();

    QSet<QString> restoredIds;
    int restored = 0, dropped = 0;
    m_queue.beginBatch();
    for (const QJsonValue &v : entries) {
        const QJsonObject o = v.toObject();
        SyncQueueEntry e;
        e.entryId = o["entryId"].toString();
        e.type = o["type"].toString();
        e.filepath = o["filepath"].toString();
        // Re-verify: the file must still exist (playlist rows carry none).
        if (o["status"].toString() != QLatin1String("sent")
            && e.type != QLatin1String("playlist")
            && !QFile::exists(e.filepath)) {
            dropped++;
            continue;
        }
        e.title = o["title"].toString();
        e.artist = o["artist"].toString();
        e.albumartist = o["albumartist"].toString();
        e.album = o["album"].toString();
        e.genre = o["genre"].toString();
        e.trackNumber = o["trackNumber"].toInt();
        e.discNumber = o["discNumber"].toInt();
        e.year = o["year"].toInt();
        e.durationMs = o["durationMs"].toInt();
        e.libraryId = qint64(o["libraryId"].toDouble(-1));
        e.videoSeries = o["videoSeries"].toString();
        e.videoSeason = o["videoSeason"].toInt();
        e.videoEpisode = o["videoEpisode"].toInt();
        e.videoDescription = o["videoDescription"].toString();
        e.videoPosterPath = o["videoPosterPath"].toString();
        e.videoCategory = o["videoCategory"].toString();
        e.estimatedBytes = qint64(o["estimatedBytes"].toDouble(0));
        e.status = o["status"].toString() == QLatin1String("sent")
            ? QStringLiteral("sent") : QStringLiteral("pending");
        if (e.status == QLatin1String("sent")) e.statusNote = o["statusNote"].toString();
        m_queue.restoreEntry(e);
        restoredIds.insert(e.entryId);
        restored++;
    }
    m_queue.endBatch();

    for (const QJsonValue &v : root["playlists"].toArray()) {
        const QJsonObject o = v.toObject();
        if (!restoredIds.contains(o["entryId"].toString()))
            continue;
        PendingPlaylist p;
        p.entryId = o["entryId"].toString();
        p.name = o["name"].toString();
        if (o.contains("members")) {
            p.members = o["members"].toArray().toVariantList();
        } else {
            // Legacy queues lack disc/track. Preserve unknown identity rather
            // than guessing disc 1 or using an artist-free fallback.
            for (const QJsonValue &key : o["fullKeys"].toArray()) {
                const QStringList fields = key.toString().split(QLatin1Char('\t'));
                QVariantMap member;
                if (fields.size() == 3) {
                    member = {{"artist", fields[0]}, {"album", fields[1]},
                              {"title", fields[2]}};
                }
                p.members.append(member); // Invalid rows remain unresolved.
            }
        }
        m_pendingPlaylists.append(p);
    }
    if (restored || dropped)
        fprintf(stderr, "[sync] queue restored: %d entries (%d dropped — "
                "files no longer exist)\n", restored, dropped);
}

// ── B2 in-flight send marker ──

QString SyncEngine::inflightPath() const {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/inflight-send");
}

bool SyncEngine::markInflight(const QString &type, const QVariantMap &identity) {
    if (!m_interruptedSend.isEmpty() || !m_device) return false;
    QVariantList baseline;
    for (const auto &row : m_device->deviceTrackRows()) {
        const auto id = row.toMap().value("itemId");
        if (RecoveryIdentity::objectId(id)) baseline.append(id);
    }
    for (const auto &row : m_device->photosList()) {
        const auto id = row.toMap().value("itemId");
        if (RecoveryIdentity::objectId(id)) baseline.append(id);
    }
    for (const auto &row : m_device->videosList()) {
        const auto id = row.toMap().value("itemId");
        if (RecoveryIdentity::objectId(id)) baseline.append(id);
    }
    m_activeRecoveryRecord = {{"version", 1}, {"type", type}, {"identity", identity},
        {"serial", m_device->deviceSerial()}, {"baselineIds", baseline},
        {"token", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"itemId", 0}};
    QSaveFile file(inflightPath());
    const auto bytes = QJsonDocument::fromVariant(m_activeRecoveryRecord).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

void SyncEngine::clearInflight() {
    if (!m_interruptedSend.isEmpty()) return;
    QFile::remove(inflightPath());
    m_activeRecoveryRecord.clear();
}

void SyncEngine::retainFailedInflight(quint32 itemId) {
    if (m_activeRecoveryRecord.isEmpty()) return;
    m_activeRecoveryRecord.insert("itemId", itemId);
    m_recoveryRecord = m_activeRecoveryRecord;
    m_interruptedSend = m_recoveryRecord.value("identity").toMap().value("title").toString();
    if (m_interruptedSend.isEmpty()) m_interruptedSend = QStringLiteral("interrupted media transfer");
    QSaveFile file(inflightPath());
    const auto bytes = QJsonDocument::fromVariant(m_recoveryRecord).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        m_recoveryRecord.insert("itemId", 0); // Original durable marker still safely refuses automatic deletion.
    m_interruptedRecoveryStatus = QStringLiteral("Transfer failed. Review recovery before sending more media.");
    emit interruptedSendChanged();
}

void SyncEngine::markInflightVideo(const QString &filename, const QString &title) {
    QSaveFile file(inflightPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"type", "video"},
                                            {"filename", filename}, {"title", title}})
                       .toJson(QJsonDocument::Compact));
        file.commit();
    }
}

int SyncEngine::purgeInterruptedSend() {
    if (m_interruptedSend.isEmpty())
        return 0;
    if (!m_device || !m_device->connected()) {
        m_interruptedRecoveryStatus = QStringLiteral("Connect the original Zune to review recovery. Nothing was deleted.");
        emit interruptedSendChanged();
        return 0;
    }
    if (!m_interruptedVideoFile.isEmpty()) {
        if (m_isSyncing) return 0;
        // Completion clears the marker only after one exact object was
        // actually deleted. A failed/ambiguous search stays recoverable.
        return m_device->purgeInterruptedVideo(m_interruptedVideoFile) ? 1 : 0;
    }
    if (m_isSyncing || !m_pendingRecoveryToken.isEmpty()) return 0;
    m_interruptedRecoveryStatus = RecoveryIdentity::refusal(m_recoveryRecord, m_device->deviceSerial());
    if (m_interruptedRecoveryStatus.isEmpty()) {
        if (m_device->purgeInterruptedObject(m_recoveryRecord)) {
            m_pendingRecoveryToken = m_recoveryRecord.value("token").toString();
            m_interruptedRecoveryStatus = QStringLiteral("Verifying and deleting the exact interrupted object…");
            emit interruptedSendChanged();
            return 1;
        }
        m_interruptedRecoveryStatus = QStringLiteral("The Zune is busy. Recovery is retained; try again when it is idle.");
    }
    emit interruptedSendChanged();
    return 0;
}

void SyncEngine::dismissInterruptedSend() {
    if (m_isSyncing || !m_pendingRecoveryToken.isEmpty()) return;
    m_interruptedSend.clear();
    m_interruptedVideoFile.clear();
    m_recoveryRecord.clear();
    m_interruptedRecoveryStatus.clear();
    clearInflight();
    emit interruptedSendChanged();
}

double SyncEngine::overallProgress() const {
    if (m_totalCount == 0)
        return 0;
    const double denom = m_totalCount + 1;   // forge = one pseudo-entry
    if (m_syncCompleted)
        return 1.0;
    if (m_forging)
        return (m_completedCount + m_forgeFraction) / denom;
    // Video phase runs after the forge — its unit is already banked.
    const double forgeUnits = m_inVideoPhase ? 1 : 0;
    return (m_completedCount + forgeUnits + m_currentFraction) / denom;
}

namespace {
MusicIdentity::Index musicIndex(const QVariantList &rows) {
    MusicIdentity::Index result;
    qint64 id = 0;
    for (const auto &value : rows) result.add(MusicIdentity::fromMap(value.toMap()), ++id);
    return result;
}
QVariantMap disconnectedQueueResult(qsizetype count) {
    return {{"added", 0}, {"rejected", count}, {"duplicates", 0},
            {"onDevice", 0}, {"playlistQueued", false},
            {"error", QStringLiteral("Connect a Zune to queue transfers.")}};
}
}

// Transfer queues belong to a connected Zune. Stored queues survive
// disconnects, but no entry may be added until a device reconnects.
// Capacity is the remaining device space minus the queue and safety margin.
qint64 SyncEngine::capacityBudget() const {
    if (!m_device || !m_device->connected())
        return 0;
    const qint64 freeBytes = qint64(m_device->freeGB() * 1e9);
    const qint64 margin = qint64(256) * 1000 * 1000;
    return freeBytes - margin - qint64(m_queue.pendingEstimatedBytes());
}

QVariantMap SyncEngine::addTracks(const QVariantList &tracks) {
    if (!m_device || !m_device->connected())
        return disconnectedQueueResult(tracks.size());
    qint64 budget = capacityBudget();
    QVariantList rejectedItems;
    int added = 0, duplicates = 0, onDevice = 0, ambiguous = 0;
    // Already on the Zune never enters the queue (same policy as
    // photos) — callers toast "already on the zune" instead of
    // queueing rows that would only be skipped at sync.
    const auto deviceKeys = deviceDedupIndex();
    const auto sourceIdentities = musicIndex(tracks);
    m_queue.beginBatch();
    for (const QVariant &v : tracks) {
        const QVariantMap t = v.toMap();
        const auto identity = MusicIdentity::fromMap(t);
        const auto match = deviceKeys.resolve(identity, identityPeers(t, sourceIdentities));
        if (match.matched()) {
            onDevice++;
            continue;
        }
        const qint64 est = estimateTrackBytes(
            t.value(QStringLiteral("durationMs")).toInt(),
            qint64(t.value(QStringLiteral("filesize")).toLongLong()));
        if (est > budget) {
            rejectedItems.append(QVariantMap{
                {"title", t.value(QStringLiteral("title")).toString()},
                {"estGB", est / 1e9}});
            continue;
        }
        const QString id = m_queue.addTrack(
            t.value(QStringLiteral("filepath")).toString(),
            t.value(QStringLiteral("title")).toString(),
            t.value(QStringLiteral("artist")).toString(),
            t.value(QStringLiteral("albumartist")).toString(),
            t.value(QStringLiteral("album")).toString(),
            t.value(QStringLiteral("genre")).toString(),
            identity.trackNumber,
            t.value(QStringLiteral("durationMs")).toInt(),
            qint64(t.value(QStringLiteral("libraryId"), -1).toLongLong()),
            est, identity.discNumber,
            t.value(QStringLiteral("year")).toInt());
        if (!id.isEmpty()) {
            budget -= est;
            added++;
            if (match.kind == MusicIdentity::MatchKind::Ambiguous) {
                ++ambiguous;
                m_queue.setStatus(id, QStringLiteral("failed"),
                    QStringLiteral("Device track identity is ambiguous; review matching tracks before syncing."));
            }
        } else {
            duplicates++;
        }
    }
    m_queue.endBatch();
    if (added > 0)
        emit queueItemsAdded(added);
    if (!rejectedItems.isEmpty())
        emitCapacityRejected(rejectedItems, added);
    return QVariantMap{{"added", added},
                       {"rejected", rejectedItems.size()},
                       {"duplicates", duplicates},
                       {"onDevice", onDevice}, {"ambiguous", ambiguous}};
}

QVariantMap SyncEngine::addVideos(const QVariantList &videos) {
    if (!m_device || !m_device->connected())
        return disconnectedQueueResult(videos.size());
    const int profile = resolveVideoProfile();
    qint64 budget = capacityBudget();
    QVariantList rejectedItems;
    int added = 0, duplicates = 0;
    m_queue.beginBatch();
    for (const QVariant &v : videos) {
        const QVariantMap m = v.toMap();
        const qint64 est = estimateVideoBytes(
            profile, m.value(QStringLiteral("durationMs")).toInt(),
            qint64(m.value(QStringLiteral("filesize")).toLongLong()));
        if (est > budget) {
            QString title = m.value(QStringLiteral("title")).toString();
            const QString series = m.value(QStringLiteral("series")).toString();
            const int episode = m.value(QStringLiteral("episode")).toInt();
            if (!series.isEmpty() && episode > 0)
                title = QStringLiteral("%1 S%2E%3 — %4")
                    .arg(series)
                    .arg(m.value(QStringLiteral("season")).toInt(),
                         2, 10, QLatin1Char('0'))
                    .arg(episode, 2, 10, QLatin1Char('0'))
                    .arg(title);
            rejectedItems.append(QVariantMap{{"title", title},
                                             {"estGB", est / 1e9}});
            continue;
        }
        const QString id = m_queue.addVideo(
            m.value(QStringLiteral("filepath")).toString(),
            m.value(QStringLiteral("title")).toString(),
            m.value(QStringLiteral("description")).toString(),
            m.value(QStringLiteral("posterPath")).toString(),
            m.value(QStringLiteral("series")).toString(),
            m.value(QStringLiteral("season")).toInt(),
            m.value(QStringLiteral("episode")).toInt(),
            m.value(QStringLiteral("category")).toString(),
            qint64(m.value(QStringLiteral("libraryId"), -1).toLongLong()),
            est);
        if (!id.isEmpty()) {
            budget -= est;
            added++;
        } else {
            duplicates++;
        }
    }
    m_queue.endBatch();
    if (added > 0)
        emit queueItemsAdded(added);
    if (!rejectedItems.isEmpty())
        emitCapacityRejected(rejectedItems, added);
    return QVariantMap{{"added", added},
                       {"rejected", rejectedItems.size()},
                       {"duplicates", duplicates}};
}

QVariantMap SyncEngine::addPhotos(const QVariantList &photos) {
    if (!m_device || !m_device->connected())
        return disconnectedQueueResult(photos.size());
    qint64 budget = capacityBudget();
    QVariantList rejectedItems;
    int added = 0, duplicates = 0, onDevice = 0;
    // Already on the Zune (stem match — device stores the armed .jpg)
    // never even enters the queue.
    QSet<QString> deviceStems;
    if (m_device && m_device->connected())
        for (const QVariant &v : m_device->photosList())
            deviceStems.insert(DeviceService::photoStem(
                v.toMap().value(QStringLiteral("name")).toString()));
    m_queue.beginBatch();
    for (const QVariant &v : photos) {
        const QVariantMap m = v.toMap();
        if (deviceStems.contains(DeviceService::photoStem(
                m.value(QStringLiteral("filename")).toString()))) {
            onDevice++;
            continue;
        }
        // Armed photos are ≤480px baseline JPEGs — call it 400KB each,
        // capped by the source size for tiny files.
        const qint64 est = qMin<qint64>(
            qMax<qint64>(m.value(QStringLiteral("filesize")).toLongLong(), 1),
            400 * 1024);
        if (est > budget) {
            rejectedItems.append(QVariantMap{
                {"title", m.value(QStringLiteral("filename")).toString()},
                {"estGB", est / 1e9}});
            continue;
        }
        const QString id = m_queue.addPhoto(
            m.value(QStringLiteral("filepath")).toString(),
            m.value(QStringLiteral("filename")).toString(),
            m.value(QStringLiteral("album")).toString(),
            qint64(m.value(QStringLiteral("libraryId"), -1).toLongLong()),
            est);
        if (!id.isEmpty()) {
            budget -= est;
            added++;
        } else {
            duplicates++;
        }
    }
    m_queue.endBatch();
    if (added > 0)
        emit queueItemsAdded(added);
    if (!rejectedItems.isEmpty())
        emitCapacityRejected(rejectedItems, added);
    return QVariantMap{{"added", added},
                       {"rejected", rejectedItems.size()},
                       {"duplicates", duplicates},
                       {"onDevice", onDevice}};
}

QVariantMap SyncEngine::addPlaylist(const QString &name,
                                    const QVariantList &tracks) {
    if (!m_device || !m_device->connected())
        return disconnectedQueueResult(tracks.size());
    // Ride-along: the normal track path queues whatever isn't on the
    // device/in the queue yet (its dedup handles both).
    QVariantMap res = addTracks(tracks);

    PendingPlaylist pl;
    pl.name = name.trimmed();
    pl.members = tracks;
    pl.entryId = m_queue.addPlaylistEntry(pl.name, tracks.size());
    const bool queued = !pl.entryId.isEmpty();
    if (queued) {
        // Replace any stale membership for the same name (entry dedup
        // means the queue row is the older one — refresh its members).
        for (int i = m_pendingPlaylists.size() - 1; i >= 0; i--)
            if (m_pendingPlaylists[i].name == pl.name)
                m_pendingPlaylists.removeAt(i);
        m_pendingPlaylists.append(pl);
        emit queueItemsAdded(1);
    } else {
        // Row already queued for this name — update its membership.
        for (PendingPlaylist &old : m_pendingPlaylists)
            if (old.name == pl.name) {
                old.members = pl.members;
            }
    }
    res.insert(QStringLiteral("playlistQueued"), queued);
    return res;
}

void SyncEngine::emitCapacityRejected(const QVariantList &rejectedItems,
                                      int addedCount) {
    const double queuedGB = m_queue.pendingEstimatedBytes() / 1e9;
    const double freeGB = m_device ? m_device->freeGB() : 0;
    fprintf(stderr,
            "[sync] capacity gate: %d entries refused, %d added "
            "(queued ~%.1f GB, %.1f GB free)\n",
            int(rejectedItems.size()), addedCount, queuedGB, freeGB);
    emit capacityRejected(rejectedItems, addedCount, queuedGB, freeGB);
}

void SyncEngine::clearQueue() {
    if (m_isSyncing)
        return;
    m_queue.clearQueue();
}

void SyncEngine::removeEntry(const QString &entryId) {
    if (m_isSyncing)
        return;
    m_queue.removeEntry(entryId);
}

MusicIdentity::Index SyncEngine::deviceDedupIndex() const {
    MusicIdentity::Index out;
    if (!m_device)
        return out;
    for (const auto &value : m_device->deviceTrackRows()) {
        const auto row = value.toMap();
        out.add(MusicIdentity::fromMap(row), row.value(QStringLiteral("itemId")).toLongLong());
    }
    return out;
}

QVector<MusicIdentity::Track> SyncEngine::identityPeers(const QVariantMap &track,
                                                       const MusicIdentity::Index &batch) const {
    QVector<MusicIdentity::Track> peers;
    if (m_library)
        for (const auto &value : m_library->musicIdentityPeers(track))
            peers.append(MusicIdentity::fromMap(value.toMap()));
    peers += batch.identitiesFor(MusicIdentity::fromMap(track));
    return peers;
}

void SyncEngine::startSync() {
    if (!m_interruptedSend.isEmpty()) {
        m_lastError = QStringLiteral("Review or dismiss the interrupted transfer before starting another sync.");
        emit progressChanged();
        return;
    }
    if (!m_isSyncing) m_videoWarningsThisRun.clear();
    if (m_isSyncing)
        return;
    if (!m_device || !m_device->connected() || !m_worker) {
        m_lastError = QStringLiteral("no device connected");
        emit syncStateChanged();
        return;
    }

    // Snapshot pending track entries. Failed/cancelled entries are
    // re-armed — pressing sync again retries them (the mac left them
    // stranded until the queue was cleared).
    const auto eligible = [](const SyncQueueEntry &e) {
        return e.status == QLatin1String("pending")
            || e.status == QLatin1String("failed");
    };
    QVector<QVariantMap> snapshot;
    for (const SyncQueueEntry &e : m_queue.entries()) {
        if (e.type != QLatin1String("track") || !eligible(e))
            continue;
        if (e.status == QLatin1String("failed"))
            m_queue.setStatus(e.entryId, QStringLiteral("pending"));
        QVariantMap m;
        m[QStringLiteral("entryId")] = e.entryId;
        m[QStringLiteral("type")] = e.type;
        m[QStringLiteral("filepath")] = e.filepath;
        m[QStringLiteral("title")] = e.title;
        m[QStringLiteral("artist")] = e.artist;
        m[QStringLiteral("origArtist")] = e.artist;   // pre-canonical, for
                                                      // the legacy art-key
                                                      // fallback (mac rule)
        m[QStringLiteral("albumartist")] = e.albumartist;
        m[QStringLiteral("album")] = e.album;
        m[QStringLiteral("genre")] = e.genre;
        m[QStringLiteral("trackNumber")] = e.trackNumber;
        m[QStringLiteral("discNumber")] = e.discNumber;
        m[QStringLiteral("year")] = e.year;
        m[QStringLiteral("durationMs")] = e.durationMs;
        m[QStringLiteral("libraryId")] = e.libraryId;
        m[QStringLiteral("sourceIdentity")] = MusicIdentity::toMap(MusicIdentity::fromMap(m));
        snapshot.append(m);
    }

    // Video entries — synced AFTER the album forge (mac phase order).
    // B3: re-dedup against the CURRENTLY connected device — the queue
    // may have been built against a different Zune (dedup at queue
    // time checked that one, not this one).
    const QSet<QString> liveVideoKeys =
        (m_device && m_device->connected())
            ? VideoBrowserModel::deviceVideoKeys(m_device->videosList())
            : QSet<QString>();
    m_videoEntries.clear();
    for (const SyncQueueEntry &e : m_queue.entries()) {
        if (e.type != QLatin1String("video") || !eligible(e))
            continue;
        if (!liveVideoKeys.isEmpty()) {
            bool onDev = false;
            if (!e.videoSeries.isEmpty() && e.videoEpisode > 0)
                onDev = liveVideoKeys.contains(VideoBrowserModel::episodeKey(
                    e.videoSeries, e.videoSeason, e.videoEpisode));
            if (!onDev && e.videoSeries.isEmpty() && e.videoCategory != QLatin1String("tv"))
                onDev = liveVideoKeys.contains(
                    VideoNaming::normalizeSeriesName(VideoIdentity::fileStem(e.title)).toLower());
            if (onDev) {
                m_queue.setStatus(e.entryId, QStringLiteral("skipped"),
                                  QStringLiteral("already on device"));
                continue;
            }
        }
        if (e.status == QLatin1String("failed"))
            m_queue.setStatus(e.entryId, QStringLiteral("pending"));
        QVariantMap m;
        m[QStringLiteral("entryId")] = e.entryId;
        m[QStringLiteral("filepath")] = e.filepath;
        m[QStringLiteral("title")] = e.title;
        m[QStringLiteral("series")] = e.videoSeries;
        m[QStringLiteral("season")] = e.videoSeason;
        m[QStringLiteral("episode")] = e.videoEpisode;
        m[QStringLiteral("description")] = e.videoDescription;
        m[QStringLiteral("posterPath")] = e.videoPosterPath;
        m[QStringLiteral("category")] = e.videoCategory;
        m_videoEntries.append(m);
    }

    // Photo entries — synced between the forge and video phases (mac
    // phase 3). Dedup against device photo NAMES at snapshot time.
    m_photoEntries.clear();
    {
        QSet<QString> deviceStems;
        if (m_device)
            for (const QVariant &v : m_device->photosList())
                deviceStems.insert(DeviceService::photoStem(
                    v.toMap().value(QStringLiteral("name")).toString()));
        for (const SyncQueueEntry &e : m_queue.entries()) {
            if (e.type != QLatin1String("photo") || !eligible(e))
                continue;
            if (e.status == QLatin1String("failed"))
                m_queue.setStatus(e.entryId, QStringLiteral("pending"));
            // STEM match — the device stores the armed .jpg, so
            // "foo.png" must dedup against "foo.jpg".
            if (deviceStems.contains(DeviceService::photoStem(e.title))) {
                m_queue.setStatus(e.entryId, QStringLiteral("skipped"),
                                  QStringLiteral("already on device"));
                continue;
            }
            QVariantMap m;
            m[QStringLiteral("entryId")] = e.entryId;
            m[QStringLiteral("filepath")] = e.filepath;
            m[QStringLiteral("title")] = e.title;
            m[QStringLiteral("album")] = e.album;
            m_photoEntries.append(m);
        }
    }

    // Playlist-only syncs are legit: every member may already live on
    // the device — the forge itself is the whole job. (Without this a
    // queue of bare playlists made "sync now" silently do nothing.)
    bool anyPlaylists = false;
    for (const SyncQueueEntry &e : m_queue.entries())
        if (e.type == QLatin1String("playlist") && eligible(e)) {
            anyPlaylists = true;
            break;
        }

    if (snapshot.isEmpty() && m_videoEntries.isEmpty()
        && m_photoEntries.isEmpty() && !anyPlaylists)
        return;

    // iTunes-style canonicalization BEFORE anything leaves the queue.
    m_entries = canonicalizeAlbumArtists(snapshot);

    // Device dedup with the CANONICAL artist (mac order).
    const auto existing = deviceDedupIndex();
    m_sentTrackIds.clear();
    m_sentTrackIdentities.clear();
    QVariantList sourceBatch;
    for (const auto &entry : m_entries) sourceBatch.append(entry);
    const auto sourceIdentities = musicIndex(sourceBatch);
    for (QVariantMap &e : m_entries) {
        const auto match = existing.resolve(MusicIdentity::fromMap(e), identityPeers(e, sourceIdentities));
        if (match.matched()) {
            e[QStringLiteral("skip")] = true;
            m_sentTrackIds.insert(e.value(QStringLiteral("filepath")).toString(), quint32(match.itemId));
            m_sentTrackIdentities.insert(e.value(QStringLiteral("filepath")).toString(),
                                        MusicIdentity::fromMap(e.value("sourceIdentity").toMap()));
            m_queue.setStatus(e.value(QStringLiteral("entryId")).toString(),
                              QStringLiteral("skipped"),
                              QStringLiteral("already on device"));
            fprintf(stderr, "[sync] step=dedup-skip title=%s\n",
                    qPrintable(e.value(QStringLiteral("title")).toString()));
        } else if (match.kind == MusicIdentity::MatchKind::Ambiguous) {
            e[QStringLiteral("skip")] = true;
            m_queue.setStatus(e.value(QStringLiteral("entryId")).toString(),
                              QStringLiteral("failed"),
                              QStringLiteral("Device track identity is ambiguous; no transfer attempted."));
        }
    }

    m_isSyncing = true;
    m_syncCompleted = false;
    m_cancelled = false;
    m_syncStartMs = QDateTime::currentMSecsSinceEpoch();
    zuuned_transcode_clear_abort();   // stale cancel must not kill this run
    // GPU decode policy for this run (Settings → Device acceleration
    // pills → libav_transcode mode: 0 sw · 1 auto · 2 NVDEC · 3 VAAPI).
    // Legacy bool hwDecode=false still counts as "cpu".
    {
        QSettings st;
        const QString accel = st.value(
            QStringLiteral("transcodeAccel"),
            st.value(QStringLiteral("hwDecode"), true).toBool()
                ? QStringLiteral("auto") : QStringLiteral("cpu")).toString();
        int mode = 1;
        if (accel == QLatin1String("cpu"))        mode = 0;
        else if (accel == QLatin1String("nvenc")) mode = 2;
        else if (accel == QLatin1String("amd"))   mode = 3;
        zuuned_transcode_set_hw_decode(mode);
    }
    m_totalCount = m_entries.size() + m_videoEntries.size()
                   + m_photoEntries.size();
    m_photoPos = -1;
    m_inPhotoPhase = false;
    m_currentIndex = 0;
    m_completedCount = 0;
    m_currentFraction = 0;
    m_forging = false;
    m_forgeFraction = 0;
    m_inVideoPhase = false;
    m_preparedBuf.clear();
    m_activePrepares = 0;
    m_nextPrepareIdx = 0;
    m_nextSendIdx = 0;
    m_videoSending = false;
    m_currentSendPath.clear();
    m_videoProfile = resolveVideoProfile();
    m_buckets.clear();
    m_lastError.clear();
    m_entryPos = -1;
    emit syncStateChanged();
    emit progressChanged();

    advance();
}

void SyncEngine::advance() {
    if (m_cancelled) {
        finishSync(false);
        return;
    }

    // Next non-skipped entry.
    while (true) {
        m_entryPos++;
        if (m_entryPos >= m_entries.size()) {
            beginForge();
            return;
        }
        if (m_entries[m_entryPos].value(QStringLiteral("skip")).toBool()) {
            m_completedCount++;
            continue;
        }
        break;
    }
    prepareEntry(m_entryPos);
}

void SyncEngine::prepareEntry(int idx) {
    const QVariantMap e = m_entries[idx];
    m_currentEntryId = e.value(QStringLiteral("entryId")).toString();
    m_currentName = e.value(QStringLiteral("title")).toString();
    m_currentIndex = idx + 1;
    m_currentFraction = 0;
    m_currentTempPath.clear();
    m_currentTranscoded = false;
    emit progressChanged();

    const QString filepath = e.value(QStringLiteral("filepath")).toString();
    QString aartist = e.value(QStringLiteral("albumartist")).toString();
    if (aartist.isEmpty())
        aartist = e.value(QStringLiteral("artist")).toString();

    const QString entryId = m_currentEntryId;
    const int discNumber = e.value(QStringLiteral("discNumber")).toInt();
    QPointer<SyncEngine> self(this);

    // Sniff + retag/transcode off the UI thread (file IO can be network).
    auto discard = QtConcurrent::run([self, entryId, filepath, aartist, discNumber] {
        const bool mpeg = sniffIsMpeg(filepath);
        // W2: MPEG retag is near-instant ("sending"); anything else
        // transcodes ("transcoding" — the slow, freeze-looking half).
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, mpeg] {
                if (self) { self->m_syncPhase = mpeg
                    ? QStringLiteral("sending") : QStringLiteral("transcoding");
                    emit self->progressChanged(); }
            }, Qt::QueuedConnection);
        QString prepared;
        QString error;
        bool transcoded = false;

        if (mpeg) {
            // ID3v2.3 retag; TPE1 ← albumartist. nil → send original
            // with its own tags (mac behavior).
            char *p = zuuned_retag_mp3_with_disc(filepath.toUtf8().constData(),
                                       aartist.isEmpty()
                                           ? nullptr
                                           : aartist.toUtf8().constData(), discNumber);
            if (p) {
                prepared = QString::fromUtf8(p);
                free(p);
                fprintf(stderr, "[sync] retagged to ID3v2.3: %s\n",
                        qPrintable(filepath));
            } else {
                if (discNumber > 0) {
                    error = QStringLiteral("Could not prepare the requested disc tag; original file was not sent.");
                } else {
                    prepared = filepath;
                    fprintf(stderr, "[sync] retag returned nil — sending original tags: %s\n",
                            qPrintable(filepath));
                }
            }
        } else {
            transcoded = true;
            if (self)
                QMetaObject::invokeMethod(self.data(), [self, entryId] {
                    if (self)
                        self->m_queue.setStatus(entryId,
                                                QStringLiteral("transcoding"));
                }, Qt::QueuedConnection);

            struct Ctx {
                QPointer<SyncEngine> engine;
                QString entryId;
                float last = -1;
            } ctx{self, entryId, -1};
            auto cb = [](float fraction, void *userdata) {
                auto *c = static_cast<Ctx *>(userdata);
                if (fraction - c->last < 0.01f)
                    return;   // 1% steps — queued-signal hygiene
                c->last = fraction;
                if (c->engine) {
                    const QString id = c->entryId;
                    const double f = fraction;
                    QMetaObject::invokeMethod(c->engine.data(),
                        [eng = c->engine, id, f] {
                            if (eng)
                                eng->onTranscodeProgress(id, f);
                        }, Qt::QueuedConnection);
                }
            };
            char *p = zuuned_transcode_audio_with_disc(filepath.toUtf8().constData(),
                                             aartist.isEmpty()
                                                 ? nullptr
                                                 : aartist.toUtf8().constData(),
                                             discNumber, cb, &ctx);
            if (p) {
                prepared = QString::fromUtf8(p);
                free(p);
            } else {
                error = QStringLiteral("transcode failed");
            }
        }

        if (self)
            QMetaObject::invokeMethod(self.data(),
                [self, entryId, prepared, transcoded, error] {
                    if (self)
                        self->onPrepareFinished(entryId, prepared,
                                                transcoded, error);
                }, Qt::QueuedConnection);
    });
    Q_UNUSED(discard);
}

void SyncEngine::onTranscodeProgress(const QString &entryId, double frac) {
    if (!m_isSyncing)
        return;
    if (m_inVideoPhase) {
        // Pipelined: several transcodes run at once — each updates ITS
        // row only (transcode = first half; send completion = 1.0).
        m_queue.setProgress(entryId, frac * 0.5);
        return;
    }
    if (entryId != m_currentEntryId)
        return;
    m_currentFraction = frac * 0.5;   // transcode occupies the first half
    m_queue.setProgress(entryId, m_currentFraction);
    emit progressChanged();
}

void SyncEngine::onPrepareFinished(const QString &entryId,
                                   const QString &preparedPath,
                                   bool transcoded, const QString &error) {
    if (entryId != m_currentEntryId || !m_isSyncing)
        return;   // stale (cancelled sync restarted, etc.)

    const QVariantMap &e = m_entries[m_entryPos];
    const QString original = e.value(QStringLiteral("filepath")).toString();

    if (m_cancelled) {
        if (!preparedPath.isEmpty() && preparedPath != original)
            QFile::remove(preparedPath);
        markEntryTerminal(entryId, QStringLiteral("failed"),
                          QStringLiteral("cancelled"));
        finishSync(false);
        return;
    }

    if (!error.isEmpty()) {
        markEntryTerminal(entryId, QStringLiteral("failed"), error);
        m_spacing.start();
        return;
    }

    m_currentTranscoded = transcoded;
    m_currentTempPath = (preparedPath != original) ? preparedPath : QString();

    QString aartist = e.value(QStringLiteral("albumartist")).toString();
    if (aartist.isEmpty())
        aartist = e.value(QStringLiteral("artist")).toString();

    // [sync-canon] pre-send snapshot — confirms the canonical artist
    // reaches the MTP layer (mac trace).
    fprintf(stderr, "[sync-canon] sendTrack artist='%s' title='%s' album='%s' file='%s'\n",
            qPrintable(aartist), qPrintable(m_currentName),
            qPrintable(e.value(QStringLiteral("album")).toString()),
            qPrintable(QFileInfo(preparedPath).fileName()));

    m_queue.setStatus(entryId, QStringLiteral("syncing"));
    // Device-side sync display (0x922A) — name + batch position
    emit workerSyncNotify(m_currentName, m_entryPos, m_totalCount);
    m_syncPhase = QStringLiteral("sending");   // W2
    auto recoveryIdentity = e;
    recoveryIdentity.insert(QStringLiteral("artist"), aartist);
    if (!markInflight(QStringLiteral("music"), recoveryIdentity)) {
        m_lastError = QStringLiteral("Could not save transfer recovery information. Nothing was sent.");
        markEntryTerminal(entryId, QStringLiteral("failed"), m_lastError);
        finishSync(false);
        return;
    }
    emit workerSendTrack(entryId, preparedPath, m_currentName, aartist,
                         e.value(QStringLiteral("album")).toString(),
                         e.value(QStringLiteral("genre")).toString(),
                         e.value(QStringLiteral("trackNumber")).toInt(),
                         e.value(QStringLiteral("durationMs")).toInt());
}

void SyncEngine::onSendProgress(const QString &entryId, double frac) {
    if (entryId != m_currentEntryId || !m_isSyncing)
        return;
    m_currentFraction = m_currentTranscoded ? 0.5 + frac * 0.5 : frac;
    m_queue.setProgress(entryId, m_currentFraction);
    emit progressChanged();
}

void SyncEngine::cleanupCurrentTemp(const QString &) {
    if (!m_currentTempPath.isEmpty()) {
        QFile::remove(m_currentTempPath);
        m_currentTempPath.clear();
    }
}

void SyncEngine::onSendDone(const QString &entryId, bool ok, quint32 itemId,
                            const QString &autopsyName) {
    if (entryId != m_currentEntryId || !m_isSyncing)
        return;
    if (ok) clearInflight();
    else { retainFailedInflight(itemId); m_cancelled = true; }

    const QVariantMap &e = m_entries[m_entryPos];
    // Live free-space accounting BEFORE the temp is cleaned up — the
    // device only reports fresh capacity at the next breach.
    if (ok && m_device) {
        const QString sent = m_currentTempPath.isEmpty()
            ? e.value(QStringLiteral("filepath")).toString()
            : m_currentTempPath;
        m_device->noteBytesWritten(QFileInfo(sent).size());
    }
    cleanupCurrentTemp(e.value(QStringLiteral("filepath")).toString());

    if (ok) {
        // Verify is warning-only inside the worker (mac rule: never
        // delete on readback mismatch) — ok means the file is on device.
        m_queue.setProgress(entryId, 1.0);
        markEntryTerminal(entryId, QStringLiteral("verified"), QString());
        if (itemId != 0) {
            m_sentTrackIds.insert(e.value(QStringLiteral("filepath")).toString(), itemId);
            m_sentTrackIdentities.insert(e.value(QStringLiteral("filepath")).toString(),
                                        MusicIdentity::fromMap(e.value("sourceIdentity").toMap()));
        }

        // Keep the DEVICE model fresh so a second sync in the same
        // session dedups against these tracks (staleness caused a full
        // duplicate re-send on the first hardware gate).
        if (m_device)
            m_device->noteSyncedTrack(
                e.value(QStringLiteral("title")).toString(),
                e.value(QStringLiteral("artist")).toString(),
                e.value(QStringLiteral("album")).toString(), itemId,
                e.value(QStringLiteral("discNumber")).toInt(),
                e.value(QStringLiteral("trackNumber")).toInt(),
                e.value(QStringLiteral("durationMs")).toInt());

        // Bucket for the album/artist forge phase, keyed albumartist\talbum.
        QString art = e.value(QStringLiteral("artist")).toString();
        if (art.isEmpty())
            art = QStringLiteral("Unknown Artist");
        QString alb = e.value(QStringLiteral("album")).toString();
        if (alb.isEmpty())
            alb = QStringLiteral("Unknown Album");
        QString aart = e.value(QStringLiteral("albumartist")).toString();
        if (aart.isEmpty())
            aart = art;

        const QString key = aart + QLatin1Char('\t') + alb;
        QVariantMap &bucket = m_buckets[key];
        if (bucket.isEmpty()) {
            bucket[QStringLiteral("albumartist")] = aart;
            bucket[QStringLiteral("artist")] = art;
            bucket[QStringLiteral("album")] = alb;
            bucket[QStringLiteral("genre")] =
                e.value(QStringLiteral("genre")).toString();
            // Art: canonical (albumartist) cache key first, legacy
            // pre-canonical track-artist key as fallback (mac order).
            QString artPath = m_artCache.cachedArtPath(aart, alb);
            if (artPath.isEmpty())
                artPath = m_artCache.cachedArtPath(
                    e.value(QStringLiteral("origArtist")).toString(), alb);
            bucket[QStringLiteral("artJpegPath")] = artPath;
            bucket[QStringLiteral("trackIds")] = QVariantList{};
        }
        QVariantList ids = bucket[QStringLiteral("trackIds")].toList();
        ids.append(uint(itemId));
        bucket[QStringLiteral("trackIds")] = ids;
    } else {
        m_lastError = autopsyName;
        markEntryTerminal(entryId, QStringLiteral("failed"), humanAutopsy(autopsyName));
    }

    if (m_cancelled) {
        finishSync(false);
        return;
    }
    m_spacing.start();   // 200ms, then advance()
}

void SyncEngine::markEntryTerminal(const QString &entryId,
                                   const QString &status, const QString &note) {
    m_queue.setStatus(entryId, status, note);
    m_completedCount++;
    m_currentFraction = 0;
    emit progressChanged();
}

void SyncEngine::beginForge() {
    m_syncPhase = QStringLiteral("organizing");   // W2 (album/artist objects)
    emit progressChanged();
    if (m_buckets.isEmpty()) {
        // Photo phase sits between forge and video — a photos-only
        // sync has no buckets and must NOT skip it.
        beginPhotoPhase();
        return;
    }
    m_forging = true;
    m_currentName = QStringLiteral("creating artist + album objects…");
    m_forgeFraction = 0;
    emit progressChanged();
    fprintf(stderr, "[sync] creating %d album objects...\n",
            int(m_buckets.size()));
    QVariantList buckets;
    for (const QVariantMap &b : std::as_const(m_buckets))
        buckets.append(b);
    emit workerForgeAlbums(buckets);
}

void SyncEngine::onForgeProgress(int current, int total) {
    if (!m_forging)
        return;
    m_forgeFraction = total > 0 ? double(current) / double(total) : 0;
    emit progressChanged();
}

void SyncEngine::onForgeDone(int okCount, int failCount) {
    if (!m_forging)
        return;
    fprintf(stderr, "[sync] forge done: %d ok, %d failed\n", okCount, failCount);
    if (failCount > 0 && m_lastError.isEmpty())
        m_lastError = QStringLiteral("%1 album objects failed").arg(failCount);
    m_forging = false;
    if (m_cancelled) {
        finishSync(false);
        return;
    }
    beginPhotoPhase();
}

// ════════════════════════════════════════════════════════════════════
//  Photo phase (mac SyncManager phase 3) — serial sends, no prep
// ════════════════════════════════════════════════════════════════════

void SyncEngine::beginPhotoPhase() {
    if (m_photoEntries.isEmpty()) {
        beginVideoPhase();
        return;
    }
    m_inPhotoPhase = true;
    m_photoPos = -1;
    fprintf(stderr, "[sync] photo phase: %d entries\n",
            int(m_photoEntries.size()));
    advancePhoto();
}

void SyncEngine::advancePhoto() {
    if (m_cancelled) {
        finishSync(false);
        return;
    }
    m_photoPos++;
    if (m_photoPos >= m_photoEntries.size()) {
        m_inPhotoPhase = false;
        beginVideoPhase();
        return;
    }
    const QVariantMap &e = m_photoEntries[m_photoPos];
    const QString entryId = e.value(QStringLiteral("entryId")).toString();
    m_currentName = e.value(QStringLiteral("title")).toString();
    m_currentFraction = 0;
    m_queue.setStatus(entryId, QStringLiteral("syncing"));
    emit workerSyncNotify(m_currentName,
                          int(m_entries.size()) + m_photoPos, m_totalCount);
    m_syncPhase = QStringLiteral("sending");   // W2
    auto recoveryIdentity = e;
    recoveryIdentity.insert(QStringLiteral("filename"), QFileInfo(e.value(QStringLiteral("filepath")).toString()).fileName());
    if (!markInflight(QStringLiteral("photo"), recoveryIdentity)) {
        m_lastError = QStringLiteral("Could not save transfer recovery information. Nothing was sent.");
        m_queue.setStatus(entryId, QStringLiteral("failed"), m_lastError);
        finishSync(false);
        return;
    }
    emit workerSendPhoto(entryId,
                         e.value(QStringLiteral("filepath")).toString(),
                         e.value(QStringLiteral("album")).toString());
    emit progressChanged();
}

void SyncEngine::onPhotoSendDone(const QString &entryId, bool ok, quint32 itemId,
                                 const QString &autopsyName) {
    if (!m_isSyncing || !m_inPhotoPhase)
        return;
    if (m_photoPos < 0 || m_photoPos >= m_photoEntries.size()
        || m_photoEntries[m_photoPos].value(QStringLiteral("entryId")).toString() != entryId) return;
    if (ok) clearInflight();
    else { retainFailedInflight(itemId); m_cancelled = true; }
    m_completedCount++;
    if (ok) {
        m_queue.setStatus(entryId, QStringLiteral("verified"));
        // Badge + dedup freshness mid-session (breach truth returns later)
        if (m_device && m_photoPos >= 0 && m_photoPos < m_photoEntries.size())
            m_device->noteSyncedPhoto(
                m_photoEntries[m_photoPos].value(QStringLiteral("title")).toString());
    } else {
        m_queue.setStatus(entryId, QStringLiteral("failed"), humanAutopsy(autopsyName));
        if (m_lastError.isEmpty())
            m_lastError = QStringLiteral("photo send failed (%1)").arg(autopsyName);
    }
    emit progressChanged();
    advancePhoto();
}

// ════════════════════════════════════════════════════════════════════
//  Video phase (mac SyncManager phase 4)
// ════════════════════════════════════════════════════════════════════

int SyncEngine::resolveVideoProfile() const {
    const int setting = QSettings().value(QStringLiteral("videoProfile"), -1).toInt();
    if (setting >= 0 && setting <= 3)
        return setting;
    // Auto: the device FAMILY (MTP 0xD21A) — the model string is just
    // "Zune" on real hardware and can't distinguish generations. Keel
    // (Zune 30) only plays WMV — profile 0 is the whole reason the
    // Linux port exists. Pavo (HD) takes H.264 at 480x272.
    const int family = m_device ? m_device->deviceFamily() : 0xFF;
    switch (family) {
    case 0x00: return 0;   // Keel — Zune 30, WMV2 320x240
    case 0x06: return 2;   // Pavo — Zune HD, H.264 480x272
    case 0x02:             // Scorpius — Zune 4/8/16
    case 0x03: return 1;   // Draco — Zune 80/120
    default:
        fprintf(stderr,
                "[sync] unknown device family 0x%02X — defaulting to WMV "
                "profile 0 (plays on every Zune)\n", family);
        return 0;
    }
}

void SyncEngine::beginVideoPhase() {
    if (m_videoEntries.isEmpty()) {
        if (m_cancelled)
            finishSync(false);
        else
            beginPlaylistPhase();
        return;
    }
    m_inVideoPhase = true;
    fprintf(stderr, "[sync] video phase: %d entries, profile %d (pipelined)\n",
            int(m_videoEntries.size()), m_videoProfile);
    pumpVideoPipeline();
}

// The pipeline pump — called after every completion (prepare done, send
// done, spacing tick). Keeps ≤2 transcodes running ahead and ≤3 prepared
// files buffered; dispatches sends STRICTLY IN ORDER, one at a time.
void SyncEngine::pumpVideoPipeline() {
    if (!m_isSyncing || !m_inVideoPhase)
        return;
    if (m_cancelled) {
        if (m_activePrepares == 0 && !m_videoSending)
            finishSync(false);
        return;   // in-flight completions will re-pump
    }

    // Producer: keep the encoders fed.
    while (m_activePrepares < 2
           && m_nextPrepareIdx < m_videoEntries.size()
           && m_activePrepares + m_preparedBuf.size() < 3) {
        prepareVideoEntry(m_nextPrepareIdx++, /*isRetry=*/false);
    }

    // All entries sent (or skipped)?
    if (m_nextSendIdx >= m_videoEntries.size()) {
        if (m_activePrepares == 0 && !m_videoSending) {
            m_inVideoPhase = false;
            beginPlaylistPhase();
        }
        return;
    }

    // Consumer: next in-order file, if it's ready and the wire is free.
    if (m_videoSending)
        return;
    for (int i = 0; i < m_preparedBuf.size(); i++) {
        if (m_preparedBuf[i].idx != m_nextSendIdx)
            continue;
        const PreparedVideo pv = m_preparedBuf.takeAt(i);
        if (pv.path.isEmpty()) {
            // Terminal transcode failure — already marked; skip past.
            m_nextSendIdx++;
            pumpVideoPipeline();
            return;
        }
        dispatchVideoSend(pv);
        return;
    }
}

void SyncEngine::dispatchVideoSend(const PreparedVideo &pv) {
    const QVariantMap &e = m_videoEntries[pv.idx];
    m_videoSending = true;
    m_currentEntryId = pv.entryId;
    m_currentSendPath = pv.path;
    m_currentName = e.value(QStringLiteral("title")).toString();
    if (m_currentName.isEmpty())
        m_currentName = e.value(QStringLiteral("series")).toString();
    m_currentIndex = m_entries.size() + pv.idx + 1;
    emit progressChanged();

    const QString series = e.value(QStringLiteral("series")).toString();
    const int season = e.value(QStringLiteral("season")).toInt();
    const int episode = e.value(QStringLiteral("episode")).toInt();
    const QString title = videoDisplayTitle(
        e.value(QStringLiteral("title")).toString(), series, season, episode);
    m_currentName = title;
    const QString wireName = videoWireName(
        e.value(QStringLiteral("title")).toString(), series, season, episode,
        m_videoProfile);
    const int metagenre = videoMetagenre(
        e.value(QStringLiteral("category")).toString(), series, episode);

    fprintf(stderr, "[sync] sending video '%s' (metagenre=0x%02X)\n",
            qPrintable(wireName), metagenre);
    m_queue.setStatus(pv.entryId, QStringLiteral("syncing"));
    // Device-side sync display (0x922A) — name + batch position
    emit workerSyncNotify(title, int(m_entries.size()) + pv.idx,
                          m_totalCount);
    m_syncPhase = QStringLiteral("sending");   // W2
    markInflightVideo(wireName, title); // B2: filename survives title changes
    emit workerSendVideo(pv.entryId, pv.path, wireName, title, metagenre,
                         e.value(QStringLiteral("description")).toString(),
                         e.value(QStringLiteral("posterPath")).toString(),
                         series, season, episode);
}

void SyncEngine::prepareVideoEntry(int idx, bool isRetry) {
    m_syncPhase = QStringLiteral("transcoding");   // W2
    emit progressChanged();
    const QVariantMap e = m_videoEntries[idx];
    const QString entryId = e.value(QStringLiteral("entryId")).toString();
    m_activePrepares++;

    m_queue.setStatus(entryId, QStringLiteral("transcoding"),
                      isRetry ? QStringLiteral("retrying") : QString());

    const QString filepath = e.value(QStringLiteral("filepath")).toString();
    const QString series = e.value(QStringLiteral("series")).toString();
    const int season = e.value(QStringLiteral("season")).toInt();
    const int episode = e.value(QStringLiteral("episode")).toInt();
    const int profile = m_videoProfile;
    // Settings → Device: which audio stream a multi-track source keeps
    // on the Zune (the transcoder falls back to the default stream when
    // no track carries this language).
    const QString audioLang = QSettings().value(
        QStringLiteral("preferredAudioLang"), QStringLiteral("eng")).toString();
    QPointer<SyncEngine> self(this);

    auto discard = QtConcurrent::run(
        [self, entryId, filepath, series, season, episode, profile,
         audioLang, isRetry] {
        struct Ctx {
            QPointer<SyncEngine> engine;
            QString entryId;
            float last = -1;
        } ctx{self, entryId, -1};
        auto cb = [](float fraction, void *userdata) {
            auto *c = static_cast<Ctx *>(userdata);
            if (fraction - c->last < 0.01f)
                return;
            c->last = fraction;
            if (c->engine) {
                const QString id = c->entryId;
                const double f = fraction;
                QMetaObject::invokeMethod(c->engine.data(),
                    [eng = c->engine, id, f] {
                        if (eng)
                            eng->onTranscodeProgress(id, f);
                    }, Qt::QueuedConnection);
            }
        };

        char *p = zuuned_transcode_video(
            filepath.toUtf8().constData(), profile,
            series.isEmpty() ? nullptr : series.toUtf8().constData(),
            season, episode,
            audioLang.isEmpty() ? nullptr : audioLang.toUtf8().constData(),
            cb, &ctx);

        QString prepared;
        QString error;
        if (p) {
            prepared = QString::fromUtf8(p);
            free(p);
        } else {
            error = QStringLiteral("video transcode failed");
        }
        if (self)
            QMetaObject::invokeMethod(self.data(),
                [self, entryId, prepared, error, isRetry] {
                    if (self)
                        self->onVideoPrepareFinished(entryId, prepared,
                                                     error, isRetry);
                }, Qt::QueuedConnection);
    });
    Q_UNUSED(discard);
}

void SyncEngine::onVideoPrepareFinished(const QString &entryId,
                                        const QString &path,
                                        const QString &error, bool wasRetry) {
    // Sync torn down while we were encoding — just drop the temp.
    if (!m_isSyncing || !m_inVideoPhase) {
        if (!path.isEmpty())
            QFile::remove(path);
        return;
    }
    m_activePrepares--;

    int idx = -1;
    for (int i = 0; i < m_videoEntries.size(); i++)
        if (m_videoEntries[i].value(QStringLiteral("entryId")).toString()
                == entryId) {
            idx = i;
            break;
        }

    if (m_cancelled) {
        if (!path.isEmpty())
            QFile::remove(path);
        markEntryTerminal(entryId, QStringLiteral("failed"),
                          QStringLiteral("cancelled"));
        pumpVideoPipeline();   // finishes when the pipeline drains
        return;
    }

    if (!error.isEmpty()) {
        if (!wasRetry && idx >= 0) {
            // In-process transcode can fail on edge cases — one retry
            // after a 1s pause (mac behavior).
            fprintf(stderr, "[sync] video transcode failed for entry %d — retrying\n",
                    idx);
            QTimer::singleShot(1000, this, [this, idx] {
                if (m_isSyncing && m_inVideoPhase && !m_cancelled)
                    prepareVideoEntry(idx, /*isRetry=*/true);
                else
                    pumpVideoPipeline();
            });
            return;
        }
        m_lastError = error;
        markEntryTerminal(entryId, QStringLiteral("failed"), error);
        // Failed marker keeps the in-order send cursor moving.
        m_preparedBuf.append(PreparedVideo{idx, entryId, QString()});
        pumpVideoPipeline();
        return;
    }

    m_preparedBuf.append(PreparedVideo{idx, entryId, path});
    pumpVideoPipeline();
}

void SyncEngine::onVideoSendDone(const QString &entryId, bool ok,
                                 quint32 itemId, const QString &autopsyName) {
    clearInflight();   // B2: the wire object is complete
    if (entryId != m_currentEntryId || !m_isSyncing || !m_inVideoPhase
        || !m_videoSending)
        return;

    // Live free-space accounting, then clean the transcoded temp.
    if (ok && m_device && !m_currentSendPath.isEmpty())
        m_device->noteBytesWritten(QFileInfo(m_currentSendPath).size());
    if (!m_currentSendPath.isEmpty()) {
        QFile::remove(m_currentSendPath);
        m_currentSendPath.clear();
    }
    m_videoSending = false;

    const int idx = m_nextSendIdx;
    m_nextSendIdx++;

    if (ok) {
        m_queue.setProgress(entryId, 1.0);
        markEntryTerminal(entryId, autopsyName.isEmpty() ? QStringLiteral("verified")
                                                        : QStringLiteral("sent"), autopsyName);
        if (!autopsyName.isEmpty()) m_videoWarningsThisRun.insert(entryId);
        if (idx >= 0 && idx < m_videoEntries.size() && m_device) {
            const QVariantMap &e = m_videoEntries[idx];
            const QString series = e.value(QStringLiteral("series")).toString();
            const int episode = e.value(QStringLiteral("episode")).toInt();
            const int metagenre = videoMetagenre(
                e.value(QStringLiteral("category")).toString(), series, episode);
            const int season = e.value(QStringLiteral("season")).toInt();
            const QString title = videoDisplayTitle(e.value(QStringLiteral("title")).toString(),
                                                     series, season, episode);
            m_device->noteSyncedVideo({
                {"itemId", itemId}, {"name", title}, {"title", title},
                {"filename", videoWireName(e.value(QStringLiteral("title")).toString(),
                                            series, season, episode, m_videoProfile)},
                {"format", m_videoProfile == 0 ? 0xB981 : 0xB982},
                {"sizeMB", 0}, {"metagenre", metagenre},
                {"kind", metagenre == 0x26 ? QStringLiteral("tv show")
                          : metagenre == 0x23 ? QStringLiteral("music video")
                          : metagenre == 0x21 ? QStringLiteral("video") : QStringLiteral("movie")},
                {"series", series}, {"season", season}, {"episode", episode},
                {"episodeTitle", metagenre == 0x26 ? title : QString()}});
        }
    } else {
        m_lastError = autopsyName;
        markEntryTerminal(entryId, QStringLiteral("failed"), humanAutopsy(autopsyName));
    }

    if (m_cancelled) {
        if (m_activePrepares == 0)
            finishSync(false);
        return;
    }
    m_spacing.start();   // 200ms breathing room, then pump
}

void SyncEngine::cancelSync() {
    if (!m_isSyncing)
        return;
    fprintf(stderr, "[sync] cancel requested\n");
    m_cancelled = true;
    if (m_worker)
        m_worker->requestAbort();   // mid-transfer abort (thread-safe)
    // Kill an in-flight retag/transcode too — without this, cancel
    // during a video encode only took effect when the episode finished
    // (minutes later; user-visibly "the cancel button does nothing").
    zuuned_transcode_abort();
    // The in-flight prepare/send completes into onPrepareFinished /
    // onSendDone, which see m_cancelled and finish. A cancel while
    // WAITING in the spacing timer must finish here.
    if (m_spacing.isActive()) {
        m_spacing.stop();
        finishSync(false);
    }
}

// ════════════════════════════════════════════════════════════════════
//  Playlist phase (UX-2) — the last wire work before finalize
// ════════════════════════════════════════════════════════════════════

void SyncEngine::beginPlaylistPhase() {
    if (m_cancelled) {
        finishSync(false);
        return;
    }
    // Which queued playlist rows are still live and eligible?
    m_playlistRound.clear();
    for (const SyncQueueEntry &e : m_queue.entries()) {
        if (e.type != QLatin1String("playlist"))
            continue;
        if (e.status != QLatin1String("pending")
            && e.status != QLatin1String("failed"))
            continue;
        for (int i = 0; i < m_pendingPlaylists.size(); i++)
            if (m_pendingPlaylists[i].entryId == e.entryId) {
                m_playlistRound.append(i);
                if (e.status == QLatin1String("failed"))
                    m_queue.setStatus(e.entryId, QStringLiteral("pending"));
                break;
            }
    }
    if (m_playlistRound.isEmpty()) {
        finishSync(true);
        return;
    }

    // Resolve against the CURRENT device track set — pre-existing rows
    // plus everything noteSyncedTrack just appended this sync.
    m_playlistIdentities = deviceDedupIndex();

    m_inPlaylistPhase = true;
    m_playlistPos = 0;
    fprintf(stderr, "[sync] playlist phase: %d playlists, %d device tracks "
            "indexed\n", int(m_playlistRound.size()), int(m_playlistIdentities.size()));
    advancePlaylist();
}

void SyncEngine::advancePlaylist() {
    if (m_cancelled) {
        finishSync(false);
        return;
    }
    if (m_playlistPos >= m_playlistRound.size()) {
        m_inPlaylistPhase = false;
        m_playlistIdentities = MusicIdentity::Index{};
        finishSync(true);
        return;
    }

    const PendingPlaylist &pl =
        m_pendingPlaylists[m_playlistRound[m_playlistPos]];

    // Exact returned IDs survive artist canonicalization within this sync.
    // Otherwise resolve all candidates without an artist-free fallback.
    QVariantList ids;
    bool unresolved = false;
    const auto sourceIdentities = musicIndex(pl.members);
    for (const auto &value : pl.members) {
        const auto member = value.toMap();
        const QString path = member.value(QStringLiteral("filepath")).toString();
        quint32 id = path.isEmpty() ? 0 : m_sentTrackIds.value(path, 0);
        if (id != 0 && (!m_sentTrackIdentities.contains(path)
            || m_sentTrackIdentities.value(path) != MusicIdentity::fromMap(member))) {
            unresolved = true;
            break; // The file's saved identity changed after it was queued.
        }
        if (id == 0) {
            const auto match = m_playlistIdentities.resolve(MusicIdentity::fromMap(member),
                                                            identityPeers(member, sourceIdentities));
            if (match.matched()) id = quint32(match.itemId);
        }
        if (id == 0) { unresolved = true; break; }
        ids.append(id); // Retain repeated playlist occurrences and order.
    }

    if (unresolved || ids.isEmpty()) {
        markEntryTerminal(pl.entryId, QStringLiteral("failed"),
                          QStringLiteral("Playlist has missing or ambiguous device tracks; no playlist was written."));
        m_playlistPos++;
        advancePlaylist();
        return;
    }

    fprintf(stderr, "[sync] forging playlist '%s': %d members resolved\n",
            pl.name.toUtf8().constData(), int(ids.size()));
    m_currentName = QStringLiteral("playlist: ") + pl.name;
    m_queue.setStatus(pl.entryId, QStringLiteral("syncing"));
    emit progressChanged();
    emit workerForgePlaylist(pl.name, ids);
}

void SyncEngine::onPlaylistForgeDone(const QString &name, quint32 playlistId,
                                     bool ok) {
    Q_UNUSED(playlistId);
    if (!m_isSyncing || !m_inPlaylistPhase)
        return;
    if (m_playlistPos >= m_playlistRound.size())
        return;
    const int pendingIdx = m_playlistRound[m_playlistPos];
    const PendingPlaylist &pl = m_pendingPlaylists[pendingIdx];
    if (pl.name != name)
        return;   // not ours (a QML-side createPlaylist, say)

    if (ok) {
        m_queue.setStatus(pl.entryId, QStringLiteral("verified"));
        m_pendingPlaylists.removeAt(pendingIdx);
        // Indexes after the removed one shift down.
        for (int &idx : m_playlistRound)
            if (idx > pendingIdx)
                idx--;
    } else {
        markEntryTerminal(pl.entryId, QStringLiteral("failed"),
                          QStringLiteral("playlist forge failed"));
    }
    m_playlistPos++;
    advancePlaylist();
}

void SyncEngine::finishSync(bool completed) {
    m_syncPhase.clear();   // W2
    clearInflight();
    schedulePersist();   // A1: statuses settled
    m_isSyncing = false;
    m_forging = false;
    m_inVideoPhase = false;
    m_inPhotoPhase = false;
    m_inPlaylistPhase = false;
    m_spacing.stop();
    // Drain the video pipeline: buffered temps are ours to delete.
    for (const PreparedVideo &pv : std::as_const(m_preparedBuf))
        if (!pv.path.isEmpty())
            QFile::remove(pv.path);
    m_preparedBuf.clear();
    if (!m_currentSendPath.isEmpty()) {
        QFile::remove(m_currentSendPath);
        m_currentSendPath.clear();
    }
    m_videoSending = false;
    normalizeStatuses(m_cancelled ? QStringLiteral("cancelled")
                                  : QStringLiteral("sync ended"));
    if (completed && !m_cancelled) {
        m_syncCompleted = true;
        m_currentName =
            QStringLiteral("sync complete — the zune is re-indexing");
        // Terminal 0x922A: notifies are PRE-transfer, so the device only
        // learns item N finished from item N+1's notify — without this
        // its screen parks at (N-1)/N (user saw a stuck 95%). One extra
        // notify at 100/100 closes the loop.
        emit workerSyncNotify(QStringLiteral("Sync complete"),
                              m_totalCount, m_totalCount);
        // Summary modal counts — tallied BEFORE the queue sweep below.
        int tracksOk = 0, videosOk = 0, photosOk = 0, skipped = 0, failed = 0;
        for (const SyncQueueEntry &e : m_queue.entries()) {
            if (e.status == QLatin1String("verified")
                || (e.status == QLatin1String("sent") && m_videoWarningsThisRun.contains(e.entryId))) {
                if (e.type == QLatin1String("track")) tracksOk++;
                else if (e.type == QLatin1String("video")) videosOk++;
                else if (e.type == QLatin1String("photo")) photosOk++;
            } else if (e.status == QLatin1String("skipped")) {
                skipped++;
            } else if (e.status == QLatin1String("failed")) {
                failed++;
            }
        }
        emit syncSummary(
            double(QDateTime::currentMSecsSinceEpoch() - m_syncStartMs),
            tracksOk, videosOk, photosOk, skipped, failed);
        // D1: persist the outcome — the queue tab shows "last sync"
        // after the queue sweeps itself clean.
        {
            QVariantMap last;
            last.insert(QStringLiteral("when"),
                        QDateTime::currentSecsSinceEpoch());
            last.insert(QStringLiteral("tracks"), tracksOk);
            last.insert(QStringLiteral("videos"), videosOk);
            last.insert(QStringLiteral("photos"), photosOk);
            last.insert(QStringLiteral("skipped"), skipped);
            last.insert(QStringLiteral("failed"), failed);
            last.insert(QStringLiteral("device"), currentDeviceIdent());
            m_lastSync = last;
            emit lastSyncChanged();
            QFile f(queueStorePath() + QStringLiteral(".lastsync"));
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(QJsonObject::fromVariantMap(last))
                            .toJson(QJsonDocument::Compact));
        }
        // Auto-clear: successful entries leave the queue; failed ones
        // stay visible for retry.
        m_queue.removeCompleted();
        if (m_device) {
            m_device->setAwaitingDisconnect(true);
            // Swap our running estimate for the device's own numbers,
            // then finalize + close the session: an open session keeps
            // the device parked in sync mode at 100% — closing it is
            // what starts the on-device DB re-index (mac
            // LIBMTP_Release_Device pattern). Worker queue order:
            // terminal notify → storage refresh → finalize → sever.
            m_device->refreshStorage();
            m_device->finalizeAndRelease();
        }
    } else {
        m_syncCompleted = false;
        m_currentName.clear();
    }
    emit syncStateChanged();
    emit progressChanged();
}

// Fix for the mac known issue: a failed/cancelled sync left entries stuck
// at ".syncing". Any non-terminal status is normalized so items are
// visibly re-queueable.
void SyncEngine::normalizeStatuses(const QString &note) {
    for (const SyncQueueEntry &e : m_queue.entries()) {
        if (e.status == QLatin1String("transcoding")
            || e.status == QLatin1String("syncing"))
            m_queue.setStatus(e.entryId, QStringLiteral("failed"), note);
    }
}
