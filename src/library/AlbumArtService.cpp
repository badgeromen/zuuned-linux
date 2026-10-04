#include "AlbumArtService.h"

#include "libav_transcode.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTemporaryFile>

// Port of AlbumArtService.swift's extractArt/artCachePath. The cache
// key is byte-identical to the mac (and the C reference the mac cites):
//   MD5( lowercase("artist\nalbum") ) → <hex>.jpg
// Device art sync reads this same cache — do not change the recipe.

struct AlbumArtService::State {
    QMutex mutex;
    QSet<QString> inFlight;
    QHash<QString, QStringList> queued;
    QHash<QString, QSet<QString>> attempted;
};

namespace {

// The mac substitutes empties before building the cache path
// (extractArt: artist.isEmpty ? "Unknown Artist" : artist).
QString effectiveArtist(const QString &artist) {
    return artist.isEmpty() ? QStringLiteral("Unknown Artist") : artist;
}
QString effectiveAlbum(const QString &album) {
    return album.isEmpty() ? QStringLiteral("Unknown Album") : album;
}

bool isImageSuffix(const QString &suffix) {
    // Method-2 extension set from the Swift (jpg/jpeg/png/bmp/gif/webp)
    static const QSet<QString> kExts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp")
    };
    return kExts.contains(suffix);
}

// Write a source image into the cache: JPEGs already ≤400px are copied
// verbatim; everything else is decoded, downscaled to ≤400px, and
// encoded as JPEG q90. (The mac copies verbatim and downscales at load
// time; on disk we normalize instead — the KEY contract is unchanged.)
bool storeToCache(const QString &src, const QString &dst) {
    const QString suffix = QFileInfo(src).suffix().toLower();
    const bool jpeg = (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"));

    if (jpeg) {
        QImageReader probe(src);
        const QSize sz = probe.size(); // header read only, no decode
        if (sz.isValid() && sz.width() <= 400 && sz.height() <= 400) {
            QFile::remove(dst);
            return QFile::copy(src, dst);
        }
    }

    QImage img(src);
    if (img.isNull())
        return false;
    if (img.width() > 400 || img.height() > 400)
        img = img.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img.save(dst, "JPEG", 90);
}

// Resolution chain from the Swift: folder known-names → any folder
// image → embedded via libav. Returns true when cachePath exists after.
bool resolveArt(const QString &trackFilepath, const QString &cachePath) {
    const QFileInfo trackInfo(trackFilepath);
    const QDir folder = trackInfo.dir();

    if (folder.exists()) {
        const QFileInfoList entries =
            folder.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);

        // Method 1: known art basenames, in the mac's priority order,
        // matched case-insensitively (covers the Swift's lowercase +
        // Capitalized variants and then some).
        static const char *kKnown[] = {"cover", "folder", "artwork", "album", "front"};
        for (const char *known : kKnown) {
            for (const QFileInfo &fi : entries) {
                const QString base = fi.completeBaseName().toLower();
                const QString suffix = fi.suffix().toLower();
                if (base == QLatin1String(known)
                    && (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
                        || suffix == QLatin1String("png"))) {
                    if (storeToCache(fi.absoluteFilePath(), cachePath)) {
                        fprintf(stderr, "[art] found folder art: %s\n",
                                qPrintable(fi.fileName()));
                        return true;
                    }
                }
            }
        }

        // Method 2: any image file in the folder
        for (const QFileInfo &fi : entries) {
            if (isImageSuffix(fi.suffix().toLower())) {
                if (storeToCache(fi.absoluteFilePath(), cachePath)) {
                    fprintf(stderr, "[art] found image in folder: %s\n",
                            qPrintable(fi.fileName()));
                    return true;
                }
            }
        }
    }

    // Method 3: embedded art via in-process libav
    if (trackInfo.exists()
        && zuuned_extract_art(trackFilepath.toUtf8().constData(),
                              cachePath.toUtf8().constData(), 400) == 0
        && QFile::exists(cachePath)) {
        fprintf(stderr, "[art] extracted embedded art from: %s\n",
                qPrintable(trackInfo.fileName()));
        return true;
    }

    return false;
}

} // namespace

AlbumArtService::AlbumArtService(QObject *parent)
    : QObject(parent), m_state(std::make_shared<State>()) {
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                 + QStringLiteral("/art");
    QDir().mkpath(m_cacheDir);
}

QString AlbumArtService::cacheKey(const QString &artist, const QString &album) {
    // Swift: let key = "\(artist)\n\(album)".lowercased(); MD5 of UTF-8.
    const QString key = (artist + QLatin1Char('\n') + album).toLower();
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5);
    return QString::fromLatin1(digest.toHex());
}

