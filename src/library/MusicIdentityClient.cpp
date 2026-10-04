#include "MusicIdentityClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSharedPointer>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QWaitCondition>
#include <algorithm>
#include <chrono>

namespace {
// Same free developer key already shipped by TmdbClient's Fanart integration.
constexpr auto kBundledFanartKey = "b5177dee86dfad2bc891dbe74bb253bf";
constexpr qint64 kPositiveTtl = 24 * 60 * 60 * 1000;
constexpr qint64 kNegativeTtl = 5 * 60 * 1000;
constexpr qint64 kStaleGraceTtl = 6 * 24 * 60 * 60 * 1000LL;
constexpr qint64 kFailureCooldown = 30 * 1000;
constexpr qint64 kMaxJsonBytes = 8 * 1024 * 1024;
QMutex cacheMutex;

struct JsonRequest {
    QWaitCondition completed;
    bool running = true;
    QJsonObject body;
    QString error;
};
struct JsonFailure {
    std::chrono::steady_clock::time_point until;
    QString error;
};
// Keys include the full cache path, not just the URL: injected transports
// and independently configured caches must never share their responses.
QHash<QString, QSharedPointer<JsonRequest>> jsonRequests;
QHash<QString, JsonFailure> jsonFailures;

bool temporaryResponse(const ArtworkHttp::Response &response) {
    switch (response.status) {
    case 429: case 500: case 502: case 503: case 504: return true;
    default:
        if (response.status != 0 && (response.status < 200 || response.status >= 300)) return false;
    }
    switch (response.networkError) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: // the transport's timeout abort
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
        return true;
    default: return false; // TLS/security, authentication and malformed data
    }
}

qint64 failureCooldown(const QByteArray &retryAfter) {
    const QString value = QString::fromLatin1(retryAfter.trimmed());
    bool secondsValid = false;
    const qint64 seconds = value.toLongLong(&secondsValid);
    qint64 delay = 0;
    if (secondsValid && seconds >= 0) delay = qMin(seconds, qint64(86400)) * 1000;
    else {
        QString dateText = value;
        if (dateText.endsWith(QLatin1String(" GMT"), Qt::CaseInsensitive)) {
            dateText.chop(4); dateText += QStringLiteral(" +0000");
        }
        const auto date = QDateTime::fromString(dateText, Qt::RFC2822Date);
        if (date.isValid()) delay = qBound(qint64(0), QDateTime::currentDateTimeUtc().msecsTo(date), qint64(86400000));
    }
    return qMax(kFailureCooldown, delay);
}

