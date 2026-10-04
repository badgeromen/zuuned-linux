#include "OnlineAlbumArtService.h"
#include "AlbumArtService.h"
#include "MusicIdentityClient.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QImageReader>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent>
#include <unistd.h>

namespace {
QByteArray normalizedCover(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > 32 * 1024 * 1024) return {};
    QBuffer source;
    source.setData(bytes);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 20 * 1000 * 1000) return {};
    if (size.width() > 400 || size.height() > 400)
        reader.setScaledSize(size.scaled(400, 400, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    QByteArray result;
    QBuffer target(&result);
    target.open(QIODevice::WriteOnly);
    if (image.isNull() || !image.save(&target, "JPEG", 90)) return {};
    return result;
}
bool install(const QByteArray &bytes, const QString &path, bool &published) {
    published = false;
    if (QFile::exists(path)) return true;
    if (bytes.isEmpty()) return false;
    QTemporaryFile staged(path + QStringLiteral(".XXXXXX"));
    if (!staged.open() || staged.write(bytes) != bytes.size() || !staged.flush()) return false;
    staged.close();
    // Publish complete bytes without replacing a local or custom cover.
    published = ::link(QFile::encodeName(staged.fileName()).constData(),
                       QFile::encodeName(path).constData()) == 0;
    return published || QFile::exists(path);
}
}

OnlineAlbumArtService::OnlineAlbumArtService(QObject *parent)
    : OnlineAlbumArtService(Resolve{}, QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                           + QStringLiteral("/art"), parent) {
    m_download = [](const QUrl &url) { return ArtworkHttp::getBody(url, false); };
    m_typedResolve = [](const QString &artist, const QString &album, const QVariantMap &context) {
        const auto result = MusicIdentityClient().resolveAlbumArtwork(artist, album, context);
        return QVariantMap{{"status", result.status}, {"reason", result.reason},
                           {"imageUrl", result.imageUrl}, {"identity", result.identity},
                           {"candidates", result.candidates}};
    };
}

OnlineAlbumArtService::OnlineAlbumArtService(Resolve resolve, QString cacheDirectory, QObject *parent)
    : QObject(parent), m_resolve(std::move(resolve)), m_cacheDir(std::move(cacheDirectory)) {
    QDir().mkpath(m_cacheDir);
}

OnlineAlbumArtService::OnlineAlbumArtService(TypedResults, TypedResolve resolve, Download download, QString cacheDirectory,
                                             QObject *parent, int retryDelayMs)
    : OnlineAlbumArtService(Resolve{}, std::move(cacheDirectory), parent) {
    m_typedResolve = std::move(resolve);
    m_download = std::move(download);
    m_retryDelayMs = qMax(1, retryDelayMs);
}

void OnlineAlbumArtService::invalidate(const QString &artist, const QString &album) {
    const QString key = AlbumArtService::cacheKey(artist, album);
    ++m_generation[key];
    m_pending.remove(key);
    m_failedUntil.remove(key);
    m_terminal.remove(key);
}

void OnlineAlbumArtService::request(const QString &artist, const QString &album, const QVariantMap &identity) {
    if (artist.trimmed().isEmpty() || album.trimmed().isEmpty()) return;
    const QString key = AlbumArtService::cacheKey(artist, album);
    if (m_identities.value(key) != identity) {
        invalidate(artist, album);
        m_identities.insert(key, identity);
    }
    const QString path = m_cacheDir + '/' + key + QStringLiteral(".jpg");
    if (QFile::exists(path)) { emit discoveryResult(artist, album, {{"status", "ready"}}); emit finished(artist, album, path, true); return; }
    if (m_terminal.contains(key) || m_pending.contains(key) || m_failedUntil.value(key) > QDateTime::currentMSecsSinceEpoch()) return;
    m_pending.insert(key, m_generation.value(key));
    m_queue.enqueue({artist, album, key, identity, m_generation.value(key)});
    emit discoveryResult(artist, album, {{"status", "lookingUp"}});
    pump();
}

