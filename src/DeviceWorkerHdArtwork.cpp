#include "DeviceWorker.h"
#include "artwork/HdArtistArtStore.h"

#include <QByteArray>
#include <QMap>
#include <array>
#include <functional>

extern "C" {
#include "zune.h"
}

namespace {
// Own every byte referenced by the public C config until serve returns. Nothing
// is loaded or edited while the HD is requesting its catalog.
struct Catalog {
    QByteArray artist, guid, biography;
    std::array<QByteArray, ZUNE_HD_ART_ASSET_MAX> ids, images;
    ZuneHDArtConfig config{};

    bool load(const QString &name, const QVariantMap &manifest, QString &error) {
        const auto snapshot = HdArtistArtStore::readSnapshot(manifest, &error);
        if (!error.isEmpty() || snapshot.isEmpty()) return false;
        artist = name.toUtf8();
        guid = snapshot.value("catalogGuid").toString().toUtf8();
        biography = snapshot.value("biography").toString().toUtf8();
        config.version = ZUNE_HD_ART_API_VERSION;
        config.artist_name = artist.constData();
        config.catalog_guid = guid.constData();
        config.duration_ms = 240000;
        config.biography = biography.isEmpty() ? nullptr : biography.constData();
        config.biography_size = size_t(biography.size());
        const auto image = [&](int index, const QVariantMap &value) {
            ids[index] = value.value("id").toString().toUtf8();
            images[index] = value.value("jpeg").toByteArray();
            return ZuneHDArtImage{ids[index].constData(),
                reinterpret_cast<const uint8_t *>(images[index].constData()),
                size_t(images[index].size())};
        };
        config.background = image(0, snapshot.value("background").toMap());
        config.thumbnail = image(1, snapshot.value("thumbnail").toMap());
        const auto gallery = snapshot.value("gallery").toList();
        if (gallery.size() > ZUNE_HD_ART_GALLERY_MAX) {
            error = QStringLiteral("Too many HD gallery images."); return false;
        }
        config.gallery_count = size_t(gallery.size());
        for (int i = 0; i < gallery.size(); ++i)
            config.gallery[i] = image(i + 2, gallery[i].toMap());
        if (zune_hd_art_validate(&config) != ZUNE_HD_ART_OK) {
            error = QString::fromUtf8(zune_get_error());
            if (error.isEmpty()) error = QStringLiteral("Invalid HD artist artwork.");
            return false;
        }
        return true;
    }
};

struct ServeContext {
    std::function<int(ZuneDevice *)> ready;
    std::function<void(const ZuneHDArtResult *)> progress;
};
int readyCallback(ZuneDevice *dev, void *p) {
    return static_cast<ServeContext *>(p)->ready(dev);
}
void progressCallback(const ZuneHDArtResult *result, void *p) {
    static_cast<ServeContext *>(p)->progress(result);
}
}

