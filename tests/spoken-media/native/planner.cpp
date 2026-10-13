#include "spoken/PodcastQueuePlanner.h"
#include "spoken/SpokenMediaService.h"
#include "spoken/SpokenStore.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimeZone>
#include <functional>
#include <memory>

namespace {
bool waitFor(const std::function<bool()> &done, int timeout = 5000) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (done()) return true;
        QTest::qWait(5);
    } while (timer.elapsed() < timeout);
    return done();
}
struct Profile {
    QString original = QCoreApplication::applicationName();
    Profile() {
        static int serial = 0;
        QCoreApplication::setApplicationName(QStringLiteral("PodcastPlannerGate%1").arg(++serial));
    }
    ~Profile() { QCoreApplication::setApplicationName(original); }
};
struct FeedServer {
    QTcpServer server;
    QString base;
    bool hold = false;
    QSet<QString> failed;
    QHash<QString, int> requests;
    QList<QPair<QPointer<QTcpSocket>, QString>> held;
    int audioRequests = 0;
    int episodeCount = 8;
    FeedServer() {
        server.listen(QHostAddress::LocalHost, 0);
        base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const auto request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                    socket->setProperty("answered", true);
                    const auto path = QString::fromUtf8(request.split(' ').value(1));
                    if (path.startsWith("/audio/")) ++audioRequests;
                    ++requests[path];
                    if (hold) held.append({socket, path});
                    else answer(socket, path);
                });
            }
        });
    }
    QString source(const QString &show, int number) const {
        return base + "/audio/" + show + "/" + QString::number(number) + ".mp3";
    }
    QByteArray body(const QString &path) const {
        const auto show = path.mid(1);
        QString rss = "<rss version=\"2.0\"><channel><title>" + show + "</title><description>Fixture</description>";
        for (int n = 1; n <= episodeCount; ++n) {
            const auto date = QDateTime(QDate(2026, 10, n), QTime(12, 0), QTimeZone::UTC);
            rss += "<item><guid>" + show + "-" + QString::number(n) + "</guid><title>Episode " +
                QString::number(n) + "</title><pubDate>" + date.toString(Qt::RFC2822Date) +
                "</pubDate><enclosure url=\"" + source(show, n) +
                "\" type=\"audio/mpeg\" length=\"200000\"/></item>";
        }
        return (rss + "</channel></rss>").toUtf8();
    }
    void answer(QTcpSocket *socket, const QString &path) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        const auto payload = failed.contains(path) ? QByteArray("unavailable") : body(path);
        socket->write(QByteArray("HTTP/1.1 ") + (failed.contains(path) ? "503 Unavailable" : "200 OK") +
            "\r\nContent-Type: application/rss+xml\r\nContent-Length: " + QByteArray::number(payload.size()) +
            "\r\nConnection: close\r\n\r\n" + payload);
        socket->disconnectFromHost();
    }
    void release() {
        hold = false;
        const auto pending = held;
        held.clear();
        for (const auto &entry : pending) if (entry.first) answer(entry.first, entry.second);
    }
    int totalRequests() const {
        int count = 0;
        for (auto n : requests) count += n;
        return count;
    }
};
struct Fixture {
    Profile profile;
    FeedServer feeds;
    PodcastQueuePlanner::Context context;
    QHash<QString, QVariantMap> statuses;
    QStringList showIds;
    QHash<int, QString> episodeIds;
    QVariantList batches;
    QStringList notices;
    int inventoryRequests = 0;
    bool ready = false;
    std::function<void()> duringAdmission;
    std::unique_ptr<SpokenMediaService> service;
    std::unique_ptr<PodcastQueuePlanner> planner;
    using Edit = std::function<void(QVariantMap &, int)>;
    Fixture(Edit edit = {}, int shows = 1) {
        context.connected = true;
        context.serial = "planner-device";
        context.generation = 1;
        context.inventoryReady = true;
        context.count = 3;
        QString error;
        {
            SpokenStore store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken");
            ready = feeds.server.isListening() && store.open(&error);
            for (int s = 0; s < shows && ready; ++s) {
                const auto name = QStringLiteral("feed%1").arg(s);
                QVariantMap show{{"kind", "show"}, {"identity", feeds.base + "/" + name},
                    {"url", feeds.base + "/" + name}, {"title", name}, {"favorite", true}};
                ready = store.put(&show, &error);
                showIds.append(show.value("id").toString());
                for (int n = 1; n <= 8 && ready; ++n) {
                    QVariantMap episode{{"kind", "episode"}, {"parent", showIds.last()},
                        {"identity", "guid:" + name + "-" + QString::number(n)},
                        {"title", QStringLiteral("Episode %1").arg(n)}, {"sourceUrl", feeds.source(name, n)},
                        {"published", QDateTime(QDate(2026, 10, n), QTime(12, 0), QTimeZone::UTC).toString(Qt::ISODate)}};
                    if (edit) edit(episode, n);
                    ready = store.put(&episode, &error);
                    if (!s) episodeIds[n] = episode.value("id").toString();
                }
            }
        }
        service = std::make_unique<SpokenMediaService>();
        service->attachDeviceQueue([this] { return context.connected && !context.syncing; });
        QObject::connect(service.get(), &SpokenMediaService::podcastTransferReady, service.get(), [this](const QVariantList &rows) {
            batches.append(QVariant(rows));
            if (duringAdmission) duringAdmission();
        });
        QObject::connect(service.get(), &SpokenMediaService::notification, service.get(), [this](const QString &message) { notices.append(message); });
        planner = std::make_unique<PodcastQueuePlanner>(service.get(), PodcastQueuePlanner::Ports{
            [this] { return context; },
            [this](const QString &, const QString &source) {
                return statuses.value(source, QVariantMap{{"known", true}, {"onDevice", false}, {"playedKnown", false}, {"played", false}});
            },
            [this] { ++inventoryRequests; }});
        ready = settle() && ready;
    }
    ~Fixture() { feeds.release(); planner.reset(); service.reset(); }
    bool settle() {
        QElapsedTimer quiet;
        quiet.start();
        return waitFor([&] {
            if (service->busy()) { quiet.restart(); return false; }
            return quiet.elapsed() > 60;
        });
    }
    void enable() { context.enabled = true; planner->update(); }
    QStringList titles() const {
        QStringList result;
        for (const auto &batch : batches)
            for (const auto &row : batch.toList()) result.append(row.toMap().value("title").toString());
        return result;
    }
    void live(int number, bool played = false, bool known = true) {
        statuses[feeds.source("feed0", number)] = {{"known", true}, {"onDevice", true}, {"playedKnown", known}, {"played", played}};
    }
};
}

