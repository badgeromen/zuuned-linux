#pragma once

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include <optional>

// UI-thread owner; provider resolution, downloads, and image decoding run on
// workers. MusicBrainz identity -> Fanart portraits, with safe Deezer fallback.
// Cache key stays byte-identical to the Mac cache. Late automatic results
// cannot overwrite a custom file or survive an explicit invalidation.
class ArtistImageService : public QObject {
    Q_OBJECT

public:
    using TypedResolve = std::function<QVariantMap(const QString &, const QVariantMap &)>;
    enum class TypedResults { Enabled };
    explicit ArtistImageService(QObject *parent = nullptr);
    using Resolve = std::function<QString(const QString &, const QVariantMap &)>;
    using Download = std::function<QByteArray(const QUrl &)>;
    ArtistImageService(Resolve resolve, Download download, QString cacheDirectory,
                        QObject *parent = nullptr, int retryDelayMs = 5 * 60 * 1000);

    ArtistImageService(TypedResults, TypedResolve resolve, Download download, QString cacheDirectory,
                       QObject *parent = nullptr, int retryDelayMs = 5 * 60 * 1000);

    static QString cacheKey(const QString &name);   // md5 hex of lowercase(name)
    static QString normalizedName(const QString &raw); // match-tolerant form
    QString cachePathFor(const QString &name) const;
    QString cachedPath(const QString &name) const;  // empty if absent

    void requestImage(const QString &name, const QVariantMap &identity = {});
    void forgetFailure(const QString &name);
    // Call on the UI thread BEFORE a custom-art/identity Apply or reset starts
    // its worker. This cancels pending automatic results without deleting any
    // cached artwork. Request again after the authoritative save completes.
    void invalidate(const QString &name);
    // Suspend publication during an artist Apply whose authoritative new name
    // is not known yet. Active/queued work is retained under fresh generations.
    // Before resuming, invalidate the final name and request its saved identity.
    // Resume on failure too so unrelated portraits are never stranded.
    void setCustomizationPaused(bool paused);

signals:
    void discoveryResult(const QString &name, const QVariantMap &result);
    void imageReady(const QString &name, const QString &path, bool ok);

private:
    void pump();
    struct Request { QString name; QVariantMap identity; quint64 generation; int retries = 0; };
    void fetch(const Request &request);

    TypedResolve m_typedResolve;
    QSet<QString> m_terminal;
    Resolve m_resolve;
    Download m_download;
    QString m_cacheDir;
    QQueue<Request> m_queue;
    QHash<QString, quint64> m_pending;
    QHash<QString, quint64> m_generations;
    QHash<QString, QByteArray> m_identities;
    QHash<QString, qint64> m_failedUntil;
    std::optional<Request> m_active;
    bool m_customizationPaused = false;
    bool m_busy = false;
    int m_retryDelayMs;
};
