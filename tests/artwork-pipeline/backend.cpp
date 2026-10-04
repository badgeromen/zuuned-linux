#include "artwork/ArtworkRequest.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>

using ArtworkPrint::Pipeline;
namespace {
int assertions = 0, failures = 0;
void check(bool condition, const char *description)
{
    ++assertions;
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}
bool until(const std::function<bool()> &condition, int milliseconds = 12000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}
QByteArray bytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void openFixture(QFile &file, QIODevice::OpenMode mode)
{
    if (!file.open(mode)) qFatal("could not open isolated fixture file");
}
QImage sample(int width = 160, int height = 112)
{
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, qRgba(x * 255 / width, y * 255 / height,
                (x * 13 + y * 7) % 256, (x + y) % 256));
    return image;
}
QString put(const QString &directory, const QString &name, const QImage &image = sample())
{
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(name);
    if (!image.save(path)) std::fprintf(stderr, "fixture image write failed\n");
    return path;
}
void begin(ArtworkRequest &request, const QString &path, const QString &style = "cleanInk")
{
    request.setStyle(style);
    request.setSource(QUrl::fromLocalFile(path));
}
bool ready(ArtworkRequest &request)
{
    return until([&] { return !request.busy(); }) && !request.result().isEmpty() && request.error().isEmpty();
}
Pipeline::Config config(const QString &root, const QString &name)
{
    Pipeline::Config cfg;
    cfg.cacheDirectory = QDir(root).filePath(name);
    return cfg;
}
struct Counter {
    std::atomic_int calls = 0, active = 0, maximum = 0;
    std::atomic_bool block = false;
    QSemaphore entered, proceed;
    ArtworkPrint::Result operator()(const QImage &image, const ArtworkPrint::Options &options)
    {
        ++calls;
        const int concurrent = ++active;
        int previous = maximum;
        while (concurrent > previous && !maximum.compare_exchange_weak(previous, concurrent)) {}
        if (block) {
            entered.release();
            proceed.tryAcquire(1, 8000);
        }
        auto result = ArtworkPrint::render(image, options);
        --active;
        return result;
    }
    Pipeline::Renderer renderer()
    {
        return [this](const QImage &image, const ArtworkPrint::Options &options) { return (*this)(image, options); };
    }
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ZuunedArtworkFixture");
    QCoreApplication::setApplicationName("ArtworkPipeline");
    QTemporaryDir temporary;
    const QString root = temporary.path();
    const QString inputs = root + "/inputs";
    const QString source = put(inputs, "art ?#.png");
    const QByteArray original = bytes(source);
    {
        Counter count;
        Pipeline pipeline(config(root, "normal"), nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        check(request.style() == "original" && request.detail() == 100 && request.texture() == 14
            && request.dotSize() == 17 && !request.monochrome() && !request.busy(),
            "production defaults and original pass-through");
        request.setSource(QUrl::fromLocalFile(source));
        check(!request.busy() && request.result().isEmpty() && count.calls == 0,
            "original performs no decode or render");
        request.setStyle("clean");
        check(ready(request) && request.style() == "cleanInk", "local artwork resolves through style aliases");
        const QUrl first = request.result();
        const QImage output(first.toLocalFile());
        bool alpha = output.size() == sample().size();
        const QImage input(source);
        for (int y = 0; y < output.height() && alpha; ++y)
            for (int x = 0; x < output.width(); ++x)
                if (output.pixelColor(x, y).alpha() != input.pixelColor(x, y).alpha()) alpha = false;
        check(alpha && bytes(source) == original, "output preserves every source alpha value without rewriting original bytes");
        request.setDotSize(93);
        request.setMonochrome(true);
        request.setTexture(99);
        check(!request.busy() && request.result() == first && count.calls == 1,
            "Halftone-only controls leave Clean ink output and cache intact");
        QUrl revision = QUrl::fromLocalFile(source);
        revision.setQuery("v=42");
        request.setSource(revision);
        check(ready(request) && request.result() == first && count.calls == 1,
            "query revisions are correlated while escaped file characters remain intact");
        request.setStyle("halftone");
        check(ready(request), "Halftone renders through shared pipeline");
        const QUrl half = request.result();
        request.setDotSize(17);
        check(ready(request) && request.result() != half, "Halftone dot-size settings produce distinct cache variants");
        request.setStyle("worn");
        check(ready(request) && request.style() == "wornPrint", "Worn print aliases resolve");
        request.setDetail(-1);
        request.setTexture(101);
        request.setDotSize(200);
        check(request.detail() == 0 && request.texture() == 100 && request.dotSize() == 100 && ready(request),
            "request controls clamp and publish the final coalesced settings");
        const int calls = count.calls;
        for (const auto *url : {"https://example.invalid/cover.png", "qrc:/cover.png", "image://device/13", "file://remote/share/a.png"}) {
            request.setSource(QUrl(url));
            check(!request.busy() && request.result().isEmpty() && request.error().isEmpty(),
                "nonlocal source passes through with no error or work");
        }
        check(count.calls == calls, "remote, resource and device-provider URLs never reach the worker");
    }
    {
        const QString big = put(inputs, "large.png", sample(1200, 720));
        Pipeline pipeline(config(root, "canonical"));
        ArtworkRequest request(pipeline);
        begin(request, big, "wornPrint");
        check(ready(request) && QImage(request.result().toLocalFile()).size() == QSize(800, 480),
            "all views receive one canonical 800px render independent of displayed size");
        QImageReader reader(big);
        reader.setAutoTransform(true);
        reader.setScaledSize(QSize(800, 480));
        const auto expected = ArtworkPrint::render(reader.read(), {ArtworkPrint::Style::WornPrint, 1, .14, .17, false}).image;
        check(QImage(request.result().toLocalFile()).convertToFormat(QImage::Format_ARGB32)
            == expected.convertToFormat(QImage::Format_ARGB32), "canonical Worn pixels exactly match the unchanged renderer");
    }
    QString persisted;
    {
        Pipeline pipeline(config(root, "persistent"));
        ArtworkRequest request(pipeline);
        begin(request, source);
        check(ready(request), "cold persistent cache completes");
        persisted = request.result().toLocalFile();
    }
    {
        Counter count;
        Pipeline pipeline(config(root, "persistent"), nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        begin(request, source);
        check(ready(request) && request.result().toLocalFile() == persisted && count.calls == 0,
            "a fresh service reuses a valid persistent PNG without invoking the renderer");
        QFile damaged(persisted);
        openFixture(damaged, QIODevice::WriteOnly | QIODevice::Truncate);
        damaged.write("broken PNG");
        damaged.close();
        request.refresh();
        check(ready(request) && !QImage(persisted).isNull() && count.calls == 0,
            "corrupt disk variant repairs atomically from bounded decoded memory cache");
    }
    {
        Counter count;
        Pipeline pipeline(config(root, "replace"), nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        QImage image(96, 96, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString path = put(inputs, "replace.bmp", image);
        const auto oldMtime = QFileInfo(path).lastModified();
        begin(request, path);
        check(ready(request), "initial same-path source completes");
        const QUrl first = request.result();
        image.fill(Qt::blue);
        image.save(path);
        QFile file(path);
        openFixture(file, QIODevice::ReadOnly);
        file.setFileTime(oldMtime, QFileDevice::FileModificationTime);
        file.close();
        request.refresh();
        check(ready(request) && request.result() != first
            && QImage(request.result().toLocalFile()).pixelColor(40, 40).blue() > 200,
            "explicit refresh detects same-size replacement even with restored modification time");
        QFile broken(path);
        openFixture(broken, QIODevice::WriteOnly | QIODevice::Truncate);
        broken.write("not an image");
        broken.close();
        request.refresh();
        check(until([&] { return !request.busy(); }) && request.result().isEmpty() && !request.error().isEmpty(),
            "malformed replacement clears treatment and exposes a useful fallback error");
        image.fill(Qt::green);
        image.save(path);
        request.refresh();
        check(ready(request), "repairing the same source clears its previous error");
    }
    {
        Counter count;
        count.block = true;
        Pipeline pipeline(config(root, "reread"), nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString path = put(inputs, "reread.bmp", image);
        begin(request, path);
        check(until([&] { return count.entered.available() >= 1; }), "source snapshot held during render");
        image.fill(Qt::blue);
        image.save(path);
        count.block = false;
        count.proceed.release(4);
        check(ready(request) && count.calls == 2
            && QImage(request.result().toLocalFile()).pixelColor(60, 60).blue() > 200,
            "worker itself detects mid-render source replacement and retries the fresh snapshot");
    }
    {
        Counter count;
        count.block = true;
        Pipeline pipeline(config(root, "coalesced"), nullptr, count.renderer());
        ArtworkRequest first(pipeline), second(pipeline);
        begin(first, source);
        begin(second, source);
        check(until([&] { return count.entered.available() == 1; }), "shared worker began for simultaneous subscribers");
        first.setStyle("original");
        count.block = false;
        count.proceed.release(4);
        check(ready(second) && first.result().isEmpty() && count.calls == 1,
            "cancelling one subscriber preserves the other and renders identical requests once");
    }
    {
        Counter count;
        count.block = true;
        Pipeline pipeline(config(root, "content-coalesced"), nullptr, count.renderer());
        const QString copy = put(inputs, "same-content.png");
        ArtworkRequest first(pipeline), second(pipeline);
        begin(first, source);
        begin(second, copy);
        check(until([&] { return count.entered.available() >= 1; }), "content-coalesced worker begins");
        count.block = false;
        count.proceed.release(4);
        check(ready(first) && ready(second) && first.result() == second.result() && count.calls == 1,
            "identical bytes at different paths share the same in-flight content render");
    }
    {
        Counter count;
        count.block = true;
        Pipeline pipeline(config(root, "stale"), nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        QImage image(140, 140, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString path = put(inputs, "stale.bmp", image);
        begin(request, path);
        check(until([&] { return count.entered.available() >= 1; }), "obsolete render held in flight");
        bool publishedRed = false;
        QObject::connect(&request, &ArtworkRequest::resultChanged, &request, [&] {
            if (request.result().isEmpty()) return;
            const QColor color = QImage(request.result().toLocalFile()).pixelColor(60, 60);
            if (color.red() > 200 && color.blue() < 50) publishedRed = true;
        });
        image.fill(Qt::blue);
        image.save(path);
        request.refresh();
        count.block = false;
        count.proceed.release(4);
        check(ready(request) && !publishedRed
            && QImage(request.result().toLocalFile()).pixelColor(60, 60).blue() > 200,
            "replacement during active render never publishes the obsolete source");
    }
    {
        Counter count;
        count.block = true;
        auto cfg = config(root, "bounded");
        cfg.workers = 2;
        cfg.pendingJobs = 1;
        Pipeline pipeline(cfg, nullptr, count.renderer());
        std::vector<std::unique_ptr<ArtworkRequest>> requests;
        for (int i = 0; i < 12; ++i) {
            auto request = std::make_unique<ArtworkRequest>(pipeline);
            request->setDetail(i * 8);
            begin(*request, source);
            requests.push_back(std::move(request));
        }
        check(until([&] { return count.entered.available() >= 2; }), "bounded pool admits two workers");
        requests[2].reset(); // Pending subscriber disappears before rendering.
        requests[8].reset(); // Overflow retry disappears too.
        count.block = false;
        count.proceed.release(20);
        const bool drained = until([&] {
            for (const auto &request : requests) if (request && request->busy()) return false;
            return true;
        });
        bool completed = drained;
        for (const auto &request : requests) if (request && request->result().isEmpty()) completed = false;
        check(completed && count.maximum <= 2 && count.calls <= 10,
            "bounded overflow retries complete visible views; destroyed queued/retry subscribers do no work");
    }
    {
        auto cfg = config(root, "pruned");
        cfg.diskEntries = 3;
        Pipeline pipeline(cfg);
        ArtworkRequest pinned(pipeline), other(pipeline);
        begin(pinned, source);
        check(ready(pinned), "leased cache image is prepared");
        const auto protectedPath = pinned.result().toLocalFile();
        const auto protectedBytes = bytes(protectedPath);
        QFile unrelated(cfg.cacheDirectory + "/keep-me.txt");
        openFixture(unrelated, QIODevice::WriteOnly);
        unrelated.write("unrelated");
        unrelated.close();
        // An owned-name cache image may itself become a new source. Its input
        // lease and existing display lease both protect it during pruning.
        begin(other, protectedPath, "halftone");
        check(ready(other), "derived image may be read as a source without being overwritten");
        for (int detail = 1; detail < 9; ++detail) {
            other.setDetail(detail * 10);
            if (!ready(other)) break;
        }
        const auto files = QDir(cfg.cacheDirectory).entryList({"print-*.png"}, QDir::Files);
        check(files.size() <= 3 && bytes(protectedPath) == protectedBytes
            && bytes(unrelated.fileName()) == "unrelated" && bytes(source) == original,
            "pruning bounds idle variants while preserving displayed sources and unrelated files");
    }
    {
        Counter count;
        auto cfg = config(root, "display-lease");
        cfg.diskEntries = 1;
        Pipeline pipeline(cfg, nullptr, count.renderer());
        ArtworkRequest request(pipeline), other(pipeline);
        begin(request, source);
        check(ready(request), "displayed lease fixture prepared");
        const auto shown = request.result();
        request.displayed(shown);
        request.setDetail(20);
        check(ready(request), "replacement publishes before image-decoder acknowledgment");
        const auto loading = request.result();
        begin(other, source, "halftone");
        other.setDetail(30);
        check(ready(other) && QFile::exists(shown.toLocalFile()) && QFile::exists(loading.toLocalFile()),
            "both acknowledged and loading PNGs remain leased during cache pressure");
        request.displayed(loading);
        other.setDetail(40);
        check(ready(other) && !QFile::exists(shown.toLocalFile()) && QFile::exists(loading.toLocalFile()),
            "acknowledging the replacement releases the prior displayed cache file");
        request.setStyle("original");
        request.displayed({});
        other.setDetail(50);
        check(ready(other) && !QFile::exists(loading.toLocalFile()),
            "returning to original releases all old display leases");
        other.setStyle("original");
        check(until([&] { return QDir(cfg.cacheDirectory).entryList({"print-*.png"}, QDir::Files).size() <= 1; }),
            "idle worker pruning restores disk limits after the last display leases are released");
    }
    {
        const auto cfg = config(root, "link-safety");
        Pipeline pipeline(cfg);
        ArtworkRequest request(pipeline);
        begin(request, source);
        check(ready(request), "symlink safety fixture prepared");
        const QString output = request.result().toLocalFile();
        request.setStyle("original");
        const auto modified = QFileInfo(source).lastModified();
        if (!QFile::remove(output) || !QFile::link(source, output)) qFatal("could not prepare fixture symlink");
        request.setStyle("cleanInk");
        check(until([&] { return !request.busy(); }) && !request.error().isEmpty()
            && request.result().isEmpty() && QFileInfo(output).isSymLink()
            && bytes(source) == original && QFileInfo(source).lastModified() == modified,
            "cache-path symlink is rejected without altering target bytes, metadata or link");
    }
    {
        Counter count;
        auto cfg = config(root, "memory-bound");
        cfg.memoryKiB = 1;
        Pipeline pipeline(cfg, nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        begin(request, source);
        check(ready(request), "tiny memory cache fixture prepared");
        if (!QFile::remove(request.result().toLocalFile())) qFatal("could not remove isolated cache image");
        request.refresh();
        check(ready(request) && count.calls == 2,
            "decoded-image memory limit evicts images exceeding its configured capacity");
    }
    {
        Counter count;
        count.block = true;
        const auto cfg = config(root, "destroyed-running");
        Pipeline pipeline(cfg, nullptr, count.renderer());
        auto request = std::make_unique<ArtworkRequest>(pipeline);
        begin(*request, source);
        check(until([&] { return count.entered.available() >= 1; }), "destroyed-view render is held in flight");
        request.reset();
        count.block = false;
        count.proceed.release(4);
        check(until([&] { return count.active == 0; })
            && QDir(cfg.cacheDirectory).entryList({"print-*.png"}, QDir::Files).isEmpty(),
            "destroying the only running subscriber suppresses publication and disk write");
    }
    {
        Counter count;
        count.block = true;
        Pipeline pipeline(config(root, "settings-stale"), nullptr, count.renderer());
        ArtworkRequest request(pipeline);
        begin(request, source, "halftone");
        check(until([&] { return count.entered.available() >= 1; }), "settings-burst render is held in flight");
        request.setDetail(31);
        request.setTexture(78);
        request.setDotSize(63);
        request.setMonochrome(true);
        count.block = false;
        count.proceed.release(4);
        const auto expected = ArtworkPrint::render(QImage(source),
            {ArtworkPrint::Style::Halftone, .31, .78, .63, true}).image;
        check(ready(request) && QImage(request.result().toLocalFile()).convertToFormat(QImage::Format_ARGB32)
            == expected.convertToFormat(QImage::Format_ARGB32),
            "active worker setting bursts publish only the final renderer control snapshot");
    }
    {
        auto cfg = config(root, "alias-source");
        cfg.diskEntries = 1;
        Pipeline pipeline(cfg);
        ArtworkRequest seed(pipeline), aliasSource(pipeline), pressure(pipeline);
        begin(seed, source);
        check(ready(seed), "cache source alias fixture prepared");
        const QString seedPath = seed.result().toLocalFile();
        const auto seedBytes = bytes(seedPath);
        const QString alias = inputs + "/cache-source-link.png";
        if (!QFile::link(seedPath, alias)) qFatal("could not create isolated source alias");
        aliasSource.setSource(QUrl::fromLocalFile(alias)); // Original mode, no job.
        seed.setStyle("original");
        begin(pressure, source, "halftone");
        check(ready(pressure) && bytes(alias) == seedBytes && QFile::exists(seedPath),
            "an original-mode source alias protects its canonical cache target during pruning");
        aliasSource.setSource({});
        check(until([&] { return !QFile::exists(seedPath); }),
            "released source alias becomes eligible for deferred bounded eviction");
    }
    {
        const QString blockedDirectory = root + "/cache-is-a-file";
        QFile blocked(blockedDirectory);
        openFixture(blocked, QIODevice::WriteOnly);
        blocked.write("existing file");
        blocked.close();
        auto cfg = config(root, "unused");
        cfg.cacheDirectory = blockedDirectory;
        Pipeline pipeline(cfg);
        ArtworkRequest request(pipeline);
        begin(request, source);
        check(until([&] { return !request.busy(); }) && request.result().isEmpty()
            && request.error().contains("cache") && bytes(blockedDirectory) == "existing file"
            && bytes(source) == original, "cache I/O failure preserves original and reports fallback without false success");
    }
    {
        Pipeline pipeline(config(root, "bad"));
        ArtworkRequest request(pipeline);
        const QString empty = inputs + "/empty.png";
        QFile file(empty);
        openFixture(file, QIODevice::WriteOnly);
        file.close();
        begin(request, empty);
        check(until([&] { return !request.busy(); }) && !request.error().isEmpty() && request.result().isEmpty(),
            "empty source fails without a stale treatment");
        const QString huge = inputs + "/huge.png";
        file.setFileName(huge);
        openFixture(file, QIODevice::WriteOnly);
        file.resize(33 * 1024 * 1024);
        file.close();
        request.setSource(QUrl::fromLocalFile(huge));
        check(until([&] { return !request.busy(); }) && request.error().contains("32 MB"),
            "oversized encoded source is rejected before decoding");
        QImage wide(40000, 1, QImage::Format_RGB32);
        wide.fill(Qt::red);
        request.setSource(QUrl::fromLocalFile(put(inputs, "too-wide.png", wide)));
        check(until([&] { return !request.busy(); }) && request.result().isEmpty()
            && request.error().contains("dimensions"), "oversized declared dimensions are rejected before pixel decoding");
    }
    std::printf("%d/%d artwork pipeline checks passed\n", assertions - failures, assertions);
    return failures ? 1 : 0;
}
