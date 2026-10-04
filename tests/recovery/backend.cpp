#include "sync/RecoveryIdentity.h"
#include "sync/SyncEngine.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <cstdio>
#include <limits>

struct RecoveryTestAccess {
    static void pending(SyncEngine &engine, const QString &token) { engine.m_pendingRecoveryToken = token; }
    static void completed(SyncEngine &engine, const QString &token, bool ok) { engine.onInterruptedObjectPurged(token, ok); }
    static void failedSend(SyncEngine &engine, const QVariantMap &record, quint32 id) {
        engine.m_activeRecoveryRecord = record; engine.retainFailedInflight(id);
    }
    static void clear(SyncEngine &engine) { engine.clearInflight(); }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("RecoveryFixture"); app.setApplicationName("Recovery");
    const auto root = qEnvironmentVariable("RECOVERY_TEST_ROOT");
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith("/tmp/zuuned-recovery-backend-") || !dir.startsWith(root + '/')) return 2;
    QDir().mkpath(dir);
    const auto path = dir + "/inflight-send";
    int count = 0, failed = 0;
    auto check = [&](bool ok, const char *name) { ++count; if (!ok) ++failed;
        printf("%s %s\n", ok ? "PASS" : "FAIL", name); };
    QVariantMap identity{{"title", "Intro"}, {"artist", "Artist"}, {"album", "Album"}, {"discNumber", 2}, {"trackNumber", 1}};
    QVariantMap marker{{"version", 1}, {"type", "music"}, {"token", "request"}, {"serial", "device-A"},
        {"itemId", 20}, {"baselineIds", QVariantList{10, 11}}, {"identity", identity}};
    auto actual = identity; actual.insert("itemId", 20);
    check(RecoveryIdentity::refusal(marker, "device-A").isEmpty(), "exact newly created ID on original device is eligible");
    check(RecoveryIdentity::matches(marker, actual), "exact ID and full music identity match");
    check(!RecoveryIdentity::refusal(marker, "device-B").isEmpty(), "wrong device refuses deletion");
    check(!RecoveryIdentity::refusal(marker, "").isEmpty(), "missing current serial refuses deletion");
    auto changed = marker; changed.insert("serial", "");
    check(!RecoveryIdentity::refusal(changed, "device-A").isEmpty(), "missing saved serial refuses deletion");
    changed = marker; changed.insert("itemId", 10);
    check(!RecoveryIdentity::refusal(changed, "device-A").isEmpty(), "pre-existing object never becomes recovery target");
    for (const auto &invalid : QVariantList{0, -1, 1.5, std::numeric_limits<double>::quiet_NaN(), 4294967296.0}) {
        changed = marker; changed.insert("itemId", invalid);
        check(!RecoveryIdentity::refusal(changed, "device-A").isEmpty(), "invalid or absent exact ID refuses deletion");
    }
    changed = marker; changed.remove("baselineIds");
    check(!RecoveryIdentity::refusal(changed, "device-A").isEmpty(), "missing baseline is unsafe");
    changed = marker; changed.insert("baselineIds", QVariantList{10, "invalid"});
    check(!RecoveryIdentity::refusal(changed, "device-A").isEmpty(), "malformed baseline is unsafe");
    auto other = actual; other.insert("itemId", 21);
    check(!RecoveryIdentity::matches(marker, other), "same metadata never selects a second object");
    other = actual; other.insert("discNumber", 1);
    check(!RecoveryIdentity::matches(marker, other), "conflicting known disc refuses deletion");
    other = actual; other.insert("artist", "Other");
    check(!RecoveryIdentity::matches(marker, other), "reused ID with different metadata refuses deletion");
    check(!RecoveryIdentity::matches(marker, {{"itemId", 20}, {"filename", "Intro.jpg"}}), "music marker never selects photo by title stem");
    auto photo = marker; photo.insert("type", "photo"); photo.insert("identity", QVariantMap{{"filename", "Intro.jpg"}});
    check(RecoveryIdentity::matches(photo, {{"itemId", 20}, {"filename", "Intro.jpg"}}), "photo recovery requires exact ID and filename");
    check(!RecoveryIdentity::matches(photo, {{"itemId", 20}, {"filename", "Intro.png"}}), "photo filename extension matters");
    for (const auto &bytes : QList<QByteArray>{"Intro", "Intro.jpg", QJsonDocument::fromVariant(marker).toJson()}) {
        QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 2; file.close();
        SyncEngine engine;
        check(!engine.interruptedSend().isEmpty(), "startup recognizes pending marker");
        check(engine.purgeInterruptedSend() == 0 && QFile::exists(path), "disconnected refusal preserves durable marker");
        engine.startSync();
        check(!engine.isSyncing() && QFile::exists(path), "pending recovery blocks a new sync from overwriting evidence");
        engine.dismissInterruptedSend();
        check(engine.interruptedSend().isEmpty() && !QFile::exists(path), "explicit dismissal removes marker");
    }
    {
        SyncEngine engine;
        RecoveryTestAccess::failedSend(engine, marker, 20);
        check(QFile::exists(path) && !engine.interruptedSend().isEmpty(), "failed send retains durable marker with returned ID");
        RecoveryTestAccess::clear(engine);
        check(QFile::exists(path), "finish-sync clearing cannot erase unresolved failed marker");
        RecoveryTestAccess::pending(engine, "request");
        engine.dismissInterruptedSend();
        check(QFile::exists(path), "dismiss cannot discard an active deletion request");
        RecoveryTestAccess::completed(engine, "unrelated", true);
        check(QFile::exists(path), "unrelated purge success cannot clear marker");
        RecoveryTestAccess::completed(engine, "request", false);
        check(QFile::exists(path) && !engine.interruptedRecoveryStatus().isEmpty(), "failed exact deletion retains marker and actionable status");
        RecoveryTestAccess::pending(engine, "request");
        RecoveryTestAccess::completed(engine, "request", true);
        check(!QFile::exists(path) && engine.interruptedSend().isEmpty(), "only correlated confirmed deletion clears marker");
    }
    printf("Recovery: %d checks, %d failures; no device service or USB constructed\n", count, failed);
    return failed ? 1 : 0;
}
