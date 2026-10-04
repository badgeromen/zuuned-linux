#include "library/LocalMusicCopies.h"
#include "library/LocalTrackModel.h"
#include "library/LibraryDb.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int checks = 0, failures = 0;
    auto check = [&](bool good, const char *label) {
        ++checks;
        if (!good) { ++failures; fprintf(stderr, "FAIL: %s\n", label); }
    };
    LibTrack local;
    local.id = 20; local.filepath = "/music/album/song.flac";
    local.artist = "Zweihänder"; local.albumartist = local.artist;
    local.album = "Ranger of the Old Woods"; local.title = "Bobbit's Journey";
    local.trackNumber = 1; local.durationMs = 174866; local.year = 2024;
    local.filesize = 12480670; local.audioFingerprint = "audio-v1:verified-fixture";
    LibTrack remote = local;
    remote.id = 10; remote.filepath = "/run/user/1000/gvfs/smb-share:server=test/music/song.flac";
    remote.filesize = 12472776;
    auto grouped = LocalMusicCopies::displayRows({remote, local});
    check(grouped.size() == 1 && grouped.front().id == local.id,
          "matching audio with different tag sizes prefers local over older GVFS source");
    grouped = LocalMusicCopies::displayRows({local, remote});
    check(grouped.size() == 1 && grouped.front().id == local.id, "preference independent of row order");
    auto older = local; older.id = 3; older.filepath = "/other/song.flac";
    grouped = LocalMusicCopies::displayRows({local, older});
    check(grouped.size() == 1 && grouped.front().id == older.id, "oldest ID wins equal source classes");
    check(LocalMusicCopies::displayRows({older, local}).front().id == older.id, "stable oldest ID regardless order");
    auto separate = [&](auto mutate, const char *label) {
        auto other = remote; mutate(other);
        check(LocalMusicCopies::displayRows({local, other}).size() == 2, label);
    };
    separate([](auto &t) { t.audioFingerprint.clear(); }, "one unverified source stays separate");
    auto unverified = local; unverified.audioFingerprint.clear();
    check(LocalMusicCopies::displayRows({unverified, unverified}).size() == 2, "no fingerprint never groups");
    separate([](auto &t) { t.audioFingerprint += "other"; }, "different audio stays separate");
    separate([](auto &t) { t.album += " [Deluxe]"; }, "deluxe edition stays separate");
    separate([](auto &t) { t.album += " [Clean]"; }, "clean edition stays separate");
    separate([](auto &t) { t.album += " [Explicit]"; }, "explicit edition stays separate");
    auto sameAudio = [&](auto mutate, const char *label) {
        auto other = remote; mutate(other);
        check(LocalMusicCopies::displayRows({local, other}).size() == 1, label);
        check(LocalMusicCopies::displayRows({other, local}).size() == 1, label);
    };
    sameAudio([](auto &t) { t.artist += " guest"; }, "verified audio survives changed artist spelling");
    sameAudio([](auto &t) { t.albumartist = "Various Artists"; }, "verified audio survives album artist tag differences");
    sameAudio([](auto &t) { t.title += " (Live)"; }, "verified audio takes precedence over contradictory title tags");
    sameAudio([](auto &t) { ++t.year; }, "verified audio survives changed year");
    sameAudio([](auto &t) { t.year = 0; }, "missing year does not prevent verified grouping");
    sameAudio([](auto &t) { t.discNumber = 1; }, "unknown disc joins unique compatible disc");
    auto disc1 = local; disc1.discNumber = 1;
    auto disc2 = remote; disc2.discNumber = 2;
    check(LocalMusicCopies::displayRows({disc1, disc2}).size() == 2, "different known discs stay separate");
    separate([](auto &t) { ++t.trackNumber; }, "different track position stays separate");
    sameAudio([](auto &t) { ++t.durationMs; }, "rounding differences do not override verified audio");
    auto incomplete = [&](auto mutate, const char *label) {
        auto first = local; mutate(first); auto second = first; second.id = 30;
        check(LocalMusicCopies::displayRows({first, second}).size() == 1, label);
    };
    incomplete([](auto &t) { t.artist = "Unknown Artist"; }, "verified unknown artist copies group");
    incomplete([](auto &t) { t.albumartist = "Unknown Artist"; }, "verified unknown album artist copies group");
    incomplete([](auto &t) { t.album = "Unknown Album"; }, "verified unknown album copies group");
    incomplete([](auto &t) { t.title.clear(); }, "verified copies with missing titles group");
    incomplete([](auto &t) { t.trackNumber = 0; }, "verified copies with missing positions group");
    incomplete([](auto &t) { t.durationMs = 0; }, "verified copies with missing durations group");

    // An incomplete source must never connect two incompatible known versions.
    auto ambiguousPermutations = [&](LibTrack a, LibTrack b, LibTrack unknown, const char *label) {
        a.id = 101; b.id = 102; unknown.id = 103;
        const std::array<LibTrack, 3> inputs{a, b, unknown};
        std::array<int, 3> order{0, 1, 2};
        do {
            const auto result = LocalMusicCopies::displayRows({inputs[order[0]], inputs[order[1]], inputs[order[2]]});
            check(result.size() == 3, label);
        } while (std::next_permutation(order.begin(), order.end()));
    };
    auto deluxe = local; deluxe.album += " [Deluxe]";
    auto unknownAlbum = local; unknownAlbum.album = "Unknown Album";
    ambiguousPermutations(local, deluxe, unknownAlbum, "missing album cannot bridge standard and deluxe in any order");
    ambiguousPermutations(disc1, disc2, local, "unknown disc cannot bridge distinct known discs in any order");
    auto position2 = local; position2.trackNumber = 2;
    auto unknownPosition = local; unknownPosition.trackNumber = 0;
    ambiguousPermutations(local, position2, unknownPosition, "unknown track cannot bridge distinct positions in any order");
    auto unknownAlbumCopy = unknownAlbum; unknownAlbumCopy.id = 104;
    check(LocalMusicCopies::displayRows({local, deluxe, unknownAlbum, unknownAlbumCopy}).size() == 3,
          "equally ambiguous verified sources group together without choosing an edition");
    auto albumOnly = local; albumOnly.discNumber = 0; albumOnly.trackNumber = 0;
    auto discOnly = local; discOnly.album.clear(); discOnly.discNumber = 1; discOnly.trackNumber = 0;
    auto trackOnly = local; trackOnly.album.clear(); trackOnly.discNumber = 0;
    albumOnly.id = 111; discOnly.id = 112; trackOnly.id = 113;
    const std::array<LibTrack, 3> partials{albumOnly, discOnly, trackOnly};
    std::array<int, 3> partialOrder{0, 1, 2};
    do {
        const auto result = LocalMusicCopies::displayRows({partials[partialOrder[0]], partials[partialOrder[1]], partials[partialOrder[2]]});
        check(result.size() == 1 && result.front().id == albumOnly.id,
              "complementary nonconflicting tags group with stable real-source representative");
    } while (std::next_permutation(partialOrder.begin(), partialOrder.end()));
    separate([](auto &t) { t.album = "Compilation"; }, "same audio on distinct known albums retains album membership");
    auto sparse = remote; sparse.album = "Unknown Album"; sparse.discNumber = 0; sparse.trackNumber = 0;
    sparse.artist = "Unknown Artist"; sparse.albumartist.clear(); sparse.title.clear(); sparse.year = 0;
    for (const auto &input : {QVector<LibTrack>{sparse, disc1}, QVector<LibTrack>{disc1, sparse}}) {
        const auto result = LocalMusicCopies::displayRows(input);
        check(result.size() == 1 && result.front().id == disc1.id,
              "unique verified match selects informative album and position representative");
    }
    // Many unrelated albums, arbitrary names and tag differences exercise the
    // same policy library-wide. Each recording has independent audio evidence.
    QVector<LibTrack> wholeLibrary;
    for (int album = 0; album < 12; ++album) {
        for (int song = 1; song <= 9; ++song) {
            LibTrack a = local;
            a.id = 1000 + album * 100 + song; a.album = QString("Collection %1").arg(album);
            a.artist = QString("Performer %1").arg(album); a.albumartist = a.artist;
            a.title = QString("Recording %1").arg(song); a.trackNumber = song;
            a.audioFingerprint = QString("audio-v1:collection-%1-recording-%2").arg(album).arg(song);
            a.filepath = QString("/folder-%1/%2.flac").arg(album).arg(song);
            auto b = a; b.id += 10000; b.filepath = "/another-root" + a.filepath;
            b.title = "old tag"; b.artist = "different tag"; b.year = 0; b.durationMs += 1;
            wholeLibrary.append(a); wholeLibrary.append(b);
        }
    }
    check(LocalMusicCopies::displayRows(wholeLibrary).size() == 108,
          "whole library groups every verified duplicate independent of titles or artists");
    std::reverse(wholeLibrary.begin(), wholeLibrary.end());
    check(LocalMusicCopies::displayRows(wholeLibrary).size() == 108,
          "whole library grouping independent of import order");
    auto earlySparse = local; earlySparse.id = 1; earlySparse.trackNumber = 0;
    auto firstSong = local; firstSong.id = 2; firstSong.trackNumber = 1;
    firstSong.audioFingerprint = "audio-v1:independent-recording";
    auto secondSong = local; secondSong.id = 3; secondSong.trackNumber = 2;
    const auto ordered = LocalMusicCopies::displayRows({earlySparse, firstSong, secondSong});
    check(ordered.size() == 2 && ordered[0].id == firstSong.id && ordered[1].id == secondSong.id,
          "representative retains its sorted source position instead of an earlier incomplete copy's position");
    LocalTrackModel model;
    model.setRows({remote, local});
    check(model.rows().size() == 2 && model.rows()[0].id == remote.id, "raw rows and IDs remain available");
    check(model.rowCount() == 1 && model.displayRows().size() == 1, "model exposes one visible copy");
    check(model.representativeId(remote.id) == local.id && model.representativeId(local.id) == local.id,
          "model maps every verified source ID to the visible representative");
    check(model.representativeId(999999) == 999999, "unknown IDs are preserved by alias lookup");
    check(model.data(model.index(0), LocalTrackModel::LibraryIdRole).toLongLong() == local.id,
          "model role uses selected source ID");
    check(model.data(model.index(0), LocalTrackModel::FilepathRole).toString() == local.filepath,
          "model playback path uses selected source");
    const auto snapshot = model.rowsSnapshot();
    check(snapshot.size() == 1 && snapshot[0].toMap().value("libraryId").toLongLong() == local.id,
          "QML snapshot has one matching representative");
    check(!model.data(model.index(1), LocalTrackModel::TitleRole).isValid(), "hidden raw index is not exposed");
    model.setRows({remote});
    check(model.rowCount() == 1 && model.displayRows().front().id == remote.id, "remaining source promoted on reload");
    check(model.representativeId(remote.id) == remote.id && model.representativeId(local.id) == local.id,
          "reload clears removed alias mappings and promotes remaining source ID");

    QTemporaryDir fixture;
    LibraryDb db;
    check(fixture.isValid() && db.open(fixture.filePath("library.db")), "isolated database opens");
    check(db.upsertTrack(remote) && db.upsertTrack(local), "both source rows import");
    auto sources = db.allTracks();
    check(sources.size() == 2, "database retains both physical paths");
    for (const auto &track : sources)
        check(db.updateAudioFingerprint(track, local.audioFingerprint), "verified fingerprint persists for source");
    sources = db.allTracks();
    qint64 localId = -1, remoteId = -1;
    for (const auto &track : sources) {
        if (track.filepath == local.filepath) localId = track.id;
        else remoteId = track.id;
    }
    const QVector<qint64> occurrences{localId, remoteId, localId};
    auto playlist = db.createPlaylist("Intentional repetitions", occurrences);
    check(playlist > 0 && db.playlistTrackIds(playlist) == occurrences, "playlist stores intentional repeated IDs");
    const auto entries = db.playlistEntries(playlist);
    model.setRows(sources);
    check(model.rowCount() == 1 && db.allTracks().size() == 2, "display grouping does not remove database records");
    check(db.playlistTrackIds(playlist) == occurrences && db.playlistEntries(playlist).size() == 3,
          "display grouping preserves playlist occurrences");
    check(entries.size() == 3 && entries[0].id != entries[2].id, "repeated playlist occurrences retain distinct entry IDs");
    db.close();
    check(db.open(fixture.filePath("library.db")) && db.playlistTrackIds(playlist) == occurrences,
          "source IDs and repeated playlist entries survive reopen");
    model.setRows(db.allTracks());
    check(model.rowCount() == 1, "fingerprint grouping survives database reopen");
    check(db.purgeFolder("/music") == 1, "removing local watch folder purges only its source");
    model.setRows(db.allTracks());
    check(model.rowCount() == 1 && model.displayRows().front().id == remoteId,
          "folder removal promotes network copy with original ID");
    check(!db.isExcluded(remote.filepath), "surviving alternate is not excluded");
    fprintf(stderr, "Local music copies: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
