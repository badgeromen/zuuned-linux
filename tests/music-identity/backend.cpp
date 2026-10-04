#include "library/MusicIdentityClient.h"
#include "library/ArtistImageService.h"
#include "library/OnlineAlbumArtService.h"
#include "library/AlbumArtService.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <atomic>
#include <cstdio>
#include <cstdlib>

// Every request must use the injected transport. Linking this fixture never
// pulls in LibraryService, a device singleton, or a real network request.
extern "C" int zuuned_extract_art(const char *, const char *, int) { return -1; }

ArtworkHttp::Response ArtworkHttp::get(const QUrl &, bool) { std::abort(); }
QByteArray ArtworkHttp::getBody(const QUrl &, bool, QString *) { std::abort(); }

namespace {
int checks = 0, failures = 0;
const QString artistA = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString artistB = QStringLiteral("22222222-2222-4222-8222-222222222222");
const QString albumId = QStringLiteral("33333333-3333-4333-8333-333333333333");
const QString portraitA = QStringLiteral("https://assets.fanart.tv/artist-a.jpg");
const QString portraitB = QStringLiteral("https://assets.fanart.tv/artist-b.jpg");

void check(bool ok, const char *message) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    ++checks;
    if (!ok) ++failures;
}

ArtworkHttp::Response response(const QJsonObject &object, int status = 200) {
    ArtworkHttp::Response result;
    result.status = status;
    result.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return result;
}

QJsonObject artist(const QString &id, const QString &name, const QString &meaning = {}) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
        {QStringLiteral("type"), QStringLiteral("Person")}, {QStringLiteral("country"), QStringLiteral("AU")},
        {QStringLiteral("disambiguation"), meaning}};
}

QJsonObject deezerArtist(int id, const QString &name, const QString &picture) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
        {QStringLiteral("type"), QStringLiteral("artist")}, {QStringLiteral("nb_album"), 12},
        {QStringLiteral("picture_xl"), picture}};
}

QByteArray jpeg(QColor color) {
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG");
    return bytes;
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) std::abort();
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool waitUntil(const std::function<bool()> &done, int timeout = 5000) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (done()) return true;
        QThread::msleep(2);
    } while (timer.elapsed() < timeout);
    return false;
}

