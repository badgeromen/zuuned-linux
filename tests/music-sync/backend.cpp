#include "sync/SyncEngine.h"
#include "LibraryService.h"
#include "library/LibraryDb.h"
extern "C" {
#include "id3_rewrite.h"
#include "libav_transcode.h"
}
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
    // Sync passthrough sniff: only MPEG Layer III audio skips transcoding.
    // ADTS AAC and MP2 share the 0xFFE sync; ID3 tags can front any of them.
    auto sniff = [&](const QByteArray &bytes) {
        const QString path = root + "/sniff.bin";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        file.write(bytes); file.close();
        return SyncEngine::sniffIsMp3(path);
    };
    auto id3 = [](int size, bool footer = false) {
        QByteArray tag("ID3\x03\x00", 5);
        tag.append(char(footer ? 0x10 : 0));
        for (int shift : {21, 14, 7, 0}) tag.append(char((size >> shift) & 0x7F));
        return tag + QByteArray(size, 'T') + (footer ? QByteArray("3DI\x03\x00\x10\0\0\0\0", 10) : QByteArray());
    };
    const QByteArray layer3("\xFF\xFB\x90\x64", 4), body(400, '\0');
    check(sniff(layer3 + body), "bare MPEG-1 Layer III frame plays as-is");
    check(sniff(id3(100) + QByteArray(300, '\0') + layer3 + body), "ID3 tag and zero padding before the frame");
    check(sniff(id3(200 * 1024) + layer3 + body), "a cover-art-sized ID3 tag is skipped, not scanned");
    check(sniff(id3(50) + id3(60) + layer3 + body), "two stacked ID3 tags");
    check(sniff(id3(80, true) + layer3 + body), "ID3v2 footer flag is honored");
    check(sniff(QByteArray("\xFF\xF3\x50\xC4", 4) + body), "MPEG-2 Layer III frame");
    const QByteArray adts("\xFF\xF1\x50\x80\x02\x1F\xFC", 7);
    check(!sniff(adts + body), "raw ADTS AAC transcodes");
    check(!sniff(id3(100) + adts + body), "ID3-tagged ADTS AAC transcodes");
    check(!sniff(QByteArray("\xFF\xFD\x90\x04", 4) + body), "MPEG-1 Layer II transcodes");
    check(!sniff(id3(100) + QByteArray("fLaC\0\0\0\x22", 8) + body), "FLAC behind an ID3 tag transcodes");
    check(!sniff(QByteArray("RIFF\x24\0\0\0WAVEfmt ", 16) + layer3 + body), "WAV is not scanned for frame syncs");
    check(!sniff(QByteArray("\x30\x26\xB2\x75\x8E\x66\xCF\x11", 8) + body), "ASF/WMA transcodes");
    check(!sniff(QByteArray("\xFF\xFB\xF0\x64", 4) + body), "reserved bitrate index is not a frame");
    check(!sniff(id3(4096).left(200)), "an ID3 size past end of file transcodes");
    check(!sniff(QByteArray()), "an empty file transcodes");

    // In-place tag edits: any MPEG audio layer, never another codec that
    // merely carries an ID3 tag (the rewriter would drop its header).
    auto mpeg = [&](const QByteArray &bytes) {
        const QString path = root + "/edit-sniff.bin";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        file.write(bytes); file.close();
        return SyncEngine::sniffIsMpeg(path);
    };
    const QByteArray flacHead = QByteArray("fLaC\0\0\0\x22", 8) + QByteArray(34, 'S')
        + QByteArray("\x03\0\0\x12", 4) + QByteArray(18, '\xFF');   // STREAMINFO, seek table
    check(mpeg(id3(100) + layer3 + body), "tagged Layer III may be edited in place");
    check(mpeg(QByteArray("\xFF\xFD\x90\x04", 4) + body), "MPEG-1 Layer II may be edited in place");
    check(!mpeg(id3(100) + flacHead + body), "an ID3-tagged FLAC is never edited as MPEG");
    check(!mpeg(id3(100) + adts + body), "an ID3-tagged ADTS AAC is never edited as MPEG");
    check(!mpeg(QByteArray("\x30\x26\xB2\x75\x8E\x66\xCF\x11", 8) + body), "ASF/WMA is never edited as MPEG");

    // The rewriter refuses such a file itself and leaves it byte for byte,
    // although the FLAC seek-table placeholders contain FF FF syncs.
    auto writeFile = [&](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return file.write(bytes) == bytes.size();
    };
    auto readFile = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    ZuunedId3Edits edits{};
    edits.title = "Edited title";
    {
        const QString path = root + "/tagged.flac";
        const QByteArray flac = id3(100) + flacHead + QByteArray(400, 'F');
        check(writeFile(path, flac) && zuuned_edit_mp3_tags(path.toUtf8().constData(), &edits) != 0
              && readFile(path) == flac && !QFile::exists(path + ".zuuned_edit.tmp"),
              "the ID3 rewriter refuses an ID3-tagged FLAC and leaves it untouched");
    }
    {
        // Two transcodes of sources with the same file name (episodes in
        // different folders) get different outputs: the sync pipeline runs
        // transcodes concurrently with a send still reading the first one.
        QByteArray wav("RIFF", 4);
        auto le = [](quint32 v, int n) { QByteArray b; for (int i = 0; i < n; ++i) b.append(char(v >> (8 * i))); return b; };
        const quint32 dataBytes = 8000 * 2 / 5;                   // 0.2 s, 8 kHz mono 16-bit
        wav += le(36 + dataBytes, 4) + "WAVEfmt " + le(16, 4) + le(1, 2) + le(1, 2) + le(8000, 4)
             + le(16000, 4) + le(2, 2) + le(16, 2) + "data" + le(dataBytes, 4) + QByteArray(int(dataBytes), '\0');
        QDir().mkpath(root + "/a"); QDir().mkpath(root + "/b");
        const QString first = root + "/a/Episode 1.wav", second = root + "/b/Episode 1.wav";
        char *outA = writeFile(first, wav) ? zuuned_transcode_audio(first.toUtf8().constData(), "Artist", nullptr, nullptr) : nullptr;
        char *outB = writeFile(second, wav) ? zuuned_transcode_audio(second.toUtf8().constData(), "Artist", nullptr, nullptr) : nullptr;
        check(outA && outB && strcmp(outA, outB) != 0 && QFile::exists(outA) && QFile::exists(outB),
              "same-named sources transcode to different temporary outputs");
        for (char *out : {outA, outB}) if (out) { QFile::remove(out); free(out); }
    }
    {
        // Stacked tags: one rewritten tag, then exactly the original audio.
        // The second tag holds UTF-16 text whose BOM (FF FE) looks like a
        // frame sync to a search.
        const QString path = root + "/stacked.mp3";
        QByteArray utf16Tag = id3(60);
        utf16Tag.replace(10, 2, QByteArray("\xFF\xFE", 2));
        check(writeFile(path, id3(50) + utf16Tag + layer3 + body)
              && zuuned_edit_mp3_tags(path.toUtf8().constData(), &edits) == 0, "stacked tags are rewritten");
        const QByteArray out = readFile(path);
        const int tagEnd = out.size() >= 10 && out.startsWith("ID3")
            ? 10 + ((uchar(out[6]) & 0x7F) << 21 | (uchar(out[7]) & 0x7F) << 14
                    | (uchar(out[8]) & 0x7F) << 7 | (uchar(out[9]) & 0x7F)) : -1;
        check(tagEnd > 0 && out.mid(tagEnd, layer3.size() + body.size()) == layer3 + body
              && out.indexOf("TTTT", tagEnd) < 0,
              "after the rewritten tag the file holds the audio, not the second tag's bytes");
    }
    printf("Music sync identity: %d checks, %d failures; no USB constructed\n", checks, failed);
    return failed ? 1 : 0;
}
