#pragma once

#include "ArtworkHttp.h"
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// Synchronous provider client: construct/call on a worker, never the UI thread.
// Default transport shares ArtworkHttp's process-wide MusicBrainz throttle.
// Provider JSON is cached on disk (24 hours positive, 5 minutes empty/missing).
class MusicIdentityClient {
public:
    using Fetch = std::function<ArtworkHttp::Response(const QUrl &, bool musicBrainz)>;
    explicit MusicIdentityClient(Fetch fetch = {}, QString cacheDirectory = {});
    struct MatchResult {
        QString status = QStringLiteral("needsMatch");
        QString reason;
        QString imageUrl;
        QVariantMap identity;
        QVariantList candidates;
    };
    MatchResult resolveArtistArtwork(const QString &name, const QVariantMap &context = {}) const;
    MatchResult resolveAlbumArtwork(const QString &artist, const QString &album,
                                   const QVariantMap &context = {}) const;

    QVariantList searchAlbums(const QString &query, const QString &artistHint = {},
                              QString *error = nullptr, const QString &artistMbid = {}) const;
    QVariantList searchArtists(const QString &query,
                               const QString &provider = QStringLiteral("musicbrainz"),
                               QString *error = nullptr) const;
    QVariantMap albumDetails(const QString &mbid, QString *error = nullptr) const;
    QVariantMap artistDetails(const QString &provider, const QString &id,
                              QString *error = nullptr) const;
    QStringList fanartArtistUrls(const QString &mbid, QString *error = nullptr) const;
    // Explicit browsing can include multiple same-name artists. An empty or
    // failed sibling must not turn available portraits into an error state.
    QStringList searchFanartArtistUrls(const QString &name, QString *error = nullptr) const;
    QStringList fanartAlbumUrls(const QString &releaseGroupMbid, QString *error = nullptr) const;
    QStringList caaAlbumUrls(const QString &releaseGroupMbid, QString *error = nullptr) const;
    QStringList caaReleaseUrls(const QString &releaseMbid, QString *error = nullptr) const;

    // Explicit identity is authoritative. Automatic name lookups require one
    // exact normalized match; detected ambiguity never selects the first row.
    QString automaticArtistImageUrl(const QString &name,
                                     const QVariantMap &selectedIdentity = {},
                                     QString *error = nullptr) const;
    QString automaticAlbumImageUrl(const QString &artist, const QString &album,
                                   const QVariantMap &selectedIdentity = {},
                                   QString *error = nullptr) const;
    static QString normalizedName(const QString &name);
    static bool validMbid(const QString &id);

private:
    QJsonObject json(const QUrl &url, const QString &provider,
                     const std::function<bool(const QJsonObject &)> &valid,
                     const std::function<bool(const QJsonObject &)> &empty,
                     QString *error, bool missingIsEmpty = false) const;
    QUrl fanartUrl(const QString &path) const;
    Fetch m_fetch;
    QString m_cacheDirectory;
};
