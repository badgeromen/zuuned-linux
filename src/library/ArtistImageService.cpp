#include "ArtistImageService.h"
#include "MusicIdentityClient.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <cerrno>
#include <utility>
#include <unistd.h>

namespace {
QByteArray portraitJpeg(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > 32 * 1024 * 1024) return {};
    QBuffer input;
    input.setData(bytes);
    if (!input.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&input);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 20 * 1000 * 1000) return {};
    if (size.width() > 1200 || size.height() > 1200)
        reader.setScaledSize(size.scaled(1200, 1200, Qt::KeepAspectRatio));
    const QImage picture = reader.read();
    if (picture.isNull()) return {};
    QByteArray encoded;
    QBuffer output(&encoded);
    if (!output.open(QIODevice::WriteOnly) || !picture.save(&output, "JPEG", 90)) return {};
    return encoded;
}

// Install the complete image atomically without replacing an existing file.
// A QFile::NewOnly stream leaves a partially written cache entry visible; a
// same-directory temporary inode + POSIX link publishes all bytes at once.
bool installAutomaticPortrait(const QByteArray &bytes, const QString &destination, bool &published) {
    published = false;
    if (QFile::exists(destination)) return true;
    QTemporaryFile staged(destination + QStringLiteral(".XXXXXX"));
    if (!staged.open() || staged.write(bytes) != bytes.size() || !staged.flush()) return false;
    staged.close();
    if (::link(QFile::encodeName(staged.fileName()).constData(),
               QFile::encodeName(destination).constData()) == 0) { published = true; return true; }
    return errno == EEXIST && QFile::exists(destination);
}
} // namespace

ArtistImageService::ArtistImageService(QObject *parent)
    : ArtistImageService(Resolve{}, [](const QUrl &url) { return ArtworkHttp::getBody(url, false); },
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artistart"), parent) {
    m_typedResolve = [](const QString &name, const QVariantMap &context) {
        const auto result = MusicIdentityClient().resolveArtistArtwork(name, context);
        return QVariantMap{{"status", result.status}, {"reason", result.reason},
                           {"imageUrl", result.imageUrl}, {"identity", result.identity},
                           {"candidates", result.candidates}};
    };
}

ArtistImageService::ArtistImageService(Resolve resolve, Download download,
                                       QString cacheDirectory, QObject *parent, int retryDelayMs)
    : QObject(parent), m_resolve(std::move(resolve)), m_download(std::move(download)),
      m_cacheDir(std::move(cacheDirectory)), m_retryDelayMs(qMax(1, retryDelayMs)) {
    QDir().mkpath(m_cacheDir);
}

ArtistImageService::ArtistImageService(TypedResults, TypedResolve resolve, Download download,
                                       QString cacheDirectory, QObject *parent, int retryDelayMs)
    : ArtistImageService(Resolve{}, std::move(download), std::move(cacheDirectory), parent, retryDelayMs) {
    m_typedResolve = std::move(resolve);
}

QString ArtistImageService::normalizedName(const QString &raw) {
    return MusicIdentityClient::normalizedName(raw);
}

QString ArtistImageService::cacheKey(const QString &name) {
    return QString::fromLatin1(QCryptographicHash::hash(name.toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}

QString ArtistImageService::cachePathFor(const QString &name) const {
    return m_cacheDir + QLatin1Char('/') + cacheKey(name) + QStringLiteral(".jpg");
}

QString ArtistImageService::cachedPath(const QString &name) const {
    const QString path = cachePathFor(name);
    return QFile::exists(path) ? path : QString();
}

void ArtistImageService::invalidate(const QString &name) {
    const QString key = cacheKey(name.trimmed());
    ++m_generations[key];
    m_pending.remove(key);
    m_failedUntil.remove(key);
    m_terminal.remove(key);
}

void ArtistImageService::setCustomizationPaused(bool paused) {
    if (m_customizationPaused == paused) return;
    m_customizationPaused = paused;
    if (!paused) { pump(); return; }

    // Retain the latest valid request for each cache key, preserving its place
    // in the queue. The active worker may finish, but its old generation can
    // neither publish bytes nor remove one of these replacement requests.
    QQueue<Request> retained;
    QHash<QString, qsizetype> positions;
    auto retain = [&](const Request &request) {
        const QString key = cacheKey(request.name);
        if (m_generations.value(key) != request.generation) return;
        if (positions.contains(key)) retained[positions.value(key)] = request;
        else { positions.insert(key, retained.size()); retained.enqueue(request); }
    };
    if (m_active) retain(*m_active);
    for (const auto &request : std::as_const(m_queue)) retain(request);
    m_queue.clear();
    for (auto &request : retained) {
        invalidate(request.name);
        const QString key = cacheKey(request.name);
        request.generation = m_generations.value(key);
        m_pending.insert(key, request.generation);
        m_queue.enqueue(request);
    }
}

void ArtistImageService::requestImage(const QString &name, const QVariantMap &identity) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    const QString key = cacheKey(trimmed);
    const QByteArray identityKey = QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact);
    if (m_identities.value(key) != identityKey) {
        invalidate(trimmed);
        m_identities.insert(key, identityKey);
    }
    if (!cachedPath(trimmed).isEmpty()) {
        emit discoveryResult(trimmed, {{"status", "ready"}});
        emit imageReady(trimmed, cachePathFor(trimmed), true);
        return;
    }
    if (m_terminal.contains(key) || m_pending.contains(key) || m_failedUntil.value(key) > QDateTime::currentMSecsSinceEpoch()) return;
    if (trimmed.compare(QStringLiteral("Unknown Artist"), Qt::CaseInsensitive) == 0
        && identity.value("providerId").toString().isEmpty()
        && identity.value("mbid").toString().isEmpty()) {
        m_terminal.insert(key);
        emit discoveryResult(trimmed, {{"status", "needsMatch"}, {"reason", "Artist metadata is missing"}});
        emit imageReady(trimmed, {}, false);
        return;
    }
    m_failedUntil.remove(key);
    const quint64 generation = m_generations.value(key);
    m_pending.insert(key, generation);
    m_queue.enqueue({trimmed, identity, generation});
    emit discoveryResult(trimmed, {{"status", "lookingUp"}});
    pump();
}

