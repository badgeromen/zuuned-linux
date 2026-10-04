#include "sync/SyncEngine.h"
#include "LibraryService.h"
#include "library/LibraryDb.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <cstdio>

// No DeviceService is constructed: real queue persistence and playlist state
// transitions run against synthetic device metadata, with no worker attached.
struct SyncIdentityTestAccess {
    static void playlist(SyncEngine &engine, const QVariantList &members,
                         const QVariantList &device, const QHash<QString, quint32> &sent = {}) {
        engine.m_pendingPlaylists.clear(); engine.m_playlistRound.clear();
        engine.m_playlistIdentities.clear();
        for (const auto &value : device) {
            const auto row = value.toMap();
            engine.m_playlistIdentities.add(MusicIdentity::fromMap(row), row.value("itemId").toLongLong());
        }
        SyncEngine::PendingPlaylist p;
        p.name = "Fixture"; p.members = members;
        p.entryId = engine.queue()->addPlaylistEntry(p.name, members.size());
        engine.m_pendingPlaylists.append(p); engine.m_playlistRound.append(0);
        engine.m_playlistPos = 0; engine.m_isSyncing = true; engine.m_inPlaylistPhase = true;
        engine.m_sentTrackIds = sent;
        engine.m_sentTrackIdentities.clear();
        for (const auto &value : members) {
            const auto row = value.toMap();
            const QString path = row.value("filepath").toString();
            if (sent.contains(path) && !engine.m_sentTrackIdentities.contains(path))
                engine.m_sentTrackIdentities.insert(path, MusicIdentity::fromMap(row));
        }
    }
    static void advance(SyncEngine &engine) { engine.advancePlaylist(); }
    static void save(SyncEngine &engine) { engine.persistQueue(); }
    static QVariantList members(const SyncEngine &engine) {
        return engine.m_pendingPlaylists.isEmpty() ? QVariantList{} : engine.m_pendingPlaylists[0].members;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("MusicSyncFixture"); app.setApplicationName("Identity");
    const auto root = qEnvironmentVariable("MUSIC_SYNC_TEST_ROOT");
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!root.startsWith("/tmp/zuuned-music-sync-backend-") || !data.startsWith(root + '/')) return 2;
    QDir().mkpath(data);
    int checks = 0, failed = 0;
    auto check = [&](bool ok, const char *name) { ++checks; if (!ok) ++failed;
        printf("%s %s\n", ok ? "PASS" : "FAIL", name); };
    QVariantMap first{{"artist", "Artist"}, {"album", "Album"}, {"title", "Intro"},
                      {"discNumber", 1}, {"trackNumber", 1}, {"filepath", "/first.mp3"}};
    auto second = first; second["discNumber"] = 2; second["filepath"] = "/second.mp3";
    auto deviceFirst = first; deviceFirst["itemId"] = 101;
    auto deviceSecond = second; deviceSecond["itemId"] = 102;
    const QString store = data + "/syncqueue.json";
    auto run = [&](const QVariantList &members, const QVariantList &device,
                   const QHash<QString, quint32> &sent = {}) {
        QFile::remove(store);
        SyncEngine engine;
        QVariantList resolved;
        QObject::connect(&engine, &SyncEngine::workerForgePlaylist, &app,
                         [&](const QString &, const QVariantList &ids) { resolved = ids; });
        SyncIdentityTestAccess::playlist(engine, members, device, sent);
        SyncIdentityTestAccess::advance(engine);
        return resolved;
    };
    check(run({first, second}, {deviceFirst, deviceSecond}) == QVariantList({101u,102u}),
          "same-title discs link to their distinct device IDs in order");
    check(run({first, first, second}, {deviceFirst, deviceSecond}) == QVariantList({101u,101u,102u}),
          "repeated playlist occurrences are preserved");
    auto unknown = deviceFirst; unknown["discNumber"] = 0; unknown["trackNumber"] = 0;
    check(run({first,second}, {unknown}).isEmpty(), "ambiguous legacy device row never forges a shortened playlist");
    check(run({first}, {unknown}) == QVariantList({101u}), "one unambiguous legacy member still resolves");
    check(run({first,second}, {deviceFirst}).isEmpty(), "missing disc refuses entire playlist");
    auto wrongArtist = deviceFirst; wrongArtist["artist"] = "Other";
    check(run({first}, {wrongArtist}).isEmpty(), "artist-free fallback cannot link another artist");
    auto duplicate = deviceFirst; duplicate["itemId"] = 103;
    check(run({first}, {deviceFirst,duplicate}).isEmpty(), "duplicate exact device candidates are explicit ambiguity");
    check(run({first,second}, {}, {{"/first.mp3",201}, {"/second.mp3",202}}) == QVariantList({201u,202u}),
          "same-sync returned IDs preserve identity across canonical artist changes");
    auto changedFile = second; changedFile["filepath"] = "/first.mp3";
    check(run({first,changedFile}, {}, {{"/first.mp3",201}}).isEmpty(),
          "same filepath with conflicting saved identity cannot reuse a sent object");
    QFile::remove(store);
    {
        SyncEngine engine;
        SyncIdentityTestAccess::playlist(engine,{first,second},{deviceFirst,deviceSecond});
        SyncIdentityTestAccess::save(engine);
    }
    {
        SyncEngine restored;
        check(SyncIdentityTestAccess::members(restored) == QVariantList({first,second}),
              "saved playlist identities preserve disc track and order across restart");
    }
    {
        QFile file(store); if (!file.open(QIODevice::WriteOnly)) return 2;
        file.write(R"json({"entries":[{"entryId":"legacy","type":"playlist","title":"Legacy","status":"pending"}],"playlists":[{"entryId":"legacy","name":"Legacy","fullKeys":["artist\talbum\tintro","artist\talbum\tintro"],"shortKeys":["album\tintro","album\tintro"]}]})json"); file.close();
        SyncEngine restored;
        const auto members = SyncIdentityTestAccess::members(restored);
        check(members.size() == 2 && members[0].toMap().value("discNumber").toInt() == 0,
              "legacy persisted occurrences survive with unknown disc identity");
    }

