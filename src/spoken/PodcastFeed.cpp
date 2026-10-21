#include "PodcastFeed.h"

#include <QHash>
#include <QLocale>
#include <QXmlStreamReader>

namespace {
constexpr auto Itunes = "http://www.itunes.com/dtds/podcast-1.0.dtd";
constexpr auto XmlNs = "http://www.w3.org/XML/1998/namespace";
constexpr auto Content = "http://purl.org/rss/1.0/modules/content/";
constexpr auto Dublin = "http://purl.org/dc/elements/1.1/";
constexpr auto Atom = "http://www.w3.org/2005/Atom";

QUrl elementBase(const QXmlStreamReader &r, const QUrl &parent)
{
    const auto value = r.attributes().value(QLatin1String(XmlNs), u"base");
    return value.isEmpty() ? parent : Podcasts::mediaUrl(value.toString(), parent);
}

qint64 duration(const QString &text)
{
    const auto parts = text.trimmed().split(u':');
    if (parts.isEmpty() || parts.size() > 3) return -1;
    qint64 seconds = 0;
    for (int i = 0; i < parts.size(); ++i) {
        if (parts[i].isEmpty()) return -1;
        for (const auto c : parts[i]) if (c < u'0' || c > u'9') return -1;
        bool ok = false;
        const qint64 n = parts[i].toLongLong(&ok);
        if (!ok || n > 315360000 || (i > 0 && n >= 60)) return -1;
        seconds = seconds * 60 + n;
        if (seconds > 315360000) return -1;
    }
    return seconds * 1000;
}

QDateTime publication(QString value)
{
    // RSS specifies RFC 822, whose named zones are not all accepted by Qt's
    // numeric-offset RFC2822 parser. Interpret only the RFC-defined names.
    static const QHash<QString, QString> zones = {
        {"UT", "+0000"}, {"GMT", "+0000"}, {"Z", "+0000"},
        {"EST", "-0500"}, {"EDT", "-0400"}, {"CST", "-0600"}, {"CDT", "-0500"},
        {"MST", "-0700"}, {"MDT", "-0600"}, {"PST", "-0800"}, {"PDT", "-0700"}
    };
    value = value.simplified();
    const auto split = value.lastIndexOf(u' ');
    const auto zone = zones.constFind(value.mid(split + 1));
    if (split >= 0 && zone != zones.cend()) value = value.left(split + 1) + *zone;
    return QDateTime::fromString(value, Qt::RFC2822Date).toUTC();
}

QString text(QXmlStreamReader &r)
{
    // Rich descriptions remain inert text here. UI consumers must not enable
    // remote HTML resources or execute feed markup.
    auto value = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
    if (value.size() > 65536) r.raiseError(QStringLiteral("Feed text exceeds 64 KiB"));
    return value;
}

void readEpisode(QXmlStreamReader &r, const QUrl &parent, PodcastEpisode &e)
{
    const QUrl base = elementBase(r, parent);
    while (r.readNextStartElement()) {
        const auto ns = r.namespaceUri();
        const auto name = r.name();
        const QUrl localBase = elementBase(r, base);
        if (ns.isEmpty() && name == u"title") e.title = text(r);
        else if (ns.isEmpty() && name == u"guid") e.guid = text(r);
        else if ((ns.isEmpty() && name == u"author") ||
                 (ns == QLatin1String(Itunes) && name == u"author") ||
                 (ns == QLatin1String(Dublin) && name == u"creator")) e.author = text(r);
        else if ((ns.isEmpty() && name == u"description") ||
                 (ns == QLatin1String(Content) && name == u"encoded")) {
            const bool fullDescription = ns == QLatin1String(Content);
            const auto value = text(r);
            if (e.description.isEmpty() || fullDescription) e.description = value;
        } else if (ns.isEmpty() && name == u"pubDate") {
            e.published = publication(text(r));
        } else if (ns == QLatin1String(Itunes) && name == u"duration") {
            e.durationMs = duration(text(r));
        } else if (ns == QLatin1String(Itunes) && name == u"episode") {
            const auto value = text(r);
            bool ok = false;
            const int number = value.toInt(&ok);
            for (const auto c : value) if (c < u'0' || c > u'9') ok = false;
            e.episodeNumber = ok && number > 0 ? number : 0;
        } else if (ns == QLatin1String(Itunes) && name == u"image") {
            e.artworkUrl = Podcasts::mediaUrl(r.attributes().value(u"href").toString(), localBase);
            r.skipCurrentElement();
        } else if (ns.isEmpty() && name == u"enclosure") {
            const auto url = Podcasts::mediaUrl(r.attributes().value(u"url").toString(), localBase);
            const auto mime = r.attributes().value(u"type").toString().trimmed().toLower();
            if (url.isValid() && (mime.startsWith(u"audio/") || mime.startsWith(u"video/") || mime == u"application/ogg")) {
                if (e.enclosureUrl.isValid() && e.enclosureUrl != url) {
                    r.raiseError(QStringLiteral("Episode has multiple different playable enclosures"));
                    return;
                }
                e.enclosureUrl = url;
                e.mimeType = mime;
                bool ok = false;
                const qint64 bytes = r.attributes().value(u"length").toLongLong(&ok);
                e.enclosureBytes = ok && bytes >= 0 ? bytes : -1;
            }
            r.skipCurrentElement();
        } else r.skipCurrentElement();
    }
    if (e.guid.isEmpty()) e.identity = QStringLiteral("url:") + e.enclosureUrl.toString(QUrl::FullyEncoded);
    else e.identity = QStringLiteral("guid:") + e.guid;
}

void readChannel(QXmlStreamReader &r, const QUrl &parent, PodcastFeed &feed)
{
    const auto base = elementBase(r, parent);
    QHash<QString, int> seen;
    int items = 0;
    QUrl archive;
    while (r.readNextStartElement()) {
        const auto ns = r.namespaceUri();
        const auto name = r.name();
        const QUrl localBase = elementBase(r, base);
        if (ns.isEmpty() && name == u"title") feed.title = text(r);
        else if (ns.isEmpty() && name == u"description") feed.description = text(r);
        else if (ns == QLatin1String(Itunes) && name == u"author") feed.author = text(r);
        else if (ns == QLatin1String(Itunes) && name == u"image") {
            feed.artworkUrl = Podcasts::mediaUrl(r.attributes().value(u"href").toString(), localBase);
            r.skipCurrentElement();
        } else if (ns == QLatin1String(Atom) && name == u"link") {
            // RFC 5005: a paged feed links its next (older) page, an archived
            // feed its previous archive. Both continue the back catalogue.
            const auto rel = r.attributes().value(u"rel").trimmed().toString().toLower();
            const auto href = Podcasts::mediaUrl(r.attributes().value(u"href").toString(), localBase);
            if (rel == u"next" && feed.nextPage.isEmpty()) feed.nextPage = href;
            else if (rel == u"prev-archive" && archive.isEmpty()) archive = href;
            r.skipCurrentElement();
        } else if (ns.isEmpty() && name == u"image") {
            while (r.readNextStartElement()) {
                if (r.namespaceUri().isEmpty() && r.name() == u"url") {
                    const auto imageBase = elementBase(r, localBase);
                    const auto url = Podcasts::mediaUrl(text(r), imageBase);
                    if (feed.artworkUrl.isEmpty()) feed.artworkUrl = url;
                } else r.skipCurrentElement();
            }
        } else if (ns.isEmpty() && name == u"item") {
            if (++items > Podcasts::MaxEpisodes) {
                r.raiseError(QStringLiteral("Feed lists more than %1 episodes in one document")
                                 .arg(QLocale(QLocale::English).toString(Podcasts::MaxEpisodes)));
                return;
            }
            PodcastEpisode e;
            readEpisode(r, base, e);
            if (r.hasError()) return;
            if (e.title.isEmpty() || !e.enclosureUrl.isValid()) { ++feed.skippedItems; continue; }
            const auto existing = seen.constFind(e.identity);
            if (existing != seen.cend()) {
                // Publishers do reuse a GUID by mistake (seen in a 9,392-episode
                // feed). Keep the first item rather than refusing the feed.
                const auto &previous = feed.episodes[*existing];
                if (previous.title != e.title || previous.enclosureUrl != e.enclosureUrl || previous.mimeType != e.mimeType)
                    ++feed.skippedItems;
                continue;
            }
            seen.insert(e.identity, feed.episodes.size());
            feed.episodes.append(e);
        } else r.skipCurrentElement();
    }
    if (feed.nextPage.isEmpty()) feed.nextPage = archive;
    for (auto &e : feed.episodes) {
        if (e.author.isEmpty()) e.author = feed.author;
        if (e.artworkUrl.isEmpty()) e.artworkUrl = feed.artworkUrl;
    }
}
}

