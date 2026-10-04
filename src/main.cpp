#include <QDir>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QPalette>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QThreadPool>
#include <QScopeGuard>
#include <cstdio>
#include <cstring>

#include "ZuunedBuildInfo.h"

#include "ApplicationIdentity.h"
#include "GrungeMaskProvider.h"
#include "LibraryService.h"
#include "library/ArtworkHttp.h"
#include "player/MprisService.h"
#include "player/PlayerService.h"
#include "player/VideoPlayerService.h"
#include "DeviceService.h"
#include "DeviceWorker.h"
#include "diagnostics/DiagnosticsService.h"
#include "diagnostics/SessionLog.h"
#include "sync/SyncEngine.h"

int main(int argc, char *argv[]) {
    // Safe release identification: no GUI, scanner, USB or settings startup.
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("Zuuned %s\n", ZuunedBuild::version);
        return 0;
    }
    // Capture native C/Qt startup errors, including failures before a window
    // exists. The guard outlives the engine/application and their workers.
    QCoreApplication::setApplicationName(QStringLiteral("Zuuned"));
    QCoreApplication::setOrganizationName(QStringLiteral("Zuuned"));
    auto &sessionLog = Zuuned::Diagnostics::SessionLog::instance();
    sessionLog.setRedactionContext(DiagnosticsService::redactionContext());
    const QString logRoot = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (!sessionLog.start(logRoot.isEmpty() ? QString() : logRoot + QStringLiteral("/logs")))
        std::fprintf(stderr, "[diagnostics] Local logs unavailable: %s\n", qUtf8Printable(sessionLog.error()));
    struct LogLifetime {
        ~LogLifetime() { Zuuned::Diagnostics::SessionLog::instance().stop(); }
    } logLifetime;
    // File/folder pickers belong to the desktop, including its chosen theme.
    // Native installs may supply their own Qt integration; otherwise prefer
    // the desktop portal. AppRun pins the matching bundled portal plugin so
    // a host Qt theme plugin cannot fail against the AppImage's Qt version.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");

    // MpvVideoItem (QQuickFramebufferObject + mpv_render_context) is
    // OpenGL-only — pin the RHI backend before any window exists.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);
    app.setApplicationName("Zuuned");
    app.setOrganizationName("Zuuned");
    app.setApplicationVersion(QString::fromLatin1(ZuunedBuild::version));
    Zuuned::configureApplicationIdentity(app);
    std::fprintf(stderr, "[zuuned] %s; Qt runtime %s\n", ZuunedBuild::version, qVersion());

    // The application palette mirrors Theme.qml. External desktop dialogs
    // use the desktop's palette and are not restyled by ZUUNED.
    {
        QPalette pal;
        const QColor bg(0x0a, 0x0a, 0x0a), surface(0x14, 0x14, 0x14);
        const QColor text(255, 255, 255, 235), dim(255, 255, 255, 140);
        const QColor pink(0xd4, 0x36, 0x7a);
        pal.setColor(QPalette::Window, bg);
        pal.setColor(QPalette::WindowText, text);
        pal.setColor(QPalette::Base, surface);
        pal.setColor(QPalette::AlternateBase, bg);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::PlaceholderText, dim);
        pal.setColor(QPalette::Button, surface);
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::Highlight, pink);
        pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        pal.setColor(QPalette::ToolTipBase, surface);
        pal.setColor(QPalette::ToolTipText, text);
        app.setPalette(pal);
    }

    // C2: single instance. Two instances share the library DB and fight
    // over the USB claim — a data hazard, not a convenience question.
    const QString runDir =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    // Test harness (scale profiles): ZUUNED_LOCK_SUFFIX isolates the
    // lock so an instrumented instance can run beside the daily one.
    QLockFile instanceLock(
        (runDir.isEmpty() ? QDir::tempPath() : runDir) + "/zuuned"
        + qEnvironmentVariable("ZUUNED_LOCK_SUFFIX") + ".lock");
    if (!instanceLock.tryLock(0)) {
        fprintf(stderr, "[zuuned] another instance is already running — "
                        "this one is bowing out\n");
        return 0;
    }

    QQmlApplicationEngine engine;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] {
        ArtworkHttp::beginShutdown();
    });
    // Drain while both the QML-owned services and QApplication still exist.
    // A watcher disappearing does not cancel its QtConcurrent callable; a
    // provider worker must never construct an event loop after app teardown.
    const auto finishWorkers = qScopeGuard([] {
        ArtworkHttp::beginShutdown();
        QThreadPool::globalInstance()->waitForDone();
    });
    engine.addImageProvider(QStringLiteral("grungemask"), new GrungeMaskProvider);
    // Surface QML load errors on stderr. Without this, a bad import or
    // attached-type reference fails objectCreationFailed silently — the
    // app just exits 1 with no clue which component broke.
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     &app, [](const QList<QQmlError> &ws) {
                         for (const auto &w : ws)
                             fprintf(stderr, "[qml-warn] %s\n",
                                     qUtf8Printable(w.toString()));
                     });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Zuuned", "Main");

    // Wire the sync engine to the device worker (engine orchestrates on
    // the UI thread; sends run on the device thread).
    auto *device = engine.singletonInstance<DeviceService *>("Zuuned", "DeviceService");
    auto *sync = engine.singletonInstance<SyncEngine *>("Zuuned", "SyncEngine");
    if (device && sync) {
        DeviceWorker *worker = device->worker();
        sync->attachDevice(device, worker);
        QObject::connect(sync, &SyncEngine::workerSendTrack,
                         worker, &DeviceWorker::doSendTrack, Qt::QueuedConnection);
        QObject::connect(sync, &SyncEngine::workerForgeAlbums,
                         worker, &DeviceWorker::doForgeAlbums, Qt::QueuedConnection);
        QObject::connect(worker, &DeviceWorker::sendProgress, sync, &SyncEngine::onSendProgress);
        QObject::connect(worker, &DeviceWorker::sendDone, sync, &SyncEngine::onSendDone);
        QObject::connect(worker, &DeviceWorker::forgeProgress, sync, &SyncEngine::onForgeProgress);
        QObject::connect(worker, &DeviceWorker::forgeDone, sync, &SyncEngine::onForgeDone);
        // Phase 6: video sends (after the album forge)
        QObject::connect(sync, &SyncEngine::workerSendVideo,
                         worker, &DeviceWorker::doSendVideo, Qt::QueuedConnection);
        QObject::connect(worker, &DeviceWorker::sendVideoDone,
                         sync, &SyncEngine::onVideoSendDone);
        // Phase 8: photo sends (between forge and video phases)
        QObject::connect(sync, &SyncEngine::workerSendPhoto,
                         worker, &DeviceWorker::doSendPhoto, Qt::QueuedConnection);
        QObject::connect(worker, &DeviceWorker::sendPhotoDone,
                         sync, &SyncEngine::onPhotoSendDone);
        // UX-2: playlist forge phase (after videos, before finalize)
        QObject::connect(sync, &SyncEngine::workerForgePlaylist,
                         worker, &DeviceWorker::doForgePlaylist,
                         Qt::QueuedConnection);
        QObject::connect(worker, &DeviceWorker::forgePlaylistDone,
                         sync, &SyncEngine::onPlaylistForgeDone);
        // 0x922A device-side sync display (queued ahead of each send)
        QObject::connect(sync, &SyncEngine::workerSyncNotify,
                         worker, &DeviceWorker::doSyncNotify, Qt::QueuedConnection);
        // Eject clears the sync queue (mac disconnectDevice parity)
        QObject::connect(device, &DeviceService::ejectStarted,
                         sync, &SyncEngine::clearQueue);
        // Mid-sync unplug aborts cleanly instead of hanging the pipeline
        QObject::connect(device, &DeviceService::stateChanged, sync, [device, sync] {
            if (!device->connected() && sync->isSyncing())
                sync->cancelSync();
        });
    }

    // Wire the player's 5s position-save into the library DB.
    auto *player = engine.singletonInstance<PlayerService *>("Zuuned", "PlayerService");
    auto *library = engine.singletonInstance<LibraryService *>("Zuuned", "LibraryService");
    if (sync) sync->attachLibrary(library);

    // Device music extracts are invisible to the local library until its
    // import root is registered and scanned. Prime portrait matching from the
    // same completed batch so an imported album's artist arrives with it.
    if (device && library)
        QObject::connect(device, &DeviceService::musicImportsReady,
                         library, [library](const QStringList &artists) {
            library->addWatchFolder(DeviceService::musicImportsDir(),
                                    QStringLiteral("music"));
            for (const QString &artist : artists)
                library->requestArtistImage(artist);
        });
    if (device && library)
        QObject::connect(device, &DeviceService::musicImportAlbumArtReady,
                         library, &LibraryService::importDeviceAlbumArt);

    auto *diagnostics = engine.singletonInstance<DiagnosticsService *>("Zuuned", "DiagnosticsService");
    if (diagnostics) {
        diagnostics->setContextProvider([device, library, sync] {
            QJsonObject state;
            if (device) {
                state.insert(QStringLiteral("zune_present"), device->devicePresent());
                state.insert(QStringLiteral("zune_connected"), device->connected());
                state.insert(QStringLiteral("zune_model"), device->modelName());
                state.insert(QStringLiteral("zune_family"), device->deviceFamily());
                state.insert(QStringLiteral("zuuned_rules_installed"), device->udevRuleOk());
            }
            if (library) {
                state.insert(QStringLiteral("library_tracks"), library->trackCount());
                state.insert(QStringLiteral("library_videos"), library->videoCount());
                state.insert(QStringLiteral("library_photos"), library->photoCount());
            }
            if (sync) state.insert(QStringLiteral("transfer_running"), sync->isSyncing());
            return state;
        });
    }

    // Test harness hook (scale passes, CI): kick a full scan at launch.
    if (library && qEnvironmentVariableIsSet("ZUUNED_RESCAN_ON_LAUNCH"))
        library->rescan();

    // C1: MPRIS2 — media keys, sound applets, now-playing widgets.
    if (player && library)
        new MprisService(player,
            [library](const QString &artist, const QString &album) {
                return library->artPaths()
                    .value(LibraryService::artKey(artist, album)).toString();
            }, player);
    if (player && library)
        player->setSavePositionHook([library](qint64 id, int ms) {
            library->savePosition(id, ms);
        });

    // Same for video: position + the 90% watched rule.
    auto *videoPlayer =
        engine.singletonInstance<VideoPlayerService *>("Zuuned", "VideoPlayerService");
    if (videoPlayer && library)
        videoPlayer->setSavePositionHook(
            [library](qint64 id, int posMs, int durMs) {
                library->saveVideoPosition(id, posMs, durMs);
            });

    // Music ducks while video plays (mac auto-pause/resume handoff).
    if (videoPlayer && player)
        QObject::connect(videoPlayer, &VideoPlayerService::stateChanged,
                         player, [videoPlayer, player] {
                             if (videoPlayer->playing() && player->playing())
                                 player->playPause();
                         });

    return app.exec();
}