QString quoted(QString text) {
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

QString imageUrl(const QString &value) {
    QUrl url(value);
    // Older provider records still use HTTP links for the same HTTPS CDNs.
    const QString host = url.host().toLower();
    if (url.scheme() == QLatin1String("http")
            && (host == QLatin1String("coverartarchive.org") || host.endsWith(QLatin1String(".archive.org"))
                || host == QLatin1String("archive.org") || host.endsWith(QLatin1String(".fanart.tv"))
                || host.endsWith(QLatin1String(".dzcdn.net")) || host.endsWith(QLatin1String(".deezer.com"))))
        url.setScheme(QStringLiteral("https"));
    return url.isValid() && url.scheme() == QLatin1String("https")
        && !url.host().isEmpty() && url.userInfo().isEmpty()
        && !value.contains(QLatin1String("/artist//")) ? url.toString(QUrl::FullyEncoded) : QString();
}

QString deezerId(const QJsonValue &value) {
    const QString id = value.isString() ? value.toString()
        : value.isDouble() ? QString::number(value.toInteger()) : QString();
    static const QRegularExpression digits(QStringLiteral("^[1-9][0-9]{0,17}$"));
    return digits.match(id).hasMatch() ? id : QString();
}

QString joinNonempty(const QStringList &values) {
    QStringList out;
    for (const QString &value : values) if (!value.isEmpty()) out.append(value);
    return out.join(QStringLiteral(" · "));
}

QString genres(const QJsonObject &object) {
    QVector<QPair<int, QString>> ranked;
    for (const auto &value : object.value(QStringLiteral("genres")).toArray()) {
        const auto genre = value.toObject();
        const QString name = genre.value(QStringLiteral("name")).toString();
        const int count = genre.value(QStringLiteral("count")).toInt();
        if (!name.isEmpty() && count > 0) ranked.append({count, name});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList result;
    for (const auto &entry : ranked) {
        if (!result.contains(entry.second)) result.append(entry.second);
        if (result.size() == 3) break;
    }
    return result.join(QStringLiteral(", "));
}

QVariantMap artistMap(const QJsonObject &object, const QString &provider) {
    const bool mb = provider == QLatin1String("musicbrainz");
    const QString id = mb ? object.value(QStringLiteral("id")).toString().toLower()
                          : deezerId(object.value(QStringLiteral("id")));
    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty() || (mb ? !MusicIdentityClient::validMbid(id) : id.isEmpty())) return {};
    const QString disambiguation = object.value(QStringLiteral("disambiguation")).toString();
    const QString country = object.value(QStringLiteral("country")).toString();
    const QString type = object.value(QStringLiteral("type")).toString();
    const QString begin = object.value(QStringLiteral("life-span")).toObject().value(QStringLiteral("begin")).toString();
    QVariantMap out{{QStringLiteral("provider"), provider}, {QStringLiteral("providerId"), id},
        {QStringLiteral("mbid"), mb ? id : QString()}, {QStringLiteral("kind"), QStringLiteral("artist")},
        {QStringLiteral("name"), name}, {QStringLiteral("title"), name}, {QStringLiteral("artist"), name},
        {QStringLiteral("disambiguation"), disambiguation}, {QStringLiteral("country"), country},
        {QStringLiteral("type"), type}, {QStringLiteral("year"), begin.left(4)},
        {QStringLiteral("genre"), genres(object)},
        {QStringLiteral("overview"), object.value(QStringLiteral("annotation")).toString()}};
    QStringList aliases;
    for (const auto &alias : object.value(QStringLiteral("aliases")).toArray())
        aliases.append(alias.toObject().value(QStringLiteral("name")).toString());
    out.insert(QStringLiteral("aliases"), aliases);
    QString subtitle = joinNonempty({disambiguation, type, country, begin});
    if (!mb) {
        QString picture = imageUrl(object.value(QStringLiteral("picture_xl")).toString());
        if (picture.isEmpty()) picture = imageUrl(object.value(QStringLiteral("picture_big")).toString());
        out.insert(QStringLiteral("artworkUrl"), picture);
        out.insert(QStringLiteral("portraitUrl"), picture);
        out.insert(QStringLiteral("posterUrl"), picture);
        subtitle = QStringLiteral("Deezer");
        if (object.contains(QStringLiteral("nb_album")))
            subtitle += QStringLiteral(" · %1 albums").arg(object.value(QStringLiteral("nb_album")).toInt());
    } else {
        for (const auto &value : object.value(QStringLiteral("relations")).toArray()) {
            const QUrl relation(value.toObject().value(QStringLiteral("url")).toObject()
                                    .value(QStringLiteral("resource")).toString());
            if (relation.host() != QLatin1String("www.deezer.com") && relation.host() != QLatin1String("deezer.com")) continue;
            static const QRegularExpression artistPath(QStringLiteral("^/(?:[a-z]{2}/)?artist/([1-9][0-9]*)/?$"));
            const auto match = artistPath.match(relation.path());
            if (match.hasMatch()) out.insert(QStringLiteral("deezerId"), match.captured(1));
        }
    }
    out.insert(QStringLiteral("subtitle"), subtitle);
    out.insert(QStringLiteral("sub"), subtitle);
    return out;
}

QVariantMap albumMap(const QJsonObject &object) {
    const QString id = object.value(QStringLiteral("id")).toString().toLower();
    const QString title = object.value(QStringLiteral("title")).toString().trimmed();
    if (!MusicIdentityClient::validMbid(id) || title.isEmpty()) return {};
    QString artist;
    QStringList artistIds;
    QStringList creditAliases;
    const auto credits = object.value(QStringLiteral("artist-credit")).toArray();
    for (const auto &value : object.value(QStringLiteral("artist-credit")).toArray()) {
        const auto credit = value.toObject();
        const auto creditedArtist = credit.value(QStringLiteral("artist")).toObject();
        const QString creditedName = credit.value(QStringLiteral("name")).toString();
        artist += (creditedName.isEmpty() ? creditedArtist.value(QStringLiteral("name")).toString() : creditedName)
                    + credit.value(QStringLiteral("joinphrase")).toString();
        const QString artistId = creditedArtist.value(QStringLiteral("id")).toString().toLower();
        if (MusicIdentityClient::validMbid(artistId) && !artistIds.contains(artistId)) artistIds.append(artistId);
        if (credits.size() == 1) {
            creditAliases.append(creditedArtist.value(QStringLiteral("name")).toString());
            for (const auto &alias : creditedArtist.value(QStringLiteral("aliases")).toArray())
                creditAliases.append(alias.toObject().value(QStringLiteral("name")).toString());
        }
    }
    const QString year = object.value(QStringLiteral("first-release-date")).toString().left(4);
    const QString type = object.value(QStringLiteral("primary-type")).toString();
    const QString disambiguation = object.value(QStringLiteral("disambiguation")).toString();
    const QString subtitle = joinNonempty({artist, year, type, disambiguation});
    return {{QStringLiteral("provider"), QStringLiteral("musicbrainz")}, {QStringLiteral("providerId"), id},
        {QStringLiteral("mbid"), id}, {QStringLiteral("kind"), QStringLiteral("album")},
        {QStringLiteral("title"), title}, {QStringLiteral("name"), title}, {QStringLiteral("album"), title},
        {QStringLiteral("artist"), artist}, {QStringLiteral("albumartist"), artist},
        {QStringLiteral("artistMbid"), artistIds.size() == 1 ? artistIds.first() : QString()},
        {QStringLiteral("artistMbids"), artistIds}, {QStringLiteral("year"), year},
        {QStringLiteral("artistAliases"), creditAliases}, {QStringLiteral("creditCount"), credits.size()},
        {QStringLiteral("type"), type}, {QStringLiteral("disambiguation"), disambiguation},
        {QStringLiteral("genre"), genres(object)}, {QStringLiteral("overview"), object.value(QStringLiteral("annotation")).toString()},
        {QStringLiteral("subtitle"), subtitle}, {QStringLiteral("sub"), subtitle}};
}

QStringList fanartImages(const QJsonArray &array) {
    QVector<QPair<int, QString>> ranked;
    for (const auto &value : array) {
        const auto object = value.toObject();
        const QString url = imageUrl(object.value(QStringLiteral("url")).toString());
        const auto likes = object.value(QStringLiteral("likes"));
        if (!url.isEmpty()) ranked.append({likes.isString() ? likes.toString().toInt() : likes.toInt(), url});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList out;
    for (const auto &entry : ranked) {
        if (!out.contains(entry.second)) out.append(entry.second);
        if (out.size() == 40) break;
    }
    return out;
}

QVariantList exactArtists(const QVariantList &artists, const QString &name) {
    QVariantList exact;
    const QString target = MusicIdentityClient::normalizedName(name);
    for (const auto &value : artists) {
        const auto artist = value.toMap();
        bool match = MusicIdentityClient::normalizedName(artist.value(QStringLiteral("name")).toString()) == target;
        for (const auto &alias : artist.value(QStringLiteral("aliases")).toStringList())
            match |= MusicIdentityClient::normalizedName(alias) == target;
        if (match) exact.append(artist);
    }
    return exact;
}
} // namespace

MusicIdentityClient::MusicIdentityClient(Fetch fetch, QString cacheDirectory)
    : m_fetch(fetch ? std::move(fetch) : Fetch([](const QUrl &url, bool mb) { return ArtworkHttp::get(url, mb); })),
      m_cacheDirectory(cacheDirectory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/music-identity-v1")
          : std::move(cacheDirectory)) {}

bool MusicIdentityClient::validMbid(const QString &id) {
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return uuid.match(id).hasMatch();
}

QString MusicIdentityClient::normalizedName(const QString &raw) {
    QString out;
    for (const QChar c : raw.normalized(QString::NormalizationForm_KD).toCaseFolded()) {
        if (c.category() == QChar::Mark_NonSpacing) continue;
        switch (c.unicode()) {
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: out += QLatin1Char('-'); break;
        case 0x2018: case 0x2019: case 0x02bc: out += QLatin1Char('\''); break;
        case 0x0142: out += QLatin1Char('l'); break;
        case 0x00f8: out += QLatin1Char('o'); break;
        default: out += c;
        }
    }
    return out.simplified();
}

QJsonObject MusicIdentityClient::json(const QUrl &url, const QString &provider,
    const std::function<bool(const QJsonObject &)> &valid,
    const std::function<bool(const QJsonObject &)> &empty, QString *error, bool missingIsEmpty) const {
    if (error) error->clear();
    const QString key = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
    const QString path = QDir(m_cacheDirectory).absoluteFilePath(key + QStringLiteral(".json"));
    QJsonObject staleBody;
    QSharedPointer<JsonRequest> request;
    {
        QMutexLocker lock(&cacheMutex);
        QFile cache(path);
        if (cache.size() <= kMaxJsonBytes && cache.open(QIODevice::ReadOnly)) {
            const auto envelope = QJsonDocument::fromJson(cache.readAll()).object();
            const auto body = envelope.value(QStringLiteral("body")).toObject();
            const qint64 expires = envelope.value(QStringLiteral("expires")).toInteger();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const bool cachedMissing = envelope.value(QStringLiteral("missing")).toBool();
            const bool usable = !cachedMissing && valid(body);
            if (expires > now && ((missingIsEmpty && cachedMissing) || usable)) return body;
            // Existing envelopes store only expiry. A positive response is
            // fresh for 24h, then eligible for six further days of outages.
            // Never extend that expiry when serving the previous response.
            if (expires > 0 && expires <= now && now - expires <= kStaleGraceTtl
                && usable && (!empty || !empty(body))) staleBody = body;
        }
        if (const auto pending = jsonRequests.value(path)) {
            while (pending->running) pending->completed.wait(&cacheMutex);
            if (error) *error = pending->error;
            return pending->body;
        }
        const auto now = std::chrono::steady_clock::now();
        for (auto it = jsonFailures.begin(); it != jsonFailures.end(); )
            if (it->until <= now) it = jsonFailures.erase(it); else ++it;
        if (const auto failure = jsonFailures.constFind(path); failure != jsonFailures.cend()) {
            if (!staleBody.isEmpty()) return staleBody;
            if (error) *error = failure->error;
            return {};
        }
        request = QSharedPointer<JsonRequest>::create();
        request->error = QStringLiteral("Could not reach ") + provider + QStringLiteral(". Try again later.");
        jsonRequests.insert(path, request);
    }
    const auto publish = qScopeGuard([&] {
        QMutexLocker lock(&cacheMutex);
        request->running = false;
        jsonRequests.remove(path);
        request->completed.wakeAll();
    });
    const auto response = m_fetch(url, provider == QLatin1String("MusicBrainz"));
    const bool missing = missingIsEmpty && (response.status == 404 || response.status == 410);
    if (!response.success() && !missing) {
        if (response.status == 429 || response.status == 503)
            request->error = provider + QStringLiteral(" is busy. Try again shortly.");
        else if (response.status == 401 || response.status == 403)
            request->error = provider + QStringLiteral(" refused the lookup. Check provider settings or try again later.");
        else if (response.status == 404 || response.status == 410)
            request->error = QStringLiteral("That ") + provider + QStringLiteral(" identity is no longer available. Search for another match.");
        if (response.status == 401 || response.status == 403 || response.status == 404 || response.status == 410) {
            // An authoritative refusal/removal invalidates the old data.
            // A later outage must not resurrect that rejected identity.
            QMutexLocker lock(&cacheMutex);
            QFile::remove(path);
        }
        if (temporaryResponse(response)) {
            {
                QMutexLocker lock(&cacheMutex);
                if (jsonFailures.size() >= 512) {
                    auto oldest = std::min_element(jsonFailures.begin(), jsonFailures.end(),
                        [](const auto &a, const auto &b) { return a.until < b.until; });
                    jsonFailures.erase(oldest);
                }
                jsonFailures.insert(path, {std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(failureCooldown(response.retryAfter)), request->error});
            }
            if (!staleBody.isEmpty()) {
                request->body = staleBody;
                request->error.clear();
                return staleBody;
            }
        }
        if (error) *error = request->error;
        return {};
    }
    QJsonParseError parseError;
    const auto document = missing ? QJsonDocument(QJsonObject()) : QJsonDocument::fromJson(response.body, &parseError);
    const auto body = document.object();
    if (!missing && (response.body.size() > kMaxJsonBytes || parseError.error != QJsonParseError::NoError
            || !document.isObject() || body.contains(QStringLiteral("error")) || !valid(body))) {
        request->error = provider + QStringLiteral(" returned an unreadable response. Try again.");
        if (error) *error = request->error;
        return {};
    }
    const qint64 ttl = missing || (empty && empty(body)) ? kNegativeTtl : kPositiveTtl;
    {
        QMutexLocker lock(&cacheMutex);
        jsonFailures.remove(path);
        QDir().mkpath(m_cacheDirectory);
        QSaveFile cache(path);
        if (cache.open(QIODevice::WriteOnly)) {
            const QByteArray encoded = QJsonDocument(QJsonObject{
                {QStringLiteral("expires"), QDateTime::currentMSecsSinceEpoch() + ttl},
                {QStringLiteral("missing"), missing}, {QStringLiteral("body"), body}}).toJson(QJsonDocument::Compact);
            if (cache.write(encoded) == encoded.size()) cache.commit();
        }
        const auto files = QDir(m_cacheDirectory).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time | QDir::Reversed);
        qint64 bytes = 0;
        for (const auto &file : files) bytes += file.size();
        for (qsizetype i = 0; i < files.size() && (files.size() - i > 512 || bytes > 64 * 1024 * 1024); ++i) {
            if (QFile::remove(files[i].absoluteFilePath())) bytes -= files[i].size();
        }
    }
    request->body = body;
    request->error.clear();
    return body;
}