void DeviceWorker::doForgeAlbums(const QVariantList &buckets) {
    const auto first = buckets.value(0).toMap();
    const QString runToken = first.value("_forgeRunToken").toString();
    const QString serial = first.value("_deviceSerial").toString();
    const quint64 generation = first.value("_deviceGeneration").toULongLong();
    const auto current = [&] {
        if (!m_dev || zune_is_aborted(m_dev)) return false;
        if (runToken.isEmpty()) return true; // Existing non-sync callers.
        return !serial.isEmpty() && generation == m_spokenGeneration.load()
            && serial == QString::fromUtf8(zune_get_serial(m_dev));
    };
    const auto done = [&](int ok, int failed) {
        emit forgeDone(ok, failed);
        emit forgeBatchDone(runToken, ok, failed);
    };
    if (!current()) { done(0, buckets.size()); return; }

    // Ordinary unconfigured sync keeps its existing batch behavior.
    bool configured = false;
    for (const auto &value : buckets)
        configured |= !value.toMap().value("hdArtistArt").toMap().isEmpty();
    if (!configured) {
        const auto counts = forgeAlbumsCore(buckets);
        done(counts.first, counts.second);
        return;
    }

    QMap<QString, QVariantList> groups;
    for (const auto &value : buckets) {
        const auto bucket = value.toMap();
        // A mixed/stale queue is never permission to touch a different device.
        if (bucket.value("_forgeRunToken").toString() != runToken
            || bucket.value("_deviceSerial").toString() != serial
            || bucket.value("_deviceGeneration").toULongLong() != generation) {
            done(0, buckets.size()); return;
        }
        groups[bucket.value("albumartist").toString().toLower()].append(value);
    }
    int ok = 0, failed = 0;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (!current()) { failed += buckets.size() - ok - failed; break; }
        const auto group = it.value();
        const auto bucket = group.first().toMap();
        const QString artist = bucket.value("albumartist").toString();
        auto manifest = bucket.value("hdArtistArt").toMap();
        bool conflicting = false;
        for (const auto &value : group) {
            const auto other = value.toMap().value("hdArtistArt").toMap();
            conflicting |= other != manifest;
            if (manifest.isEmpty() && !other.isEmpty()) manifest = other;
        }
        if (manifest.isEmpty()) {
            const auto counts = forgeAlbumsCore(group);
            ok += counts.first; failed += counts.second;
            continue;
        }
        QVariantMap report{{"runToken", runToken}, {"revision", manifest.value("revision")},
            {"catalogGuid", manifest.value("catalogGuid")}, {"delivered", false}};
        QString error;
        bool coreCalled = false;
        bool stop = false;
        ZuneHDArtistResult binding{};
        ZuneHDArtResult result{};
        ZuneHDArtStatus bindStatus = ZUNE_HD_ART_INVALID;
        Catalog catalog;
        if (conflicting) {
            error = QStringLiteral("Conflicting saved HD artwork for the same artist. Reopen Customize to choose one set.");
        } else if (runToken.isEmpty()) {
            error = QStringLiteral("HD artwork requires a current sync session.");
        } else if (zune_get_family(m_dev) != ZUNE_FAMILY_PAVO) {
            error = QStringLiteral("Artist backgrounds are available on Zune HD only.");
            report.insert("status", "unsupported");
        } else if (catalog.load(artist, manifest, error)) {
            ServeContext callbacks;
            callbacks.ready = [&](ZuneDevice *dev) {
                if (!current()) return -1;
                bindStatus = zune_hd_art_ensure_artist(dev, catalog.artist.constData(),
                                                       catalog.guid.constData(), &binding);
                if (bindStatus != ZUNE_HD_ART_OK) {
                    error = bindStatus == ZUNE_HD_ART_IDENTITY_CONFLICT
                        ? QStringLiteral("The existing artist cannot receive this HD set automatically. Its identity was preserved.")
                        : QString::fromUtf8(zune_get_error());
                    return binding.transport_uncertain ? -2 : -1;
                }
                if (!current()) return -1;
                coreCalled = true;
                const auto counts = forgeAlbumsCore(group, {{it.key(), binding.handle}});
                ok += counts.first; failed += counts.second;
                if (counts.second) {
                    error = QStringLiteral("Artist album linking did not complete.");
                    return zune_autopsy(dev) == 0x02ff ? -2 : -1;
                }
                return 0;
            };
            callbacks.progress = [&](const ZuneHDArtResult *r) {
                int acknowledged = 0;
                for (size_t i = 0; i < r->asset_count; ++i)
                    acknowledged += r->assets[i].acknowledged > 0;
                const int total = int(r->asset_count) + !catalog.biography.isEmpty();
                acknowledged += !catalog.biography.isEmpty() && r->biography_acknowledged > 0;
                emit hdArtworkProgress(serial, generation, runToken, artist, acknowledged, total);
            };
            emit hdArtworkProgress(serial, generation, runToken, artist, 0,
                                    int(catalog.config.gallery_count) + 2 + !catalog.biography.isEmpty());
            const auto status = zune_hd_art_serve(m_dev, &catalog.config, readyCallback,
                &callbacks, progressCallback, &callbacks, &result);
            stop = !result.safe_to_continue || binding.transport_uncertain
                || (binding.write_attempted && bindStatus != ZUNE_HD_ART_OK);
            QVariantList assets;
            for (size_t i = 0; i < result.asset_count; ++i)
                assets.append(QVariantMap{{"id", QString::fromUtf8(result.assets[i].guid)},
                    {"requests", result.assets[i].requests}, {"acknowledged", result.assets[i].acknowledged}});
            report.insert("assets", assets);
            report.insert("httpRequests", result.http_requests);
            report.insert("biographyRequests", result.biography_requests);
            report.insert("biographyAcknowledged", result.biography_acknowledged);
            report.insert("artistHandle", binding.handle);
            report.insert("artistCreated", bool(binding.created));
            report.insert("artistWriteAttempted", bool(binding.write_attempted));
            report.insert("transportUncertain", bool(result.transport_uncertain || binding.transport_uncertain));
            report.insert("sessionClosed", bool(result.network_closed));
            report.insert("reconnectRequired", stop);
            report.insert("statusCode", int(status));
            const bool delivered = status == ZUNE_HD_ART_OK && coreCalled
                && error.isEmpty() && !stop;
            report.insert("delivered", delivered);
            if (!delivered && error.isEmpty()) {
                if (status == ZUNE_HD_ART_PARTIAL)
                    error = QStringLiteral("The HD did not request the complete artist set. Some content may already be cached; delivery is incomplete.");
                else if (status == ZUNE_HD_ART_CANCELLED)
                    error = QStringLiteral("HD artist artwork was cancelled.");
                else
                    error = QStringLiteral("HD artist artwork did not complete: %1").arg(QString::fromUtf8(zune_get_error()));
            }
        }
        report.insert("message", error);
        emit hdArtworkDone(serial, generation, artist, report);
        // Optional art failure must not duplicate or recreate a possibly written
        // artist. Only an untouched, known usable session may take normal forge.
        if (!coreCalled && !stop && !binding.write_attempted && current()) {
            const auto counts = forgeAlbumsCore(group);
            ok += counts.first; failed += counts.second;
        } else if (!coreCalled) {
            failed += group.size();
        }
        if (stop) {
            failed += buckets.size() - ok - failed;
            doSever(); // Library skips wire cleanup after an uncertain tunnel.
            break;
        }
    }
    done(ok, failed);
}