int runPodcastPlannerChecks() {
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) ++failures;
        printf("%s planner: %s\n", ok ? "PASS" : "FAIL", label);
        fflush(stdout);
    };
    {
        Fixture f;
        check(f.ready && f.batches.isEmpty() && f.feeds.totalRequests() == 0 && !f.inventoryRequests,
              "disabled automation does not refresh feeds or inspect inventory");
        f.live(8, false, false);
        f.enable();
        check(f.settle() && f.titles() == QStringList{"Episode 7", "Episode 6"},
              "existing newest episode with unknown playback counts toward newest three");
        bool drafts = !f.batches.isEmpty();
        for (const auto &row : f.batches.value(0).toList())
            drafts = drafts && row.toMap().value("needsPreparation").toBool() && row.toMap().value("filepath").toString().isEmpty();
        check(drafts && f.feeds.audioRequests == 0, "feed-only episodes enter queue without downloading audio");
        f.batches.clear();
        f.service->setFavorite(f.episodeIds.value(2), true);
        check(f.settle() && f.batches.isEmpty() && f.feeds.totalRequests() == 1,
              "unrelated episode favorite save does not refill reviewed or cleared queue");
        f.context.connected = false; f.planner->update();
        f.context.connected = true; ++f.context.generation; f.planner->update();
        check(f.settle() && f.titles() == QStringList{"Episode 7", "Episode 6"} && f.feeds.totalRequests() == 2,
              "physical reconnect rearms exactly one show refresh");
    }
    {
        Fixture f([](QVariantMap &row, int n) {
            if (n == 8 || n == 5) row["played"] = true;
            if (n == 5) row["keepOnDevice"] = true;
            if (n == 6) row["devicePlayedSerials"] = QVariantMap{{"planner-device", true}};
            if (n == 4) row["devicePlayedSerials"] = QVariantMap{{"another-device", true}};
        });
        f.live(7, true);
        f.enable();
        check(f.ready && f.settle() && f.titles() == QStringList{"Episode 4", "Episode 3", "Episode 2"},
              "local live and this-device history filter before limit; other-device history does not");
        check(f.service->item(f.episodeIds.value(5)).value("keepOnDevice").toBool() &&
              !f.titles().contains("Episode 5"), "Keep remains protected metadata without auto-adding a played episode");
    }
    {
        Fixture f;
        f.service->setPodcastSyncSelection(f.showIds.first(), "oldest", 2, false);
        check(f.settle(), "saved oldest selection finishes before manual request");
        f.service->queuePodcastSelection(f.showIds.first());
        check(f.settle() && f.titles() == QStringList{"Episode 1", "Episode 2"},
              "manual plus refreshes and applies saved oldest count while automation is off");
    }
    {
        Fixture f;
        f.feeds.hold = true; f.enable();
        check(waitFor([&] { return !f.feeds.held.isEmpty(); }), "held RSS fixture reaches pending response");
        f.context.enabled = false; f.planner->update(); f.feeds.release();
        check(f.settle() && f.batches.isEmpty(), "disable during RSS request cancels admission");
    }
    {
        Fixture f;
        f.feeds.hold = true; f.enable();
        const bool held = waitFor([&] { return !f.feeds.held.isEmpty(); });
        f.context.connected = false; f.planner->update();
        f.context.connected = true; ++f.context.generation; f.planner->update();
        f.feeds.release();
        check(held && f.settle() && f.batches.size() == 1 && f.feeds.totalRequests() == 2,
              "same-serial reconnect discards old response and starts one fresh generation pass");
    }
    {
        Fixture f;
        f.feeds.hold = true; f.enable();
        const bool held = waitFor([&] { return !f.feeds.held.isEmpty(); });
        f.service->setFavorite(f.showIds.first(), false);
        f.feeds.release();
        check(held && f.settle() && !f.service->item(f.showIds.first()).value("favorite").toBool() && f.batches.isEmpty(),
              "unstar queued behind held feed finishes before any automatic admission");
    }
    {
        Fixture f({}, 2);
        f.feeds.failed.insert("/feed0");
        f.enable();
        check(f.ready && f.settle() && f.feeds.requests.value("/feed0") == 1 && f.feeds.requests.value("/feed1") == 1 &&
              f.batches.size() == 1 && f.batches.first().toList().first().toMap().value("album") == "feed1",
              "one failed feed leaves a healthy starred show's automatic queue working");
    }
    {
        Fixture f;
        f.context.inventoryReady = false;
        f.context.inventoryError = "Fixture read failed";
        f.enable();
        check(f.settle() && f.batches.isEmpty() && !f.notices.isEmpty(),
              "unknown failed inventory cannot authorize automatic additions");
        f.context.inventoryError.clear(); f.context.inventoryReady = true; f.planner->update();
        check(f.settle() && f.batches.size() == 1 && f.feeds.totalRequests() == 1,
              "successful inventory recovery admits retained request without another RSS fetch");
    }
    {
        Fixture f;
        f.context.queueMismatch = true; f.enable();
        check(f.settle() && f.feeds.totalRequests() == 0 && f.batches.isEmpty(),
              "queue stamped for another device defers all automatic work");
        f.context.queueMismatch = false; f.context.syncing = true; f.planner->update();
        check(f.settle() && f.feeds.totalRequests() == 0, "active sync defers automatic refresh and admission");
        f.context.syncing = false; f.planner->update();
        check(f.settle() && f.batches.size() == 1, "idle matching device resumes deferred automatic work");
    }
    {
        Fixture f;
        f.feeds.hold = true; f.enable();
        const bool held = waitFor([&] { return !f.feeds.held.isEmpty(); });
        f.context.syncing = true; f.planner->update(); f.feeds.release();
        const bool deferred = f.settle() && f.batches.isEmpty();
        f.context.syncing = false; f.planner->update();
        check(held && deferred && f.settle() && f.batches.size() == 1 && f.feeds.totalRequests() == 2,
              "sync beginning during refresh cancels stale admission and retries when idle");
    }
    {
        Fixture f;
        f.feeds.hold = true; f.enable();
        const bool held = waitFor([&] { return !f.feeds.held.isEmpty(); });
        f.planner->dismissPending(); f.feeds.release();
        check(held && f.settle() && f.batches.isEmpty(),
              "explicit queue clear cancels pending RSS admission");
        f.feeds.episodeCount = 9;
        f.context.connected = false; f.planner->update();
        f.context.connected = true; ++f.context.generation; f.planner->update();
        check(f.settle() && f.titles() == QStringList{"Episode 9", "Episode 8", "Episode 7"},
              "reconnect after clear refreshes and discovers newly published feed-only episode");
    }
    {
        Fixture f({}, 3);
        f.duringAdmission = [&] { f.planner->update(); };
        f.enable();
        check(f.settle() && f.batches.size() == 3 && f.feeds.totalRequests() == 3,
              "synchronous queue callback cannot start overlapping or repeated show passes");
    }
    printf("Podcast planner gate: %d checks, %d failures\n", checks, failures);
    return failures;
}