QVariantList MusicIdentityClient::searchAlbums(const QString &query, const QString &artistHint, QString *error, const QString &artistMbid) const {
    if (error) error->clear();
    if (query.trimmed().isEmpty()) return {};
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release-group/"));
    QUrlQuery parameters;
    QString expression = QStringLiteral("releasegroup:") + quoted(query.trimmed());
    if (validMbid(artistMbid)) expression += QStringLiteral(" AND arid:") + artistMbid.toLower();
    else if (!artistHint.trimmed().isEmpty()) expression += QStringLiteral(" AND artistname:") + quoted(artistHint.trimmed());
    parameters.addQueryItem(QStringLiteral("query"), expression);
    parameters.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    parameters.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
    url.setQuery(parameters);
    const auto body = json(url, QStringLiteral("MusicBrainz"), [](const auto &o) { return o.value(QStringLiteral("release-groups")).isArray(); },
        [](const auto &o) { return o.value(QStringLiteral("release-groups")).toArray().isEmpty(); }, error);
    QVariantList out;
    QSet<QString> ids;
    for (const auto &value : body.value(QStringLiteral("release-groups")).toArray()) {
        auto row = albumMap(value.toObject());
        if (!row.isEmpty()) row.insert(QStringLiteral("searchIncomplete"),
            body.value(QStringLiteral("count")).toInt() > body.value(QStringLiteral("release-groups")).toArray().size());
        const QString id = row.value(QStringLiteral("providerId")).toString();
        if (!row.isEmpty() && !ids.contains(id)) { out.append(row); ids.insert(id); }
    }
    return out;
}

