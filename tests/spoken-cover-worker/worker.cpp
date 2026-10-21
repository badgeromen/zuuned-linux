// Audiobook and podcast covers through the real DeviceWorker send paths, with
// every device call wrapped. A cover must reach zune_brand as a baseline
// 4:2:0 JPEG inside the limits the device declares for the object.
#include "DeviceWorker.h"
#include "sync/DeviceCover.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QRandomGenerator>
#include <cstdio>
extern "C" {
#include "zune.h"
}

struct SpokenCoverWorkerTestAccess {
    static void attach(DeviceWorker &w) { w.m_dev = reinterpret_cast<ZuneDevice *>(quintptr(1)); }
};

namespace {
uint32_t limitWidth, limitHeight, limitBytes, foundPart;
bool limitsFail, brandFails;
int limitCalls, brandCalls, smuggled;
uint32_t brandedItem;
QByteArray branded;
QString warning;
bool done, reused;

void reset(uint32_t width, uint32_t height, uint32_t bytes) {
    limitWidth = width; limitHeight = height; limitBytes = bytes;
    foundPart = 0; limitsFail = brandFails = false;
    limitCalls = brandCalls = smuggled = 0; brandedItem = 0;
    branded.clear(); warning.clear(); done = reused = false;
}

// Frame size, scan type and chroma sampling from a JPEG's SOF marker.
struct Jpeg { int width = 0, height = 0; bool baseline = false, yuv420 = false; };
Jpeg inspect(const QByteArray &d) {
    Jpeg j;
    const auto *p = reinterpret_cast<const unsigned char *>(d.constData());
    const qsizetype n = d.size();
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return j;
    for (qsizetype i = 2; i + 9 < n;) {
        if (p[i] != 0xFF) { ++i; continue; }
        const unsigned char m = p[i + 1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) { i += m == 0xFF ? 1 : 2; continue; }
        if (m >= 0xC0 && m <= 0xC3) {
            j.height = p[i + 5] << 8 | p[i + 6];
            j.width = p[i + 7] << 8 | p[i + 8];
            j.baseline = m == 0xC0;
            j.yuv420 = p[i + 9] == 3 && i + 11 < n && p[i + 11] == 0x22;
            return j;
        }
        i += 2 + (p[i + 2] << 8 | p[i + 3]);
    }
    return j;
}

// A detailed cover the size of the stored audiobook covers (1600x1600 at
// quality 93, which Qt writes as 4:4:4).
QImage artwork(int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, width, height);
    gradient.setColorAt(0, QColor(180, 30, 40));
    gradient.setColorAt(1, QColor(20, 40, 160));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QPen(Qt::yellow, 9));
    for (int x = 0; x < width; x += 37) painter.drawLine(x, 0, width - x, height);
    return image;
}

bool save(const QImage &image, const QString &path, int quality) {
    QImageWriter writer(path);
    writer.setQuality(quality);
    return writer.write(image);
}

// A landscape JPEG whose EXIF orientation 6 says to show it turned 90
// degrees. Qt cannot write the tag (it rotates the pixels), so insert it.
bool saveSideways(const QImage &image, const QString &path) {
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "JPEG", 90)) return false;
    static const char exif[] = "\xFF\xE1\x00\x22" "Exif\0\0" "II*\0\x08\0\0\0"
                               "\x01\0" "\x12\x01\x03\0\x01\0\0\0\x06\0\0\0" "\0\0\0\0";
    jpeg.insert(2, QByteArray(exif, sizeof(exif) - 1));
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(jpeg) == jpeg.size();
}
}

