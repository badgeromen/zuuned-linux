#include "player/PlayerService.h"
#include "spoken/SpokenMediaService.h"
#include "spoken/SpokenStore.h"
#include "spoken/PodcastFeed.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUrlQuery>
#include <functional>
#include <memory>
#include <algorithm>
#include <sqlite3.h>

static bool until(const std::function<bool()> &done, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents();
        if (done())
            return true;
        QTest::qWait(10);
    }
    return done();
}
int runPodcastPlannerChecks();

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("SpokenNativeGate");
    app.setOrganizationName("SpokenNativeGate");
    const QString root = qEnvironmentVariable("SPOKEN_GATE_ROOT");
    if (!root.startsWith("/tmp/zuuned-spoken-native-") || qgetenv("ZUUNED_MPV_AO") != "null")
        return 2;
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char *label) {
        ++checks;
        printf("%s %s\n", ok ? "PASS" : "FAIL", label);
        fflush(stdout);
        if (!ok)
            ++failures;
    };
    QString error;
    {
        SpokenStore store(root + "/db-gate");
        check(store.open(&error), "new spoken database opens");
        QVariantMap item{{"kind", "show"}, {"identity", "opaque-feed"}, {"title", "Original"}};
        check(store.begin(&error) && store.put(&item, &error),
              "transaction accepts a stable collection identity");
        store.rollback();
        check(store.rows(&error).isEmpty(), "rollback leaves no partial collection");
        check(store.put(&item, &error), "collection commits");
        QVariantMap duplicate{
            {"kind", "show"}, {"identity", "opaque-feed"}, {"title", "Duplicate"}};
        error.clear();
        check(!store.put(&duplicate, &error), "duplicate feed identity is rejected");
        error.clear();
        check(store.rows(&error).size() == 1, "duplicate rejection preserves original row");
        item["identity"] = "different-feed";
        check(!store.put(&item, &error),
              "existing identity cannot drift away from its database key");
    }
    {
        SpokenStore store(root + "/db-gate");
        error.clear();
        check(store.open(&error) && store.rows(&error).size() == 1, "database survives reopening");
    }
    {
        sqlite3 *db = nullptr;
        sqlite3_open(QFile::encodeName(root + "/db-gate/spoken.db").constData(), &db);
        sqlite3_exec(db, "PRAGMA user_version=99", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        SpokenStore store(root + "/db-gate");
        error.clear();
        check(!store.open(&error), "future schema refused without resetting it");
        error.clear();
        check(!store.open(&error), "retrying open cannot bypass the schema guard");
    }
    QString enclosureSuffix;
    QString episodeNumberXml = "<i:episode>190</i:episode>";
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "loopback fixture server listens");
    QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    int audioRequests = 0;
    // Paged back catalogues (RFC 5005), newest first.
    int pagedCount = 8, cappedCount = 6, repeatCount = 12;
    bool stuckNoNext = false;
    QSet<int> flakyFailing;
    QHash<QString, int> pagedRequests;
    QList<QTcpSocket *> held;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool())
                    return;
                socket->setProperty("answered", true);
                const auto path = request.split(' ').value(1);
                QByteArray body;
                int status = 200;
                if (path == "/stall") {
                    held.append(socket);
                    return;
                }
                if (path == "/redirect") {
                    socket->write("HTTP/1.1 302 Found\r\nLocation: " + base.toUtf8() +
                                  "/feed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
                if (path == "/feed")
                    body = ("<rss version=\"2.0\" xmlns:i=\"http://www.itunes.com/dtds/podcast-1.0.dtd\"><channel><title>Fixture "
                            "show</title><description>Test</description><item><guid>one</"
                            "guid><title>Same title</title>" + episodeNumberXml + "<enclosure url=\"" +
                            base +
                            "/audio.mp3" + enclosureSuffix + "\" "
                            "type=\"audio/mpeg\"/></item><item><guid>two</"
                            "guid><title>Same title</title><enclosure url=\"" +
                            base +
                            "/audio.mp3?second=1\" "
                            "type=\"audio/mpeg\"/></item></channel></rss>")
                               .toUtf8();
                else if (path.startsWith("/audio.mp3")) {
                    ++audioRequests;
                    QFile f(root + "/part.mp3");
                    f.open(QIODevice::ReadOnly);
                    body = f.readAll();
                } else if (path == "/bad")
                    body = "<rss>broken";
                else if (path == "/large") {
                    // A long back catalogue, past the former 8 MiB cap.
                    body = "<rss version=\"2.0\"><channel><title>Back catalogue</title>";
                    for (int i = 0; i < 3104; ++i)
                        body += "<item><guid>c" + QByteArray::number(i) + "</guid><title>Episode " +
                                QByteArray::number(i) + "</title><description>" + QByteArray(4096, 'n') +
                                "</description><enclosure url=\"" + base.toUtf8() + "/audio.mp3?c=" +
                                QByteArray::number(i) + "\" type=\"audio/mpeg\"/></item>";
                    body += "</channel></rss>";
                } else if (path.startsWith("/paged") || path.startsWith("/flaky") || path.startsWith("/loop") ||
                           path.startsWith("/capped") || path.startsWith("/repeat") || path.startsWith("/slow") ||
                           path.startsWith("/dupe") || path.startsWith("/empty") || path.startsWith("/stuck")) {
                    // Three per document (paged-big: 100). loop's second page links
                    // back to its first; repeat's third page repeats its second;
                    // slow answers after 400 ms; dupe's second page repeats the newest
                    // GUID with stale content; empty's second page lists nothing.
                    const QUrl url(base + QString::fromUtf8(path));
                    const QString name = url.path().mid(1);
                    const int requested = std::max(1, QUrlQuery(url).queryItemValue("page").toInt());
                    ++pagedRequests[name + ":" + QString::number(requested)];
                    const int number = name == "repeat" && requested >= 3 ? requested - 1 : requested;
                    const int total = name == "paged-big" ? 250 : name == "loop" ? 6 : name == "flaky" ? 8
                                    : name == "capped" ? cappedCount : name == "repeat" ? repeatCount
                                    : name.startsWith("slow") ? 9 : name == "dupe" ? 6 : name == "empty" ? 9
                                    : name == "stuck" ? 9 : pagedCount;
                    const int per = name == "paged-big" ? 100 : 3;
                    if ((name == "flaky" && flakyFailing.contains(number)) || (name == "stuck" && number == 3)) {
                        status = 503;
                        body = "Unavailable";
                    } else {
                        body = "<rss version=\"2.0\" xmlns:atom=\"http://www.w3.org/2005/Atom\"><channel><title>" +
                               name.toUtf8() + " show</title>";
                        if (name == "loop")
                            body += "<atom:link rel=\"next\" href=\"" + base.toUtf8() +
                                    (number == 1 ? "/loop?page=2" : "/loop") + "\"/>";
                        else if (number * per < total && !(name == "stuck" && number == 1 && stuckNoNext))
                            body += "<atom:link rel=\"next\" href=\"/" + name.toUtf8() + "?page=" +
                                    QByteArray::number(requested + 1) + "\"/>";
                        if (name == "dupe" && number == 2)
                            body += "<item><guid>dupe-6</guid><title>stale copy</title><pubDate>Mon, 01 Jan 2001 12:00:00 +0000</pubDate>"
                                    "<enclosure url=\"" + base.toUtf8() + "/audio.mp3?dupe=stale\" type=\"audio/mpeg\"/></item>";
                        for (int i = (number - 1) * per; i < std::min(total, number * per); ++i) {
                            if (name == "empty" && number == 2)
                                break;
                            const int n = total - i;
                            const auto date = QDateTime(QDate(2020, 1, 1).addDays(n), QTime(12, 0), QTimeZone::UTC);
                            body += "<item><guid>" + name.toUtf8() + "-" + QByteArray::number(n) + "</guid><title>" +
                                    name.toUtf8() + " episode " + QByteArray::number(n) + "</title><pubDate>" +
                                    date.toString(Qt::RFC2822Date).toUtf8() + "</pubDate><enclosure url=\"" +
                                    base.toUtf8() + "/audio.mp3?" + name.toUtf8() + "=" + QByteArray::number(n) +
                                    "\" type=\"audio/mpeg\"/></item>";
                        }
                        body += "</channel></rss>";
                    }
                } else if (path == "/ratio") {
                    QFile f(root + "/ratio.xml.gz");
                    const auto gz = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " +
                                  QByteArray::number(gz.size()) + "\r\nConnection: close\r\n\r\n" + gz);
                    socket->disconnectFromHost();
                    return;
                } else if (path == "/huge-declared") {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                                  QByteArray::number(qint64(Podcasts::MaxFeedBytes) + 1) +
                                  "\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                } else if (path == "/huge-stream") {
                    // No length: the body itself has to be cut off.
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
                    socket->write(QByteArray(Podcasts::MaxFeedBytes + 1024 * 1024, 'x'));
                    socket->disconnectFromHost();
                    return;
                }
                else {
                    status = 404;
                    body = "Not found";
                }
                const auto reply = "HTTP/1.1 " + QByteArray::number(status) +
                                   " OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                                   "\r\nConnection: close\r\n\r\n" + body;
                if (path.startsWith("/slow")) {
                    QTimer::singleShot(400, socket, [socket, reply] { socket->write(reply); socket->disconnectFromHost(); });
                    return;
                }
                socket->write(reply);
                socket->disconnectFromHost();
            });
        }
    });
    QStringList qmlErrors;
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [&](const QList<QQmlError> &errors) {
                         for (const auto &e : errors)
                             qmlErrors.append(e.toString());
                     });
    engine.loadFromModule("Zuuned", "Main");
    check(!engine.rootObjects().isEmpty(), "actual spoken QML pages instantiate");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *service = engine.singletonInstance<SpokenMediaService *>("Zuuned", "SpokenMediaService");
    auto *player = engine.singletonInstance<PlayerService *>("Zuuned", "PlayerService");
    service->attachPlayer(player);
    QString lastError;
    bool lastOk = true;
    int foregroundOperations = 0;
    QObject::connect(service, &SpokenMediaService::operationFinished, &app,
                     [&](const QString &, const QString &, bool ok, const QString &error) {
                         ++foregroundOperations;
                         lastOk = ok;
                         lastError = error;
                         if (!ok)
                             fprintf(stderr, "[native-op] %s\n", qPrintable(error));
                     });
    auto settle = [&] { return until([&] { return !service->busy(); }, 15000); };
    check(settle(), "service loads its isolated collection");
    service->previewFeed(QUrl(base + "/large"));
    check(settle() && lastOk && service->preview().value("episodeCount").toInt() == 3104 &&
              !service->preview().contains("episodes"),
          "a 3,104-episode feed over 12 MiB previews, keeping its episodes out of QML");
    service->previewFeed(QUrl(base + "/ratio"));
    check(settle() && lastOk && service->preview().value("episodeCount").toInt() == 3000,
          "a gzip feed compressed far past Qt's bomb ratio previews (our own byte limit applies)");
    service->previewFeed(QUrl(base + "/huge-declared"));
    check(settle() && !lastOk && lastError.contains("128 MiB") && service->preview().isEmpty(),
          "a response declaring more than 128 MiB is refused, naming the limit");
    service->previewFeed(QUrl(base + "/huge-stream"));
    check(settle() && !lastOk && lastError.contains("128 MiB") && service->preview().isEmpty(),
          "an undeclared response past 128 MiB is cut off, naming the limit");
    service->previewFeed(QUrl(base + "/redirect"));
    check(settle() && lastOk && service->preview().value("title") == "Fixture show",
          "RSS preview follows validated redirect");
    check(service->collections("show").isEmpty(), "preview never commits subscription");
    service->subscribePreview();
    check(settle() && lastOk && service->collections("show").size() == 1,
          "subscription commits to native store");
    if (service->collections("show").isEmpty())
        return 1;
    const QString showId = service->collections("show").first().toMap().value("id").toString();
    auto rows = service->entries(showId);
    check(rows.size() == 2, "same-title episodes retain distinct GUID identities");
    const QString ep = rows.first().toMap().value("id").toString();
    const auto numberedId = [&] {
        for (const auto &entry : service->entries(showId))
            if (entry.toMap().value("guid").toString() == "one") return entry.toMap().value("id").toString();
        return QString();
    }();
    check(service->item(numberedId).value("episodeNumber").toInt() == 190,
          "subscription persists feed episode number");
    service->subscribePreview();
    check(settle() && service->collections("show").size() == 1 &&
              service->entries(showId).size() == 2,
          "repeated subscription is idempotent");
    service->download({ep});
    check(settle() && lastOk && !service->item(ep).value("path").toString().isEmpty(),
          "episode streamed to local storage and native-probed");
    service->download({ep});
    check(settle() && audioRequests == 1, "repeat download reuses complete local file");
    QImage cover(64, 64, QImage::Format_RGB32);
    cover.fill(Qt::magenta);
    check(cover.save(root + "/cover.png"), "local cover fixture written");
    service->saveCover(showId, QUrl::fromLocalFile(root + "/cover.png"), false, 0);
    check(settle() && lastOk && !service->cover(showId).isEmpty(), "manual cover atomically saved");
    QString coverUrl = service->cover(showId);
    service->saveCover(showId, {}, true, 0);
    check(settle() && !lastOk && service->cover(showId) == coverUrl,
          "stale cover reset is rejected");
    service->refreshFeed(showId);
    check(settle() && lastOk && service->cover(showId) == coverUrl &&
              !service->item(ep).value("path").toString().isEmpty(),
          "feed refresh preserves manual art and downloaded paths");
    episodeNumberXml = "<i:episode>191</i:episode>";
    service->refreshFeed(showId);
    check(settle() && lastOk && service->item(numberedId).value("episodeNumber").toInt() == 191,
          "refresh updates feed number without changing episode identity");
    episodeNumberXml.clear();
    service->refreshFeed(showId);
    check(settle() && lastOk && service->item(numberedId).value("episodeNumber").toInt() == 0,
          "refresh clears a withdrawn feed episode number");
    QVariantList preparedTransfers, resolvedTransfers;
    QString resolvedToken, resolvedError;
    int transferSignals = 0, preparationSignals = 0;
    bool connectedForQueue = false;
    service->attachDeviceQueue([&] { return connectedForQueue; });
    QObject::connect(service, &SpokenMediaService::podcastTransferReady, &app, [&](const QVariantList &items) {
        preparedTransfers = items; ++transferSignals;
    });
    QObject::connect(service, &SpokenMediaService::transferPrepared, &app,
        [&](const QString &token, const QVariantList &items, const QString &error) {
            resolvedToken = token; resolvedTransfers = items; resolvedError = error; ++preparationSignals;
        });
    int prepareSequence = 0;
    auto prepare = [&](const QVariantMap &draft) {
        const QString token = QStringLiteral("native-prepare-%1").arg(++prepareSequence);
        resolvedToken.clear(); resolvedTransfers.clear(); resolvedError.clear();
        const int operations = foregroundOperations;
        service->prepareTransfer(token, draft);
        const bool quiet = !service->foregroundBusy();
        return until([&] { return resolvedToken == token; }, 15000) && quiet &&
               !service->foregroundBusy() && foregroundOperations == operations;
    };
    service->queuePodcasts({ep});
    check(!service->busy() && transferSignals == 0, "offline queue admission starts no downloads or device work");
    connectedForQueue = true;
    const int podcastOperations = foregroundOperations;
    service->queuePodcasts({ep, ep});
    check(!service->busy() && transferSignals == 1 && preparedTransfers.size() == 1 &&
          foregroundOperations == podcastOperations && preparedTransfers.first().toMap().value("needsPreparation").toBool(),
          "podcast selection emits one quiet synchronous draft without foreground work");
    const auto firstTransfer = preparedTransfers.value(0).toMap();
    check(firstTransfer.value("feedUrl") == service->item(showId).value("identity") &&
          firstTransfer.value("identity") == service->item(ep).value("identity") &&
          QFileInfo(firstTransfer.value("posterPath").toString()).isFile(),
          "queue payload carries feed-scoped GUID and manual artwork");
    check(prepare(firstTransfer) && resolvedError.isEmpty() && resolvedTransfers.size() == 1,
          "explicit sync preparation resolves downloaded podcast without foreground status");
    enclosureSuffix = "?rotated=1";
    service->refreshFeed(showId);
    check(settle() && service->item(ep).value("deviceSourceUrl") == firstTransfer.value("sourceUrl"),
          "feed refresh preserves pinned device source identity");
    service->queuePodcasts({ep});
    check(!service->busy() && transferSignals == 2 && preparedTransfers.first().toMap().value("sourceUrl") == firstTransfer.value("sourceUrl"),
          "changed enclosure URL cannot change a queued episode's wire identity");
    const QString secondEpisode = rows.last().toMap().value("id").toString();
    const int downloadsBeforeQueue = audioRequests;
    service->queuePodcasts({secondEpisode});
    const auto feedOnlyDraft = preparedTransfers.first().toMap();
    check(!service->busy() && transferSignals == 3 && service->item(secondEpisode).value("path").toString().isEmpty() &&
          feedOnlyDraft.value("filepath").toString().isEmpty() && audioRequests == downloadsBeforeQueue,
          "feed-only episode appears as a draft immediately without a download");
    service->queuePodcasts({ep});
    check(!service->busy() && transferSignals == 4, "successive selections publish immediately without a busy gate");
    check(prepare(feedOnlyDraft) && resolvedError.isEmpty() && resolvedTransfers.size() == 1 &&
          !service->item(secondEpisode).value("path").toString().isEmpty() && audioRequests == downloadsBeforeQueue + 1,
          "feed-only episode downloads only during explicit sync preparation");
    const int beforeCancel = preparationSignals;
    service->prepareTransfer("cancelled-native-token", firstTransfer);
    service->cancelTransfer("cancelled-native-token");
    check(until([&] { return resolvedToken == "cancelled-native-token"; }) && preparationSignals == beforeCancel+1 &&
          resolvedTransfers.isEmpty() && !resolvedError.isEmpty(),
          "cancelled preparation token returns only a correlated cancellation error");
    connectedForQueue = false;
    service->queuePodcasts({ep});
    check(!service->busy() && transferSignals == 4, "offline subsequent selection does not publish a draft");
    connectedForQueue = true;
    service->queuePodcasts({showId});
    check(!service->busy() && transferSignals == 4, "collections and non-episode IDs cannot use podcast upload");
    enclosureSuffix.clear();
    service->previewFeed(QUrl(base + "/bad"));
    check(settle() && !lastOk && service->collections("show").size() == 1,
          "bad RSS cannot replace good saved data");
    service->previewBook(
        {QUrl::fromLocalFile(root + "/book.m4b"), QUrl::fromLocalFile(root + "/part.mp3")});
    check(settle() && lastOk && service->preview().value("parts").toList().size() == 2,
          "native multi-part import preview");
    check(service->collections("book").isEmpty(), "book preview does not import files");
    const QStringList order{root + "/part.mp3", root + "/book.m4b"};
    service->importBook("Fixture Book", "Author", "Narrator", order);
    check(settle() && lastOk && service->collections("book").size() == 1,
          "reviewed book imports as one collection");
    if (service->collections("book").isEmpty())
        return 1;
    const QString bookId = service->collections("book").first().toMap().value("id").toString();
    const auto parts = service->entries(bookId);
    check(parts.size() == 2 && parts.first().toMap().value("path") == order.first(),
          "explicit user part order preserved");
    check(parts.last().toMap().value("chapters").toList().size() == 2,
          "chapter boundaries retained within their source part");
    service->importBook("Other title", "Author", "Narrator", order);
    check(settle() && service->collections("book").size() == 1,
          "same source set cannot create a duplicate book");
    service->markPlayed(ep, true);
    check(settle() && service->item(ep).value("played").toBool(),
          "played state persists independently from feed data");
    service->addBookmark(ep, 500, "A saved moment");
    check(settle() && service->item(ep).value("bookmarks").toList().size() == 1,
          "bookmark saved under stable episode ID");
    service->play(ep, 500);
    check(until([&] { return player->durationMs() > 0 && player->positionMs() >= 450; }),
          "actual mpv resumes spoken episode after correlated file load");
    player->playPause();
    check(settle(), "paused position write completes");
    check(player->queue().first().toMap().value("libraryId").toInt() == -1,
          "spoken queue cannot overwrite music listening positions");
    auto *page = engine.rootObjects().first()->findChild<QQuickItem *>("spokenPage");
    page->setProperty("collectionId", showId);
    QTest::qWait(400);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    check(window && window->grabWindow().save(root + "/native-podcast.png"),
          "native podcast detail captured");
    auto *sheet = engine.rootObjects().first()->findChild<QObject *>("spokenCoverSheet");
    check(sheet &&
              QMetaObject::invokeMethod(sheet, "openFor", Q_ARG(QVariant, service->item(showId))),
          "native Customize opens with a staged cover");
    if (!sheet)
        return 1;
    auto clickCover = [&](const char *name) {
        auto *button = sheet->findChild<QQuickItem *>(name);
        if (!button)
            return false;
        QTest::qWait(100);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        return true;
    };
    cover.fill(Qt::darkCyan);
    cover.save(root + "/cover-new.png");
    sheet->setProperty("draftSource", QUrl::fromLocalFile(root + "/cover-new.png"));
    sheet->setProperty("dirty", true);
    check(clickCover("spokenCancelCover") &&
              until([&] { return !sheet->property("visible").toBool(); }) &&
              service->cover(showId) == coverUrl,
          "native Cancel discards a staged cover without saving");
    QMetaObject::invokeMethod(sheet, "openFor", Q_ARG(QVariant, service->item(showId)));
    sheet->setProperty("draftSource", QUrl::fromLocalFile(root + "/cover-new.png"));
    sheet->setProperty("dirty", true);
    check(clickCover("spokenApplyCover") && settle() &&
              until([&] { return !sheet->property("visible").toBool(); }) &&
              service->cover(showId) != coverUrl,
          "native Apply saves the staged cover and closes after completion");
    coverUrl = service->cover(showId);
    page->setProperty("books", true);
    page->setProperty("collectionId", bookId);
    QTest::qWait(200);
    check(window->grabWindow().save(root + "/native-book.png"),
          "native audiobook chapter page captured");
    const QString lastPart = parts.last().toMap().value("id").toString();
    service->play(lastPart, 1500);
    check(until([&] { return player->durationMs() > 0 && player->positionMs() >= 1450; }) &&
              player->queue().size() == 2 && player->queueIndex() == 1,
          "book playback retains reviewed part order and selected chapter offset");
    player->playPause();
    check(settle() && service->item(lastPart).value("positionMs").toLongLong() >= 1450,
          "book pause saves the active part position");
    service->play(bookId);
    check(until([&] { return player->durationMs() > 0 && player->positionMs() >= 1450; }) &&
              player->queueIndex() == 1,
          "book resume returns to its most recently listened part");
    player->stop();
    check(settle() && service->item(lastPart).value("positionMs").toLongLong() >= 1450,
          "stopping does not replace a saved book position with zero");
    const auto beforeMissing = player->queue();
    check(QFile::rename(root + "/part.mp3", root + "/part-away.mp3"),
          "missing source fixture prepared");
    service->play(lastPart);
    check(player->queue() == beforeMissing,
          "missing book part refuses playback without replacing the queue");
    check(QFile::rename(root + "/part-away.mp3", root + "/part.mp3"),
          "missing source fixture restored");
    // Actual shared playback surfaces and native player properties.
    service->play(lastPart, 1000);
    check(until([&] { return player->durationMs() > 0 && player->positionMs() >= 950; }) &&
              player->mediaKind() == "audiobook",
          "book queue advertises the audiobook control mode");
    auto *ribbon = engine.rootObjects().first()->findChild<QObject *>("nativeRibbon");
    auto *ribbonControls = ribbon->findChild<QObject *>("ribbonSpokenControls");
    check(ribbonControls->property("visible").toBool() &&
              !ribbon->findChild<QObject *>("ribbonMusicControls")->property("visible").toBool(),
          "top ribbon replaces music controls with spoken controls");
    engine.rootObjects().first()->setProperty("islandMode", true);
    QTest::qWait(500);
    auto *island = engine.rootObjects().first()->findChild<QObject *>("nativeIsland");
    check(island->findChild<QObject *>("islandSpokenControls")->property("visible").toBool() &&
              !island->findChild<QObject *>("islandMusicControls")->property("visible").toBool() &&
              !ribbon->property("visible").toBool(),
          "spoken player migrates into the existing island with one active ribbon");
    auto *bookmarkButton=island->findChild<QQuickItem*>("spokenBookmark");
    const int marksBefore=service->item(lastPart).value("bookmarks").toList().size();
    if(bookmarkButton)QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,bookmarkButton->mapToScene(QPointF(bookmarkButton->width()/2,bookmarkButton->height()/2)).toPoint());
    check(bookmarkButton && settle() && service->item(lastPart).value("bookmarks").toList().size()==marksBefore+1,"island bookmark button saves the currently playing book part");
    check(window->grabWindow().save(root + "/native-spoken-island.png"),
          "spoken island screenshot captured");
    window->setHeight(660);
    QTest::qWait(150);
    auto *compactControls=island->findChild<QQuickItem*>("islandSpokenControls");
    const auto controlBottom=compactControls->mapToScene(QPointF(0,compactControls->height())).y();
    check(controlBottom < window->height()-20 && compactControls->mapToScene(QPointF()).y()>=20, "spoken island controls fit in a short window");
    check(window->grabWindow().save(root+"/native-spoken-island-short.png"), "short island screenshot captured");
    window->setHeight(1000);
    service->skipChapter(1);
    check(until([&] { return player->positionMs() >= 3950 && player->positionMs() < 6000; }),
          "next chapter seeks the native chapter boundary");
    player->setSpokenRate(1.5);
    check(qAbs(player->playbackRate() - 1.5) < 0.01, "spoken speed changes the real mpv property");
    player->setSleepMinutes(15);
    auto *sleepTimer = player->findChild<QTimer *>("spokenSleepTimer");
    check(sleepTimer && sleepTimer->isActive() && player->sleepMinutes() == 15,
          "spoken sleep timer is armed");
    if (sleepTimer)
        sleepTimer->start(1);
    check(until([&] { return !player->playing() && player->sleepMinutes() == 0; }) &&
              !player->stopped(),
          "sleep timer pauses rather than clearing the listening position");
    QVariantList music{QVariantMap{
        {"filepath", root + "/part.mp3"}, {"title", "Music fixture"}, {"libraryId", -1}}};
    player->setQueue(music, 0);
    check(until([&] { return player->durationMs() > 0; }) && player->mediaKind() == "music" &&
              qAbs(player->playbackRate() - 1) < 0.01,
          "returning to music resets spoken speed");
    player->toggleShuffle();
    player->cycleRepeat();
    const bool musicShuffle = player->shuffled();
    const int musicRepeat = player->repeatMode();
    service->play(lastPart, 500);
    check(until([&] { return player->durationMs() > 0; }) && !player->shuffled() &&
              player->repeatMode() == 0,
          "spoken playback remains sequential with music shuffle enabled");
    player->setSleepMinutes(30);
    player->setQueue(music, 0);
    check(until([&] { return player->durationMs() > 0; }) && player->sleepMinutes() == 0 &&
              player->shuffled() == musicShuffle && player->repeatMode() == musicRepeat,
          "music restores its modes and cancels the spoken sleep timer");
    engine.rootObjects().first()->setProperty("islandMode", false);
    QTest::qWait(100);
    check(ribbon->findChild<QObject *>("ribbonMusicControls")->property("visible").toBool() &&
              !ribbonControls->property("visible").toBool(),
          "music ribbon controls return after spoken playback");
    player->stop();
    settle();

    // Folder grouping, natural ordering, rollback and actual native import.
    const QString folder = root + "/Books";
    QDir().mkpath(folder + "/First/CD 1");
    QDir().mkpath(folder + "/Second");
    check(QFile::copy(root + "/part.mp3", folder + "/First/CD 1/track 10.mp3") &&
              QFile::copy(root + "/part.mp3", folder + "/First/CD 1/track 2.mp3") &&
              QFile::copy(root + "/book.m4b", folder + "/Second/book.m4b"),
          "folder fixtures include nested discs and numbered parts");
    QFile::link(root + "/part.mp3", folder + "/outside.mp3");
    service->previewBookFolder(QUrl::fromLocalFile(folder), true);
    check(settle() && lastOk && service->preview().value("books").toList().size() == 2,
          "collection scan groups by immediate book folders and skips symlinks");
    auto folderBooks = service->preview().value("books").toList();
    if (folderBooks.size() != 2)
        return 1;
    check(folderBooks.first()
              .toMap()
              .value("parts")
              .toList()
              .first()
              .toMap()
              .value("path")
              .toString()
              .endsWith("track 2.mp3"),
          "natural file order puts part 2 before part 10");
    check(folderBooks.first().toMap().value("title")=="First", "untagged folder uses its book name rather than the first track filename");
    const int booksBefore = service->collections("book").size();
    auto reviewed = [&](QVariantList drafts) {
        QVariantList result;
        for (int i = 0; i < drafts.size(); ++i) {
            auto row = drafts[i].toMap();
            QVariantList order;
            for (const auto &part : row.value("parts").toList())
                order.append(part.toMap().value("path"));
            row["index"] = i;
            row["order"] = order;
            result.append(row);
        }
        return result;
    };
    auto edits = reviewed(folderBooks);
    auto bad = edits.last().toMap();
    bad["order"] = QVariantList{root + "/does-not-exist.mp3"};
    edits.last() = bad;
    service->importBookFolder(edits);
    check(settle() && !lastOk && service->collections("book").size() == booksBefore,
          "a bad later book rolls back the whole import batch");
    auto *folderSheet = page->findChild<QObject *>("spokenFolderSheet");
    check(folderSheet && QMetaObject::invokeMethod(folderSheet, "begin"),
          "native folder review opens");
    if (!folderSheet)
        return 1;
    folderSheet->setProperty("awaitingPreview", true);
    service->previewBookFolder(QUrl::fromLocalFile(folder), true);
    check(settle() && folderSheet->property("books").toList().size() == 2,
          "folder preview populates the native review sheet");
    check(QMetaObject::invokeMethod(folderSheet,"review",Q_ARG(QVariant,0)), "native folder review opens the selected book's parts");
    auto reversed=folderBooks.first().toMap().value("parts").toList();std::reverse(reversed.begin(),reversed.end());
    folderSheet->setProperty("reviewParts",reversed);
    QMetaObject::invokeMethod(folderSheet,"finishReview");
    auto *importFolder = folderSheet->findChild<QQuickItem *>("spokenImportFolder");
    QTest::qWait(300);
    check(window->grabWindow().save(root + "/native-folder-review.png"),
          "folder review screenshot captured");
    if (importFolder)
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            importFolder->mapToScene(QPointF(importFolder->width() / 2, importFolder->height() / 2))
                .toPoint());
    check(importFolder && settle() && lastOk &&
              service->collections("book").size() == booksBefore + 2 &&
              !folderSheet->property("visible").toBool(),
          "native Import saves reviewed books and closes only after completion");
    bool reviewedOrderSaved=false;
    for(const auto &book:service->collections("book")) {
        const auto children=service->entries(book.toMap().value("id").toString());
        if(!children.isEmpty() && children.first().toMap().value("path").toString().endsWith("First/CD 1/track 10.mp3"))reviewedOrderSaved=true;
    }
    check(reviewedOrderSaved,"native folder import preserves the user's revised part order");
    service->importBookFolder(reviewed(folderBooks));
    check(settle() && lastOk && service->collections("book").size() == booksBefore + 2,
          "repeat folder import reuses existing books");
    service->previewBookFolder(QUrl::fromLocalFile(folder + "/First"), false);
    check(settle() && lastOk && service->preview().value("books").toList().size() == 1 &&
              service->preview()
                      .value("books")
                      .toList()
                      .first()
                      .toMap()
                      .value("parts")
                      .toList()
                      .size() == 2,
          "one-book mode includes all nested disc files");
    QFile::copy(root + "/part.mp3", folder + "/First/CD 1/track 3.mp3");
    service->previewBookFolder(QUrl::fromLocalFile(folder + "/First"), false);
    settle();
    service->importBookFolder(reviewed(service->preview().value("books").toList()));
    check(settle() && !lastOk && service->collections("book").size() == booksBefore + 2,
          "changed source sets cannot clone existing book parts");
    service->previewFeed(QUrl(base + "/stall"));
    check(until([&] { return !held.isEmpty(); }), "stalled provider fixture active");
    QElapsedTimer stop;
    stop.start();
    service->cancelWork();
    check(settle() && !lastOk && stop.elapsed() < 2000,
          "cancellation aborts a stalled provider promptly");
    {
        SpokenStore saved(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                          "/spoken");
        error.clear();
        check(saved.open(&error) && saved.get(showId, &error).value("customArt") == coverUrl,
              "independent reopen sees committed custom cover");
        check(saved.get(ep, &error).value("positionMs").toLongLong() > 0,
              "listening position persisted to disk");
    }
    // Real pointer gestures against the production cards/rows and DragGhost.
    window->setProperty("islandMode", false);
    window->resize(1400, 1000);
    page->setProperty("books", false);
    page->setProperty("filter", "all");
    page->setProperty("collectionId", QString());
    window->setProperty("testConnected", true);
    QTest::qWait(350);
    auto dragToDevice = [&](QQuickItem *area) {
        if (!area) return false;
        // Gallery cards now share music's centered play affordance. Start on
        // exposed artwork so this verifies the cover gesture, not the play button.
        const qreal startX = area->objectName() == "spokenCardDragArea" ? area->width() * 0.2 : area->width()/2;
        const auto start = area->mapToScene(QPointF(startX, area->height()/2)).toPoint();
        const QPoint end(window->width()-85, 140);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, start + QPoint(4,4), 20);
        QTest::mouseMove(window, start + QPoint(20,20), 20);
        for (int step = 1; step <= 14; ++step) {
            QTest::mouseMove(window, start + (end-start)*step/14, 20);
            QCoreApplication::processEvents();
        }
        window->grabWindow().save(root + "/native-drag-" + area->objectName() + ".png");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(150);
        return true;
    };
    std::function<QQuickItem *(QQuickItem *, const QString &)> visibleItem;
    visibleItem = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        if (!parent || !parent->isVisible()) return nullptr;
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems())
            if (auto *found = visibleItem(child, name)) return found;
        return nullptr;
    };
    auto *cardArea = visibleItem(page, "spokenCardDragArea");
    auto *transferPicker = page->findChild<QObject *>("spokenTransferPicker");
    check(dragToDevice(cardArea) && window->property("droppedKind").toString() == "podcast-shows" &&
          transferPicker && transferPicker->property("visible").toBool(),
          "dragging a podcast cover to the device opens episode selection");
    if (transferPicker) QMetaObject::invokeMethod(transferPicker, "close");
    page->setProperty("collectionId", showId);
    window->setProperty("droppedKind", QString());
    page->setProperty("checks", QVariantMap{{ep,true},{secondEpisode,true}});
    QTest::qWait(350);
    auto *heroArea = visibleItem(page, "spokenHeroDragArea");
    check(dragToDevice(heroArea) && window->property("droppedKind").toString() == "podcast-shows" &&
          transferPicker && transferPicker->property("visible").toBool(),
          "detail cover drag opens the same episode picker");
    if (transferPicker) QMetaObject::invokeMethod(transferPicker, "close");
    window->setProperty("droppedKind", QString());
    QTest::qWait(250);
    auto *rowArea = visibleItem(page, "spokenEpisodeDragArea");
    check(dragToDevice(rowArea) && window->property("droppedKind").toString() == "podcasts" &&
          window->property("droppedIds").toList().size() == 2,
          "dragging a selected episode carries the entire selected batch");
    window->setProperty("testConnected", false);
    window->setProperty("droppedKind", QString());
    QTest::qWait(100);
    dragToDevice(rowArea);
    check(window->property("droppedKind").toString().isEmpty(), "offline episode gestures cannot drop onto the device");
    // Audiobook GUI and queue preparation stay separate from podcast identity.
    QVariantList bookTransfers;int bookSignals=0;
    QObject::connect(service,&SpokenMediaService::audiobookTransferReady,&app,[&](const QVariantList &items){bookTransfers=items;++bookSignals;});
    QString plainBook;
    for(const auto &b:service->collections("book")) {
        const auto rows=service->entries(b.toMap().value("id").toString());
        bool plain=!rows.isEmpty();for(const auto &v:rows)if(!v.toMap().value("chapters").toList().isEmpty())plain=false;
        if(plain){plainBook=b.toMap().value("id").toString();break;}
    }
    check(!plainBook.isEmpty(),"plain multipart book fixture available");
    const auto bookParts=service->entries(plainBook);
    QStringList bookIds;for(const auto &v:bookParts)bookIds.append(v.toMap().value("id").toString());
    connectedForQueue=true;
    const int bookOperations=foregroundOperations;
    service->queueAudiobooks({bookIds.last()});
    check(!service->busy() && bookSignals==1 && bookTransfers.size()==bookIds.size() && foregroundOperations==bookOperations,
          "selecting a source part immediately queues its whole book without foreground work");
    const auto plainTransfers=bookTransfers;
    check(plainTransfers.first().toMap().value("needsPreparation").toBool() &&
          plainTransfers.first().toMap().value("sourceHash").toString().isEmpty(),
          "book queue drafts defer source hashing until sync");
    check(prepare(plainTransfers.first().toMap()) && resolvedError.isEmpty() && resolvedTransfers.size()==bookIds.size(),
          "explicit sync preparation validates every source of the whole book quietly");
    const auto bookKey=resolvedTransfers.value(0).toMap().value("bookKey").toString();
    check(bookKey.size()==64 && resolvedTransfers.value(0).toMap().value("sourceHash").toString().size()==64 && resolvedTransfers.value(0).toMap().value("partNumber").toInt()==1,
          "sync preparation supplies verified book content identity and reviewed order");
    service->queueAudiobooks({plainBook,bookIds.first(),plainBook});
    check(!service->busy() && bookSignals==2 && bookTransfers==plainTransfers,
          "book and repeated source IDs expand exactly once with stable draft identity");
    service->queueAudiobooks({parts.last().toMap().value("id").toString()});
    check(!service->busy() && bookSignals==3 && bookTransfers.size()==3,
          "chaptered source selection immediately queues all sources and chapters from cached metadata");
    const auto chapterDrafts=bookTransfers;
    check(prepare(chapterDrafts.first().toMap()) && resolvedError.isEmpty() && resolvedTransfers.size()==3,
          "chapter validation and hashing run only during explicit sync preparation");
    const auto chapterTransfers=resolvedTransfers;
    const QString chapterPath=parts.last().toMap().value("path").toString();
    qint64 chapterEnd=0;int chapterRows=0;bool contiguous=true,ordered=true;
    QSet<QString> chapterIdentities;
    QString chapterSourceHash;
    for(int i=0;i<chapterTransfers.size();++i) {
        const auto row=chapterTransfers[i].toMap();
        ordered=ordered && row.value("partNumber").toInt()==i+1;
        chapterIdentities.insert(row.value("identity").toString());
        if(row.value("filepath").toString()!=chapterPath)continue;
        const auto start=row.value("rangeStartMs").toLongLong();
        const auto end=row.value("rangeEndMs").toLongLong();
        contiguous=contiguous && start==chapterEnd && end>start && row.value("durationMs").toLongLong()==end-start;
        chapterEnd=end;++chapterRows;
        if(chapterSourceHash.isEmpty())chapterSourceHash=row.value("sourceHash").toString();
        else contiguous=contiguous && chapterSourceHash==row.value("sourceHash").toString();
    }
    const auto chapterDuration=chapterTransfers.last().toMap().value("sourceDurationMs").toLongLong();
    check(chapterRows==2 && contiguous && chapterEnd==chapterDuration && chapterDuration>0,
          "chapter transfer ranges cover the entire source without gaps or overlap");
    check(ordered && chapterIdentities.size()==chapterTransfers.size() && !chapterIdentities.contains(QString()),
          "chapter identities are distinct and retain global reviewed book order");
    service->queueAudiobooks({bookId});
    check(!service->busy() && bookSignals==4 && bookTransfers==chapterDrafts,
          "whole collection and chaptered source selection produce the same complete draft book");
    connectedForQueue=false;service->queueAudiobooks({bookId});
    check(!service->busy() && bookSignals==4,"offline book admission refuses work");
    connectedForQueue=true;
    page->setProperty("books",true);page->setProperty("collectionId",QString());
    window->setProperty("testConnected",true);window->setProperty("droppedKind",QString());QTest::qWait(300);
    auto *bookArea=visibleItem(page,"spokenCardDragArea");
    const int coverSignals=bookSignals;
    check(dragToDevice(bookArea) && settle() && window->property("droppedKind").toString()=="audiobook-books" &&
          window->property("droppedIds").toList().size()==1 && !transferPicker->property("visible").toBool() && bookSignals==coverSignals+1,
          "book cover drag queues a whole book without opening a part picker");
    page->setProperty("collectionId",plainBook);page->setProperty("filter","all");
    page->setProperty("checks",QVariantMap());window->setProperty("droppedKind",QString());QTest::qWait(300);
    auto *bookRow=visibleItem(page,"spokenEpisodeDragArea");
    const int rowSignals=bookSignals;
    check(dragToDevice(bookRow) && settle() && window->property("droppedKind").toString()=="audiobook-books" &&
          window->property("droppedIds").toList()==QVariantList{plainBook} && bookSignals==rowSignals+1 && bookTransfers.size()==bookIds.size(),
          "book source row drag queues its complete parent book");
    const auto laterPath=bookParts.last().toMap().value("path").toString();
    const int beforeFailure=bookSignals;
    check(QFile::rename(laterPath,laterPath+".unavailable"),"later book source temporarily removed for atomic admission test");
    service->queueAudiobooks({bookIds.first()});
    check(!service->busy() && bookSignals==beforeFailure+1,
          "adding a book performs no filesystem scan even when a source is unavailable");
    check(prepare(bookTransfers.first().toMap()) && !resolvedError.isEmpty() && resolvedTransfers.isEmpty(),
          "sync preparation rejects a missing later source without resolving a partial book");
    check(QFile::rename(laterPath+".unavailable",laterPath),"original source restored after missing-file check");
    QFileInfo originalInfo(laterPath);
    const auto originalSize=originalInfo.size();
    const auto originalModified=originalInfo.lastModified();
    QFile changedSource(laterPath);
    const bool changed=changedSource.open(QIODevice::ReadWrite) && changedSource.seek(changedSource.size()) && changedSource.write("changed")==7;
    changedSource.close();
    check(changed,"later source changed for stale import admission test");
    service->queueAudiobooks({plainBook});
    check(!service->busy() && bookSignals==beforeFailure+2,
          "modified source does not block immediate cached queue admission");
    check(prepare(bookTransfers.first().toMap()) && !resolvedError.isEmpty() && resolvedTransfers.isEmpty(),
          "sync preparation rejects a modified later source for the entire book");
    const bool restored=changedSource.open(QIODevice::ReadWrite) && changedSource.resize(originalSize) &&
        changedSource.setFileTime(originalModified,QFileDevice::FileModificationTime);
    changedSource.close();
    check(restored,"original source contents and timestamp restored after stale import test");
    const int beforeConcurrentBooks=bookSignals;
    const int beforeConcurrentPodcasts=transferSignals;
    service->prepareTransfer("concurrent-native-token",plainTransfers.first().toMap());
    check(service->busy() && !service->foregroundBusy(),
          "sync preparation runs in the background without a page busy banner");
    service->queueAudiobooks({plainBook});service->queuePodcasts({ep});
    check(bookSignals==beforeConcurrentBooks+1 && transferSignals==beforeConcurrentPodcasts+1,
          "new book and podcast selections remain immediate while earlier preparation is active");
    service->cancelTransfer("concurrent-native-token");
    check(until([&] { return resolvedToken=="concurrent-native-token"; }) && resolvedTransfers.isEmpty(),
          "concurrent preparation cancellation cannot publish ready media");
    // Organization is local metadata. It must not mutate playback/source identity
    // or the independently prepared transfer manifest.
    QString organizationResultId;
    QObject::connect(service, &SpokenMediaService::operationFinished, &app,
                     [&](const QString &operation, const QString &id, bool, const QString &) {
        if (operation == "save audiobook collection") organizationResultId = id;
    });
    const auto originalBook = service->item(bookId);
    const auto originalEpisode = service->item(ep);
    service->setFavorite(showId, true);
    service->setFavorite(ep, true);
    service->setFavorite(bookId, true);
    check(settle() && lastOk && service->item(showId).value("favorite").toBool() &&
          service->item(ep).value("favorite").toBool() && service->item(bookId).value("favorite").toBool(),
          "shows episodes and books can be starred independently");
    service->setFavorite(bookIds.first(), true);
    check(settle() && !lastOk && !service->item(bookIds.first()).contains("favorite"),
          "book parts cannot masquerade as favorite books");
    service->setFavorite("missing-favorite-id", true);
    check(settle() && !lastOk && service->item("missing-favorite-id").isEmpty(),
          "unknown favorites never create incomplete media records");
    service->refreshFeed(showId);
    check(settle() && lastOk && service->item(showId).value("favorite").toBool() &&
          service->item(ep).value("favorite").toBool(),
          "feed refresh preserves show and episode favorites");
    service->markPlayed(ep, true);
    check(settle() && lastOk && service->item(ep).value("favorite").toBool(),
          "listening metadata changes preserve episode favorites");
    service->setFavorite(showId, false);
    check(settle() && lastOk && !service->item(showId).value("favorite").toBool() &&
          service->item(ep).value("favorite").toBool(),
          "unstarring a show preserves its independently starred episode");
    service->saveAudiobookCollection({}, "  Fixture series  ", {plainBook, bookId}, 0);
    check(settle() && lastOk && !organizationResultId.isEmpty(),
          "virtual collection creation returns its saved stable identity");
    const auto shelfId = organizationResultId;
    auto shelf = service->item(shelfId);
    check(shelf.value("title") == "Fixture series" && shelf.value("revision").toInt() == 1 &&
          shelf.value("bookIds").toStringList() == QStringList{plainBook, bookId} &&
          service->collectionBooks(shelfId).size() == 2 &&
          service->collectionBooks(shelfId).first().toMap().value("id") == plainBook,
          "collections retain explicit whole-book order and trimmed titles");
    service->saveAudiobookCollection({}, "Fixture series", {bookId}, 0);
    check(settle() && lastOk && organizationResultId != shelfId &&
          service->audiobookCollections().size() == 2,
          "one book may belong to distinct collections including duplicate collection titles");
    const auto secondShelfId = organizationResultId;
    service->saveAudiobookCollection(shelfId, "Invalid duplicate", {bookId, bookId}, 1);
    check(settle() && !lastOk && service->item(shelfId) == shelf,
          "duplicate membership rejection leaves the entire existing collection unchanged");
    service->saveAudiobookCollection(shelfId, "Invalid member", {bookId, ep}, 1);
    check(settle() && !lastOk && service->item(shelfId) == shelf,
          "non-book membership rolls back both title and complete member order");
    service->saveAudiobookCollection(shelfId, "Missing member", {bookId, "missing-book"}, 1);
    check(settle() && !lastOk && service->item(shelfId) == shelf,
          "missing members cannot produce partial collection saves");
    service->saveAudiobookCollection(shelfId, " ", {bookId}, 1);
    check(settle() && !lastOk && service->item(shelfId) == shelf,
          "blank collection titles cannot erase the saved draft");
    service->saveAudiobookCollection(bookId, "Wrong target", {}, 1);
    check(settle() && !lastOk && service->item(bookId).value("kind") == "book",
          "collection updates cannot rewrite book identity");
    service->saveAudiobookCollection(shelfId, "Reordered series", {bookId, plainBook}, 1);
    check(settle() && lastOk && service->item(shelfId).value("revision").toInt() == 2 &&
          service->collectionBooks(shelfId).first().toMap().value("id") == bookId,
          "collection reorder saves atomically and advances its own revision");
    shelf = service->item(shelfId);
    service->saveAudiobookCollection(shelfId, "Stale overwrite", {plainBook}, 1);
    check(settle() && !lastOk && service->item(shelfId) == shelf,
          "stale collection edits cannot overwrite a newer member order");
    service->deleteAudiobookCollection(shelfId, 1);
    check(settle() && !lastOk && service->item(shelfId) == shelf,
          "stale collection deletion preserves newer changes");
    {
        SpokenMediaService reopened;
        check(until([&] { return !reopened.busy(); }) && reopened.item(shelfId) == shelf &&
              reopened.item(ep).value("favorite").toBool() &&
              reopened.item(bookId).value("favorite").toBool() &&
              !reopened.item(showId).value("favorite").toBool(),
              "collection order and all independent favorite choices survive service reopening");
    }
    service->deleteAudiobookCollection(bookId, 1);
    check(settle() && !lastOk && !service->item(bookId).isEmpty(),
          "metadata-only collection removal refuses actual books");
    const auto beforeDeleteParts = service->entries(bookId);
    service->deleteAudiobookCollection(shelfId, 2);
    check(settle() && lastOk && service->item(shelfId).isEmpty() &&
          service->entries(bookId) == beforeDeleteParts &&
          service->item(bookId).value("identity") == originalBook.value("identity") &&
          service->item(ep).value("identity") == originalEpisode.value("identity") &&
          service->collectionBooks(secondShelfId).size() == 1 && QFileInfo::exists(chapterPath),
          "collection deletion preserves source files books chapters favorites and other memberships");
    service->saveAudiobookCollection({}, "Empty collection", {}, 0);
    check(settle() && lastOk && organizationResultId != shelfId &&
          service->collectionBooks(organizationResultId).isEmpty(),
          "empty collections are allowed and removed collection IDs are never reused");
    service->saveAudiobookCollection(shelfId, "Resurrected", {bookId}, 2);
    check(settle() && !lastOk && service->item(shelfId).isEmpty(),
          "deleted collections cannot be resurrected by stale saves");
    // Retention persistence precedes any device deletion. This gate has no USB.
    QString playedToken, playedSerial, playedError;
    QVariantList playedRows;
    int playedAcks = 0;
    QObject::connect(service, &SpokenMediaService::devicePlayedRecorded, &app,
        [&](const QString &token, const QString &serial, const QVariantList &accepted, const QString &error) {
            playedToken = token; playedSerial = serial; playedRows = accepted; playedError = error; ++playedAcks;
        });
    // Candidates are read for the device audio URLs that matter (on the Zune or queued).
    auto candidateFor = [&](const QString &id) {
        const auto source = service->podcastDeviceIdentity(id).value("sourceUrl").toString();
        for (const auto &v : service->podcastCleanupCandidates({source}))
            if (v.toMap().value("spokenId") == id) return v.toMap();
        return QVariantMap();
    };
    const auto candidate = candidateFor(ep);
    const auto otherCandidate = candidateFor(secondEpisode);
    check(!candidate.isEmpty() && candidate.value("feedUrl") == firstTransfer.value("feedUrl") &&
          candidate.value("sourceUrl") == firstTransfer.value("sourceUrl") &&
          candidate.value("album") == service->item(showId).value("title") &&
          candidate.value("showTitle") == service->item(showId).value("title") &&
          candidate.value("posterPath").toString() == QUrl(service->cover(ep)).toLocalFile() &&
          QFileInfo(candidate.value("posterPath").toString()).isFile() &&
          !otherCandidate.isEmpty(), "cleanup candidates retain exact identities and local show artwork for display");
    {
        const auto only = service->podcastCleanupCandidates({candidate.value("sourceUrl").toString()});
        check(only.size() == 1 && only.first().toMap().value("spokenId") == ep &&
              service->podcastCleanupCandidates({}).isEmpty(),
              "cleanup candidates are read only for the device audio URLs asked for");
        const auto all = service->podcastCleanupCandidates({}, true);
        check(all.size() == 2 && std::any_of(all.begin(), all.end(), [&](const QVariant &v) { return v.toMap().value("spokenId") == ep; }),
              "before the Zune's inventory is read, every episode Zuuned sent is a candidate");
    }
    const auto deviceIdentity = service->podcastDeviceIdentity(ep);
    check(deviceIdentity.size() == 2 && deviceIdentity.value("feedUrl") == firstTransfer.value("feedUrl") &&
          deviceIdentity.value("sourceUrl") == firstTransfer.value("sourceUrl"),
          "podcast badge identity uses the same show identity and pinned enclosure as transfers");
    check(service->podcastDeviceIdentity("missing-episode").isEmpty() &&
          service->podcastDeviceIdentity(bookId).isEmpty() && service->podcastDeviceIdentity(showId).isEmpty(),
          "podcast badge identity refuses missing items books and shows");
    const auto episodeBeforeRetention = service->item(ep);
    connectedForQueue = false;
    service->setPodcastKeepOnDevice(ep, true);
    check(settle() && lastOk && candidateFor(ep).isEmpty() &&
          service->item(ep).value("favorite") == episodeBeforeRetention.value("favorite"),
          "offline keep-on-device excludes episode without changing its favorite");
    service->refreshFeed(showId);
    check(settle() && lastOk && service->item(ep).value("keepOnDevice").toBool(),
          "feed refresh preserves keep-on-device choice");
    service->recordDevicePlayed("protected", "classic-test", {candidate});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty() &&
          !service->item(ep).contains("devicePlayedSerials"),
          "stale cleanup cannot consume newly protected episode");
    service->setPodcastKeepOnDevice(showId, true);
    check(settle() && !lastOk && !service->item(showId).contains("keepOnDevice"),
          "keep-on-device refuses show and non-episode targets");
    service->setPodcastKeepOnDevice(ep, false);
    check(settle() && lastOk && !candidateFor(ep).isEmpty(), "unprotect restores cleanup eligibility");
    auto mismatch = otherCandidate;
    mismatch["identity"] = "wrong-identity";
    service->recordDevicePlayed("atomic-mismatch", "classic-test", {candidate, mismatch});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty() &&
          !service->item(ep).contains("devicePlayedSerials"),
          "later identity mismatch rolls back earlier consumed state atomically");
    mismatch = candidate; mismatch["sourceUrl"] = base + "/different";
    service->recordDevicePlayed("source-mismatch", "classic-test", {mismatch});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty(),
          "source mismatch cannot authorize cleanup acknowledgement");
    mismatch = candidate; mismatch["feedUrl"] = base + "/other-feed";
    service->recordDevicePlayed("feed-mismatch", "classic-test", {mismatch});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty(),
          "feed mismatch cannot authorize cleanup acknowledgement");
    mismatch = candidate; mismatch["spokenId"] = bookId;
    service->recordDevicePlayed("wrong-kind", "classic-test", {mismatch});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty() &&
          !service->item(bookId).contains("devicePlayedSerials"),
          "audiobooks cannot enter podcast consumed history");
    service->recordDevicePlayed("no-serial", "", {candidate});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty(),
          "missing device serial refuses consumed history");
    service->recordDevicePlayed("duplicated", "classic-test", {candidate, candidate});
    check(settle() && !playedError.isEmpty() && playedRows.isEmpty() &&
          !service->item(ep).contains("devicePlayedSerials"),
          "duplicate request rows roll back consumed history");
    service->recordDevicePlayed("completed", "classic-test", {candidate});
    check(settle() && playedError.isEmpty() && playedToken == "completed" &&
          playedSerial == "classic-test" && playedRows == QVariantList{candidate} &&
          service->item(ep).value("played").toBool() &&
          service->item(ep).value("devicePlayedSerials").toMap().value("classic-test").toBool(),
          "successful acknowledgement follows persistent device-scoped consumed state");
    service->recordDevicePlayed("repeat", "classic-test", {candidate});
    check(settle() && playedError.isEmpty() &&
          service->item(ep).value("devicePlayedSerials").toMap().size() == 1,
          "repeat consumed acknowledgement is idempotent");
    service->recordDevicePlayed("other-device", "hd-test", {candidate});
    check(settle() && playedError.isEmpty() &&
          service->item(ep).value("devicePlayedSerials").toMap().size() == 2 &&
          candidateFor(ep).value("devicePlayedSerials").toMap().size() == 2,
          "consumed history and fresh cleanup candidates preserve independent device serials");
    service->refreshFeed(showId);
    check(settle() && lastOk && service->item(ep).value("played").toBool() &&
          service->item(ep).value("devicePlayedSerials").toMap().size() == 2,
          "feed refresh preserves device consumed history");
    service->setPodcastKeepOnDevice(ep, true);
    check(settle() && lastOk, "episode can be protected after consuming it");
    connectedForQueue = true;
    service->queuePodcasts({ep});
    check(preparedTransfers.size() == 1 && preparedTransfers.first().toMap().value("keepOnDevice").toBool() &&
          preparedTransfers.first().toMap().value("devicePlayedSerials").toMap().size() == 2,
          "transfer drafts carry current keep choice and device consumed history");
    {
        SpokenMediaService reopened;
        check(until([&] { return !reopened.busy(); }) &&
              reopened.item(ep).value("keepOnDevice").toBool() &&
              reopened.item(ep).value("devicePlayedSerials").toMap().size() == 2,
              "keep choice and consumed serials survive service reopening");
    }
    service->setPodcastKeepOnDevice(ep, false);
    check(settle() && lastOk, "failure fixture leaves episode unprotected");
    {
        sqlite3 *db = nullptr;
        const auto dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken/spoken.db";
        const bool opened = sqlite3_open(QFile::encodeName(dbPath).constData(), &db) == SQLITE_OK;
        const bool injected = opened && sqlite3_exec(db,
            "CREATE TRIGGER reject_retention BEFORE UPDATE ON items BEGIN SELECT RAISE(ABORT,'fixture write failure'); END",
            nullptr, nullptr, nullptr) == SQLITE_OK;
        check(injected, "retention write failure fixture installed");
        service->recordDevicePlayed("write-failure", "never-committed", {candidate});
        check(settle() && !playedError.isEmpty() && playedRows.isEmpty() &&
              !service->item(ep).value("devicePlayedSerials").toMap().contains("never-committed"),
              "failed persistence emits no rows that could authorize deletion");
        if (db) {
            sqlite3_exec(db, "DROP TRIGGER IF EXISTS reject_retention", nullptr, nullptr, nullptr);
            sqlite3_close(db);
        }
    }
    check(QFileInfo::exists(service->item(ep).value("path").toString()) &&
          service->item(ep).value("identity") == episodeBeforeRetention.value("identity") &&
          service->item(ep).value("favorite") == episodeBeforeRetention.value("favorite") && playedAcks == 11,
          "retention preserves source file identity favorite and one acknowledgement per request");
    // Episode ranges are a pure preview, separate from existing manual selection.
    const int transfersBeforeSelection = transferSignals;
    service->setPodcastSyncSelection(showId, "oldest", 7, false);
    check(settle() && lastOk && service->item(showId).value("syncOrder") == "oldest" &&
          service->item(showId).value("syncCount").toInt() == 7 &&
          !service->item(showId).value("syncUnplayedOnly").toBool(),
          "per-show sync selection saves ordering count and played inclusion");
    service->refreshFeed(showId);
    check(settle() && lastOk && service->item(showId).value("syncCount").toInt() == 7 &&
          service->item(showId).value("syncOrder") == "oldest" &&
          !service->item(showId).value("syncUnplayedOnly").toBool() &&
          transferSignals == transfersBeforeSelection,
          "feed refresh preserves sync selection without automatically queueing episodes");
    const auto showBeforeInvalidSelection = service->item(showId);
    service->setPodcastSyncSelection(showId, "latest-ish", 2, true);
    check(settle() && !lastOk && service->item(showId) == showBeforeInvalidSelection,
          "invalid selection order leaves all show metadata unchanged");
    service->setPodcastSyncSelection(showId, "newest", 0, true);
    check(settle() && !lastOk && service->item(showId) == showBeforeInvalidSelection,
          "zero selection count cannot overwrite saved preferences");
    service->setPodcastSyncSelection(showId, "newest", 5001, true);
    check(settle() && !lastOk && service->item(showId) == showBeforeInvalidSelection,
          "oversized selection count cannot overwrite saved preferences");
    service->setPodcastSyncSelection(bookId, "newest", 3, true);
    check(settle() && !lastOk && !service->item(bookId).contains("syncCount"),
          "audiobooks cannot receive podcast selection preferences");
    QString selectionShowId;
    {
        const auto dbRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken";
        SpokenStore store(dbRoot);
        error.clear();
        QVariantMap show{{"kind", "show"}, {"identity", "selection-fixture"}, {"title", "Order fixture"}};
        bool fixtureReady = store.open(&error) && store.put(&show, &error) && store.begin(&error);
        selectionShowId = show.value("id").toString();
        const QList<QPair<QString, QString>> dates{
            {"c", "2026-10-08T20:00:00-05:00"}, {"v", "invalid"},
            {"a", "2026-10-09T02:00:00+02:00"}, {"d", "2026-10-09T00:30:00Z"},
            {"u", ""}, {"b", "2026-10-09T01:00:00Z"}};
        for (const auto &[identity, date] : dates) {
            QVariantMap episode{{"kind", "episode"}, {"parent", selectionShowId},
                {"identity", identity}, {"title", "Episode " + identity}, {"published", date},
                {"sourceUrl", base + "/selection/" + identity}, {"played", identity == "d"}};
            fixtureReady = fixtureReady && store.put(&episode, &error);
        }
        fixtureReady = fixtureReady && store.commit(&error);
        check(fixtureReady, "selection fixture stores date offsets ties played and undated episodes");
    }
    {
        SpokenMediaService selector;
        check(until([&] { return !selector.busy(); }) &&
              selector.item(showId).value("syncOrder") == "oldest" &&
              selector.item(showId).value("syncCount").toInt() == 7 &&
              !selector.item(showId).value("syncUnplayedOnly").toBool(),
              "per-show sync choices survive service restart");
        auto identities = [](const QVariantList &values) {
            QStringList ids;
            for (const auto &v : values) ids.append(v.toMap().value("identity").toString());
            return ids;
        };
        const auto beforeSelection = selector.entries(selectionShowId);
        const auto unpinned = selector.selectPodcastEpisodes(selectionShowId, "newest", 1).value(0).toMap();
        check(selector.podcastDeviceIdentity(unpinned.value("id").toString()) ==
                  QVariantMap{{"feedUrl", "selection-fixture"}, {"sourceUrl", base + "/selection/b"}},
              "podcast badge identity falls back to the feed source before a device pin exists");
        check(identities(selector.selectPodcastEpisodes(selectionShowId, "newest", 3)) ==
                  QStringList{"b", "c", "a"},
              "newest unplayed preview sorts real instants and resolves equal-date ties deterministically");
        check(identities(selector.selectPodcastEpisodes(selectionShowId, "oldest", 1)) == QStringList{"a"},
              "oldest preview honors timezone offsets and exact count");
        check(identities(selector.selectPodcastEpisodes(selectionShowId, "oldest", 5000)) ==
                  QStringList{"a", "b", "c", "u", "v"},
              "oldest unplayed preview puts missing and invalid dates last in stable identity order");
        check(identities(selector.selectPodcastEpisodes(selectionShowId, "newest", 5000)) ==
                  QStringList{"b", "c", "a", "u", "v"},
              "newest preview also places unknown dates after all dated episodes");
        check(identities(selector.selectPodcastEpisodes(selectionShowId, "oldest", 5000, false)) ==
                  QStringList{"a", "d", "b", "c", "u", "v"},
              "played inclusion returns the complete ordered range when explicitly requested");
        check(selector.entries(selectionShowId) == beforeSelection && beforeSelection.size() == 6 &&
              !selector.busy() && transferSignals == transfersBeforeSelection,
              "range preview leaves manual selection library state and transfer queues untouched");
        check(selector.selectPodcastEpisodes(bookId, "newest", 3).isEmpty() &&
              selector.selectPodcastEpisodes(ep, "newest", 3).isEmpty() &&
              selector.selectPodcastEpisodes("missing-show", "newest", 3).isEmpty(),
              "range preview refuses non-show and missing identities");
        check(selector.selectPodcastEpisodes(selectionShowId, "newest", 0).isEmpty() &&
              selector.selectPodcastEpisodes(selectionShowId, "newest", -1).isEmpty() &&
              selector.selectPodcastEpisodes(selectionShowId, "newest", 5001).isEmpty() &&
              selector.selectPodcastEpisodes(selectionShowId, "random", 3).isEmpty(),
              "range preview rejects invalid orders and out-of-range counts");
    }
    // Paged back catalogues: older pages load in the background, one at a time.
    {
        auto walked = [&] { return until([&] { return !service->busy() && !service->loadingOlder(); }, 30000); };
        auto showFor = [&](const QString &title) {
            for (const auto &v : service->collections("show"))
                if (v.toMap().value("title") == title) return v.toMap();
            return QVariantMap();
        };
        auto titlesOf = [](const QVariantMap &page) {
            QStringList titles;
            for (const auto &v : page.value("rows").toList()) titles << v.toMap().value("title").toString();
            return titles;
        };
        service->previewFeed(QUrl(base + "/paged"));
        check(settle() && lastOk && service->preview().value("episodeCount").toInt() == 3 &&
                  service->preview().value("hasOlder").toBool(),
              "a paged feed previews its newest document and reports older pages");
        service->subscribePreview();
        check(settle() && lastOk && walked(), "subscribing reads the older pages in the background");
        auto paged = showFor("paged show");
        const QString pagedId = paged.value("id").toString();
        check(paged.value("count").toInt() == 8 && service->showSummary(pagedId).value("olderState") == "complete" &&
                  pagedRequests.value("paged:2") == 1 && pagedRequests.value("paged:3") == 1,
              "all eight episodes across three documents are saved and the walk completes");
        const auto first = service->episodePage(pagedId, {{"pageSize", 3}});
        const auto firstRow = first.value("rows").toList().value(0).toMap();
        check(first.value("total").toInt() == 8 && first.value("pages").toInt() == 3 &&
                  titlesOf(first) == QStringList{"paged episode 8", "paged episode 7", "paged episode 6"} &&
                  !firstRow.contains("description") && firstRow.value("feedUrl") == paged.value("identity") &&
                  firstRow.value("deviceSource") == firstRow.value("sourceUrl"),
              "episode pages come newest first, without show notes, carrying the device identity");
        const auto beyond = service->episodePage(pagedId, {{"pageSize", 3}, {"page", 7}});
        check(beyond.value("page").toInt() == 2 && titlesOf(beyond) == QStringList{"paged episode 2", "paged episode 1"},
              "a page past the end shows the last page");
        check(titlesOf(service->episodePage(pagedId, {{"search", "EPISODE 1"}})) == QStringList{"paged episode 1"} &&
                  service->episodePage(pagedId, {{"filter", "downloaded"}}).value("total").toInt() == 0 &&
                  service->episodePage(pagedId, {{"filter", "unplayed"}, {"playedSources", QStringList{firstRow.value("deviceSource").toString()}}})
                          .value("total").toInt() == 7,
              "search folds case; downloaded and unplayed filters page in SQLite, device-played episodes excluded");
        service->setFavorite(firstRow.value("id").toString(), true);
        check(settle() && titlesOf(service->episodePage({}, {{"starred", true}})).contains("paged episode 8") &&
                  service->episodePage(pagedId, {{"filter", "starred"}}).value("total").toInt() == 1 &&
                  service->showOnDevice(pagedId, {firstRow.value("sourceUrl").toString()}) &&
                  !service->showOnDevice(pagedId, {base + "/nowhere.mp3"}) && service->resumeEpisode(pagedId).isEmpty(),
              "starred episodes page across shows; on-device and resume lookups query SQLite");
        service->refreshFeed(pagedId);
        check(settle() && lastOk && walked() && pagedRequests.value("paged:2") == 1 &&
                  service->showSummary(pagedId).value("count").toInt() == 8,
              "refreshing a complete show with nothing new reads no older page");
        pagedCount = 12;
        service->refreshFeed(pagedId);
        check(settle() && lastOk && walked() && service->showSummary(pagedId).value("count").toInt() == 12 &&
                  pagedRequests.value("paged:2") == 2 && pagedRequests.value("paged:3") == 2 &&
                  pagedRequests.value("paged:4") == 0,
              "new episodes are caught up page by page, stopping at a page with nothing new");
        flakyFailing = {2};
        service->previewFeed(QUrl(base + "/flaky"));
        settle();
        service->subscribePreview();
        check(settle() && walked(), "a feed whose second page fails still subscribes");
        auto flaky = showFor("flaky show");
        const QString flakyId = flaky.value("id").toString();
        check(flaky.value("count").toInt() == 3 && flaky.value("olderState") == "incomplete" &&
                  flaky.value("olderPage") == base + "/flaky?page=2" && !flaky.value("olderError").toString().isEmpty(),
              "a failed older page leaves the show incomplete, its cursor on that page");
        flakyFailing.clear();
        service->refreshFeed(flakyId);
        check(settle() && lastOk && walked() && showFor("flaky show").value("count").toInt() == 8 &&
                  showFor("flaky show").value("olderState") == "complete" &&
                  showFor("flaky show").value("olderPage").toString().isEmpty(),
              "the next refresh resumes from the failed page and completes");
        {
            sqlite3 *db = nullptr;
            const auto dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken/spoken.db";
            const auto cursor = (base + "/flaky?page=3").toUtf8();
            bool prepared = sqlite3_open(QFile::encodeName(dbPath).constData(), &db) == SQLITE_OK;
            sqlite3_stmt *stmt = nullptr;
            prepared = prepared && sqlite3_prepare_v2(db, "UPDATE items SET payload=json_set(payload,'$.olderState','loading','$.olderPage',?) WHERE id=?", -1, &stmt, nullptr) == SQLITE_OK;
            if (prepared) {
                sqlite3_bind_text(stmt, 1, cursor.constData(), cursor.size(), SQLITE_TRANSIENT);
                const auto id = flakyId.toUtf8();
                sqlite3_bind_text(stmt, 2, id.constData(), id.size(), SQLITE_TRANSIENT);
                prepared = sqlite3_step(stmt) == SQLITE_DONE;
            }
            sqlite3_finalize(stmt);
            prepared = prepared && sqlite3_exec(db, "DELETE FROM items WHERE kind='episode' AND identity IN ('guid:flaky-1','guid:flaky-2')", nullptr, nullptr, nullptr) == SQLITE_OK;
            if (db) sqlite3_close(db);
            check(prepared, "fixture: a walk interrupted two episodes short of its last page");
            SpokenMediaService reopened;
            check(until([&] { return !reopened.busy() && !reopened.loadingOlder() &&
                                     reopened.showSummary(flakyId).value("count").toInt() == 8; }, 30000) &&
                      reopened.showSummary(flakyId).value("olderState") == "complete",
                  "an interrupted walk resumes when the library loads, from its saved page");
        }
        service->previewFeed(QUrl(base + "/loop"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("loop show").value("olderState") == "complete" &&
                  showFor("loop show").value("count").toInt() == 6 && pagedRequests.value("loop:2") == 1 &&
                  pagedRequests.value("loop:1") == 1,
              "a page linking back to the feed ends the chain as complete");
        // A walk stopped before reading its first older page, and a chain that repeats a page:
        // the next refresh reads to the end, never stopping where catching up would.
        service->previewFeed(QUrl(base + "/repeat"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("repeat show").value("count").toInt() == 12 &&
                  showFor("repeat show").value("olderState") == "complete",
              "a chain repeating one page still reads to its last page");
        const QString repeatId = showFor("repeat show").value("id").toString();
        auto sql = [&](const QByteArray &statement) {
            sqlite3 *db = nullptr;
            const auto dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken/spoken.db";
            bool ok = sqlite3_open(QFile::encodeName(dbPath).constData(), &db) == SQLITE_OK &&
                      sqlite3_exec(db, statement.constData(), nullptr, nullptr, nullptr) == SQLITE_OK;
            if (db) sqlite3_close(db);
            return ok;
        };
        check(sql("UPDATE items SET payload=json_set(payload,'$.olderState','loading','$.olderPage','" + base.toUtf8() +
                  "/repeat?page=2') WHERE id='" + repeatId.toUtf8() + "'; DELETE FROM items WHERE kind='episode' AND parent='" +
                  repeatId.toUtf8() + "' AND CAST(substr(identity,13) AS INTEGER)<=9"),
              "fixture: a walk that never read its first older page");
        repeatCount = 13;
        service->refreshFeed(repeatId);
        check(settle() && walked() && service->showSummary(repeatId).value("count").toInt() == 13 &&
                  service->showSummary(repeatId).value("olderState") == "complete" && pagedRequests.value("repeat:6") >= 1,
              "a refresh with new episodes reads the never-read chain to its end");
        // Cancelling other work leaves the walk going; quitting resumes it at the next start.
        service->previewFeed(QUrl(base + "/slow"));
        settle();
        service->subscribePreview();
        check(settle() && until([&] { return pagedRequests.value("slow:2") == 1; }), "a slow walk is under way");
        service->cancelWork();
        check(walked() && showFor("slow show").value("count").toInt() == 9 &&
                  showFor("slow show").value("olderState") == "complete",
              "cancelling a task does not stop the walk through older pages");
        {
            auto quitting = std::make_unique<SpokenMediaService>();
            until([&] { return !quitting->busy(); });
            quitting->previewFeed(QUrl(base + "/slow2"));
            until([&] { return !quitting->busy(); });
            quitting->subscribePreview();
            check(until([&] { return pagedRequests.value("slow2:2") == 1; }), "fixture: a walk in flight when Zuuned quits");
            quitting.reset();
        }
        {
            SpokenMediaService resumed;
            QString slowId;
            until([&] {
                for (const auto &v : resumed.collections("show"))
                    if (v.toMap().value("title") == "slow2 show") slowId = v.toMap().value("id").toString();
                return !slowId.isEmpty() && !resumed.busy() && !resumed.loadingOlder() &&
                       resumed.showSummary(slowId).value("count").toInt() == 9;
            }, 30000);
            check(resumed.showSummary(slowId).value("count").toInt() == 9 &&
                      resumed.showSummary(slowId).value("olderState") == "complete" && pagedRequests.value("slow2:2") == 2,
                  "a walk interrupted by quitting resumes at the next start");
        }
        // Older pages only add episodes: a stale copy of the newest GUID changes nothing.
        service->previewFeed(QUrl(base + "/dupe"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("dupe show").value("count").toInt() == 6 &&
                  titlesOf(service->episodePage(showFor("dupe show").value("id").toString(), {{"pageSize", 1}})) ==
                      QStringList{"dupe episode 6"},
              "an older page repeating the newest GUID cannot overwrite it");
        service->previewFeed(QUrl(base + "/empty"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("empty show").value("count").toInt() == 3 &&
                  showFor("empty show").value("olderState") == "complete" && pagedRequests.value("empty:3") == 0,
              "an empty page ends the chain even with a next link");
        // A page that always fails: the cursor is retried, then the walk starts again from the
        // first older page, and after three failures only a refresh the user asks for retries.
        service->previewFeed(QUrl(base + "/stuck"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("stuck show").value("count").toInt() == 6 &&
                  showFor("stuck show").value("olderState") == "incomplete" &&
                  showFor("stuck show").value("olderPage") == base + "/stuck?page=3" &&
                  showFor("stuck show").value("olderFailures").toInt() == 1,
              "a deep page failing leaves the show incomplete with its cursor on that page");
        const QString stuckId = showFor("stuck show").value("id").toString();
        service->refreshFeed(stuckId);
        check(settle() && walked() && pagedRequests.value("stuck:3") == 2 && pagedRequests.value("stuck:2") == 1 &&
                  showFor("stuck show").value("olderFailures").toInt() == 2,
              "the next refresh retries only the page that failed");
        service->refreshFeed(stuckId);
        check(settle() && walked() && pagedRequests.value("stuck:2") == 2 && pagedRequests.value("stuck:3") == 3 &&
                  showFor("stuck show").value("olderFailures").toInt() == 3,
              "after two failures the walk starts again from the first older page");
        bool quietDone = false;
        service->refreshFeedForQueue(stuckId, [&](bool) { quietDone = true; });
        check(until([&] { return quietDone; }) && settle() && walked() && pagedRequests.value("stuck:3") == 3 &&
                  pagedRequests.value("stuck:2") == 2,
              "after three failures automatic refreshes stop retrying the walk");
        stuckNoNext = true;
        service->refreshFeed(stuckId);
        check(settle() && walked() && pagedRequests.value("stuck:3") == 4 &&
                  showFor("stuck show").value("olderState") == "incomplete" &&
                  showFor("stuck show").value("count").toInt() == 6,
              "a refresh the user asks for retries; a response without a next link leaves the walk unfinished");
        check(service->podcastCleanupCandidates({}, true).size() == 2,
              "without the Zune's inventory, cleanup candidates are the sent episodes, not the whole library");
        // A show keeps at most 100,000 episodes; newer ones past that are left out and reported.
        service->previewFeed(QUrl(base + "/capped"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("capped show").value("count").toInt() == 6, "a capped fixture show subscribes");
        const QString cappedId = showFor("capped show").value("id").toString();
        {
            sqlite3 *db = nullptr;
            const auto dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken/spoken.db";
            bool filled = sqlite3_open(QFile::encodeName(dbPath).constData(), &db) == SQLITE_OK;
            sqlite3_stmt *stmt = nullptr;
            filled = filled && sqlite3_prepare_v2(db,
                "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<99993) "
                "INSERT INTO items(id,kind,parent,identity,payload) SELECT 'cap-'||x,'episode',?1,'guid:filler-'||x,"
                "json_object('id','cap-'||x,'kind','episode','parent',?1,'identity','guid:filler-'||x,'title','Filler '||x,"
                "'sourceUrl','https://example.test/filler/'||x,'published','2001-01-01T00:00:00Z') FROM n", -1, &stmt, nullptr) == SQLITE_OK;
            if (filled) {
                const auto id = cappedId.toUtf8();
                sqlite3_bind_text(stmt, 1, id.constData(), id.size(), SQLITE_TRANSIENT);
                filled = sqlite3_step(stmt) == SQLITE_DONE;
            }
            sqlite3_finalize(stmt);
            if (db) sqlite3_close(db);
            check(filled, "fixture: the show holds 99,999 episodes");
        }
        cappedCount = 9;
        service->refreshFeed(cappedId);
        check(settle() && walked() && service->showSummary(cappedId).value("count").toInt() == 100000 &&
                  service->showSummary(cappedId).value("olderState") == "limited" &&
                  service->showSummary(cappedId).value("olderError").toString().contains("100,000"),
              "a show stops at 100,000 episodes and says so");
        // The actual show page: one list, 25 more as it scrolls, a newest/oldest toggle.
        service->previewFeed(QUrl(base + "/paged-big"));
        settle();
        service->subscribePreview();
        check(settle() && walked() && showFor("paged-big show").value("count").toInt() == 250,
              "a 250-episode show in three documents is saved");
        const QString bigId = showFor("paged-big show").value("id").toString();
        page->setProperty("books", false);
        QMetaObject::invokeMethod(page, "openCollection", Q_ARG(QVariant, bigId));
        QTest::qWait(300);
        auto *episodeList = page->findChild<QQuickItem *>("spokenEpisodeList");
        auto count = [&] { return episodeList->property("count").toInt(); };
        auto firstTitle = [&] {
            QJSValue row;
            QMetaObject::invokeMethod(episodeList->property("model").value<QObject *>(), "get", Q_RETURN_ARG(QJSValue, row), Q_ARG(int, 0));
            return row.property("row").property("title").toString();
        };
        auto pressItem = [&](const char *name) {
            auto *button = page->findChild<QQuickItem *>(name);
            if (!button || !button->isVisible() || !button->isEnabled()) return false;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTest::qWait(150);
            return true;
        };
        auto toEnd = [&] {
            episodeList->setProperty("contentY", episodeList->property("contentHeight").toDouble() - episodeList->height());
            QTest::qWait(100);
        };
        check(count() > 0 && count() < 250 && count() % 25 == 0 && firstTitle() == "paged-big episode 250" &&
                  !page->findChild<QQuickItem *>("spokenEpisodePager"),
              "the show page lists the newest episodes 25 at a time, without page buttons");
        const int loaded = count();
        toEnd();
        check(until([&] { return count() > loaded; }), "scrolling towards the end loads the next 25");
        for (int i = 0; i < 20 && count() < 250; ++i) toEnd();
        check(count() == 250, "scrolling reaches the oldest episode");
        episodeList->setProperty("contentY", 300.0);
        QTest::qWait(100);
        const double place = episodeList->property("contentY").toDouble();
        service->setFavorite(showFor("paged-big show").value("id").toString(), true);
        check(settle() && qAbs(episodeList->property("contentY").toDouble() - place) < 1 && count() == 250,
              "a library change keeps the list where it was");
        check(pressItem("spokenEpisodeOrder") && until([&] { return firstTitle() == "paged-big episode 1"; }) &&
                  count() > 0 && count() < 250 &&
                  until([&] { return qAbs(episodeList->property("contentY").toDouble() -
                                          episodeList->property("originY").toDouble()) < 1; }),
              "the toggle lists the oldest first, seen from the top");
        check(pressItem("spokenSelectAll") && page->property("checks").toMap().size() == 250,
              "select all covers the whole show, not just the loaded rows");
        pressItem("spokenSelectAll");
        auto *episodeSearch = page->findChild<QQuickItem *>("spokenEpisodeSearch");
        episodeSearch->setProperty("text", "Episode 7");
        // Episode 7 and 70 to 79: none of them among the first rows loaded.
        check(until([&] { return count() == 11; }), "searching a show's episodes finds them anywhere in the show");
        check(window->grabWindow().save(root + "/native-podcast-pages.png"), "podcast episode list captured");
        QMetaObject::invokeMethod(page, "openCollection", Q_ARG(QVariant, QString()));
    }
    for (const auto &e : qmlErrors)
        fprintf(stderr, "[native-qml] %s\n", qPrintable(e));
    check(qmlErrors.isEmpty(), "native QML has no runtime warnings");
    player->stop();
    failures += runPodcastPlannerChecks();
    printf("Spoken native gate: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
