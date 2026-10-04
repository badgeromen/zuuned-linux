#include "TmdbClient.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QUrl>

namespace {

constexpr const char *kBaseUrl = "https://api.themoviedb.org/3";
constexpr const char *kImageCdn = "https://image.tmdb.org/t/p";
// Bundled keys — identical to the mac app's shipped fallbacks; Settings
// "tmdbApiKey" / "fanarttvApiKey" override.
constexpr const char *kBundledTmdbKey = "353d73db46a9e224de176291da7ee045";
constexpr const char *kBundledFanartKey = "b5177dee86dfad2bc891dbe74bb253bf";
constexpr const char *kPosterSize = "w500";   // detail sidebar at 2×
constexpr const char *kStillSize = "w300";    // 16:9 rows at 2×

QString extractYear(const QString &date) {
    return date.size() >= 4 ? date.left(4) : QString();
}

// Pull the LAST year token out of the query (titles can BE years:
// "1917 2019") and return the query without it.
QString splitTrailingYear(const QString &query, QString *yearOut) {
    static const QRegularExpression yearRe(QStringLiteral("\\b(19\\d{2}|20\\d{2})\\b"));
    QString year;
    auto it = yearRe.globalMatch(query);
    int lastStart = -1, lastLen = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        year = m.captured(1);
        lastStart = m.capturedStart();
        lastLen = m.capturedLength();
    }
    *yearOut = year;
    if (lastStart < 0)
        return query;
    QString clean = query;
    clean.remove(lastStart, lastLen);
    return clean.simplified();
}

QVector<TmdbResult> parseSearch(const QByteArray &data, bool tv) {
    QVector<TmdbResult> out;
    const QJsonArray results =
        QJsonDocument::fromJson(data).object().value(QLatin1String("results")).toArray();
    for (const auto &vv : results) {
        if (out.size() >= 10)
            break;
        const QJsonObject o = vv.toObject();
        TmdbResult r;
        r.id = o.value(QLatin1String("id")).toInt();
        r.title = o.value(tv ? QLatin1String("name") : QLatin1String("title")).toString();
        r.year = extractYear(o.value(tv ? QLatin1String("first_air_date")
                                        : QLatin1String("release_date")).toString());
        r.posterPath = o.value(QLatin1String("poster_path")).toString();
        r.overview = o.value(QLatin1String("overview")).toString();
        r.popularity = o.value(QLatin1String("popularity")).toDouble();
        r.voteCount = o.value(QLatin1String("vote_count")).toInt();
        out.append(r);
    }
    return out;
}

