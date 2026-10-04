#include "library/MusicIdentityClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <vector>

// Every network response is injected. This fixture never invokes transport.
ArtworkHttp::Response ArtworkHttp::get(const QUrl &, bool) { std::abort(); }

static ArtworkHttp::Response good(QString name = "Cached Artist") {
    ArtworkHttp::Response response; response.status = 200;
    response.body = QJsonDocument(QJsonObject{{"artists", QJsonArray{
        QJsonObject{{"id", "11111111-1111-4111-8111-111111111111"}, {"name", name}}}}}).toJson();
    return response;
}
static ArtworkHttp::Response failed(int status) {
    ArtworkHttp::Response response; response.status = status; return response;
}
static QByteArray read(const QString &path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static QString onlyCacheFile(const QString &directory) {
    const auto files = QDir(directory).entryList({"*.json"}, QDir::Files);
    return files.isEmpty() ? QString() : QDir(directory).absoluteFilePath(files.first());
}
static bool expire(const QString &directory, qint64 ageAfterExpiry = 1000) {
    const QString path = onlyCacheFile(directory);
    auto envelope = QJsonDocument::fromJson(read(path)).object();
    if (envelope.isEmpty()) return false;
    envelope.insert("expires", QDateTime::currentMSecsSinceEpoch() - ageAfterExpiry);
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(QJsonDocument(envelope).toJson()) > 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root("/tmp/zuuned-music-cache-XXXXXX");
    if (!root.isValid()) return 2;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *label) { ++checks; if (!ok) ++failures;
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label); fflush(stdout); };
    auto directory = [&](QString name) { return root.path() + '/' + name; };
    QString error;

    // Start the real cooldown clocks now; other tests run during the wait.
    int cooldownCalls = 0, retryCalls = 0, dateCalls = 0;
    MusicIdentityClient cooldown([&](const QUrl &, bool) {
        return ++cooldownCalls == 1 ? failed(503) : good("Recovered");
    }, directory("cooldown"));
    MusicIdentityClient retry([&](const QUrl &, bool) {
        ++retryCalls; auto result = failed(429); result.retryAfter = "60"; return result;
    }, directory("retry-after"));
    MusicIdentityClient retryDate([&](const QUrl &, bool) {
        ++dateCalls; auto result = failed(503);
        result.retryAfter = QDateTime::currentDateTimeUtc().addSecs(60)
            .toString("ddd, dd MMM yyyy HH:mm:ss 'GMT'").toLatin1(); return result;
    }, directory("retry-date"));
    QElapsedTimer cooldownClock; cooldownClock.start();
    cooldown.searchArtists("Clock", "musicbrainz", &error);
    retry.searchArtists("Clock", "musicbrainz", &error);
    retryDate.searchArtists("Clock", "musicbrainz", &error);

    {
        std::atomic_int calls = 0;
        QSemaphore entered, release, started;
        MusicIdentityClient client([&](const QUrl &, bool mb) {
            if (!mb) std::abort();
            ++calls; entered.release(); release.tryAcquire(1, 5000); return good();
        }, directory("coalesced"));
        std::vector<std::future<QPair<QVariantList, QString>>> workers;
        for (int i = 0; i < 8; ++i) workers.emplace_back(std::async(std::launch::async, [&] {
            started.release(); QString message;
            auto result = client.searchArtists("Shared", "musicbrainz", &message);
            return qMakePair(result, message);
        }));
        started.tryAcquire(8, 5000); entered.tryAcquire(1, 5000); QThread::msleep(100);
        check(calls == 1, "concurrent same-URL lookups have only one in-flight transport request");
        release.release(8);
        bool complete = true;
        for (auto &worker : workers) { const auto result = worker.get(); complete &= result.first.size() == 1 && result.second.isEmpty(); }
        check(complete && calls == 1, "all coalesced workers receive the same validated successful result");
    }
    {
        int first = 0, second = 0;
        MusicIdentityClient a([&](const QUrl &, bool) { ++first; return failed(503); }, directory("isolated-a"));
        MusicIdentityClient b([&](const QUrl &, bool) { ++second; return good("Separate cache"); }, directory("isolated-b"));
        a.searchArtists("Same URL", "musicbrainz", &error);
        const auto result = b.searchArtists("Same URL", "musicbrainz", &error);
        check(first == 1 && second == 1 && result.size() == 1 && error.isEmpty(),
              "different cache directories cannot share cached failures or injected responses");
    }
    for (int status : {429, 500, 502, 503, 504}) {
        int calls = 0;
        const QString dir = directory(QStringLiteral("stale-%1").arg(status));
        MusicIdentityClient client([&](const QUrl &, bool) { return ++calls == 1 ? good() : failed(status); }, dir);
        if (client.searchArtists("Existing").size() != 1 || !expire(dir)) return 2;
        const QByteArray old = read(onlyCacheFile(dir));
        error = "old error";
        const auto result = client.searchArtists("Existing", "musicbrainz", &error);
        const auto repeated = client.searchArtists("Existing", "musicbrainz", &error);
        const QByteArray label = QStringLiteral("HTTP%1 reuses valid positive cache without renewing expiry or repeating the failed batch").arg(status).toUtf8();
        check(result.size() == 1 && repeated.size() == 1 && error.isEmpty() && calls == 2
              && read(onlyCacheFile(dir)) == old, label.constData());
    }
    {
        int calls = 0;
        const QString dir = directory("too-old");
        MusicIdentityClient client([&](const QUrl &, bool) { return ++calls == 1 ? good() : failed(503); }, dir);
        client.searchArtists("Old");
        if (!expire(dir, 6 * 24 * 60 * 60 * 1000LL + 1000)) return 2;
        check(client.searchArtists("Old", "musicbrainz", &error).isEmpty() && !error.isEmpty() && calls == 2,
              "positive cache older than seven days total cannot hide a current outage");
    }
    {
        int calls = 0;
        const QString dir = directory("empty");
        MusicIdentityClient client([&](const QUrl &, bool) {
            if (++calls > 1) return failed(503);
            auto result = good(); result.body = "{\"artists\":[]}"; return result;
        }, dir);
        client.searchArtists("Nothing");
        if (!expire(dir)) return 2;
        check(client.searchArtists("Nothing", "musicbrainz", &error).isEmpty() && !error.isEmpty() && calls == 2,
              "expired empty results do not turn an outage into a successful no-results answer");
    }
    for (int status : {401, 403, 404, 410}) {
        int calls = 0;
        const QString dir = directory(QStringLiteral("permanent-%1").arg(status));
        MusicIdentityClient client([&](const QUrl &, bool) {
            ++calls; return calls == 1 ? good() : failed(calls == 2 ? status : 503);
        }, dir);
        client.searchArtists("Removed");
        if (!expire(dir)) return 2;
        const bool rejected = client.searchArtists("Removed", "musicbrainz", &error).isEmpty() && !error.isEmpty();
        const bool staysRejected = client.searchArtists("Removed", "musicbrainz", &error).isEmpty() && !error.isEmpty();
        const QByteArray label = QStringLiteral("HTTP%1 rejects and invalidates old data so a later 503 cannot revive it").arg(status).toUtf8();
        check(rejected && staysRejected && calls == 3 && onlyCacheFile(dir).isEmpty(), label.constData());
    }
    {
        int calls = 0;
        const QString dir = directory("malformed");
        MusicIdentityClient client([&](const QUrl &, bool) {
            ++calls; auto result = good(calls == 3 ? "Fresh" : "Old");
            if (calls == 2) result.body = "not json";
            return result;
        }, dir);
        client.searchArtists("Parse");
        if (!expire(dir)) return 2;
        const bool rejected = client.searchArtists("Parse", "musicbrainz", &error).isEmpty() && error.contains("unreadable");
        const auto recovered = client.searchArtists("Parse", "musicbrainz", &error);
        check(rejected && recovered.size() == 1 && recovered.first().toMap().value("name") == "Fresh"
              && error.isEmpty() && calls == 3, "malformed responses never use stale fallback and a later success clears the error");
    }
    {
        bool all = true;
        for (const auto network : {QNetworkReply::RemoteHostClosedError, QNetworkReply::TimeoutError,
                                  QNetworkReply::OperationCanceledError, QNetworkReply::HostNotFoundError}) {
            int calls = 0;
            const QString dir = directory(QStringLiteral("network-%1").arg(int(network)));
            MusicIdentityClient client([&](const QUrl &, bool) {
                if (++calls == 1) return good();
                auto response = failed(network == QNetworkReply::RemoteHostClosedError ? 200 : 0);
                response.networkError = network; return response;
            }, dir);
            client.searchArtists("Transport");
            if (!expire(dir)) return 2;
            all &= client.searchArtists("Transport", "musicbrainz", &error).size() == 1 && error.isEmpty();
        }
        check(all, "temporary connection/timeout failures can reuse positive cache, including an interrupted HTTP200 body");
    }
    {
        int calls = 0;
        const QString dir = directory("tls");
        MusicIdentityClient client([&](const QUrl &, bool) {
            if (++calls == 1) return good();
            auto response = failed(0); response.networkError = QNetworkReply::SslHandshakeFailedError; return response;
        }, dir);
        client.searchArtists("TLS");
        if (!expire(dir)) return 2;
        check(client.searchArtists("TLS", "musicbrainz", &error).isEmpty() && !error.isEmpty(),
              "TLS security failures do not silently fall back to stale data");
    }
    {
        int calls = 0;
        MusicIdentityClient client([&](const QUrl &, bool) { ++calls; return failed(503); }, directory("cold"));
        const auto first = client.searchArtists("No cache", "musicbrainz", &error);
        const QString firstError = error;
        const auto second = client.searchArtists("No cache", "musicbrainz", &error);
        check(first.isEmpty() && second.isEmpty() && calls == 1
              && firstError == "MusicBrainz is busy. Try again shortly." && error == firstError,
              "a genuine uncached outage stays actionable while immediate retries reuse the short error cooldown");
    }
    // Wait only until the real 30-second cooldown expires; injected HTTP
    // responses and all persistent caches remain local and isolated.
    const qint64 remaining = 30200 - cooldownClock.elapsed();
    if (remaining > 0) QThread::msleep(static_cast<unsigned long>(remaining));
    const auto recovered = cooldown.searchArtists("Clock", "musicbrainz", &error);
    check(recovered.size() == 1 && error.isEmpty() && cooldownCalls == 2,
          "the real 30-second cooldown expires and permits a fresh successful request");
    retry.searchArtists("Clock", "musicbrainz", &error);
    retryDate.searchArtists("Clock", "musicbrainz", &error);
    check(retryCalls == 1 && dateCalls == 1,
          "Retry-After seconds and HTTP dates retain cooldowns longer than the default 30 seconds");
    {
        int calls = 0;
        MusicIdentityClient client([&](const QUrl &, bool) { ++calls; return failed(503); }, directory("bounded"));
        for (int i = 0; i < 513; ++i) client.searchArtists(QString::number(i), "musicbrainz", &error);
        client.searchArtists("512", "musicbrainz", &error);
        const bool newestRetained = calls == 513;
        client.searchArtists("0", "musicbrainz", &error);
        check(newestRetained && calls == 514,
              "transient failure memory is bounded: newest entries remain and oldest entries can be retried");
    }
    printf("Music identity cache: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
