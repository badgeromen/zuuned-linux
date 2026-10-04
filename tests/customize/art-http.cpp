// Actual Qt HTTP transport against a controlled loopback provider.
#include "library/ArtworkHttp.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) {
        fprintf(stderr, "Controlled HTTP server could not bind: %s\n", qPrintable(server.errorString()));
        return 2;
    }
    QElapsedTimer clock; clock.start();
    QHash<QByteArray, int> count;
    QHash<QByteArray, QList<qint64>> arrived;
    QList<qint64> allRequests;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (QTcpSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                if (!request.contains("\r\n\r\n")) { socket->setProperty("request", request); return; }
                if (socket->property("served").toBool()) return;
                socket->setProperty("served", true);
                const QByteArray path = request.split(' ').value(1);
                const int attempt = ++count[path];
                arrived[path].append(clock.elapsed()); allRequests.append(clock.elapsed());
                int status = 200;
                QByteArray retry;
                if (path == "/recover503" && attempt == 1) { status = 503; retry = "1"; }
                if (path == "/recover429" && attempt == 1) { status = 429; retry = "2"; }
                if (path == "/permanent404") status = 404;
                if (path == "/busy") { status = 503; retry = "0"; }
                if (path == "/long") { status = 503; retry = "60"; }
                const QByteArray body = status == 200 ? QByteArray("{\"release-groups\":[{\"id\":\"fixture\"}]}")
                                                      : QByteArray("Provider failure, no magic retry phrase.");
                QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\n";
                response += "Content-Type: application/json\r\nConnection: close\r\n";
                if (!retry.isEmpty()) response += "Retry-After: " + retry + "\r\n";
                response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
                socket->write(response); socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failures;
    };
    auto get = [&](const QString &path) {
        QFutureWatcher<ArtworkHttp::Response> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<ArtworkHttp::Response>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([url = QUrl(base + path)] { return ArtworkHttp::get(url, true); }));
        loop.exec();
        return watcher.result();
    };
    auto result = get(QStringLiteral("/recover503"));
    check(result.success() && count["/recover503"] == 2 && result.body.contains("release-groups"),
          "HTTP503 retries by status even without the old body phrase, then returns artwork data");
    result = get(QStringLiteral("/recover429"));
    check(result.success() && count["/recover429"] == 2
          && arrived["/recover429"].last() - arrived["/recover429"].first() >= 1950,
          "HTTP429 observes Retry-After before retrying");
    result = get(QStringLiteral("/permanent404"));
    const QString missing = ArtworkHttp::userMessage(result, true);
    check(!result.success() && result.status == 404 && count["/permanent404"] == 1
          && !result.body.isEmpty(), "permanent errors preserve their status and body without retrying");
    check(!missing.contains(QStringLiteral("http")) && !missing.contains(QStringLiteral("127.0.0.1"))
          && missing.contains(QStringLiteral("Choose another")), "UI error is actionable and contains no transport URL");
    result = get(QStringLiteral("/busy"));
    check(!result.success() && count["/busy"] == 3
          && ArtworkHttp::userMessage(result, true).contains(QStringLiteral("MusicBrainz is busy")),
          "persistent overload stops after three attempts with a clear provider message");
    {
        QFutureWatcher<QPair<QByteArray, QString>> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<QPair<QByteArray, QString>>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([url = QUrl(base + QStringLiteral("/ok"))] {
            QString error = QStringLiteral("stale failure from an earlier lookup");
            const auto body = ArtworkHttp::getBody(url, true, &error);
            return qMakePair(body, error);
        }));
        loop.exec();
        check(!watcher.result().first.isEmpty() && watcher.result().second.isEmpty(),
              "successful lookup clears an earlier error string");
    }
    {
        QFutureWatcher<ArtworkHttp::Response> first, second;
        QEventLoop loop;
        int remaining = 2;
        auto finish = [&] { if (--remaining == 0) loop.quit(); };
        QObject::connect(&first, &QFutureWatcher<ArtworkHttp::Response>::finished, &loop, finish);
        QObject::connect(&second, &QFutureWatcher<ArtworkHttp::Response>::finished, &loop, finish);
        first.setFuture(QtConcurrent::run([url = QUrl(base + QStringLiteral("/first"))] { return ArtworkHttp::get(url, true); }));
        second.setFuture(QtConcurrent::run([url = QUrl(base + QStringLiteral("/second"))] { return ArtworkHttp::get(url, true); }));
        loop.exec();
        check(first.result().success() && second.result().success()
              && qAbs(arrived["/first"].first() - arrived["/second"].first()) >= 1000,
              "concurrent lookups share the same MusicBrainz request throttle");
    }
    bool paced = true;
    for (int i = 1; i < allRequests.size(); ++i) paced &= allRequests[i] - allRequests[i - 1] >= 1000;
    check(paced, "all MusicBrainz attempts, including retries, are at least one second apart");
    const auto dateHeader = QDateTime::currentDateTimeUtc().addSecs(5).toString(
        QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'")).toLatin1();
    const qint64 dateWait = ArtworkHttp::retryAfterMs(dateHeader);
    check(dateWait >= 3500 && dateWait <= 5000 && ArtworkHttp::retryAfterMs("not a date") == -1,
          "Retry-After supports HTTP dates and rejects malformed headers");
    QElapsedTimer longWait; longWait.start();
    result = get(QStringLiteral("/long"));
    check(!result.success() && count["/long"] == 1 && longWait.elapsed() < 3000,
          "long server cooldown stops automatic retries without keeping the lookup waiting");
    result = get(QStringLiteral("/after-long"));
    check(!result.success() && count["/after-long"] == 0,
          "a later concurrent lookup also respects the server-wide cooldown");
    printf("Artwork HTTP integration: %s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
