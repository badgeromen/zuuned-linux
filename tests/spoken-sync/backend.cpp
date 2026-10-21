#include "sync/SyncEngine.h"
#include "sync/RecoveryIdentity.h"
#include "DeviceService.h"
#include <QSettings>
#include <memory>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <cstdio>

// Production queue and orchestration with no USB watcher or connected worker.
// The cleanup fixture constructs the facade's explicit no-watcher test seam and
// disconnects all facade-to-worker signals before setting synthetic connection state.
struct DeviceSpokenTestAccess {
    static std::unique_ptr<DeviceService> create() {
        auto d = std::unique_ptr<DeviceService>(new DeviceService(nullptr, false));
        QObject::disconnect(d.get(), nullptr, d->m_worker, nullptr);
        d->m_connected = true; d->m_serial = "cleanup-fixture";
        return d;
    }
    static void invalidate(DeviceService &d) { ++d.m_spokenGeneration; }
    static void reconnect(DeviceService &d) {
        ++d.m_spokenGeneration; d.m_connected = true; d.m_serial = "cleanup-fixture";
        d.m_busy = false; d.m_ejecting = false; d.m_awaitingDisconnect = false;
    }
    static void seedPodcasts(DeviceService &d, const QVariantList &rows) { d.m_podcastEpisodesList = rows; }
    static void libraryUnreadable(DeviceService &d, const QString &why) { d.m_libraryError = why; }
    static void seedPreview(DeviceService &d, const QVariantList &rows) {
        d.m_podcastEpisodesList = rows; d.m_spokenRequested = true;
        d.m_spokenMediaLoading = false; d.m_spokenMediaError.clear();
    }
};
struct SpokenSyncTestAccess {
    static void cleanupDevice(SyncEngine &e, DeviceService *d) {
        e.m_device = d; e.m_worker = d->worker();
    }
    static QString cleanupStage(const SyncEngine &e) { return e.m_cleanupStage; }
    static int removed(const SyncEngine &e) { return e.m_podcastsRemoved; }
    static void save(SyncEngine &e) { e.persistQueue(); }
    static void photoActive(SyncEngine &e, const QString &id) {
        e.m_photoEntries = {QVariantMap{{"entryId", id}, {"title", "holiday.png"},
                                        {"filepath", "/photos/holiday.png"}, {"album", "Trip"}}};
        e.m_photoPos = 0; e.m_isSyncing = true; e.m_inPhotoPhase = true; e.m_cancelled = false;
        e.m_totalCount = 1;
        // What advancePhoto does before every send.
        e.markInflight("photo", QVariantMap{{"entryId", id}, {"title", "holiday.png"},
                                            {"filename", "holiday.png"}});
    }
    static bool cancelled(const SyncEngine &e) { return e.m_cancelled; }
    static void videoActive(SyncEngine &e, const QString &id) {
        e.m_videoEntries = {QVariantMap{{"entryId", id}, {"title", "Pilot"}}};
        e.m_nextSendIdx = 0; e.m_currentEntryId = id; e.m_currentSendPath.clear();
        e.m_isSyncing = true; e.m_inVideoPhase = true; e.m_videoSending = true;
        e.m_cancelled = false; e.m_activePrepares = 0; e.m_totalCount = 1;
        e.markInflightVideo("Show - S01E01 - Pilot.mp4", "Pilot");   // as beginVideoSend does
    }
    static void active(SyncEngine &e, const QString &id, QVariantMap row) {
        if (!row.contains("type")) row["type"] = "podcast"; row["entryId"] = id;
        e.m_entries = {row}; e.m_entryPos = 0; e.m_currentEntryId = id;
        e.m_isSyncing = true; e.m_totalCount = 1;
        if (e.m_device) {
            e.m_sendDeviceGeneration = e.m_device->spokenGeneration();
            e.m_sendDeviceSerial = e.m_device->deviceSerial();
        }
        e.m_activeRecoveryRecord = {{"type", row.value("type")}, {"version", 1}, {"token", "fixture"},
            {"serial", "fixture-device"}, {"identity", row}, {"itemId", 0}};
    }
    static bool persistActive(SyncEngine &e) {
        QFile marker(e.inflightPath());
        const auto bytes = QJsonDocument::fromVariant(e.m_activeRecoveryRecord).toJson();
        return marker.open(QIODevice::WriteOnly) && marker.write(bytes) == bytes.size();
    }
    static void prepare(SyncEngine &e) { e.prepareEntry(0); }
    static void ownPrepared(SyncEngine &e, const QString &path) { e.m_currentTempPath=path; }
    static bool cleanMusic(const SyncEngine &e) {
        return e.m_buckets.isEmpty() && e.m_sentTrackIds.isEmpty() && e.m_sentTrackIdentities.isEmpty();
    }
    static QVariantMap recovery(const SyncEngine &e) { return e.m_recoveryRecord; }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("SpokenSyncFixture"); app.setApplicationName("Sync");
    const auto root = qEnvironmentVariable("SPOKEN_SYNC_TEST_ROOT");
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith("/tmp/zuuned-spoken-sync-backend-") || !data.startsWith(root + '/')) return 2;
    QDir().mkpath(data);
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char *label) { ++checks; failed += !ok;
        printf("%s %s\n", ok ? "PASS" : "FAIL", label); };
    const auto path = root + "/episode.audio";
    QFile file(path); file.open(QIODevice::WriteOnly); file.write("fixture"); file.close();
    QVariantMap row{{"type", "podcast"}, {"filepath", path}, {"title", "Same title"},
        {"artist", "Author"}, {"album", "Show"}, {"feedUrl", "https://example.test/feed"},
        {"identity", "guid:opaque-one"}, {"sourceUrl", "https://example.test/audio-one"},
        {"durationMs", 120000}, {"posterPath", root + "/cover.jpg"}, {"codec", "mp3"}};
    check(SyncEngine::estimateTrackBytes(120000,2000000000)==SyncEngine::estimateTrackBytes(120000,4000000000LL) &&
          SyncEngine::estimateTrackBytes(120000,2000000000)<SyncEngine::estimateTrackBytes(240000,2000000000),
          "chapter capacity estimate uses segment duration rather than the entire source file");
    QString entry;
    {
        SyncEngine e;
        int notices = 0;
        QObject::connect(&e, &SyncEngine::podcastQueueNotice, &app, [&](const QString &) { ++notices; });
        e.addPodcasts({row});
        check(e.queue()->count() == 0 && notices == 1, "offline engine boundary refuses queue mutation");
        entry = e.queue()->addPodcast(row, 40000);
        check(!entry.isEmpty() && e.queue()->count() == 1, "typed podcast entry added");
        auto changed = row; changed["sourceUrl"] = "https://example.test/new-download";
        check(e.queue()->addPodcast(changed, 40000).isEmpty(), "same feed and GUID cannot duplicate after a URL change");
        changed = row; changed["identity"] = "guid:two";
        check(e.queue()->addPodcast(changed, 40000).isEmpty(), "reused source URL cannot duplicate within a show");
        changed["sourceUrl"] = "https://example.test/audio-two";
        check(!e.queue()->addPodcast(changed, 40000).isEmpty(), "different episodes with the same title remain distinct");
        changed = row; changed["feedUrl"] = "https://example.test/other-feed";
        check(!e.queue()->addPodcast(changed, 40000).isEmpty(), "GUID identity is scoped to the feed");
        const auto groups = e.queue()->groupSummary();
        check(groups.size() == 2 && groups.first().toMap().value("kind") == "podcasts" &&
              groups.first().toMap().value("count").toInt() == 2, "show grouping uses feed identity and podcast labels");
        check(e.queue()->pendingEstimatedBytes() == 120000, "podcasts participate in capacity accounting");
        SpokenSyncTestAccess::save(e);
    }
    {
        SyncEngine e;
        const auto saved = e.queue()->entries();
        check(saved.size() == 3 && saved.first().entryId == entry && saved.first().podcast == row,
              "restart preserves complete podcast identity and original queue IDs");
        SpokenSyncTestAccess::active(e, entry, row);
        e.onPodcastSendDone("stale", true, 123, {}, false);
        check(e.queue()->entries().first().status == "pending", "stale completion cannot settle a current episode");
        e.onPodcastSendDone(entry, true, 123, {}, false);
        check(e.queue()->entries().first().status == "verified", "correlated podcast completion verifies its row");
        check(SpokenSyncTestAccess::cleanMusic(e), "podcasts cannot forge music albums or add music identities");
        check(QFile::exists(path), "successful transfer never removes original downloaded audio");
    }
    {
        SyncEngine e; e.clearQueue();
        entry = e.queue()->addPodcast(row, 40000);
        SpokenSyncTestAccess::active(e, entry, row);
        e.onPodcastSendDone(entry, true, 123, {}, true);
        check(e.queue()->entries().first().status == "skipped", "live duplicate at send time is skipped, never failed");
        check(SpokenSyncTestAccess::cleanMusic(e), "duplicate completion does not update music collections");
    }
    {
        SyncEngine e; e.clearQueue();
        entry = e.queue()->addPodcast(row, 40000);
        SpokenSyncTestAccess::active(e, entry, row);
        check(SpokenSyncTestAccess::persistActive(e), "fixture persists the in-flight marker before a failed send");
        e.onPodcastSendDone("stale", false, 0, "Removed", false, false);
        check(e.isSyncing() && QFile::exists(data + "/inflight-send"),
              "stale cleanup completion cannot erase the active transfer marker");
        e.onPodcastSendDone(entry, false, 0, "Metadata verification failed; new item removed", false, false);
        check(!e.isSyncing() && e.interruptedSend().isEmpty() && !QFile::exists(data + "/inflight-send"),
              "confirmed cleanup clears durable recovery without an interruption banner");
        check(e.queue()->entries().first().status == "failed" &&
              e.queue()->entries().first().statusNote.contains("Metadata verification failed"),
              "confirmed cleanup still reports the actual failed transfer on its queue row");
    }
    {
        SyncEngine e;
        check(e.interruptedSend().isEmpty(), "confirmed cleanup cannot resurrect a recovery banner after restart");
        e.clearQueue(); entry = e.queue()->addPodcast(row, 40000);
        SpokenSyncTestAccess::active(e, entry, row);
        e.onPodcastSendDone(entry, false, 0, "No response from device", false);
        check(!e.isSyncing() && !e.interruptedSend().isEmpty() && QFile::exists(data + "/inflight-send"),
              "unknown outcome with zero object ID still retains recovery");
        e.dismissInterruptedSend();
    }
    {
        SyncEngine e; e.clearQueue();
        entry = e.queue()->addPodcast(row, 40000);
        SpokenSyncTestAccess::active(e, entry, row);
        e.onPodcastSendDone(entry, false, 123, "Upload interrupted", false);
        check(!e.isSyncing() && !e.interruptedSend().isEmpty() && e.queue()->entries().first().status == "failed",
              "interrupted upload halts sync and retains its recovery record");
        check(!RecoveryIdentity::refusal(SpokenSyncTestAccess::recovery(e), "fixture-device").isEmpty(),
              "unproven podcast recovery cannot automatically purge device objects");
        SpokenSyncTestAccess::save(e);
    }
    {
        SyncEngine e;
        check(e.interruptedSend() == "Same title", "restart identifies interrupted podcast without showing raw JSON");
    }
    QVariantMap book=row;
    book["type"]="audiobook";book["bookKey"]=QString(64,'a');book["partNumber"]=1;book["sourceHash"]=QString(64,'b');
    book["rangeStartMs"]=0;book["rangeEndMs"]=120000;book["sourceDurationMs"]=240000;
    book["identity"]="chapter-one";
    {
        SyncEngine e;e.clearQueue();
        e.addAudiobooks({book});check(e.queue()->count()==0,"offline audiobook admission refused");
        entry=e.queue()->addAudiobook(book,40000);
        check(!entry.isEmpty() && e.queue()->addAudiobook(book,40000).isEmpty(),"book part identity excludes duplicates");
        auto second=book;second["partNumber"]=2;second["identity"]="chapter-two";second["rangeStartMs"]=120000;second["rangeEndMs"]=240000;
        check(!e.queue()->addAudiobook(second,40000).isEmpty(),"same-title parts with different order remain distinct");
        check(e.queue()->groupSummary().first().toMap().value("kind")=="audiobooks","book queue has dedicated grouping");
        check(e.queue()->pendingEstimatedBytes()==80000,"chapter estimates count each prepared segment once");
        SpokenSyncTestAccess::save(e);
    }
    {
        SyncEngine e;
        const auto restored=e.queue()->entries();
        check(restored.size()==2 && restored.first().audiobook==book,"complete chapter identity and source hash persist across restart");
        check(restored.size()==2 && restored[0].audiobook.value("rangeEndMs")==restored[1].audiobook.value("rangeStartMs") &&
              restored[1].audiobook.value("rangeEndMs")==book.value("sourceDurationMs"),
              "restart preserves contiguous chapter ranges and source duration");
        SpokenSyncTestAccess::active(e,entry,book);
        e.onPodcastSendDone(entry,true,123,{},false);
        check(e.queue()->entries().first().status=="pending","podcast completion cannot settle a book");
        e.onAudiobookSendDone(entry,true,123,{},false);
        check(e.queue()->entries().first().status=="verified" && SpokenSyncTestAccess::cleanMusic(e),"book completion isolated from music");
    }
    {
        SyncEngine e;e.clearQueue();entry=e.queue()->addAudiobook(book,40000);
        SpokenSyncTestAccess::active(e,entry,book);
        e.onAudiobookSendDone(entry,false,123,"Interrupted",false);
        const auto recovery=SpokenSyncTestAccess::recovery(e);
        check(!e.interruptedSend().isEmpty() && !RecoveryIdentity::refusal(recovery,"fixture-device").isEmpty(),"book recovery retains exact ID and refuses automatic purge");
        check(recovery.value("identity").toMap().value("rangeEndMs")==book.value("rangeEndMs") &&
              recovery.value("identity").toMap().value("sourceHash")==book.value("sourceHash"),
              "interrupted chapter recovery retains exact source and range identity");
    }
    {
        SyncEngine e;e.clearQueue();
        auto invalid=book;invalid["rangeEndMs"]=300000;
        entry=e.queue()->addAudiobook(invalid,40000);
        SpokenSyncTestAccess::active(e,entry,invalid);
        int sends=0;
        QObject::connect(&e,&SyncEngine::workerSendAudiobook,&app,[&]{++sends;});
        SpokenSyncTestAccess::prepare(e);
        check(e.queue()->entries().first().status=="failed" && sends==0,
              "invalid saved chapter range is rejected before any worker send");
    }
    {
        SyncEngine e;e.clearQueue();entry=e.queue()->addAudiobook(book,40000);
        SpokenSyncTestAccess::active(e,entry,book);
        int sends=0;
        QObject::connect(&e,&SyncEngine::workerSendAudiobook,&app,[&]{++sends;});
        SpokenSyncTestAccess::prepare(e);
        QElapsedTimer wait;wait.start();
        while(e.queue()->entries().first().status!="failed" && wait.elapsed()<10000) {
            QCoreApplication::processEvents();QThread::msleep(5);
        }
        check(e.queue()->entries().first().status=="failed" && sends==0 && QFile::exists(path),
              "source hash mismatch prevents transcoding or USB send and preserves the original");
    }
    {
        const auto snapshots=[] {
            const auto names=QDir(QDir::tempPath()).entryList({"zuuned-book-*.audio"},QDir::Files);
            return QSet<QString>(names.cbegin(),names.cend());
        };
        auto plain=book;
        plain.remove("rangeStartMs");plain.remove("rangeEndMs");plain.remove("sourceDurationMs");
        plain["sourceHash"]=QString::fromLatin1(QCryptographicHash::hash(QByteArray("fixture"),QCryptographicHash::Sha256).toHex());
        const auto before=snapshots();
        auto *e=new SyncEngine;e->clearQueue();entry=e->queue()->addAudiobook(plain,40000);
        SpokenSyncTestAccess::active(*e,entry,plain);
        SpokenSyncTestAccess::prepare(*e);
        // Let the worker finish, deliberately leave its delivery queued, and
        // destroy the receiver exactly as shutdown can do after draining workers.
        const bool drained=QThreadPool::globalInstance()->waitForDone(10000);
        const auto abandoned=snapshots()-before;
        check(drained && abandoned.size()==1,"prepared audiobook snapshot exists before queued completion delivery");
        delete e;
        QCoreApplication::processEvents();
        bool removed=true;
        for(const auto &name:abandoned)removed=removed && !QFile::exists(QDir::tempPath()+'/'+name);
        QFile original(path);const bool preserved=original.open(QIODevice::ReadOnly) && original.readAll()=="fixture";
        check(removed && preserved,"destroying a preparation receiver removes its abandoned snapshot and preserves source audio");
    }
    {
        const auto prepared=root+"/accepted-prepared.mp3";
        QFile owned(prepared);const bool created=owned.open(QIODevice::WriteOnly) && owned.write("prepared")==8;owned.close();
        auto *e=new SyncEngine;
        SpokenSyncTestAccess::ownPrepared(*e,prepared);
        delete e;
        QFile original(path);const bool preserved=original.open(QIODevice::ReadOnly) && original.readAll()=="fixture";
        check(created && !QFile::exists(prepared) && preserved,
              "engine destruction cleans an accepted owned transfer temp without deleting its original");
    }
    QVariantMap podcastDraft=row;
    podcastDraft["needsPreparation"]=true;podcastDraft["spokenId"]="feed-only-episode";podcastDraft["filepath"]=QString();
    QVariantMap bookDraft=book;
    bookDraft["needsPreparation"]=true;bookDraft["bookId"]="imported-book";bookDraft.remove("sourceHash");
    QString feedDraftId,bookDraftId;
    {
        SyncEngine e;e.clearQueue();
        feedDraftId=e.queue()->addPodcast(podcastDraft,40000);
        bookDraftId=e.queue()->addAudiobook(bookDraft,40000);
        check(!feedDraftId.isEmpty() && !bookDraftId.isEmpty() && e.queue()->count()==2,
              "normal queue accepts feed-only episodes and unhashed book drafts immediately");
        check(e.queue()->entries().first().status=="pending" && e.queue()->entries().last().status=="pending",
              "unprepared spoken entries retain the regular pending queue presentation");
        check(e.queue()->addPodcast(podcastDraft,40000).isEmpty() && e.queue()->addAudiobook(bookDraft,40000).isEmpty(),
              "repeated immediate selections do not duplicate draft entries");
        SpokenSyncTestAccess::save(e);
    }
    {
        SyncEngine e;
        const auto entries=e.queue()->entries();
        bool podcastSaved=false,bookSaved=false;
        for(const auto &queued:entries) {
            if(queued.entryId==feedDraftId)podcastSaved=queued.podcast==podcastDraft && queued.filepath.isEmpty();
            if(queued.entryId==bookDraftId)bookSaved=queued.audiobook==bookDraft && queued.audiobook.value("sourceHash").toString().isEmpty();
        }
        check(podcastSaved && bookSaved,"restart preserves deferred source URL, cached chapter ranges and original queue IDs");
        auto resolved=bookDraft;resolved["bookKey"]=QString(64,'f');resolved["sourceHash"]=QString(64,'b');resolved["needsPreparation"]=false;
        const auto groupsBefore=e.queue()->groupSummary();
        e.queue()->updateSpokenPayload(bookDraftId,resolved,40000);
        check(e.queue()->groupSummary()==groupsBefore,
              "resolving a book content key preserves its group identity, expansion key and display");
        check(e.queue()->addAudiobook(bookDraft,40000).isEmpty(),
              "book draft repeated after content resolution stays deduplicated by imported book and part");
    }
    {
        SyncEngine e;e.clearQueue();
        entry=e.queue()->addPodcast(podcastDraft,40000);
        SpokenSyncTestAccess::active(e,entry,podcastDraft);
        QString preparationToken,cancellationToken;QVariantMap preparationDraft;int prepares=0,sends=0;
        QObject::connect(&e,&SyncEngine::prepareSpokenTransfer,&app,[&](const QString &token,const QVariantMap &draft){
            preparationToken=token;preparationDraft=draft;++prepares;
        });
        QObject::connect(&e,&SyncEngine::cancelSpokenTransfer,&app,[&](const QString &token){cancellationToken=token;});
        QObject::connect(&e,&SyncEngine::workerSendPodcast,&app,[&]{++sends;});
        check(prepares==0 && sends==0,"pending queue draft starts neither preparation nor transfer");
        SpokenSyncTestAccess::prepare(e);
        check(prepares==1 && !preparationToken.isEmpty() && preparationDraft.value("identity")==podcastDraft.value("identity") &&
              e.queue()->entries().first().status=="preparing" && sends==0,
              "active sync requests correlated preparation through the existing queue status");
        e.onSpokenPrepared("stale-preparation",{},"stale failure");
        check(e.isSyncing() && e.queue()->entries().first().status=="preparing" && sends==0,
              "stale preparation result cannot fail or send a current queue entry");
        e.cancelSync();
        check(cancellationToken==preparationToken,"sync cancellation propagates the exact active preparation token");
        e.onSpokenPrepared(preparationToken,{},"cancelled");
        check(!e.isSyncing() && sends==0 && QFile::exists(path),
              "cancelled preparation cannot send audio or remove the original source");
    }
    {
        SyncEngine e;e.clearQueue();
        auto legacy=book;legacy.remove("bookId");legacy.remove("needsPreparation");
        entry=e.queue()->addAudiobook(legacy,40000);
        SpokenSyncTestAccess::save(e);
        check(!entry.isEmpty(),"legacy prepared audiobook without imported book ID remains a valid queue row");
    }
    {
        SyncEngine e;
        auto current=bookDraft;current["bookKey"]=QString(64,'c');
        check(e.queue()->entries().size()==1 && e.queue()->containsAudiobook(current) &&
              e.queue()->addAudiobook(current,40000).isEmpty(),
              "new draft cannot duplicate a restored legacy prepared source-range identity");
        auto next=current;next["identity"]="chapter-two";next["partNumber"]=2;
        next["rangeStartMs"]=120000;next["rangeEndMs"]=240000;
        check(!e.queue()->containsAudiobook(next) && !e.queue()->addAudiobook(next,40000).isEmpty(),
              "legacy queue reuse still admits a different chapter range and part");
        auto other=current;other["bookId"]="different-import";other["album"]="Different edition";
        other["bookKey"]=QString(64,'d');
        check(!e.queue()->containsAudiobook(other) && !e.queue()->addAudiobook(other,40000).isEmpty(),
              "legacy identity reuse does not merge books with different album metadata");
    }
    // Exercise the actual startSync -> inspect -> durable history -> purge path.
    // No signal below is connected to a device worker. Completion is injected
    // only through the production engine's correlation-checked slots.
    QVariantMap cleanupRow{{"spokenId", "episode-1"}, {"identity", "guid:opaque-one"},
        {"feedUrl", row.value("feedUrl")}, {"sourceUrl", row.value("sourceUrl")},
        {"keepOnDevice", false}, {"itemId", 101u}, {"seriesId", 50u},
        {"durationMs", 120000u}, {"bookmarkMs", 40600u}, {"rawFlags", 0x200u},
        {"parentId", 40u}, {"sizeBytes", 2000000ULL}, {"deviceTitle", "Same title"}};
    cleanupRow["album"] = "Fixture podcast";
    auto cleanupTwo = cleanupRow; cleanupTwo["spokenId"] = "episode-2";
    cleanupTwo["identity"] = "guid:two"; cleanupTwo["sourceUrl"] = "https://example.test/audio-two";
    cleanupTwo["itemId"] = 102u; cleanupTwo["deviceTitle"] = "Second played episode";
    const QVariantList cleanupRows{cleanupRow, cleanupTwo};
    auto cleanupCase = [&](const QString &mode) {
        auto d = DeviceSpokenTestAccess::create();
        DeviceSpokenTestAccess::seedPodcasts(*d, cleanupRows);
        SyncEngine e; e.dismissInterruptedSend(); e.clearQueue();
        SpokenSyncTestAccess::cleanupDevice(e, d.get());
        const auto removalStatus = [&](quint32 id) {
            for (const auto &entry : e.queue()->entries())
                if (entry.type == "podcast-removal" && entry.podcast.value("itemId").toUInt() == id)
                    return entry.status;
            return QString();
        };
        const auto removalCount = [&] {
            int count = 0;
            for (const auto &entry : e.queue()->entries()) if (entry.type == "podcast-removal") ++count;
            return count;
        };
        auto candidates = cleanupRows;
        if (mode == "prior-played" || mode == "other-device-history") {
            auto remembered = cleanupRow;
            remembered["devicePlayedSerials"] = QVariantMap{{mode == "prior-played" ? "cleanup-fixture" : "other-device", true}};
            candidates[0] = remembered;
        }
        e.attachPodcastCleanup([&](const QStringList &, bool) { return candidates; });
        QSettings().setValue("podcastAutoCleanup", mode != "disabled");
        int inspected = 0, persisted = 0, purges = 0, transfers = 0;
        int summaryRemoved = -1;
        QObject::connect(&e, &SyncEngine::syncSummary, &app, [&] {
            summaryRemoved = e.lastSync().value("podcastsRemoved").toInt();
        });
        quint64 generation = 0; QString token, serial; QVariantList persistRows, purgeRows;
        QStringList notices;
        QObject::connect(&e, &SyncEngine::podcastQueueNotice, &app, [&](const QString &message) { notices.append(message); });
        QObject::connect(&e, &SyncEngine::inspectPodcastCleanup, &app,
            [&](quint64 g, const QString &t, const QString &s, const QVariantList &) {
                ++inspected; generation = g; token = t; serial = s;
            });
        QObject::connect(&e, &SyncEngine::recordDevicePlayed, &app,
            [&](const QString &, const QString &, const QVariantList &rows) { ++persisted; persistRows = rows; });
        QObject::connect(&e, &SyncEngine::purgePlayedPodcasts, &app,
            [&](quint64, const QString &, const QString &, const QVariantList &rows) { ++purges; purgeRows = rows; });
        QObject::connect(&e, &SyncEngine::workerSendPodcast, &app, [&] { ++transfers; });
        QObject::connect(&e, &SyncEngine::prepareSpokenTransfer, &app, [&] { ++transfers; });
        QString queued;
        if (mode == "queued" || mode == "prior-played" || mode == "other-device-history")
            queued = e.queue()->addPodcast(row, 40000);
        e.startSync();
        if (mode == "prior-played") {
            check(inspected == 1 && e.queue()->entries().first().status == "skipped" && transfers == 0,
                  "fresh current-device played history suppresses restored queue before inspection");
            e.onPodcastCleanupInspected(generation, token, serial, {}, {});
            check(!e.isSyncing() && transfers == 0 && e.queue()->count() == 0,
                  "previously removed played copy cannot return when fresh device inventory is empty");
            return;
        }
        if (mode == "other-device-history") {
            check(inspected == 1 && e.queue()->entries().first().status == "pending",
                  "played history for a different device cannot suppress this device queue");
            e.cancelSync(); e.onPodcastCleanupInspected(generation, token, serial, {}, {});
            return;
        }
        if (mode == "disabled") {
            check(!e.isSyncing() && inspected == 0 && purges == 0, "empty-queue cleanup requires explicit opt-in");
            return;
        }
        if (mode == "success") {
            check(e.isSyncing() && inspected == 1 && persisted == 0 && purges == 0,
                  "opted-in empty queue starts read-only cleanup inspection");
            e.onPodcastCleanupInspected(generation, "stale-token", serial, cleanupRows, {});
            e.onPodcastCleanupInspected(generation + 1, token, serial, cleanupRows, {});
            e.onPodcastCleanupInspected(generation, token, "other-device", cleanupRows, {});
            check(persisted == 0 && purges == 0 && e.isSyncing(), "stale cleanup token, generation and serial cannot advance");
        }
        if (mode == "inspect-error") {
            e.onPodcastCleanupInspected(generation, token, serial, {}, "unavailable state");
            check(persisted == 0 && purges == 0 && !e.isSyncing() && e.lastError().contains("unavailable state"),
                  "inspection error never writes history or deletes and remains visible"); return;
        }
        if (mode == "empty-result") {
            e.onPodcastCleanupInspected(generation, token, serial, {}, {});
            check(persisted == 0 && purges == 0 && !e.isSyncing(), "no played candidates completes without mutation"); return;
        }
        if (mode == "cancel-inspect") {
            e.cancelSync(); e.onPodcastCleanupInspected(generation, token, serial, cleanupRows, {});
            check(persisted == 0 && purges == 0 && !e.isSyncing(), "cancelled inspection cannot write or delete"); return;
        }
        e.onPodcastCleanupInspected(generation, token, serial, cleanupRows, {});
        if (mode == "success") {
            check(persisted == 1 && persistRows == cleanupRows && purges == 0,
                  "device completion is persisted with exact rows before any purge request");
            const auto shown = e.queue()->entries();
            check(shown.size() == 2 && shown[0].title == "Same title" && shown[1].title == "Second played episode"
                  && shown[0].album == "Fixture podcast" && removalStatus(101) == "pending-removal"
                  && removalStatus(102) == "pending-removal" && e.queue()->pendingEstimatedBytes() == 0,
                  "validated inspection exposes named pending removal rows with no upload capacity cost");
            check(e.queue()->groupSummary().first().toMap().value("kind") == "podcast-removals",
                  "removal group is distinct from episode upload grouping");
            SpokenSyncTestAccess::save(e);
            SyncEngine restored;
            check(restored.queue()->count() == 0, "pending removal plan never survives queue persistence or restart");
            auto forbidden = shown.first(); forbidden.status = "failed";
            restored.queue()->restoreEntry(forbidden);
            check(restored.queue()->count() == 0, "restore boundary rejects removal even with a retryable status");
            e.onDevicePlayedRecorded("stale", serial, cleanupRows, {});
            e.onDevicePlayedRecorded(token, "other-device", cleanupRows, {});
            check(purges == 0 && SpokenSyncTestAccess::cleanupStage(e) == "persist",
                  "unrelated persistence acknowledgement cannot authorize deletion");
        }
        if (mode == "persist-error") {
            e.onDevicePlayedRecorded(token, serial, {}, "database locked");
            check(purges == 0 && !e.isSyncing() && e.lastError().contains("database locked"),
                  "history persistence failure preserves device copies");
            check(removalStatus(101) == "not-removed" && removalStatus(102) == "not-removed",
                  "history failure visibly retains every unstarted removal"); return;
        }
        if (mode == "changed-ack") {
            e.onDevicePlayedRecorded(token, serial, {cleanupRow}, {});
            check(purges == 0 && !e.isSyncing(), "partial acknowledgement cannot authorize deletion"); return;
        }
        if (mode == "cancel-persist") {
            e.cancelSync(); e.onDevicePlayedRecorded(token, serial, cleanupRows, {});
            check(purges == 0 && !e.isSyncing(), "cancellation while saving history prevents purge");
            check(removalStatus(101) == "not-removed" && removalStatus(102) == "not-removed",
                  "cancel during history save retires pending display actions without removal claims"); return;
        }
        if (mode == "reconnect") {
            DeviceSpokenTestAccess::invalidate(*d); e.onDevicePlayedRecorded(token, serial, cleanupRows, {});
            check(purges == 0 && !e.isSyncing(), "new device generation after history save prevents purge"); return;
        }
        e.onDevicePlayedRecorded(token, serial, cleanupRows, {});
        if (mode == "success") {
            check(purges == 1 && purgeRows == cleanupRows && e.isSyncing(),
                  "exact durable acknowledgement authorizes unchanged cleanup rows once");
            e.onDevicePlayedRecorded(token, serial, cleanupRows, {});
            check(purges == 1, "repeated acknowledgement cannot request duplicate deletion");
            e.onPlayedPodcastsPurged(generation + 1, token, serial, cleanupRows, {});
            e.onPlayedPodcastsPurged(generation, "stale", serial, cleanupRows, {});
            e.onPlayedPodcastsPurged(generation, token, "wrong-session", cleanupRows, {});
            check(e.isSyncing() && SpokenSyncTestAccess::removed(e) == 0 && d->podcastEpisodesList() == cleanupRows,
                  "stale deletion callbacks cannot change removal counts or prune device rows");
            e.onPodcastCleanupProgress(generation + 1, token, serial, 101, true);
            e.onPodcastCleanupProgress(generation, "stale", serial, 101, true);
            e.onPodcastCleanupProgress(generation, token, "wrong-device", 101, true);
            e.onPodcastCleanupProgress(generation, token, serial, 999, true);
            check(removalStatus(101) == "pending-removal", "stale or foreign progress cannot mark a removal confirmed");
            e.onPodcastCleanupProgress(generation, token, serial, 101, false);
            check(removalStatus(101) == "removing" && removalStatus(102) == "pending-removal",
                  "only the currently deleting episode shows removing");
            e.onPodcastCleanupProgress(generation, token, serial, 101, true);
            e.onPodcastCleanupProgress(generation, token, serial, 101, false);
            check(removalStatus(101) == "removed" && SpokenSyncTestAccess::removed(e) == 0,
                  "confirmed progress marks removed once without regressing or double-counting summary");
        }
        if (mode == "queued") {
            check(e.queue()->entries().size() == 3 && e.queue()->entries().first().entryId == queued
                && e.queue()->entries().first().status == "skipped" && transfers == 0,
                "played queued episode is suppressed before cleanup can recreate it");
        }
        if (mode == "cancel-purge") {
            e.onPodcastCleanupProgress(generation, token, serial, 101, false);
            e.onPodcastCleanupProgress(generation, token, serial, 101, true);
            e.cancelSync();
            check(e.isSyncing() && d->podcastEpisodesList() == cleanupRows,
                  "cancelling active purge waits for confirmed worker results");
            e.onPlayedPodcastsPurged(generation, token, serial, {cleanupRow}, "cancelled before next episode");
            check(!e.isSyncing() && d->podcastEpisodesList() == QVariantList{cleanupTwo}
                  && notices.contains("Removed 1 played podcasts from the Zune."),
                  "cancelled partial cleanup prunes only confirmed episode and announces removal count");
            check(removalStatus(101) == "removed" && removalStatus(102) == "not-removed",
                  "partial cancellation keeps confirmed removed and unstarted retained rows distinct");
            return;
        }
        if (mode == "reconnect-purge") {
            DeviceSpokenTestAccess::invalidate(*d);
            e.onPlayedPodcastsPurged(generation, token, serial, {cleanupRow}, {});
            check(!e.isSyncing() && d->podcastEpisodesList() == cleanupRows,
                  "prior-session delete acknowledgement cannot prune new-session browser rows");
            return;
        }
        if (mode == "unknown-result") {
            auto unknown = cleanupRow; unknown["itemId"] = 999u;
            e.onPlayedPodcastsPurged(generation, token, serial, {unknown}, {});
            check(SpokenSyncTestAccess::removed(e) == 0 && e.lastError().contains("Unexpected"),
                  "unknown deletion result cannot be counted as confirmed"); return;
        }
        if (mode == "partial") {
            e.onPodcastCleanupProgress(generation, token, serial, 101, false);
            e.onPodcastCleanupProgress(generation, token, serial, 101, true);
            e.onPodcastCleanupProgress(generation, token, serial, 102, false);
            e.onPlayedPodcastsPurged(generation, token, serial, {cleanupRow}, "remaining item refused");
            check(!e.isSyncing() && e.lastSync().value("podcastsRemoved").toInt() == 1
                && e.lastSync().value("podcastCleanupWarning").toString().contains("remaining item refused"),
                "partial deletion counts confirmed rows and preserves warning in last sync");
            check(removalStatus(101) == "removed" && removalStatus(102) == "removal-unconfirmed",
                  "failed deletion never falsely claims the attempted episode was removed or retained"); return;
        }
        e.onPlayedPodcastsPurged(generation, token, serial, cleanupRows, {});
        if (mode == "success") {
            check(!e.isSyncing() && e.lastSync().value("podcastsRemoved").toInt() == 2 && summaryRemoved == 2 && d->podcastEpisodesList().isEmpty(),
                  "confirmed cleanup completion records removed count and prunes device rows");
            check(removalCount() == 2 && removalStatus(101) == "removed" && removalStatus(102) == "removed"
                  && e.lastSync().value("podcasts").toInt() == 0 && e.lastSync().value("tracks").toInt() == 0,
                  "completed action rows remain reviewable without increasing upload summary counts");
            e.onPodcastCleanupProgress(generation, token, serial, 102, false);
            check(removalStatus(102) == "removed", "finished sync ignores late progress callbacks");
            QSettings().setValue("podcastAutoCleanup", false);
            DeviceSpokenTestAccess::reconnect(*d);
            e.startSync();
            check(e.queue()->count() == 0 && transfers == 0 && !e.isSyncing(),
                  "next sync clears transient history and never re-arms deletion as upload");
        }
        if (mode == "queued")
            check(!e.isSyncing() && transfers == 0 && e.queue()->count() == 2 && removalCount() == 2,
                  "completed cleanup never downloads or retransfers suppressed played episode");
    };
    for (const auto &mode : {"disabled", "success", "inspect-error", "empty-result", "cancel-inspect",
                            "persist-error", "changed-ack", "cancel-persist", "reconnect", "queued",
                            "unknown-result", "partial", "prior-played", "other-device-history", "cancel-purge", "reconnect-purge"}) cleanupCase(QString::fromLatin1(mode));
    QSettings().remove("podcastAutoCleanup");
    {
        // A photo libzune refused before any device write leaves nothing to
        // recover: the entry fails with the real reason and the sync goes on.
        auto d = DeviceSpokenTestAccess::create();
        SyncEngine e; e.clearQueue(); e.dismissInterruptedSend();
        SpokenSyncTestAccess::cleanupDevice(e, d.get());
        const QString why = "Photo is not a JPEG and could not be converted (ffmpeg not found): /photos/holiday.png";
        check(DeviceWorker::photoRefusedBeforeWrite(why)
              && DeviceWorker::photoRefusedBeforeWrite("Could not read the device's photo albums to find 'Trip'")
              && !DeviceWorker::photoRefusedBeforeWrite("Failed to send photo (StoreFull 0x200C): holiday.jpg"),
              "photo refusals before any device write are told apart from failed sends");
        SpokenSyncTestAccess::photoActive(e, "photo-1");
        const bool marked = QFile::exists(data + "/inflight-send");
        e.onPhotoSendDone("photo-1", false, 0, "OK", why);
        check(marked && !QFile::exists(data + "/inflight-send") && e.interruptedSend().isEmpty()
              && e.lastError().contains("not a JPEG"),
              "a photo refused before any write is skipped without a recovery marker");
        SpokenSyncTestAccess::photoActive(e, "photo-2");
        e.onPhotoSendDone("photo-2", false, 0, "StoreFull", QString());
        check(QFile::exists(data + "/inflight-send") && !e.interruptedSend().isEmpty() && !e.isSyncing(),
              "a photo that failed on the device still stops the sync and keeps its recovery marker");
        e.dismissInterruptedSend();
    }
    {
        // A video libzune refused leaves nothing behind and clears its marker;
        // one whose partial object could not be removed keeps it for review.
        auto d = DeviceSpokenTestAccess::create();
        SyncEngine e; e.clearQueue(); e.dismissInterruptedSend();
        SpokenSyncTestAccess::cleanupDevice(e, d.get());
        SpokenSyncTestAccess::videoActive(e, "video-1");
        e.onVideoSendDone("video-1", false, 0, "Failed to send video (StoreFull 0x200C): /tmp/pilot.mp4");
        check(!QFile::exists(data + "/inflight-send") && e.interruptedSend().isEmpty(),
              "a refused video clears its transfer marker");
        SpokenSyncTestAccess::videoActive(e, "video-2");
        e.onVideoSendDone("video-2", false, 0, "Failed to send video (IncompleteTransfer 0x02FF): /tmp/pilot.mp4; "
                          "new object 33555750 could not be removed, inspect it before retrying");
        check(QFile::exists(data + "/inflight-send") && e.interruptedSend() == "Pilot" && !e.isSyncing(),
              "a video whose partial object could not be removed keeps its marker and stops the sync");
        e.dismissInterruptedSend();
        SpokenSyncTestAccess::videoActive(e, "video-cancel-uncertain");
        e.cancelSync();
        e.onVideoSendDone("video-cancel-uncertain", false, 0,
                          "Failed to send video (IncompleteTransfer 0x02FF): /tmp/pilot.mp4; "
                          "the device gave no usable answer and may hold an incomplete object");
        check(QFile::exists(data + "/inflight-send") && e.interruptedSend() == "Pilot" && !e.isSyncing(),
              "a cancelled video with an unanswered creation keeps its recovery marker");
        e.dismissInterruptedSend();
        SpokenSyncTestAccess::videoActive(e, "video-cancel-before-write");
        e.cancelSync();
        e.onVideoSendDone("video-cancel-before-write", false, 0, "Aborted: /tmp/pilot.mp4");
        check(!QFile::exists(data + "/inflight-send") && e.interruptedSend().isEmpty() && !e.isSyncing(),
              "a video cancelled before any write clears its marker without reporting an incomplete object");
    }
    {
        // A connected Zune whose library could not be read is not an empty
        // device: sync refuses instead of resending everything as missing.
        auto d = DeviceSpokenTestAccess::create();
        DeviceSpokenTestAccess::libraryUnreadable(*d, "ZMDB read incomplete: got 0 of 1000 bytes");
        SyncEngine e; e.clearQueue(); e.dismissInterruptedSend();
        SpokenSyncTestAccess::cleanupDevice(e, d.get());
        int transfers = 0;
        QObject::connect(&e, &SyncEngine::syncStateChanged, &e, [&] { if (e.isSyncing()) ++transfers; });
        e.startSync();
        check(!e.isSyncing() && transfers == 0 && e.lastError().contains("library could not be read"),
              "sync refuses a Zune whose library could not be read");
        DeviceSpokenTestAccess::libraryUnreadable(*d, QString());
    }
    {
        auto d = DeviceSpokenTestAccess::create();
        SyncEngine e; e.clearQueue(); e.dismissInterruptedSend();
        SpokenSyncTestAccess::cleanupDevice(e, d.get());
        QVariantList candidates{cleanupRow, cleanupTwo};
        e.attachPodcastCleanup([&](const QStringList &, bool) { return candidates; });
        auto live = cleanupRow;
        live["title"] = "Completed on device"; live["author"] = "Host";
        live["sourceKnown"] = true; live["playedKnown"] = true; live["played"] = true;
        DeviceSpokenTestAccess::seedPreview(*d, {live});
        int writes = 0;
        QObject::connect(&e, &SyncEngine::recordDevicePlayed, &e, [&] { ++writes; });
        QObject::connect(&e, &SyncEngine::purgePlayedPodcasts, &e, [&] { ++writes; });
        QSettings().setValue("podcastAutoCleanup", true);
        e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 1 && e.queue()->entries()[0].title == "Completed on device"
              && e.queue()->entries()[0].statusNote == "Will remove on sync" && writes == 0 && !e.isSyncing(),
              "completed episode appears before sync without persisting history or issuing deletion");
        e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 1, "repeated previews do not duplicate removal actions");
        live["playedKnown"] = false; DeviceSpokenTestAccess::seedPreview(*d, {live});
        e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 0, "unknown played state clears pending removal preview");
        live["playedKnown"] = true; DeviceSpokenTestAccess::seedPreview(*d, {live});
        candidates.clear(); e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 0, "keep-on-device candidate exclusion clears preview");
        candidates = {cleanupRow, cleanupTwo}; e.refreshPodcastCleanupPreview();
        QSettings().setValue("podcastAutoCleanup", false); e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 0, "disabling cleanup removes pending previews");
        QSettings().setValue("podcastAutoCleanup", true); e.refreshPodcastCleanupPreview();
        e.removeGroup(SyncQueueModel::groupKeyFor(e.queue()->entries()[0]));
        e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 0, "group removal excludes cleanup previews too");
        DeviceSpokenTestAccess::invalidate(*d); e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 1, "new connection rebuilds previews without previous session exclusions");
        e.clearQueue(); e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 0, "clearing queue cannot leave a hidden planned deletion");
        DeviceSpokenTestAccess::invalidate(*d); e.refreshPodcastCleanupPreview();
        e.removeEntry(e.queue()->entries()[0].entryId); e.refreshPodcastCleanupPreview();
        check(e.queue()->count() == 0, "removing preview row keeps it excluded for this connection");
        int inspections = 0;
        QObject::connect(&e, &SyncEngine::inspectPodcastCleanup, &e,
            [&](quint64, const QString &, const QString &, const QVariantList &rows) {
                ++inspections;
                check(rows.size() == 1 && rows[0].toMap().value("spokenId") == "episode-2",
                      "sync inspection excludes the removed preview episode");
            });
        e.startSync(); check(inspections == 1, "preview exclusion reaches the live sync path");
        e.cancelSync();
    }
    QSettings().remove("podcastAutoCleanup");
    for (const auto &mode : {"success", "reused", "failed", "stale-session", "stale-entry", "audiobook"}) {
        auto d = DeviceSpokenTestAccess::create();
        SyncEngine e; e.clearQueue(); e.dismissInterruptedSend();
        SpokenSyncTestAccess::cleanupDevice(e, d.get());
        auto payload = row;
        if (QString::fromLatin1(mode) == "audiobook") payload["type"] = "audiobook";
        const auto id = e.queue()->addPodcast(row, 1);
        SpokenSyncTestAccess::active(e, id, payload);
        if (QString::fromLatin1(mode) == "stale-session") DeviceSpokenTestAccess::invalidate(*d);
        const bool ok = QString::fromLatin1(mode) != "failed";
        e.onSpokenSendDone(QString::fromLatin1(mode) == "stale-entry" ? "unrelated" : id,
            ok, 123u, ok ? QString() : QString("failure"), QString::fromLatin1(mode) == "reused",
            QString::fromLatin1(mode) == "audiobook", false);
        const bool expected = QString::fromLatin1(mode) == "success" || QString::fromLatin1(mode) == "reused";
        check(d->podcastDeviceStatus(row.value("feedUrl").toString(), row.value("sourceUrl").toString())
                  .value("onDevice").toBool() == expected,
              qPrintable(QString("podcast badge admission for %1 send result").arg(mode)));
    }
    printf("Spoken sync: %d checks, %d failures; no USB\n", checks, failed);
    return failed ? 1 : 0;
}
