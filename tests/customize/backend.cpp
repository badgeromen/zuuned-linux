// Exercise the real asynchronous Apply boundary against disposable local data.
// run-backend.mjs supplies isolated XDG roots before this process starts.
#include "LibraryService.h"
#include "DevicePlaylistState.h"
#include "library/AlbumArtService.h"
#include "library/ArtistImageService.h"
#include "library/OnlineAlbumArtService.h"
#include "library/LibraryDb.h"
#include "library/LocalTrackModel.h"
#include "library/MusicIdentityClient.h"
#include "sync/SyncEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <cstdio>
#include <sqlite3.h>

namespace {
QByteArray contents(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool saveImage(const QString &path, QRgb color) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(24, 24, QImage::Format_RGB32);
    image.fill(color);
    return image.save(path);
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ZuunedCustomizeFixture"));
    app.setApplicationName(QStringLiteral("BackendApply"));
    const QString root = qEnvironmentVariable("CUSTOMIZE_BACKEND_ROOT");
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!root.startsWith(QLatin1String("/tmp/zuuned-customize-backend-"))
        || !data.startsWith(root + QLatin1Char('/'))
        || !cache.startsWith(root + QLatin1Char('/'))) {
        fprintf(stderr, "Refusing to run outside isolated fixture paths.\n");
        return 2;
    }
    QDir().mkpath(data);
    QDir().mkpath(cache);
    int failures = 0;
    auto check = [&failures](bool condition, const char *label) {
        printf("  %s %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    };
    LibraryDb db;
    if (!db.open(data + QStringLiteral("/library.db"))) return 2;
    const QString media = root + QStringLiteral("/media/song.flac");
    QDir().mkpath(QFileInfo(media).absolutePath());
    QFile song(media);
    if (!song.open(QIODevice::WriteOnly)) return 2;
    song.write("fLaC fixture"); song.close();
    LibTrack seed;
    seed.filepath = media; seed.title = QStringLiteral("Fixture song");
    seed.artist = QStringLiteral("Owner"); seed.albumartist = QStringLiteral("Owner");
    seed.album = QStringLiteral("Original"); seed.year = 2008;
    seed.genre = QStringLiteral("Alternative");
    if (!db.upsertTrack(seed)) return 2;
    const qint64 target = db.allTracks().first().id;
    LibTrack other = seed;
    other.filepath = root + QStringLiteral("/other.flac");
    other.albumartist = QStringLiteral("Other owner");
    if (!db.upsertTrack(other)) return 2;

    const auto pathFor = [&cache](const QString &owner, const QString &album) {
        return cache + QStringLiteral("/art/") + AlbumArtService::cacheKey(owner, album)
            + QStringLiteral(".jpg");
    };
    const QString originalPath = pathFor(QStringLiteral("Owner"), QStringLiteral("Original"));
    const QString selectedPath = root + QStringLiteral("/selected.png");
    if (!saveImage(originalPath, qRgb(230, 20, 20))
        || !saveImage(selectedPath, qRgb(20, 20, 230))
        || !saveImage(root + QStringLiteral("/media/cover.jpg"), qRgb(20, 230, 20))) return 2;
    QFile invalid(root + QStringLiteral("/invalid.png"));
    if (!invalid.open(QIODevice::WriteOnly)) return 2;
    invalid.write("This is not image data."); invalid.close();
    const QByteArray originalBytes = contents(originalPath);
    auto row = [&db, target] {
        for (const auto &track : db.allTracks()) if (track.id == target) return track;
        return LibTrack();
    };

    LibTrack copy = seed;
    copy.artist = copy.albumartist = QStringLiteral("Old owner tag");
    copy.album = QStringLiteral("Copy album");
    copy.trackNumber = 1; copy.durationMs = 180000;
    copy.filepath = root + QStringLiteral("/gvfs/share/copy.flac");
    if (!db.upsertTrack(copy)) return 2;
    copy.artist = copy.albumartist = QStringLiteral("OwnerCopy");
    copy.title = QStringLiteral("Different title tag"); copy.year = 2019;
    copy.filepath = root + QStringLiteral("/local-copy.flac");
    if (!db.upsertTrack(copy)) return 2;
    qint64 copyLocalId = -1, copyRemoteId = -1;
    for (const auto &track : db.allTracks()) {
        if (track.album != QStringLiteral("Copy album")) continue;
        if (!db.updateAudioFingerprint(track, QStringLiteral("audio-v1:verified-fixture"))) return 2;
        if (track.filepath == copy.filepath) copyLocalId = track.id;
        else copyRemoteId = track.id;
    }
    const QVector<qint64> copyOccurrences{copyRemoteId, copyLocalId, copyRemoteId};
    const auto copyPlaylist = db.createPlaylist(QStringLiteral("Copy occurrences"), copyOccurrences);
    if (copyPlaylist <= 0 || !saveImage(root + QStringLiteral("/gvfs/share/cover.jpg"), qRgb(80, 90, 100))) return 2;

    LibraryService service;
    auto *copyModel = qobject_cast<LocalTrackModel *>(service.tracksModel());
    check(copyModel && copyModel->rows().size() == 4 && copyModel->rowCount() == 3
        && service.trackCount() == 3, "production library counts verified copies once while retaining all source rows");
    bool copyAlbumCount = false, copyArtistCount = false;
    for (const auto &album : service.albumsList()) {
        const auto map = album.toMap();
        if (map.value("name") == copy.album) copyAlbumCount = map.value("count").toInt() == 1;
    }
    for (const auto &artist : service.artistsList()) {
        const auto map = artist.toMap();
        if (map.value("name") == copy.albumartist) copyArtistCount = map.value("count").toInt() == 1;
    }
    check(copyAlbumCount && copyArtistCount, "production album and artist counts exclude verified copies");
    const auto copyPlaylistRows = service.playlistTracks(copyPlaylist);
    check(copyPlaylistRows.size() == 3 && db.playlistTrackIds(copyPlaylist) == copyOccurrences,
          "production playlists retain hidden source IDs and intentional repetitions");
    bool copyOrder = copyPlaylistRows.size() == copyOccurrences.size();
    for (qsizetype i = 0; copyOrder && i < copyPlaylistRows.size(); ++i)
        copyOrder = copyPlaylistRows[i].toMap().value("id").toLongLong() == copyOccurrences[i];
    check(copyOrder, "production playlist rows preserve original source IDs in occurrence order");
    const auto logicalPlaylist = db.createPlaylist(QStringLiteral("Logical copies"), {copyRemoteId});
    service.addToPlaylist(double(logicalPlaylist), {double(copyLocalId)});
    check(logicalPlaylist > 0 && db.playlistTrackIds(logicalPlaylist) == QVector<qint64>{copyRemoteId},
          "adding visible representative does not duplicate a playlist's existing alternate source");
    service.addToPlaylist(double(logicalPlaylist), {double(target), double(copyLocalId), double(target)});
    check(db.playlistTrackIds(logicalPlaylist) == QVector<qint64>{copyRemoteId, target},
          "playlist alias filtering still adds a different recording exactly once");
    const QString copyArtKey = LibraryService::artKey(copy.albumartist, copy.album);
    {
        QEventLoop artLoop;
        QObject::connect(&service, &LibraryService::artChanged, &artLoop, [&] {
            if (!service.artPaths().value(copyArtKey).toString().isEmpty()) artLoop.quit();
        });
        QTimer::singleShot(5000, &artLoop, &QEventLoop::quit);
        service.requestArt(copy.albumartist, copy.album, copy.filepath);
        if (service.artPaths().value(copyArtKey).toString().isEmpty()) artLoop.exec();
    }
    check(!service.artPaths().value(copyArtKey).toString().isEmpty()
        && QFileInfo::exists(pathFor(copy.albumartist, copy.album)),
          "representative album discovers local cover from hidden copy with different artist tags");
    // Hold the SQLite write lock so a real queued discovery save can only
    // observe the state after the user's newer manual action commits.
    const QString discoveryKey = AlbumArtService::cacheKey("Owner", "Original");
    auto *discoveryService = service.findChild<OnlineAlbumArtService *>();
    check(discoveryService && db.begin(), "discovery race fixture holds the write lock");
    if (discoveryService) {
        emit discoveryService->discoveryResult("Owner", "Original", {{"status", "needsMatch"}, {"reason", "Old lookup"}});
        db.setCollectionCustomization("album", discoveryKey,
            {{"_artworkChoiceRevision", "newer-manual-reset"}}, {});
        db.commit();
        check(QThreadPool::globalInstance()->waitForDone(5000)
            && !db.collectionCustomization("album", discoveryKey).value("identity").toMap().contains("_artworkDiscovery"),
            "queued discovery cannot undo a manual reset with an unchanged empty image path");
        db.removeCollectionCustomization("album", discoveryKey);
        db.begin();
        emit discoveryService->discoveryResult("Owner", "Original", {{"status", "needsMatch"}, {"reason", "First lookup"}});
        emit discoveryService->discoveryResult("Owner", "Original", {{"status", "noArtwork"}, {"reason", "Newest lookup"}});
        db.commit();
        check(QThreadPool::globalInstance()->waitForDone(5000)
            && db.collectionCustomization("album", discoveryKey).value("identity").toMap()
                .value("_artworkDiscovery").toMap().value("reason").toString() == "Newest lookup",
            "newest discovery survives concurrent first-time persistence regardless of worker order");
        db.removeCollectionCustomization("album", discoveryKey);
    }
    // Exercise the actual asynchronous artwork boundary with cached provider
    // responses: the second same-name artist has Fanart's legacy HTTP200 {}.
    const QString browseArtist = "44444444-4444-4444-8444-444444444444";
    const QString missingArtist = "55555555-5555-4555-8555-555555555555";
    const QString browsePortrait = "https://assets.fanart.tv/fixture-portrait.jpg";
    MusicIdentityClient artworkFixture([&](const QUrl &url, bool mb) {
        ArtworkHttp::Response response; response.status = 200;
        if (mb && url.path().contains("release-group"))
            response.body = R"({"release-groups":[{"id":"66666666-6666-4666-8666-666666666666","title":"Skyfall fixture"}]})";
        else if (mb) response.body = R"({"artists":[{"id":"44444444-4444-4444-8444-444444444444","name":"Billie fixture"},{"id":"55555555-5555-4555-8555-555555555555","name":"Billie fixture"}]})";
        else if (url.path().endsWith(browseArtist)) response.body = R"({"mbid_id":"44444444-4444-4444-8444-444444444444","artistthumb":[{"url":"https://assets.fanart.tv/fixture-portrait.jpg"}]})";
        else response.body = "{}";
        return response;
    });
    QString artworkError;
    check(artworkFixture.searchFanartArtistUrls("Billie fixture", &artworkError) == QStringList{browsePortrait}
          && artworkError.isEmpty(), "same-name Fanart fixture retains the available portrait");
    check(artworkFixture.searchAlbums("Skyfall fixture", "Adele fixture", &artworkError).size() == 1
          && artworkError.isEmpty(), "album identity search primes the shared provider cache");
    auto browseArt = [&](const QVariantMap &context, const QVariantList &expected, const char *label,
                         const QString &kind = "artist") {
        QEventLoop loop;
        QTimer timeout; timeout.setSingleShot(true);
        bool finished = false, correct = false;
        const auto connection = QObject::connect(&service, &LibraryService::customizeArtReady, &loop,
            [&](const QString &request, const QVariantList &urls, const QString &error) {
                if (request != "browse-fixture") return;
                finished = true; correct = urls == expected && error.isEmpty(); loop.quit();
            });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(2000);
        service.requestCustomizeArt("browse-fixture", kind, kind == "album" ? "caa" : "fanart", context);
        if (!finished) loop.exec();
        QObject::disconnect(connection);
        check(finished && correct, label);
    };
    browseArt({{"name", "Billie fixture"}}, {browsePortrait}, "production artwork signal shows portraits without a sibling's false error");
    browseArt({{"name", "Billie fixture"}, {"musicIdentity", QVariantMap{{"mbid", missingArtist}}}}, {},
              "explicit unmatched Fanart identity emits a clean no-art result");
    browseArt({{"album", "Skyfall fixture"}, {"artist", "Adele fixture"}},
              {"https://coverartarchive.org/release-group/66666666-6666-4666-8666-666666666666/front-500"},
              "album artwork reuses the identity search cache without another MusicBrainz request", "album");
    int sequence = 0;
    auto apply = [&](const QVariantMap &context, const QVariantMap &draft, bool expectedSuccess,
                     const std::function<bool()> &committedState, const char *label) {
        const QString request = QStringLiteral("fixture-%1").arg(++sequence);
        QEventLoop loop;
        QTimer timeout; timeout.setSingleShot(true);
        bool signaled = false, success = false, visibleAtSignal = false;
        QString error;
        const auto connection = QObject::connect(&service, &LibraryService::customizationFinished,
            &loop, [&](const QString &id, bool ok, const QString &message) {
                if (id != request) return;
                signaled = true; success = ok; error = message;
                visibleAtSignal = committedState();
                loop.quit();
            });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(10000);
        service.applyCustomization(request, context.value(QStringLiteral("kind"), QStringLiteral("album")).toString(), context, draft);
        if (!signaled) loop.exec();
        QObject::disconnect(connection);
        check(signaled && success == expectedSuccess && visibleAtSignal, label);
        if (!signaled || success != expectedSuccess || !visibleAtSignal)
            fprintf(stderr, "request %s: signaled=%d success=%d visible=%d error=%s\n",
                    qPrintable(request), signaled, success, visibleAtSignal, qPrintable(error));
        if (!expectedSuccess) check(!error.isEmpty(), "failed Apply reports a usable error");
    };

    // Artwork matches are durable browsing choices, never tag edits. Start
    // before any explicit identity edits so even userEdited must stay false.
    const QString artGroup = QStringLiteral("77777777-7777-4777-8777-777777777777");
    const QVariantMap borrowedMatch{{"provider", "musicbrainz"}, {"providerId", artGroup},
        {"scope", "general"}, {"title", "Deliberately borrowed album"}};
    const QByteArray sourceBeforeArtwork = contents(media);
    const QString originalKey = AlbumArtService::cacheKey("Owner", "Original");
    apply({{"artist", "Owner"}, {"album", "Original"}},
          {{"identityMode", "keep"}, {"artworkMatch", borrowedMatch}}, true, [&] {
        const auto track = row();
        const auto identity = db.collectionCustomization("album", originalKey).value("identity").toMap();
        return track.album == "Original" && track.albumartist == "Owner" && track.title == "Fixture song"
            && !track.userEdited && track.year == 2008 && track.genre == "Alternative"
            && contents(media) == sourceBeforeArtwork && contents(originalPath) == originalBytes
            && identity.value("provider").toString().isEmpty()
            && identity.value("_artworkMatch").toMap().value("providerId") == artGroup;
    }, "artwork-only match persists without changing canonical identity, tags, file bytes or current cover");
    {
        LibraryDb reopened;
        check(reopened.open(data + QStringLiteral("/library.db"))
              && reopened.collectionCustomization("album", originalKey).value("identity").toMap()
                  .value("_artworkMatch").toMap().value("providerId") == artGroup,
              "artwork match survives a fresh database connection");
    }
    auto invalidMatch = borrowedMatch;
    invalidMatch.insert("scope", "exact"); // exact requires a release ID
    apply({{"artist", "Owner"}, {"album", "Original"}},
          {{"identityMode", "keep"}, {"artworkMatch", invalidMatch}}, false, [&] {
        return db.collectionCustomization("album", originalKey).value("identity").toMap()
            .value("_artworkMatch").toMap().value("scope") == "general"
            && contents(originalPath) == originalBytes && contents(media) == sourceBeforeArtwork;
    }, "invalid exact scope rejects the complete draft and preserves the earlier match");
    apply({{"artist", "Owner"}, {"album", "Original"}},
          {{"identityMode", "keep"}, {"resetArt", true}}, true, [&] {
        return db.collectionCustomization("album", originalKey).value("identity").toMap()
            .value("_artworkMatch").toMap().value("providerId") == artGroup
            && row().album == "Original" && !row().userEdited && contents(media) == sourceBeforeArtwork;
    }, "artwork reset preserves the confirmed artwork match without changing metadata");
    // Reinstall the original cache fixture for the existing rename assertions.
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    saveImage(originalPath, qRgb(230, 20, 20));

    const QString renamedPath = pathFor(QStringLiteral("New owner"), QStringLiteral("Renamed"));
    apply({{"artist", "Owner"}, {"album", "Original"}},
          {{"identityMode", "manual"}, {"fields", QVariantMap{{"album", "Renamed"},
            {"albumartist", "New owner"}}}, {"artUrl", QUrl::fromLocalFile(selectedPath).toString()}},
          true, [&] {
              const auto track = row();
              const QImage image(renamedPath);
              return track.album == QLatin1String("Renamed")
                  && track.albumartist == QLatin1String("New owner") && track.userEdited
                  && !image.isNull() && qBlue(image.pixel(0, 0)) > 200;
          }, "combined rename/art is committed at the new cache key before success");
    check(contents(originalPath) == originalBytes, "old cache entry remains intact");
    bool unrelated = false;
    for (const auto &track : db.allTracks())
        if (track.filepath == other.filepath) unrelated = track.album == QLatin1String("Original")
            && track.albumartist == QLatin1String("Other owner") && !track.userEdited;
    check(unrelated, "featured performer does not pull another owner's album into Apply");

    const QByteArray chosenBytes = contents(renamedPath);
    const QString carriedPath = pathFor(QStringLiteral("New owner"), QStringLiteral("Liner notes"));
    apply({{"artist", "New owner"}, {"album", "Renamed"}},
          {{"identityMode", "manual"}, {"fields", QVariantMap{{"album", "Liner notes"}}}},
          true, [&] {
              return row().album == QLatin1String("Liner notes")
                  && contents(carriedPath) == chosenBytes && !chosenBytes.isEmpty();
          }, "identity-only rename carries the chosen art without changing its bytes");

    apply({{"artist", "New owner"}, {"album", "Liner notes"}},
          {{"identityMode", "manual"}, {"fields", QVariantMap{{"album", "Must not save"}}},
           {"artUrl", QUrl::fromLocalFile(invalid.fileName()).toString()}}, false, [&] {
              return row().album == QLatin1String("Liner notes")
                  && contents(carriedPath) == chosenBytes
                  && !QFile::exists(pathFor(QStringLiteral("New owner"), QStringLiteral("Must not save")));
          }, "invalid artwork rejects the entire draft without metadata/cache changes");

    apply({{"artist", "New owner"}, {"album", "Liner notes"}},
          {{"identityMode", "keep"}, {"resetArt", true}}, true, [&] {
              const auto track = row();
              return track.album == QLatin1String("Liner notes")
                  && track.albumartist == QLatin1String("New owner") && track.userEdited
                  && track.year == 2008 && track.genre == QLatin1String("Alternative");
          }, "reset artwork preserves the committed music identity and tags");
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    const QImage restored(carriedPath);
    check(!restored.isNull() && qGreen(restored.pixel(0, 0)) > 200
          && qBlue(restored.pixel(0, 0)) < 80,
          "reset artwork resumes the real folder-art fallback");

    const auto edited = service.editTrackMetadata(target, {{"title", "Edited offline"}});
    check(edited.value("success").toBool() && !edited.value("fileWritten").toBool()
          && row().title == QLatin1String("Edited offline"),
          "track editor distinguishes a successful library-only save");
    const auto missing = service.editTrackMetadata(-1, {{"title", "Missing"}});
    check(!missing.value("success").toBool() && !missing.value("error").toString().isEmpty(),
          "missing track is a save failure, not library-only success");
    const auto blank = service.editTrackMetadata(target, {{"title", "  "}});
    check(!blank.value("success").toBool() && row().title == QLatin1String("Edited offline"),
          "blank titles are rejected at the save boundary");
    sqlite3 *faultDb = nullptr;
    const QByteArray dbPath = (data + QStringLiteral("/library.db")).toUtf8();
    const bool faultReady = sqlite3_open(dbPath.constData(), &faultDb) == SQLITE_OK
        && sqlite3_exec(faultDb, "CREATE TRIGGER reject_track_edit BEFORE UPDATE ON tracks "
                                "BEGIN SELECT RAISE(ABORT, 'fixture rejects edit'); END;",
                        nullptr, nullptr, nullptr) == SQLITE_OK;
    check(faultReady, "database failure fixture installed");
    if (faultReady) {
        const auto rejected = service.editTrackMetadata(target, {{"title", "Must not save"}});
        check(!rejected.value("success").toBool() && !rejected.value("error").toString().isEmpty()
              && row().title == QLatin1String("Edited offline"),
              "database failure returns error without changing metadata");
        sqlite3_exec(faultDb, "DROP TRIGGER reject_track_edit", nullptr, nullptr, nullptr);
    }
    if (faultDb) sqlite3_close(faultDb);

    // No DeviceService is constructed: this gate never probes USB or sends media.
    SyncEngine transfer;
    const QVariantList transferItems{QVariantMap{{"filepath", media}, {"title", "Fixture"}}};
    int addedSignals = 0;
    QObject::connect(&transfer, &SyncEngine::queueItemsAdded, &app, [&] { ++addedSignals; });
    const QVariantList refusals{
        transfer.addTracks(transferItems), transfer.addVideos(transferItems),
        transfer.addPhotos(transferItems), transfer.addPlaylist(QStringLiteral("Offline"), transferItems)};
    for (const auto &value : refusals) {
        const auto result = value.toMap();
        check(result.value("added").toInt() == 0 && result.value("rejected").toInt() == 1
              && !result.value("playlistQueued").toBool() && !result.value("error").toString().isEmpty(),
              "transfer add requires a connected Zune");
    }
    check(transfer.queue()->rowCount() == 0 && addedSignals == 0,
          "offline transfer attempts leave queue and playlist entries untouched");
    const double offlinePlaylist = service.createPlaylist(QStringLiteral("Offline mixtape"), {target});
    check(offlinePlaylist >= 0 && service.playlistTracks(offlinePlaylist).size() == 1,
          "playlist creation and membership remain available without a device");
    const QVariantMap mixtape{{"kind", "mixtape"}, {"id", offlinePlaylist}};
    const int artRevision = service.collectionArtRevision();
    apply(mixtape, {{"identityMode", "keep"}, {"artUrl", QUrl::fromLocalFile(selectedPath).toString()}}, true, [&] {
        const auto path = service.collectionArt("mixtape", QString::number(qint64(offlinePlaylist)));
        return !path.isEmpty() && !QImage(QUrl(path).toLocalFile()).isNull()
            && service.collectionArtRevision() > artRevision;
    }, "mixtape artwork saves offline and repaints before success");
    const auto savedMixtape = db.collectionCustomization("mixtape", QString::number(qint64(offlinePlaylist)));
    LibraryDb reopened;
    check(reopened.open(data + "/library.db") && reopened.collectionCustomization("mixtape", QString::number(qint64(offlinePlaylist))) == savedMixtape,
          "mixtape artwork survives database reopen");
    service.renamePlaylist(offlinePlaylist, "New mixtape name");
    check(db.collectionCustomization("mixtape", QString::number(qint64(offlinePlaylist))) == savedMixtape,
          "mixtape rename preserves its cover by stable ID");
    apply(mixtape, {{"identityMode", "keep"}, {"artUrl", QUrl::fromLocalFile(invalid.fileName()).toString()}}, false, [&] {
        return db.collectionCustomization("mixtape", QString::number(qint64(offlinePlaylist))) == savedMixtape;
    }, "invalid replacement leaves saved mixtape artwork untouched");
    apply(mixtape, {{"identityMode", "keep"}, {"resetArt", true}}, true, [&] {
        return service.collectionArt("mixtape", QString::number(qint64(offlinePlaylist))).isEmpty()
            && service.playlistTracks(offlinePlaylist).size() == 1;
    }, "reset mixtape artwork restores mosaic without touching membership");
    apply({{"kind", "genre"}, {"name", "Alternative"}},
          {{"identityMode", "keep"}, {"artUrl", QUrl::fromLocalFile(selectedPath).toString()}}, true, [&] {
        return !service.collectionArt("genre", "Alternative").isEmpty() && row().genre == "Alternative";
    }, "genre artwork saves without editing track metadata");
    apply({{"kind", "genre"}, {"name", "Missing genre"}},
          {{"identityMode", "keep"}, {"artUrl", QUrl::fromLocalFile(selectedPath).toString()}}, false, [&] {
        return db.collectionCustomization("genre", "Missing genre").isEmpty();
    }, "stale collection cannot acquire a phantom override");

    // Prime the real persistent provider cache through an injected transport;
    // Apply then uses its normal client, including authoritative detail lookup.
    const QString albumMbid = "11111111-1111-4111-8111-111111111111";
    MusicIdentityClient fixtureClient([&](const QUrl &, bool) {
        ArtworkHttp::Response response; response.status = 200;
        response.body = R"({"id":"11111111-1111-4111-8111-111111111111","title":"Matched album","first-release-date":"2019-03-01","artist-credit":[{"name":"Matched owner","artist":{"name":"Matched owner","id":"22222222-2222-4222-8222-222222222222"}}],"genres":[{"name":"Alternative","count":5}]})";
        return response;
    });
    QString providerError;
    const auto matchIdentity = fixtureClient.albumDetails(albumMbid, &providerError);
    check(providerError.isEmpty() && matchIdentity.value("mbid") == albumMbid, "isolated authoritative album details cached");
    apply({{"artist", "New owner"}, {"album", "Liner notes"}},
          {{"identityMode", "match"}, {"musicIdentity", matchIdentity},
           {"fields", QVariantMap{{"album", "Untrusted title"}, {"albumartist", "Untrusted owner"}}},
           {"artUrl", QUrl::fromLocalFile(selectedPath).toString()}}, true, [&] {
        const auto track = row();
        const auto saved = db.collectionCustomization("album", AlbumArtService::cacheKey("Matched owner", "Matched album"));
        return track.album == "Matched album" && track.albumartist == "Matched owner" && track.year == 2019 && track.userEdited
            && saved.value("identity").toMap().value("mbid") == albumMbid
            && !QImage(saved.value("artPath").toString()).isNull();
    }, "matched album applies authoritative metadata, durable identity and art atomically");
    LibTrack rescan = row(); rescan.album = "Scanner overwrite"; rescan.year = 2001; rescan.mtime++;
    db.upsertTrack(rescan);
    check(row().album == "Matched album" && row().year == 2019,
          "rescan cannot overwrite a chosen database identity");
    apply({{"artist", "Matched owner"}, {"album", "Matched album"}},
          {{"identityMode", "match"}, {"musicIdentity", QVariantMap{{"mbid", "invalid"}}}}, false, [&] {
        return row().album == "Matched album";
    }, "invalid music match fails before changing the library");
    const QString artistMbid = "22222222-2222-4222-8222-222222222222";
    MusicIdentityClient artistFixture([](const QUrl &, bool) {
        ArtworkHttp::Response response; response.status = 200;
        response.body = R"({"id":"22222222-2222-4222-8222-222222222222","name":"The composer","disambiguation":"game composer","type":"Person","country":"AU"})";
        return response;
    });
    const auto artistIdentity = artistFixture.artistDetails("musicbrainz", artistMbid, &providerError);
    apply({{"kind", "artist"}, {"name", "Matched owner"}},
          {{"identityMode", "match"}, {"musicIdentity", artistIdentity},
           {"artUrl", QUrl::fromLocalFile(selectedPath).toString()}}, true, [&] {
        const auto saved = service.customizeContext("artist", {{"name", "The composer"}});
        const auto album = service.customizeContext("album", {{"artist", "The composer"}, {"album", "Matched album"}});
        return row().albumartist == "The composer"
            && saved.value("musicIdentity").toMap().value("mbid") == artistMbid
            && album.value("musicIdentity").toMap().value("mbid") == albumMbid
            && album.value("customArt").toBool();
    }, "artist matching preserves album identities and custom covers under their new owner");
    const QVariantMap destinationIdentity{{"provider", "manual"}, {"overview", "Existing artist notes"}};
    db.setCollectionCustomization("artist", ArtistImageService::cacheKey("Other owner"), destinationIdentity, {});
    apply({{"kind", "artist"}, {"name", "The composer"}},
          {{"identityMode", "manual"}, {"fields", QVariantMap{{"artist", "Other owner"}}}}, false, [&] {
        return row().albumartist == "The composer"
            && db.collectionCustomization("artist", ArtistImageService::cacheKey("Other owner")).value("identity").toMap() == destinationIdentity
            && service.customizeContext("album", {{"artist", "The composer"}, {"album", "Matched album"}}).value("musicIdentity").toMap().value("mbid") == albumMbid;
    }, "artist name collision rolls back album migrations and preserves the destination choices");
    QVariantList devicePlaylists{
        QVariantMap{{"itemId", 100}, {"name", "Keep"}, {"count", 3}, {"trackIds", QVariantList{1, 2, 3}}},
        QVariantMap{{"itemId", 200}, {"name", "Delete"}, {"count", 1}, {"trackIds", QVariantList{4}}}};
    pruneDevicePlaylists(devicePlaylists, {2, 200});
    check(devicePlaylists.size() == 1 && devicePlaylists[0].toMap().value("count").toInt() == 2
          && devicePlaylists[0].toMap().value("trackIds").toList() == QVariantList{1, 3},
          "confirmed device deletions prune only the deleted playlist/member snapshots");
    // Simulate a scanner deleting the DB row before its queued UI reload.
    // LibraryService deliberately still holds its original track snapshot.
    sqlite3 *scanDb = nullptr;
    const auto deleteSql = QStringLiteral("DELETE FROM tracks WHERE id=%1").arg(target).toUtf8();
    const bool removed = sqlite3_open(dbPath.constData(), &scanDb) == SQLITE_OK
        && sqlite3_exec(scanDb, deleteSql.constData(), nullptr, nullptr, nullptr) == SQLITE_OK;
    const QByteArray mediaBefore = contents(media);
    const auto vanished = service.editTrackMetadata(target, {{"title", "Vanished while editing"}});
    check(removed && !vanished.value("success").toBool()
          && !vanished.value("fileWritten").toBool() && !vanished.value("error").toString().isEmpty()
          && contents(media) == mediaBefore,
          "a row removed before the UI refresh is a save failure and leaves file tags alone");
    if (scanDb) sqlite3_close(scanDb);
    printf("Apply integration: %s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