void finishWorker() {
    if (!QThreadPool::globalInstance()->waitForDone(5000)) std::abort();
    QEventLoop loop;
    QTimer::singleShot(20, &loop, &QEventLoop::quit);
    loop.exec();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ZuunedFixture"));
    QCoreApplication::setApplicationName(QStringLiteral("MusicIdentity"));
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const auto cache = [&](const QString &test) { return temporary.path() + QLatin1Char('/') + test; };
    QString error;

    QJsonObject release{{QStringLiteral("id"), albumId}, {QStringLiteral("title"), QStringLiteral("Skyfall")},
        {QStringLiteral("first-release-date"), QStringLiteral("2012-10-05")},
        {QStringLiteral("primary-type"), QStringLiteral("Single")},
        {QStringLiteral("disambiguation"), QStringLiteral("Adele single")},
        {QStringLiteral("artist-credit"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Adele")},
            {QStringLiteral("artist"), artist(artistA, QStringLiteral("Adele"))}}}},
        {QStringLiteral("genres"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("pop")}, {QStringLiteral("count"), 4}}}}};
    int calls = 0;
    MusicIdentityClient albums([&](const QUrl &url, bool mb) {
        ++calls;
        check(mb && url.host() == QLatin1String("musicbrainz.org"), "album search/detail uses shared MusicBrainz transport classification");
        if (url.path().endsWith(albumId)) return response(release);
        const QString query = QUrlQuery(url).queryItemValue(QStringLiteral("query"));
        check(query.contains(QStringLiteral("releasegroup:\"Skyfall\"")) && query.contains(QStringLiteral("artistname:\"Adele\"")),
              "album search keeps artist hint separate in the MusicBrainz query");
        return response({{QStringLiteral("release-groups"), QJsonArray{release, release}}});
    }, cache(QStringLiteral("albums")));
    const auto albumRows = albums.searchAlbums(QStringLiteral("Skyfall"), QStringLiteral("Adele"), &error);
    const auto album = albums.albumDetails(albumId, &error);
    check(error.isEmpty() && albumRows.size() == 1 && album.value(QStringLiteral("providerId")) == albumId
          && album.value(QStringLiteral("artistMbid")) == artistA && album.value(QStringLiteral("genre")) == QStringLiteral("pop")
          && album.value(QStringLiteral("year")) == QStringLiteral("2012")
          && album.value(QStringLiteral("sub")).toString().contains(QStringLiteral("Adele single")),
          "album results preserve stable release-group ID, artist ID, year, genre, and disambiguation");
    MusicIdentityClient reopened([](const QUrl &, bool) -> ArtworkHttp::Response { std::abort(); }, cache(QStringLiteral("albums")));
    check(reopened.albumDetails(albumId, &error) == album && calls == 2, "provider details survive client reconstruction from disk cache");
    const int beforeInvalid = calls;
    check(albums.albumDetails(QStringLiteral("../../bad"), &error).isEmpty() && !error.isEmpty() && calls == beforeInvalid,
          "malformed identity IDs are rejected before any request");

    int fallbackCalls = 0;
    const auto ambiguousTransport = [&](const QUrl &, bool mb) {
        if (!mb) { ++fallbackCalls; return response({}); }
        return response({{QStringLiteral("artists"), QJsonArray{
            artist(artistA, QStringLiteral("Christopher Larkin"), QStringLiteral("Hollow Knight composer")),
            artist(artistB, QStringLiteral("Christopher Larkin"), QStringLiteral("American actor"))}},
            {QStringLiteral("count"), 2}});
    };
    MusicIdentityClient ambiguous(ambiguousTransport, cache(QStringLiteral("ambiguous")));
    const auto identities = ambiguous.searchArtists(QStringLiteral("Christopher Larkin"), QStringLiteral("musicbrainz"), &error);
    check(identities.size() == 2 && identities[0].toMap().value(QStringLiteral("sub")).toString().contains(QStringLiteral("composer")),
          "artist search exposes homonyms and their disambiguation for explicit selection");
    check(ambiguous.automaticArtistImageUrl(QStringLiteral("Christopher Larkin"), {}, &error).isEmpty() && fallbackCalls == 0,
          "automatic portrait refuses ambiguous artist names instead of falling through to an unrelated first result");

    QStringList requested;
    MusicIdentityClient fanart([&](const QUrl &url, bool mb) {
        requested.append(url.path());
        check(!mb, "Fanart/CAA art requests do not consume the MusicBrainz throttle");
        if (url.host() == QLatin1String("coverartarchive.org")) {
            const QJsonArray images{
                QJsonObject{{QStringLiteral("front"), true}, {QStringLiteral("image"), QStringLiteral("http://coverartarchive.org/front.jpg")}},
                QJsonObject{{QStringLiteral("front"), false}, {QStringLiteral("image"), QStringLiteral("https://coverartarchive.org/back.jpg")}}};
            return response({{QStringLiteral("images"), images}});
        }
        if (url.path().contains(QStringLiteral("/albums/"))) {
            const QJsonArray chosen{QJsonObject{{QStringLiteral("url"), portraitA}, {QStringLiteral("likes"), QStringLiteral("5")}}};
            const QJsonArray other{QJsonObject{{QStringLiteral("url"), portraitB}}};
            const QJsonObject albums{{albumId, QJsonObject{{QStringLiteral("albumcover"), chosen}}},
                                     {artistB, QJsonObject{{QStringLiteral("albumcover"), other}}}};
            return response({{QStringLiteral("albums"), albums}});
        }
        const QJsonArray portraits{
            QJsonObject{{QStringLiteral("url"), portraitB}, {QStringLiteral("likes"), QStringLiteral("2")}},
            QJsonObject{{QStringLiteral("url"), portraitA}, {QStringLiteral("likes"), QStringLiteral("20")}},
            QJsonObject{{QStringLiteral("url"), portraitA}, {QStringLiteral("likes"), QStringLiteral("1")}}};
        return response({{QStringLiteral("mbid_id"), artistA}, {QStringLiteral("artistthumb"), portraits}});
    }, cache(QStringLiteral("fanart")));
    const auto selected = QVariantMap{{QStringLiteral("provider"), QStringLiteral("musicbrainz")}, {QStringLiteral("providerId"), artistA}};
    check(fanart.automaticArtistImageUrl(QStringLiteral("Christopher Larkin"), selected, &error) == portraitA
          && requested.size() == 1 && requested.first().endsWith(artistA),
          "selected MBID requests its exact Fanart portraits without an ambiguous name lookup");
    check(fanart.fanartArtistUrls(artistA, &error) == QStringList{portraitA, portraitB},
          "Fanart portraits are deduplicated and ordered by provider likes");
    MusicIdentityClient wrongArtist([&](const QUrl &, bool) {
        const QJsonArray portraits{QJsonObject{{QStringLiteral("url"), portraitB}}};
        return response({{QStringLiteral("mbid_id"), artistB}, {QStringLiteral("artistthumb"), portraits}});
    }, cache(QStringLiteral("wrong-artist")));
    check(wrongArtist.fanartArtistUrls(artistA, &error).isEmpty() && !error.isEmpty(),
          "mismatched Fanart artist identity is rejected instead of showing another person's portrait");
    check(fanart.fanartAlbumUrls(albumId, &error) == QStringList{portraitA}
          && requested.last().endsWith(QStringLiteral("/albums/") + albumId),
          "Fanart album lookup reads only the selected release-group artwork");
    check(fanart.caaAlbumUrls(albumId, &error) == QStringList{QStringLiteral("https://coverartarchive.org/front.jpg")},
          "exact CAA lookup returns front covers and upgrades legacy provider HTTP URLs");

    int deezerSearches = 0;
    MusicIdentityClient deezer([&](const QUrl &url, bool mb) {
        if (url.path() == QLatin1String("/artist/77")) {
            check(!mb, "Deezer exact identity lookup uses its own provider transport");
            return response(deezerArtist(77, QStringLiteral("Christopher Larkin"), portraitB));
        }
        if (url.host() == QLatin1String("musicbrainz.org")) return response({{QStringLiteral("artists"), QJsonArray{}}});
        ++deezerSearches;
        return response({{QStringLiteral("data"), QJsonArray{
            deezerArtist(1, QStringLiteral("Wrong Person"), portraitA),
            deezerArtist(77, QStringLiteral("Christopher Larkin"), portraitB)}}});
    }, cache(QStringLiteral("deezer")));
    check(deezer.automaticArtistImageUrl(QStringLiteral("Christopher Larkin"), {}, &error) == portraitB && deezerSearches == 1,
          "Deezer fallback selects the unique exact normalized name, not the first search result");
    const auto deezerIdentity = QVariantMap{{QStringLiteral("provider"), QStringLiteral("deezer")}, {QStringLiteral("providerId"), QStringLiteral("77")}};
    check(deezer.automaticArtistImageUrl(QStringLiteral("Any local spelling"), deezerIdentity, &error) == portraitB && deezerSearches == 1
          && deezer.artistDetails(QStringLiteral("deezer"), QStringLiteral("77")).value(QStringLiteral("posterUrl")) == portraitB,
          "explicit Deezer selection uses stable ID and exposes its exact portrait to Customize");

    MusicIdentityClient duplicateDeezer([&](const QUrl &, bool mb) {
        return mb ? response({{QStringLiteral("artists"), QJsonArray{}}})
                  : response({{QStringLiteral("data"), QJsonArray{
                      deezerArtist(1, QStringLiteral("Same Name"), portraitA), deezerArtist(2, QStringLiteral("Same Name"), portraitB)}}});
    }, cache(QStringLiteral("duplicate-deezer")));
    check(duplicateDeezer.automaticArtistImageUrl(QStringLiteral("Same Name"), {}, &error).isEmpty(),
          "duplicate Deezer names produce no automatic portrait");
    check(MusicIdentityClient::normalizedName(QStringLiteral("  blink‐182 ")) == QStringLiteral("blink-182")
          && MusicIdentityClient::normalizedName(QStringLiteral("Przybyłowicz")) == QStringLiteral("przybylowicz"),
          "artist normalization retains existing Unicode dash and diacritic tolerance");

    int negativeCalls = 0;
    MusicIdentityClient negative([&](const QUrl &, bool) { ++negativeCalls; return response({}, 404); }, cache(QStringLiteral("negative")));
    check(negative.fanartArtistUrls(artistA, &error).isEmpty() && error.isEmpty()
          && negative.fanartArtistUrls(artistA, &error).isEmpty() && negativeCalls == 1,
          "provider no-art response is cached briefly without pretending it is a network error");
    const auto negatives = QDir(cache(QStringLiteral("negative"))).entryList({QStringLiteral("*.json")}, QDir::Files);
    const QString negativePath = cache(QStringLiteral("negative")) + QLatin1Char('/') + negatives.first();
    auto envelope = QJsonDocument::fromJson(readFile(negativePath)).object();
    const qint64 ttl = envelope.value(QStringLiteral("expires")).toInteger() - QDateTime::currentMSecsSinceEpoch();
    check(ttl > 0 && ttl <= 5 * 60 * 1000, "negative provider result expires within five minutes");
    envelope.insert(QStringLiteral("expires"), 1);
    writeFile(negativePath, QJsonDocument(envelope).toJson());
    negative.fanartArtistUrls(artistA, &error);
    check(negativeCalls == 2, "expired provider cache retries instead of hiding new artwork indefinitely");

    int emptyFanartCalls = 0;
    const QString emptyFanartCache = cache(QStringLiteral("fanart-empty-200"));
    MusicIdentityClient emptyFanart([&](const QUrl &, bool) {
        ++emptyFanartCalls;
        return response({});
    }, emptyFanartCache);
    error = QStringLiteral("stale provider error");
    check(emptyFanart.fanartArtistUrls(artistA, &error).isEmpty() && error.isEmpty()
          && emptyFanart.fanartArtistUrls(artistA, &error).isEmpty() && error.isEmpty() && emptyFanartCalls == 1,
          "Fanart HTTP200 empty artist object is no artwork, clears prior error, and is cached");
    error = QStringLiteral("stale provider error");
    check(emptyFanart.fanartAlbumUrls(albumId, &error).isEmpty() && error.isEmpty()
          && emptyFanart.fanartAlbumUrls(albumId, &error).isEmpty() && error.isEmpty() && emptyFanartCalls == 2,
          "Fanart HTTP200 empty album object is no artwork, clears prior error, and is cached");
    MusicIdentityClient reopenedEmptyFanart([](const QUrl &, bool) -> ArtworkHttp::Response { std::abort(); }, emptyFanartCache);
    check(reopenedEmptyFanart.fanartArtistUrls(artistA, &error).isEmpty() && error.isEmpty()
          && reopenedEmptyFanart.fanartAlbumUrls(albumId, &error).isEmpty() && error.isEmpty(),
          "Fanart HTTP200 empty results survive client reconstruction without another request");
    const auto emptyFanartFiles = QDir(emptyFanartCache).entryList({QStringLiteral("*.json")}, QDir::Files);
    bool emptyFanartTtls = emptyFanartFiles.size() == 2;
    for (const QString &filename : emptyFanartFiles) {
        const auto entry = QJsonDocument::fromJson(readFile(emptyFanartCache + QLatin1Char('/') + filename)).object();
        const qint64 remaining = entry.value(QStringLiteral("expires")).toInteger() - QDateTime::currentMSecsSinceEpoch();
        emptyFanartTtls = emptyFanartTtls && remaining > 0 && remaining <= 5 * 60 * 1000;
    }
    check(emptyFanartTtls, "Fanart HTTP200 empty objects receive the short negative-cache lifetime");

    int malformedFanartCalls = 0;
    const QList<QByteArray> malformedFanartBodies{
        {}, "[]", "{broken", "<html>upstream error</html>",
        R"({"error":"invalid API key"})",
        R"({"artistthumb":"not an array","albums":[]})"};
    bool rejectsMalformedFanart = true;
    for (qsizetype i = 0; i < malformedFanartBodies.size(); ++i) {
        MusicIdentityClient malformedFanart([&](const QUrl &, bool) {
            ++malformedFanartCalls;
            ArtworkHttp::Response result;
            result.status = 200;
            result.body = malformedFanartBodies[i];
            return result;
        }, cache(QStringLiteral("fanart-malformed-%1").arg(i)));
        for (int attempt = 0; attempt < 2; ++attempt) {
            const auto artistUrls = malformedFanart.fanartArtistUrls(artistA, &error);
            rejectsMalformedFanart = rejectsMalformedFanart && artistUrls.isEmpty() && error.contains(QStringLiteral("unreadable"));
            const auto albumUrls = malformedFanart.fanartAlbumUrls(albumId, &error);
            rejectsMalformedFanart = rejectsMalformedFanart && albumUrls.isEmpty() && error.contains(QStringLiteral("unreadable"));
        }
    }
    check(rejectsMalformedFanart && malformedFanartCalls == malformedFanartBodies.size() * 4,
          "Fanart malformed JSON, HTML, error objects, and invalid artwork types remain errors and are never cached");
    MusicIdentityClient emptyOtherProviders([](const QUrl &, bool) { return response({}); }, cache(QStringLiteral("empty-other-providers")));
    const auto emptyMbRows = emptyOtherProviders.searchArtists(QStringLiteral("Artist"), QStringLiteral("musicbrainz"), &error);
    const bool rejectsEmptyMb = emptyMbRows.isEmpty() && error.contains(QStringLiteral("unreadable"));
    const auto emptyCaaUrls = emptyOtherProviders.caaAlbumUrls(albumId, &error);
    check(rejectsEmptyMb && emptyCaaUrls.isEmpty() && error.contains(QStringLiteral("unreadable")),
          "Fanart empty-object exception does not weaken MusicBrainz or CAA response validation");

    const auto fanartBrowseArtists = response({{QStringLiteral("artists"), QJsonArray{
        artist(artistA, QStringLiteral("Billie fixture"), QStringLiteral("Singer")),
        artist(artistB, QStringLiteral("Billie fixture"), QStringLiteral("Same-name contributor"))}}});
    const auto fanartBrowseSuccess = response({{QStringLiteral("artistthumb"), QJsonArray{
        QJsonObject{{QStringLiteral("url"), portraitA}}, QJsonObject{{QStringLiteral("url"), portraitA}},
        QJsonObject{{QStringLiteral("url"), portraitB}}}}});
    for (const bool successFirst : {true, false}) {
        for (const int otherStatus : {200, 503}) {
            MusicIdentityClient browse([&](const QUrl &url, bool mb) {
                if (mb) return fanartBrowseArtists;
                const bool firstArtist = url.path().endsWith(artistA);
                return firstArtist == successFirst ? fanartBrowseSuccess : response({}, otherStatus);
            }, cache(QStringLiteral("fanart-browse-%1-%2").arg(successFirst).arg(otherStatus)));
            const auto urls = browse.searchFanartArtistUrls(QStringLiteral("Billie fixture"), &error);
            check(urls == QStringList{portraitA, portraitB} && error.isEmpty(),
                  qPrintable(QStringLiteral("Fanart browse preserves unique portraits with success %1 and another candidate HTTP%2")
                      .arg(successFirst ? QStringLiteral("first") : QStringLiteral("last")).arg(otherStatus)));
        }
    }
    for (const bool failureFirst : {true, false}) {
        MusicIdentityClient browse([&](const QUrl &url, bool mb) {
            if (mb) return fanartBrowseArtists;
            return response({}, url.path().endsWith(artistA) == failureFirst ? 503 : 200);
        }, cache(QStringLiteral("fanart-browse-no-success-%1").arg(failureFirst)));
        check(browse.searchFanartArtistUrls(QStringLiteral("Billie fixture"), &error).isEmpty()
              && error == QStringLiteral("Fanart.tv is busy. Try again shortly."),
              qPrintable(QStringLiteral("Fanart browse retains actionable error when no portraits and failed candidate is %1")
                  .arg(failureFirst ? QStringLiteral("first") : QStringLiteral("last"))));
    }
    MusicIdentityClient noBrowseArtwork([&](const QUrl &, bool mb) {
        return mb ? fanartBrowseArtists : response({});
    }, cache(QStringLiteral("fanart-browse-all-missing")));
    check(noBrowseArtwork.searchFanartArtistUrls(QStringLiteral("Billie fixture"), &error).isEmpty() && error.isEmpty(),
          "Fanart browse with no artwork for any candidate returns a clean empty result");

    int brokenCalls = 0;
    MusicIdentityClient broken([&](const QUrl &, bool) { ++brokenCalls; return response({{QStringLiteral("artists"), QStringLiteral("not an array")}}); }, cache(QStringLiteral("broken")));
    check(broken.searchArtists(QStringLiteral("Name"), QStringLiteral("musicbrainz"), &error).isEmpty() && error.contains(QStringLiteral("unreadable")),
          "malformed provider data returns a concrete parsing error");
    broken.searchArtists(QStringLiteral("Name"), QStringLiteral("musicbrainz"), &error);
    check(brokenCalls == 2, "invalid provider responses are not persisted as successful search results");
    MusicIdentityClient busy([](const QUrl &, bool) { return response({}, 503); }, cache(QStringLiteral("busy")));
    check(busy.searchAlbums(QStringLiteral("Album"), {}, &error).isEmpty() && error == QLatin1String("MusicBrainz is busy. Try again shortly."),
          "provider overload remains distinct from empty search results");

    const QByteArray customBytes = jpeg(Qt::magenta), oldBytes = jpeg(Qt::red), newBytes = jpeg(Qt::green);
    {
        QSemaphore entered, release;
        ArtistImageService service([&](const QString &, const QVariantMap &) { entered.release(); release.acquire(); return portraitA; },
            [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("custom-race")));
        int imageSignals = 0;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &, const QString &, bool) { ++imageSignals; });
        service.requestImage(QStringLiteral("Artist"), selected);
        check(waitUntil([&] { return entered.available() > 0; }), "portrait lookup runs on a worker while UI remains responsive");
        service.invalidate(QStringLiteral("Artist"));
        const QString path = service.cachePathFor(QStringLiteral("Artist"));
        writeFile(path, customBytes);
        release.release(); finishWorker();
        check(readFile(path) == customBytes && imageSignals == 0,
              "Apply invalidation suppresses late automatic completion and preserves custom artwork bytes");
    }
    {
        QSemaphore entered, release;
        std::atomic<int> attempts = 0;
        ArtistImageService service([&](const QString &, const QVariantMap &identity) {
            if (++attempts == 1) { entered.release(); release.acquire(); }
            return identity.value(QStringLiteral("providerId")) == artistA ? portraitA : portraitB;
        }, [&](const QUrl &url) { return url.toString() == portraitA ? oldBytes : newBytes; }, cache(QStringLiteral("replace-race")));
        int imageSignals = 0;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &, const QString &, bool ok) { if (ok) ++imageSignals; });
        service.requestImage(QStringLiteral("Artist"), selected);
        waitUntil([&] { return entered.available() > 0; });
        service.requestImage(QStringLiteral("Artist"), {{QStringLiteral("provider"), QStringLiteral("musicbrainz")}, {QStringLiteral("providerId"), artistB}});
        release.release();
        check(waitUntil([&] { return imageSignals == 1; }) && attempts == 2
              && QImage(service.cachePathFor(QStringLiteral("Artist"))).pixelColor(0, 0).green() > 200,
              "identity change during lookup discards old generation and publishes only the new portrait");
    }
    {
        QSemaphore entered, release;
        std::atomic<int> attempts = 0;
        ArtistImageService service([&](const QString &, const QVariantMap &) {
            if (++attempts == 1) { entered.release(); release.acquire(); return portraitA; }
            return portraitB;
        }, [&](const QUrl &url) { return url.toString() == portraitA ? oldBytes : newBytes; }, cache(QStringLiteral("reset-race")));
        int imageSignals = 0;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &, const QString &, bool ok) { if (ok) ++imageSignals; });
        service.requestImage(QStringLiteral("Artist"));
        waitUntil([&] { return entered.available() > 0; });
        service.invalidate(QStringLiteral("Artist"));
        service.requestImage(QStringLiteral("Artist"));
        release.release();
        check(waitUntil([&] { return imageSignals == 1; }) && attempts == 2
              && QImage(service.cachePathFor(QStringLiteral("Artist"))).pixelColor(0, 0).green() > 200,
              "reset can restart the same identity while a stale request is finishing");
    }
    {
        QSemaphore entered, release;
        std::atomic<int> attempts = 0;
        const QString canonical = QStringLiteral("Authoritative canonical artist");
        ArtistImageService service([&](const QString &, const QVariantMap &identity) {
            if (++attempts == 1) { entered.release(); release.acquire(); }
            return identity.value(QStringLiteral("providerId")) == artistA ? portraitA : portraitB;
        }, [&](const QUrl &url) { return url.toString() == portraitA ? oldBytes : newBytes; }, cache(QStringLiteral("pause-canonical")));
        QHash<QString, int> completed;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &name, const QString &, bool ok) {
            if (ok) ++completed[name];
        });
        service.requestImage(canonical, selected);
        waitUntil([&] { return entered.available() > 0; });
        service.requestImage(QStringLiteral("Already queued"), selected);
        service.setCustomizationPaused(true);
        service.requestImage(QStringLiteral("Queued during Apply"), selected);
        release.release(); finishWorker();
        waitUntil([] { return false; }, 350);
        check(attempts == 1 && completed.isEmpty() && service.cachedPath(canonical).isEmpty(),
              "artist Apply pause suppresses an unknown final-name completion and holds queued work");
        service.invalidate(canonical);
        service.requestImage(canonical, {{QStringLiteral("provider"), QStringLiteral("musicbrainz")}, {QStringLiteral("providerId"), artistB}});
        service.setCustomizationPaused(false);
        check(waitUntil([&] { return completed.size() == 3; }) && attempts == 4
              && completed.value(canonical) == 1
              && completed.value(QStringLiteral("Already queued")) == 1
              && completed.value(QStringLiteral("Queued during Apply")) == 1
              && QImage(service.cachePathFor(canonical)).pixelColor(0, 0).green() > 200,
              "resume publishes only committed canonical identity and preserves requests queued before and during Apply");
    }
    {
        QSemaphore entered, release;
        std::atomic<int> attempts = 0;
        ArtistImageService service([&](const QString &, const QVariantMap &) {
            if (++attempts == 1) { entered.release(); release.acquire(); }
            return portraitA;
        }, [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("pause-failed-apply")));
        QHash<QString, int> completed;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &name, const QString &, bool ok) {
            if (ok) ++completed[name];
        });
        service.requestImage(QStringLiteral("Active"), selected);
        waitUntil([&] { return entered.available() > 0; });
        service.requestImage(QStringLiteral("Waiting"), selected);
        service.setCustomizationPaused(true);
        service.setCustomizationPaused(true);
        service.setCustomizationPaused(false);
        service.setCustomizationPaused(true);
        release.release(); finishWorker();
        check(attempts == 1 && completed.isEmpty(),
              "repeated pause and resume while stale worker runs cannot publish or duplicate work");
        service.setCustomizationPaused(false);
        check(waitUntil([&] { return completed.size() == 2; }) && attempts == 3
              && completed.value(QStringLiteral("Active")) == 1
              && completed.value(QStringLiteral("Waiting")) == 1
              && QImage(service.cachePathFor(QStringLiteral("Active"))).pixelColor(0, 0).red() > 200,
              "failed Apply resume retries the active portrait and retains unrelated queued work exactly once");
    }
    {
        ArtistImageService service([&](const QString &, const QVariantMap &) { return portraitA; },
            [](const QUrl &) { return QByteArray(2000, '<'); }, cache(QStringLiteral("bad-image")));
        bool finished = false, ok = true;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &, const QString &, bool result) { finished = true; ok = result; });
        service.requestImage(QStringLiteral("Artist"));
        check(waitUntil([&] { return finished; }) && !ok && service.cachedPath(QStringLiteral("Artist")).isEmpty(),
              "malformed image responses never become a persistent portrait cache entry");
    }
    {
        QSemaphore entered, release;
        ArtistImageService service([&](const QString &, const QVariantMap &) { entered.release(); release.acquire(); return portraitA; },
            [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("existing-race")));
        bool finished = false;
        QObject::connect(&service, &ArtistImageService::imageReady, &app, [&](const QString &, const QString &, bool) { finished = true; });
        service.requestImage(QStringLiteral("Artist"));
        waitUntil([&] { return entered.available() > 0; });
        const QString path = service.cachePathFor(QStringLiteral("Artist"));
        writeFile(path, customBytes);
        release.release();
        check(waitUntil([&] { return finished; }) && readFile(path) == customBytes,
              "atomic automatic install preserves a custom cache that appeared during download even without invalidation");
    }
    {
        QSemaphore entered, release;
        auto *service = new ArtistImageService([&](const QString &, const QVariantMap &) { entered.release(); release.acquire(); return portraitA; },
            [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("destroy-race")));
        const QString path = service->cachePathFor(QStringLiteral("Artist"));
        service->requestImage(QStringLiteral("Artist"));
        waitUntil([&] { return entered.available() > 0; });
        delete service;
        release.release(); finishWorker();
        check(!QFile::exists(path), "destroying the service during lookup cannot publish an orphaned late result");
    }
    {
        std::atomic<int> attempts{0};
        ArtistImageService service([&](const QString &, const QVariantMap &) {
            return ++attempts == 1 ? QString() : portraitA;
        }, [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("retry-recovery")), nullptr, 20);
        bool recovered = false;
        QObject::connect(&service, &ArtistImageService::imageReady, &app,
            [&](const QString &, const QString &, bool ok) { recovered = ok; });
        service.requestImage(QStringLiteral("Retry artist"));
        check(waitUntil([&] { return recovered; }) && attempts == 2,
              "failed automatic portrait retries and recovers without recreating the card");
    }
    {
        std::atomic<int> attempts{0};
        ArtistImageService service([&](const QString &, const QVariantMap &) {
            ++attempts; return QString();
        }, [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("retry-bound")), nullptr, 20);
        service.requestImage(QStringLiteral("Missing portrait"));
        check(waitUntil([&] { return attempts == 2; }), "missing portrait receives one delayed retry");
        waitUntil([] { return false; }, 700);
        check(attempts == 2, "missing portraits do not create an endless retry loop");
    }
    {
        std::atomic<int> attempts{0}; bool failed = false;
        ArtistImageService service([&](const QString &, const QVariantMap &) {
            ++attempts; return QString();
        }, [&](const QUrl &) { return oldBytes; }, cache(QStringLiteral("retry-invalidation")), nullptr, 100);
        QObject::connect(&service, &ArtistImageService::imageReady, &app,
            [&](const QString &, const QString &, bool) { failed = true; });
        service.requestImage(QStringLiteral("Changed artist"));
        check(waitUntil([&] { return failed; }), "portrait failure schedules a retry");
        service.invalidate(QStringLiteral("Changed artist"));
        waitUntil([] { return false; }, 500);
        check(attempts == 1, "identity invalidation cancels the old scheduled retry");
    }
    {
        auto group = [&](const QString &id, const QString &title, const QString &owner) {
            return QJsonObject{{"id", id}, {"title", title},
                {"artist-credit", QJsonArray{QJsonObject{{"name", owner}, {"artist", artist(artistA, owner)}}}}};
        };
        auto run = [&](const QJsonArray &groups, const QString &owner, const QString &title,
                       const QVariantMap &identity = QVariantMap()) {
            MusicIdentityClient client([&](const QUrl &url, bool) {
                if (url.host() == QLatin1String("musicbrainz.org"))
                    return response({{"release-groups", groups}});
                return response({{"images", QJsonArray{QJsonObject{{"front", true}, {"image", portraitA}}}}});
            }, cache(QUuid::createUuid().toString()));
            return client.automaticAlbumImageUrl(owner, title, identity);
        };
        check(run({group(albumId, "Album", "Artist")}, "Artist", "Album") == portraitA,
              "automatic album cover requires an exact artist and album match");
        check(run({group(albumId, "Album", "Wrong Artist")}, "Artist", "Album").isEmpty(),
              "same album title from a different artist cannot supply a cover");
        check(run({group(albumId, "Album", "Artist")}, "Artist", "Album (Deluxe)").isEmpty(),
              "automatic cover lookup never strips deluxe qualifiers");
        check(run({group(albumId, "Album", "Artist"), group(artistB, "Album", "Artist")}, "Artist", "Album").isEmpty(),
              "multiple exact release groups remain ambiguous");
        check(run({}, "Artist", "Renamed album", {{"provider", "musicbrainz"}, {"providerId", albumId}}) == portraitA,
              "saved album provider identity takes precedence over a renamed title");
        check(run({}, "Artist", "Album", {{"provider", "musicbrainz"}, {"providerId", "invalid"}}).isEmpty(),
              "invalid saved album identities cannot silently fall back to names");
    }
    {
        std::atomic<int> attempts{0};
        OnlineAlbumArtService service([&](const QString &, const QString &, const QVariantMap &) {
            ++attempts; return oldBytes;
        }, cache(QStringLiteral("album-online")));
        int completed = 0;
        QObject::connect(&service, &OnlineAlbumArtService::finished, &app,
            [&](const QString &, const QString &, const QString &, bool ok) { if (ok) ++completed; });
        service.request("Artist", "Album"); service.request("Artist", "Album");
        check(waitUntil([&] { return completed == 1; }) && attempts == 1,
              "duplicate online album requests share one download");
        service.request("Artist", "Album");
        check(completed == 2 && attempts == 1, "cached covers bypass online providers");
    }
    {
        QSemaphore entered, release;
        const QString dir = cache(QStringLiteral("album-custom-race"));
        OnlineAlbumArtService service([&](const QString &, const QString &, const QVariantMap &) {
            entered.release(); release.acquire(); return oldBytes;
        }, dir);
        bool done = false;
        QObject::connect(&service, &OnlineAlbumArtService::finished, &app,
            [&](const QString &, const QString &, const QString &, bool) { done = true; });
        service.request("Artist", "Album");
        waitUntil([&] { return entered.available() > 0; });
        const QString path = dir + '/' + AlbumArtService::cacheKey("Artist", "Album") + ".jpg";
        writeFile(path, customBytes);
        release.release();
        check(waitUntil([&] { return done; }) && readFile(path) == customBytes,
              "late online album result preserves local or custom artwork published during download");
    }
    {
        QSemaphore entered, release;
        const QString dir = cache(QStringLiteral("album-invalidated"));
        OnlineAlbumArtService service([&](const QString &, const QString &, const QVariantMap &) {
            entered.release(); release.acquire(); return oldBytes;
        }, dir);
        service.request("Artist", "Album");
        waitUntil([&] { return entered.available() > 0; });
        service.invalidate("Artist", "Album"); release.release(); finishWorker();
        check(!QFile::exists(dir + '/' + AlbumArtService::cacheKey("Artist", "Album") + ".jpg"),
              "removed album or changed local sources invalidates an in-flight cover download");
    }
    {
        std::atomic<int> attempts{0};
        OnlineAlbumArtService service([&](const QString &, const QString &, const QVariantMap &) {
            ++attempts; return QByteArray("not an image");
        }, cache(QStringLiteral("album-failure")));
        int completed = 0;
        QObject::connect(&service, &OnlineAlbumArtService::finished, &app,
            [&](const QString &, const QString &, const QString &, bool) { ++completed; });
        service.request("Artist", "Album");
        check(waitUntil([&] { return completed == 1; }), "invalid online cover reports failure");
        service.request("Artist", "Album"); finishWorker();
        check(attempts == 1, "failed cover lookups observe cooldown");
        service.retryFailures(); service.request("Artist", "Album");
        check(waitUntil([&] { return completed == 2; }) && attempts == 2,
              "explicit missing-art retry makes another attempt without deleting existing covers");
    }
    {
        QSemaphore entered, release;
        std::atomic<int> attempts{0};
        const QString dir = cache(QStringLiteral("album-pause"));
        OnlineAlbumArtService service([&](const QString &, const QString &, const QVariantMap &) {
            if (++attempts == 1) { entered.release(); release.acquire(); }
            return oldBytes;
        }, dir);
        int completed = 0;
        QObject::connect(&service, &OnlineAlbumArtService::finished, &app,
            [&](const QString &, const QString &, const QString &, bool) { ++completed; });
        service.request("Artist", "Album");
        waitUntil([&] { return entered.available() > 0; });
        service.setPaused(true); release.release(); finishWorker();
        check(completed == 0 && !QFile::exists(dir + '/' + AlbumArtService::cacheKey("Artist", "Album") + ".jpg"),
              "Customize pause prevents an active album download from publishing");
        service.setPaused(false);
        check(waitUntil([&] { return completed == 1; }) && attempts == 2,
              "resuming after Customize retains the pending album request");
    }
    {
        QSemaphore entered, release;
        const QString dir = cache(QStringLiteral("album-destroy"));
        auto *service = new OnlineAlbumArtService([&](const QString &, const QString &, const QVariantMap &) {
            entered.release(); release.acquire(); return oldBytes;
        }, dir);
        service->request("Artist", "Album");
        waitUntil([&] { return entered.available() > 0; });
        delete service; release.release(); finishWorker();
        check(!QFile::exists(dir + '/' + AlbumArtService::cacheKey("Artist", "Album") + ".jpg"),
              "destroyed online album service cannot publish a late result");
    }
    {
        auto lookup = [&](bool conflicting, bool incomplete, const QStringList &albums) {
            MusicIdentityClient client([&](const QUrl &url, bool) {
                if (url.path().contains("release-group")) {
                    const bool second = QUrlQuery(url).queryItemValue("query").contains("Second");
                    const QString owner = conflicting && second ? artistB : artistA;
                    return response({{"count", incomplete ? 30 : 1}, {"release-groups", QJsonArray{
                        QJsonObject{{"id", albumId}, {"title", second ? "Second" : "First"},
                            {"artist-credit", QJsonArray{QJsonObject{{"name", "Shared"}, {"artist", artist(owner, "Shared")}}}}}
                    }}});
                }
                if (url.host() == QLatin1String("musicbrainz.org"))
                    return response({{"artists", QJsonArray{artist(artistA, "Shared"), artist(artistB, "Shared")}}});
                return response({{"artistthumb", QJsonArray{QJsonObject{{"url", portraitA}}}}});
            }, cache(QUuid::createUuid().toString()));
            return client.automaticArtistImageUrl("Shared", {{"_libraryAlbums", albums}});
        };
        check(lookup(false, false, {"First"}) == portraitA,
              "exact local album credit resolves a same-name artist without popularity guessing");
        check(lookup(true, false, {"First", "Second"}).isEmpty(),
              "conflicting album credits cannot resolve a same-name artist");
        check(lookup(false, true, {"First"}).isEmpty(),
              "incomplete album search cannot establish artist identity");
        check(lookup(false, false, {}).isEmpty(),
              "artist ambiguity without album evidence still requires a choice");
    }
    finishWorker();
    {
        const QString releaseA = "44444444-4444-4444-8444-444444444444";
        const QString releaseB = "55555555-5555-4555-8555-555555555555";
        auto exercise = [&](QString title, bool twoReleases, bool truncated, bool outage, bool joint, QVariantMap evidence) {
            MusicIdentityClient client([&](const QUrl &url, bool) {
                if (outage) return response({}, 503);
                auto owner = artist(artistA, "“Weird Al” Yankovic");
                owner.insert("aliases", QJsonArray{QJsonObject{{"name", "Weird Al Yankovic"}}});
                QJsonArray credits{QJsonObject{{"artist", owner}}};
                if (joint) credits.append(QJsonObject{{"artist", artist(artistB, "Guest")}});
                if (url.path() == "/ws/2/release-group/") return response({{"count", truncated ? 50 : 1}, {"release-groups", QJsonArray{
                    QJsonObject{{"id", albumId}, {"title", "Poodle Hat"}, {"artist-credit", credits}}}}});
                if (url.path() == "/ws/2/release") {
                    QJsonArray rows{QJsonObject{{"id", releaseA}}};
                    if (twoReleases) rows.append(QJsonObject{{"id", releaseB}});
                    return response({{"release-count", rows.size()}, {"releases", rows}});
                }
                if (url.path().startsWith("/ws/2/release/")) {
                    QJsonArray tracks;
                    for (int i = 1; i <= 3; ++i) tracks.append(QJsonObject{{"position", i}, {"title", QString("Song %1").arg(i)}, {"length", 180000}});
                    return response({{"id", url.path().section('/', -1)}, {"title", "Poodle Hat"}, {"artist-credit", credits},
                        {"release-group", QJsonObject{{"id", albumId}}}, {"date", "2003-05-20"},
                        {"media", QJsonArray{QJsonObject{{"position", 1}, {"format", "CD"}, {"track-count", 3}, {"tracks", tracks}}}}});
                }
                return response({{"images", QJsonArray{QJsonObject{{"front", true}, {"image", "https://archive.org/cover.jpg"}}}}});
            }, cache(QUuid::createUuid().toString()));
            return client.resolveAlbumArtwork("Weird Al Yankovic", title, evidence);
        };
        const auto isGeneral = [](const MusicIdentityClient::MatchResult &result) {
            return result.status == "ready" && result.identity.value("artworkScope") == "generalAlbum"
                && !result.identity.contains("releaseId");
        };
        const auto alias = exercise("Poodle Hat", false, false, false, false, {});
        check(isGeneral(alias) && alias.identity.value("releaseGroupId") == albumId,
              "Poodle Hat credit alias establishes album concept without claiming an exact edition");
        check(isGeneral(exercise("Poodle Hat [Deluxe]", false, false, false, false, {})),
              "recognized edition suffix downloads general cover without claiming an exact edition");
        const auto approvedGeneral = exercise("Poodle Hat [Deluxe]", false, false, false, false,
            {{"releaseGroupId", albumId}, {"scope", "general"}, {"choice", "manual"}});
        check(approvedGeneral.status == "ready" && approvedGeneral.identity.value("artworkScope") == "generalAlbum"
              && !approvedGeneral.identity.contains("releaseId"),
              "explicitly confirmed general cover can reload without claiming an exact edition");
        check(exercise("Poodle Hat (Live at Home)", false, false, false, false, {}).status == "needsMatch",
              "meaningful parenthetical title is never stripped");
        check(exercise("Poodle Hat", false, false, false, true, {}).status == "needsMatch",
              "individual artist alias cannot claim a collaboration credit");
        check(exercise("Poodle Hat", false, true, false, false, {}).status == "needsMatch",
              "truncated album search cannot select a concept automatically");
        check(exercise("Poodle Hat", false, false, true, false, {}).status == "providerUnavailable",
              "provider failure differs from missing artwork and ambiguity");
        QVariantList tracks;
        for (int i = 1; i <= 3; ++i) tracks.append(QVariantMap{{"title", QString("Song %1").arg(i)}, {"track", i}, {"disc", 1}, {"duration", 180000}});
        QVariantMap evidence{{"_libraryTracks", tracks}, {"_libraryYear", 2003}};
        const auto exact = exercise("Poodle Hat", false, false, false, false, evidence);
        check(exact.status == "ready" && exact.identity.value("releaseId") == releaseA && exact.identity.value("artworkScope") == "exactRelease",
              "complete matching tracks dates and disc positions establish exact release artwork");
        check(isGeneral(exercise("Poodle Hat", true, false, false, false, evidence)),
              "equally supported releases use general cover without arbitrary exact release");
        check(isGeneral(exercise("Poodle Hat [Clean]", false, false, false, false, evidence)),
              "unsupported clean evidence never certifies a standard release");
        auto extraTracks = tracks;
        extraTracks.append(QVariantMap{{"title", "Bonus song"}, {"track", 4}, {"disc", 1}});
        auto extraEvidence = evidence; extraEvidence.insert("_libraryTracks", extraTracks);
        check(isGeneral(exercise("Poodle Hat", false, false, false, false, extraEvidence)),
              "extra positive local track position blocks false exact release");
        extraTracks = tracks; extraTracks.append(QVariantMap{{"title", "Song 1"}, {"track", 1}, {"disc", 2}});
        extraEvidence.insert("_libraryTracks", extraTracks);
        check(isGeneral(exercise("Poodle Hat", false, false, false, false, extraEvidence)),
              "additional local disc blocks a release that lacks that disc");
        extraTracks = tracks; extraTracks.append(tracks.first()); extraEvidence.insert("_libraryTracks", extraTracks);
        check(isGeneral(exercise("Poodle Hat", false, false, false, false, extraEvidence)),
              "duplicate local positions cannot inflate matching confidence");
        auto generalEvidence = evidence; generalEvidence.insert("releaseGroupId", albumId);
        generalEvidence.insert("releaseId", releaseA); generalEvidence.insert("choice", "manual"); generalEvidence.insert("scope", "general");
        const auto confirmed = exercise("Poodle Hat", false, false, false, false, generalEvidence);
        check(confirmed.status == "ready" && confirmed.identity.value("artworkScope") == "generalAlbum"
              && !confirmed.identity.contains("releaseId"), "confirmed general scope wins over stale saved release ID");
        auto wrongTracks = tracks; auto wrong = wrongTracks.first().toMap(); wrong.insert("title", "Other song"); wrongTracks[0] = wrong;
        evidence.insert("_libraryTracks", wrongTracks);
        check(isGeneral(exercise("Poodle Hat", false, false, false, false, evidence)),
              "conflicting track title blocks exact release selection");
        evidence.insert("releaseId", releaseA); evidence.insert("releaseGroupId", albumId);
        check(exercise("Poodle Hat", false, false, false, false, evidence).status == "ready",
              "saved release identity takes precedence over incomplete or altered local tags");
    }
    {
        auto resolve = [&](bool duplicate, bool incomplete, bool portrait, bool outage, QVariantMap context = {}) {
            MusicIdentityClient client([&](const QUrl &url, bool) {
                if (outage) return response({}, 503);
                auto owner = artist(artistA, "“Weird Al” Yankovic");
                owner.insert("aliases", QJsonArray{QJsonObject{{"name", "Weird Al Yankovic"}}});
                if (url.path() == "/ws/2/artist/") {
                    QJsonArray rows{owner}; if (duplicate) rows.append(artist(artistB, "Weird Al Yankovic"));
                    return response({{"count", incomplete ? 30 : rows.size()}, {"artists", rows}});
                }
                if (url.path().startsWith("/ws/2/artist/")) return response(owner);
                if (url.host() == "webservice.fanart.tv")
                    return response(portrait ? QJsonObject{{"artistthumb", QJsonArray{QJsonObject{{"url", portraitA}}}}} : QJsonObject{});
                return response({});
            }, cache(QUuid::createUuid().toString()));
            return client.resolveArtistArtwork("Weird Al Yankovic", context);
        };
        auto ready = resolve(false, false, true, false);
        check(ready.status == "ready" && ready.identity.value("artistMbid") == artistA && ready.imageUrl == portraitA,
              "typed artist discovery accepts provider alias and preserves resolved ID");
        auto noArt = resolve(false, false, false, false);
        check(noArt.status == "noArtwork" && noArt.identity.value("artistMbid") == artistA,
              "resolved artist without portrait stays resolved and reports no artwork");
        check(resolve(true, false, true, false).status == "needsMatch",
              "colliding artist aliases need evidence rather than punctuation guessing");
        check(resolve(false, true, true, false).status == "needsMatch",
              "incomplete artist results do not establish unique identity");
        check(resolve(false, false, false, true).status == "providerUnavailable",
              "artist transport failure is not reported as missing portrait");
        check(resolve(true, true, true, false, {{"provider", "musicbrainz"}, {"providerId", artistA}}).status == "ready",
              "manual artist ID bypasses ambiguous name search");
    }
    printf("Music identity/provider fixture: %d checks, %d failures; no live providers or library\n", checks, failures);
    return failures ? 1 : 0;
}
