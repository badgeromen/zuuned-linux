#include "DeviceWorker.h"
#include "sync/RecoveryIdentity.h"
#include "library/VideoIdentity.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QThread>

#include <vector>

#include <algorithm>

extern "C" {
#include "zune.h"
}

void DeviceWorker::doBreach() {
    BreachData r;

    if (m_dev) { // already connected — treat as success, no rescan
        emit breachDone(r);
        return;
    }

    emit stage(QStringLiteral("Opening USB + MTPZ authentication…"), 0.15);
    ZuneDevice *dev = zune_breach();
    if (!dev) {
        const char *err = zune_get_error();
        r.error = err && err[0]
            ? QString::fromUtf8(err)
            : QStringLiteral("No Zune found (check USB + udev rule)");
        emit breachDone(r);
        return;
    }

    m_dev = dev;
    m_devShared.store(dev);
    emit stage(QStringLiteral("Reading device info…"), 0.45);
    r.ok = true;
    r.name = QString::fromUtf8(zune_get_name(dev));
    r.model = QString::fromUtf8(zune_get_model(dev));
    r.serial = QString::fromUtf8(zune_get_serial(dev));
    r.family = int(zune_get_family(dev));
    fprintf(stderr, "[worker] device family: 0x%02X\n", r.family);
    r.battery = zune_get_battery(dev);
    r.capacityGB = zune_get_capacity(dev) / 1e9;
    r.freeGB = zune_get_headroom(dev) / 1e9;

    // ZMDB scan — the whole library in one vendor operation.
    emit stage(QStringLiteral("Scanning library (ZMDB)…"), 0.6);
    ZuneDBLibrary *lib = nullptr;
    if (zune_infiltrate(dev, &lib) == 0 && lib) {
        r.tracks.reserve(lib->track_count);
        for (int i = 0; i < lib->track_count; i++) {
            const ZuneTrack &t = lib->tracks[i];
            TrackRow row;
            row.itemId = t.item_id;
            row.title = QString::fromUtf8(t.title ? t.title : "");
            row.artist = QString::fromUtf8(t.artist ? t.artist : "");
            row.album = QString::fromUtf8(t.album ? t.album : "");
            row.genre = QString::fromUtf8(t.genre ? t.genre : "");
            row.durationMs = int(t.duration_ms);
            row.trackNumber = t.tracknumber;
            row.discNumber = t.disc_number;
            // Preserve returned disc metadata. Raw ZMDB track slots on Keel
            // are known to contain garbage; intended send metadata is separate.
            row.trackNumberReliable = false;
            row.identityFromReadback = true;
            row.filesize = qint64(t.filesize);
            r.tracks.append(std::move(row));
        }
        r.albumCount = lib->album_count;
        r.artistCount = lib->artist_count;
        // NOTE: lib stays alive until AFTER the video/photo/playlist
        // loops below — freeing it here was a latent use-after-free
        // that detonated the moment the video loop started allocating
        // (series vendor-prop reads recycled the freed arrays).

        std::sort(r.tracks.begin(), r.tracks.end(),
                  [](const TrackRow &a, const TrackRow &b) {
            int c = a.artist.compare(b.artist, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
            c = a.album.compare(b.album, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
            if (a.trackNumber != b.trackNumber)
                return a.trackNumber < b.trackNumber;
            return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        });

        // Grouped lists for the artists/albums/genres pivots, derived
        // from tracks so counts always match what's shown. Album entries
        // carry the first track's itemId so the UI can request art.
        QMap<QString, int> artistCounts, genreCounts;
        struct AlbumAgg { QString artist; int count = 0; quint32 artItem = 0; };
        QMap<QString, AlbumAgg> albumAgg;
        for (const TrackRow &t : std::as_const(r.tracks)) {
            const QString artist = t.artist.isEmpty()
                ? QStringLiteral("Unknown Artist") : t.artist;
            artistCounts[artist]++;
            if (!t.album.isEmpty()) {
                AlbumAgg &agg = albumAgg[t.album];
                agg.artist = artist;
                agg.count++;
                if (agg.artItem == 0)
                    agg.artItem = t.itemId;
            }
            if (!t.genre.isEmpty())
                genreCounts[t.genre]++;
        }
        for (auto it = artistCounts.cbegin(); it != artistCounts.cend(); ++it)
            r.artists.append(QVariantMap{{"name", it.key()},
                                         {"count", it.value()}});
        for (auto it = albumAgg.cbegin(); it != albumAgg.cend(); ++it)
            r.albums.append(QVariantMap{{"name", it.key()},
                                        {"subtitle", it.value().artist},
                                        {"count", it.value().count},
                                        {"artItemId", it.value().artItem}});
        for (auto it = genreCounts.cbegin(); it != genreCounts.cend(); ++it)
            r.genres.append(QVariantMap{{"name", it.key()},
                                        {"count", it.value()}});

        // Device videos / pictures / playlists for the browse pages.
        auto metagenreLabel = [](uint16_t mg) -> QString {
            switch (mg) {
            case 0x23: return QStringLiteral("music video");
            case 0x25: return QStringLiteral("movie");
            case 0x26: return QStringLiteral("tv show");
            default:   return QStringLiteral("video");
            }
        };
        auto buildVideoEntry = [dev, &metagenreLabel](const ZuneVideoFile &v) {
            QVariantMap entry{
                {"itemId", quint32(v.item_id)},
                {"name", QString::fromUtf8(v.title && v.title[0] ? v.title : v.filename ? v.filename : "")},
                {"title", QString::fromUtf8(v.title ? v.title : "")},
                {"filename", QString::fromUtf8(v.object_filename ? v.object_filename : "")},
                {"format", int(v.object_format)},
                {"sizeMB", double(v.filesize) / 1e6},
                {"metagenre", int(v.metagenre)},
                {"kind", metagenreLabel(v.metagenre)}};
            // TV episodes: series/season/episode/title live in Zune
            // vendor properties (0xDA9A/0xDAB5/0xDAB6) — the mac reads
            // these at connect so the device browser can group by show.
            if (v.metagenre == 0x26) {
                char *series = nullptr, *title = nullptr;
                int season = 0, episode = 0;
                if (zune_get_series_info(dev, v.item_id, &series, &season,
                                         &episode, &title) == 0) {
                    entry.insert(QStringLiteral("series"),
                                 QString::fromUtf8(series ? series : ""));
                    entry.insert(QStringLiteral("season"), season);
                    entry.insert(QStringLiteral("episode"), episode);
                    const QString episodeTitle = QString::fromUtf8(title ? title : "");
                    entry.insert(QStringLiteral("episodeTitle"), episodeTitle);
                    if (!episodeTitle.isEmpty()) {
                        entry.insert(QStringLiteral("name"), episodeTitle);
                        entry.insert(QStringLiteral("title"), episodeTitle);
                    }
                }
                free(series);
                free(title);
            }
            return entry;
        };
        for (int i = 0; i < lib->video_count; i++)
            r.videos.append(buildVideoEntry(lib->videos[i]));

        // Zune HD (Pavo): the ZMDB scan carries no video rows — the mac
        // falls back to plain MTP enumeration, and so do we.
        if (r.videos.isEmpty() && r.family == 0x06) {
            emit stage(QStringLiteral("Loading videos (HD MTP fallback)…"), 0.8);
            int nvid = 0;
            if (ZuneVideoFile *vids = zune_get_videos(dev, &nvid)) {
                for (int i = 0; i < nvid; i++)
                    r.videos.append(buildVideoEntry(vids[i]));
                zune_free_videos(vids, nvid);
                fprintf(stderr, "[worker] HD MTP video fallback: %d videos\n",
                        nvid);
            }
        }
        // Dead-pipe guard for every per-item probe below: once the
        // transport reports 0x02FF, every further call burns a full
        // 10s USB timeout — ~50 photos turned a failed enumeration
        // into an 8-minute "connection freeze" (Pavo, 2026-08-31).
        // Two consecutive failures → stop probing, keep what we have.
        int probeFailStreak = 0;
        for (int i = 0; i < lib->photo_count; i++) {
            const ZunePhotoFile &p = lib->photos[i];
            QString name = QString::fromUtf8(p.filename ? p.filename : "");
            // Classic ZMDB photo records resolve to empty names —
            // backfill from the MTP object (photos are few; ~50ms each).
            if (name.isEmpty() && probeFailStreak < 2) {
                char *mtpName = nullptr;
                if (zune_probe_object_named(m_dev, p.item_id, nullptr,
                                            nullptr, &mtpName) == 0
                    && mtpName) {
                    name = QString::fromUtf8(mtpName);
                    free(mtpName);
                    probeFailStreak = 0;
                } else {
                    probeFailStreak++;
                    if (probeFailStreak >= 2)
                        fprintf(stderr, "[worker] photo-name backfill: "
                                "transport looks dead — skipping the rest\n");
                }
            }
            r.photos.append(QVariantMap{
                {"itemId", quint32(p.item_id)},
                {"name", name},
                {"parentId", quint32(p.parent_id)},
                {"sizeMB", double(p.filesize) / 1e6}});
        }
        {
            // Stats-tab breakdown depends on these — log what the ZMDB
            // picture records actually carried (HD sizes suspect 0).
            int pz = 0;
            double pmb = 0;
            for (const QVariant &v : std::as_const(r.photos)) {
                const double mb = v.toMap().value(QStringLiteral("sizeMB")).toDouble();
                if (mb <= 0.0) pz++;
                pmb += mb;
            }
            fprintf(stderr, "[worker] photo sizes: %d total, %d zero, %.1f MB summed\n",
                    int(r.photos.size()), pz, pmb);
            int vz = 0;
            double vmb = 0;
            for (const QVariant &v : std::as_const(r.videos)) {
                const double mb = v.toMap().value(QStringLiteral("sizeMB")).toDouble();
                if (mb <= 0.0) vz++;
                vmb += mb;
            }
            fprintf(stderr, "[worker] video sizes: %d total, %d zero, %.1f MB summed\n",
                    int(r.videos.size()), vz, vmb);
            for (int i = 0; i < qMin(3, int(r.videos.size())); i++) {
                const QVariantMap m = r.videos[i].toMap();
                fprintf(stderr, "[worker]   video[%d] '%s' %.1f MB\n", i,
                        m.value(QStringLiteral("name")).toString().toUtf8().constData(),
                        m.value(QStringLiteral("sizeMB")).toDouble());
            }
        }
        // Photo albums = the MTP FOLDER objects the photos actually
        // live in. The ZMDB's photo-album atoms carry different ids
        // than the photos' MTP parent_id, so grouping by them pooled
        // everything under root — resolve real folder names instead.
        {
            QSet<quint32> parents;
            for (int i = 0; i < lib->photo_count; i++)
                if (lib->photos[i].parent_id != 0)
                    parents.insert(quint32(lib->photos[i].parent_id));
            if (!parents.isEmpty()) {
                ZuneFolderEntry *folders = nullptr;
                const int nf = zune_get_folders(m_dev, &folders);
                QHash<quint32, QString> folderName;
                for (int i = 0; i < nf; i++)
                    folderName.insert(quint32(folders[i].id),
                                      QString::fromUtf8(
                                          folders[i].name ? folders[i].name : ""));
                if (folders)
                    zune_free_folders(folders, nf);
                for (quint32 pid : std::as_const(parents)) {
                    const QString name = folderName.value(pid);
                    r.photoAlbums.append(QVariantMap{
                        {"itemId", pid},
                        {"name", name.isEmpty()
                                     ? QStringLiteral("photos") : name}});
                }
            }
        }
        // Playlists via MTP ENUMERATION, not the ZMDB — the ZMDB
        // playlist record layout is undecoded (scan reports 0 on both
        // families even with real playlists present). A format-filtered
        // handle query + per-playlist name/refs is cheap: playlists
        // are few. (Phase 9, hardware-verified Keel + Pavo.)
        {
            int npl = 0;
            ZunePlaylist *pls = zune_get_playlists(m_dev, &npl);
            for (int i = 0; i < npl; i++) {
                QVariantList ids;
                for (uint32_t j = 0; j < pls[i].track_count; j++)
                    ids.append(quint32(pls[i].track_ids[j]));
                r.playlists.append(QVariantMap{
                    {"itemId", quint32(pls[i].playlist_id)},
                    {"name", QString::fromUtf8(pls[i].name ? pls[i].name : "")},
                    {"count", int(pls[i].track_count)},
                    {"trackIds", ids}});
            }
            zune_free_playlists(pls, npl);
        }
        zune_free_scan(lib);

        // Album objects (MTP enumeration) — the mac's getAlbumList()
        emit stage(QStringLiteral("Enumerating albums…"), 0.85);
        // path. Each carries its member track_ids; art requests for any
        // track get redirected to its album object.
        int albumObjCount = 0;
        if (ZuneAlbumObject *objs = zune_get_albums(dev, &albumObjCount)) {
            for (int i = 0; i < albumObjCount; i++) {
                const QString albumArtist = QString::fromUtf8(
                    objs[i].artist ? objs[i].artist : "");
                for (uint32_t t = 0; t < objs[i].track_count; t++) {
                    r.trackToAlbumArt.insert(objs[i].track_ids[t],
                                             objs[i].album_id);
                    if (!albumArtist.isEmpty())
                        r.trackToAlbumArtist.insert(objs[i].track_ids[t],
                                                   albumArtist);
                }
            }
            zune_free_albums(objs, albumObjCount);
        }
    }

    emit breachDone(r);
}

void DeviceWorker::doSever() {
    if (m_dev) {
        m_devShared.store(nullptr);
        zune_sever(m_dev);
        m_dev = nullptr;
    }
    emit severDone();
}

void DeviceWorker::requestAbort() {
    // zune_abort just sets the device's volatile cancel flag — safe from
    // any thread. The handle can only vanish through doSever, which
    // clears m_devShared BEFORE freeing.
    if (ZuneDevice *dev = m_devShared.load())
        zune_abort(dev);
}

void DeviceWorker::doPurgeItems(const QVariantList &itemIds) {
    int ok = 0, fail = 0;
    QVariantList purged;
    const int total = itemIds.size();
    fprintf(stderr, "[worker] purge: %d item(s) requested\n", total);
    for (int i = 0; i < total; i++) {
        if (!m_dev) { fail = total - i; break; }
        const quint32 id = itemIds[i].toUInt();
        emit purgeProgress(i + 1, total);
        if (zune_purge_track(m_dev, id) == 0) {  // generic DeleteObject
            fprintf(stderr, "[worker] purged %u ok\n", id);
            ok++;
            purged.append(id);
        } else {
            fprintf(stderr, "[worker] purge failed for %u (%s)\n", id,
                    zune_autopsy_name(zune_autopsy(m_dev)));
            fail++;
        }
        QThread::msleep(100); // firmware breathing room
    }
    emit purgeDone(ok, fail, purged);
}

void DeviceWorker::doPurgeInterruptedVideo(const QString &filename) {
    // Resolve ObjectFileName directly only when recovery is requested.
    // ZMDB exposes Name instead; matching that could delete an unrelated
    // episode called "Pilot". Refuse ambiguous filename matches too.
    QVariantList ids;
    if (m_dev && !filename.isEmpty()) {
        int count = 0;
        ZuneVideoFile *videos = zune_get_videos(m_dev, &count);
        for (int i = 0; videos && i < count; ++i) {
            const QString actual = QString::fromUtf8(videos[i].object_filename
                                                       ? videos[i].object_filename : "");
            if (VideoIdentity::matchesInterruptedFile({{"filename", actual}}, filename))
                ids.append(quint32(videos[i].item_id));
        }
        zune_free_videos(videos, count);
    }
    bool ok = false;
    if (ids.size() == 1) {
        emit purgeProgress(1, 1);
        ok = zune_purge_video(m_dev, ids.first().toUInt()) == 0;
    }
    fprintf(stderr, "[worker] interrupted video '%s': %d exact filename matches, purge %s\n",
            qPrintable(filename), int(ids.size()), ok ? "OK" : "refused/failed");
    emit purgeDone(ok ? 1 : 0, ok ? 0 : 1, ok ? ids : QVariantList());
    emit interruptedVideoPurged(filename, ok);
}

void DeviceWorker::doPurgeInterruptedObject(const QVariantMap &record) {
    bool ok = false;
    const auto id = RecoveryIdentity::objectId(record.value("itemId"));
    if (m_dev && RecoveryIdentity::refusal(record, QString::fromUtf8(zune_get_serial(m_dev))).isEmpty()) {
        QVariantList matches;
        int count = 0;
        if (record.value("type") == "music") {
            auto *tracks = zune_get_tracks(m_dev, &count);
            for (int i = 0; tracks && i < count; ++i) {
                const auto &t = tracks[i];
                QVariantMap actual{{"itemId", t.item_id}, {"title", QString::fromUtf8(t.title)},
                    {"artist", QString::fromUtf8(t.artist)}, {"album", QString::fromUtf8(t.album)},
                    {"trackNumber", t.tracknumber}, {"discNumber", t.disc_number}};
                if (RecoveryIdentity::matches(record, actual)) matches.append(id);
            }
            zune_free_tracks(tracks, count);
        } else {
            auto *photos = zune_get_photos(m_dev, &count);
            for (int i = 0; photos && i < count; ++i)
                if (RecoveryIdentity::matches(record, {{"itemId", photos[i].item_id},
                        {"filename", QString::fromUtf8(photos[i].filename)}})) matches.append(id);
            zune_free_photos(photos, count);
        }
        // Re-read through the live serialized worker before deletion. Stale
        // UI rows, another device or an ID reused for other media cannot pass.
        if (matches.size() == 1) ok = zune_purge_track(m_dev, id) == 0;
    }
    emit purgeDone(ok ? 1 : 0, ok ? 0 : 1, ok ? QVariantList{id} : QVariantList{});
    emit interruptedObjectPurged(record.value("token").toString(), ok);
}

// ── Sync (Phase 5) ──

namespace {
struct SendProgressCtx {
    DeviceWorker *worker;
    const QString *entryId;
    double lastEmitted;
};

void sendProgressThunk(uint64_t sent, uint64_t total, void *userdata) {
    auto *ctx = static_cast<SendProgressCtx *>(userdata);
    if (total == 0)
        return;
    const double frac = double(sent) / double(total);
    // Throttle: a 100MB file at 64KB chunks would emit thousands of
    // queued signals — 1% steps are plenty for an 8px bar.
    if (frac - ctx->lastEmitted >= 0.01 || frac >= 1.0) {
        ctx->lastEmitted = frac;
        ctx->worker->notifySendProgress(*ctx->entryId, frac);
    }
}
} // namespace

void DeviceWorker::doSendTrack(const QString &entryId, const QString &filepath,
                               const QString &title, const QString &albumartist,
                               const QString &album, const QString &genre,
                               int trackNumber, int durationMs) {
    if (!m_dev) {
        emit sendDone(entryId, false, 0, QStringLiteral("NotConnected"));
        return;
    }

    zune_clear_abort(m_dev);

    SendProgressCtx ctx{this, &entryId, 0.0};
    uint32_t itemId = 0;
    // Mac rule: the wire artist IS the albumartist — track-level artist
    // display comes from the artist objects forged in phase 2.
    const int rc = zune_smuggle_track_ex(
        m_dev, filepath.toUtf8().constData(),
        title.isEmpty() ? nullptr : title.toUtf8().constData(),
        albumartist.isEmpty() ? nullptr : albumartist.toUtf8().constData(),
        album.isEmpty() ? nullptr : album.toUtf8().constData(),
        genre.isEmpty() ? nullptr : genre.toUtf8().constData(),
        uint16_t(std::clamp(trackNumber, 0, 0xFFFF)),
        uint32_t(std::max(0, durationMs)),
        sendProgressThunk, &ctx, &itemId);

    const QString autopsy =
        QString::fromUtf8(zune_autopsy_name(zune_autopsy(m_dev)));

    if (rc != 0) {
        emit sendDone(entryId, false, itemId, autopsy);
        return;
    }

    // Verify readback — warning-only, never deletes (mac posture)
    if (zune_verify(m_dev, itemId,
                    title.isEmpty() ? nullptr : title.toUtf8().constData(),
                    nullptr, 0) != 0) {
        fprintf(stderr, "[worker] verify mismatch for %s (item %u) — keeping\n",
                qPrintable(title), itemId);
    }

    emit sendDone(entryId, true, itemId, autopsy);
}

void DeviceWorker::doSendVideo(const QString &entryId, const QString &filepath,
                               const QString &objectFilename, const QString &title, int metagenre,
                               const QString &description,
                               const QString &posterPath,
                               const QString &series, int season, int episode) {
    if (!m_dev) {
        emit sendVideoDone(entryId, false, 0, QStringLiteral("NotConnected"));
        return;
    }

    zune_clear_abort(m_dev);

    QByteArray poster;
    if (!posterPath.isEmpty()) {
        QFile f(posterPath);
        if (f.open(QIODevice::ReadOnly))
            poster = f.readAll();
    }
    const uint8_t *posterData =
        poster.isEmpty() ? nullptr
                         : reinterpret_cast<const uint8_t *>(poster.constData());

    fprintf(stderr, "[worker] sending video: %s (file=%s metagenre=0x%02X)\n",
            qPrintable(title), qPrintable(objectFilename), metagenre);
    const QByteArray title8 = title.toUtf8();
    uint32_t itemId = 0;
    const int rc = zune_smuggle_video_named(m_dev, filepath.toUtf8().constData(),
        objectFilename.toUtf8().constData(), title8.constData(), uint16_t(metagenre),
        series.toUtf8().constData(), season, episode,
        description.toUtf8().constData(), posterData, size_t(poster.size()), &itemId);
    // A completed upload with metadata trouble must never be re-uploaded
    // automatically. Keep the real object ID and surface a repairable warning.
    QString warning;
    if (rc < 0) {
        emit sendVideoDone(entryId, false, itemId,
            QString::fromUtf8(zune_get_error()));
        return;
    }
    if (rc == ZUNE_VIDEO_METADATA_INCOMPLETE)
        warning = QStringLiteral("uploaded; ") + QString::fromUtf8(zune_get_error());
    // Name now has its own contract, so verify it as well as byte size.
    if (zune_verify(m_dev, itemId, title8.constData(), nullptr,
                    quint64(QFileInfo(filepath).size())) != 0) {
        if (!warning.isEmpty()) warning += QStringLiteral("; ");
        warning += QStringLiteral("uploaded; title or size readback could not be verified");
    }
    emit sendVideoDone(entryId, true, itemId, warning);
}

void DeviceWorker::doForgePlaylist(const QString &name,
                                   const QVariantList &trackIds) {
    if (!m_dev || name.isEmpty()) {
        emit forgePlaylistDone(name, 0, false);
        return;
    }
    zune_clear_abort(m_dev);
    QVector<uint32_t> ids;
    ids.reserve(trackIds.size());
    for (const QVariant &v : trackIds)
        ids.append(v.toUInt());
    const quint32 plid = zune_forge_playlist(
        m_dev, name.toUtf8().constData(), ids.data(), int(ids.size()));
    if (plid == 0)
        fprintf(stderr, "[worker] playlist forge failed (%s)\n",
                zune_autopsy_name(zune_autopsy(m_dev)));
    emit forgePlaylistDone(name, plid, plid != 0);
}

void DeviceWorker::doSendPhoto(const QString &entryId, const QString &filepath,
                               const QString &albumName) {
    if (!m_dev) {
        emit sendPhotoDone(entryId, false, 0, QStringLiteral("no device"));
        return;
    }
    zune_clear_abort(m_dev);
    fprintf(stderr, "[worker] sending photo: %s (album='%s')\n",
            qPrintable(QFileInfo(filepath).fileName()), qPrintable(albumName));
    const int rc = zune_smuggle_photo(
        m_dev, filepath.toUtf8().constData(),
        albumName.isEmpty() ? nullptr : albumName.toUtf8().constData());
    const QString autopsy = rc == 0
        ? QString()
        : QString::fromUtf8(zune_autopsy_name(zune_autopsy(m_dev)));
    emit sendPhotoDone(entryId, rc == 0, 0, autopsy);
}

void DeviceWorker::doExtractPhoto(quint32 itemId, const QString &destPath) {
    if (!m_dev) {
        emit extractPhotoDone(itemId, destPath, false);
        return;
    }
    zune_clear_abort(m_dev);
    const int rc = zune_extract_photo(m_dev, itemId,
                                      destPath.toUtf8().constData());
    if (rc != 0)
        fprintf(stderr, "[worker] photo extract failed for %u (%s)\n", itemId,
                zune_autopsy_name(zune_autopsy(m_dev)));
    emit extractPhotoDone(itemId, destPath, rc == 0);
}

void DeviceWorker::doExtractTrack(quint32 itemId, const QString &destPath) {
    if (!m_dev) {
        emit extractTrackDone(itemId, destPath, false);
        return;
    }
    zune_clear_abort(m_dev);
    fprintf(stderr, "[worker] extracting track %u → %s\n", itemId,
            qPrintable(destPath));
    const int rc = zune_extract_track(m_dev, itemId,
                                      destPath.toUtf8().constData());
    if (rc != 0)
        fprintf(stderr, "[worker] track extract failed (%s)\n",
                zune_autopsy_name(zune_autopsy(m_dev)));
    emit extractTrackDone(itemId, destPath, rc == 0);
}

void DeviceWorker::doExtractVideo(quint32 itemId, const QString &destPath) {
    if (!m_dev) {
        emit extractVideoDone(itemId, destPath, false);
        return;
    }
    zune_clear_abort(m_dev);
    fprintf(stderr, "[worker] extracting video %u → %s\n", itemId,
            qPrintable(destPath));
    const int rc = zune_extract_video(m_dev, itemId,
                                      destPath.toUtf8().constData());
    if (rc != 0)
        fprintf(stderr, "[worker] video extract failed (%s)\n",
                zune_autopsy_name(zune_autopsy(m_dev)));
    emit extractVideoDone(itemId, destPath, rc == 0);
}

void DeviceWorker::doRenameItem(quint32 itemId, const QString &newName) {
    if (!m_dev || newName.isEmpty()) {
        emit renameItemDone(itemId, newName, false);
        return;
    }
    const int rc = zune_rename_item(m_dev, itemId,
                                    newName.toUtf8().constData());
    emit renameItemDone(itemId, newName, rc == 0);
}

void DeviceWorker::doRefreshStorage() {
    if (!m_dev)
        return;
    const double cap = zune_get_capacity(m_dev) / 1e9;
    const double free = zune_get_headroom(m_dev) / 1e9;
    fprintf(stderr, "[worker] storage refresh: %.1f GB free of %.1f GB\n",
            free, cap);
    emit storageRefreshed(cap, free);
}

void DeviceWorker::doRenameDevice(const QString &newName) {
    if (!m_dev || newName.isEmpty()) {
        emit renameDeviceDone(newName, false);
        return;
    }
    const int rc = zune_rename(m_dev, newName.toUtf8().constData());
    fprintf(stderr, "[worker] rename device -> '%s': %s\n",
            qPrintable(newName), rc == 0 ? "OK" : "FAILED");
    emit renameDeviceDone(newName, rc == 0);
}

void DeviceWorker::doSyncNotify(const QString &name, int itemIndex,
                                int totalItems) {
    if (!m_dev)
        return;
    // Wire semantics (capture-bulkreading.pcapng): op_kind 0 = writing
    // TO the device (what the display reacts to), progress fields are
    // whole-batch PERCENTAGES, item index is 1-based. itemIndex ==
    // totalItems is the terminal "sync complete" notify (100/100);
    // the wire index clamps to the batch size.
    const int total = qMax(1, totalItems);
    const quint32 before = quint32(qMin(100, itemIndex * 100 / total));
    const quint32 after = quint32(qMin(100, (itemIndex + 1) * 100 / total));
    zune_sync_notify(m_dev, name.toUtf8().constData(), 0,
                     quint32(qMin(itemIndex + 1, total)), quint32(total),
                     before, after);
}

void DeviceWorker::doForgeAlbums(const QVariantList &buckets) {
    if (!m_dev || buckets.isEmpty()) {
        emit forgeDone(0, m_dev ? 0 : buckets.size());
        return;
    }

    int okCount = 0, failCount = 0;

    // (a) libzune reuses existing device artists; this map additionally
    // coalesces repeated album owners within the current batch.
    QHash<QString, quint32> artistHandles; // lower(name) → handle
    for (const QVariant &v : buckets) {
        const QString aa = v.toMap().value("albumartist").toString();
        const QString key = aa.toLower();
        if (aa.isEmpty() || artistHandles.contains(key))
            continue;
        const uint32_t h = zune_forge_artist(m_dev, aa.toUtf8().constData());
        artistHandles.insert(key, h); // 0 = failed; links skipped below
        if (h == 0)
            fprintf(stderr, "[worker] forge_artist returned 0 for %s "
                    "(unsupported, unreadable inventory or creation failure; "
                    "album artist metadata is retained)\n", qPrintable(aa));
    }
    QThread::msleep(500); // firmware breathing room after artist forging

    // (b) Existing device albums for merge, keyed lower(artist\tname)
    // PLUS a name-only index: albums synced by older clients carry
    // unpredictable Artist strings (pre-albumartist-fix era), and the
    // device identifies albums by NAME anyway — creating a same-named
    // album fails 0x2002. Hardware-observed on the first sync gate.
    struct Existing { quint32 id; QVector<quint32> trackIds; };
    QHash<QString, Existing> existing;
    QHash<QString, Existing> existingByName;
    {
        int n = 0;
        if (ZuneAlbumObject *objs = zune_get_albums(m_dev, &n)) {
            for (int i = 0; i < n; i++) {
                const QString name =
                    QString::fromUtf8(objs[i].name ? objs[i].name : "");
                const QString key =
                    (QString::fromUtf8(objs[i].artist ? objs[i].artist : "")
                     + QLatin1Char('\t') + name).toLower();
                Existing e{objs[i].album_id, {}};
                for (uint32_t t = 0; t < objs[i].track_count; t++)
                    e.trackIds.append(objs[i].track_ids[t]);
                existing.insert(key, e);
                if (!existingByName.contains(name.toLower()))
                    existingByName.insert(name.toLower(), e);
            }
            zune_free_albums(objs, n);
        }
    }

    // (c) Per bucket: merge-or-create, link, brand
    const int total = buckets.size();
    for (int b = 0; b < total; b++) {
        if (zune_is_aborted(m_dev)) {
            failCount += total - b;
            break;
        }
        emit forgeProgress(b + 1, total);

        const QVariantMap bucket = buckets[b].toMap();
        const QString albumartist = bucket.value("albumartist").toString();
        const QString album = bucket.value("album").toString();
        const QString genre = bucket.value("genre").toString();
        const QString artPath = bucket.value("artJpegPath").toString();

        QVector<quint32> trackIds;
        for (const QVariant &t : bucket.value("trackIds").toList())
            trackIds.append(t.toUInt());
        if (album.isEmpty() || trackIds.isEmpty()) {
            failCount++;
            continue;
        }

        quint32 albumId = 0;
        const QString mergeKey =
            (albumartist + QLatin1Char('\t') + album).toLower();
        const Existing *match = nullptr;
        if (existing.contains(mergeKey))
            match = &existing[mergeKey];
        else if (existingByName.contains(album.toLower())) {
            match = &existingByName[album.toLower()];
            fprintf(stderr, "[worker] album '%s' matched by NAME only "
                    "(device artist string differs) — merging\n",
                    qPrintable(album));
        }
        if (match) {
            // Merge: existing ids + new ones, deduped, order preserved
            QVector<quint32> merged = match->trackIds;
            for (quint32 id : std::as_const(trackIds))
                if (!merged.contains(id))
                    merged.append(id);
            std::vector<uint32_t> ids(merged.begin(), merged.end());
            if (zune_rewire_album(m_dev, match->id, album.toUtf8().constData(),
                                  albumartist.toUtf8().constData(),
                                  ids.data(), int(ids.size())) == 0) {
                albumId = match->id;
            }
        } else {
            std::vector<uint32_t> ids(trackIds.begin(), trackIds.end());
            albumId = zune_forge_album(m_dev, album.toUtf8().constData(),
                                       albumartist.toUtf8().constData(),
                                       genre.isEmpty() ? nullptr
                                                       : genre.toUtf8().constData(),
                                       ids.data(), int(ids.size()));
        }

        if (albumId == 0) {
            fprintf(stderr, "[worker] forge/rewire album failed: %s (%s)\n",
                    qPrintable(album),
                    zune_autopsy_name(zune_autopsy(m_dev)));
            failCount++;
            QThread::msleep(300);
            continue;
        }

        // (d) Artist links: album AND each track (ArtistId 0xDAB9)
        const quint32 artistHandle = artistHandles.value(albumartist.toLower(), 0);
        if (artistHandle != 0) {
            zune_link_artist(m_dev, albumId, artistHandle);
            for (quint32 tid : std::as_const(trackIds))
                zune_link_artist(m_dev, tid, artistHandle);
        }

        // (f) Album art via representative sample
        if (!artPath.isEmpty()) {
            QFile f(artPath);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray jpeg = f.readAll();
                if (!jpeg.isEmpty())
                    zune_brand(m_dev, albumId,
                               reinterpret_cast<const uint8_t *>(jpeg.constData()),
                               size_t(jpeg.size()));
            }
        }

        okCount++;
        QThread::msleep(300); // mac's inter-album breathing room
    }

    emit forgeDone(okCount, failCount);
}

void DeviceWorker::doFinalize() {
    if (!m_dev) {
        emit finalizeDone(false);
        return;
    }
    emit finalizeDone(zune_finalize(m_dev) == 0);
}

void DeviceWorker::doGrabPhotoThumb(quint32 itemId, const QString &cachePath) {
    if (QFile::exists(cachePath)) {
        emit artDone(itemId, cachePath, true);
        return;
    }
    if (!m_dev) {
        emit artDone(itemId, cachePath, false);
        return;
    }
    int rc = zune_grab_photo_thumb(m_dev, itemId, cachePath.toUtf8().constData());
    emit artDone(itemId, cachePath, rc == 0 && QFile::exists(cachePath));
}

void DeviceWorker::doGrabArt(quint32 itemId, const QString &cachePath) {
    if (QFile::exists(cachePath)) {
        emit artDone(itemId, cachePath, true);
        return;
    }
    if (!m_dev) {
        emit artDone(itemId, cachePath, false);
        return;
    }
    int rc = zune_grab_thumb(m_dev, itemId, cachePath.toUtf8().constData());
    emit artDone(itemId, cachePath, rc == 0 && QFile::exists(cachePath));
}