    LibraryDb db;
    if (!db.open(data + "/library.db")) return 2;
    LibTrack local;
    local.artist = "Artist"; local.albumartist = "Owner"; local.album = "Album";
    local.title = "Intro"; local.trackNumber = 1; local.discNumber = 1; local.filepath = root + "/disc1.mp3";
    if (!db.upsertTrack(local)) return 2;
    local.discNumber = 2; local.filepath = root + "/disc2.mp3";
    if (!db.upsertTrack(local)) return 2;
    {
        LibraryService library;
        check(!library.hasTrack("Artist","Album","Intro"), "local imports reject unknown-disc ambiguity");
        check(library.hasTrack("Owner","Album","Intro",2,1), "local import resolves the album-artist alias and disc");
        check(!library.hasTrack("Artist","Album","Intro",3,1), "missing local disc is not suppressed");
        auto imported = library.importDevicePlaylist("Disc order", {second,first});
        const auto entries = db.playlistEntries(imported.value("id").toLongLong());
        check(imported.value("here").toInt() == 2 && entries.size() == 2
              && entries[0].trackId != entries[1].trackId, "device playlist imports select distinct local disc rows");
    }
    // Reverse imports also need source peers: a unique unknown local file
    // cannot represent two differently numbered device songs.
    local.album = "Unknown Numbers"; local.discNumber = 0; local.trackNumber = 0;
    local.filepath = root + "/unknown.mp3";
    if (!db.upsertTrack(local)) return 2;
    auto reverseFirst = first; reverseFirst["album"] = "Unknown Numbers"; reverseFirst["itemId"] = 401;
    auto reverseSecond = reverseFirst; reverseSecond["discNumber"] = 2; reverseSecond["itemId"] = 402;
    const QVariantList devicePeers{reverseFirst,reverseSecond};
    qint64 reversePlaylist = -1, legacyPlaylist = -1;
    {
        LibraryService library;
        check(!library.hasTrack("Artist","Unknown Numbers","Intro",1,1,true,true,devicePeers),
              "reverse import does not suppress either device disc against one unknown local row");
        check(!library.hasLocalTrack("Artist","Unknown Numbers","Intro",1,1,true,true,devicePeers),
              "reverse local badge uses device source ambiguity too");
        const auto imported = library.importDevicePlaylist("Unknown local",devicePeers,devicePeers);
        reversePlaylist = imported.value("id").toLongLong();
        const auto entries = db.playlistEntries(reversePlaylist);
        check(imported.value("here").toInt()==0 && imported.value("pending").toInt()==2
              && entries.size()==2 && entries[0].discNumber==1 && entries[1].discNumber==2,
              "reverse playlist keeps both unresolved numbered source identities");
        QVariantMap legacy{{"artist","Artist"},{"album","Album"},{"title","Intro"}};
        const auto legacyImport=library.importDevicePlaylist("Legacy unknown discs",{legacy,legacy});
        legacyPlaylist=legacyImport.value("id").toLongLong();
        check(legacyImport.value("here").toInt()==0 && legacyImport.value("pending").toInt()==2,
              "unknown legacy occurrences never pick one of multiple local discs");
    }
    {
        LibraryService library;
        const auto entries=db.playlistEntries(reversePlaylist);
        check(entries.size()==2 && entries[0].trackId<0 && entries[1].trackId<0,
              "pending resolution retains peer ambiguity across reopening");
        const auto legacy=db.playlistEntries(legacyPlaylist);
        check(legacy.size()==2 && legacy[0].trackId<0 && legacy[1].trackId<0,
              "legacy unknown pending slots remain unresolved after reopening");
    }
    printf("Music sync identity: %d checks, %d failures; no USB constructed\n", checks, failed);
    return failed ? 1 : 0;
}
