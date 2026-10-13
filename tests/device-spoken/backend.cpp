#include "DeviceService.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <memory>
#include <cstdio>

// This seam never constructs a UsbWatcher. Remove every facade -> worker
// connection before faking a connection: no fixture can enqueue USB work.
struct DeviceSpokenTestAccess {
    static std::unique_ptr<DeviceService> create() {
        auto service = std::unique_ptr<DeviceService>(new DeviceService(nullptr, false));
        QObject::disconnect(service.get(), nullptr, service->m_worker, nullptr);
        return service;
    }
    static void connect(DeviceService &s) { s.m_connected = true; s.m_serial = "fixture-one"; }
    static void disconnect(DeviceService &s) { s.m_connected = false; s.clearDeviceState(); }
    static quint64 generation(DeviceService &s) { return s.m_spokenGeneration; }
    static quint64 request(DeviceService &s) { return s.m_spokenRequest; }
    static void busy(DeviceService &s, bool value) { s.m_busy = value; }
    static void pulling(DeviceService &s, bool value) { s.m_pulling = value; }
    static void complete(DeviceService &s, const QVariantList &podcasts, const QVariantList &books,
                         const QString &error = {}) {
        s.worker()->spokenMediaRead(s.m_spokenGeneration, s.m_spokenRequest,
                                   s.m_serial, podcasts, books, error);
    }
    static void purgeDone(DeviceService &s, const QVariantList &ids, int failed = 0) {
        s.worker()->spokenItemsPurged(s.m_spokenGeneration, s.m_spokenRequest,
                                     s.m_serial, ids, failed,
                                     failed ? QStringLiteral("Device refused remaining items") : QString());
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("DeviceSpokenFixture");
    app.setApplicationName("DeviceSpoken");
    const auto root = qEnvironmentVariable("DEVICE_SPOKEN_TEST_ROOT");
    if (!root.startsWith("/tmp/zuuned-device-spoken-")
        || !QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).startsWith(root + '/')) return 2;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks; failures += !ok; printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    };
    auto s = DeviceSpokenTestAccess::create();
    int reads = 0, deletes = 0, genericDeletes = 0, artRequests = 0;
    QVariantList requestedRows;
    QString artPath;
    quint32 artId = 0;
    QObject::connect(s.get(), &DeviceService::workerReadSpokenMedia, &app,
                     [&](quint64, quint64, const QString &) { ++reads; });
    QObject::connect(s.get(), &DeviceService::workerPurgeSpokenItems, &app,
                     [&](quint64, quint64, const QString &, const QString &, const QVariantList &rows) {
        ++deletes; requestedRows = rows;
    });
    QObject::connect(s.get(), &DeviceService::workerPurgeItems, &app,
                     [&](const QVariantList &) { ++genericDeletes; });
    QObject::connect(s.get(), &DeviceService::workerGrabArt, &app,
                     [&](quint32 id, const QString &path) { ++artRequests; artId = id; artPath = path; });
    const QVariantMap podcast{{"itemId", 11u}, {"parentId", 2u}, {"seriesId", 3u},
        {"title", "Episode"}, {"author", "Author"}, {"seriesName", "Show"},
        {"durationMs", 1000u}, {"sizeMB", 1.0}, {"sizeBytes", 1000000LL}, {"artItemId", 3u}};
    auto secondPodcast = podcast; secondPodcast["itemId"] = 12u;
    const QVariantMap book{{"itemId", 21u}, {"parentId", 4u}, {"partNumber", 1u},
        {"title", "Chapter"}, {"author", "Author"}, {"bookTitle", "Book"},
        {"durationMs", 1000u}, {"sizeMB", 2.0}, {"sizeBytes", 2000000LL}, {"artItemId", 21u}};
    s->refreshSpokenMedia(); s->purgeSpokenItems("podcast", {11u});
    check(reads == 0 && deletes == 0, "disconnected facade never queues spoken I/O");
    DeviceSpokenTestAccess::connect(*s);
    s->refreshSpokenMedia();
    check(reads == 1 && s->spokenMediaLoading() && s->busy(), "lazy refresh marks device busy and queues one read");
    s->refreshSpokenMedia(); s->purgeSpokenItems("podcast", {11u});
    check(reads == 1 && deletes == 0, "duplicate refresh and deletion refused during inventory read");
    s->worker()->spokenMediaRead(DeviceSpokenTestAccess::generation(*s), 999, "fixture-one", {podcast}, {book}, {});
    check(s->spokenMediaLoading() && s->podcastEpisodesList().isEmpty(), "stale request token cannot publish an inventory");
    s->worker()->spokenMediaRead(DeviceSpokenTestAccess::generation(*s), DeviceSpokenTestAccess::request(*s), "other-device", {podcast}, {book}, {});
    check(s->spokenMediaLoading(), "another device serial cannot finish an inventory request");
    DeviceSpokenTestAccess::complete(*s, {podcast, secondPodcast}, {book});
    check(!s->busy() && s->podcastEpisodesList().size() == 2 && s->audiobookPartsList().size() == 1,
          "correlated read publishes separate spoken inventories");
    check(s->trackCount() == 0 && s->tracks()->rowCount() == 0, "spoken inventories do not create music rows");
    s->requestArt(3u);
    check(artRequests == 1 && artId == 3u && artPath.contains("/spoken_"), "show art uses series object and a session-scoped cache");
    s->worker()->artDone(artId, artPath, true);
    check(s->artPaths().contains("3"), "device spoken artwork enters the existing artPaths facade");
    s->refreshSpokenMedia();
    DeviceSpokenTestAccess::complete(*s, {}, {}, "Inventory unavailable");
    check(!s->spokenMediaLoading() && s->spokenMediaError() == "Inventory unavailable"
          && s->podcastEpisodesList().size() == 2, "failed refresh keeps last good rows and reports the error");
    s->purgeSpokenItems("audiobook", {11u});
    s->purgeSpokenItems("podcast", {21u});
    s->purgeSpokenItems("podcast", {999u});
    s->purgeSpokenItems("wrong-kind", {11u});
    s->purgeSpokenItems("podcast", {11.2});
    s->purgeSpokenItems("podcast", {4294967307.0});
    s->purgeSpokenItems("podcast", {4294967295.0});
    check(deletes == 0, "unknown IDs, cross-category IDs and unknown kinds cannot enqueue deletion");
    s->setSyncActive(true);
    s->purgeSpokenItems("podcast", {11u}); s->purgeItems({11u}); s->refreshSpokenMedia();
    check(deletes == 0 && genericDeletes == 0 && reads == 2, "sync ownership blocks spoken read and all delete admission");
    s->setSyncActive(false);
    DeviceSpokenTestAccess::busy(*s, true); s->purgeSpokenItems("podcast", {11u});
    check(deletes == 0, "connection work blocks spoken deletion");
    DeviceSpokenTestAccess::busy(*s, false);
    DeviceSpokenTestAccess::pulling(*s, true); s->purgeSpokenItems("podcast", {11u});
    check(deletes == 0, "device extraction blocks spoken deletion");
    DeviceSpokenTestAccess::pulling(*s, false);
    s->purgeSpokenItems("podcast", {11u, 11u, 12u});
    check(deletes == 1 && requestedRows.size() == 2 && s->purging(), "duplicate selected IDs are coalesced with complete expected identity");
    s->purgeSpokenItems("audiobook", {21u});
    check(deletes == 1, "overlapping deletes are refused");
    s->worker()->spokenItemsPurged(DeviceSpokenTestAccess::generation(*s), 999, "fixture-one", {11u}, 0, {});
    check(s->podcastEpisodesList().size() == 2 && s->purging(), "stale delete completion cannot prune rows or finish the current operation");
    DeviceSpokenTestAccess::purgeDone(*s, {11u}, 1);
    check(s->podcastEpisodesList().size() == 1 && s->podcastEpisodesList().first().toMap().value("itemId").toUInt() == 12u
          && s->audiobookPartsList().size() == 1 && !s->purging(), "partial deletion removes confirmed IDs only and preserves other categories");
    check(!s->spokenMediaError().isEmpty(), "partial deletion reports its failure");
    s->requestArt(21u);
    const QString deletedArtPath = artPath;
    s->worker()->artDone(21u, deletedArtPath, true);
    s->purgeSpokenItems("audiobook", {21u});
    DeviceSpokenTestAccess::purgeDone(*s, {21u});
    check(!s->artPaths().contains("21"), "confirmed deleted book parts lose cached artwork aliases");
    s->worker()->artDone(21u, deletedArtPath, true);
    check(!s->artPaths().contains("21"), "late artwork cannot resurrect a deleted part");
    s->refreshSpokenMedia();
    DeviceSpokenTestAccess::complete(*s, {podcast}, {book});
    s->requestArt(21u);
    check(artPath != deletedArtPath, "refreshed recycled IDs use fresh disk artwork paths");
    s->worker()->artDone(21u, deletedArtPath, true);
    check(!s->artPaths().contains("21"), "prior inventory artwork cannot overwrite refreshed items");
    const QString currentArtPath = artPath;
    auto otherProcess = DeviceSpokenTestAccess::create();
    DeviceSpokenTestAccess::connect(*otherProcess);
    otherProcess->refreshSpokenMedia();
    DeviceSpokenTestAccess::complete(*otherProcess, {podcast}, {book});
    QString otherArtPath;
    QObject::connect(otherProcess.get(), &DeviceService::workerGrabArt, &app,
                     [&](quint32, const QString &path) { otherArtPath = path; });
    otherProcess->requestArt(21u);
    check(!otherArtPath.isEmpty() && otherArtPath != currentArtPath,
          "independent app sessions cannot share spoken artwork paths for reused IDs");
    DeviceSpokenTestAccess::disconnect(*otherProcess);
    s->refreshSpokenMedia();
    const auto oldGeneration = DeviceSpokenTestAccess::generation(*s);
    const auto oldRequest = DeviceSpokenTestAccess::request(*s);
    DeviceSpokenTestAccess::disconnect(*s);
    check(s->podcastEpisodesList().isEmpty() && s->audiobookPartsList().isEmpty() && !s->spokenMediaLoading()
          && s->artPaths().isEmpty(), "disconnect clears spoken inventory, loading state and art aliases");
    DeviceSpokenTestAccess::connect(*s);
    s->worker()->spokenMediaRead(oldGeneration, oldRequest, "fixture-one", {podcast}, {book}, {});
    s->worker()->artDone(3u, artPath, true);
    check(s->podcastEpisodesList().isEmpty() && s->artPaths().isEmpty(), "same-serial reconnect rejects prior session inventory and artwork");
    DeviceSpokenTestAccess::disconnect(*s);
    {
        auto d = DeviceSpokenTestAccess::create();
        int statusReads = 0;
        QObject::connect(d.get(), &DeviceService::workerReadSpokenMedia, &app,
                         [&](quint64, quint64, const QString &) { ++statusReads; });
        const QString feed = "https://example.test/feed", source = "https://example.test/episode";
        auto first = podcast; first["feedUrl"] = feed; first["sourceUrl"] = source;
        first["playedKnown"] = true; first["played"] = true; first["bookmarkMs"] = 40600u;
        auto second = first; second["itemId"] = 12u; second["sourceUrl"] = source + "-two";
        second["playedKnown"] = false; second["played"] = true;
        check(!d->podcastDeviceStatus(feed, source).value("known").toBool(), "disconnected podcast membership is unknown");
        DeviceSpokenTestAccess::connect(*d);
        check(!d->podcastDeviceStatus(feed, source).value("known").toBool(), "unread device inventory is unknown rather than absent");
        DeviceSpokenTestAccess::busy(*d, true);
        d->ensureSpokenMedia(); d->ensureSpokenMedia();
        check(statusReads == 0, "lazy podcast status never interrupts active device ownership");
        DeviceSpokenTestAccess::busy(*d, false); d->stateChanged(); QCoreApplication::processEvents();
        check(statusReads == 1 && d->spokenMediaLoading(), "deferred lazy status read resumes when device ownership is free");
        d->ensureSpokenMedia(); d->ensureSpokenMedia();
        check(statusReads == 1, "repeated visible-page signals coalesce into one status read");
        DeviceSpokenTestAccess::complete(*d, {first, second}, {});
        const auto found = d->podcastDeviceStatus(feed, source);
        check(found.value("known").toBool() && found.value("onDevice").toBool()
              && found.value("playedKnown").toBool() && found.value("played").toBool()
              && found.value("bookmarkMs").toUInt() == 40600, "exact episode membership exposes verified played status and bookmark");
        check(d->podcastDeviceStatus(feed, source + "-two").value("onDevice").toBool()
              && !d->podcastDeviceStatus(feed, source + "-two").value("played").toBool(), "unknown playback state cannot become a played indicator");
        const auto absent = d->podcastDeviceStatus(feed, "different-source-same-title");
        check(absent.value("known").toBool() && !absent.value("onDevice").toBool(), "complete inventory establishes absence using exact source, never title");
        check(!d->podcastDeviceStatus(feed.toUpper(), source).value("onDevice").toBool(), "podcast source identities are not case-folded");
        d->ensureSpokenMedia(); check(statusReads == 1, "completed lazy lookup remains idempotent within the session");
        auto legacy = first; legacy["sourceKnown"] = false; legacy["feedUrl"] = QString(); legacy["sourceUrl"] = QString();
        d->refreshSpokenMedia(); DeviceSpokenTestAccess::complete(*d, {legacy, second}, {book});
        check(d->podcastEpisodesList().size() == 2 && d->audiobookPartsList().size() == 1 && d->spokenMediaError().isEmpty()
              && !d->podcastStatusWarning().isEmpty(), "unknown podcast source retains both browsers and surfaces a separate badge warning");
        check(!d->podcastDeviceStatus(feed, source).value("known").toBool()
              && d->podcastDeviceStatus(feed, source + "-two").value("onDevice").toBool(), "incomplete source mapping preserves exact positives but never establishes global absence");
        d->refreshSpokenMedia(); DeviceSpokenTestAccess::complete(*d, {}, {book});
        check(d->podcastDeviceStatus(feed, source).value("known").toBool()
              && !d->podcastDeviceStatus(feed, source).value("onDevice").toBool()
              && d->podcastStatusWarning().isEmpty(), "complete empty podcast inventory gives known absence without source-warning state");
        d->refreshSpokenMedia(); DeviceSpokenTestAccess::complete(*d, {first, second}, {});
        d->refreshSpokenMedia(); DeviceSpokenTestAccess::complete(*d, {}, {}, "source lookup unavailable");
        check(!d->podcastDeviceStatus(feed, source).value("known").toBool()
              && !d->podcastDeviceStatus(feed, "missing").value("known").toBool()
              && d->podcastEpisodesList().size() == 2, "failed refresh preserves browser rows but invalidates membership certainty");
        const auto generation = d->spokenGeneration();
        d->notePodcastOnDevice(generation, "fixture-one", first, 11u);
        check(d->podcastDeviceStatus(feed, source).value("onDevice").toBool()
              && !d->podcastDeviceStatus(feed, source).value("playedKnown").toBool()
              && !d->podcastDeviceStatus(feed, "missing").value("known").toBool(), "confirmed send proves only its exact source presence, not played state or global absence");
        d->notePodcastOnDevice(generation + 1, "fixture-one", second, 12u);
        d->notePodcastOnDevice(generation, "other-device", second, 12u);
        check(!d->podcastDeviceStatus(feed, source + "-two").value("onDevice").toBool(), "stale session and serial send results cannot add device badges");
        auto replaced = first; replaced["sourceUrl"] = source + "-recycled";
        d->notePodcastOnDevice(generation, "fixture-one", replaced, 11u);
        check(!d->podcastDeviceStatus(feed, source).value("onDevice").toBool()
              && d->podcastDeviceStatus(feed, source + "-recycled").value("onDevice").toBool(), "recycled object ID cannot retain an old source badge");
        d->refreshSpokenMedia(); DeviceSpokenTestAccess::complete(*d, {first, second}, {});
        d->notePodcastOnDevice(generation, "fixture-one", first, 11u);
        check(d->podcastDeviceStatus(feed, source).value("played").toBool(), "same exact reused identity preserves known firmware played state");
        d->applyPodcastCleanup(generation + 1, "fixture-one", {first});
        check(d->podcastDeviceStatus(feed, source).value("onDevice").toBool(), "old-session cleanup cannot remove a current device badge");
        d->applyPodcastCleanup(generation, "fixture-one", {first});
        check(!d->podcastDeviceStatus(feed, source).value("onDevice").toBool()
              && d->podcastDeviceStatus(feed, source + "-two").value("onDevice").toBool(), "cleanup removes only confirmed source badges");
        d->purgeSpokenItems("podcast", {12u}); DeviceSpokenTestAccess::purgeDone(*d, {12u});
        check(!d->podcastDeviceStatus(feed, source + "-two").value("onDevice").toBool(), "manual confirmed deletion removes podcast membership badge");
        DeviceSpokenTestAccess::disconnect(*d);
        check(!d->podcastDeviceStatus(feed, source).value("known").toBool(), "disconnect clears all podcast membership and playback keys");
        DeviceSpokenTestAccess::connect(*d); d->ensureSpokenMedia();
        check(d->spokenMediaLoading(), "same-serial reconnect permits a fresh lazy status read");
        DeviceSpokenTestAccess::disconnect(*d);
    }
    printf("Device spoken facade: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
