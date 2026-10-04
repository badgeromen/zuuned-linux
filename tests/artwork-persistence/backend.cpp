#include "library/LibraryDb.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <cstdio>

static int checks = 0, failures = 0;
static void check(bool ok, const char *label) {
    ++checks;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", label);
}
static QString key(const QString &name) {
    return QString::fromLatin1(QCryptographicHash::hash(name.toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    int fixture = 0;
    auto exercise = [&](const QVariantMap &identity, const QString &artPath, bool pinned, bool manual = false) {
        LibraryDb db;
        if (!db.open(temporary.path() + '/' + QString::number(++fixture) + ".db")) return false;
        LibTrack track;
        track.filepath = "/fixture/music.flac";
        track.artist = track.albumartist = "Old artist";
        track.album = "Old album";
        track.title = "Old title";
        track.probeVersion = 1;
        if (!db.upsertTrack(track)) return false;
        if (manual && !db.updateTrackMetadata(db.allTracks().first().id, track, true)) return false;
        if (!db.setCollectionCustomization("artist", key(track.albumartist), identity, artPath)
            || !db.setCollectionCustomization("album", key(track.albumartist + '\n' + track.album), identity, artPath)) return false;
        track.artist = track.albumartist = "New artist";
        track.album = "New album";
        track.title = "New title";
        track.probeVersion = 2;
        if (!db.upsertTrack(track)) return false;
        const auto actual = db.allTracks().first();
        const bool metadata = pinned
            ? actual.artist == "Old artist" && actual.albumartist == "Old artist" && actual.album == "Old album"
            : actual.artist == "New artist" && actual.albumartist == "New artist" && actual.album == "New album";
        const bool title = actual.title == (manual ? "Old title" : "New title");
        const bool preserved = db.collectionCustomization("artist", key("Old artist")).value("identity").toMap() == identity
            && db.collectionCustomization("album", key("Old artist\nOld album")).value("artPath").toString() == artPath;
        return metadata && title && preserved && actual.probeVersion == 2;
    };
    const QVariantMap discovery{{"status", "ready"}, {"identity", QVariantMap{{"providerId", "automatic-id"}}}};
    const QVariantMap match{{"choice", "manual"}, {"providerId", "art-only-id"}};
    check(exercise({}, {}, false), "ordinary reprobe adopts changed file metadata");
    check(exercise({{"_artworkDiscovery", discovery}}, {}, false), "automatic discovery persistence does not pin artist or album grouping");
    check(exercise({{"_artworkMatch", match}}, {}, false), "artwork-only provider choice does not pin music metadata");
    check(exercise({{"_artworkDiscovery", discovery}, {"_artworkMatch", match}}, {}, false), "combined artwork state stays separate from metadata choices");
    check(exercise({{"_artworkDiscovery", discovery}, {"providerId", "manual-metadata-id"}}, {}, true), "legacy manual provider identity remains protected");
    check(exercise({{"_artworkMatch", match}}, "/fixture/custom.jpg", true), "existing custom-art collection pin remains protected");
    check(exercise({{"_artworkDiscovery", discovery}}, {}, true, true), "manually edited track metadata remains protected");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
