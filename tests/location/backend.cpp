// Real LibraryService + QML dependency binding, with disposable local media.
#include "LibraryService.h"
#include "library/LibraryDb.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <memory>

static bool writeWave(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4); out << quint32(196);
    out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
    out << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    out.writeRawData("data", 4); out << quint32(160);
    const QByteArray silence(160, '\0'); out.writeRawData(silence.constData(), silence.size());
    return out.status() == QDataStream::Ok;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ZuunedLocationFixture"));
    app.setApplicationName(QStringLiteral("LibraryMembership"));
    const QString root = qEnvironmentVariable("LOCATION_BACKEND_ROOT");
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith(QLatin1String("/tmp/zuuned-location-backend-"))
        || !data.startsWith(root + QLatin1Char('/'))) return 2;
    QDir().mkpath(data);
    const QString mediaRoot = root + QStringLiteral("/music");
    QDir().mkpath(mediaRoot);
    LibraryDb db;
    if (!db.open(data + QStringLiteral("/library.db"))) return 2;
    LibTrack seed;
    seed.title = QStringLiteral("Original Song"); seed.album = QStringLiteral("Album");
    seed.artist = QStringLiteral("Performer"); seed.albumartist = QStringLiteral("Owner");
    // The badge describes a library row even when its storage is offline.
    seed.filepath = mediaRoot + QStringLiteral("/offline.flac");
    if (!db.upsertTrack(seed) || !db.addWatchFolder(mediaRoot, QStringLiteral("music"))) return 2;
    const qint64 trackId = db.allTracks().first().id;
    const qint64 folderId = db.watchFolders().first().id;
    int failed = 0;
    auto check = [&](bool ok, const char *label) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) ++failed;
    };

    LibraryService service;
    check(service.hasLocalTrack("OWNER", "album", "original song")
          && service.hasLocalTrack("Performer", "Album", "Original Song")
          && !QFile::exists(seed.filepath),
          "initial index recognizes both artist aliases without requiring online storage");
    check(!service.hasLocalTrack("", "Album", "Original Song")
          && !service.hasLocalTrack("Owner", "", "Original Song")
          && !service.hasTrack("", "", "Original Song"),
          "badges and import matching both refuse incomplete title-only identity");

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("fixtureLibrary"), &service);
    QQmlComponent component(&engine);
    component.setData(R"qml(
        import QtQml
        QtObject {
            property string wantedTitle: "Original Song"
            property bool present: {
                void fixtureLibrary.localTrackIdentityRevision
                return fixtureLibrary.hasLocalTrack("Owner", "Album", wantedTitle)
            }
        }
    )qml", QUrl());
    std::unique_ptr<QObject> binding(component.create());
    if (!binding) {
        fprintf(stderr, "%s\n", qPrintable(component.errorString()));
        return 2;
    }
    check(binding->property("present").toBool(), "QML can bind the revision and query the native membership index");
    int notifications = 0;
    bool freshAtNotification = false;
    QObject::connect(&service, &LibraryService::localTrackIdentitiesChanged, &app, [&] {
        ++notifications;
        freshAtNotification = service.hasLocalTrack("Owner", "Album", "Renamed Song")
            && !service.hasLocalTrack("Owner", "Album", "Original Song");
    });
    const quint64 firstRevision = service.localTrackIdentityRevision();
    const auto edited = service.editTrackMetadata(double(trackId), {{"title", "Renamed Song"}});
    check(edited.value("success").toBool() && service.trackCount() == 1
          && service.localTrackIdentityRevision() > firstRevision && notifications == 1
          && freshAtNotification && !binding->property("present").toBool(),
          "same-count metadata edits refresh device bindings with the new index already installed");
    const quint64 renamedRevision = service.localTrackIdentityRevision();
    const auto genre = service.editTrackMetadata(double(trackId), {{"genre", "Alternative"}});
    check(genre.value("success").toBool() && notifications == 1
          && service.localTrackIdentityRevision() == renamedRevision,
          "unrelated tag changes avoid invalidating all device rows");
    binding->setProperty("wantedTitle", QStringLiteral("Renamed Song"));
    check(binding->property("present").toBool(), "a device row's changed metadata rechecks membership normally");

    const QString imported = mediaRoot + QStringLiteral("/Added Artist/Added Album/Imported Song.wav");
    if (!writeWave(imported)) return 2;
    QEventLoop scanLoop;
    QTimer timeout; timeout.setSingleShot(true);
    bool finished = false;
    const auto scanConnection = QObject::connect(&service, &LibraryService::scanChanged, &scanLoop, [&] {
        if (!service.scanning()) { finished = true; scanLoop.quit(); }
    });
    QObject::connect(&timeout, &QTimer::timeout, &scanLoop, &QEventLoop::quit);
    timeout.start(10000);
    service.rescan();
    scanLoop.exec();
    QObject::disconnect(scanConnection);
    check(finished && service.hasLocalTrack("Added Artist", "Added Album", "Imported Song")
          && service.localTrackIdentityRevision() > renamedRevision,
          "a real scan publishes newly imported identities through the same reactive index");
    const quint64 scannedRevision = service.localTrackIdentityRevision();
    service.removeWatchFolder(double(folderId));
    check(service.trackCount() == 0 && service.localTrackIdentityRevision() > scannedRevision
          && !service.hasLocalTrack("Added Artist", "Added Album", "Imported Song")
          && !binding->property("present").toBool(),
          "folder deletion removes stale membership and updates the existing device binding");
    printf("Library location integration: %s (%d failures)\n", failed ? "FAILED" : "ALL PASS", failed);
    return failed ? 1 : 0;
}
