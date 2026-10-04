// Deliberate, bounded live provider check. No library or device is constructed.
// The runner supplies disposable XDG roots and never reads user API settings.
#include "library/MusicIdentityClient.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZuunedCustomizeFixture");
    app.setApplicationName("LiveMusicProviders");
    const QString root = qEnvironmentVariable("CUSTOMIZE_BACKEND_ROOT");
    if (!root.startsWith("/tmp/zuuned-customize-backend-")
        || !QStandardPaths::writableLocation(QStandardPaths::CacheLocation).startsWith(root + '/')) return 2;
    MusicIdentityClient client;
    QString error;
    // Reproduce the reported same-name branch with the actual v3 response.
    MusicIdentityClient inspected([](const QUrl &url, bool mb) {
        const auto response = ArtworkHttp::get(url, mb);
        if (url.host() == "webservice.fanart.tv")
            printf("Fanart live reply: HTTP %d, %lld bytes, empty object=%s\n", response.status,
                   qint64(response.body.size()), response.body.trimmed() == "{}" ? "yes" : "no");
        return response;
    });
    const auto absent = inspected.fanartArtistUrls("2fd39f0c-9ce1-4493-92e5-5e734c1aa05b", &error);
    printf("Fanart second Billie Eilish identity: %lld portraits; %s\n", qint64(absent.size()), qPrintable(error));
    if (!error.isEmpty()) return 1;
    const auto billie = inspected.searchFanartArtistUrls("Billie Eilish", &error);
    printf("Fanart combined Billie Eilish search: %lld portraits; %s\n", qint64(billie.size()), qPrintable(error));
    if (!error.isEmpty() || billie.isEmpty()) return 1;
    auto artists = client.searchArtists("Christopher Larkin", "musicbrainz", &error);
    printf("MusicBrainz disambiguation: %lld matches; %s\n", qint64(artists.size()), qPrintable(error));
    if (!error.isEmpty() || artists.isEmpty()) return 1;
    for (const auto &value : artists) {
        const auto row = value.toMap();
        printf("  %s: %s\n", qPrintable(row.value("title").toString()), qPrintable(row.value("subtitle").toString()));
    }
    const auto albums = client.searchAlbums("Skyfall", "Adele", &error);
    printf("MusicBrainz album search: %lld matches; %s\n", qint64(albums.size()), qPrintable(error));
    if (!error.isEmpty() || albums.isEmpty()) return 1;
    const auto album = client.albumDetails(albums.first().toMap().value("mbid").toString(), &error);
    printf("Album details: %s / %s; %s\n", qPrintable(album.value("title").toString()),
           qPrintable(album.value("artist").toString()), qPrintable(error));
    if (!error.isEmpty() || album.isEmpty()) return 1;
    const auto portraits = client.fanartArtistUrls(album.value("artistMbid").toString(), &error);
    printf("Fanart artist portraits: %lld; %s\n", qint64(portraits.size()), qPrintable(error));
    if (!error.isEmpty() || portraits.isEmpty()) return 1;
    const auto covers = client.caaAlbumUrls(album.value("mbid").toString(), &error);
    printf("Cover Art Archive exact album covers: %lld; %s\n", qint64(covers.size()), qPrintable(error));
    return error.isEmpty() && !covers.isEmpty() ? 0 : 1;
}
