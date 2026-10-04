#include "LibraryService.h"
#include "library/LibraryDb.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <limits>

static QVariantList ids(const QVariantList &rows) {
    QVariantList out;
    for (const auto &row : rows) out.append(row.toMap().value("id"));
    return out;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZuunedPhotoAlbumFixture");
    app.setApplicationName("Albums");
    const QString root = qEnvironmentVariable("PHOTO_ALBUMS_TEST_ROOT");
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith("/tmp/zuuned-photo-albums-backend-") || !data.startsWith(root + '/')) return 2;
    QDir().mkpath(data);
    LibraryDb db;
    if (!db.open(data + "/library.db")) return 2;
    for (const auto *name : {"A.jpg", "B.jpg", "C.jpg", "D.jpg", "E.jpg"}) {
        LibPhoto photo;
        photo.filename = name;
        photo.filepath = root + '/' + name;
        photo.filesize = 7;
        QFile file(photo.filepath);
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7) return 2;
        file.close();
        if (!db.upsertPhoto(photo)) return 2;
    }
    QHash<QString, double> photos;
    for (const auto &photo : db.allPhotos()) photos.insert(photo.filename, double(photo.id));
    const double a = photos["A.jpg"], b = photos["B.jpg"], c = photos["C.jpg"];
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *label) {
        ++checks; if (!ok) ++failures;
        printf("  %s %s\n", ok ? "PASS" : "FAIL", label);
    };
    double retained = 0;
    {
        LibraryService library;
        int notifications = 0;
        QObject::connect(&library, &LibraryService::libraryChanged, [&] { ++notifications; });
        auto mutation = [&](const auto &operation, bool expected, const char *label) {
            const int before = notifications;
            const auto result = operation();
            check(result.value("success").toBool() == expected
                  && notifications == before + (expected ? 1 : 0)
                  && (expected ? result.value("error").toString().isEmpty()
                               : !result.value("error").toString().isEmpty()), label);
            return result;
        };
        auto create = [&](const QString &name, double parent) {
            return mutation([&] { return library.createPhotoAlbum(name, parent); }, true,
                            "offline album creation reports success and one refresh").value("id").toDouble();
        };
        mutation([&] { return library.createPhotoAlbum("Failed initial", 0, {a, 999999}); }, false,
                 "missing initial photo rolls back creation without refresh");
        check(library.customPhotoAlbums().isEmpty(), "failed create-with-members leaves no empty album");
        const double initial = mutation([&] { return library.createPhotoAlbum("Initial", 0, {b, a}); }, true,
                                        "create-with-members commits once and emits one refresh").value("id").toDouble();
        check(ids(library.customAlbumPhotos(initial)) == QVariantList{b, a}, "initial membership preserves selection order");
        check(library.customPhotoAlbums({"a"}).first().toMap().value("onZune").toInt() == 1,
              "custom album on-device count uses direct matching photo stems");
        mutation([&] { return library.deletePhotoAlbum(initial); }, true, "initial fixture album deletes without deleting photos");
        const double parent = create("Trip", 0);
        const double child = create("Beach", parent);
        const double grandchild = create("Sunset", child);
        retained = create("Keep", 0);
        auto album = [&](double id) {
            for (const auto &row : library.customPhotoAlbums())
                if (row.toMap().value("id").toDouble() == id) return row.toMap();
            return QVariantMap{};
        };
        check(album(grandchild).value("pathLabel").toString() == "Trip / Beach / Sunset",
              "nested album labels include every ancestor");
        mutation([&] { return library.addPhotosToAlbum(child, {c, a, b, photos["D.jpg"], photos["E.jpg"]}); }, true,
                 "membership add completes offline and refreshes views");
        check(ids(library.customAlbumPhotos(child)) == QVariantList{c, a, b, photos["D.jpg"], photos["E.jpg"]}
              && album(child).value("count").toInt() == 5
              && album(parent).value("count").toInt() == 0 && library.customAlbumPhotos(parent).isEmpty(),
              "direct membership preserves chosen order and does not leak into ancestors");
        check(album(child).value("covers").toList() == QVariantList{
                  QUrl::fromLocalFile(root + "/C.jpg").toString(), QUrl::fromLocalFile(root + "/A.jpg").toString(),
                  QUrl::fromLocalFile(root + "/B.jpg").toString(), QUrl::fromLocalFile(root + "/D.jpg").toString()},
              "album cover previews use the first four members in album order");
        mutation([&] { return library.addPhotosToAlbum(retained, {a}); }, true,
                 "one photo can belong to multiple albums");
        mutation([&] { return library.removePhotosFromAlbum(child, {photos["D.jpg"], photos["E.jpg"]}); }, true,
                 "removing memberships reports actual success");
        mutation([&] { return library.setPhotoAlbumOrder(child, {b, c, a}); }, true,
                 "manual album reorder reports success");
        check(ids(library.customAlbumPhotos(child)) == QVariantList{b, c, a}, "reordered membership is returned to QML");
        mutation([&] { return library.setPhotoAlbumOrder(child, {b, a}); }, false,
                 "incomplete reorder fails without a misleading refresh");
        mutation([&] { return library.movePhotoAlbum(parent, grandchild); }, false,
                 "moving an ancestor under its descendant rejects a cycle");
        mutation([&] { return library.renamePhotoAlbum(parent, "Holiday"); }, true,
                 "rename completes offline and refreshes views");
        check(album(grandchild).value("pathLabel").toString() == "Holiday / Beach / Sunset",
              "ancestor rename updates descendant paths");
        mutation([&] { return library.movePhotoAlbum(grandchild, retained); }, true,
                 "album move completes offline");
        check(album(grandchild).value("pathLabel").toString() == "Keep / Sunset",
              "moved child uses its new parent path");
        mutation([&] { return library.movePhotoAlbum(grandchild, child); }, true, "child can be moved back");
        for (double invalid : {0.0, -1.0, 1.5, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(), 9007199254740992.0}) {
            mutation([&] { return library.renamePhotoAlbum(invalid, "No"); }, false,
                     "invalid album IDs are rejected without refresh");
            mutation([&] { return library.addPhotosToAlbum(child, {a, invalid}); }, false,
                     "invalid photo IDs reject the complete selection without refresh");
            check(library.customAlbumPhotos(invalid).isEmpty(), "invalid album lookup is empty");
        }
        mutation([&] { return library.createPhotoAlbum("No", 1.5); }, false, "fractional parent IDs are rejected");
        mutation([&] { return library.createPhotoAlbum(" ", 0); }, false, "blank names fail without refresh");
        mutation([&] { return library.deletePhotoAlbum(999999); }, false, "missing album deletion fails truthfully");
        mutation([&] { return library.addPhotosToAlbum(child, {a, 999999}); }, false,
                 "missing photo aborts membership batch");
        check(ids(library.customAlbumPhotos(child)) == QVariantList{b, c, a}, "failed mutations preserve the full ordered membership");
        auto scanned = db.allPhotos().first();
        scanned.mtime = 42;
        if (!db.upsertPhoto(scanned)) return 2;
        check(ids(library.customAlbumPhotos(child)) == QVariantList{b, c, a},
              "scanner-style upsert preserves virtual album memberships");
        const int initialCount = library.customPhotoAlbums().size();
        mutation([&] { return library.createPhotoAlbum("Invalid initial selection", 0, {a, 999999}); }, false,
                 "create with missing initial photo rolls back without refresh");
        check(library.customPhotoAlbums().size() == initialCount, "failed initial membership leaves no empty album");
        const double draft = mutation([&] { return library.savePhotoAlbumDraft(-1, "Draft", 0, {a, b}, {}); }, true,
                 "new draft commits name and ordered photos with one refresh").value("id").toDouble();
        const auto original = library.photoAlbumSnapshot(draft);
        check(original.value("photoIds").toList() == QVariantList{a, b}, "snapshot contains stable ordered membership");
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Changed", retained, {c, b}, original); }, true,
                 "existing draft commits rename, parent, membership and order atomically");
        const auto committed = library.photoAlbumSnapshot(draft);
        check(committed.value("name").toString() == "Changed"
              && committed.value("parentId").toDouble() == retained
              && committed.value("photoIds").toList() == QVariantList{c, b}, "all draft fields read back together");
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Lost update", 0, {a}, original); }, false,
                 "concurrent edit rejects stale draft without refresh");
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Invalid parent", 999999, {a}, committed); }, false,
                 "invalid parent rolls back earlier rename");
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Invalid photos", 0, {a, 999999}, committed); }, false,
                 "missing draft member rolls back rename, move and membership removal");
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Duplicate photos", 0, {a, a}, committed); }, false,
                 "duplicate draft membership rejects without refresh");
        check(library.photoAlbumSnapshot(draft) == committed, "all failed drafts preserve the complete committed snapshot");
        if (!db.reorderPhotoAlbumPhotos(qint64(draft), {qint64(b), qint64(c)})) return 2;
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Stale order", retained, {a}, committed); }, false,
                 "concurrent order-only change rejects stale draft");
        const auto reordered = library.photoAlbumSnapshot(draft);
        if (!db.addPhotoAlbumPhotos(qint64(draft), {qint64(a)})) return 2;
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Stale membership", retained, {b}, reordered); }, false,
                 "concurrent membership-only change rejects stale draft");
        mutation([&] { return library.deletePhotoAlbum(draft); }, true, "draft album deletion succeeds");
        mutation([&] { return library.savePhotoAlbumDraft(draft, "Resurrect", 0, {a}, committed); }, false,
                 "deleted draft cannot recreate its album");
        mutation([&] { return library.deletePhotoAlbum(parent); }, true, "subtree deletion reports committed success");
        check(library.customPhotoAlbums().size() == 1 && album(parent).isEmpty() && album(child).isEmpty()
              && album(grandchild).isEmpty() && ids(library.customAlbumPhotos(retained)) == QVariantList{a},
              "subtree deletion removes descendants but preserves unrelated album memberships");
        bool filesIntact = true;
        for (const auto &photo : db.allPhotos()) {
            QFile file(photo.filepath);
            filesIntact &= file.open(QIODevice::ReadOnly) && file.readAll() == "fixture";
        }
        check(db.allPhotos().size() == 5 && filesIntact, "album deletion preserves all source rows and file bytes");
        QVariantList largeIds;
        if (!db.begin()) return 2;
        for (int index = 0; index < 5000; ++index) {
            LibPhoto photo;
            photo.filename = QString::number(index) + ".jpg";
            photo.filepath = root + "/unloaded/" + photo.filename;
            if (!db.upsertPhoto(photo)) return 2;
        }
        if (!db.commit()) return 2;
        for (const auto &photo : db.allPhotos())
            if (photo.filepath.contains("/unloaded/")) largeIds.append(double(photo.id));
        QElapsedTimer elapsed;
        elapsed.start();
        const double large = mutation([&] { return library.savePhotoAlbumDraft(-1, "Large", 0, largeIds, {}); }, true,
                 "5000-photo draft saves in one transaction without image loading").value("id").toDouble();
        printf("  TIMING 5000-photo create: %lld ms\n", static_cast<long long>(elapsed.elapsed()));
        const auto largeSnapshot = library.photoAlbumSnapshot(large);
        check(largeSnapshot.value("photoIds").toList() == largeIds, "large draft retains every ordered member");
        std::reverse(largeIds.begin(), largeIds.end());
        elapsed.restart();
        mutation([&] { return library.savePhotoAlbumDraft(large, "Large reversed", retained, largeIds, largeSnapshot); }, true,
                 "5000-photo reorder and move commits once");
        printf("  TIMING 5000-photo reorder: %lld ms\n", static_cast<long long>(elapsed.elapsed()));
        check(library.photoAlbumSnapshot(large).value("photoIds").toList() == largeIds, "large reordered draft reads back exactly");
        mutation([&] { return library.deletePhotoAlbum(large); }, true, "large fixture cleanup removes only album");
    }
    {
        LibraryService library;
        check(library.customPhotoAlbums().size() == 1 && ids(library.customAlbumPhotos(retained)) == QVariantList{a},
              "album and membership survive service restart");
    }
    printf("Photo album service integration: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
