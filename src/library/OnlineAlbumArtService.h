#pragma once

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QVariantMap>
#include <QUrl>
#include <functional>
#include <optional>

// UI-thread queue for missing album covers. Local discovery runs first.
// Downloads/decode run on workers; only a current UI-thread completion can
// atomically publish a cover. Never overwrites an existing local/custom file.
class OnlineAlbumArtService : public QObject {
    Q_OBJECT
public:
    using TypedResolve = std::function<QVariantMap(const QString &, const QString &, const QVariantMap &)>;
    using Download = std::function<QByteArray(const QUrl &)>;
    enum class TypedResults { Enabled };
    using Resolve = std::function<QByteArray(const QString &, const QString &, const QVariantMap &)>;
    explicit OnlineAlbumArtService(QObject *parent = nullptr);
    OnlineAlbumArtService(Resolve resolve, QString cacheDirectory, QObject *parent = nullptr);
    OnlineAlbumArtService(TypedResults, TypedResolve resolve, Download download, QString cacheDirectory,
                          QObject *parent = nullptr, int retryDelayMs = 5 * 60 * 1000);
    void request(const QString &artist, const QString &album, const QVariantMap &identity = {});
    void invalidate(const QString &artist, const QString &album);
    void setPaused(bool paused);
    void retryFailures();

signals:
    void discoveryResult(const QString &artist, const QString &album, const QVariantMap &result);
    void finished(const QString &artist, const QString &album, const QString &path, bool ok);

private:
    struct Request { QString artist, album, key; QVariantMap identity; quint64 generation; int retries = 0; };
    void pump();
    TypedResolve m_typedResolve;
    Download m_download;
    QSet<QString> m_terminal;
    Resolve m_resolve;
    QString m_cacheDir;
    QQueue<Request> m_queue;
    QHash<QString, quint64> m_generation;
    QHash<QString, quint64> m_pending;
    QHash<QString, qint64> m_failedUntil;
    QHash<QString, QVariantMap> m_identities;
    std::optional<Request> m_active;
    bool m_paused = false;
    bool m_busy = false;
    int m_retryDelayMs = 5 * 60 * 1000;
};
