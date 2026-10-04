// Actual controller + temporary LibraryService + recording device. No USB.
#include "VideoTitleRepair.h"
#include "LibraryService.h"
#include "library/LibraryDb.h"

#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#include <memory>

static QStringList qmlWarnings;
static void recordQmlWarning(QtMsgType type, const QMessageLogContext &, const QString &message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        qmlWarnings.append(message);
    fprintf(stderr, "%s\n", qPrintable(message));
}

class RecordingDevice : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected MEMBER connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY stateChanged)
    Q_PROPERTY(bool pulling MEMBER pulling NOTIFY stateChanged)
    Q_PROPERTY(bool purging MEMBER purging NOTIFY stateChanged)
    Q_PROPERTY(QVariantList videosList MEMBER videos NOTIFY stateChanged)
public:
    bool connected = true, busy = false, pulling = false, purging = false;
    QVariantList videos, calls;
    Q_INVOKABLE void renameItem(quint32 id, const QString &title) {
        calls.append(QVariantMap{{"itemId", id}, {"title", title}});
    }
    void complete(int call, bool ok) {
        const auto request = calls[call].toMap();
        const quint32 id = request.value("itemId").toUInt();
        const QString title = request.value("title").toString();
        if (ok) for (auto &entry : videos) {
            auto video = entry.toMap();
            if (video.value("itemId").toUInt() != id) continue;
            video.insert("title", title); video.insert("name", title);
            if (video.value("metagenre").toInt() == 0x26) video.insert("episodeTitle", title);
            entry = video;
        }
        emit stateChanged();
        emit renameFinished(id, title, ok);
    }
signals:
    void stateChanged();
    void renameFinished(quint32 itemId, const QString &newName, bool ok);
};

class RecordingSync : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isSyncing MEMBER isSyncing NOTIFY syncStateChanged)
public:
    bool isSyncing = false;
signals:
    void syncStateChanged();
};

static void settle(int delay = 1) {
    QEventLoop loop;
    QTimer::singleShot(delay, &loop, &QEventLoop::quit);
    loop.exec();
}

static LibVideo localEpisode(QString series, int episode, QString title) {
    LibVideo row;
    row.id = episode;
    row.category = "tv"; row.series = series; row.season = 1; row.episode = episode;
    row.episodeTitle = title; row.tmdbTitle = series; row.tmdbId = 101;
    row.tmdbCached = true;
    row.filename = QStringLiteral("%1.S01E%2.1080p.mkv").arg(series).arg(episode, 2, 10, QLatin1Char('0'));
    row.filepath = "/offline/" + row.filename;
    return row;
}

static LibVideo localMovie(QString title, QString year, int tmdbId) {
    LibVideo row;
    row.id = tmdbId; row.category = "movie"; row.tmdbId = tmdbId; row.tmdbCached = true;
    row.tmdbTitle = title; row.tmdbYear = year;
    row.filename = title + "." + year + ".1080p.BluRay.mkv";
    row.filepath = "/offline/" + row.filename;
    return row;
}

static QVariantMap episodeOnZune(quint32 id, QString series, int episode, QString title = {}) {
    if (title.isEmpty()) title = QStringLiteral("%1 - S01E%2.wmv").arg(series).arg(episode, 2, 10, QLatin1Char('0'));
    return {{"itemId", id}, {"series", series}, {"season", 1}, {"episode", episode},
        {"metagenre", 0x26}, {"kind", "tv show"}, {"title", title}, {"name", title},
        {"filename", QStringLiteral("wire-%1.wmv").arg(id)}, {"sizeMB", 150.0}};
}

static QVariantMap movieOnZune(quint32 id, QString title, QString filename = {}) {
    return {{"itemId", id}, {"metagenre", 0x25}, {"kind", "movie"},
        {"title", title}, {"name", title}, {"filename", filename}, {"sizeMB", 1000.0}};
}