QVariantList MusicIdentityClient::searchArtists(const QString &query, const QString &provider, QString *error) const {
    if (error) error->clear();
    if (query.trimmed().isEmpty()) return {};
    const bool mb = provider == QLatin1String("musicbrainz");
    if (!mb && provider != QLatin1String("deezer")) { if (error) *error = QStringLiteral("Choose MusicBrainz or Deezer for artist identity."); return {}; }
    QUrl url(mb ? QStringLiteral("https://musicbrainz.org/ws/2/artist/") : QStringLiteral("https://api.deezer.com/search/artist"));
    QUrlQuery parameters;
    parameters.addQueryItem(mb ? QStringLiteral("query") : QStringLiteral("q"), mb ? QStringLiteral("artist:") + quoted(query.trimmed()) : query.trimmed());
    if (mb) parameters.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    parameters.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
    url.setQuery(parameters);
    const QString arrayKey = mb ? QStringLiteral("artists") : QStringLiteral("data");
    const auto body = json(url, mb ? QStringLiteral("MusicBrainz") : QStringLiteral("Deezer"),
        [arrayKey](const auto &o) { return o.value(arrayKey).isArray(); },
        [arrayKey](const auto &o) { return o.value(arrayKey).toArray().isEmpty(); }, error);
    const auto rows = body.value(arrayKey).toArray();
    const int total = body.value(mb ? QStringLiteral("count") : QStringLiteral("total")).toInt(rows.size());
    QVariantList out;
    QSet<QString> ids;
    for (const auto &value : rows) {
        auto row = artistMap(value.toObject(), provider);
        const QString id = row.value(QStringLiteral("providerId")).toString();
        if (row.isEmpty() || ids.contains(id)) continue;
        row.insert(QStringLiteral("searchIncomplete"), total > rows.size());
        out.append(row); ids.insert(id);
    }
    return out;
}

QVariantMap MusicIdentityClient::albumDetails(const QString &mbid, QString *error) const {
    if (error) error->clear();
    if (!validMbid(mbid)) { if (error) *error = QStringLiteral("Choose a valid MusicBrainz album match."); return {}; }
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release-group/%1").arg(mbid.toLower()));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("inc"), QStringLiteral("artist-credits+genres+annotation"));
    url.setQuery(query);
    const auto body = json(url, QStringLiteral("MusicBrainz"), [mbid](const auto &o) {
        return o.value(QStringLiteral("id")).toString().compare(mbid, Qt::CaseInsensitive) == 0 && !albumMap(o).isEmpty();
    }, {}, error);
    return albumMap(body);
}

QVariantMap MusicIdentityClient::artistDetails(const QString &provider, const QString &id, QString *error) const {
    if (error) error->clear();
    const bool mb = provider == QLatin1String("musicbrainz");
    if ((mb && !validMbid(id)) || (!mb && (provider != QLatin1String("deezer") || deezerId(QJsonValue(id)).isEmpty()))) {
        if (error) *error = QStringLiteral("Choose a valid artist match.");
        return {};
    }
    QUrl url(mb ? QStringLiteral("https://musicbrainz.org/ws/2/artist/%1").arg(id.toLower())
                : QStringLiteral("https://api.deezer.com/artist/%1").arg(id));
    if (mb) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
        query.addQueryItem(QStringLiteral("inc"), QStringLiteral("aliases+genres+annotation+url-rels"));
        url.setQuery(query);
    }
    const auto body = json(url, mb ? QStringLiteral("MusicBrainz") : QStringLiteral("Deezer"), [provider, id](const auto &o) {
        const auto row = artistMap(o, provider);
        return !row.isEmpty() && row.value(QStringLiteral("providerId")).toString().compare(id, Qt::CaseInsensitive) == 0;
    }, {}, error);
    return artistMap(body, provider);
}

QUrl MusicIdentityClient::fanartUrl(const QString &path) const {
    QUrl url(QStringLiteral("https://webservice.fanart.tv/v3/music") + path);
    QUrlQuery query;
    const QString overrideKey = QSettings().value(QStringLiteral("fanarttvApiKey")).toString();
    query.addQueryItem(QStringLiteral("api_key"), overrideKey.isEmpty() ? QString::fromLatin1(kBundledFanartKey) : overrideKey);
    url.setQuery(query);
    return url;
}

QStringList MusicIdentityClient::fanartArtistUrls(const QString &mbid, QString *error) const {
    if (error) error->clear();
    if (!validMbid(mbid)) { if (error) *error = QStringLiteral("Choose a MusicBrainz artist to find Fanart portraits."); return {}; }
    const auto body = json(fanartUrl(QLatin1Char('/') + mbid.toLower()), QStringLiteral("Fanart.tv"),
        [mbid](const auto &o) {
            // Fanart v3 deliberately returns HTTP200 {} for an unknown
            // resource. This is a valid, short-lived negative result.
            if (o.isEmpty()) return true;
            if (o.contains(QStringLiteral("artistthumb")) && !o.value(QStringLiteral("artistthumb")).isArray()) return false;
            const QString responseId = o.value(QStringLiteral("mbid_id")).toString();
            return (responseId.isEmpty() || responseId.compare(mbid, Qt::CaseInsensitive) == 0)
                && (o.value(QStringLiteral("artistthumb")).isArray() || !responseId.isEmpty());
        },
        [](const auto &o) { return o.value(QStringLiteral("artistthumb")).toArray().isEmpty(); }, error, true);
    return fanartImages(body.value(QStringLiteral("artistthumb")).toArray());
}

QStringList MusicIdentityClient::searchFanartArtistUrls(const QString &name, QString *error) const {
    QString failure;
    const auto matches = exactArtists(searchArtists(name, QStringLiteral("musicbrainz"), &failure), name);
    QStringList portraits;
    for (const auto &value : matches) {
        QString candidateError;
        const auto urls = fanartArtistUrls(value.toMap().value(QStringLiteral("mbid")).toString(), &candidateError);
        if (failure.isEmpty() && !candidateError.isEmpty()) failure = candidateError;
        for (const auto &url : urls) {
            if (!portraits.contains(url)) portraits.append(url);
            if (portraits.size() >= 30) break;
        }
        if (portraits.size() >= 30) break;
    }
    if (error) *error = portraits.isEmpty() ? failure : QString();
    return portraits;
}