QUrl Podcasts::mediaUrl(const QString &text, const QUrl &base)
{
    if (text.trimmed().isEmpty()) return {};
    const QUrl candidate(text.trimmed(), QUrl::StrictMode);
    if (!candidate.isValid()) return {};
    auto url = base.resolved(candidate);
    if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http") ||
        url.host().isEmpty() || !url.userInfo().isEmpty()) return {};
    url.setFragment(QString());
    return url;
}

bool Podcasts::parseRss(const QByteArray &bytes, const QUrl &url, PodcastFeed *out, QString *error)
{
    if (out) *out = {};
    if (error) error->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!out || bytes.isEmpty() || bytes.size() > MaxFeedBytes)
        return fail(QStringLiteral("Feed is empty or larger than %1 MiB").arg(MaxFeedBytes / (1024 * 1024)));
    const auto source = mediaUrl(url.toString(QUrl::FullyEncoded), {});
    if (!source.isValid()) return fail(QStringLiteral("Feed requires an HTTP or HTTPS response URL"));
    // Validate every token, even unknown extensions, before skipCurrentElement.
    // Qt does not fetch external entities; rejecting DTDs also prevents internal
    // entity expansion and gives a deterministic bounded parsing contract.
    QXmlStreamReader validation(bytes);
    int depth = 0;
    while (!validation.atEnd()) {
        const auto token = validation.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            return fail(QStringLiteral("DTD and entity declarations are not supported"));
        if (token == QXmlStreamReader::StartElement && ++depth > 32)
            return fail(QStringLiteral("Feed nesting exceeds limit"));
        if (token == QXmlStreamReader::EndElement) --depth;
    }
    if (validation.hasError()) return fail(validation.errorString());
    QXmlStreamReader r(bytes);
    if (!r.readNextStartElement() || !r.namespaceUri().isEmpty() || r.name() != u"rss" ||
        r.attributes().value(u"version") != u"2.0") return fail(QStringLiteral("Expected an RSS 2.0 feed"));
    const QUrl base = elementBase(r, source);
    PodcastFeed feed;
    feed.sourceUrl = source;
    bool channel = false;
    while (r.readNextStartElement()) {
        if (r.namespaceUri().isEmpty() && r.name() == u"channel") {
            if (channel) return fail(QStringLiteral("Feed contains multiple channels"));
            channel = true;
            readChannel(r, base, feed);
        } else r.skipCurrentElement();
    }
    if (r.hasError()) return fail(r.errorString());
    if (!channel || feed.title.isEmpty()) return fail(QStringLiteral("Feed is missing its channel title"));
    *out = std::move(feed);
    return true;
}
