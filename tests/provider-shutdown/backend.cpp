#include "library/ArtworkHttp.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <cstdio>
#include <memory>

static std::atomic_int lifecycleWarnings{0};
static void messages(QtMsgType, const QMessageLogContext &, const QString &message) {
    if (message.contains("without QApplication") || message.contains("Timers cannot")) ++lifecycleWarnings;
    fprintf(stderr, "%s\n", qPrintable(message));
}

int main(int argc, char **argv) {
    auto app = std::make_unique<QCoreApplication>(argc, argv);
    qInstallMessageHandler(messages);
    QThreadPool::globalInstance()->setMaxThreadCount(4);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 2;
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/stall").arg(server.serverPort()));
    QVector<QFuture<ArtworkHttp::Response>> requests;
    requests.append(QtConcurrent::run([url] { return ArtworkHttp::get(url, true); }));
    requests.append(QtConcurrent::run([url] { return ArtworkHttp::get(url, false); }));
    int connections = 0;
    bool timedOut = false;
    QObject::connect(&server, &QTcpServer::newConnection, app.get(), [&] {
        while (server.hasPendingConnections()) {
            server.nextPendingConnection(); // Deliberately never return HTTP headers.
            ++connections;
        }
        if (connections == 2) {
            // This request waits on the pacing mutex held by the first one.
            requests.append(QtConcurrent::run([url] { return ArtworkHttp::get(url, true); }));
            QTimer::singleShot(100, app.get(), &QCoreApplication::quit);
        }
    });
    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, [] { ArtworkHttp::beginShutdown(); });
    QTimer::singleShot(5000, app.get(), [&] { timedOut = true; app->quit(); });
    app->exec();
    QElapsedTimer elapsed;
    elapsed.start();
    QThreadPool::globalInstance()->waitForDone();
    const qint64 drainMs = elapsed.elapsed();
    int failed = 0, checks = 0;
    auto check = [&](bool ok, const char *label) {
        ++checks; if (!ok) ++failed;
        printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    };
    check(!timedOut && connections == 2 && requests.size() == 3, "active provider and mutex-waiting jobs established");
    bool cancelled = requests.size() == 3;
    for (const auto &request : requests)
        cancelled &= request.isFinished() && request.result().networkError == QNetworkReply::OperationCanceledError;
    check(cancelled, "shutdown cancels active requests and mutex waiters");
    check(drainMs < 1000, "worker drain finishes within one second");
    check(ArtworkHttp::get(url).networkError == QNetworkReply::OperationCanceledError,
          "shutdown rejects new requests before application teardown");
    server.close();
    app.reset();
    check(ArtworkHttp::get(url, true).networkError == QNetworkReply::OperationCanceledError,
          "late request after application destruction creates no event loop");
    check(lifecycleWarnings.load() == 0, "no application/event-loop lifecycle warnings");
    printf("Provider shutdown: %d checks, %d failures; drain %lld ms\n", checks, failed, static_cast<long long>(drainMs));
    return failed ? 1 : 0;
}