void ArtistImageService::pump() {
    if (m_busy || m_customizationPaused) return;
    while (!m_queue.isEmpty()) {
        const Request request = m_queue.dequeue();
        if (m_generations.value(cacheKey(request.name)) != request.generation) continue;
        m_busy = true;
        fetch(request);
        return;
    }
}

void ArtistImageService::fetch(const Request &request) {
    m_active = request;
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, request] {
        auto result = watcher->result();
        const QByteArray bytes = result.take("_bytes").toByteArray();
        watcher->deleteLater();
        m_active.reset();
        const QString key = cacheKey(request.name);
        if (m_generations.value(key) == request.generation) {
            m_pending.remove(key);
            const QString path = cachePathFor(request.name);
            const bool existed = QFile::exists(path);
            bool published = false;
            const bool ok = existed || (result.value("status").toString() == "ready"
                && !bytes.isEmpty() && installAutomaticPortrait(bytes, path, published));
            if (ok) {
                result["status"] = "ready";
                if (!published) result.remove("identity");
                m_failedUntil.remove(key);
            } else {
                if (result.value("status").toString() == "ready") {
                    result["status"] = "providerUnavailable";
                    result["reason"] = "Could not save artwork";
                }
                m_terminal.insert(key);
                const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + m_retryDelayMs;
                m_failedUntil.insert(key, deadline);
                // One delayed retry also covers a provider/download outage while
                // the card stays on screen. No endless retry for absent portraits.
                if (result.value("status").toString() == "providerUnavailable" && request.retries == 0) QTimer::singleShot(m_retryDelayMs, Qt::PreciseTimer, this,
                    [this, request, key, deadline] {
                    if (m_generations.value(key) != request.generation
                        || m_failedUntil.value(key) != deadline || m_pending.contains(key)) return;
                    m_failedUntil.remove(key);
                    m_terminal.remove(key);
                    m_pending.insert(key, request.generation);
                    Request retry = request;
                    ++retry.retries;
                    m_queue.enqueue(retry);
                    emit discoveryResult(request.name, {{"status", "lookingUp"}});
                    pump();
                });
            }
            emit discoveryResult(request.name, result);
            emit imageReady(request.name, ok ? path : QString(), ok);
        }
        QTimer::singleShot(300, this, [this] { m_busy = false; pump(); });
    });
    // No worker captures this. Destruction can release the watcher safely;
    // only its UI-thread completion slot can publish a cache file or signal.
    watcher->setFuture(QtConcurrent::run([request, resolve = m_resolve, typedResolve = m_typedResolve,
                                          download = m_download] {
        QVariantMap result;
        if (typedResolve) result = typedResolve(request.name, request.identity);
        else {
            result["status"] = "ready";
            result["imageUrl"] = resolve ? resolve(request.name, request.identity) : QString();
        }
        if (result.value("status").toString() == "ready") {
            const QUrl url(result.value("imageUrl").toString());
            QByteArray bytes;
            if (download && url.isValid() && url.scheme() == "https" && !url.host().isEmpty())
                bytes = portraitJpeg(download(url));
            if (bytes.isEmpty()) {
                result["status"] = "providerUnavailable";
                result["reason"] = "Portrait download failed or returned an invalid image";
            } else result["_bytes"] = bytes;
        }
        return result;
    }));
}

void ArtistImageService::forgetFailure(const QString &name) {
    m_failedUntil.remove(cacheKey(name.trimmed()));
    m_terminal.remove(cacheKey(name.trimmed()));
}