QStringList MusicIdentityClient::fanartAlbumUrls(const QString &releaseGroupMbid, QString *error) const {
    if (error) error->clear();
    if (!validMbid(releaseGroupMbid)) { if (error) *error = QStringLiteral("Choose a MusicBrainz album to find Fanart covers."); return {}; }
    const QString id = releaseGroupMbid.toLower();
    const auto body = json(fanartUrl(QStringLiteral("/albums/") + id), QStringLiteral("Fanart.tv"),
        [](const auto &o) { return o.isEmpty() || o.value(QStringLiteral("albums")).isObject() || o.value(QStringLiteral("albumcover")).isArray(); },
        [id](const auto &o) { return o.value(QStringLiteral("albums")).toObject().value(id).toObject().value(QStringLiteral("albumcover")).toArray().isEmpty()
                              && o.value(QStringLiteral("albumcover")).toArray().isEmpty(); }, error, true);
    auto covers = body.value(QStringLiteral("albums")).toObject().value(id).toObject().value(QStringLiteral("albumcover")).toArray();
    if (covers.isEmpty()) covers = body.value(QStringLiteral("albumcover")).toArray();
    return fanartImages(covers);
}

QStringList MusicIdentityClient::caaAlbumUrls(const QString &releaseGroupMbid, QString *error) const {
    if (error) error->clear();
    if (!validMbid(releaseGroupMbid)) { if (error) *error = QStringLiteral("Choose a MusicBrainz album to find its covers."); return {}; }
    const auto body = json(QUrl(QStringLiteral("https://coverartarchive.org/release-group/%1").arg(releaseGroupMbid.toLower())),
        QStringLiteral("Cover Art Archive"), [](const auto &o) { return o.value(QStringLiteral("images")).isArray(); },
        [](const auto &o) { return o.value(QStringLiteral("images")).toArray().isEmpty(); }, error, true);
    QStringList out;
    for (const auto &value : body.value(QStringLiteral("images")).toArray()) {
        const auto artwork = value.toObject();
        if (!artwork.value(QStringLiteral("front")).toBool()) continue;
        QString url = imageUrl(artwork.value(QStringLiteral("image")).toString());
        if (url.isEmpty()) url = imageUrl(artwork.value(QStringLiteral("thumbnails")).toObject().value(QStringLiteral("large")).toString());
        if (!url.isEmpty() && !out.contains(url)) out.append(url);
    }
    return out;
}

QString MusicIdentityClient::automaticAlbumImageUrl(const QString &artist, const QString &album,
    const QVariantMap &selectedIdentity, QString *error) const {
    if (error) error->clear();
    QString id = selectedIdentity.value(QStringLiteral("providerId")).toString();
    if (id.isEmpty()) id = selectedIdentity.value(QStringLiteral("mbid")).toString();
    const QString provider = selectedIdentity.value(QStringLiteral("provider")).toString();
    if (provider == QLatin1String("manual")) id.clear();
    else if ((!provider.isEmpty() && provider != QLatin1String("musicbrainz"))
             || (!id.isEmpty() && !validMbid(id))) return {};
    if (id.isEmpty()) {
        const QString owner = normalizedName(artist), title = normalizedName(album);
        if (owner.isEmpty() || title.isEmpty() || owner == QLatin1String("unknown artist")
            || title == QLatin1String("unknown album")) return {};
        QString searchError;
        const auto matches = searchAlbums(album, artist, &searchError);
        if (!searchError.isEmpty()) { if (error) *error = searchError; return {}; }
        QSet<QString> exact;
        for (const auto &value : matches) {
            const auto row = value.toMap();
            // Never remove edition qualifiers or pick the first fuzzy match.
            if (normalizedName(row.value(QStringLiteral("artist")).toString()) == owner
                && normalizedName(row.value(QStringLiteral("album")).toString()) == title)
                exact.insert(row.value(QStringLiteral("providerId")).toString());
        }
        if (exact.size() != 1) {
            if (error && exact.size() > 1) *error = QStringLiteral("Ambiguous album; choose a match in Customize.");
            return {};
        }
        id = *exact.cbegin();
    }
    QString caaError, fanartError;
    const auto covers = caaAlbumUrls(id, &caaError);
    if (!covers.isEmpty()) return covers.first();
    const auto alternatives = fanartAlbumUrls(id, &fanartError);
    if (!alternatives.isEmpty()) return alternatives.first();
    if (error) *error = !caaError.isEmpty() ? caaError : fanartError;
    return {};
}