struct Rig {
    RecordingDevice device;
    RecordingSync sync;
    VideoTitleRepair repair;
    int finished = 0, saved = 0, failed = 0, skipped = 0;
    Rig(LibraryService &library, const QVariantList &videos) {
        device.videos = videos;
        repair.setLibrary(&library); repair.setDevice(&device); repair.setSync(&sync);
        QObject::connect(&repair, &VideoTitleRepair::finished, &repair,
            [this](int s, int f, int k) { ++finished; saved = s; failed = f; skipped = k; });
        repair.rebuildPreview();
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ZuunedTitleRepairFixture");
    app.setApplicationName("TitleRepair");
    const QString root = qEnvironmentVariable("TITLE_REPAIR_TEST_ROOT");
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith("/tmp/zuuned-title-repair-") || !data.startsWith(root + '/')) return 2;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *message) {
        ++checks; if (!ok) ++failures;
        printf("  %s %s\n", ok ? "PASS" : "FAIL", message);
    };

    const auto alpha = localEpisode("Alpha Show", 1, "Pilot");
    auto beta = localEpisode("Beta Show", 1, "Another Pilot"); beta.id = 2;
    const auto dune1984 = localMovie("Dune", "1984", 84);
    const auto dune2021 = localMovie("Dune", "2021", 21);
    using Repair = VideoTitleRepair;
    {
        const auto plan = Repair::buildPlan({episodeOnZune(1, "ALPHA.Show", 1),
            episodeOnZune(2, "Beta_Show", 1), episodeOnZune(3, "", 1, "Pilot")}, {alpha, beta});
        check(plan.rows.size() == 2 && plan.rows[0].toMap().value("after") == "Pilot"
              && plan.rows[1].toMap().value("after") == "Another Pilot" && plan.unmatched == 1,
              "episode matches require normalized show plus season/episode; generic titles cannot cross shows");
        auto wrongSeason = episodeOnZune(1, "Alpha Show", 1); wrongSeason["season"] = 2;
        check(Repair::buildPlan({wrongSeason}, {alpha}).unmatched == 1,
              "the same episode number in another season is not a match");
        auto special = alpha; special.season = 0;
        auto missingSeason = episodeOnZune(1, "Alpha Show", 1); missingSeason.remove("season");
        check(Repair::buildPlan({missingSeason}, {special}).unmatched == 1,
              "missing season metadata cannot silently match season-zero specials");
        auto duplicate = alpha; duplicate.id = 77; duplicate.episodeTitle = "Different Cut";
        const auto ambiguous = Repair::buildPlan({episodeOnZune(1, "Alpha Show", 1)}, {alpha, duplicate});
        check(ambiguous.rows.isEmpty() && ambiguous.ambiguous == 1,
              "multiple local episode candidates are left untouched");
        auto missing = alpha; missing.episodeTitle.clear();
        auto automatic = alpha; automatic.tmdbId = 0; automatic.tmdbCached = false;
        check(Repair::buildPlan({episodeOnZune(1, "Alpha Show", 1)}, {missing}).rows.isEmpty()
              && Repair::buildPlan({episodeOnZune(1, "Alpha Show", 1)}, {automatic}).rows.isEmpty(),
              "missing titles and unconfirmed parser identity do not authorize a rename");
        const auto unchanged = Repair::buildPlan({episodeOnZune(1, "Alpha Show", 1, "Pilot")}, {alpha});
        check(unchanged.rows.isEmpty() && unchanged.unchanged == 1, "already-correct device titles are counted separately");
        const auto duplicateHandles = Repair::buildPlan({episodeOnZune(1, "Alpha Show", 1), episodeOnZune(1, "Beta Show", 1)}, {alpha, beta});
        check(duplicateHandles.rows.isEmpty() && duplicateHandles.ambiguous == 2,
              "duplicate device handles never produce competing rename requests");
    }
    {
        const auto ambiguous = Repair::buildPlan({movieOnZune(1, "Dune.wmv")}, {dune1984, dune2021});
        check(ambiguous.rows.isEmpty() && ambiguous.ambiguous == 1,
              "a bare movie title cannot choose between remakes");
        const auto exact = Repair::buildPlan({movieOnZune(1, "Dune.2021.1080p.BluRay.wmv")}, {dune1984, dune2021});
        check(exact.rows.size() == 1 && exact.rows.first().toMap().value("after") == "Dune",
              "a year-qualified legacy release name can identify one movie");
        const auto contradictory = Repair::buildPlan({movieOnZune(1, "Dune.1984.wmv", "Dune.2021.wmv")}, {dune1984, dune2021});
        check(contradictory.rows.isEmpty() && contradictory.ambiguous == 1,
              "contradictory Name and filename metadata is rejected");
        const auto absentRemake = Repair::buildPlan({movieOnZune(1, "Dune.wmv", "Dune.2021.wmv")}, {dune1984});
        check(absentRemake.rows.isEmpty() && absentRemake.unmatched == 1,
              "a conflicting release year blocks repair even when the other remake is not in the library");
        const auto dotted = localMovie("Mr. Nobody", "2009", 9);
        const auto punctuation = Repair::buildPlan({movieOnZune(1, "Mr. Nobody.2009.wmv")}, {dotted});
        check(punctuation.rows.size() == 1 && punctuation.rows.first().toMap().value("after") == "Mr. Nobody",
              "dots within display titles survive filename cleaning");
        const auto slash = Repair::buildPlan({movieOnZune(1, "Off.wmv")}, {localMovie("Face/Off", "1997", 7)});
        const auto slashMatch = Repair::buildPlan({movieOnZune(1, "Face-Off.1997.wmv")}, {localMovie("Face/Off", "1997", 7)});
        check(slash.rows.isEmpty() && slashMatch.rows.size() == 1,
              "a slash inside Face/Off cannot collapse the movie key to Off");
        const auto numberedTitle = Repair::buildPlan({movieOnZune(1, "1917.2019.wmv")}, {localMovie("1917", "2019", 19)});
        check(numberedTitle.rows.size() == 1 && numberedTitle.rows.first().toMap().value("after") == "1917",
              "numbers in a movie title are not misread as contradictory release years");
        auto clip = movieOnZune(1, "Dune.2021.wmv"); clip["metagenre"] = 0x23; clip["kind"] = "music video";
        check(Repair::buildPlan({clip}, {dune2021}).unmatched == 1,
              "music videos and unclassified clips cannot enter the movie repair path");
    }

    QDir().mkpath(data);
    LibraryDb db;
    if (!db.open(data + "/library.db")) return 2;
    const auto episode1 = localEpisode("Sword Art Online Abridged", 1, "The Meat and the Greet");
    const auto episode2 = localEpisode("Sword Art Online Abridged", 2, "A Bridge Too Far");
    const auto movie = localMovie("Skyfall", "2012", 77);
    for (auto row : {episode1, episode2, movie}) {
        if (!db.insertVideo(row)) return 2;
        for (const auto &inserted : db.allVideos()) if (inserted.filepath == row.filepath) row.id = inserted.id;
        if (!db.updateVideoCustomization(row)) return 2;
    }
    LibraryService library;
    const QVariantList original{episodeOnZune(1, episode1.series, 1), episodeOnZune(2, episode2.series, 2)};
    {
        Rig rig(library, original);
        check(rig.repair.candidateCount() == 2 && rig.repair.canApply(),
              "the controller builds a selectable preview from the actual library");
        rig.repair.setSelected(1, false);
        rig.repair.applySelected();
        check(rig.device.calls.size() == 1 && rig.device.calls[0].toMap().value("itemId").toUInt() == 2
              && rig.repair.busy() && rig.finished == 0,
              "only selected rows are sent and success waits for renameFinished");
        emit rig.device.renameFinished(1, "Wrong", true);
        check(rig.repair.busy() && rig.finished == 0, "unrelated rename callbacks cannot complete a pending request");
        rig.device.complete(0, true); settle();
        check(!rig.repair.busy() && rig.finished == 1 && rig.saved == 1 && rig.failed == 0
              && rig.device.videos[1].toMap().value("filename") == original[1].toMap().value("filename"),
              "completion follows the device verdict and only requests a display title change");
    }
    {
        Rig rig(library, original);
        rig.repair.applySelected();
        check(rig.device.calls.size() == 1 && rig.repair.busy(), "batch repair never queues two USB renames at once");
        rig.device.complete(0, false); settle();
        check(rig.device.calls.size() == 2 && rig.repair.busy(), "a failed title reports failure while later selected rows can proceed");
        rig.device.complete(1, true); settle();
        check(rig.finished == 1 && rig.saved == 1 && rig.failed == 1 && !rig.repair.error().isEmpty(),
              "partial success reports exact saved and failed counts");
    }
    {
        Rig rig(library, original);
        rig.device.pulling = true; emit rig.device.stateChanged(); rig.repair.applySelected();
        const bool pullBlocked = !rig.repair.canApply() && rig.device.calls.isEmpty();
        rig.device.pulling = false; rig.device.purging = true; emit rig.device.stateChanged(); rig.repair.applySelected();
        const bool purgeBlocked = !rig.repair.canApply() && rig.device.calls.isEmpty();
        rig.device.purging = false; rig.device.busy = true; emit rig.device.stateChanged(); rig.repair.applySelected();
        const bool busyBlocked = !rig.repair.canApply() && rig.device.calls.isEmpty();
        rig.device.busy = false; rig.sync.isSyncing = true; emit rig.sync.syncStateChanged(); rig.repair.applySelected();
        check(pullBlocked && purgeBlocked && busyBlocked && !rig.repair.canApply() && rig.device.calls.isEmpty(),
              "pull, purge, connection work and sync all block applying a preview");
    }
    {
        Rig rig(library, original);
        rig.repair.applySelected();
        rig.sync.isSyncing = true; emit rig.sync.syncStateChanged();
        rig.device.complete(0, true); settle();
        check(rig.device.calls.size() == 1 && rig.finished == 1 && rig.saved == 1 && rig.skipped == 1,
              "a transfer starting during a rename prevents every subsequent rename");
    }
    {
        Rig rig(library, original);
        rig.repair.applySelected();
        rig.device.connected = false; emit rig.device.stateChanged();
        rig.device.connected = true; emit rig.device.stateChanged();
        rig.repair.applySelected();
        check(rig.device.calls.size() == 1 && rig.finished == 1 && rig.failed == 1 && rig.skipped == 1
              && !rig.repair.canApply(), "disconnect invalidates the plan even if a reconnected device reuses handles");
        rig.repair.rebuildPreview();
        check(rig.repair.canApply(), "a fresh preview is required after reconnecting");
    }
    {
        Rig rig(library, original);
        auto changed = rig.device.videos[0].toMap(); changed["filename"] = "different-object.wmv";
        rig.device.videos[0] = changed;
        rig.repair.applySelected();
        check(rig.device.calls.size() == 1 && rig.device.calls[0].toMap().value("itemId").toUInt() == 2,
              "changed object metadata is rejected immediately before sending its rename");
        rig.device.complete(0, true); settle();
        check(rig.saved == 1 && rig.skipped == 1, "stale rows are reported as skipped, not saved");
    }
    {
        Rig rig(library, original);
        rig.repair.applySelected();
        auto changed = rig.device.videos[1].toMap(); changed["episode"] = 9;
        rig.device.videos[1] = changed;
        rig.device.complete(0, true); settle();
        check(rig.device.calls.size() == 1 && rig.saved == 1 && rig.skipped == 1,
              "each queued row is revalidated after the previous rename finishes");
    }
    {
        Rig rig(library, original);
        qint64 target = -1;
        for (const auto &video : library.videos()) if (video.episode == 1) target = video.id;
        library.setManualVideoIdentity({double(target)}, {{"type", "tv"}, {"title", episode1.series},
            {"season", 1}, {"episode", 1}, {"episodeTitle", "A New Library Title"}});
        rig.repair.applySelected();
        check(rig.device.calls.size() == 1 && rig.device.calls[0].toMap().value("itemId").toUInt() == 2,
              "a library edit after preview cannot silently change the approved destination title");
        rig.device.complete(0, true); settle();
        library.setManualVideoIdentity({double(target)}, {{"type", "tv"}, {"title", episode1.series},
            {"season", 1}, {"episode", 1}, {"episodeTitle", episode1.episodeTitle}});
    }

    // Exercise production QML directly from disk, with explicitly injected
    // services. The real DeviceService singleton is never referenced.
    {
        RecordingDevice device; RecordingSync sync;
        device.videos = original;
        device.videos.append(movieOnZune(3, "Skyfall.2012.1080p.BluRay.x264.wmv"));
        qmlWarnings.clear();
        const auto previousHandler = qInstallMessageHandler(recordQmlWarning);
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1000, 840); window.setColor(QColor("#0a0a0a")); window.show();
        QQmlComponent component(&engine, QUrl::fromLocalFile(qEnvironmentVariable("TITLE_REPAIR_SOURCE")));
        std::unique_ptr<QObject> sheet(component.createWithInitialProperties({
            {"parent", QVariant::fromValue(window.contentItem())},
            {"libraryService", QVariant::fromValue<QObject *>(&library)},
            {"deviceService", QVariant::fromValue<QObject *>(&device)},
            {"syncEngine", QVariant::fromValue<QObject *>(&sync)}}));
        if (!sheet) fprintf(stderr, "%s\n", qPrintable(component.errorString()));
        check(bool(sheet), "production title repair sheet loads with injected native fixture services");
        if (sheet) {
            const bool opened = QMetaObject::invokeMethod(sheet.get(), "openForPreview");
            settle(120);
            const auto *controller = qobject_cast<VideoTitleRepair *>(sheet->property("repair").value<QObject *>());
            check(opened && controller && controller->candidateCount() == 3 && controller->canApply()
                  && device.calls.isEmpty(), "opening the production sheet previews three rows without writing to the device");
            if (app.arguments().contains("--capture")) {
                const QString path = "/tmp/zuuned-video-title-repair.png";
                check(window.grabWindow().save(path), "offscreen native sheet capture saved");
                printf("Capture: %s\n", qPrintable(path));
            }
        }
        check(qmlWarnings.isEmpty(), "the native sheet renders without QML or layout warnings");
        qInstallMessageHandler(previousHandler);
    }
    printf("Video title repair: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

#include "repair-backend.moc"