QString joinNames(const QJsonArray &arr, int limit) {
    QStringList names;
    for (const auto &v : arr) {
        if (names.size() >= limit)
            break;
        const QString n = v.toObject().value(QLatin1String("name")).toString();
        if (!n.isEmpty())
            names.append(n);
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

TmdbClient::TmdbClient() {
    m_nam = new QNetworkAccessManager();
    m_nam->setTransferTimeout(30000);
}

TmdbClient::~TmdbClient() { delete m_nam; }

QString TmdbClient::apiKey() const {
    const QString user = QSettings().value(QStringLiteral("tmdbApiKey")).toString();
    return user.isEmpty() ? QString::fromLatin1(kBundledTmdbKey) : user;
}

QByteArray TmdbClient::fetch(const QString &url, bool *ok, bool throttled,
                             const QByteArray &apiKeyHeader) {
    if (ok) *ok = false;

    if (throttled) {
        if (m_lastRequest.isValid()) {
            const qint64 elapsed = m_lastRequest.elapsed();
            if (elapsed < 100)
                QThread::msleep(100 - elapsed);
        }
        m_lastRequest.start();
    }

    QNetworkRequest req{QUrl(url)};
    if (!apiKeyHeader.isEmpty())
        req.setRawHeader("api-key", apiKeyHeader);

    QNetworkReply *reply = m_nam->get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data;
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status == 200) {
        data = reply->readAll();
        if (ok) *ok = true;
    } else {
        fprintf(stderr, "[tmdb] fetch failed (%d/%s): %s\n", status,
                qPrintable(reply->errorString()), qPrintable(url.left(80)));
    }
    reply->deleteLater();
    return data;
}

QVector<TmdbResult> TmdbClient::searchMovie(const QString &query, bool *ok) {
    QString year;
    const QString clean = splitTrailingYear(query, &year);
    QString url = QStringLiteral("%1/search/movie?api_key=%2&query=%3&page=1")
        .arg(QLatin1String(kBaseUrl), apiKey(),
             QString::fromUtf8(QUrl::toPercentEncoding(clean)));
    if (!year.isEmpty())
        url += QStringLiteral("&year=") + year;
    const QByteArray data = fetch(url, ok);
    return data.isEmpty() ? QVector<TmdbResult>() : parseSearch(data, false);
}

QVector<TmdbResult> TmdbClient::searchTV(const QString &query, bool *ok) {
    static const QRegularExpression seasonSuffix(
        QStringLiteral("\\b(?:Season|Series)\\s+\\d+\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QString q = query;
    q.remove(seasonSuffix);
    q = q.simplified();

    QString year;
    const QString clean = splitTrailingYear(q, &year);
    QString url = QStringLiteral("%1/search/tv?api_key=%2&query=%3&page=1")
        .arg(QLatin1String(kBaseUrl), apiKey(),
             QString::fromUtf8(QUrl::toPercentEncoding(clean)));
    if (!year.isEmpty())
        url += QStringLiteral("&first_air_date_year=") + year;
    const QByteArray data = fetch(url, ok);
    return data.isEmpty() ? QVector<TmdbResult>() : parseSearch(data, true);
}

bool TmdbClient::movieDetails(int tmdbId, TmdbDetails *out) {
    bool ok = false;
    const QByteArray data = fetch(
        QStringLiteral("%1/movie/%2?api_key=%3&append_to_response=credits")
            .arg(QLatin1String(kBaseUrl)).arg(tmdbId).arg(apiKey()), &ok);
    if (!ok)
        return false;
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    out->id = o.value(QLatin1String("id")).toInt();
    out->title = o.value(QLatin1String("title")).toString();
    out->year = extractYear(o.value(QLatin1String("release_date")).toString());
    out->overview = o.value(QLatin1String("overview")).toString();
    out->posterPath = o.value(QLatin1String("poster_path")).toString();
    out->rating = o.value(QLatin1String("vote_average")).toDouble();
    out->genres = joinNames(o.value(QLatin1String("genres")).toArray(), 5);
    const QJsonObject credits = o.value(QLatin1String("credits")).toObject();
    out->cast = joinNames(credits.value(QLatin1String("cast")).toArray(), 5);
    for (const auto &c : credits.value(QLatin1String("crew")).toArray()) {
        const QJsonObject crew = c.toObject();
        if (crew.value(QLatin1String("job")).toString() == QLatin1String("Director")) {
            out->director = crew.value(QLatin1String("name")).toString();
            break;
        }
    }
    return true;
}

bool TmdbClient::tvDetails(int tmdbId, TmdbDetails *out) {
    bool ok = false;
    const QByteArray data = fetch(
        QStringLiteral("%1/tv/%2?api_key=%3&append_to_response=credits")
            .arg(QLatin1String(kBaseUrl)).arg(tmdbId).arg(apiKey()), &ok);
    if (!ok)
        return false;
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    out->id = o.value(QLatin1String("id")).toInt();
    out->title = o.value(QLatin1String("name")).toString();
    out->year = extractYear(o.value(QLatin1String("first_air_date")).toString());
    out->overview = o.value(QLatin1String("overview")).toString();
    out->posterPath = o.value(QLatin1String("poster_path")).toString();
    out->rating = o.value(QLatin1String("vote_average")).toDouble();
    out->genres = joinNames(o.value(QLatin1String("genres")).toArray(), 5);
    out->director = joinNames(o.value(QLatin1String("created_by")).toArray(), 3);
    out->cast = joinNames(
        o.value(QLatin1String("credits")).toObject().value(QLatin1String("cast")).toArray(), 5);
    for (const auto &sv : o.value(QLatin1String("seasons")).toArray()) {
        const QJsonObject s = sv.toObject();
        out->seasonEpisodeCounts.insert(
            s.value(QLatin1String("season_number")).toInt(),
            s.value(QLatin1String("episode_count")).toInt());
    }
    return true;
}

QVector<TmdbEpisode> TmdbClient::seasonEpisodes(int showId, int season, bool *ok) {
    const QByteArray data = fetch(
        QStringLiteral("%1/tv/%2/season/%3?api_key=%4")
            .arg(QLatin1String(kBaseUrl)).arg(showId).arg(season).arg(apiKey()), ok);
    QVector<TmdbEpisode> out;
    if (data.isEmpty())
        return out;
    for (const auto &ev :
         QJsonDocument::fromJson(data).object().value(QLatin1String("episodes")).toArray()) {
        const QJsonObject e = ev.toObject();
        TmdbEpisode ep;
        ep.episodeNumber = e.value(QLatin1String("episode_number")).toInt();
        ep.name = e.value(QLatin1String("name")).toString();
        ep.overview = e.value(QLatin1String("overview")).toString();
        ep.airDate = e.value(QLatin1String("air_date")).toString();
        ep.tmdbId = e.value(QLatin1String("id")).toInt();
        ep.stillPath = e.value(QLatin1String("still_path")).toString();
        ep.rating = e.value(QLatin1String("vote_average")).toDouble();
        ep.runtime = e.value(QLatin1String("runtime")).toInt();
        out.append(ep);
    }
    return out;
}

int TmdbClient::tvdbIdFor(int tmdbId) {
    const auto it = m_tvdbCache.constFind(tmdbId);
    if (it != m_tvdbCache.constEnd())
        return it.value();
    bool ok = false;
    const QByteArray data = fetch(
        QStringLiteral("%1/tv/%2/external_ids?api_key=%3")
            .arg(QLatin1String(kBaseUrl)).arg(tmdbId).arg(apiKey()), &ok);
    if (!ok)
        return 0;
    const int tvdb =
        QJsonDocument::fromJson(data).object().value(QLatin1String("tvdb_id")).toInt();
    if (tvdb > 0)
        m_tvdbCache.insert(tmdbId, tvdb);
    return tvdb;
}

static QVector<TmdbPoster> parsePosters(const QByteArray &data) {
    QVector<TmdbPoster> out;
    for (const auto &pv :
         QJsonDocument::fromJson(data).object().value(QLatin1String("posters")).toArray()) {
        const QJsonObject p = pv.toObject();
        TmdbPoster poster;
        poster.filePath = p.value(QLatin1String("file_path")).toString();
        poster.language = p.value(QLatin1String("iso_639_1")).toString();
        poster.width = p.value(QLatin1String("width")).toInt();
        poster.height = p.value(QLatin1String("height")).toInt();
        poster.voteAverage = p.value(QLatin1String("vote_average")).toDouble();
        out.append(poster);
    }
    return out;
}

QVector<TmdbPoster> TmdbClient::moviePosters(int tmdbId, bool *success) {
    bool ok = false;
    const QByteArray data = fetch(
        QStringLiteral("%1/movie/%2/images?api_key=%3")
            .arg(QLatin1String(kBaseUrl)).arg(tmdbId).arg(apiKey()), &ok);
    if (success) *success = ok;
    return ok ? parsePosters(data) : QVector<TmdbPoster>();
}

QVector<TmdbPoster> TmdbClient::tvPosters(int tmdbId, bool *success) {
    bool ok = false;
    const QByteArray data = fetch(
        QStringLiteral("%1/tv/%2/images?api_key=%3")
            .arg(QLatin1String(kBaseUrl)).arg(tmdbId).arg(apiKey()), &ok);
    if (success) *success = ok;
    return ok ? parsePosters(data) : QVector<TmdbPoster>();
}

bool TmdbClient::downloadPoster(const QString &posterPath, const QString &destPath) {
    if (posterPath.isEmpty())
        return false;
    return downloadUrl(QStringLiteral("%1/%2%3").arg(
        QLatin1String(kImageCdn), QLatin1String(kPosterSize), posterPath), destPath);
}

bool TmdbClient::downloadStill(const QString &stillPath, const QString &destPath) {
    if (stillPath.isEmpty())
        return false;
    return downloadUrl(QStringLiteral("%1/%2%3").arg(
        QLatin1String(kImageCdn), QLatin1String(kStillSize), stillPath), destPath);
}

bool TmdbClient::downloadUrl(const QString &url, const QString &destPath) {
    bool ok = false;
    const QByteArray data = fetch(url, &ok, /*throttled=*/false);
    if (!ok || data.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return true;
}

// ── Fanart.tv ──

QStringList FanartClient::posterUrls(const QString &path, const char *jsonKey, bool *ok) {
    const QString userKey = QSettings().value(QStringLiteral("fanarttvApiKey")).toString();
    const QByteArray key =
        (userKey.isEmpty() ? QByteArray(kBundledFanartKey) : userKey.toUtf8());
    const QByteArray data = m_tmdb->fetch(
        QStringLiteral("https://webservice.fanart.tv/v3") + path, ok,
        /*throttled=*/true, key);
    QStringList urls;
    if (data.isEmpty())
        return urls;

    struct Item { QString url; QString lang; int likes = 0; };
    QVector<Item> items;
    for (const auto &pv :
         QJsonDocument::fromJson(data).object().value(QLatin1String(jsonKey)).toArray()) {
        const QJsonObject p = pv.toObject();
        Item i;
        i.url = p.value(QLatin1String("url")).toString();
        i.lang = p.value(QLatin1String("lang")).toString();
        i.likes = p.value(QLatin1String("likes")).toString().toInt();
        if (!i.url.isEmpty())
            items.append(i);
    }
    // English first, then most-liked (the mac's ordering).
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const bool ae = a.lang == QLatin1String("en"), be = b.lang == QLatin1String("en");
        if (ae != be) return ae;
        return a.likes > b.likes;
    });
    for (const Item &i : items)
        urls.append(i.url);
    return urls;
}

QStringList FanartClient::moviePosterUrls(int tmdbId, bool *ok) {
    return posterUrls(QStringLiteral("/movies/%1").arg(tmdbId), "movieposter", ok);
}

QStringList FanartClient::tvPosterUrls(int tvdbId, bool *ok) {
    return posterUrls(QStringLiteral("/tv/%1").arg(tvdbId), "tvposter", ok);
}
