#include "DeviceWorker.h"
#include <QCoreApplication>
#include <cstring>
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "zune.h"
}
// Every device function reached by these two slots is wrapped at link time.
// The sentinel handle never reaches libusb and no facade/watcher is constructed.
struct PodcastCleanupWorkerTestAccess {
    static void attach(DeviceWorker &w) { w.m_dev = reinterpret_cast<ZuneDevice *>(quintptr(1)); }
};
static QVector<ZunePodcastState> states;
static QVector<ZunePodcastEpisode> inventory;
static QVector<ZuneAudiobookPart> bookInventory;
static QHash<QString, quint32> mappings;
static QList<quint32> deleted;
static bool aborted = false, failState = false, failInventory = false, failFind = false;
static int clearCount = 0, failDeleteAt = -1, sourceReads = 0, stateReads = 0;
static DeviceWorker *worker = nullptr;
static ZuneDeviceFamily family = ZUNE_FAMILY_KEEL;
static int bookReads = 0;
static bool changeGenerationDuringInventory = false;
static bool changeGenerationDuringFind = false, cancelAfterDelete = false;
extern "C" {
ZuneDeviceFamily __wrap_zune_get_family(ZuneDeviceHandle) { return family; }
const char *__wrap_zune_get_serial(ZuneDeviceHandle) { return "fixture"; }
int __wrap_zune_is_aborted(ZuneDeviceHandle) { return aborted; }
void __wrap_zune_clear_abort(ZuneDeviceHandle) { aborted = false; ++clearCount; }
int __wrap_zune_get_podcast_states(ZuneDeviceHandle, ZunePodcastState **out, int *count) {
    ++stateReads; *out = nullptr; *count = 0; if (failState || (family != ZUNE_FAMILY_KEEL && family != ZUNE_FAMILY_DRACO && family != ZUNE_FAMILY_PAVO)) return -1;
    *count = states.size(); *out = static_cast<ZunePodcastState *>(calloc(*count, sizeof(**out)));
    if (*count) memcpy(*out, states.data(), *count * sizeof(**out));
    for (int i = 0; i < *count; ++i) (*out)[i].title = states[i].title ? strdup(states[i].title) : nullptr;
    return 0;
}
void __wrap_zune_free_podcast_states(ZunePodcastState *p, int count) {
    for (int i = 0; i < count; ++i) free(p[i].title);
    free(p);
}
int __wrap_zune_get_podcast_episodes(ZuneDeviceHandle, ZunePodcastEpisode **out, int *count) {
    *out = nullptr; *count = 0; if (failInventory) return -1;
    *count = inventory.size(); *out = static_cast<ZunePodcastEpisode *>(calloc(*count, sizeof(**out)));
    if (*count) memcpy(*out, inventory.data(), *count * sizeof(**out));
    if (changeGenerationDuringInventory) worker->setSpokenGeneration(2);
    return 0;
}
void __wrap_zune_free_podcast_episodes(ZunePodcastEpisode *p, int) { free(p); }
int __wrap_zune_get_podcast_episode_sources(ZuneDeviceHandle, ZunePodcastEpisodeSource **out, int *count) {
    ++sourceReads; *out = nullptr; *count = 0;
    if (failFind) return -1;
    *out = static_cast<ZunePodcastEpisodeSource *>(calloc(mappings.size(), sizeof(**out)));
    for (auto it = mappings.cbegin(); it != mappings.cend(); ++it) {
        if (!it.value()) continue;
        auto &source = (*out)[(*count)++];
        const auto values = it.key().split('|');
        source.item_id = it.value(); source.series_id = values[0] == "https://other-feed" ? 8 : 7; source.parent_id = 9;
        source.feed_url = strdup(values[0].toUtf8().constData());
        source.source_url = strdup(values[1].toUtf8().constData());
    }
    if (changeGenerationDuringFind) worker->setSpokenGeneration(2);
    return 0;
}
void __wrap_zune_free_podcast_episode_sources(ZunePodcastEpisodeSource *p, int count) {
    for (int i = 0; i < count; ++i) { free(p[i].feed_url); free(p[i].source_url); }
    free(p);
}
int __wrap_zune_get_podcast_series(ZuneDeviceHandle, ZunePodcastSeries **out, int *count) {
    *out = nullptr; *count = 0; return 0;
}
void __wrap_zune_free_podcast_series(ZunePodcastSeries *p, int) { free(p); }
int __wrap_zune_get_audiobook_parts(ZuneDeviceHandle, ZuneAudiobookPart **out, int *count) {
    ++bookReads;
    if (family != ZUNE_FAMILY_KEEL && family != ZUNE_FAMILY_DRACO && family != ZUNE_FAMILY_PAVO) { *out = nullptr; *count = 0; return -1; }
    *count = bookInventory.size(); *out = static_cast<ZuneAudiobookPart *>(calloc(*count, sizeof(**out)));
    if (*count) memcpy(*out, bookInventory.data(), *count * sizeof(**out));
    return 0;
}
void __wrap_zune_free_audiobook_parts(ZuneAudiobookPart *p, int) { free(p); }
int __wrap_zune_purge_track(ZuneDeviceHandle, uint32_t id) {
    if (deleted.size() == failDeleteAt) return -1;
    deleted.append(id); if (cancelAfterDelete) aborted = true; return 0;
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    DeviceWorker w; worker = &w; PodcastCleanupWorkerTestAccess::attach(w);
    QVariantList planned, purged, progress; QString inspectError, purgeError, spokenPurgeError;
    QObject::connect(&w, &DeviceWorker::podcastCleanupProgress, &app,
        [&](quint64 generation, const QString &token, const QString &serial, quint32 id, bool removed) {
            progress.append(QVariantMap{{"generation", QVariant::fromValue(generation)}, {"token", token},
                {"serial", serial}, {"itemId", id}, {"removed", removed}});
        });
    QObject::connect(&w, &DeviceWorker::spokenItemsPurged, &app,
        [&](quint64, quint64, const QString &, const QVariantList &, int, const QString &error) { spokenPurgeError = error; });
    int checks = 0, failures = 0;
    auto check = [&](bool value, const char *name) { ++checks; failures += !value; printf("%s %s\n", value ? "PASS" : "FAIL", name); };
    QObject::connect(&w, &DeviceWorker::podcastCleanupInspected, &app,
        [&](quint64, const QString &, const QString &, const QVariantList &rows, const QString &error) { planned = rows; inspectError = error; });
    QObject::connect(&w, &DeviceWorker::playedPodcastsPurged, &app,
        [&](quint64, const QString &, const QString &, const QVariantList &rows, const QString &error) { purged = rows; purgeError = error; });
    const QVariantMap one{{"spokenId", "local1"}, {"identity", "feed-guid-1"}, {"feedUrl", "https://feed"}, {"sourceUrl", "https://one"}};
    auto two = one; two["spokenId"] = "local2"; two["identity"] = "feed-guid-2"; two["sourceUrl"] = "https://two";
    auto reset = [&] {
        states = {{11, 7, 60000, 40600, 0x200, 1}, {12, 7, 60000, 9000, 0, 0}};
        states[0].title = const_cast<char *>("One"); states[1].title = const_cast<char *>("Two");
        inventory = {{11, 9, 7, 60000, 1000, const_cast<char *>("One"), const_cast<char *>("Author"), nullptr},
                     {12, 9, 7, 60000, 1000, const_cast<char *>("Two"), const_cast<char *>("Author"), nullptr}};
        mappings = {{"https://feed|https://one", 11}, {"https://feed|https://two", 12}};
        aborted = failState = failInventory = failFind = changeGenerationDuringFind = cancelAfterDelete = false;
        family = ZUNE_FAMILY_KEEL; bookReads = 0; changeGenerationDuringInventory = false;
        bookInventory.clear(); deleted.clear(); progress.clear(); clearCount = 0; sourceReads = 0; stateReads = 0; failDeleteAt = -1; w.setSpokenGeneration(1);
    };
    auto inspect = [&](const QVariantList &input) { w.doInspectPodcastCleanup(1, "token", "fixture", input); };
    auto purge = [&](const QVariantList &input) { w.doPurgePlayedPodcasts(1, "token", "fixture", input); };
    reset(); inspect({one, two});
    check(inspectError.isEmpty() && planned.size() == 1 && planned[0].toMap().value("bookmarkMs").toUInt() == 40600, "marked played with retained bookmark is selected; partial episode remains");
    check(deleted.isEmpty(), "inspection is read only");
    check(sourceReads == 1, "multiple local episodes use one complete bulk source inventory");
    auto approved = planned; purge(approved);
    check(purgeError.isEmpty() && purged == approved && deleted == QList<quint32>{11}, "unchanged played identity is deleted and confirmed");
    check(clearCount == 1, "purge never clears abort");
    check(progress.size() == 2 && !progress[0].toMap().value("removed").toBool()
        && progress[1].toMap().value("removed").toBool() && progress[1].toMap().value("itemId").toUInt() == 11
        && progress[1].toMap().value("generation").toULongLong() == 1
        && progress[1].toMap().value("token") == "token" && progress[1].toMap().value("serial") == "fixture",
        "removal progress carries exact session and emits confirmed state only after successful delete");
    reset(); auto kept = one; kept["keepOnDevice"] = true; inspect({kept});
    check(inspectError.isEmpty() && planned.isEmpty(), "keep on device excludes played episode");
    reset(); states.clear(); inspect({one}); check(inspectError.isEmpty() && planned.isEmpty(), "missing state is unknown and retained");
    reset(); states[0].raw_flags = 0x100; inspect({one}); check(planned.isEmpty(), "unrelated state flags do not imply played");
    reset(); failState = true; inspect({one}); check(!inspectError.isEmpty() && planned.isEmpty(), "state read failure cannot produce a partial plan");
    reset(); failInventory = true; inspect({one}); check(!inspectError.isEmpty() && planned.isEmpty(), "inventory read failure fails closed");
    reset(); family = ZUNE_FAMILY_DRACO; inventory.clear(); inspect({one, two});
    check(inspectError.isEmpty() && planned.isEmpty() && stateReads == 0 && sourceReads == 0 && deleted.isEmpty(),
        "empty Draco inventory needs no unnecessary listening-state read or warning");
    for (const auto testedFamily : {ZUNE_FAMILY_KEEL, ZUNE_FAMILY_DRACO, ZUNE_FAMILY_PAVO}) {
        const auto familyName = testedFamily == ZUNE_FAMILY_KEEL ? "Keel" : testedFamily == ZUNE_FAMILY_DRACO ? "Draco" : "Pavo";
        const auto familyCheck = [&](bool passed, const char *caseName) {
            const auto label = QByteArray(familyName) + ": " + caseName;
            check(passed, label.constData());
        };
        reset(); family = testedFamily; inspect({one, two});
        familyCheck(inspectError.isEmpty() && planned.size() == 1 && deleted.isEmpty()
            && planned.first().toMap().value("itemId").toUInt() == 11,
            "complete played identity plans one removal without writing");
        approved = planned; purge(approved);
        familyCheck(purgeError.isEmpty() && purged == approved && deleted == QList<quint32>{11},
            "revalidated played episode is removed and partial episode retained");
        reset(); family = testedFamily; inspect({kept});
        familyCheck(inspectError.isEmpty() && planned.isEmpty() && deleted.isEmpty(),
            "Keep on Zune protects a firmware-played episode");
        reset(); family = testedFamily; failState = true; inspect({one});
        familyCheck(inspectError.contains("Could not read complete") && !inspectError.contains("not supported")
            && planned.isEmpty() && deleted.isEmpty(),
            "failed supported listening-state read reports failure and authorizes no deletion");
        reset(); family = testedFamily; inspect({one}); approved = planned; failState = true; purge(approved);
        familyCheck(!purgeError.isEmpty() && purged.isEmpty() && deleted.isEmpty(),
            "state read failure after preview prevents deletion");
        reset(); family = testedFamily; states[0].marked_played = 0; states[0].raw_flags = 0; inspect({one, two});
        familyCheck(inspectError.isEmpty() && planned.isEmpty() && sourceReads == 0 && deleted.isEmpty(),
            "complete unplayed snapshot is a successful no-op");
    }
    reset(); family = ZUNE_FAMILY_SCORPIUS; inspect({one});
    check(inspectError.contains("not supported") && planned.isEmpty() && sourceReads == 0 && deleted.isEmpty(),
        "unvalidated Scorpius remains unsupported and cannot authorize cleanup");
    reset(); family = ZUNE_FAMILY_DRACO; inventory.clear(); failInventory = true; inspect({one});
    check(!inspectError.isEmpty() && planned.isEmpty() && stateReads == 0 && deleted.isEmpty(),
        "failed Draco inventory is never treated as known empty");
    reset(); family = ZUNE_FAMILY_DRACO; inventory.clear(); changeGenerationDuringInventory = true; inspect({one});
    check(!inspectError.isEmpty() && planned.isEmpty() && deleted.isEmpty(),
        "empty inventory from a stale device generation is rejected");
    reset(); inspect({one}); approved = planned; inventory.clear(); purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(),
        "inventory emptied after approval invalidates cleanup before deletion");
    reset(); failFind = true; inspect({one}); check(!inspectError.isEmpty() && planned.isEmpty(), "exact identity read failure fails closed");
    reset(); inspect({one, one}); check(!inspectError.isEmpty() && planned.isEmpty(), "duplicate local identity rejected");
    reset(); auto otherFeed = two; otherFeed["feedUrl"] = "https://other-feed"; otherFeed["identity"] = one.value("identity");
    mappings.remove("https://feed|https://two"); mappings["https://other-feed|https://two"] = 12; states[1].marked_played = 1; states[1].raw_flags = 0x200;
    states[1].series_id = 8; inventory[1].series_id = 8;
    inspect({one, otherFeed}); check(inspectError.isEmpty() && planned.size() == 2, "identical GUIDs in different feeds retain independent source identities");
    reset(); auto alias = two; alias["sourceUrl"] = "https://one"; inspect({one, alias}); check(!inspectError.isEmpty(), "duplicate feed and enclosure rejected");
    reset(); mappings["https://feed|https://two"] = 11; inspect({one, two}); check(!inspectError.isEmpty() && planned.isEmpty(), "two local identities mapping same device object rejected");
    reset(); states[0].series_id = 8; inspect({one}); check(!inspectError.isEmpty() && planned.isEmpty(), "series identity mismatch fails closed");
    reset(); states[0].duration_ms++; inspect({one}); check(!inspectError.isEmpty() && planned.isEmpty(), "state and live duration mismatch rejects stale database candidate");
    reset(); states[0].title = const_cast<char *>("Previous occupant of handle"); inspect({one});
    check(!inspectError.isEmpty() && planned.isEmpty(), "state and live title mismatch rejects recycled handle within a show");
    reset(); states[0].title = nullptr; inspect({one});
    check(!inspectError.isEmpty() && planned.isEmpty(), "absent state title cannot authorize cleanup");
    reset(); states.append(states[0]); inspect({one}); check(!inspectError.isEmpty(), "duplicate snapshot handles fail closed");
    reset(); inspect({one}); approved = planned; states[0].marked_played = 0; states[0].raw_flags = 0; purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(), "played flag cleared after planning prevents deletion");
    reset(); inspect({one}); approved = planned; inventory[0].title = const_cast<char *>("Recycled handle"); purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(), "changed metadata on same handle prevents deletion");
    reset(); inspect({one}); approved = planned; mappings["https://feed|https://one"] = 0; purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(), "vanished exact source prevents deletion");
    reset(); inspect({one}); approved = planned; aborted = true; purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty() && clearCount == 1, "cancellation between inspect and queued purge remains effective");
    reset(); inspect({one}); approved = planned; w.setSpokenGeneration(2); purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(), "stale generation prevents deletion");
    reset(); changeGenerationDuringFind = true; inspect({one}); check(!inspectError.isEmpty() && planned.isEmpty(), "generation changes during read discard plan");
    reset(); states[1].raw_flags = 0x200; states[1].marked_played = 1; inspect({one, two}); approved = planned;
    inventory[1].filesize++; purge(approved); check(!purgeError.isEmpty() && deleted.isEmpty(), "entire selection validated before first deletion");
    reset(); states[1].raw_flags = 0x200; states[1].marked_played = 1; inspect({one, two}); approved = planned;
    failDeleteAt = 1; purge(approved); check(!purgeError.isEmpty() && purged.size() == 1 && deleted == QList<quint32>{11}, "partial failure reports only confirmed deletions");
    reset(); states[1].raw_flags = 0x200; states[1].marked_played = 1; inspect({one, two}); approved = planned;
    cancelAfterDelete = true; purge(approved); check(!purgeError.isEmpty() && purged.size() == 1 && deleted == QList<quint32>{11}, "cancellation between writes preserves remaining episode");
    reset(); w.doPurgePlayedPodcasts(1, "token", "other-device", approved); check(!purgeError.isEmpty() && deleted.isEmpty(), "different serial cannot delete");
    reset(); QVariantList excessive; for (int i = 0; i < 5001; ++i) excessive.append(one); inspect(excessive);
    check(!inspectError.isEmpty(), "cleanup admission bound enforced");
    reset(); auto malformed = one; malformed["identity"] = QString(); inspect({malformed});
    check(!inspectError.isEmpty() && planned.isEmpty(), "missing source identity fails closed");
    reset(); auto nul = one; nul["sourceUrl"] = QString("https://one") + QChar(0) + "hidden"; inspect({nul});
    check(!inspectError.isEmpty(), "embedded NUL cannot truncate a source identity on the C boundary");
    reset(); w.doInspectPodcastCleanup(1, "", "fixture", {one});
    check(!inspectError.isEmpty(), "missing operation token rejects cleanup");
    reset(); inspect({one}); approved = planned; states.clear(); purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(), "state becoming unavailable between phases retains episode");
    reset(); inspect({one}); approved = planned; approved[0] = QVariantMap{{"itemId", 11u}}; purge(approved);
    check(!purgeError.isEmpty() && deleted.isEmpty(), "bare handle is insufficient authorization for deletion");
    reset(); states[1].raw_flags = 0x200; states[1].marked_played = 1; inspect({one, two}); approved = planned;
    failDeleteAt = 0; purge(approved);
    check(!purgeError.isEmpty() && purged.isEmpty() && deleted.isEmpty(), "first delete failure reports no confirmed removals");
    check(progress.size() == 1 && !progress.first().toMap().value("removed").toBool(),
        "failed deletion never emits a removed progress state");
    QVariantList browsed, browsedBooks; QString browseError;
    QObject::connect(&w, &DeviceWorker::spokenMediaRead, &app,
        [&](quint64, quint64, const QString &, const QVariantList &podcasts, const QVariantList &books, const QString &error) {
            browsed = podcasts; browsedBooks = books; browseError = error;
        });
    reset(); w.doReadSpokenMedia(1, 1, "fixture");
    check(browseError.isEmpty() && browsed.size() == 2 && browsed[0].toMap().value("feedUrl") == "https://feed"
        && browsed[0].toMap().value("sourceUrl") == "https://one", "spoken browser carries exact feed and enclosure identities");
    check(browsed[0].toMap().value("playedKnown").toBool() && browsed[0].toMap().value("played").toBool()
        && browsed[0].toMap().value("bookmarkMs").toUInt() == 40600
        && browsed[1].toMap().value("playedKnown").toBool() && !browsed[1].toMap().value("played").toBool(),
        "spoken browser distinguishes played from known partial progress");
    const auto completeBrowseRow = browsed.first();
    reset(); failState = true; w.doReadSpokenMedia(1, 2, "fixture");
    check(browseError.isEmpty() && browsed.size() == 2 && !browsed[0].toMap().value("playedKnown").toBool(),
        "unavailable firmware state preserves exact device membership without guessing played");
    reset(); states[0].title = const_cast<char *>("Stale title"); w.doReadSpokenMedia(1, 3, "fixture");
    check(browseError.isEmpty() && !browsed[0].toMap().value("playedKnown").toBool(),
        "stale state title cannot display a played badge");
    reset(); states[0].duration_ms++; w.doReadSpokenMedia(1, 4, "fixture");
    check(browseError.isEmpty() && !browsed[0].toMap().value("playedKnown").toBool(),
        "stale state duration cannot display a played badge");
    reset(); failFind = true; w.doReadSpokenMedia(1, 5, "fixture");
    check(browseError.isEmpty() && browsed.size() == 2 && !browsed[0].toMap().value("sourceKnown").toBool()
        && browsed[0].toMap().value("sourceUrl").toString().isEmpty(), "source lookup failure preserves browse rows with unknown membership");
    reset(); inventory[0].series_id = 999; w.doReadSpokenMedia(1, 6, "fixture");
    check(browseError.isEmpty() && browsed.size() == 2 && !browsed[0].toMap().value("sourceKnown").toBool(),
        "source and inventory series mismatch invalidates badge certainty without hiding rows");
    reset(); mappings.remove("https://feed|https://one"); w.doReadSpokenMedia(1, 7, "fixture");
    check(browseError.isEmpty() && browsed.size() == 2 && !browsed[0].toMap().value("sourceKnown").toBool(),
        "incomplete source mapping preserves browse rows without negative membership claims");
    check(deleted.isEmpty(), "all browsing status reads are write-free");
    reset(); failFind = true;
    ZuneAudiobookPart book{}; book.item_id = 21; book.parent_id = 20; book.part_number = 1;
    book.title = const_cast<char *>("Chapter"); book.author = const_cast<char *>("Author");
    book.book_title = const_cast<char *>("Book"); book.duration_ms = 60000; book.filesize = 1000;
    bookInventory.append(book); w.doReadSpokenMedia(1, 8, "fixture");
    check(browseError.isEmpty() && browsedBooks.size() == 1 && browsed.size() == 2,
        "legacy unreadable podcast source cannot hide audiobook inventory");
    w.doPurgeSpokenItems(1, 9, "fixture", "audiobook", browsedBooks);
    check(deleted == QList<quint32>{21}, "unreadable podcast source cannot block confirmed stable-identity audiobook deletion");
    reset(); failFind = true;
    w.doPurgeSpokenItems(1, 10, "fixture", "podcast", {completeBrowseRow});
    check(deleted == QList<quint32>{11} && sourceReads == 0 && stateReads == 0,
        "explicit stable-identity podcast deletion performs no optional source or ZMDB state reads");
    reset(); failFind = true; inventory[0].title = const_cast<char *>("Reused object handle");
    w.doPurgeSpokenItems(1, 11, "fixture", "podcast", {completeBrowseRow});
    check(deleted.isEmpty(), "source failure never weakens original metadata validation before explicit deletion");
    reset(); failInventory = true; w.doReadSpokenMedia(1, 12, "fixture");
    check(!browseError.isEmpty() && browsed.isEmpty() && browsedBooks.isEmpty(),
        "core inventory failure still publishes no partial browse result");
    reset(); inventory.clear(); states.clear(); failFind = true; w.doReadSpokenMedia(1, 13, "fixture");
    check(browseError.isEmpty() && browsed.isEmpty() && sourceReads == 0,
        "complete empty podcast inventory skips optional source reads and establishes empty membership");
    reset(); family = ZUNE_FAMILY_DRACO; w.doReadSpokenMedia(1, 14, "fixture");
    check(browseError.isEmpty() && browsed.size() == 2 && browsedBooks.isEmpty() && bookReads == 1
        && browsed[0].toMap().value("sourceKnown").toBool() && browsed[0].toMap().value("playedKnown").toBool()
        && browsed[0].toMap().value("played").toBool(),
        "Draco podcast browser reads audiobook inventory and exact correlated played state");
    const auto dracoBrowseRow = browsed.first();
    w.doPurgeSpokenItems(1, 15, "fixture", "podcast", {dracoBrowseRow});
    check(deleted == QList<quint32>{11} && bookReads == 2,
        "explicit Draco podcast deletion validates metadata against complete spoken inventory");
    reset(); family = ZUNE_FAMILY_DRACO; bookInventory.append(book);
    w.doReadSpokenMedia(1, 16, "fixture");
    check(browseError.isEmpty() && browsedBooks.size() == 1 && bookReads == 1
        && browsedBooks.first().toMap().value("parentId").toUInt() == 20,
        "Draco audiobook browser returns verified folder and part identity");
    const auto dracoBookRows = browsedBooks;
    w.doPurgeSpokenItems(1, 17, "fixture", "audiobook", dracoBookRows);
    check(spokenPurgeError.isEmpty() && deleted == QList<quint32>{21} && bookReads == 2,
        "explicit Draco audiobook removal revalidates full live metadata");
    reset(); family = ZUNE_FAMILY_DRACO; bookInventory.append(book);
    bookInventory[0].title = const_cast<char *>("Recycled chapter handle");
    w.doPurgeSpokenItems(1, 18, "fixture", "audiobook", dracoBookRows);
    check(!spokenPurgeError.isEmpty() && deleted.isEmpty(),
        "changed Draco audiobook metadata rejects deletion before any write");
    reset(); family = ZUNE_FAMILY_SCORPIUS;
    w.doPurgeSpokenItems(1, 19, "fixture", "audiobook", {QVariantMap{{"itemId", 21u}}});
    check(!spokenPurgeError.isEmpty() && deleted.isEmpty(),
        "unsupported Scorpius audiobook inventory never authorizes deletion");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