QString MusicIdentityClient::automaticArtistImageUrl(const QString &name, const QVariantMap &selectedIdentity, QString *error) const {
    if (error) error->clear();
    QString provider = selectedIdentity.value(QStringLiteral("provider")).toString();
    QString id = selectedIdentity.value(QStringLiteral("providerId")).toString();
    if (id.isEmpty()) id = selectedIdentity.value(QStringLiteral("mbid")).toString();
    if (provider == QLatin1String("manual")) { provider.clear(); id.clear(); }
    if (provider.isEmpty() && validMbid(id)) provider = QStringLiteral("musicbrainz");
    if (!provider.isEmpty() && provider != QLatin1String("manual")
            && ((provider == QLatin1String("musicbrainz") && !validMbid(id))
                || (provider == QLatin1String("deezer") && deezerId(QJsonValue(id)).isEmpty())
                || (provider != QLatin1String("musicbrainz") && provider != QLatin1String("deezer")))) {
        if (error) *error = QStringLiteral("Choose a valid artist identity in Customize.");
        return {};
    }
    if (provider == QLatin1String("deezer") && !id.isEmpty())
        return artistDetails(provider, id, error).value(QStringLiteral("portraitUrl")).toString();
    const bool explicitMbid = provider == QLatin1String("musicbrainz") && validMbid(id);
    QVariantList mbMatches;
    bool albumCorroborated = false;
    if (!explicitMbid) {
        mbMatches = exactArtists(searchArtists(name, QStringLiteral("musicbrainz"), error), name);
        if (mbMatches.size() > 1 || (!mbMatches.isEmpty() && mbMatches.first().toMap().value(QStringLiteral("searchIncomplete")).toBool())) {
            // Corroborate a name with the user's actual albums. No popularity
            // ranking: every usable exact album credit must agree on one ID.
            QSet<QString> candidates, evidence;
            for (const auto &value : mbMatches)
                candidates.insert(value.toMap().value(QStringLiteral("mbid")).toString());
            const auto albums = selectedIdentity.value(QStringLiteral("_libraryAlbums")).toStringList();
            for (const QString &album : albums.mid(0, 3)) {
                QString lookupError;
                const auto releases = searchAlbums(album, name, &lookupError);
                if (!lookupError.isEmpty()) continue;
                for (const auto &value : releases) {
                    const auto row = value.toMap();
                    if (row.value(QStringLiteral("searchIncomplete")).toBool()) continue;
                    if (normalizedName(row.value(QStringLiteral("artist")).toString()) != normalizedName(name)
                        || normalizedName(row.value(QStringLiteral("album")).toString()) != normalizedName(album)) continue;
                    const QString credited = row.value(QStringLiteral("artistMbid")).toString();
                    if (validMbid(credited)) evidence.insert(credited);
                }
            }
            if (evidence.size() != 1 || !candidates.contains(*evidence.cbegin())) {
                if (error) *error = QStringLiteral("Several artists share that name; album evidence could not resolve them. Choose the artist in Customize.");
                return {};
            }
            id = *evidence.cbegin();
            albumCorroborated = true;
            if (error) error->clear();
        }
        if (mbMatches.size() == 1) id = mbMatches.first().toMap().value(QStringLiteral("mbid")).toString();
    }
    if (validMbid(id)) {
        const auto portraits = fanartArtistUrls(id, error);
        if (!portraits.isEmpty()) return portraits.first();
        const auto details = artistDetails(QStringLiteral("musicbrainz"), id, error);
        const QString linkedDeezer = details.value(QStringLiteral("deezerId")).toString();
        if (!linkedDeezer.isEmpty())
            return artistDetails(QStringLiteral("deezer"), linkedDeezer, error).value(QStringLiteral("portraitUrl")).toString();
        // A user's selected MBID must never silently become a same-name Deezer
        // identity. Without an explicit provider relationship, keep it empty.
        if (explicitMbid || albumCorroborated) return {};
    }
    const auto matches = exactArtists(searchArtists(name, QStringLiteral("deezer"), error), name);
    if (matches.size() != 1 || matches.first().toMap().value(QStringLiteral("searchIncomplete")).toBool()) return {};
    return matches.first().toMap().value(QStringLiteral("portraitUrl")).toString();
}

namespace {
bool creditedTo(const QVariantMap &row, const QString &name, const QString &mbid = {}) {
    // A single performer's alias must never match a joint credit.
    if (row.value("creditCount").toInt() != 1) return false;
    if (!mbid.isEmpty()) return row.value("artistMbid").toString().compare(mbid, Qt::CaseInsensitive) == 0;
    const QString target = MusicIdentityClient::normalizedName(name);
    if (MusicIdentityClient::normalizedName(row.value("artist").toString()) == target) return true;
    for (const auto &alias : row.value("artistAliases").toStringList())
        if (MusicIdentityClient::normalizedName(alias) == target) return true;
    return false;
}
struct AlbumTitle { QString base; QStringList hints; };
AlbumTitle albumTitle(const QString &title) {
    AlbumTitle result{title.trimmed(), {}};
    // Only a deliberately bounded vocabulary is an edition qualifier.
    // Unknown parentheses (including named works) remain part of the title.
    static const QRegularExpression suffix(QStringLiteral("\\s*[\\[(](deluxe(?: edition)?|expanded(?: edition)?|clean|explicit|cd\\s*[&+]\\s*dvd|remaster(?:ed)?(?: \\d{4})?)[\\])]\\s*$"), QRegularExpression::CaseInsensitiveOption);
    while (true) {
        const auto match = suffix.match(result.base);
        if (!match.hasMatch()) break;
        result.hints.prepend(MusicIdentityClient::normalizedName(match.captured(1)));
        result.base = result.base.left(match.capturedStart()).trimmed();
    }
    return result;
}
}

MusicIdentityClient::MatchResult MusicIdentityClient::resolveArtistArtwork(const QString &name, const QVariantMap &context) const {
    MatchResult result;
    QString provider = context.value("provider").toString(), id = context.value("providerId").toString();
    if (id.isEmpty()) id = context.value("artistMbid").toString();
    if (id.isEmpty()) id = context.value("mbid").toString();
    if (provider == "manual") { provider.clear(); id.clear(); }
    if (provider.isEmpty() && validMbid(id)) provider = "musicbrainz";
    QString error;
    QVariantMap chosen;
    if (!id.isEmpty() || !provider.isEmpty()) {
        if ((provider != "musicbrainz" && provider != "deezer") ||
            (provider == "musicbrainz" && !validMbid(id)) ||
            (provider == "deezer" && deezerId(QJsonValue(id)).isEmpty())) {
            result.reason = "Choose a valid saved artist match."; return result;
        }
        chosen = artistDetails(provider, id, &error);
    } else {
        const auto rows = searchArtists(name, "musicbrainz", &error);
        auto matches = exactArtists(rows, name);
        result.candidates = matches;
        const bool incomplete = !rows.isEmpty() && rows.first().toMap().value("searchIncomplete").toBool();
        if (matches.size() == 1 && !incomplete) chosen = matches.first().toMap();
        else if (!matches.isEmpty()) {
            QSet<QString> evidence;
            bool evidenceIncomplete = false;
            for (const auto &album : context.value("_libraryAlbums").toStringList().mid(0, 3)) {
                QString failure;
                const auto title = albumTitle(album);
                const auto albums = searchAlbums(title.base, name, &failure);
                if (!failure.isEmpty()) { error = failure; evidenceIncomplete = true; continue; }
                for (const auto &value : albums) {
                    const auto row = value.toMap();
                    if (row.value("searchIncomplete").toBool()) { evidenceIncomplete = true; continue; }
                    if (normalizedName(row.value("title").toString()) == normalizedName(title.base) && creditedTo(row, name))
                        evidence.insert(row.value("artistMbid").toString());
                }
            }
            if (!evidenceIncomplete && evidence.size() == 1)
                for (const auto &value : matches)
                    if (value.toMap().value("providerId").toString() == *evidence.cbegin()) chosen = value.toMap();
        }
    }
    if (chosen.isEmpty()) {
        result.status = error.isEmpty() ? "needsMatch" : "providerUnavailable";
        result.reason = error.isEmpty() ? "Artist identity needs a match; available credits do not establish one artist." : error;
        return result;
    }
    provider = chosen.value("provider").toString(); id = chosen.value("providerId").toString();
    result.identity = {{"provider", provider}, {"providerId", id}, {"artworkScope", "artist"}};
    if (provider == "musicbrainz") {
        result.identity.insert("artistMbid", id);
        QString portraitError, detailsError;
        const auto portraits = fanartArtistUrls(id, &portraitError);
        if (!portraits.isEmpty()) result.imageUrl = portraits.first();
        else {
            const auto details = artistDetails(provider, id, &detailsError);
            const QString linked = details.value("deezerId").toString();
            if (!linked.isEmpty()) result.imageUrl = artistDetails("deezer", linked, &detailsError).value("portraitUrl").toString();
        }
        error = !portraitError.isEmpty() ? portraitError : detailsError;
    } else result.imageUrl = chosen.value("portraitUrl").toString();
    result.status = !result.imageUrl.isEmpty() ? "ready" : error.isEmpty() ? "noArtwork" : "providerUnavailable";
    result.reason = !result.imageUrl.isEmpty() ? "Portrait belongs to the resolved artist." : error.isEmpty() ? "Artist matched; no portrait is available." : error;
    return result;
}

