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
    // The document holding older episodes (RFC 5005 paged or archived feeds).
    QUrl nextPage;
    QString title, author, description;
    QVector<PodcastEpisode> episodes;
    int skippedItems = 0;
};

namespace Podcasts {
// Limits come from a survey of about 640 of the busiest public feeds
// (October 20, 2026). One document: the heaviest was 102.4 MiB (10,000 items),
// the longest 24,258 items. One show across its pages: 50,193 episodes in six
// documents, and 40,045 in 27. Each limit leaves about twice that room.
constexpr qsizetype MaxFeedBytes = 128 * 1024 * 1024; // decompressed, per document
constexpr int MaxEpisodes = 50000;                    // per document
constexpr int MaxShowEpisodes = 100000;               // per show, every page together
constexpr int MaxFeedPages = 1000;                    // documents read for one show
// HTTP fetching belongs to a worker service. Pass the final response URL for
// relative resolution, while retaining the subscription ID across redirects.
// RSS 2.0 only. Failure clears output; no partial feed may replace good data.
// A later item repeating an episode identity with different content is
// skipped (counted in skippedItems); the first, usually newest, stays.
bool parseRss(const QByteArray &xml, const QUrl &responseUrl,
              PodcastFeed *out, QString *error);
// Query strings remain significant, including signed enclosure URLs.
QUrl mediaUrl(const QString &text, const QUrl &base);
}
