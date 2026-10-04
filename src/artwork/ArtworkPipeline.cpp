#include "ArtworkPipeline.h"

#include <QBuffer>
#include <QCache>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QImageReader>
#include <QImageWriter>
#include <QMutex>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QWaitCondition>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <sys/stat.h>

namespace ArtworkPrint {
namespace {

// Increment whenever renderer output or the canonical decode policy changes.
constexpr auto revision = "print-app-1:renderer-10:canonical-800";
constexpr qint64 maxEncodedBytes = 32 * 1024 * 1024;
constexpr qint64 maxSourcePixels = 64 * 1000 * 1000;
constexpr int canonicalEdge = 800;

struct Stamp {
    quint64 device = 0, inode = 0;
    qint64 size = -1, modified = 0, changed = 0;
    bool operator==(const Stamp &) const = default;
};

Stamp stamp(const QString &path)
{
    struct stat value {};
    if (::stat(QFile::encodeName(path).constData(), &value) != 0 || !S_ISREG(value.st_mode)) return {};
    return {quint64(value.st_dev), quint64(value.st_ino), value.st_size,
            qint64(value.st_mtim.tv_sec) * 1000000000 + value.st_mtim.tv_nsec,
            qint64(value.st_ctim.tv_sec) * 1000000000 + value.st_ctim.tv_nsec};
}

Options normalized(Options value)
{
    const auto control = [](double x, double fallback) {
        return std::isfinite(x) ? std::clamp(x, 0.0, 1.0) : fallback;
    };
    value.detail = control(value.detail, 1.0);
    value.texture = control(value.texture, .14);
    value.dotSize = control(value.dotSize, .17);
    if (value.style != Style::Halftone) {
        value.dotSize = .17;
        value.monochrome = false;
    }
    if (value.style == Style::CleanInk) value.texture = 0;
    return value;
}

QByteArray settingsKey(const Options &value)
{
    return QByteArray::number(int(value.style)) + ':' + QByteArray::number(value.detail, 'g', 17)
        + ':' + QByteArray::number(value.texture, 'g', 17) + ':'
        + QByteArray::number(value.dotSize, 'g', 17) + ':' + (value.monochrome ? '1' : '0');
}

struct Cache {
    explicit Cache(Pipeline::Config configuration, Pipeline::Renderer renderFunction)
        : config(std::move(configuration)), renderer(std::move(renderFunction)), images(config.memoryKiB) {}
    Pipeline::Config config;
    Pipeline::Renderer renderer;
    QMutex mutex;
    QMutex diskMutex;
    QCache<QString, QImage> images;
    QHash<QString, int> pins;
    quint64 pinsRevision = 0;
    QSet<QString> contentFlights;
    QWaitCondition contentReady;
};

std::shared_ptr<void> pin(const std::shared_ptr<Cache> &cache, const QString &path)
{
    {
        QMutexLocker lock(&cache->mutex);
        ++cache->pins[path];
        ++cache->pinsRevision;
    }
    return std::shared_ptr<void>(new QString(path), [cache](void *pointer) {
        std::unique_ptr<QString> path(static_cast<QString *>(pointer));
        QMutexLocker lock(&cache->mutex);
        auto entry = cache->pins.find(*path);
        if (entry != cache->pins.end() && --entry.value() == 0) cache->pins.erase(entry);
        ++cache->pinsRevision;
    });
}

QImage remembered(const std::shared_ptr<Cache> &cache, const QString &key)
{
    QMutexLocker lock(&cache->mutex);
    const auto *found = cache->images.object(key);
    return found ? *found : QImage();
}

void remember(const std::shared_ptr<Cache> &cache, const QString &key, const QImage &image)
{
    const int cost = int((image.sizeInBytes() + 1023) / 1024);
    QMutexLocker lock(&cache->mutex);
    cache->images.insert(key, new QImage(image), std::max(1, cost));
}

// Called under diskMutex, exclusively on a worker. Only our strict filename
// namespace is eligible. Unrelated files, symlinks and leased sources survive.
void prune(const std::shared_ptr<Cache> &cache)
{
    const QRegularExpression owned(QStringLiteral("^print-[a-f0-9]{64}\\.png$"));
    auto files = QDir(cache->config.cacheDirectory).entryInfoList(
        QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
    QList<QFileInfo> candidates;
    qint64 bytes = 0;
    for (const auto &file : files) {
        if (!owned.match(file.fileName()).hasMatch()) continue;
        candidates.append(file);
        bytes += file.size();
    }
    int entries = candidates.size();
    QSet<QString> protectedPaths;
    quint64 pinRevision;
    {
        QMutexLocker lock(&cache->mutex);
        const auto paths = cache->pins.keys();
        protectedPaths = QSet<QString>(paths.begin(), paths.end());
        pinRevision = cache->pinsRevision;
    }
    // Resolve aliases on this worker, outside the short GUI-facing mutex.
    // Pending input symlinks into the cache must protect their actual target.
    const auto rawPaths = protectedPaths;
    for (const auto &path : rawPaths) {
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty()) protectedPaths.insert(canonical);
    }
    QHash<QString, QString> canonicalFiles;
    for (const auto &file : candidates)
        canonicalFiles.insert(file.absoluteFilePath(), file.canonicalFilePath());
    QMutexLocker lock(&cache->mutex);
    if (pinRevision != cache->pinsRevision) return; // A later idle pass retries.
    for (const auto &file : candidates) {
        if (entries <= cache->config.diskEntries && bytes <= cache->config.diskBytes) break;
        if (protectedPaths.contains(file.absoluteFilePath())
            || protectedPaths.contains(canonicalFiles.value(file.absoluteFilePath()))) continue;
        if (QFile::remove(file.absoluteFilePath())) {
            --entries;
            bytes -= file.size();
        }
    }
}

QImage readCached(const QString &path)
{
    if (QFileInfo(path).size() > maxEncodedBytes) return {};
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isEmpty() || size.width() > canonicalEdge || size.height() > canonicalEdge) return {};
    return reader.read(); // Validate the complete file, not just its PNG header.
}

struct Job {
    struct Subscriber {
        quint64 id;
        QPointer<QObject> receiver;
        Pipeline::Completion completion;
        QMetaObject::Connection destroyed;
    };
    QString key;
    QString path;
    Options options;
    std::atomic_bool cancelled = false;
    bool running = false;
    QList<Subscriber> subscribers;
    std::shared_ptr<void> sourceLease;
};

Pipeline::Output failure(const char *message)
{
    return {{}, QString::fromUtf8(message), {}};
}

Pipeline::Output prepare(const std::shared_ptr<Cache> &cache, const std::shared_ptr<Job> &job)
{
    // A replacement while decoding/rendering gets a fresh bounded attempt.
    // Hash and decode the same byte snapshot; nanosecond ctime also catches
    // same-size replacement even when the caller restores an old mtime.
    for (int attempt = 0; attempt < 3 && !job->cancelled; ++attempt) {
        const Stamp before = stamp(job->path);
        if (before.size < 0) return failure("Artwork is unavailable. Choose the image again.");
        if (before.size == 0) return failure("Artwork is empty. Choose another image.");
        if (before.size > maxEncodedBytes) return failure("Artwork is too large to process. Choose an image under 32 MB.");
        const auto canonicalSource = QFileInfo(job->path).canonicalFilePath();
        auto sourceLease = pin(cache, canonicalSource);
        QFile source(job->path);
        if (!source.open(QIODevice::ReadOnly)) return failure("Artwork could not be read. Check the file is available.");
        const QByteArray bytes = source.read(maxEncodedBytes + 1);
        if (source.error() != QFileDevice::NoError) return failure("Artwork could not be read. Try again.");
        source.close();
        if (!(before == stamp(job->path)) || bytes.size() != before.size) continue;
        if (job->cancelled) return {};

        const QByteArray content = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        const QString sourceKey = QStringLiteral("source:") + QString::fromLatin1(content);
        const QString variant = QString::fromLatin1(QCryptographicHash::hash(
            QByteArray(revision) + ':' + content + ':' + settingsKey(job->options),
            QCryptographicHash::Sha256).toHex());
        const QString outputPath = QDir(cache->config.cacheDirectory).absoluteFilePath(
            QStringLiteral("print-") + variant + QStringLiteral(".png"));
        const QString resultKey = QStringLiteral("result:") + variant;

        // Different paths with identical bytes coalesce too, after hashing.
        {
            QMutexLocker lock(&cache->mutex);
            while (cache->contentFlights.contains(variant) && !job->cancelled)
                cache->contentReady.wait(&cache->mutex, 100);
            if (job->cancelled) return {};
            cache->contentFlights.insert(variant);
        }
        const auto flight = std::shared_ptr<void>(new QString(variant), [cache](void *pointer) {
            std::unique_ptr<QString> variant(static_cast<QString *>(pointer));
            QMutexLocker lock(&cache->mutex);
            cache->contentFlights.remove(*variant);
            cache->contentReady.wakeAll();
        });
        auto outputLease = pin(cache, outputPath);
        bool cached = false;
        {
            QMutexLocker diskLock(&cache->diskMutex);
            if (QFileInfo(outputPath).isSymLink())
                return failure("Artwork cache contains an invalid link. Clear the print cache and try again.");
            const QImage image = readCached(outputPath);
            cached = !image.isNull();
            if (cached) {
                remember(cache, resultKey, image);
                prune(cache);
            }
        }
        if (cached) {
            if (!(before == stamp(job->path))) continue;
            if (job->cancelled) return {};
            return {QUrl::fromLocalFile(outputPath), {}, std::move(outputLease)};
        }

        QImage rendered = remembered(cache, resultKey);
        if (rendered.isNull()) {
            QImage image = remembered(cache, sourceKey);
            if (image.isNull()) {
                QBuffer buffer;
                buffer.setData(bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                reader.setAutoTransform(true);
                const QSize size = reader.size();
                if (size.isEmpty() || qint64(size.width()) * size.height() > maxSourcePixels
                    || size.width() > 32768 || size.height() > 32768)
                    return failure("Artwork has invalid or oversized dimensions. Choose another image.");
                if (size.width() > canonicalEdge || size.height() > canonicalEdge)
                    reader.setScaledSize(size.scaled(canonicalEdge, canonicalEdge, Qt::KeepAspectRatio));
                image = reader.read();
                if (image.isNull()) return failure("Artwork could not be decoded. Choose another image.");
                if (image.width() > canonicalEdge || image.height() > canonicalEdge)
                    image = image.scaled(canonicalEdge, canonicalEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                remember(cache, sourceKey, image);
            }
            if (job->cancelled) return {};
            rendered = cache->renderer(image, job->options).image;
            if (rendered.isNull()) return failure("Artwork treatment could not be created. Try another image.");
            remember(cache, resultKey, rendered);
        }
        if (job->cancelled) return {};
        if (!(before == stamp(job->path))) continue;
        {
            QMutexLocker diskLock(&cache->diskMutex);
            if (!QDir().mkpath(cache->config.cacheDirectory))
                return failure("Artwork cache could not be created. Check available disk space.");
            if (QFileInfo(outputPath).isSymLink())
                return failure("Artwork cache contains an invalid link. Clear the print cache and try again.");
            // Even if an input is itself inside this cache, never replace it.
            const Stamp destination = stamp(outputPath);
            if (outputPath == canonicalSource || (destination.size >= 0
                && destination.device == before.device && destination.inode == before.inode))
                return failure("Artwork cache conflicts with its source. Choose another cache image.");
            QSaveFile output(outputPath);
            if (!output.open(QIODevice::WriteOnly))
                return failure("Artwork cache could not be written. Check available disk space.");
            QImageWriter writer(&output, "png");
            if (!writer.write(rendered)) return failure("Artwork cache could not be written. Check available disk space.");
            if (job->cancelled || !(before == stamp(job->path))) {
                output.cancelWriting();
                continue;
            }
            if (!output.commit()) return failure("Artwork cache could not be saved. Check available disk space.");
            prune(cache);
        }
        if (job->cancelled) return {};
        if (!(before == stamp(job->path))) continue;
        return {QUrl::fromLocalFile(outputPath), {}, std::move(outputLease)};
    }
    return job->cancelled ? Pipeline::Output{}
        : failure("Artwork changed while loading. Try again once the file is ready.");
}

} // namespace

class Pipeline::Impl {
public:
    Impl(Pipeline *owner, Config config, Renderer renderer) : owner(owner)
    {
        if (config.cacheDirectory.isEmpty())
            config.cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                + QStringLiteral("/printed-art");
        config.cacheDirectory = QDir(config.cacheDirectory).absolutePath();
        config.workers = std::clamp(config.workers, 1, 2);
        config.pendingJobs = std::clamp(config.pendingJobs, 1, 256);
        config.memoryKiB = std::clamp(config.memoryKiB, 1, 128 * 1024);
        config.diskEntries = std::clamp(config.diskEntries, 1, 2048);
        config.diskBytes = std::clamp<qint64>(config.diskBytes, 1024, 1024ll * 1024 * 1024);
        if (!renderer) renderer = ArtworkPrint::render;
        cache = std::make_shared<Cache>(config, std::move(renderer));
        pool.setMaxThreadCount(config.workers);
        pool.setExpiryTimeout(30000);
        maintenance.setSingleShot(true);
        QObject::connect(&maintenance, &QTimer::timeout, owner, [this] {
            trimRequested = true;
            pump();
        });
    }

    void pump()
    {
        while (active < pool.maxThreadCount() && !pending.empty()) {
            auto job = pending.front();
            pending.pop_front();
            if (job->cancelled) continue;
            job->running = true;
            ++active;
            auto *watcher = new QFutureWatcher<Output>(owner);
            QObject::connect(watcher, &QFutureWatcher<Output>::finished, owner, [this, watcher, job] {
                Output output = watcher->result();
                watcher->deleteLater();
                --active;
                if (jobs.value(job->key) == job) jobs.remove(job->key);
                const auto subscribers = std::move(job->subscribers);
                for (const auto &subscriber : subscribers) {
                    subscriptions.remove(subscriber.id);
                    QObject::disconnect(subscriber.destroyed);
                }
                // Complete ownership bookkeeping before callbacks can submit
                // more work or delete another subscriber.
                for (const auto &subscriber : subscribers)
                    if (subscriber.receiver && !job->cancelled) subscriber.completion(output);
                maintenance.start(250);
                pump();
            });
            watcher->setFuture(QtConcurrent::run(&pool, [cache = cache, job] {
                try { return prepare(cache, job); }
                catch (...) { return failure("Artwork treatment could not be created. Try again."); }
            }));
        }
        if (trimRequested && active == 0 && pending.empty()) {
            trimRequested = false;
            ++active;
            auto *watcher = new QFutureWatcher<void>(owner);
            QObject::connect(watcher, &QFutureWatcher<void>::finished, owner, [this, watcher] {
                watcher->deleteLater();
                --active;
                pump();
            });
            watcher->setFuture(QtConcurrent::run(&pool, [cache = cache] {
                QMutexLocker lock(&cache->diskMutex);
                prune(cache);
            }));
        }
    }

    Pipeline *owner;
    std::shared_ptr<Cache> cache;
    QThreadPool pool;
    QTimer maintenance;
    bool trimRequested = false;
    int active = 0;
    quint64 nextId = 0;
    quint64 refreshEpoch = 0;
    QHash<QString, std::shared_ptr<Job>> jobs;
    QHash<quint64, std::weak_ptr<Job>> subscriptions;
    std::deque<std::shared_ptr<Job>> pending;
};

Pipeline::Pipeline(Config config, QObject *parent, Renderer renderer)
    : QObject(parent), d(std::make_unique<Impl>(this, std::move(config), std::move(renderer))) {}

Pipeline::~Pipeline()
{
    for (const auto &job : std::as_const(d->jobs)) {
        job->cancelled = true;
        for (const auto &subscriber : job->subscribers) QObject::disconnect(subscriber.destroyed);
    }
    // Also covers cancelled running jobs already removed from the key map.
    d->pool.waitForDone();
}

Pipeline *Pipeline::shared()
{
    static QPointer<Pipeline> service;
    if (!service) service = new Pipeline({}, QCoreApplication::instance());
    return service;
}

QString Pipeline::localPath(const QUrl &source)
{
    if (!source.isValid() || source.isEmpty()) return {};
    QUrl file = source;
    file.setQuery(QString());
    file.setFragment(QString());
    QString path;
    if (file.isLocalFile() && (file.host().isEmpty() || file.host() == "localhost")) path = file.toLocalFile();
    else if (file.scheme().isEmpty() && file.host().isEmpty()) path = file.path();
    if (!QDir::isAbsolutePath(path)) return {};
    return QDir::cleanPath(path);
}

quint64 Pipeline::submit(const QUrl &source, const Options &options, QObject *receiver,
                         Completion completion, bool refresh)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QString path = localPath(source);
    if (path.isEmpty() || !receiver || options.style == Style::Original) return 0;
    const Options values = normalized(options);
    QString key = source.toString(QUrl::FullyEncoded) + ':' + QString::fromLatin1(settingsKey(values));
    if (refresh) key += QStringLiteral(":refresh:") + QString::number(++d->refreshEpoch);
    auto job = d->jobs.value(key);
    if (!job) {
        if (d->active >= d->pool.maxThreadCount()
            && int(d->pending.size()) >= d->cache->config.pendingJobs) return 0;
        job = std::make_shared<Job>();
        job->key = key;
        job->path = path;
        job->options = values;
        job->sourceLease = pin(d->cache, path);
        d->jobs.insert(key, job);
        d->pending.push_back(job);
    }
    const quint64 id = ++d->nextId;
    auto destroyed = connect(receiver, &QObject::destroyed, this, [this, id] { cancel(id); });
    job->subscribers.append({id, receiver, std::move(completion), destroyed});
    d->subscriptions.insert(id, job);
    d->pump();
    return id;
}

void Pipeline::cancel(quint64 subscription)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto found = d->subscriptions.find(subscription);
    if (found == d->subscriptions.end()) return;
    const auto job = found.value().lock();
    d->subscriptions.erase(found);
    if (!job) return;
    for (auto it = job->subscribers.begin(); it != job->subscribers.end(); ++it) {
        if (it->id != subscription) continue;
        disconnect(it->destroyed);
        job->subscribers.erase(it);
        break;
    }
    if (!job->subscribers.isEmpty()) return;
    job->cancelled = true;
    if (d->jobs.value(job->key) == job) d->jobs.remove(job->key);
    if (!job->running) {
        const auto it = std::find(d->pending.begin(), d->pending.end(), job);
        if (it != d->pending.end()) d->pending.erase(it);
    }
    releaseUnused();
}

std::shared_ptr<void> Pipeline::protectSource(const QUrl &source)
{
    const QString path = localPath(source);
    return path.isEmpty() ? nullptr : pin(d->cache, path);
}

void Pipeline::releaseUnused()
{
    d->maintenance.start(250);
}

} // namespace ArtworkPrint
