#include "library/LibraryDb.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <cstdio>

static qint64 scalar(const QString &path, const char *query) {
    sqlite3 *db = nullptr; sqlite3_stmt *stmt = nullptr; qint64 value = -1;
    if (sqlite3_open(path.toUtf8().constData(),&db)==SQLITE_OK
        && sqlite3_prepare_v2(db,query,-1,&stmt,nullptr)==SQLITE_OK
        && sqlite3_step(stmt)==SQLITE_ROW) value=sqlite3_column_int64(stmt,0);
    sqlite3_finalize(stmt); sqlite3_close(db); return value;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir directory;
    const QString path=directory.path()+"/library.db";
    LibraryDb db;
    if (!directory.isValid() || !db.open(path)) return 2;
    LibTrack one; one.filepath="/one.flac"; one.title="Intro"; one.artist="Owner";one.album="Double";one.discNumber=1;one.trackNumber=1;
    LibTrack two=one;two.filepath="/two.flac";two.discNumber=2;
    if (!db.upsertTrack(one) || !db.upsertTrack(two)) return 2;
    const auto tracks=db.allTracks();
    const qint64 firstTrack=tracks[0].id, secondTrack=tracks[1].id;
    LibraryDb::PlaylistEntry old;old.artist="Owner";old.album="Double";old.title="Intro";
    const auto oldList=db.createPlaylistEntries("Legacy",{old});
    const auto album=db.createPhotoAlbum("Preserved photo album");
    db.close();
    sqlite3 *writer=nullptr;
    if (sqlite3_open(path.toUtf8().constData(),&writer)!=SQLITE_OK) return 2;
    const QByteArray setup=QString("PRAGMA journal_mode=WAL;PRAGMA wal_autocheckpoint=0;"
        "ALTER TABLE tracks DROP COLUMN audio_fingerprint;ALTER TABLE playlist_tracks DROP COLUMN pending_disc_number;"
        "ALTER TABLE playlist_tracks DROP COLUMN pending_track_number;PRAGMA user_version=7;"
        "INSERT INTO playlist_tracks(playlist_id,track_id,position,pending_artist,pending_album,pending_title)"
        " VALUES(%1,NULL,1,'Owner','Double','Committed WAL');").arg(oldList).toUtf8();
    if (sqlite3_exec(writer,setup.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK) return 2;
    int checks=0,failed=0;
    auto check=[&](bool ok,const char *label){++checks;if(!ok)++failed;printf("%s %s\n",ok?"PASS":"FAIL",label);};
    check(db.open(path) && scalar(path,"PRAGMA user_version")==9,"v7 migrates to v9 while committed WAL remains open");
    check(QFile::exists(path+".v7-backup") && scalar(path+".v7-backup","PRAGMA user_version")==7
        && scalar(path+".v7-backup","SELECT COUNT(*) FROM playlist_tracks")==2,
        "rollback snapshot preserves original v7 and committed WAL row");
    check(scalar(path+".v7-backup","SELECT COUNT(*) FROM pragma_table_info('playlist_tracks') WHERE name='pending_disc_number'")==0,
        "backup retains original schema without v8 columns");
    sqlite3_close(writer);
    auto oldRows=db.playlistEntries(oldList);
    check(oldRows.size()==2 && oldRows[1].title=="Committed WAL" && oldRows[0].discNumber==0 && oldRows[1].trackNumber==0,
        "legacy pending identity remains unknown and order survives migration");
    check(db.allPhotoAlbums().first().toMap().value("id").toLongLong()==album,
        "v8 migration preserves existing virtual albums");
    LibraryDb::PlaylistEntry first=old;first.discNumber=1;first.trackNumber=1;
    LibraryDb::PlaylistEntry second=old;second.discNumber=2;second.trackNumber=1;
    LibraryDb::PlaylistEntry resolved;resolved.trackId=firstTrack;
    const auto playlist=db.createPlaylistEntries("Double disc",{resolved,first,second,first});
    auto rows=db.playlistEntries(playlist);
    check(rows.size()==4 && rows[1].discNumber==1 && rows[2].discNumber==2 && rows[3].discNumber==1,
        "same-title unresolved discs and intentional repetitions stay distinct");
    const auto secondId=rows[2].id;
    const auto firstId=rows[1].id;
    check(db.editPlaylistEntries(playlist,"Edited",{rows[0]},{rows[0].id})
        && db.playlistEntries(playlist)[2].discNumber==2,
        "editing observed members preserves pending disc metadata");
    db.close();
    check(db.open(path) && db.playlistEntries(playlist)[2].id==secondId
        && db.playlistEntries(playlist)[2].trackNumber==1,
        "pending disc track and occurrence identities survive reopening");
    check(db.resolvePlaylistEntry(secondId,secondTrack) && db.playlistEntries(playlist)[1].id==firstId
        && db.playlistEntries(playlist)[1].trackId<0,
        "resolving later disc does not move or consume earlier pending sibling");
    const auto resolvedRow=db.playlistEntries(playlist)[2];
    check(resolvedRow.discNumber==0 && resolvedRow.trackNumber==0 && resolvedRow.title.isEmpty(),
        "resolved slot clears obsolete pending identity");
    check(db.resolvePlaylistEntry(firstId,firstTrack)
        && db.playlistTrackIds(playlist)==QVector<qint64>{firstTrack,firstTrack,secondTrack},
        "out-of-order resolution retains declared member ordering");
    auto migrate=second;migrate.playlistId=oldList;
    check(db.migratePendingPlaylistEntries({migrate}) && db.playlistEntries(oldList).last().discNumber==2
        && db.playlistEntries(oldList).last().trackNumber==1,
        "legacy pending-note migration retains supplied disc and track values");
    check(scalar(path,"SELECT COUNT(*) FROM pragma_foreign_key_check")==0,"migrated database foreign keys are intact");
    printf("Music identity migration: %d checks, %d failures\n",checks,failed);
    return failed?1:0;
}