void OnlineAlbumArtService::retryFailures() { m_failedUntil.clear(); m_terminal.clear(); }

void OnlineAlbumArtService::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    if (!paused) { pump(); return; }
    // Keep requests while preventing any in-flight result from publishing
    // during Customize. Requeue the active request behind a new generation.
    if (m_active && m_generation.value(m_active->key) == m_active->generation) {
        auto request = *m_active;
        invalidate(request.artist, request.album);
        request.generation = m_generation.value(request.key);
        m_pending.insert(request.key, request.generation);
        m_queue.prepend(request);
    }
}

void OnlineAlbumArtService::pump() {
    if (m_paused || m_busy) return;
    while (!m_queue.isEmpty()) {
        const Request request = m_queue.dequeue();
        if (m_generation.value(request.key) != request.generation) continue;
        m_busy = true;
        m_active = request;
        auto *watcher = new QFutureWatcher<QVariantMap>(this);
        connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, request] {
            auto result = watcher->result();
            const QByteArray bytes = result.take("_bytes").toByteArray();
            watcher->deleteLater();
            m_active.reset();
            m_busy = false;
            if (m_generation.value(request.key) == request.generation) {
                m_pending.remove(request.key);
                const QString path = m_cacheDir + '/' + request.key + QStringLiteral(".jpg");
                const bool existed = QFile::exists(path);
                bool published = false;
                const bool ok = existed || (result.value("status").toString() == "ready" && install(bytes, path, published));
                if (ok) {
                    result["status"] = "ready";
                    if (!published) result.remove("identity");
                } else {
                    if (result.value("status").toString() == "ready") {
                        result["status"] = "providerUnavailable";
                        result["reason"] = "Could not save artwork";
                    }
                    m_terminal.insert(request.key);
                    if (result.value("status").toString() == "providerUnavailable" && request.retries == 0) {
                        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + m_retryDelayMs;
                        m_failedUntil.insert(request.key, deadline);
                        QTimer::singleShot(m_retryDelayMs, Qt::PreciseTimer, this, [this, request, deadline] {
                            if (m_generation.value(request.key) != request.generation
                                || m_failedUntil.value(request.key) != deadline || m_pending.contains(request.key)) return;
                            m_terminal.remove(request.key);
                            m_failedUntil.remove(request.key);
                            auto retry = request;
                            ++retry.retries;
                            m_pending.insert(request.key, request.generation);
                            m_queue.enqueue(retry);
                            emit discoveryResult(request.artist, request.album, {{"status", "lookingUp"}});
                            pump();
                        });
                    }
                }
                emit discoveryResult(request.artist, request.album, result);
                fprintf(stderr, "[album-art] %s: %s / %s (%s)\n", qPrintable(result.value("status").toString()),
                        qPrintable(request.artist), qPrintable(request.album), qPrintable(result.value("reason").toString()));
                emit finished(request.artist, request.album, ok ? path : QString(), ok);
            }
            pump();
        });
        watcher->setFuture(QtConcurrent::run([request, resolve = m_resolve, typedResolve = m_typedResolve, download = m_download] {
            QVariantMap result;
            QByteArray bytes;
            if (typedResolve) {
                result = typedResolve(request.artist, request.album, request.identity);
                if (result.value("status").toString() == "ready") {
                    const QUrl url(result.value("imageUrl").toString());
                    if (download && url.isValid() && url.scheme() == "https" && !url.host().isEmpty())
                        bytes = download(url);
                }
            } else {
                result["status"] = "ready";
                if (resolve) bytes = resolve(request.artist, request.album, request.identity);
            }
            if (result.value("status").toString() == "ready") {
                bytes = normalizedCover(bytes);
                if (bytes.isEmpty()) {
                    result["status"] = "providerUnavailable";
                    result["reason"] = "Artwork download failed or returned an invalid image";
                } else result["_bytes"] = bytes;
            }
            return result;
        }));
        return;
    }
}
