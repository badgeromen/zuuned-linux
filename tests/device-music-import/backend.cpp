#include "TrackImportBatch.h"
#include "TrackImportPath.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int failures = 0;

void check(bool condition, const char *message) {
    if (condition) {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    TrackImportBatch album;
    album.started(11, QStringLiteral("Delamain"), QStringLiteral("Radio"),
                  QStringLiteral("Various Artists"));
    album.started(12, QStringLiteral("delamain"), QStringLiteral("Radio"),
                  QStringLiteral("various artists"));
    album.started(13, QStringLiteral("Lizzy Wizzy"), QStringLiteral("Laments"));
    check(!album.finished(12, true).has_value(),
          "album import waits for every queued track");
    check(!album.finished(11, true).has_value(),
          "album import does not rescan early");
    const auto complete = album.finished(13, true);
    check(complete.has_value() && complete->hasSuccessfulTrack,
          "successful album import completes exactly once");
    check(complete && complete->artists == QStringList{
              QStringLiteral("Delamain"), QStringLiteral("Lizzy Wizzy")},
          "artist matching is deduplicated case-insensitively");
    check(complete && complete->albums.size() == 2,
          "successful album artwork requests are deduplicated by owner and title");
    check(complete && complete->albums[0].artist.compare(
              QStringLiteral("Lizzy Wizzy"), Qt::CaseInsensitive) == 0
              && complete->albums[0].title == QStringLiteral("Laments")
              && complete->albums[1].artist.compare(
                  QStringLiteral("Various Artists"), Qt::CaseInsensitive) == 0
              && complete->albums[1].title == QStringLiteral("Radio"),
          "device album owner is retained for local artwork cache identity");
    check(album.pending() == 0, "completed album leaves no pending imports");

    TrackImportBatch failed;
    failed.started(21, QStringLiteral("Nobody"));
    const auto failure = failed.finished(21, false);
    check(failure.has_value() && !failure->hasSuccessfulTrack
              && failure->artists.isEmpty(),
          "failed extracts do not trigger a scan or artist request");
    check(!failed.finished(999, true).has_value(),
          "unknown worker completions cannot corrupt the batch");

    QTemporaryDir importDir;
    check(importDir.isValid(), "temporary import directory is available");
    const QString requested = importDir.filePath(QStringLiteral("Same title.mp3"));
    QSet<QString> reserved;
    const QString first = TrackImportPath::available(requested, reserved);
    check(first == requested, "first queued title keeps its requested filename");
    reserved.insert(TrackImportPath::key(first));
    const QString second = TrackImportPath::available(requested, reserved);
    check(second.endsWith(QStringLiteral("Same title (2).mp3")),
          "simultaneously queued duplicate title receives a unique filename");
    QFile existingFirst(first);
    QFile existingSecond(second);
    const bool fixturesCreated = existingFirst.open(QIODevice::WriteOnly)
        && existingFirst.write("track") == 5
        && existingSecond.open(QIODevice::WriteOnly)
        && existingSecond.write("track") == 5;
    check(fixturesCreated, "existing duplicate fixtures are created");
    existingFirst.close();
    existingSecond.close();
    reserved.clear();
    const QString third = TrackImportPath::available(requested, reserved);
    check(third.endsWith(QStringLiteral("Same title (3).mp3")),
          "existing imports are never overwritten by a later import");
    return failures == 0 ? 0 : 1;
}