MusicIdentityClient::MatchResult MusicIdentityClient::resolveAlbumArtwork(const QString &artist, const QString &album, const QVariantMap &context) const {
    MatchResult result;
    QString error, groupId = context.value("releaseGroupId").toString();
    QString releaseId = context.value("releaseId").toString();
    const bool confirmedGeneral = context.value("choice").toString() == "manual" &&
        (context.value("scope").toString() == "general" || context.value("artworkScope").toString() == "generalAlbum");
    if (confirmedGeneral) releaseId.clear();
    const QString provider = context.value("provider").toString();
    if (groupId.isEmpty() && provider != "manual") groupId = context.value("providerId").toString();
    if (groupId.isEmpty() && provider != "manual") groupId = context.value("mbid").toString();
    if ((!provider.isEmpty() && provider != "musicbrainz" && provider != "manual") ||
        (!groupId.isEmpty() && !validMbid(groupId)) || (!releaseId.isEmpty() && !validMbid(releaseId))) {
        result.reason = "Choose a valid saved album match."; return result;
    }
    groupId = groupId.toLower(); releaseId = releaseId.toLower();
    const auto title = albumTitle(album);
    if (title.base.isEmpty() || artist.trimmed().isEmpty()) { result.reason = "Artist and album are required."; return result; }
    const QString artistId = context.value("artistMbid").toString();
    auto fetchRelease = [&](const QString &id, QString *failure) {
        QUrl url("https://musicbrainz.org/ws/2/release/" + id);
        QUrlQuery query; query.addQueryItem("fmt", "json");
        query.addQueryItem("inc", "artist-credits+recordings+release-groups"); url.setQuery(query);
        return json(url, "MusicBrainz", [id](const auto &o) { return o.value("id").toString() == id && o.value("media").isArray(); }, {}, failure);
    };
    QJsonObject selectedRelease;
    if (!releaseId.isEmpty()) {
        selectedRelease = fetchRelease(releaseId, &error);
        if (selectedRelease.isEmpty()) { result.status = "providerUnavailable"; result.reason = error; return result; }
        const QString actualGroup = selectedRelease.value("release-group").toObject().value("id").toString();
        if ((!groupId.isEmpty() && actualGroup != groupId) || !validMbid(actualGroup)) {
            result.reason = "Saved release and album identity disagree."; return result;
        }
        groupId = actualGroup;
    }
    if (groupId.isEmpty()) {
        const auto rows = searchAlbums(title.base, artist, &error, artistId);
        bool incomplete = false;
        for (const auto &value : rows) {
            const auto row = value.toMap(); incomplete |= row.value("searchIncomplete").toBool();
            if (normalizedName(row.value("title").toString()) == normalizedName(title.base) && creditedTo(row, artist, artistId))
                result.candidates.append(row);
        }
        if (!error.isEmpty() || incomplete || result.candidates.size() != 1) {
            result.status = error.isEmpty() ? "needsMatch" : "providerUnavailable";
            result.reason = error.isEmpty() ? "Album candidates need a match; no unique complete artist and title match." : error;
            return result;
        }
        groupId = result.candidates.first().toMap().value("providerId").toString();
    }
    result.identity = {{"provider", "musicbrainz"}, {"providerId", groupId}, {"releaseGroupId", groupId}};
    for (auto &value : result.candidates) {
        auto candidate = value.toMap();
        candidate.insert("releaseGroupId", candidate.value("providerId"));
        candidate.insert("scope", "general"); candidate.insert("artworkScope", "generalAlbum");
        value = candidate;
    }
    if (validMbid(artistId)) result.identity.insert("artistMbid", artistId);
    else if (result.candidates.size() == 1) result.identity.insert("artistMbid", result.candidates.first().toMap().value("artistMbid"));
    const auto tracks = context.value("_libraryTracks").toList();
    // Examine only a complete bounded edition inventory. Never pick a winner
    // from a truncated first page. Large discographies remain a manual choice.
    if (selectedRelease.isEmpty() && !confirmedGeneral) {
        QUrl url("https://musicbrainz.org/ws/2/release"); QUrlQuery query;
        query.addQueryItem("release-group", groupId); query.addQueryItem("fmt", "json");
        query.addQueryItem("limit", "8"); url.setQuery(query);
        const auto inventory = json(url, "MusicBrainz", [](const auto &o) { return o.value("releases").isArray(); },
            [](const auto &o) { return o.value("releases").toArray().isEmpty(); }, &error);
        const auto releases = inventory.value("releases").toArray();
        struct Scored { int score; QJsonObject release; };
        QList<Scored> ranked;
        bool complete = error.isEmpty() && inventory.value("release-count").toInt(releases.size()) <= releases.size() && releases.size() <= 8;
        if (complete) for (const auto &value : releases) {
            const QString id = value.toObject().value("id").toString();
            if (!validMbid(id)) { complete = false; break; }
            QString failure; const auto release = fetchRelease(id, &failure);
            if (!failure.isEmpty() || release.isEmpty()) { error = failure; complete = false; break; }
            const auto credit = albumMap(release);
            if (release.value("release-group").toObject().value("id").toString() != groupId ||
                !creditedTo(credit, artist, result.identity.value("artistMbid").toString())) continue;
            auto candidate = credit;
            candidate.insert("providerId", groupId); candidate.insert("releaseGroupId", groupId);
            candidate.insert("releaseId", id); candidate.insert("artworkScope", "exactRelease");
            candidate.insert("scope", "exact"); candidate.insert("year", release.value("date").toString().left(4));
            candidate.insert("country", release.value("country").toString());
            candidate.insert("subtitle", joinNonempty({candidate.value("artist").toString(),
                candidate.value("year").toString(), candidate.value("country").toString(), candidate.value("disambiguation").toString()}));
            result.candidates.append(candidate);
            if (tracks.isEmpty()) continue;
            const auto media = release.value("media").toArray();
            QString description = normalizedName(release.value("title").toString() + ' ' + release.value("disambiguation").toString());
            QStringList formats;
            for (const auto &medium : media) formats.append(medium.toObject().value("format").toString().toLower());
            bool contradiction = false, supportedHints = true;
            if (title.hints.isEmpty()) {
                static const QRegularExpression markedEdition(QStringLiteral("\\b(deluxe|expanded|clean|explicit|remastered)\\b"));
                if (markedEdition.match(description).hasMatch()) continue;
            }
            for (const auto &hint : title.hints) {
                if (hint.startsWith("cd")) supportedHints &= formats.contains("cd") && formats.contains("dvd");
                else { const auto token = hint.section(' ', 0, 0); supportedHints &= description.contains(token);
                    if ((token == "clean" && description.contains("explicit")) || (token == "explicit" && description.contains("clean"))) contradiction = true; }
            }
            if (contradiction || !supportedHints) continue;
            int matches = 0, durationMatches = 0, totalTracks = 0;
            QSet<QString> positions;
            for (const auto &medium : media) totalTracks += medium.toObject().value("track-count").toInt();
            for (const auto &value : tracks) {
                const auto local = value.toMap(); const int disc = local.value("disc").toInt(), number = local.value("track").toInt();
                if (disc > 0) {
                    bool foundDisc = false;
                    for (const auto &medium : media) foundDisc |= medium.toObject().value("position").toInt() == disc;
                    if (!foundDisc) { contradiction = true; continue; }
                }
                if (number <= 0 || (disc <= 0 && media.size() > 1)) continue;
                const QString positionKey = QString::number(disc > 0 ? disc : 1) + ':' + QString::number(number);
                if (positions.contains(positionKey)) { contradiction = true; continue; }
                positions.insert(positionKey);
                bool foundPosition = false;
                for (const auto &mediumValue : media) {
                    const auto medium = mediumValue.toObject();
                    if (disc > 0 && medium.value("position").toInt() != disc) continue;
                    for (const auto &trackValue : medium.value("tracks").toArray()) {
                        const auto remote = trackValue.toObject(); if (remote.value("position").toInt() != number) continue;
                        foundPosition = true;
                        if (local.value("title").toString().isEmpty()) continue;
                        const auto recording = remote.value("recording").toObject();
                        const QString remoteTitle = remote.value("title").toString(recording.value("title").toString());
                        if (normalizedName(remoteTitle) != normalizedName(local.value("title").toString())) { contradiction = true; continue; }
                        ++matches;
                        const int duration = local.value("duration").toInt(), length = remote.value("length").toInt(recording.value("length").toInt());
                        if (duration > 0 && length > 0) { if (qAbs(duration - length) > 10000) contradiction = true; else ++durationMatches; }
                    }
                }
                if (!foundPosition) contradiction = true;
            }
            // At least three corroborating positions, or all tracks on a short
            // release. Partial libraries supply evidence without fake totals.
            const int required = totalTracks > 0 ? qMin(3, totalTracks) : 3;
            if (contradiction || matches < required) continue;
            int score = 60 + qMin(matches, 10) * 2 + qMin(durationMatches, 5) * 2;
            if (totalTracks == tracks.size() && matches == tracks.size()) score += 15;
            if (!title.hints.isEmpty()) score += 10;
            const int year = context.value("_libraryYear").toInt();
            if (year > 0 && release.value("date").toString().left(4).toInt() == year) score += 5;
            ranked.append({score, release});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
        if (complete && !ranked.isEmpty() && ranked.first().score >= 85 && (ranked.size() == 1 || ranked.first().score - ranked[1].score >= 10)) {
            selectedRelease = ranked.first().release; releaseId = selectedRelease.value("id").toString();
            result.identity.insert("evidence", QStringLiteral("Track positions, titles, supported edition hints and available dates; score %1.").arg(ranked.first().score));
        }
    }
    if (!selectedRelease.isEmpty()) {
        result.identity.insert("releaseId", releaseId);
        const auto releaseCovers = caaReleaseUrls(releaseId, &error);
        if (!releaseCovers.isEmpty()) result.imageUrl = releaseCovers.first();
        if (!result.imageUrl.isEmpty()) {
            result.status = "ready"; result.identity.insert("artworkScope", "exactRelease");
            result.reason = "Cover belongs to the established release."; return result;
        }
    }
    QString coverError; auto covers = caaAlbumUrls(groupId, &coverError);
    if (covers.isEmpty()) { QString fallbackError; covers = fanartAlbumUrls(groupId, &fallbackError); if (coverError.isEmpty()) coverError = fallbackError; }
    if (!covers.isEmpty()) {
        result.identity.remove("releaseId");
        result.imageUrl = covers.first(); result.status = "ready";
        result.identity.insert("artworkScope", "generalAlbum");
        result.reason = "Using the matched album cover; exact edition artwork is unavailable or uncertain.";
        if (context.value("choice").toString() == "manual" &&
            (context.value("scope").toString() == "general" || context.value("artworkScope").toString() == "generalAlbum")) {
            result.status = "ready";
            result.identity.insert("choice", "manual"); result.identity.insert("scope", "general");
            result.reason = "Using your confirmed general album cover.";
        }
    } else {
        if (error.isEmpty()) error = coverError;
        result.status = error.isEmpty() ? "noArtwork" : "providerUnavailable";
        result.reason = error.isEmpty() ? "Album matched; no cover is available." : error;
    }
    return result;
}

QStringList MusicIdentityClient::caaReleaseUrls(const QString &releaseMbid, QString *error) const {
    if (error) error->clear();
    if (!validMbid(releaseMbid)) { if (error) *error = "Choose a valid MusicBrainz release."; return {}; }
    const auto body = json(QUrl("https://coverartarchive.org/release/" + releaseMbid.toLower()), "Cover Art Archive",
        [](const auto &o) { return o.value("images").isArray(); },
        [](const auto &o) { return o.value("images").toArray().isEmpty(); }, error, true);
    QStringList urls;
    for (const auto &value : body.value("images").toArray()) {
        const auto cover = value.toObject(); if (!cover.value("front").toBool()) continue;
        QString url = imageUrl(cover.value("image").toString());
        if (url.isEmpty()) url = imageUrl(cover.value("thumbnails").toObject().value("large").toString());
        if (!url.isEmpty() && !urls.contains(url)) urls.append(url);
    }
    return urls;
}
