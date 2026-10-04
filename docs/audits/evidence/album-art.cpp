#include "library/AlbumArtService.cpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

static int extracted = 0;
extern "C" int zuuned_extract_art(const char *path, const char *dest, int) {
    ++extracted;
    if (!QString::fromUtf8(path).endsWith(QStringLiteral("with-art.flac"))) return -1;
    QImage embedded(12, 12, QImage::Format_RGB32); embedded.fill(Qt::green);
    return embedded.save(QString::fromUtf8(dest), "JPEG") ? 0 : -1;
}
static void touch(const QString &path) { QFile file(path); if (!file.open(QIODevice::WriteOnly)) std::abort(); }
static void image(const QString &path, QColor color) {
    QImage img(12, 12, QImage::Format_RGB32); img.fill(color); if (!img.save(path)) std::abort();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir scratch;
    qputenv("XDG_CACHE_HOME", scratch.filePath("cache").toUtf8());
    QDir().mkpath(scratch.filePath("arbitrary"));
    const auto arbitrary = scratch.filePath("arbitrary/with-art.flac"); touch(arbitrary);
    image(scratch.filePath("arbitrary/back.jpg"), Qt::red);
    auto out = scratch.filePath("out.jpg");
    bool ok = resolveArt(arbitrary, out);
    printf("arbitrary sidecar chosen before available embedded cover: %s (embedded calls=%d, red=%d)\n",
        ok && extracted == 0 ? "CONFIRMED" : "UNEXPECTED", extracted, QImage(out).pixelColor(0,0).red());
    QDir().mkpath(scratch.filePath("Album/CD1"));
    const auto disc = scratch.filePath("Album/CD1/no-art.flac"); touch(disc);
    image(scratch.filePath("Album/cover.jpg"), Qt::blue);
    printf("album cover one level above CD1 is missed: %s\n", !resolveArt(disc, scratch.filePath("disc.jpg")) ? "CONFIRMED" : "UNEXPECTED");
    QDir().mkpath(scratch.filePath("no-art"));
    const auto empty = scratch.filePath("no-art/no-art.flac"); touch(empty);
    const auto full = scratch.filePath("no-art/with-art.flac"); touch(full);
    AlbumArtService service;
    service.requestArt("Owner", "Miss", empty);
    QThreadPool::globalInstance()->waitForDone();
    const int before = extracted;
    service.requestArt("Owner", "Miss", full);
    QThreadPool::globalInstance()->waitForDone();
    printf("first-track miss prevents trying second track with embedded art: %s\n",
        service.cachedArtPath("Owner", "Miss").isEmpty() && extracted == before ? "CONFIRMED" : "UNEXPECTED");
    service.requestArt("Owner", "Cached", arbitrary);
    QThreadPool::globalInstance()->waitForDone();
    image(scratch.filePath("arbitrary/back.jpg"), Qt::blue);
    service.requestArt("Owner", "Cached", arbitrary);
    QThreadPool::globalInstance()->waitForDone();
    printf("automatic cache persists after source sidecar change: %s\n",
        QImage(service.cachedArtPath("Owner", "Cached")).pixelColor(0,0).red() > 200 ? "CONFIRMED" : "UNEXPECTED");
}
