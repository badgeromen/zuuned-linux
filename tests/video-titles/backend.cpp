// Exercise the production title/filename boundary without constructing a device
// service, opening a library, or entering the transfer pipeline.
#include "DeviceWorker.h"
#include "library/VideoBrowserModel.h"
#include "library/VideoIdentity.h"
#include "sync/SyncEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

static QMetaMethod methodNamed(const QMetaObject &object, const char *name) {
    for (int i = object.methodOffset(); i < object.methodCount(); ++i) {
        const QMetaMethod method = object.method(i);
        if (method.name() == name) return method;
    }
    return {};
}

static QVariantMap episode(const QString &series, int season, int number,
                           const QString &title = QStringLiteral("Pilot")) {
    return {{"name", title}, {"title", title}, {"metagenre", 0x26},
            {"series", series}, {"season", season}, {"episode", number},
            {"episodeTitle", title}, {"format", 0xB981},
            {"filename", SyncEngine::videoWireName(title, series, season, number, 0)}};
}

static bool writeFixture(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

static QByteArray readFixture(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ZuunedVideoTitleFixture"));
    app.setApplicationName(QStringLiteral("VideoTitles"));
    const QString root = qEnvironmentVariable("VIDEO_TITLE_TEST_ROOT");
    if (!root.startsWith(QLatin1String("/tmp/zuuned-video-titles-"))
        || !QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                .startsWith(root + QLatin1Char('/'))) return 2;

    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *label) {
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
        ok ? ++passed : ++failed;
    };
    const QString title = QStringLiteral("Dr. Strange: Who's There? / Part <II> | Épisode");
    check(SyncEngine::videoDisplayTitle(QStringLiteral("  ") + title + QStringLiteral("  "),
                                      QStringLiteral("Show A"), 1, 1) == title,
          "display title preserves punctuation, accents, and case while trimming whitespace");
    check(SyncEngine::videoDisplayTitle(QStringLiteral("Pilot"), QStringLiteral("Show A"), 1, 1)
              == QStringLiteral("Pilot"),
          "episode display title contains no filename or series prefix");
    check(SyncEngine::videoDisplayTitle(QStringLiteral("Dr. Strangelove"), {}, 0, 0)
              == QStringLiteral("Dr. Strangelove"),
          "period in a movie title is not treated as a file extension");
    check(SyncEngine::videoDisplayTitle({}, QStringLiteral("Show A"), 2, 3)
              == QStringLiteral("Episode 3"),
          "missing episode title has a readable fallback");
    check(SyncEngine::videoDisplayTitle(QStringLiteral(" s02E003 "), QStringLiteral("Show A"), 2, 3)
              == QStringLiteral("Episode 3"),
          "legacy episode-number label has a readable fallback");
    check(SyncEngine::videoDisplayTitle(QStringLiteral("S02E03"), {}, 0, 0)
              == QStringLiteral("S02E03"),
          "an explicit non-series title is preserved even when it resembles an episode number");
    check(SyncEngine::videoDisplayTitle(QStringLiteral(" \t "), {}, 0, 0)
              == QStringLiteral("Untitled video")
          && SyncEngine::videoDisplayTitle({}, {}, 0, 3) == QStringLiteral("Untitled video"),
          "missing identity does not manufacture a series episode");

    check(VideoIdentity::fileStem(QStringLiteral("Dr. Strangelove.WMV"))
              == QStringLiteral("Dr. Strangelove"),
          "known media extension is removed case-insensitively");
    check(VideoIdentity::fileStem(QStringLiteral("Dr. Strangelove"))
              == QStringLiteral("Dr. Strangelove")
          && VideoIdentity::fileStem(QStringLiteral("Story.Part II")) == QStringLiteral("Story.Part II"),
          "arbitrary dots and unknown suffixes survive filename cleanup");

    const QRegularExpression illegal(QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}]"));
    const QString unsafe = title + QChar(1) + QStringLiteral("*\\\"");
    const QString wire = SyncEngine::videoWireName(unsafe, QStringLiteral("Show: A/B"), 2, 3, 0);
    check(!illegal.match(wire).hasMatch() && wire.endsWith(QStringLiteral(".wmv"))
          && wire.contains(QStringLiteral("S02E03")),
          "TV object filename has episode identity, playable extension, and no FAT32-illegal characters");
    check(SyncEngine::videoDisplayTitle(unsafe, QStringLiteral("Show: A/B"), 2, 3) == unsafe,
          "filename sanitization does not alter the independently supplied display title");
    check(SyncEngine::videoWireName(QStringLiteral("Pilot"), QStringLiteral("Show A"), 1, 1, 0)
              == QStringLiteral("Show A - S01E01 - Pilot.wmv")
          && SyncEngine::videoWireName(QStringLiteral("Pilot"), QStringLiteral("Show A"), 1, 1, 1)
              == QStringLiteral("Show A - S01E01 - Pilot.mp4"),
          "wire filename extension follows the selected transfer profile");
    check(SyncEngine::videoWireName(QStringLiteral("S01E01"), QStringLiteral("Show A"), 1, 1, 0)
              == QStringLiteral("Show A - S01E01.wmv"),
          "legacy episode label is not duplicated in the object filename");
    check(SyncEngine::videoWireName(QStringLiteral("  ...  "), {}, 0, 0, 0)
              == QStringLiteral("video.wmv"),
          "empty sanitized object name retains a usable basename and extension");

    const QVariantMap showA = episode(QStringLiteral("Show A"), 1, 1);
    const QVariantMap showB = episode(QStringLiteral("Show B"), 1, 1);
    const QString keyA = VideoBrowserModel::episodeKey(QStringLiteral("Show A"), 1, 1);
    const QString keyB = VideoBrowserModel::episodeKey(QStringLiteral("Show B"), 1, 1);
    const auto keysA = VideoBrowserModel::deviceVideoKeys({showA});
    check(keysA.contains(keyA) && !keysA.contains(keyB) && !keysA.contains(QStringLiteral("pilot")),
          "same Pilot title on another show cannot collide with the on-device episode key");
    check(!keysA.contains(VideoBrowserModel::episodeKey(QStringLiteral("Show A"), 2, 1))
          && !keysA.contains(VideoBrowserModel::episodeKey(QStringLiteral("Show A"), 1, 2)),
          "season and episode number both distinguish on-device identity");
    check(VideoBrowserModel::deviceVideoKeys({showA, showB}) == QSet<QString>{keyA, keyB},
          "two shows named Pilot produce exactly two distinct episode keys");
    QVariantMap vendorOnly = showA;
    vendorOnly.remove(QStringLiteral("filename"));
    check(VideoBrowserModel::deviceVideoKeys({vendorOnly}) == QSet<QString>{keyA},
          "vendor identity alone works with a clean device display title");
    QVariantMap legacy{{"name", QStringLiteral("Pilot")},
                       {"filename", showA.value(QStringLiteral("filename"))}};
    check(VideoBrowserModel::deviceVideoKeys({legacy}) == QSet<QString>{keyA},
          "legacy object filename recovers exact episode identity without a title-only key");
    legacy = {{"name", showA.value(QStringLiteral("filename"))}};
    check(VideoBrowserModel::deviceVideoKeys({legacy}) == QSet<QString>{keyA},
          "legacy filename stored in Name still recovers the correct episode");
    check(VideoBrowserModel::deviceVideoKeys({QVariantMap{{"name", "Pilot"}, {"metagenre", 0x26}}}).isEmpty(),
          "an unidentifiable TV Pilot is not guessed to belong to every show");
    const auto movieKeys = VideoBrowserModel::deviceVideoKeys({
        QVariantMap{{"name", "Pilot"}, {"metagenre", 0x25}}});
    check(movieKeys.contains(QStringLiteral("pilot")) && !movieKeys.contains(keyA),
          "movie title matching remains separate from an episode key");

    check(VideoIdentity::matchesInterruptedFile(showA,
              showA.value(QStringLiteral("filename")).toString().toUpper()),
          "interrupted-send lookup uses the exact filename case-insensitively");
    check(!VideoIdentity::matchesInterruptedFile(showA,
              showB.value(QStringLiteral("filename")).toString())
          && !VideoIdentity::matchesInterruptedFile(showA, QStringLiteral("Pilot")),
          "matching clean titles cannot select another show's interrupted object");
    check(!VideoIdentity::matchesInterruptedFile(vendorOnly, QStringLiteral("Pilot"))
          && !VideoIdentity::matchesInterruptedFile(showA, {}),
          "missing filename never falls back to a destructive title match");

    check(VideoIdentity::importFilename(showA) == showA.value(QStringLiteral("filename")).toString(),
          "save-to-library keeps the actual object filename and extension");
    QVariantMap importB = showB;
    importB.remove(QStringLiteral("filename"));
    check(VideoIdentity::importFilename(vendorOnly) == QStringLiteral("Show A - S01E01 - Pilot.wmv")
          && VideoIdentity::importFilename(importB) == QStringLiteral("Show B - S01E01 - Pilot.wmv"),
          "filename-less episodes with the same title import to different files");
    check(VideoIdentity::importFilename({{"name", "Dr. Strangelove"}, {"format", 0x300D}})
              == QStringLiteral("Dr. Strangelove.wmv")
          && VideoIdentity::importFilename({{"name", "Dr. Strangelove"}, {"format", 0xB982}})
              == QStringLiteral("Dr. Strangelove.mp4"),
          "fallback import names retain title punctuation and use the device object format");
    const QString imported = VideoIdentity::importFilename({{"name", unsafe}, {"format", 0xB981}});
    check(!illegal.match(imported).hasMatch() && imported.endsWith(QStringLiteral(".wmv")),
          "fallback import filename is safe independently of its display title");

    const QMetaMethod send = methodNamed(SyncEngine::staticMetaObject, "workerSendVideo");
    const QMetaMethod receive = methodNamed(DeviceWorker::staticMetaObject, "doSendVideo");
    const QList<QByteArray> expectedNames{"entryId", "filepath", "objectFilename", "title",
        "metagenre", "description", "posterPath", "series", "season", "episode"};
    check(send.isValid() && send.methodType() == QMetaMethod::Signal
          && send.parameterNames() == expectedNames,
          "compiled sync signal carries distinct object filename and display-title parameters");
    check(receive.isValid() && send.parameterTypes() == receive.parameterTypes()
          && receive.parameterNames() == expectedNames,
          "compiled device-worker slot preserves the same filename/title boundary");

    // SyncEngine alone owns only timers and the isolated queue files. No device
    // is attached, so these startup cases cannot schedule a hardware operation.
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(data)) return 2;
    const QString markerPath = data + QStringLiteral("/inflight-send");
    const QString objectFilename = showA.value(QStringLiteral("filename")).toString();
    const QByteArray typedMarker = QJsonDocument(QJsonObject{
        {"type", "video"}, {"filename", objectFilename}, {"title", title}}).toJson();
    if (!writeFixture(markerPath, typedMarker)) return 2;
    {
        SyncEngine engine;
        check(engine.interruptedSend() == title,
              "typed video marker exposes the clean title instead of JSON or the wire filename");
        check(engine.purgeInterruptedSend() == 0 && readFixture(markerPath) == typedMarker,
              "unavailable device leaves typed recovery evidence intact");
        engine.dismissInterruptedSend();
        check(engine.interruptedSend().isEmpty() && !QFile::exists(markerPath),
              "explicit dismissal clears both the recovery label and its marker");
    }
    if (!writeFixture(markerPath, objectFilename.toUtf8())) return 2;
    {
        SyncEngine engine;
        check(engine.interruptedSend() == objectFilename,
              "legacy plain-text video filename is still recognized on startup");
        check(engine.purgeInterruptedSend() == 0 && QFile::exists(markerPath),
              "legacy recovery marker also survives an offline recovery request");
        engine.dismissInterruptedSend();
    }
    if (!writeFixture(markerPath, QJsonDocument(QJsonObject{
        {"type", "video"}, {"filename", objectFilename}, {"title", ""}}).toJson())) return 2;
    {
        SyncEngine engine;
        check(engine.interruptedSend() == objectFilename,
              "typed marker with no display title falls back to its usable filename");
        engine.dismissInterruptedSend();
    }

    const QString queuePath = data + QStringLiteral("/syncqueue.json");
    const QString availablePath = root + QStringLiteral("/available.mkv");
    if (!writeFixture(availablePath, QByteArrayLiteral("isolated file-existence fixture"))) return 2;
    const QString warning = QStringLiteral("uploaded; title readback could not be verified");
    {
        SyncEngine engine;
        SyncQueueEntry sent;
        sent.entryId = QStringLiteral("uploaded-warning");
        sent.type = QStringLiteral("video");
        sent.filepath = root + QStringLiteral("/source-no-longer-present.mkv");
        sent.title = QStringLiteral("Pilot");
        sent.videoSeries = QStringLiteral("Show A");
        sent.videoSeason = 1; sent.videoEpisode = 1;
        sent.status = QStringLiteral("sent"); sent.statusNote = warning;
        sent.estimatedBytes = 1000;
        engine.queue()->restoreEntry(sent);
        SyncQueueEntry pending = sent;
        pending.entryId = QStringLiteral("next-upload");
        pending.filepath = availablePath;
        pending.status = QStringLiteral("pending"); pending.statusNote.clear();
        pending.estimatedBytes = 250;
        engine.queue()->restoreEntry(pending);
        check(engine.queue()->pendingEstimatedBytes() == 250,
              "uploaded warning is excluded from the next transfer's byte estimate");
        QEventLoop debounce;
        QTimer::singleShot(1100, &debounce, &QEventLoop::quit);
        debounce.exec();
        const QJsonArray persisted = QJsonDocument::fromJson(readFixture(queuePath))
                                        .object().value(QStringLiteral("entries")).toArray();
        QJsonObject warningRow;
        for (const QJsonValue &entry : persisted)
            if (entry.toObject().value(QStringLiteral("entryId")).toString() == sent.entryId)
                warningRow = entry.toObject();
        check(persisted.size() == 2 && warningRow.value(QStringLiteral("status")).toString() == "sent"
              && warningRow.value(QStringLiteral("statusNote")).toString() == warning,
              "normal debounced persistence records uploaded status and its warning text");
    }
    {
        SyncEngine engine;
        const auto entries = engine.queue()->entries();
        SyncQueueEntry restored;
        for (const auto &entry : entries)
            if (entry.entryId == QLatin1String("uploaded-warning")) restored = entry;
        check(entries.size() == 2 && restored.status == QLatin1String("sent")
              && restored.statusNote == warning && !QFile::exists(restored.filepath),
              "restart preserves uploaded warning even after its original source file disappears");
        check(engine.queue()->pendingEstimatedBytes() == 250
              && restored.status != QLatin1String("pending") && restored.status != QLatin1String("failed"),
              "restored warning stays outside the pending/failed reupload state and estimate");
        engine.queue()->removeCompleted();
        check(engine.queue()->count() == 2,
              "completion cleanup retains the uploaded warning for review");
    }

    printf("Video title regressions: %d passed, %d failed. No device service instantiated.\n", passed, failed);
    return failed ? 1 : 0;
}
