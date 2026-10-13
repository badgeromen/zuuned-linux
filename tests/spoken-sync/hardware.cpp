#include "DeviceService.h"
#include "DeviceWorker.h"
#include "sync/SyncEngine.h"
#include "spoken/SpokenMediaService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <functional>

static bool waitFor(const std::function<bool()> &done, int timeout = 120000) {
    QElapsedTimer clock; clock.start();
    while (!done() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(); QThread::msleep(10);
    }
    return done();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("SpokenHardwareGate"); app.setApplicationName("Sync");
    const auto root = qEnvironmentVariable("SPOKEN_SYNC_TEST_ROOT");
    const auto feed = qEnvironmentVariable("SPOKEN_SYNC_FEED");
    const auto identity = qEnvironmentVariable("SPOKEN_SYNC_EPISODE");
    if (!root.startsWith("/tmp/zuuned-spoken-sync-backend-") || feed.isEmpty() || identity.isEmpty()) return 2;
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    auto report = [](const char *message) { printf("[spoken-hardware] %s\n", message); fflush(stdout); };
    DeviceService device;
    SyncEngine sync;
    SpokenMediaService spoken;
    auto *worker = device.worker();
    sync.attachDevice(&device, worker);
    QObject::connect(&sync, &SyncEngine::workerInspectPodcasts, worker, &DeviceWorker::doInspectPodcasts, Qt::QueuedConnection);
    QObject::connect(worker, &DeviceWorker::podcastsInspected, &sync, &SyncEngine::onPodcastsInspected);
    QObject::connect(&sync, &SyncEngine::workerSendPodcast, worker, &DeviceWorker::doSendPodcast, Qt::QueuedConnection);
    QObject::connect(worker, &DeviceWorker::podcastSendDone, &sync, &SyncEngine::onPodcastSendDone);
    QObject::connect(worker, &DeviceWorker::sendProgress, &sync, &SyncEngine::onSendProgress);
    QObject::connect(&sync, &SyncEngine::workerForgeAlbums, worker, &DeviceWorker::doForgeAlbums, Qt::QueuedConnection);
    QObject::connect(worker, &DeviceWorker::forgeDone, &sync, &SyncEngine::onForgeDone);
    QObject::connect(&sync, &SyncEngine::workerSyncNotify, worker, &DeviceWorker::doSyncNotify, Qt::QueuedConnection);
    QObject::connect(&device, &DeviceService::stateChanged, &sync, [&] { if (!device.connected() && sync.isSyncing()) sync.cancelSync(); });
    spoken.attachDeviceQueue([&] { return device.connected() && !sync.isSyncing(); });
    QObject::connect(&sync, &SyncEngine::prepareSpokenTransfer, &spoken, &SpokenMediaService::prepareTransfer);
    QObject::connect(&sync, &SyncEngine::cancelSpokenTransfer, &spoken, &SpokenMediaService::cancelTransfer);
    QObject::connect(&spoken, &SpokenMediaService::transferPrepared, &sync, &SyncEngine::onSpokenPrepared);
    QObject::connect(&spoken, &SpokenMediaService::podcastTransferReady, &sync, &SyncEngine::addPodcasts);
    int inspections = 0;
    QObject::connect(worker, &DeviceWorker::podcastsInspected, &app,
        [&](const QString &, const QString &, const QVariantList &, const QString &) { ++inspections; });
    bool sent = false, sendOK = false, reused = false; quint32 sentId = 0;
    QObject::connect(worker, &DeviceWorker::podcastSendDone, &app,
        [&](const QString &, bool ok, quint32 id, const QString &, bool duplicate) { sent = true; sendOK = ok; sentId = id; reused = duplicate; });
    device.breach();
    if (!waitFor([&] { return device.connected() || !device.busy(); }) || !device.connected()) {
        report("FAIL connection/authentication; no episode queued"); return 1;
    }
    report("PASS application DeviceService connected");
    waitFor([&] { return !spoken.busy(); });
    spoken.previewFeed(QUrl(feed));
    if (!waitFor([&] { return !spoken.busy(); }) || spoken.preview().value("kind") != "show") {
        report("FAIL feed preview"); return 1;
    }
    spoken.subscribePreview(); waitFor([&] { return !spoken.busy(); });
    const auto shows = spoken.collections("show");
    if (shows.size() != 1) { report("FAIL subscription"); return 1; }
    const auto entries = spoken.entries(shows.first().toMap().value("id").toString());
    QString selected;
    for (const auto &v : entries) if (v.toMap().value("identity") == identity) selected = v.toMap().value("id").toString();
    if (selected.isEmpty()) { report("FAIL selected GUID is absent from the feed"); return 1; }
    spoken.queuePodcasts({selected});
    if (sync.queue()->count() != 1 || sync.queue()->entries().first().type != "podcast" ||
        !sync.queue()->entries().first().podcast.value("needsPreparation").toBool() ||
        !spoken.item(selected).value("path").toString().isEmpty() || spoken.busy() || inspections != 0) {
        report("FAIL feed-only episode did not enter pending queue immediately"); return 1;
    }
    report("PASS selected feed-only episode entered pending queue without download or device inspection");
    sync.startSync();
    if (!waitFor([&] { return !sync.isSyncing(); }, 180000) || !sent || !sendOK || !sync.syncCompleted()) {
        report("FAIL application transfer or completion"); return 1;
    }
    if ((sync.lastSync().value("podcasts").toInt()+sync.lastSync().value("skipped").toInt()) != 1 || sync.lastSync().value("tracks").toInt() != 0 || sync.queue()->count()) {
        report("FAIL queue sweep or completion counts"); return 1;
    }
    printf("[spoken-hardware] PASS app queue transferred or reused podcast item=%u; music count=0; successful queue swept\n", sentId);
    fflush(stdout);
    if (!waitFor([&] { return !device.connected() && !device.busy(); })) {
        report("FAIL finalize/release timed out"); return 1;
    }
    report("PASS app finalize/release completed; unplug and inspect device category, cover and playback");
    return 0;
}
