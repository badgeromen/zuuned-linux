#include "AlbumArtService.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QSemaphore>
#include <atomic>
#include <cstdio>

std::atomic<int> extractions{0};
QSemaphore entered, release;
extern "C" int zuuned_extract_art(const char *source, const char *destination, int) {
    ++extractions;
    if (QString::fromUtf8(source).endsWith("embedded.mp3")) {
        QImage cover(40, 40, QImage::Format_RGB32); cover.fill(Qt::green);
        return cover.save(QString::fromUtf8(destination), "JPEG") ? 0 : -1;
    }
    if (QString::fromUtf8(source).endsWith("blocked.mp3")) {
        entered.release();
        release.acquire();
    }
    return -1;
}

namespace {
int failures = 0;

void check(bool condition, const char *message) {
    if (condition) {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    check(root.isValid(), "isolated album-art cache is available");
    qputenv("XDG_CACHE_HOME", root.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("ZuunedTests"));
    QCoreApplication::setApplicationName(QStringLiteral("DeviceAlbumArt"));

    const QString source = root.filePath(QStringLiteral("device-cover.png"));
    QImage image(640, 480, QImage::Format_RGB32);
    image.fill(QColor(QStringLiteral("#2468a0")));
    check(image.save(source, "PNG"), "device representative sample fixture is written");

    AlbumArtService service;
    bool completed = false;
    QString importedPath;
    QEventLoop wait;
    QObject::connect(&service, &AlbumArtService::artReady, &wait,
                     [&](const QString &artist, const QString &album,
                         const QString &path, bool ok) {
        if (artist == QLatin1String("Artist")
                && album == QLatin1String("Album")) {
            completed = ok;
            importedPath = path;
            wait.quit();
        }
    });
    service.importDeviceArt(QStringLiteral("Artist"), QStringLiteral("Album"),
                            source);
    QTimer::singleShot(5000, &wait, &QEventLoop::quit);
    wait.exec();
    const QImage imported(importedPath);
    check(completed && !imported.isNull(),
          "Zune album JPEG is promoted into the local album cache");
    check(imported.width() <= 400 && imported.height() <= 400,
          "imported device art is normalized to the local cache bounds");

    const QByteArray originalBytes = [&] {
        QFile file(importedPath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    QImage replacement(100, 100, QImage::Format_RGB32);
    replacement.fill(QColor(QStringLiteral("#ff0000")));
    const QString replacementPath = root.filePath(QStringLiteral("replacement.png"));
    replacement.save(replacementPath, "PNG");
    service.importDeviceArt(QStringLiteral("Artist"), QStringLiteral("Album"),
                            replacementPath);
    QFile retained(importedPath);
    const QByteArray retainedBytes = retained.open(QIODevice::ReadOnly)
        ? retained.readAll() : QByteArray();
    check(!originalBytes.isEmpty() && retainedBytes == originalBytes,
          "device import never replaces artwork already chosen or cached locally");

    auto spin = [](auto ready, int timeout = 3000) {
        QElapsedTimer timer; timer.start();
        while (!ready() && timer.elapsed() < timeout) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        return ready();
    };
    const QString emptyDir = root.filePath("first-library/album");
    const QString artDir = root.filePath("second-library/album");
    QDir().mkpath(emptyDir); QDir().mkpath(artDir);
    const QString emptyTrack = emptyDir + "/track.mp3";
    QFile track(emptyTrack); track.open(QIODevice::WriteOnly); track.close();
    check(image.save(artDir + "/cover.png"), "second library cover fixture is written");
    int results = 0; bool lastOk = false;
    QObject::connect(&service, &AlbumArtService::artReady, &app,
        [&](const QString &, const QString &album, const QString &, bool ok) {
            if (album.startsWith("Multi")) { ++results; lastOk = ok; }
        });
    service.requestArt("Artist", "Multi-library", emptyTrack);
    check(spin([&] { return results == 1; }) && !lastOk,
          "first library without cover reports a miss");
    const int previous = extractions.load();
    service.requestArt("Artist", "Multi-library", emptyTrack);
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    check(extractions == previous, "same missing source does not cause extraction storms");
    results = 1;
    service.requestArt("Artist", "Multi-library", artDir + "/track.mp3");
    check(spin([&] { return results == 2; }) && lastOk,
          "second library supplies art after the first source was negatively cached");

    const QString blockedTrack = emptyDir + "/blocked.mp3";
    QFile blocked(blockedTrack); blocked.open(QIODevice::WriteOnly); blocked.close();
    service.requestArt("Artist", "Multi-inflight", blockedTrack);
    check(spin([&] { return entered.available() > 0; }), "first extraction is in flight");
    service.requestArt("Artist", "Multi-inflight", artDir + "/track.mp3");
    release.release();
    check(spin([&] { return results == 3; }) && lastOk,
          "second source queued during a failed extraction is tried without another UI request");
    const QString embeddedTrack = emptyDir + "/embedded.mp3";
    QFile embedded(embeddedTrack); embedded.open(QIODevice::WriteOnly); embedded.close();
    service.requestSources("Artist", "Multi-batch", {emptyTrack, embeddedTrack});
    check(spin([&] { return results == 4; }) && lastOk,
          "complete local batch finds later embedded cover before reporting any miss for online fallback");
    QThreadPool::globalInstance()->waitForDone();
    return failures == 0 ? 0 : 1;
}
