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
    const auto bookDir = qEnvironmentVariable("SPOKEN_SYNC_BOOK_DIR");
    const auto chapterFile = qEnvironmentVariable("SPOKEN_SYNC_CHAPTER_FILE");
    if (!root.startsWith("/tmp/zuuned-spoken-sync-backend-") || bookDir.isEmpty()) return 2;
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    auto report = [](const char *message) { printf("[spoken-hardware] %s\n", message); fflush(stdout); };
    DeviceService device;
    SyncEngine sync;
    SpokenMediaService spoken;
    auto *worker = device.worker();
    sync.attachDevice(&device, worker);
    QObject::connect(&sync, &SyncEngine::workerInspectAudiobooks, worker, &DeviceWorker::doInspectAudiobooks, Qt::QueuedConnection);
    QObject::connect(worker, &DeviceWorker::audiobooksInspected, &sync, &SyncEngine::onAudiobooksInspected);
    QObject::connect(&sync, &SyncEngine::workerSendAudiobook, worker, &DeviceWorker::doSendAudiobook, Qt::QueuedConnection);
    QObject::connect(worker, &DeviceWorker::audiobookSendDone, &sync, &SyncEngine::onAudiobookSendDone);
    QObject::connect(worker, &DeviceWorker::sendProgress, &sync, &SyncEngine::onSendProgress);
    QObject::connect(&sync, &SyncEngine::workerForgeAlbums, worker, &DeviceWorker::doForgeAlbums, Qt::QueuedConnection);
    QObject::connect(worker, &DeviceWorker::forgeDone, &sync, &SyncEngine::onForgeDone);
    QObject::connect(&sync, &SyncEngine::workerSyncNotify, worker, &DeviceWorker::doSyncNotify, Qt::QueuedConnection);
    QObject::connect(&device, &DeviceService::stateChanged, &sync, [&] { if (!device.connected() && sync.isSyncing()) sync.cancelSync(); });
    spoken.attachDeviceQueue([&] { return device.connected() && !sync.isSyncing(); });
    QObject::connect(&sync, &SyncEngine::prepareSpokenTransfer, &spoken, &SpokenMediaService::prepareTransfer);
    QObject::connect(&sync, &SyncEngine::cancelSpokenTransfer, &spoken, &SpokenMediaService::cancelTransfer);
    QObject::connect(&spoken, &SpokenMediaService::transferPrepared, &sync, &SyncEngine::onSpokenPrepared);
    QObject::connect(&spoken, &SpokenMediaService::audiobookTransferReady, &sync, &SyncEngine::addAudiobooks);
    int inspections = 0;
    QObject::connect(worker, &DeviceWorker::audiobooksInspected, &app,
        [&](const QString &, const QString &, const QVariantList &, const QString &) { ++inspections; });
    bool sent = false, sendOK = false, reused = false; quint32 sentId = 0;
    QObject::connect(worker, &DeviceWorker::audiobookSendDone, &app,
        [&](const QString &, bool ok, quint32 id, const QString &, bool duplicate) { sent = true; sendOK = ok; sentId = id; reused = duplicate; });
    device.breach();
    if (!waitFor([&] { return device.connected() || !device.busy(); }) || !device.connected()) {
        report("FAIL connection/authentication; no book queued"); return 1;
    }
    report("PASS application DeviceService connected");
    waitFor([&] { return !spoken.busy(); });
    const QStringList paths=chapterFile.isEmpty() ? QStringList{bookDir+"/01.mp3",bookDir+"/02.mp3"} : QStringList{chapterFile};
    QList<QUrl> urls;for(const auto &path:paths)urls.append(QUrl::fromLocalFile(path));
    spoken.previewBook(urls);
    if(!waitFor([&]{return !spoken.busy();}) || spoken.preview().value("kind")!="book") {report("FAIL book preview");return 1;}
    spoken.importBook(chapterFile.isEmpty() ? "ZUUNED GUI Book Test" : "ZUUNED Chapter Book Test","ZUUNED","",paths);
    waitFor([&]{return !spoken.busy();});
    const auto books=spoken.collections("book");
    if(books.size()!=1){report("FAIL book import");return 1;}
    const auto book=books.first().toMap();
    spoken.saveCover(book.value("id").toString(),QUrl::fromLocalFile(bookDir+"/cover.jpg"),false,book.value("coverRevision").toInt());
    waitFor([&]{return !spoken.busy();});
    spoken.queueAudiobooks({book.value("id").toString()});
    const int queued=sync.queue()->count();
    if (queued<1 || sync.queue()->entries().first().type != "audiobook" ||
        !sync.queue()->entries().first().audiobook.value("needsPreparation").toBool() ||
        spoken.busy() || inspections != 0) {
        report("FAIL whole book did not enter pending queue immediately"); return 1;
    }
    report("PASS whole book entered typed pending queue without pre-sync preparation or device inspection");
    sync.startSync();
    if (!waitFor([&] { return !sync.isSyncing(); }, 180000) || !sent || !sendOK || !sync.syncCompleted()) {
        report("FAIL application transfer or completion"); return 1;
    }
    if ((sync.lastSync().value("audiobooks").toInt()+sync.lastSync().value("skipped").toInt()) != queued || sync.lastSync().value("tracks").toInt() != 0 || sync.queue()->count()) {
        report("FAIL queue sweep or completion counts"); return 1;
    }
    printf("[spoken-hardware] PASS app queue transferred or reused audiobook item=%u; music count=0; successful queue swept\n", sentId);
    fflush(stdout);
    if (!waitFor([&] { return !device.connected() && !device.busy(); })) {
        report("FAIL finalize/release timed out"); return 1;
    }
    report("PASS app finalize/release completed; unplug and inspect device category, cover and playback");
    return 0;
}