extern "C" {
void __wrap_zune_clear_abort(ZuneDeviceHandle) {}
int __wrap_zune_find_audiobook_part(ZuneDeviceHandle, const ZuneAudiobookPartInput *, uint32_t *folder,
                                    uint32_t *out) { *folder = 77; *out = foundPart; return 0; }
int __wrap_zune_smuggle_audiobook_part(ZuneDeviceHandle, const char *, const ZuneAudiobookPartInput *,
                                       uint32_t *out) { ++smuggled; *out = 4242; return 0; }
int __wrap_zune_find_podcast_episode(ZuneDeviceHandle, const char *, const char *, uint32_t *parent,
                                     uint32_t *series, uint32_t *episode) {
    *parent = 88; *series = 99; *episode = 0; return 0;
}
int __wrap_zune_get_podcast_series(ZuneDeviceHandle, ZunePodcastSeries **out, int *count) {
    *out = nullptr; *count = 0; return 0;
}
void __wrap_zune_free_podcast_series(ZunePodcastSeries *, int) {}
int __wrap_zune_smuggle_podcast_episode(ZuneDeviceHandle, uint32_t, const char *,
                                        const ZunePodcastEpisodeInput *, uint32_t *out) {
    ++smuggled; *out = 5151; return 0;
}
int __wrap_zune_get_brand_limits(ZuneDeviceHandle, uint32_t, uint32_t *w, uint32_t *h, uint32_t *b) {
    ++limitCalls;
    if (limitsFail) { *w = *h = *b = 0; return -1; }
    *w = limitWidth; *h = limitHeight; *b = limitBytes;
    return 0;
}
int __wrap_zune_brand(ZuneDeviceHandle, uint32_t item, const uint8_t *data, size_t len) {
    ++brandCalls; brandedItem = item;
    branded = QByteArray(reinterpret_cast<const char *>(data), qsizetype(len));
    return brandFails ? -1 : 0;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto root = qEnvironmentVariable("SPOKEN_COVER_TEST_ROOT");
    if (!root.startsWith("/tmp/zuuned-spoken-cover-worker-")) return 2;
    int checks = 0, failures = 0;
    auto check = [&](bool yes, const QString &name) {
        ++checks; if (!yes) ++failures;
        std::printf("%s %s\n", yes ? "PASS" : "FAIL", qPrintable(name));
    };

    const QString big = root + "/big.jpg", wide = root + "/wide.jpg", small = root + "/small.jpg";
    const QString turned = root + "/turned.jpg", clear = root + "/clear.png", noisy = root + "/noisy.jpg";
    const QString broken = root + "/broken.jpg", audio = root + "/part.mp3";
    if (!save(artwork(1600, 1600), big, 93) || !save(artwork(1600, 1000), wide, 90)
        || !save(artwork(200, 120), small, 90)
        || !saveSideways(artwork(400, 200), turned)) return 2;
    QImage alpha(300, 300, QImage::Format_ARGB32);
    alpha.fill(Qt::transparent);
    { QPainter p(&alpha); p.fillRect(100, 100, 100, 100, QColor(250, 250, 250)); }
    if (!alpha.save(clear)) return 2;
    QImage noise(800, 800, QImage::Format_RGB32);
    for (int y = 0; y < noise.height(); ++y)
        for (int x = 0; x < noise.width(); ++x) noise.setPixel(x, y, QRandomGenerator::global()->generate() | 0xFF000000u);
    if (!noise.save(noisy, "JPEG", 95)) return 2;
    { QFile f(broken); if (!f.open(QIODevice::WriteOnly) || f.write("not a picture") < 0) return 2; }
    { QFile f(audio); if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray(4096, 'a')) < 0) return 2; }
    QFile bigFile(big);
    const auto original = bigFile.open(QIODevice::ReadOnly) ? inspect(bigFile.readAll()) : Jpeg();
    check(original.width == 1600 && !original.yuv420,
          "fixture: a 1600x1600 4:4:4 cover like the stored audiobook covers");
    check(QImageReader(turned).transformation() == QImageIOHandler::TransformationRotate90,
          "fixture: a cover stored sideways with an EXIF rotation");

    DeviceWorker w;
    SpokenCoverWorkerTestAccess::attach(w);
    QObject::connect(&w, &DeviceWorker::audiobookSendDone, &app,
                     [&](const QString &, bool ok, quint32, const QString &error, bool reuse) {
                         done = ok; warning = error; reused = reuse; });
    QObject::connect(&w, &DeviceWorker::podcastSendDone, &app,
                     [&](const QString &, bool ok, quint32, const QString &error, bool reuse, bool) {
                         done = ok; warning = error; reused = reuse; });
    auto book = [&](const QString &cover) {
        const QVariantMap part{{"bookKey", QString(64, 'a')}, {"album", "Book"}, {"artist", "Author"},
                               {"title", "Part 1"}, {"partNumber", 1}, {"durationMs", 60000},
                               {"posterPath", cover}};
        w.doSendAudiobook("entry", audio, part);
    };
    auto episode = [&](const QString &cover) {
        const QVariantMap row{{"feedUrl", "https://example.invalid/feed"}, {"sourceUrl", "https://example.invalid/1.mp3"},
                              {"title", "Episode"}, {"artist", "Host"}, {"album", "Show"},
                              {"durationMs", 60000}, {"posterPath", cover}};
        w.doSendPodcast("entry", audio, row);
    };

    reset(240, 240, 1048576); book(big);
    auto j = inspect(branded);
    check(done && warning.isEmpty() && brandCalls == 1 && brandedItem == 4242 && limitCalls == 1,
          "an audiobook part is branded after its upload, with the limits read for that part");
    check(j.width == 240 && j.height == 240 && j.baseline && j.yuv420,
          QString("a 1600x1600 cover reaches a Zune 30 as baseline 4:2:0 at 240x240 (got %1x%2)").arg(j.width).arg(j.height));
    check(branded.size() < QFile(big).size() / 10 && branded.size() <= 1048576,
          QString("and is a fraction of the original's size (%1 of %2 bytes)").arg(branded.size()).arg(QFile(big).size()));

    reset(480, 272, 524288); episode(big);
    j = inspect(branded);
    check(done && brandedItem == 99 && j.width == 272 && j.height == 272 && warning.isEmpty(),
          QString("a podcast show cover fits 480x272 declared for the series (got %1x%2)").arg(j.width).arg(j.height));

    reset(1024, 1024, 0); book(big);
    j = inspect(branded);
    check(j.width == 480 && j.height == 480, "a declared limit above any Zune screen is capped at 480");

    reset(0, 0, 0); book(big);
    j = inspect(branded);
    check(j.width == 240 && j.height == 240 && branded.size() <= DeviceCover::FallbackBytes,
          "no declared limit falls back to 240x240 within 1 MiB");

    reset(480, 480, 1048576); limitsFail = true; book(big);
    j = inspect(branded);
    check(done && brandCalls == 1 && j.width == 240 && warning.isEmpty(),
          "an unreadable limit still sends a 240x240 cover; the part stays synced");

    reset(240, 240, 1048576); book(wide);
    j = inspect(branded);
    check(j.width == 240 && j.height == 150, QString("a wide cover keeps its shape (got %1x%2)").arg(j.width).arg(j.height));

    reset(240, 240, 1048576); book(small);
    j = inspect(branded);
    check(j.width == 200 && j.height == 120, "a small cover is never enlarged");

    reset(480, 272, 1048576); book(turned);
    j = inspect(branded);
    check(j.width == 136 && j.height == 272,
          QString("a sideways-stored cover is turned upright and fits 480x272 (got %1x%2)").arg(j.width).arg(j.height));

    reset(240, 240, 1048576); book(clear);
    QImage seen = QImage::fromData(branded, "JPEG");
    check(!seen.isNull() && qGray(seen.pixel(5, 5)) < 16 && qGray(seen.pixel(120, 120)) > 230,
          "transparency becomes black and opaque areas keep their color");

    QString error;
    const QByteArray full = DeviceCover::prepare(noisy, 240, 240, 1 << 30, &error);
    const QByteArray lower = DeviceCover::prepare(noisy, 240, 240, full.size() - 1, &error);
    check(!full.isEmpty() && !lower.isEmpty() && lower.size() < full.size() && inspect(lower).width == 240,
          QString("a byte limit lowers quality until the cover fits (%1 -> %2 bytes)").arg(full.size()).arg(lower.size()));

    reset(240, 240, 2000); book(noisy);
    check(done && brandCalls == 0 && warning == "Book part synced; its cover could not be prepared.",
          "a cover that cannot fit is reported, not sent; the part stays synced");

    reset(240, 240, 1048576); book(broken);
    check(done && brandCalls == 0 && warning == "Book part synced; its cover could not be prepared.",
          "an undecodable cover is reported, not sent");

    reset(240, 240, 1048576); book(root + "/missing.jpg");
    check(done && limitCalls == 0 && brandCalls == 0 && warning.isEmpty(), "a part without a cover file is unchanged");

    reset(240, 240, 1048576); brandFails = true; book(big);
    check(done && warning == "Book part synced; this device did not accept its cover.",
          "a refused part cover keeps the existing warning");

    reset(240, 240, 1048576); brandFails = true; episode(big);
    check(done && warning == "Episode synced; this device did not accept the show cover.",
          "a refused show cover keeps the existing warning");

    reset(240, 240, 1048576); foundPart = 31337; book(big);
    check(done && reused && smuggled == 0 && brandCalls == 0, "an existing part is reused without sending a cover");

    check(DeviceCover::edge(0) == 240 && DeviceCover::edge(100) == 100 && DeviceCover::edge(240) == 240
          && DeviceCover::edge(4096) == 480, "declared edges map to 240 (none), as declared, or at most 480");
    check(DeviceCover::prepare(big, 0, 240, 1000, &error).isEmpty() && !error.isEmpty(), "invalid limits are refused");

    std::printf("Spoken covers: %d checks, %d failures; no USB\n", checks, failures);
    return failures ? 1 : 0;
}
