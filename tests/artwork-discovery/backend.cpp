#include "library/AlbumArtService.h"
#include "library/ArtistImageService.h"
#include "library/ArtworkHttp.h"
#include "library/OnlineAlbumArtService.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <cstdio>
extern "C" int zuuned_extract_art(const char *, const char *) { return -1; }
ArtworkHttp::Response ArtworkHttp::get(const QUrl &, bool) { std::abort(); }
QByteArray ArtworkHttp::getBody(const QUrl &, bool, QString *) { std::abort(); }
static int checks = 0, failures = 0;
void check(bool ok, const char *what) {
  ++checks;
  if (!ok)
    ++failures;
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}
bool wait(const std::function<bool()> &done, int ms = 2500) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < ms) {
    QCoreApplication::processEvents();
    if (done())
      return true;
    QThread::msleep(2);
  }
  return done();
}
void drain(int ms) {
  wait([] { return false; }, ms);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  QImage image(10, 10, QImage::Format_RGB32);
  image.fill(Qt::red);
  QByteArray bytes;
  QBuffer b(&bytes);
  b.open(QIODevice::WriteOnly);
  image.save(&b, "PNG");
  for (const QString status : {QString("needsMatch"), QString("noArtwork"),
                               QString("generalCoverAvailable")}) {
    std::atomic<int> calls = 0, downloads = 0;
    int results = 0;
    QVariantMap last;
    OnlineAlbumArtService service(
        OnlineAlbumArtService::TypedResults::Enabled,
        [&](const QString &, const QString &, const QVariantMap &) {
          ++calls;
          return QVariantMap{{"status", status},
                             {"imageUrl", "https://example.org/art"},
                             {"identity", QVariantMap{{"providerId", "test"}}}};
        },
        [&](const QUrl &) {
          ++downloads;
          return bytes;
        },
        root.path() + "/" + status, nullptr, 20);
    QObject::connect(
        &service, &OnlineAlbumArtService::discoveryResult,
        [&](const QString &, const QString &, const QVariantMap &r) {
          last = r;
          ++results;
        });
    service.request("artist", "album");
    check(wait([&] { return last.value("status") == status; }),
          "typed album terminal status delivered");
    service.request("artist", "album");
    drain(50);
    check(calls == 1 && downloads == 0,
          "terminal album does not retry or download general artwork");
    service.request("artist", "album", {{"year", 2003}});
    check(wait([&] { return calls == 2; }),
          "changed album evidence retries terminal result");
    QThreadPool::globalInstance()->waitForDone();
  }
  {
    std::atomic<int> calls = 0;
    int results = 0;
    OnlineAlbumArtService service(
        OnlineAlbumArtService::TypedResults::Enabled,
        [&](const QString &, const QString &, const QVariantMap &) {
          ++calls;
          return QVariantMap{{"status", "providerUnavailable"}};
        },
        [&](const QUrl &) { return bytes; }, root.path() + "/retry", nullptr,
        20);
    QObject::connect(&service, &OnlineAlbumArtService::finished,
                     [&](auto, auto, auto, bool) { ++results; });
    service.request("artist", "album");
    check(wait([&] { return results == 2; }),
          "album provider failure retries once");
    drain(60);
    service.request("artist", "album");
    drain(30);
    check(calls == 2,
          "album automatic retries remain bounded after further requests");
    service.retryFailures();
    service.request("artist", "album");
    check(wait([&] { return calls >= 3; }),
          "explicit retry releases album failure");
    QThreadPool::globalInstance()->waitForDone();
  }
  {
    std::atomic<int> calls = 0, downloads = 0;
    QVariantMap last;
    ArtistImageService service(
        ArtistImageService::TypedResults::Enabled,
        [&](const QString &, const QVariantMap &) {
          ++calls;
          return QVariantMap{{"status", "needsMatch"}};
        },
        [&](const QUrl &) {
          ++downloads;
          return bytes;
        },
        root.path() + "/artist", nullptr, 20);
    QObject::connect(&service, &ArtistImageService::discoveryResult,
                     [&](auto, const QVariantMap &r) { last = r; });
    service.requestImage("artist");
    check(wait([&] { return last.value("status") == "needsMatch"; }),
          "artist typed unresolved status delivered");
    drain(400);
    service.requestImage("artist");
    drain(40);
    check(calls == 1 && downloads == 0,
          "artist terminal failure does not retry");
    service.forgetFailure("artist");
    service.requestImage("artist");
    check(wait([&] { return calls == 2; }),
          "manual artist retry clears terminal result");
    QThreadPool::globalInstance()->waitForDone();
  }
  for (const QString scope : {QString("exactRelease"), QString("generalAlbum")}) {
    QVariantMap last;
    bool readyFile = false;
    const QString dir = root.path() + "/ready-" + scope;
    OnlineAlbumArtService service(
        OnlineAlbumArtService::TypedResults::Enabled,
        [&](const QString &, const QString &, const QVariantMap &) {
          return QVariantMap{
              {"status", "ready"},
              {"identity", QVariantMap{{"providerId", "release"}, {"artworkScope", scope}}},
              {"imageUrl", "https://example.org/art"}};
        },
        [&](const QUrl &) { return bytes; }, dir);
    QObject::connect(&service, &OnlineAlbumArtService::discoveryResult,
                     [&](auto, auto, const QVariantMap &r) {
                       last = r;
                       if (r.value("status") == "ready")
                         readyFile = QFile::exists(
                             dir + "/" +
                             AlbumArtService::cacheKey("artist", "album") +
                             ".jpg");
                     });
    service.request("artist", "album");
    check(wait([&] { return last.value("status") == "ready"; }),
          "typed ready album publishes");
    check(readyFile &&
              last.value("identity").toMap().value("providerId") == "release"
              && last.value("identity").toMap().value("artworkScope") == scope,
          "identity accompanies current result only after publication");
  }
  {
    std::atomic<int> calls = 0, downloads = 0;
    QVariantMap last;
    ArtistImageService service(
        ArtistImageService::TypedResults::Enabled,
        [&](const QString &, const QVariantMap &) {
          ++calls;
          return QVariantMap{{"status", "noArtwork"}};
        },
        [&](const QUrl &) {
          ++downloads;
          return bytes;
        },
        root.path() + "/unknown", nullptr, 20);
    QObject::connect(
        &service, &ArtistImageService::discoveryResult,
        [&](const QString &, const QVariantMap &result) { last = result; });
    service.requestImage("Unknown Artist");
    check(last.value("status") == "needsMatch" &&
              !last.value("reason").toString().isEmpty(),
          "unknown artist reports missing metadata without a provider request");
    drain(40);
    check(calls == 0 && downloads == 0,
          "unknown artist never calls provider or schedules retry");
    service.requestImage("Unknown Artist",
                         {{"providerId", "explicit-portrait-id"}});
    check(wait([&] { return last.value("status") == "noArtwork"; }) &&
              calls == 1,
          "explicit portrait identity can resolve despite unknown artist "
          "metadata");
    QThreadPool::globalInstance()->waitForDone();
  }
  QThreadPool::globalInstance()->waitForDone();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
