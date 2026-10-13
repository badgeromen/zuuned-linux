#pragma once

#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QUrl>
#include <QVector>

struct PodcastEpisode {
    // Scoped to a subscription. A GUID is opaque, never a title or a URL to normalize.
    QString identity, guid, title, author, description, mimeType;
    QUrl enclosureUrl, artworkUrl;
    qint64 enclosureBytes = -1, durationMs = -1;
    int episodeNumber = 0; // Positive feed-supplied number; zero means unknown.
    QDateTime published;
};

struct PodcastFeed {
    QUrl sourceUrl, artworkUrl;
    QString title, author, description;
    QVector<PodcastEpisode> episodes;
    int skippedItems = 0;
};

namespace Podcasts {
constexpr qsizetype MaxFeedBytes = 8 * 1024 * 1024;
constexpr int MaxEpisodes = 10000;
// HTTP fetching belongs to a worker service. Pass the final response URL for
// relative resolution, while retaining the subscription ID across redirects.
// RSS 2.0 only. Failure clears output; no partial feed may replace good data.
bool parseRss(const QByteArray &xml, const QUrl &responseUrl,
              PodcastFeed *out, QString *error);
// Query strings remain significant, including signed enclosure URLs.
QUrl mediaUrl(const QString &text, const QUrl &base);
}
