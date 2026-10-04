#include "LibraryService.h"
#include "library/LibraryDb.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <cstdio>
#include <memory>

static LibTrack track(QString title) {
    LibTrack row; row.filepath = "/offline/" + title + ".flac";
    row.title = title; row.artist = "Artist"; row.album = "Album";
    return row;
}

static QVariantMap deviceTrack(QString title, quint32 id) {
    return {{"itemId", id}, {"title", title}, {"artist", "Artist"}, {"album", "Album"}};
}

static QStringList titles(const QVariantList &rows) {
    QStringList result; for (const auto &row : rows) result.append(row.toMap().value("title").toString()); return result;
}

static QVariantList entryIds(const QVariantList &rows) {
    QVariantList result; for (const auto &row : rows) result.append(row.toMap().value("playlistEntryId")); return result;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZuunedPlaylistOrderFixture"); app.setApplicationName("Order");
    const QString root = qEnvironmentVariable("PLAYLIST_ORDER_TEST_ROOT");
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith("/tmp/zuuned-playlist-order-backend-") || !data.startsWith(root + '/')) return 2;
    QDir().mkpath(data);
    LibraryDb db;
    if (!db.open(data + "/library.db")) return 2;
    for (const auto *title : {"A", "C", "E"}) if (!db.upsertTrack(track(title))) return 2;
    QHash<QString, qint64> ids;
    for (const auto &row : db.allTracks()) ids.insert(row.title, row.id);
    const double legacyId = double(db.createPlaylist("Legacy", {ids["A"]}));
    QVariantList oldNotes;
    for (const auto *title : {"B", "D", "B"})
        oldNotes.append(QVariantMap{{"pl", legacyId}, {"artist", "Artist"}, {"album", "Album"}, {"title", title}});
    QSettings().setValue("pendingPlaylistAdds", oldNotes);
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char *label) { ++checks; if (!ok) ++failed;
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label); };
    double importedId = -1;
    QVariantList initialEntries;
    {
        LibraryService library;
        check(library.pendingPlaylistAddCount(legacyId) == 3
              && !QSettings().contains("pendingPlaylistAdds") && db.playlistEntries(qint64(legacyId)).size() == 4,
              "LibraryService migrates legacy notes into durable slots before clearing QSettings");
        const auto imported = library.importDevicePlaylist("Device sequence", {
            deviceTrack("A", 10), deviceTrack("B", 20), deviceTrack("C", 30),
            deviceTrack("D", 40), deviceTrack("B", 20), deviceTrack("A", 10)});
        importedId = imported.value("id").toDouble();
        check(imported.value("success").toBool() && imported.value("here").toInt() == 3
              && imported.value("pending").toInt() == 3 && imported.value("itemsToPull").toList().size() == 2,
              "batch import preserves repeated occurrences but requests each missing device file only once");
        check(titles(library.playlistTracks(importedId)) == QStringList{"A", "C", "A"}
              && library.pendingPlaylistAddCount(importedId) == 3,
              "already-owned repeated tracks appear immediately in their original relative order");
        for (const auto &entry : db.playlistEntries(qint64(importedId))) initialEntries.append(double(entry.id));
        if (!db.upsertTrack(track("D"))) return 2;
        const auto edited = library.editTrackMetadata(double(ids["A"]), {{"genre", "Reload after D"}});
        check(edited.value("success").toBool()
              && titles(library.playlistTracks(importedId)) == QStringList{"A", "C", "D", "A"}
              && library.pendingPlaylistAddCount(importedId) == 2,
              "a real library refresh resolves a later arrival in place instead of appending it");
    }
    {
        LibraryService library;
        QVariantList reopenedEntries;
        for (const auto &entry : db.playlistEntries(qint64(importedId))) reopenedEntries.append(double(entry.id));
        check(reopenedEntries == initialEntries && library.pendingPlaylistAddCount(importedId) == 2,
              "service restart preserves every unresolved occurrence and its stable entry ID");
        const QVariantList editorRows = library.playlistTracks(importedId);
        const QVariantList editorBaseline = entryIds(editorRows);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("fixtureLibrary", &library);
        engine.rootContext()->setContextProperty("fixturePlaylistId", importedId);
        QQmlComponent component(&engine);
        component.setData(R"qml(import QtQml
            QtObject {
                property int pending: {
                    void fixtureLibrary.playlists
                    return fixtureLibrary.pendingPlaylistAddCount(fixturePlaylistId)
                }
            })qml", QUrl());
        std::unique_ptr<QObject> binding(component.create());
        if (!binding) { fprintf(stderr, "%s\n", qPrintable(component.errorString())); return 2; }
        check(binding->property("pending").toInt() == 2, "QML observes the durable pending count through playlist notifications");
        if (!db.upsertTrack(track("B"))) return 2;
        library.editTrackMetadata(double(ids["A"]), {{"genre", "Reload after B"}});
        check(titles(library.playlistTracks(importedId)) == QStringList{"A", "B", "C", "D", "B", "A"}
              && binding->property("pending").toInt() == 0,
              "an earlier late arrival restores the full original sequence, repeats and reactive counts");
        check(titles(library.playlistTracks(legacyId)) == QStringList{"A", "B", "D", "B"},
              "legacy pending records resolve in their stored order rather than backwards");
        const bool saved = library.editPlaylistEntries(importedId, "Edited while downloading",
            {editorRows[3], editorRows[1], editorRows[2], QVariantMap{{"id", double(ids["E"])}}}, editorBaseline);
        const QVariantList changed = library.playlistTracks(importedId);
        check(saved && titles(changed) == QStringList{"B", "A", "C", "B", "D", "E"}
              && changed[1].toMap().value("playlistEntryId") == editorRows[3].toMap().value("playlistEntryId"),
              "editor save preserves unseen arrivals while applying exact repeat removal, reorder and append");
        const auto missing = library.importDevicePlaylist("Delete pending", {deviceTrack("Future", 99)});
        const double deleted = missing.value("id").toDouble();
        library.deletePlaylist(deleted);
        if (!db.upsertTrack(track("Future"))) return 2;
        library.editTrackMetadata(double(ids["A"]), {{"genre", "Reload after deletion"}});
        bool exists = false;
        for (const auto &row : library.playlists()) if (row.toMap().value("id").toDouble() == deleted) exists = true;
        check(!exists && library.pendingPlaylistAddCount(deleted) == 0 && library.playlistTracks(deleted).isEmpty(),
              "a later scan cannot resurrect a playlist deleted while its tracks were pending");
        const int count = library.playlists().size();
        const auto invalid = library.importDevicePlaylist("", {deviceTrack("A", 10)});
        check(!invalid.value("success").toBool() && !invalid.value("error").toString().isEmpty()
              && library.playlists().size() == count, "a failed batch import reports an error without a partial playlist");
        const QString m3uPath = root + "/Repeated.m3u8";
        QFile file(m3uPath);
        if (!file.open(QIODevice::WriteOnly) || file.write("#EXTM3U\n/offline/A.flac\n/offline/C.flac\n/offline/A.flac\n") < 0) return 2;
        file.close();
        const auto m3u = library.importPlaylistM3U(m3uPath);
        bool repeats = false;
        for (const auto &row : library.playlists()) if (row.toMap().value("name").toString() == "Repeated")
            repeats = titles(library.playlistTracks(row.toMap().value("id").toDouble())) == QStringList{"A", "C", "A"};
        check(m3u.value("added").toInt() == 3 && repeats, "M3U import also retains intentional repeated tracks");
    }
    QSettings().setValue("pendingPlaylistAdds", oldNotes); // crash before old settings cleanup
    {
        LibraryService library;
        check(!QSettings().contains("pendingPlaylistAdds")
              && titles(library.playlistTracks(legacyId)) == QStringList{"A", "B", "D", "B"},
              "restart after committed migration cannot import the same legacy notes twice");
    }
    printf("Playlist order integration: %d checks, %d failures\n", checks, failed);
    return failed ? 1 : 0;
}