QString AlbumArtService::cachePathFor(const QString &artist, const QString &album) const {
    return m_cacheDir + QLatin1Char('/')
           + cacheKey(effectiveArtist(artist), effectiveAlbum(album))
           + QStringLiteral(".jpg");
}

QString AlbumArtService::cachedArtPath(const QString &artist, const QString &album) const {
    const QString path = cachePathFor(artist, album);
    return QFile::exists(path) ? path : QString();
}

void AlbumArtService::requestArt(const QString &artist, const QString &album,
                                 const QString &trackFilepath) {
    requestSources(artist, album, {trackFilepath});
}

void AlbumArtService::requestSources(const QString &artist, const QString &album,
                                    const QStringList &trackFilepaths) {
    if (artist.isEmpty() && album.isEmpty())
        return;

    const QString path = cachePathFor(artist, album);
    if (QFile::exists(path))
        return; // contract: no-op when cached — caller uses cachedArtPath

    const QString key = cacheKey(effectiveArtist(artist), effectiveAlbum(album));
    const auto state = m_state;
    {
        QMutexLocker lock(&state->mutex);
        for (const QString &source : trackFilepaths) {
            if (!source.isEmpty() && !state->attempted[key].contains(source)
                && !state->queued[key].contains(source))
                state->queued[key].append(source);
        }
        if (state->inFlight.contains(key)) return;
        if (state->queued[key].isEmpty()) {
            // Publish the memoized miss too: the caller may explicitly retry
            // online after an earlier outage without rescanning local bytes.
            QMetaObject::invokeMethod(this, [this, artist, album] {
                emit artReady(artist, album, {}, false);
            }, Qt::QueuedConnection);
            return;
        }
        state->inFlight.insert(key);
    }

    QPointer<AlbumArtService> self(this);
    QThreadPool::globalInstance()->start([self, state, artist, album, path, key] {
        bool ok = false;
        for (;;) {
            QString source;
            {
                QMutexLocker lock(&state->mutex);
                if (ok || state->queued[key].isEmpty()) {
                    state->queued.remove(key);
                    state->inFlight.remove(key);
                    break;
                }
                source = state->queued[key].takeFirst();
                state->attempted[key].insert(source);
            }
            // A miss belongs to this source, not the album. A second folder
            // or another track may supply art that the first file lacked.
            QTemporaryFile staged(QFileInfo(path).dir().filePath(QStringLiteral("auto_XXXXXX.jpg")));
            const bool opened = staged.open();
            staged.close();
            if (!QFile::exists(path) && opened && resolveArt(source, staged.fileName()))
                QFile::copy(staged.fileName(), path); // never replace a custom choice
            ok = QFile::exists(path);
        }
        if (self)
            emit self->artReady(artist, album, ok ? path : QString(), ok);
    });
}

void AlbumArtService::importDeviceArt(const QString &artist,
                                      const QString &album,
                                      const QString &sourcePath) {
    if (album.trimmed().isEmpty() || !QFileInfo::exists(sourcePath))
        return;
    const QString path = cachePathFor(artist, album);
    if (QFile::exists(path)) {
        emit artReady(artist, album, path, true);
        return;
    }

    const QString key = cacheKey(effectiveArtist(artist), effectiveAlbum(album));
    {
        QMutexLocker lock(&m_state->mutex);
        m_state->attempted.remove(key);
    }
    QPointer<AlbumArtService> self(this);
    QThreadPool::globalInstance()->start(
        [self, state = m_state, artist, album, sourcePath, path, key] {
        QTemporaryFile staged(QFileInfo(path).dir().filePath(
            QStringLiteral("device_XXXXXX.jpg")));
        const bool opened = staged.open();
        staged.close();
        const bool prepared = opened
            && storeToCache(sourcePath, staged.fileName());
        if (prepared && !QFile::exists(path))
            QFile::copy(staged.fileName(), path); // never replace custom/local art
        const bool ok = QFile::exists(path);
        {
            QMutexLocker lock(&state->mutex);
            if (ok)
                state->attempted.remove(key);
        }
        if (self)
            emit self->artReady(artist, album, ok ? path : QString(), ok);
    });
}

bool AlbumArtService::isResolving(const QString &artist, const QString &album) const {
    QMutexLocker lock(&m_state->mutex);
    return m_state->inFlight.contains(cacheKey(effectiveArtist(artist), effectiveAlbum(album)));
}

void AlbumArtService::forgetFailure(const QString &artist, const QString &album) {
    QMutexLocker lock(&m_state->mutex);
    m_state->attempted.remove(cacheKey(effectiveArtist(artist), effectiveAlbum(album)));
}
