#include "library/LibraryDb.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <cstdio>

static qint64 scalar(const QString &path, const char *query) {
    sqlite3 *db = nullptr; sqlite3_stmt *s = nullptr; qint64 value = -1;
    if (sqlite3_open(path.toUtf8().constData(), &db) == SQLITE_OK &&
        sqlite3_prepare_v2(db, query, -1, &s, nullptr) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
        value = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s); sqlite3_close(db); return value;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root("/tmp/zuuned-photo-albums-db-XXXXXX");
    if (!root.isValid()) return 2;
    const QString path = root.path() + "/library.db";
    LibraryDb db;
    if (!db.open(path)) return 2;
    QVector<qint64> photos;
    for (int i = 0; i < 4; ++i) {
        LibPhoto photo; photo.filepath = root.path() + QString("/%1.jpg").arg(i);
        photo.filename = QString("%1.jpg").arg(i);
        QFile file(photo.filepath); if (!file.open(QIODevice::WriteOnly)) return 2;
        file.write("photo fixture"); file.close();
        if (!db.upsertPhoto(photo)) return 2;
    }
    for (const auto &photo : db.allPhotos()) photos.append(photo.id);
    db.close();
    // Downgrade only this disposable fixture to the actual v6 schema. Keep a
    // committed WAL writer alive to exercise the migration snapshot API.
    sqlite3 *old = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &old) != SQLITE_OK ||
        sqlite3_exec(old, "ALTER TABLE tracks DROP COLUMN audio_fingerprint;ALTER TABLE playlist_tracks DROP COLUMN pending_disc_number; ALTER TABLE playlist_tracks DROP COLUMN pending_track_number; DROP TABLE photo_album_photos; DROP TABLE photo_albums; PRAGMA user_version=6;"
                         "INSERT INTO photos(filepath,filename) VALUES('/offline/wal.jpg','wal.jpg');",
                     nullptr, nullptr, nullptr) != SQLITE_OK || !db.open(path)) return 2;
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char *label) { ++checks; if (!ok) ++failed;
        printf("%s %s\n", ok ? "PASS" : "FAIL", label); };
    check(scalar(path,"PRAGMA user_version") == 9, "v6 migrates to v9");
    check(scalar(path + ".v6-backup", "PRAGMA user_version") == 6 &&
          scalar(path + ".v6-backup", "SELECT COUNT(*) FROM photos") == 5,
          "pre-migration backup preserves committed WAL and original version");
    sqlite3_close(old);
    check(db.allPhotoAlbums().isEmpty() && db.allPhotos().size() == 5, "migration preserves photos, starts with no virtual albums");
    const qint64 a = db.createPhotoAlbum(" Travel ");
    const qint64 b = db.createPhotoAlbum("Japan", a);
    const qint64 c = db.createPhotoAlbum("Kyoto", b);
    const qint64 d = db.createPhotoAlbum("Favorites");
    check(a > 0 && b > a && c > b && d > c, "creates stable root and nested IDs");
    check(db.createPhotoAlbum("travel") < 0 && db.createPhotoAlbum(" ") < 0 &&
          db.createPhotoAlbum(QString(201, 'a')) < 0 && db.createPhotoAlbum("Missing",99999) < 0,
          "rejects duplicate sibling names, empty/oversized names and missing parents");
    check(db.createPhotoAlbum("Japan", d) > 0, "same name allowed under different parents");
    check(!db.movePhotoAlbum(a,c) && !db.movePhotoAlbum(a,a), "rejects descendant and self cycles");
    check(!db.movePhotoAlbum(b,d), "move rejects duplicate sibling name");
    check(db.movePhotoAlbum(c,d) && db.movePhotoAlbum(c,0) && db.movePhotoAlbum(c,b), "moves nested album and supports root");
    check(!db.renamePhotoAlbum(a,"Favorites") && db.renamePhotoAlbum(a,"Trips"), "rename validates sibling uniqueness");
    check(db.addPhotoAlbumPhotos(b,{photos[2],photos[0],photos[2],photos[1]}) &&
          db.photoAlbumPhotoIds(b) == QVector<qint64>{photos[2],photos[0],photos[1]},
          "membership preserves explicit order and deduplicates");
    check(!db.addPhotoAlbumPhotos(b,{photos[3],99999}) && db.photoAlbumPhotoIds(b).size() == 3,
          "invalid append rolls back all prior additions");
    check(db.addPhotoAlbumPhotos(d,{photos[0]}) && db.photoAlbumPhotoIds(b).contains(photos[0]),
          "one photo belongs to multiple albums");
    check(!db.reorderPhotoAlbumPhotos(b,{photos[0],photos[2]}) &&
          !db.reorderPhotoAlbumPhotos(b,{photos[0],photos[2],photos[2]}) &&
          !db.reorderPhotoAlbumPhotos(b,{photos[0],photos[2],photos[3]}),
          "reorder rejects stale, repeated and nonmember IDs");
    check(db.reorderPhotoAlbumPhotos(b,{photos[1],photos[0],photos[2]}) &&
          db.photoAlbumPhotoIds(b) == QVector<qint64>{photos[1],photos[0],photos[2]}, "exact reorder persists");
    check(db.removePhotoAlbumPhotos(b,{photos[0]}) && db.photoAlbumPhotoIds(d) == QVector<qint64>{photos[0]} &&
          db.allPhotos().size() == 5, "remove membership preserves other albums and library photos");
    check(db.begin() && db.renamePhotoAlbum(a,"Temporary") && db.rollback(), "edits participate in caller transaction");
    check(scalar(path,"SELECT COUNT(*) FROM photo_albums WHERE name='Trips'") == 1, "outer rollback undoes nested edit");
    db.close(); check(db.open(path) && db.photoAlbumPhotoIds(b) == QVector<qint64>{photos[1],photos[2]}, "tree and membership survive reopen");
    check(db.removePhoto(photos[1],false) && db.photoAlbumPhotoIds(b) == QVector<qint64>{photos[2]},
          "library photo removal cascades membership");
    check(db.deletePhotoAlbum(a) && db.photoAlbumPhotoIds(b).isEmpty() && db.photoAlbumPhotoIds(d).size() == 1 &&
          db.allPhotos().size() == 4, "subtree deletion preserves photos and other albums");
    check(!db.renamePhotoAlbum(b,"Deleted") && !db.addPhotoAlbumPhotos(b,{photos[0]}), "stale album IDs fail");
    check(db.createPhotoAlbum("Trips") > d, "deleted stable IDs are not reused");
    bool filesRemain = true;
    for (int i=0;i<4;++i) filesRemain &= QFile::exists(root.path() + QString("/%1.jpg").arg(i));
    check(filesRemain, "all source files untouched by edits and deletions");
    check(scalar(path,"SELECT COUNT(*) FROM pragma_foreign_key_check") == 0, "all foreign keys remain valid");
    qint64 deep = db.createPhotoAlbum("Depth");
    bool nested = deep > 0;
    for (int i = 1; i < 32; ++i) { deep = db.createPhotoAlbum(QString::number(i), deep); nested &= deep > 0; }
    check(nested && db.createPhotoAlbum("Too deep", deep) < 0, "creation bounds nesting to 32 levels");
    check(!db.movePhotoAlbum(d,deep), "moving an existing subtree enforces depth limit");
    const auto maxId = scalar(path,"SELECT MAX(id) FROM photos");
    check(db.removePhoto(maxId,false), "remove highest indexed photo");
    LibPhoto fresh; fresh.filepath = "/offline/new.jpg"; fresh.filename = "new.jpg";
    check(db.upsertPhoto(fresh) && db.photoPathIndex().value(fresh.filepath).first > maxId,
          "photo IDs never reuse deleted IDs after migration");
    printf("%d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
