#include "SpokenMediaService.h"
#include "../player/PlayerService.h"
#include "AudiobookProbe.h"
#include "PodcastFeed.h"
#include "SpokenStore.h"
#include <QBuffer>
#include <QCollator>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {
QString localPath(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : QString(); }
QString hash(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
bool webUrl(const QUrl &url) {
    return url.isValid() && !url.host().isEmpty() &&
           (url.scheme() == "https" || url.scheme() == "http") && url.userInfo().isEmpty();
}
struct Fetch {
    QByteArray bytes;
    QUrl finalUrl;
    QString error;
};
// Worker-only streaming transport. Redirects are validated individually; query
// strings are preserved and never logged. All replies share a total deadline.
Fetch fetch(const QUrl &input, qint64 limit, const std::atomic_bool *cancel,
            QIODevice *output = nullptr) {
    Fetch result;
    QUrl url = input;
    QNetworkAccessManager manager;
    QElapsedTimer elapsed;
    elapsed.start();
    // A 128 MiB feed served uncompressed needs more than 45 seconds below
    // about 3 MB/s.
    const qint64 totalDeadline = output ? 30 * 60 * 1000 : limit > 16 * 1024 * 1024 ? 180000 : 45000;
    for (int redirects = 0; redirects <= 5; ++redirects) {
        if (!webUrl(url) || (cancel && cancel->load())) {
            result.error = "Invalid URL or operation cancelled";
            return result;
        }
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Zuuned/0.1 (+https://zuuned.com)"));
        req.setTransferTimeout(30000);
        // drain() caps the decompressed bytes at limit. Qt's own bomb check
        // also refuses any gzip body past 10 MiB at a high ratio, which a
        // 102 MiB feed of repeated show notes (2.1 MB on the wire) is.
        req.setDecompressedSafetyCheckThreshold(-1);
        auto *reply = manager.get(req);
        reply->setReadBufferSize(256 * 1024);
        QEventLoop loop;
        QTimer poll;
        poll.setInterval(50);
        qint64 received = 0;
        bool tooLarge = false, writeFailed = false, timedOut = false;
        auto drain = [&] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status < 200 || status >= 300) {
                reply->readAll();
                return;
            }
            const auto data = reply->read(256 * 1024);
            received += data.size();
            if (received > limit) {
                tooLarge = true;
                reply->abort();
                return;
            }
            if (output) {
                if (output->write(data) != data.size()) {
                    writeFailed = true;
                    reply->abort();
                }
            } else
                result.bytes.append(data);
        };
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            while (reply->bytesAvailable() && !tooLarge && !writeFailed)
                drain();
        });
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&] {
            const qint64 size = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (size > limit) {
                tooLarge = true;
                reply->abort();
            }
        });
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (elapsed.elapsed() > totalDeadline)
                timedOut = true;
            if ((cancel && cancel->load()) || timedOut)
                reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        poll.start();
        if (!reply->isFinished())
            loop.exec();
        poll.stop();
        while (reply->bytesAvailable() && !tooLarge && !writeFailed)
            drain();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const auto replyError = reply->error();
        const bool ok = replyError == QNetworkReply::NoError;
        delete reply;
        if (status >= 300 && status < 400 && !redirect.isEmpty()) {
            const auto next = url.resolved(redirect);
            if (url.scheme() == "https" && next.scheme() != "https") {
                result.error = "Refused an insecure redirect";
                return result;
            }
            url = next;
            continue;
        }
        if (!ok || status < 200 || status >= 300 || tooLarge || writeFailed) {
            result.bytes.clear();
            // Never the reply's errorString: it can carry the URL and its query.
            result.error =
                tooLarge      ? QStringLiteral("Response exceeds the %1 MiB limit").arg(limit / (1024 * 1024))
                : writeFailed ? "Could not save the download"
                : (cancel && cancel->load())
                    ? "Cancelled"
                : timedOut ? QStringLiteral("The download took longer than %1 seconds. Try again.").arg(totalDeadline / 1000)
                : status >= 200 && status < 300
                    ? QStringLiteral("The download failed part way (network error %1). Try again.").arg(int(replyError))
                    : QStringLiteral("Provider request failed (HTTP %1). Try again.").arg(status);
            return result;
        }
        result.finalUrl = url;
        return result;
    }
    result.error = "Too many redirects";
    return result;
}
QString prepareCover(const QString &root, const QByteArray &bytes, QString *error) {
    if (bytes.isEmpty() || bytes.size() > 10 * 1024 * 1024) {
        *error = "Cover must be under 10 MiB";
        return {};
    }
    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const QByteArray format = reader.format().toLower();
    if (format != "jpeg" && format != "jpg" && format != "png" && format != "webp") {
        *error = "Choose a JPEG, PNG or WebP cover";
        return {};
    }
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) {
        *error = "Cover dimensions exceed the limit";
        return {};
    }
    reader.setAutoTransform(true);
    reader.setScaledSize(size.scaled(1600, 1600, Qt::KeepAspectRatio));
    const auto image = reader.read();
    if (image.isNull()) {
        *error = "Cannot decode this cover";
        return {};
    }
    QDir().mkpath(root + "/art");
    const QString path = root + "/art/" + hash(bytes) + ".jpg";
    if (!QFileInfo::exists(path)) {
        QSaveFile out(path);
        out.setDirectWriteFallback(false);
        if (!out.open(QIODevice::WriteOnly) || !image.save(&out, "JPEG", 93) || !out.commit()) {
            *error = "Cannot save the cover";
            return {};
        }
    }
    return QUrl::fromLocalFile(path).toString();
}
QString remoteCover(const QString &root, const QUrl &url, const std::atomic_bool *cancel) {
    if (!webUrl(url))
        return {};
    auto response = fetch(url, 10 * 1024 * 1024, cancel);
    if (!response.error.isEmpty())
        return {};
    QString error;
    return prepareCover(root, response.bytes, &error);
}
QVariantMap feedMap(const PodcastFeed &f, const QUrl &requested) {
    QVariantList episodes;
    for (const auto &e : f.episodes)
        episodes.append(QVariantMap{{"identity", e.identity},
                                    {"guid", e.guid},
                                    {"title", e.title},
                                    {"author", e.author},
                                    {"description", e.description},
                                    {"sourceUrl", e.enclosureUrl.toString(QUrl::FullyEncoded)},
                                    {"artUrl", e.artworkUrl.toString()},
                                    {"mimeType", e.mimeType},
                                    {"bytes", e.enclosureBytes},
                                    {"durationMs", e.durationMs},
                                    {"episodeNumber", e.episodeNumber},
                                    {"published", e.published.toString(Qt::ISODate)}});
    return {{"kind", "show"},
            {"identity", requested.toString(QUrl::FullyEncoded)},
            {"url", requested.toString(QUrl::FullyEncoded)},
            {"responseUrl", f.sourceUrl.toString()},
            {"title", f.title},
            {"author", f.author},
            {"description", f.description},
            {"artUrl", f.artworkUrl.toString()},
            {"episodes", episodes},
            {"nextPage", f.nextPage.toString(QUrl::FullyEncoded)},
            {"skippedItems", f.skippedItems}};
}
QVariantMap readFeed(const QString &root, const QUrl &url, const std::atomic_bool *cancel,
                     QString *error, bool withCover = true) {
    if (!webUrl(url)) {
        *error = "Enter an HTTP or HTTPS RSS feed without embedded credentials";
        return {};
    }
    auto response = fetch(url, Podcasts::MaxFeedBytes, cancel);
    if (!response.error.isEmpty()) {
        *error = response.error;
        return {};
    }
    PodcastFeed feed;
    if (!Podcasts::parseRss(response.bytes, response.finalUrl, &feed, error))
        return {};
    // A document can be 128 MiB; free it before converting the episodes.
    response.bytes = QByteArray();
    auto result = feedMap(feed, url);
    if (withCover)
        result["autoArt"] = remoteCover(root, feed.artworkUrl, cancel);
    return result;
}
// Merges feed episodes into a show. Saved rows keep their downloads, art and
// listening state; rows the feed did not change are not rewritten. A show
// holds at most MaxShowEpisodes; *full reports that new episodes were left out.
// insertOnly (older pages) adds new episodes and leaves saved ones alone.
bool saveEpisodes(SpokenStore &store, const QString &showId, const QVariantList &episodes,
                  int *added, bool *full, QString *error, bool insertOnly = false) {
    *added = 0;
    *full = false;
    SpokenStore::EpisodeQuery all;
    all.show = showId;
    int count = store.countEpisodes(all, error);
    if (!error->isEmpty())
        return false;
    for (const auto &value : episodes) {
        const auto data = value.toMap();
        auto row = store.find("episode", showId, data.value("identity").toString(), error);
        if (!error->isEmpty())
            return false;
        const bool fresh = row.isEmpty();
        if (!fresh && insertOnly)
            continue;
        if (fresh && count >= Podcasts::MaxShowEpisodes) {
            *full = true;
            continue;
        }
        const auto saved = row;
        for (auto it = data.begin(); it != data.end(); ++it)
            row[it.key()] = it.value();
        row["kind"] = "episode";
        row["parent"] = showId;
        if (!fresh && row == saved)
            continue;
        if (!store.put(&row, error))
            return false;
        if (fresh) {
            ++count;
            ++*added;
        }
    }
    return true;
}
bool saveFeed(SpokenStore &store, const QVariantMap &feed, QString *id, QString *error,
              int *added = nullptr) {
    if (!store.begin(error))
        return false;
    auto show = store.find("show", {}, feed.value("identity").toString(), error);
    if (!error->isEmpty()) {
        store.rollback();
        return false;
    }
    const QVariantList eps = feed.value("episodes").toList();
    for (auto it = feed.begin(); it != feed.end(); ++it)
        if (it.key() != "episodes" && it.key() != "nextPage" &&
            (it.key() != "autoArt" || !it.value().toString().isEmpty()))
            show[it.key()] = it.value();
    show["count"] = eps.size();
    show["refreshed"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    // A single-document feed holds its whole back catalogue. An unfinished
    // walk keeps its cursor even if one response lists no older page.
    const auto older = show.value("olderState").toString();
    const auto nextPage = feed.value("nextPage").toString();
    if (older.isEmpty() && nextPage.isEmpty()) {
        show["olderState"] = "complete";
    } else if (older.isEmpty() && QUrl(feed.value("responseUrl").toString()).scheme() == "https" &&
               QUrl(nextPage).scheme() == "http") {
        show["olderState"] = "limited";
        show["olderError"] = "The feed offers its older pages only over unencrypted HTTP";
    } else if (older.isEmpty()) {
        // Older pages to read: quitting before the first is saved resumes it.
        show["olderState"] = "loading";
        show["olderPage"] = nextPage;
    }
    if (!store.put(&show, error)) {
        store.rollback();
        return false;
    }
    *id = show.value("id").toString();
    int fresh = 0;
    bool full = false;
    if (!saveEpisodes(store, *id, eps, &fresh, &full, error)) {
        store.rollback();
        return false;
    }
    if (full) {
        show["olderState"] = "limited";
        show["olderError"] = QStringLiteral("Zuuned keeps at most %1 episodes of a show")
                                 .arg(QLocale(QLocale::English).toString(Podcasts::MaxShowEpisodes));
        if (!store.put(&show, error)) {
            store.rollback();
            return false;
        }
    }
    if (!store.commit(error)) {
        store.rollback();
        return false;
    }
    if (added)
        *added = fresh;
    return true;
}
QVariantMap inspectBook(const QString &root, const QList<QUrl> &urls, QString *error,
                        const std::atomic_bool *cancel) {
    QVariantList parts;
    QSet<QString> paths;
    QString automaticArt;
    for (const auto &url : urls) {
        const QString path = QFileInfo(localPath(url)).canonicalFilePath();
        if (path.isEmpty() || paths.contains(path)) {
            *error = "Missing file or duplicate audiobook part";
            return {};
        }
        paths.insert(path);
        AudiobookPart part;
        if (!Audiobooks::probePart(path, &part, error, cancel))
            return {};
        if (automaticArt.isEmpty() && !part.embeddedArtwork.isEmpty()) {
            QString artError;
            automaticArt = prepareCover(root, part.embeddedArtwork, &artError);
        }
        QVariantList chapters;
        for (const auto &ch : part.chapters)
            chapters.append(
                QVariantMap{{"title", ch.title}, {"startMs", ch.startMs}, {"endMs", ch.endMs}});
        parts.append(
            QVariantMap{{"path", path},
                        {"identity", path},
                        {"title", part.partTitle.isEmpty() ? QFileInfo(path).completeBaseName()
                                                           : part.partTitle},
                        {"bookTitle", part.bookTitle},
                        {"bookTitleFromTag", part.bookTitleFromTag},
                        {"author", part.author},
                        {"narrator", part.narrator},
                        {"durationMs", part.durationMs},
                        {"codec", part.codec},
                        {"bytes", part.bytes},
                        {"mtime", QFileInfo(path).lastModified().toMSecsSinceEpoch()},
                        {"disc", part.discNumber},
                        {"part", part.partNumber},
                        {"chapters", chapters}});
    }
    const auto first = parts.first().toMap();
    return {{"kind", "book"},
            {"title", first.value("bookTitle").toString().isEmpty() ? first.value("title")
                                                                    : first.value("bookTitle")},
            {"author", first.value("author")},
            {"narrator", first.value("narrator")},
            {"parts", parts},
            {"autoArt", automaticArt}};
}
int writeBook(SpokenStore &s, const QVariantMap &draft, const QString &title, const QString &author,
              const QString &narrator, const QStringList &order, QString *id, QString *error) {
    const auto parts = draft.value("parts").toList();
    QSet<QString> unique(order.begin(), order.end());
    if (draft.value("kind") != "book" || title.trimmed().isEmpty() || title.size() > 500 ||
        order.size() != parts.size() || unique.size() != order.size()) {
        *error = "Review the book title and complete part order";
        return -1;
    }
    QVariantMap byPath;
    for (const auto &p : parts)
        byPath[p.toMap().value("path").toString()] = p;
    QStringList sorted = order;
    std::sort(sorted.begin(), sorted.end());
    const QString identity =
        hash(QJsonDocument::fromVariant(sorted).toJson(QJsonDocument::Compact));
    auto existing = s.find("book", {}, identity, error);
    if (!error->isEmpty())
        return -1;
    if (!existing.isEmpty()) {
        *id = existing.value("id").toString();
        return 0;
    }
    // A changed source set must not silently clone the files of an existing
    // book. Extending/replacing a book is a distinct future edit operation.
    const auto existingRows = s.collectionRows(error);
    if (!error->isEmpty())
        return -1;
    for (const auto &value : existingRows) {
        const auto saved = value.toMap();
        if (saved.value("kind") == "part" && unique.contains(saved.value("path").toString())) {
            *error = "Some files already belong to a book. Exclude this folder and "
                     "import only new "
                     "books.";
            return -1;
        }
    }
    for (const auto &path : order) {
        const auto p = byPath.value(path).toMap();
        QFileInfo file(path);
        if (p.isEmpty() || !file.isFile() || file.size() != p.value("bytes").toLongLong() ||
            file.lastModified().toMSecsSinceEpoch() != p.value("mtime").toLongLong()) {
            *error = "A source file changed. Inspect the book again.";
            return -1;
        }
    }
    QVariantMap book{{"kind", "book"},
                     {"identity", identity},
                     {"title", title.trimmed()},
                     {"author", author.trimmed()},
                     {"narrator", narrator.trimmed()},
                     {"autoArt", draft.value("autoArt")}};
    if (!s.put(&book, error)) {
        return -1;
    }
    *id = book.value("id").toString();
    int index = 0;
    for (const auto &path : order) {
        auto p = byPath.value(path).toMap();
        p["kind"] = "part";
        p["parent"] = *id;
        p["order"] = index++;
        if (!s.put(&p, error)) {
            return -1;
        }
    }

    return 1;
}
} // namespace

SpokenMediaService::SpokenMediaService(QObject *parent)
    : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false)),
      m_olderCancel(std::make_shared<std::atomic_bool>(false)) {
    m_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/spoken";
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    for (auto *lane : {&m_feedLane, &m_olderLane}) {
        lane->worker = new QObject;
        lane->worker->moveToThread(&lane->thread);
        connect(&lane->thread, &QThread::finished, lane->worker, &QObject::deleteLater);
        lane->thread.start();
    }
    connect(qApp, &QCoreApplication::aboutToQuit, this, &SpokenMediaService::shutdown,
            Qt::DirectConnection);
    run("load", [](SpokenStore &, Result &, const std::atomic_bool *) {}, -1, {}, [this](const Result &) {
        // Older pages still being read when Zuuned quit continue now.
        for (const auto &row : std::as_const(m_rows)) {
            const auto show = row.toMap();
            if (show.value("kind") == "show" && show.value("olderState") == "loading")
                planOlder(show.value("id").toString(), {}, 0, false);
        }
    });
    m_positionTimer.setInterval(5000);
    connect(&m_positionTimer, &QTimer::timeout, this, [this] { savePosition(); });
    m_positionTimer.start();
}
SpokenMediaService::~SpokenMediaService() {
    shutdown();
    for (auto *lane : {&m_feedLane, &m_olderLane}) {
        lane->thread.quit();
        lane->thread.wait();
    }
    m_thread.quit();
    m_thread.wait();
}
void SpokenMediaService::shutdown() {
    if (m_stopping)
        return;
    m_stopping = true;
    m_positionTimer.stop();
    m_cancel->store(true);
    m_olderCancel->store(true);
    // The last connection to close checkpoints the WAL; the reader cannot.
    m_reader.reset();
    for (const auto &cancel : m_transferCancels)
        cancel->store(true);
    const auto id = m_positionId;
    const auto position = m_position;
    const auto root = m_root;
    QMetaObject::invokeMethod(
        m_worker,
        [id, position, root] {
            if (id.isEmpty())
                return;
            QString error;
            SpokenStore s(root);
            if (!s.open(&error))
                return;
            auto row = s.get(id, &error);
            if (row.isEmpty())
                return;
            row["positionMs"] = position;
            s.put(&row, &error);
        },
        Qt::BlockingQueuedConnection);
}
// The task banner's cancel: reading older pages carries on (only quitting
// stops it, and then it resumes at the next start).
void SpokenMediaService::cancelWork() { m_cancel->store(true); }
void SpokenMediaService::run(const QString &operation, Job job, int generation,
                             const QString &transferToken, std::function<void(const Result &)> finished,
                             bool background) {
    if (m_stopping)
        return;
    // Background jobs (older feed pages) are quiet: no status line, no
    // foreground count, and they do not make the service busy.
    const bool foreground = transferToken.isEmpty() && !background;
    if (foreground && !m_foregroundPending)
        m_cancel->store(false);
    ++m_pending;
    if (background)
        ++m_backgroundPending;
    if (foreground) {
        ++m_foregroundPending;
        m_status = operation;
    }
    emit busyChanged();
    QPointer<SpokenMediaService> self(this);
    const auto root = m_root;
    const auto cancel = !transferToken.isEmpty() ? m_transferCancels.value(transferToken)
                        : background            ? m_olderCancel
                                                : m_cancel;
    // A network request runs a nested event loop. Posting every job directly
    // to that worker lets the next database job execute inside the download.
    // Dispatch exactly one job until its completion returns to the UI thread.
    m_jobs.push_back([self, root, cancel, operation, job = std::move(job), generation,
                      transferToken, finished = std::move(finished), foreground, background] {
        if (!self || self->m_stopping)
            return;
        if (foreground)
            self->m_status = operation;
        emit self->busyChanged();
        QMetaObject::invokeMethod(
            self->m_worker,
            [self, root, cancel, operation, job = std::move(job), generation, transferToken,
             finished = std::move(finished), foreground, background] {
                Result result;
                SpokenStore store(root);
                if (cancel->load() && operation != "position")
                    result.error = "Cancelled";
                else if (store.open(&result.error)) {
                    job(store, result, cancel.get());
                    // Episodes stay in SQLite; only their per-show counts
                    // travel back, for the shows this job wrote.
                    QString readError;
                    result.rows = store.collectionRows(&readError);
                    result.touched = store.takeTouchedShows();
                    result.allStats = operation == "load";
                    if (result.allStats || !result.touched.isEmpty())
                        result.stats = store.episodeStats(result.allStats ? QSet<QString>() : result.touched,
                                                          &readError);
                    if (result.error.isEmpty())
                        result.error = readError;
                }
                if (!self)
                    return;
                QMetaObject::invokeMethod(
                    self,
                    [self, result = std::move(result), operation, generation, transferToken,
                     finished = std::move(finished), foreground, background] {
                        if (!self)
                            return;
                        --self->m_pending;
                        if (background)
                            --self->m_backgroundPending;
                        if (foreground)
                            --self->m_foregroundPending;
                        if (!self->m_foregroundPending)
                            self->m_status.clear();
                        emit self->busyChanged();
                        if (result.error.isEmpty() || !result.rows.isEmpty()) {
                            self->m_rows = result.rows;
                            self->m_byId.clear();
                            self->m_children.clear();
                            for (const auto &v : result.rows) {
                                const auto row = v.toMap();
                                self->m_byId.insert(row.value("id").toString(), row);
                                self->m_children[row.value("parent").toString()].append(row);
                            }
                            if (result.allStats)
                                self->m_stats = result.stats;
                            else
                                for (const auto &show : result.touched)
                                    self->m_stats.insert(show, result.stats.value(show));
                            self->m_episodeCache.clear();
                            ++self->m_revision;
                            emit self->changed();
                        }
                        if (result.hasPreview &&
                            (generation < 0 || generation == self->m_previewGeneration)) {
                            // QML copies the whole property on every read: a
                            // feed preview carries its count, the episodes
                            // stay here for subscribePreview.
                            self->m_preview = result.preview;
                            self->m_previewFeed.clear();
                            if (result.preview.value("kind") == "show") {
                                self->m_previewFeed = result.preview;
                                self->m_preview.remove("episodes");
                                self->m_preview["episodeCount"] = result.preview.value("episodes").toList().size();
                                self->m_preview["hasOlder"] = !result.preview.value("nextPage").toString().isEmpty();
                            }
                            emit self->previewChanged();
                        }
                        if (!transferToken.isEmpty()) {
                            self->m_transferCancels.remove(transferToken);
                            emit self->transferPrepared(transferToken,
                                                        result.error.isEmpty() ? result.transfers
                                                                               : QVariantList(),
                                                        result.error);
                        } else if (background) {
                            if (!result.error.isEmpty() && result.error != "Cancelled")
                                emit self->notification(result.error);
                        } else {
                            if (!result.error.isEmpty() && operation != "position")
                                emit self->notification(result.error);
                            else if (!result.message.isEmpty())
                                emit self->notification(result.message);
                            emit self->operationFinished(operation, result.id,
                                                         result.error.isEmpty(), result.error);
                        }
                        if (finished)
                            finished(result);
                        self->m_jobs.pop_front();
                        if (self->m_stopping)
                            self->m_jobs.clear();
                        if (self->m_jobs.empty())
                            self->m_dispatching = false;
                        else {
                            auto next = self->m_jobs.front();
                            next();
                        }
                    },
                    Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    });
    if (!m_dispatching) {
        m_dispatching = true;
        auto next = m_jobs.front();
        next();
    }
}
SpokenStore *SpokenMediaService::reader() const {
    if (!m_reader)
        m_reader = std::make_unique<SpokenStore>(m_root);
    QString error;
    // Opening fails until the worker has created the database; try again later.
    if (!m_reader->openReader(&error)) {
        m_reader.reset();
        return nullptr;
    }
    return m_reader.get();
}
QVariantMap SpokenMediaService::item(const QString &id) const {
    const auto found = m_byId.constFind(id);
    if (found != m_byId.cend())
        return *found;
    if (id.isEmpty())
        return {};
    // Not a show, book, part or collection: an episode, read from SQLite.
    const auto cached = m_episodeCache.constFind(id);
    if (cached != m_episodeCache.cend())
        return *cached;
    auto *db = reader();
    if (!db)
        return {};
    QString error;
    auto row = db->get(id, &error);
    if (!error.isEmpty() || row.value("kind") != "episode")
        return {};
    if (m_episodeCache.size() > 5000)
        m_episodeCache.clear();
    m_episodeCache.insert(id, row);
    return row;
}
void SpokenMediaService::setFavorite(const QString &id, bool favorite) {
    run("favorite", [id, favorite](SpokenStore &s, Result &r, const std::atomic_bool *) {
        r.id = id;
        auto row = s.get(id, &r.error);
        if (!r.error.isEmpty())
            return;
        const auto kind = row.value("kind").toString();
        if (kind != "show" && kind != "episode" && kind != "book") {
            r.error = "Choose a podcast show, episode or audiobook to star";
            return;
        }
        row["favorite"] = favorite;
        s.put(&row, &r.error);
    });
}
QVariantMap SpokenMediaService::podcastDeviceIdentity(const QString &id) const {
    const auto episode = item(id);
    const auto show = item(episode.value("parent").toString());
    if (episode.value("kind") != "episode" || show.value("kind") != "show")
        return {};
    const auto feed = show.value("identity").toString();
    const auto source = episode.value("deviceSourceUrl", episode.value("sourceUrl")).toString();
    if (feed.isEmpty() || source.isEmpty())
        return {};
    return {{"feedUrl", feed}, {"sourceUrl", source}};
}
// This is a local identity snapshot, never proof that a device object is played.
// DeviceWorker must correlate it with fresh authenticated device inventory/state.
// Only episodes using the given device audio URLs (on the Zune or queued) are
// read: a library can hold far more episodes than the 5,000 a snapshot allows.
// everything: the Zune's inventory has not been read yet, so every episode
// Zuuned ever sent goes too; the worker correlates them with the live
// inventory either way.
QVariantList SpokenMediaService::podcastCleanupCandidates(const QStringList &sources, bool everything) const {
    QVariantList candidates;
    auto *db = reader();
    if (!db || (sources.isEmpty() && !everything))
        return candidates;
    SpokenStore::EpisodeQuery q;
    q.skipKept = true;
    q.limit = 5001;
    QString error;
    QVariantList rows;
    QSet<QString> seen;
    auto collect = [&] {
        for (const auto &value : db->episodes(q, &error))
            if (!seen.contains(value.toMap().value("id").toString())) {
                seen.insert(value.toMap().value("id").toString());
                rows.append(value);
            }
    };
    if (!sources.isEmpty()) {
        q.sources = sources;
        collect();
    }
    if (everything) {
        // Without the Zune's inventory: every episode Zuuned has sent to a Zune
        // (each keeps the audio URL it was sent with).
        q.sources.clear();
        q.sentOnly = true;
        collect();
    }
    if (!error.isEmpty() || rows.size() > 5000)
        return {};
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto deviceIdentity = podcastDeviceIdentity(row.value("id").toString());
        if (deviceIdentity.isEmpty() || row.value("identity").toString().isEmpty())
            return {}; // Never silently publish an incomplete candidate inventory.
        const auto show = item(row.value("parent").toString());
        candidates.append(QVariantMap{{"spokenId", row.value("id")},
                                     {"album", show.value("title")},
                                     {"showTitle", show.value("title")},
                                     {"posterPath", QUrl(cover(row.value("id").toString())).toLocalFile()},
                                     {"identity", row.value("identity")},
                                     {"feedUrl", deviceIdentity.value("feedUrl")},
                                     {"sourceUrl", deviceIdentity.value("sourceUrl")}, {"keepOnDevice", false},
                                     {"devicePlayedSerials", row.value("devicePlayedSerials").toMap()}});
    }
    return candidates;
}
void SpokenMediaService::setPodcastKeepOnDevice(const QString &id, bool keep) {
    run("podcast retention", [id, keep](SpokenStore &s, Result &r, const std::atomic_bool *) {
        r.id = id;
        auto row = s.get(id, &r.error);
        if (!r.error.isEmpty())
            return;
        if (row.value("kind") != "episode") {
            r.error = "Choose a podcast episode to keep on the Zune";
            return;
        }
        row["keepOnDevice"] = keep;
        s.put(&row, &r.error);
    });
}
void SpokenMediaService::recordDevicePlayed(const QString &token, const QString &serial,
                                           const QVariantList &rows) {
    run("podcast device listening state",
        [token, serial, rows](SpokenStore &s, Result &r, const std::atomic_bool *cancel) {
            if (token.trimmed().isEmpty() || token.size() > 256 || token.contains(QChar(0)) ||
                serial.trimmed().isEmpty() || serial != serial.trimmed() || serial.size() > 512 ||
                serial.contains(QChar(0)) || rows.isEmpty() || rows.size() > 5000) {
                r.error = "Invalid podcast listening-state request";
                return;
            }
            if (!s.begin(&r.error))
                return;
            QSet<QString> seen;
            for (const auto &value : rows) {
                const auto expected = value.toMap();
                const auto id = expected.value("spokenId").toString();
                auto row = s.get(id, &r.error);
                const auto show = s.get(row.value("parent").toString(), &r.error);
                if (cancel->load()) {
                    r.error = "Cancelled";
                } else if (r.error.isEmpty() &&
                    (id.isEmpty() || seen.contains(id) || row.value("kind") != "episode" ||
                     show.value("kind") != "show" || row.value("keepOnDevice").toBool() ||
                     expected.value("keepOnDevice").toBool() ||
                     row.value("identity").toString().isEmpty() ||
                     show.value("identity").toString().isEmpty() ||
                     expected.value("sourceUrl").toString().isEmpty() ||
                     row.value("identity") != expected.value("identity") ||
                     show.value("identity") != expected.value("feedUrl") ||
                     row.value("deviceSourceUrl", row.value("sourceUrl")) != expected.value("sourceUrl"))) {
                    r.error = "Podcast identity or keep-on-device choice changed; cleanup was skipped";
                }
                if (!r.error.isEmpty()) {
                    s.rollback();
                    return;
                }
                seen.insert(id);
                auto devices = row.value("devicePlayedSerials").toMap();
                devices.insert(serial, true);
                row["devicePlayedSerials"] = devices;
                row["played"] = true;
                if (!s.put(&row, &r.error)) {
                    s.rollback();
                    return;
                }
            }
            if (cancel->load()) {
                r.error = "Cancelled";
                s.rollback();
                return;
            }
            if (!s.commit(&r.error)) {
                s.rollback();
                return;
            }
            r.transfers = rows;
        }, -1, {}, [this, token, serial](const Result &result) {
            emit devicePlayedRecorded(token, serial,
                result.error.isEmpty() ? result.transfers : QVariantList(), result.error);
        });
}
QVariantList SpokenMediaService::audiobookCollections() const {
    QVariantList list;
    for (const auto &v : m_rows) {
        auto row = v.toMap();
        if (row.value("kind") != "book-collection")
            continue;
        const auto books = collectionBooks(row.value("id").toString());
        row["count"] = books.size();
        row["cover"] = books.isEmpty() ? QString() : books.first().toMap().value("cover");
        list.append(row);
    }
    return list;
}
QVariantList SpokenMediaService::collectionBooks(const QString &id) const {
    const auto collection = item(id);
    if (collection.value("kind") != "book-collection")
        return {};
    QHash<QString, QVariantMap> books;
    for (const auto &v : collections("book")) {
        const auto row = v.toMap();
        books.insert(row.value("id").toString(), row);
    }
    QVariantList result;
    for (const auto &bookId : collection.value("bookIds").toStringList())
        if (books.contains(bookId))
            result.append(books.value(bookId));
    return result;
}
void SpokenMediaService::saveAudiobookCollection(const QString &id, const QString &title,
                                                 const QStringList &bookIds,
                                                 int expectedRevision) {
    run("save audiobook collection", [id, title, bookIds, expectedRevision](
            SpokenStore &s, Result &r, const std::atomic_bool *) {
        r.id = id;
        if (title.trimmed().isEmpty() || title.size() > 500 || title.contains(QChar::Null) ||
            bookIds.size() > 5000 || expectedRevision == INT_MAX ||
            QSet<QString>(bookIds.begin(), bookIds.end()).size() != bookIds.size()) {
            r.error = "Enter a collection name and a complete list of up to 5,000 distinct books";
            return;
        }
        if (!s.begin(&r.error))
            return;
        QVariantMap row;
        if (id.isEmpty()) {
            if (expectedRevision != 0)
                r.error = "Start a new collection before saving";
            row = {{"kind", "book-collection"},
                   {"identity", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
        } else {
            row = s.get(id, &r.error);
            if (r.error.isEmpty() && (row.value("kind") != "book-collection" ||
                expectedRevision < 1 || row.value("revision").toInt() != expectedRevision))
                r.error = "This collection changed or was removed. Reopen it before saving.";
        }
        for (const auto &bookId : bookIds) {
            if (!r.error.isEmpty())
                break;
            const auto book = s.get(bookId, &r.error);
            if (r.error.isEmpty() && book.value("kind") != "book")
                r.error = "A selected audiobook no longer exists. Review the collection.";
        }
        if (r.error.isEmpty()) {
            row["title"] = title.trimmed();
            row["bookIds"] = bookIds;
            row["revision"] = expectedRevision + 1;
            s.put(&row, &r.error);
        }
        if (!r.error.isEmpty() || !s.commit(&r.error)) {
            s.rollback();
            return;
        }
        r.id = row.value("id").toString();
    });
}
void SpokenMediaService::deleteAudiobookCollection(const QString &id, int expectedRevision) {
    run("delete audiobook collection", [id, expectedRevision](
            SpokenStore &s, Result &r, const std::atomic_bool *) {
        r.id = id;
        if (!s.begin(&r.error))
            return;
        const auto row = s.get(id, &r.error);
        if (r.error.isEmpty() && (row.value("kind") != "book-collection" ||
            expectedRevision < 1 || row.value("revision").toInt() != expectedRevision))
            r.error = "This collection changed or was removed. Reopen it before deleting.";
        if (r.error.isEmpty())
            s.removeAudiobookCollection(id, &r.error);
        if (!r.error.isEmpty() || !s.commit(&r.error))
            s.rollback();
    });
}
QVariantList SpokenMediaService::collections(const QString &kind) const {
    QVariantList list;
    for (const auto &v : m_children.value(QString())) {
        auto row = v.toMap();
        if (row.value("kind").toString() != kind)
            continue;
        if (kind == "show") {
            const auto stats = m_stats.value(row.value("id").toString());
            row["count"] = stats.first;
            row["downloaded"] = stats.second;
        } else {
            const auto children = m_children.value(row.value("id").toString());
            int downloaded = 0;
            for (const auto &e : children)
                if (!e.toMap().value("path").toString().isEmpty())
                    ++downloaded;
            row["count"] = children.size();
            row["downloaded"] = downloaded;
        }
        row["cover"] = cover(row.value("id").toString());
        list.append(row);
    }
    return list;
}
QVariantList SpokenMediaService::entries(const QString &parent) const {
    if (item(parent).value("kind") == "show") {
        auto *db = reader();
        if (!db)
            return {};
        SpokenStore::EpisodeQuery q;
        q.show = parent;
        q.limit = Podcasts::MaxShowEpisodes;
        QString error;
        const auto rows = db->episodes(q, &error);
        return error.isEmpty() ? rows : QVariantList();
    }
    auto list = m_children.value(parent);
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("order").toInt() < b.toMap().value("order").toInt();
    });
    return list;
}
// What a list row needs: no show notes (up to 64 KiB each), plus the identity
// the Zune knows the episode by, so a row needs no lookup of its own.
QVariantMap SpokenMediaService::listEpisode(QVariantMap row) const {
    row.remove("description");
    row["feedUrl"] = item(row.value("parent").toString()).value("identity");
    row["deviceSource"] = row.value("deviceSourceUrl", row.value("sourceUrl"));
    return row;
}
namespace {
SpokenStore::EpisodeQuery listQuery(const QString &showId, const QVariantMap &options) {
    SpokenStore::EpisodeQuery q;
    q.show = showId;
    q.filter = options.value("filter").toString();
    q.search = options.value("search").toString();
    q.playedSources = options.value("playedSources").toStringList();
    q.starredOnly = options.value("starred").toBool();
    q.newestFirst = !options.value("oldest").toBool();
    return q;
}
} // namespace
QVariantMap SpokenMediaService::episodePage(const QString &showId, const QVariantMap &options) const {
    auto q = listQuery(showId, options);
    const bool range = options.contains("limit");
    const int size = std::clamp(options.value(range ? "limit" : "pageSize", EpisodePageSize).toInt(), 1, 5000);
    QVariantMap page{{"rows", QVariantList()}, {"total", 0}, {"page", 0}, {"pages", 1}, {"pageSize", size}};
    if (showId.isEmpty() ? !q.starredOnly && q.filter != "starred" : item(showId).value("kind") != "show")
        return page;
    auto *db = reader();
    if (!db)
        return page;
    QString error;
    const int total = db->countEpisodes(q, &error);
    const int pages = std::max(1, (total + size - 1) / size);
    const int index = std::clamp(options.value("page").toInt(), 0, pages - 1);
    q.offset = range ? std::max(0, options.value("offset").toInt()) : index * size;
    q.limit = size;
    QVariantList rows;
    for (const auto &row : db->episodes(q, &error))
        rows.append(listEpisode(row.toMap()));
    if (!error.isEmpty())
        return page;
    page["rows"] = rows;
    page["total"] = total;
    page["page"] = range ? q.offset / size : index;
    page["pages"] = pages;
    return page;
}
QStringList SpokenMediaService::episodeIds(const QString &showId, const QVariantMap &options, int limit) const {
    auto q = listQuery(showId, options);
    QStringList ids;
    auto *db = reader();
    if (!db || limit < 1 ||
        (showId.isEmpty() ? !q.starredOnly && q.filter != "starred" : item(showId).value("kind") != "show"))
        return ids;
    q.limit = std::min(limit, 5000);
    QString error;
    for (const auto &row : db->episodes(q, &error))
        ids.append(row.toMap().value("id").toString());
    return error.isEmpty() ? ids : QStringList();
}
QVariantMap SpokenMediaService::showSummary(const QString &showId) const {
    const auto show = item(showId);
    if (show.value("kind") != "show")
        return {};
    const auto stats = m_stats.value(showId);
    return {{"count", stats.first},
            {"downloaded", stats.second},
            {"olderState", show.value("olderState")},
            {"olderError", show.value("olderError")},
            {"readingOlder", m_walks.contains(showId)}};
}
QString SpokenMediaService::resumeEpisode(const QString &showId) const {
    auto *db = reader();
    if (!db || item(showId).value("kind") != "show")
        return {};
    SpokenStore::EpisodeQuery q;
    q.show = showId;
    q.filter = "downloaded";
    q.limit = 1;
    q.localUnplayed = true;
    QString error;
    auto rows = db->episodes(q, &error);
    if (rows.isEmpty()) {
        q.localUnplayed = false;
        rows = db->episodes(q, &error);
    }
    return rows.value(0).toMap().value("id").toString();
}
bool SpokenMediaService::showOnDevice(const QString &showId, const QStringList &deviceSources) const {
    auto *db = reader();
    if (!db || deviceSources.isEmpty() || item(showId).value("kind") != "show")
        return false;
    SpokenStore::EpisodeQuery q;
    q.show = showId;
    q.sources = deviceSources;
    q.limit = 1;
    QString error;
    return !db->episodes(q, &error).isEmpty();
}
QString SpokenMediaService::cover(const QString &id) const {
    const auto row = item(id);
    if (!row.value("customArt").toString().isEmpty())
        return row.value("customArt").toString();
    const auto parent = item(row.value("parent").toString());
    if (!parent.value("customArt").toString().isEmpty())
        return parent.value("customArt").toString();
    const auto local = row.value("autoArt").toString();
    return local.isEmpty() ? parent.value("autoArt").toString() : local;
}
void SpokenMediaService::beginForeground(const QString &operation) {
    if (!m_foregroundPending)
        m_cancel->store(false);
    ++m_pending;
    ++m_foregroundPending;
    m_status = operation;
    emit busyChanged();
}
void SpokenMediaService::endForeground() {
    --m_pending;
    --m_foregroundPending;
    if (!m_foregroundPending)
        m_status.clear();
    emit busyChanged();
}
void SpokenMediaService::netFetch(NetLane &lane, const QString &url, bool withCover,
                                  std::shared_ptr<std::atomic_bool> cancel,
                                  std::function<void(const QVariantMap &, const QString &)> done) {
    QPointer<SpokenMediaService> self(this);
    NetLane *l = &lane;
    const auto root = m_root;
    lane.tasks.push_back([self, l, url, withCover, cancel, root, done = std::move(done)] {
        QMetaObject::invokeMethod(l->worker, [self, l, url, withCover, cancel, root, done] {
            QString error;
            const auto feed = readFeed(root, QUrl(url), cancel.get(), &error, withCover);
            QMetaObject::invokeMethod(self, [self, l, feed, error, done] {
                if (!self)
                    return;
                l->tasks.pop_front();
                l->running = false;
                if (self->m_stopping)
                    return;
                done(feed, error);
                self->nextNet(*l);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    });
    nextNet(lane);
}
void SpokenMediaService::nextNet(NetLane &lane) {
    if (lane.running || lane.tasks.empty() || m_stopping)
        return;
    lane.running = true;
    lane.tasks.front()();
}
void SpokenMediaService::previewFeed(const QUrl &input) {
    QUrl url = input;
    url.setFragment({});
    m_preview.clear();
    m_previewFeed.clear();
    emit previewChanged();
    const int generation = ++m_previewGeneration;
    beginForeground("preview feed");
    netFetch(m_feedLane, url.toString(QUrl::FullyEncoded), true, m_cancel,
             [this, generation](const QVariantMap &feed, const QString &error) {
        endForeground();
        // QML copies the whole property on every read: a feed preview carries
        // its count; the episodes stay here for subscribePreview.
        if (generation == m_previewGeneration && error.isEmpty()) {
            m_previewFeed = feed;
            m_preview = feed;
            m_preview.remove("episodes");
            m_preview["episodeCount"] = feed.value("episodes").toList().size();
            m_preview["hasOlder"] = !feed.value("nextPage").toString().isEmpty();
            emit previewChanged();
        }
        if (!error.isEmpty())
            emit notification(error);
        emit operationFinished("preview feed", {}, error.isEmpty(), error);
    });
}
void SpokenMediaService::subscribePreview() {
    const auto preview = m_previewFeed;
    if (preview.value("kind") != "show") {
        emit notification("Preview a feed first");
        return;
    }
    run("subscribe", [preview](SpokenStore &s, Result &r, const std::atomic_bool *) {
        if (saveFeed(s, preview, &r.id, &r.error, &r.added)) {
            r.older = preview.value("nextPage").toString();
            r.message = "Podcast saved to your library";
        }
    }, -1, {}, [this](const Result &r) {
        if (r.error.isEmpty())
            planOlder(r.id, r.older, r.added, true);
    });
}
void SpokenMediaService::refreshFeed(const QString &id) {
    refreshFeedImpl(id, false, {});
}
void SpokenMediaService::refreshFeedForQueue(const QString &id, std::function<void(bool)> finished) {
    refreshFeedImpl(id, true, std::move(finished));
}
void SpokenMediaService::refreshFeedImpl(const QString &id, bool quiet, std::function<void(bool)> finished) {
    const auto current = item(id);
    if (current.value("kind") != "show") {
        emit notification("Show no longer exists");
        emit operationFinished("refresh feed", id, false, "Show no longer exists");
        if (finished) finished(false);
        return;
    }
    beginForeground("refresh feed");
    netFetch(m_feedLane, current.value("url").toString(), true, m_cancel,
             [this, quiet, finished = std::move(finished)](const QVariantMap &data, const QString &error) mutable {
        if (!error.isEmpty()) {
            endForeground();
            emit notification(error);
            emit operationFinished("refresh feed", {}, false, error);
            if (finished) finished(false);
            return;
        }
        run("refresh feed", [data, quiet](SpokenStore &s, Result &r, const std::atomic_bool *) {
            if (saveFeed(s, data, &r.id, &r.error, &r.added)) {
                r.older = data.value("nextPage").toString();
                if (!quiet)
                    r.message = "Feed refreshed; downloads and your cover were preserved";
            }
        }, -1, {}, [this, quiet, finished = std::move(finished)](const Result &r) {
            // The newest page is saved: the queue planner can go on while any
            // older pages are read in the background.
            if (r.error.isEmpty())
                planOlder(r.id, r.older, r.added, !quiet);
            if (finished) finished(r.error.isEmpty());
        });
        endForeground();
    });
}
// Feeds that page their back catalogue (RFC 5005; Omny, Audacy and Audioboom
// do) hold only the newest episodes in their first document. After the first
// page is saved, older pages are read one at a time: downloaded and parsed in
// the older-page lane, then saved by a short background job.
//  - A new subscription is saved as "loading" with its cursor (olderPage) on
//    the first older page and reads to the last one. Quitting keeps "loading";
//    the walk resumes when the library next loads.
//  - A failed page leaves "incomplete": the next refresh retries that page.
//    After two walks in a row failed without adding an episode, the walk
//    starts again from the first older page (the cursor URL may have
//    expired); after three, only a refresh the user asks for tries again.
//  - A complete show reads on only while pages hold new episodes, because new
//    episodes push older ones down a page ("catching up"). With an unfinished
//    walk the pages pushed down come first, then the cursor.
// New episodes only push items later, so a stale cursor repeats items and
// never skips one. Older pages only add episodes: a repeated GUID on an older
// page never overwrites the newer copy. "limited" means MaxShowEpisodes, the
// page limit, or older pages offered only over plain HTTP by an HTTPS feed.
void SpokenMediaService::planOlder(const QString &showId, const QString &next, int added, bool manual) {
    const auto show = item(showId);
    if (show.value("kind") != "show" || m_walks.contains(showId) || m_stopping)
        return;
    const auto state = show.value("olderState").toString();
    const int failures = show.value("olderFailures").toInt();
    Walk walk;
    if (state == "limited")
        return;
    if (state == "complete") {
        if (next.isEmpty() || added == 0)
            return;
        walk.url = next;
        walk.catchUp = true;
    } else {
        if (state == "incomplete" && failures >= 3 && !manual)
            return;
        auto cursor = show.value("olderPage").toString();
        if (state == "incomplete" && failures >= 2 && !next.isEmpty())
            cursor = next;
        if (!cursor.isEmpty() && !next.isEmpty() && added > 0 && next != cursor) {
            walk.url = next;
            walk.catchUp = true;
            walk.resume = cursor;
        } else if (!cursor.isEmpty()) {
            walk.url = cursor;
        } else if (!next.isEmpty()) {
            walk.url = next;
        } else {
            return;
        }
    }
    walk.secure = QUrl(show.value("responseUrl").toString()).scheme() == "https" ||
                  QUrl(show.value("url").toString()).scheme() == "https";
    // A page linking back to the feed itself ends the chain too.
    walk.base = {show.value("url").toString(), show.value("responseUrl").toString()};
    walk.visited = walk.base;
    walk.visited.remove(walk.url);
    m_olderCancel->store(false);
    m_walks.insert(showId, walk);
    emit busyChanged();
    readOlder(showId);
}
void SpokenMediaService::readOlder(const QString &showId) {
    auto walk = m_walks.value(showId);
    if (walk.url.isEmpty() || walk.visited.contains(walk.url) || m_stopping) {
        m_walks.remove(showId);
        emit busyChanged();
        return;
    }
    // Never an HTTPS feed's pages over plain HTTP, a saved cursor included. A
    // catch-up just stops; a walk ends "limited", saying why.
    if (walk.secure && QUrl(walk.url).scheme() == "http") {
        m_walks.remove(showId);
        emit busyChanged();
        if (!walk.catchUp)
            run("older episodes", [showId](SpokenStore &s, Result &r, const std::atomic_bool *) {
                auto show = s.get(showId, &r.error);
                if (show.value("kind") != "show" || !r.error.isEmpty())
                    return;
                show["olderState"] = "limited";
                show["olderPage"] = QString();
                show["olderError"] = "The feed offers its older pages only over unencrypted HTTP";
                s.put(&show, &r.error);
            }, -1, {}, {}, true);
        return;
    }
    walk.visited.insert(walk.url);
    ++walk.pages;
    m_walks.insert(showId, walk);
    const auto url = walk.url;
    netFetch(m_olderLane, url, false, m_olderCancel, [this, showId, url](const QVariantMap &feed, const QString &error) {
        saveOlder(showId, url, feed, error);
    });
}
void SpokenMediaService::saveOlder(const QString &showId, const QString &url, const QVariantMap &feed,
                                   const QString &fetchError) {
    const auto walk = m_walks.value(showId);
    const bool catchUp = walk.catchUp, progress = walk.added > 0;
    const int pages = walk.pages;
    const auto visited = walk.visited;
    run("older episodes", [showId, url, feed, fetchError, catchUp, progress, pages, visited](
            SpokenStore &s, Result &r, const std::atomic_bool *) {
        r.id = showId;
        auto show = s.get(showId, &r.error);
        if (show.value("kind") != "show" || !r.error.isEmpty()) {
            r.error = "Show no longer exists";
            return;
        }
        const auto title = show.value("title").toString();
        if (pages > Podcasts::MaxFeedPages) {
            show["olderState"] = "limited";
            show["olderPage"] = QString();
            show["olderError"] = QStringLiteral("Zuuned reads at most %1 pages of a feed").arg(Podcasts::MaxFeedPages);
            s.put(&show, &r.error);
            return;
        }
        if (!fetchError.isEmpty()) {
            // A walk that failed before adding anything counts towards giving
            // up; any new episode resets the count. Page URLs may change on
            // every response, so this measures progress, not addresses.
            show["olderFailures"] = progress ? 1 : show.value("olderFailures").toInt() + 1;
            show["olderState"] = "incomplete";
            show["olderPage"] = url;
            show["olderError"] = fetchError;
            QString ignored;
            s.put(&show, &ignored);
            r.error = QStringLiteral("Older episodes of %1 stopped: %2").arg(title, fetchError);
            return;
        }
        const auto episodes = feed.value("episodes").toList();
        r.older = feed.value("nextPage").toString();
        QString limit;
        // An empty page ends the chain, as does a link back to a page read.
        if ((episodes.isEmpty() && feed.value("skippedItems").toInt() == 0) || visited.contains(r.older))
            r.older.clear();
        if (QUrl(url).scheme() == "https" && QUrl(r.older).scheme() == "http") {
            if (!catchUp)
                limit = "The feed offers its older pages only over unencrypted HTTP";
            r.older.clear();
        }
        bool full = false;
        if (!s.begin(&r.error))
            return;
        if (!saveEpisodes(s, showId, episodes, &r.added, &full, &r.error, true)) {
            s.rollback();
            return;
        }
        if (full)
            limit = QStringLiteral("Zuuned keeps at most %1 episodes of a show")
                        .arg(QLocale(QLocale::English).toString(Podcasts::MaxShowEpisodes));
        if (!limit.isEmpty()) {
            show["olderState"] = "limited";
            show["olderPage"] = QString();
            show["olderError"] = limit;
            r.older.clear();
        } else if (!catchUp) {
            // Catching up never moves the cursor of an unfinished walk.
            show["olderState"] = r.older.isEmpty() ? "complete" : "loading";
            show["olderPage"] = r.older;
            show["olderError"] = QString();
        }
        if (r.added > 0 || show.value("olderState") == "complete")
            show.remove("olderFailures");
        if (!s.put(&show, &r.error) || !s.commit(&r.error))
            s.rollback();
    }, -1, {}, [this, showId](const Result &r) {
        auto walk = m_walks.value(showId);
        walk.added += r.added;
        const bool ok = r.error.isEmpty() && item(showId).value("olderState") != "limited";
        if (ok && walk.catchUp && r.added > 0 && !r.older.isEmpty()) {
            walk.url = r.older;
        } else if (ok && walk.catchUp && !walk.resume.isEmpty()) {
            // On from the cursor. Pages read while catching up do not count
            // as visited: the cursor's chain may run through them.
            walk.url = walk.resume;
            walk.resume.clear();
            walk.catchUp = false;
            walk.visited = walk.base;
        } else if (ok && !walk.catchUp && !r.older.isEmpty()) {
            walk.url = r.older;
        } else {
            m_walks.remove(showId);
            emit busyChanged();
            return;
        }
        m_walks.insert(showId, walk);
        readOlder(showId);
    }, true);
}
void SpokenMediaService::previewBook(const QList<QUrl> &urls) {
    m_preview.clear();
    m_previewFeed.clear();
    emit previewChanged();
    const int generation = ++m_previewGeneration;
    run(
        "inspect audiobook",
        [urls](SpokenStore &store, Result &r, const std::atomic_bool *cancel) {
            r.hasPreview = true;
            if (urls.isEmpty() || urls.size() > 5000) {
                r.error = "Choose between 1 and 5,000 files for one book";
                return;
            }
            r.preview = inspectBook(store.root(), urls, &r.error, cancel);
        },
        generation);
}
void SpokenMediaService::importBook(const QString &title, const QString &author,
                                    const QString &narrator, const QStringList &order) {
    const auto draft = m_preview;
    run("import book", [draft, title, author, narrator, order](SpokenStore &s, Result &r,
                                                               const std::atomic_bool *) {
        if (!s.begin(&r.error))
            return;
        const int result = writeBook(s, draft, title, author, narrator, order, &r.id, &r.error);
        if (result < 0 || !s.commit(&r.error)) {
            s.rollback();
            return;
        }
        if (result == 0) {
            r.message = "This book is already in your library";
            return;
        }
        r.message = "Audiobook imported; original files preserved";
    });
}
void SpokenMediaService::download(const QStringList &ids) {
    run("download episodes", [ids](SpokenStore &s, Result &r, const std::atomic_bool *cancel) {
        QDir().mkpath(s.root() + "/episodes");
        int count = 0;
        for (const auto &id : ids) {
            if (cancel->load()) {
                r.error = "Cancelled; completed downloads were kept";
                return;
            }
            auto row = s.get(id, &r.error);
            if (!r.error.isEmpty())
                return;
            if (row.value("kind") != "episode") {
                r.error = "Episode no longer exists";
                return;
            }
            if (QFileInfo::exists(row.value("path").toString()))
                continue;
            const QString path = s.root() + "/episodes/" + id + ".audio";
            QSaveFile file(path);
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly)) {
                r.error = "Cannot create download file";
                return;
            }
            auto response =
                fetch(QUrl(row.value("sourceUrl").toString()), 512LL * 1024 * 1024, cancel, &file);
            if (!response.error.isEmpty()) {
                r.error = response.error;
                return;
            }
            if (!file.commit()) {
                r.error = "Cannot finish download";
                return;
            }
            AudiobookPart audio;
            if (!Audiobooks::probePart(path, &audio, &r.error, cancel)) {
                QFile::remove(path);
                return;
            }
            row["path"] = path;
            row["bytes"] = audio.bytes;
            row["durationMs"] = audio.durationMs;
            row["codec"] = audio.codec;
            QString artError;
            QString art;
            if (!audio.embeddedArtwork.isEmpty())
                art = prepareCover(s.root(), audio.embeddedArtwork, &artError);
            if (art.isEmpty())
                art = remoteCover(s.root(), QUrl(row.value("artUrl").toString()), cancel);
            if (!art.isEmpty()) {
                row["autoArt"] = art;
                auto show = s.get(row.value("parent").toString(), &r.error);
                if (show.value("autoArt").toString().isEmpty()) {
                    show["autoArt"] = art;
                    if (!s.put(&show, &r.error))
                        return;
                }
            }
            if (!s.put(&row, &r.error))
                return;
            ++count;
        }
        r.message = QStringLiteral("%1 episode(s) downloaded").arg(count);
    });
}
void SpokenMediaService::saveCover(const QString &id, const QUrl &source, bool reset,
                                   int expected) {
    run("save cover",
        [id, source, reset, expected](SpokenStore &s, Result &r, const std::atomic_bool *) {
            r.id = id;
            auto row = s.get(id, &r.error);
            if (row.isEmpty()) {
                r.error = "This item no longer exists";
                return;
            }
            if (row.value("coverRevision").toInt() != expected) {
                r.error = "The cover changed since you opened Customize. Reopen it and "
                          "try again.";
                return;
            }
            QString art;
            if (!reset) {
                QFile file(localPath(source));
                if (!file.open(QIODevice::ReadOnly) || file.size() > 10 * 1024 * 1024) {
                    r.error = "Choose a local image under 10 MiB";
                    return;
                }
                art = prepareCover(s.root(), file.readAll(), &r.error);
                if (!r.error.isEmpty())
                    return;
            }
            row["customArt"] = art;
            row["coverRevision"] = expected + 1;
            if (s.put(&row, &r.error))
                r.message = "Cover saved";
        });
}
void SpokenMediaService::markPlayed(const QString &id, bool played) {
    run("listening state", [id, played](SpokenStore &s, Result &r, const std::atomic_bool *) {
        auto row = s.get(id, &r.error);
        if (row.isEmpty()) {
            r.error = "Item no longer exists";
            return;
        }
        row["played"] = played;
        s.put(&row, &r.error);
    });
}
void SpokenMediaService::addBookmark(const QString &id, double position, const QString &note) {
    run("bookmark", [id, position, note](SpokenStore &s, Result &r, const std::atomic_bool *) {
        auto row = s.get(id, &r.error);
        if (row.isEmpty() || position < 0 || !std::isfinite(position)) {
            r.error = "No spoken item is playing";
            return;
        }
        auto bookmarks = row.value("bookmarks").toList();
        if (bookmarks.size() >= 10000) {
            r.error = "Bookmark limit reached";
            return;
        }
        bookmarks.append(QVariantMap{{"positionMs", qint64(position)}, {"note", note.left(1000)}});
        row["bookmarks"] = bookmarks;
        if (s.put(&row, &r.error))
            r.message = "Bookmark saved";
    });
}
void SpokenMediaService::attachPlayer(PlayerService *player) {
    m_player = player;
    if (!player)
        return;
    connect(player, &PlayerService::positionChanged, this, [this] {
        if (m_player->durationMs() <= 0)
            return;
        const auto q = m_player->queue();
        const int i = m_player->queueIndex();
        if (i < 0 || i >= q.size())
            return;
        const QString id = q[i].toMap().value("spokenId").toString();
        if (id != m_positionId) {
            savePosition(true);
            m_positionId = id;
        }
        m_position = qMax(qint64(0), qint64(m_player->positionMs()));
    });
    connect(player, &PlayerService::stateChanged, this, [this] {
        if (!m_player->playing())
            savePosition(true);
    });
}
void SpokenMediaService::savePosition(bool flush) {
    // Older feed pages are background work: the periodic save goes on, one
    // queued save at a time.
    if (m_positionId.isEmpty() || (!flush && (busy() || m_positionPending)))
        return;
    const auto id = m_positionId;
    const auto position = m_position;
    const auto saved = item(id);
    if (saved.value("listenedAt").toLongLong() > 0 &&
        saved.value("positionMs").toLongLong() == position)
        return;
    m_positionPending = true;
    run("position", [id, position](SpokenStore &s, Result &r, const std::atomic_bool *) {
        auto row = s.get(id, &r.error);
        if (row.isEmpty())
            return;
        row["positionMs"] = position;
        row["listenedAt"] = QDateTime::currentMSecsSinceEpoch();
        s.put(&row, &r.error);
    }, -1, {}, [this](const Result &) { m_positionPending = false; });
}
void SpokenMediaService::play(const QString &id, double position) {
    if (!m_player || !std::isfinite(position))
        return;
    auto row = item(id);
    if (row.value("kind") == "book") {
        const auto parts = entries(id);
        if (parts.isEmpty())
            return;
        row = parts.first().toMap();
        for (const auto &part : parts)
            if (part.toMap().value("listenedAt").toLongLong() >
                row.value("listenedAt").toLongLong())
                row = part.toMap();
    }
    const QString path = row.value("path").toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        emit notification("Download this episode or restore its source file first");
        return;
    }
    savePosition(true);
    const auto parent = item(row.value("parent").toString());
    const QString nextId = row.value("id").toString();
    const qint64 nextPosition =
        qMax(qint64(0), position < 0 ? row.value("positionMs").toLongLong() : qint64(position));
    const auto source =
        parent.value("kind") == "book" ? entries(parent.value("id").toString()) : QVariantList{row};
    QVariantList queue;
    int start = 0;
    for (const auto &v : source) {
        const auto part = v.toMap();
        if (!QFileInfo::exists(part.value("path").toString())) {
            emit notification("A book part is missing. Restore its source file first.");
            return;
        }
        const bool current = part.value("id").toString() == nextId;
        if (current)
            start = queue.size();
        queue.append(
            QVariantMap{{"filepath", part.value("path")},
                        {"title", part.value("title")},
                        {"artist", parent.value("author")},
                        {"album", parent.value("title")},
                        {"libraryId", -1},
                        {"spokenId", part.value("id")},
                        {"spokenKind", parent.value("kind") == "book" ? "audiobook" : "podcast"},
                        {"startPositionMs", current ? nextPosition : qint64(0)}});
    }
    m_player->setQueue(queue, start);
}

void SpokenMediaService::previewBookFolder(const QUrl &folder, bool collection) {
    m_preview.clear();
    m_previewFeed.clear();
    emit previewChanged();
    const int generation = ++m_previewGeneration;
    run(
        "inspect book folder",
        [folder, collection](SpokenStore &store, Result &r, const std::atomic_bool *cancel) {
            r.hasPreview = true;
            const QFileInfo selected(localPath(folder));
            const QString root = selected.canonicalFilePath();
            if (!folder.isLocalFile() || !selected.isDir() || root.isEmpty()) {
                r.error = "Choose a readable local audiobook folder";
                return;
            }
            QMap<QString, QStringList> groups;
            const QSet<QString> extensions = {"m4b", "m4a",  "mp3", "flac",
                                              "ogg", "opus", "wav", "wma"};
            QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            int visited = 0, files = 0;
            while (it.hasNext()) {
                if (cancel->load()) {
                    r.error = "Cancelled";
                    return;
                }
                it.next();
                if (++visited > 100000) {
                    r.error = "Folder is too large; choose a smaller book collection";
                    return;
                }
                const auto file = it.fileInfo();
                if (file.isDir() && !QDir(file.absoluteFilePath()).isReadable()) {
                    r.error = "A subfolder cannot be read. Fix its access permissions "
                              "and try again.";
                    return;
                }
                if (!file.isFile() || !extensions.contains(file.suffix().toLower()))
                    continue;
                if (++files > 5000) {
                    r.error = "Import up to 5,000 audio files at a time";
                    return;
                }
                const QString relative = QDir(root).relativeFilePath(file.absoluteFilePath());
                const QString group =
                    collection && relative.contains('/') ? relative.section('/', 0, 0) : QString();
                groups[group].append(file.absoluteFilePath());
            }
            if (groups.isEmpty()) {
                r.error = "No supported audiobook files found in this folder";
                return;
            }
            QCollator natural(QLocale(QLocale::English));
            natural.setNumericMode(true);
            natural.setCaseSensitivity(Qt::CaseInsensitive);
            QVariantList books;
            for (auto g = groups.begin(); g != groups.end(); ++g) {
                std::sort(g->begin(), g->end(), [&natural](const QString &a, const QString &b) {
                    return natural.compare(a, b) < 0;
                });
                QList<QUrl> urls;
                for (const auto &path : *g)
                    urls.append(QUrl::fromLocalFile(path));
                auto book = inspectBook(store.root(), urls, &r.error, cancel);
                if (!r.error.isEmpty())
                    return;
                book["folder"] = g.key().isEmpty() ? root : QDir(root).filePath(g.key());
                const auto parts = book.value("parts").toList();
                if (!parts.first().toMap().value("bookTitleFromTag").toBool())
                    book["title"] = QFileInfo(book.value("folder").toString()).fileName();
                book["index"] = books.size();
                books.append(book);
            }
            r.preview = {
                {"kind", "bookFolder"}, {"folder", root}, {"books", books}, {"files", files}};
        },
        generation);
}
void SpokenMediaService::importBookFolder(const QVariantList &reviewed) {
    const auto preview = m_preview;
    run("import book folder", [preview, reviewed](SpokenStore &s, Result &r,
                                                  const std::atomic_bool *cancel) {
        const auto books = preview.value("books").toList();
        QSet<int> used;
        int added = 0, reused = 0;
        if (preview.value("kind") != "bookFolder" || reviewed.isEmpty() ||
            reviewed.size() > books.size()) {
            r.error = "Preview and review the folder first";
            return;
        }
        if (!s.begin(&r.error))
            return;
        for (const auto &value : reviewed) {
            const auto edit = value.toMap();
            const int index = edit.value("index", -1).toInt();
            if (cancel->load() || index < 0 || index >= books.size() || used.contains(index)) {
                r.error = "Cancelled or invalid book selection";
                s.rollback();
                return;
            }
            used.insert(index);
            QStringList order;
            for (const auto &path : edit.value("order").toList())
                order.append(path.toString());
            const int result = writeBook(s, books[index].toMap(), edit.value("title").toString(),
                                         edit.value("author").toString(),
                                         edit.value("narrator").toString(), order, &r.id, &r.error);
            if (result < 0) {
                s.rollback();
                return;
            }
            if (result)
                ++added;
            else
                ++reused;
        }
        if (!s.commit(&r.error)) {
            s.rollback();
            return;
        }
        r.message =
            QStringLiteral("%1 books imported; %2 already in your library").arg(added).arg(reused);
    });
}
void SpokenMediaService::skipChapter(int direction) {
    if (!m_player || m_player->stopped())
        return;
    const auto queue = m_player->queue();
    const int index = m_player->queueIndex();
    if (index < 0 || index >= queue.size())
        return;
    const auto row = item(queue[index].toMap().value("spokenId").toString());
    if (row.isEmpty())
        return;
    const auto chapters = row.value("chapters").toList();
    const auto position = m_player->positionMs();
    if (direction > 0) {
        for (const auto &ch : chapters) {
            const auto start = ch.toMap().value("startMs").toLongLong();
            if (start > position + 250) {
                m_player->seekMs(start);
                return;
            }
        }
        if (index + 1 < queue.size())
            play(queue[index + 1].toMap().value("spokenId").toString(), 0);
    } else {
        for (auto it = chapters.crbegin(); it != chapters.crend(); ++it) {
            const auto start = it->toMap().value("startMs").toLongLong();
            if (start < position - 3000) {
                m_player->seekMs(start);
                return;
            }
        }
        if (position > 3000 || index == 0)
            m_player->seekMs(0);
        else
            play(queue[index - 1].toMap().value("spokenId").toString(), 0);
    }
}

QVariantList SpokenMediaService::orderedPodcastEpisodes(const QString &showId, const QString &order,
                                                        int offset, int limit) const {
    if (item(showId).value("kind") != "show" || (order != "newest" && order != "oldest")) return {};
    auto *db = reader();
    if (!db) return {};
    // Real instants (offsets honoured); equal ones by identity, then ID; and
    // undated or invalid dates after every dated episode in both directions.
    SpokenStore::EpisodeQuery q;
    q.show = showId;
    q.identityTies = true;
    q.newestFirst = order == "newest";
    q.offset = offset;
    q.limit = limit;
    QString error;
    const auto rows = db->episodes(q, &error);
    return error.isEmpty() ? rows : QVariantList();
}
QVariantList SpokenMediaService::selectPodcastEpisodes(const QString &showId, const QString &order,
                                                       int count, bool unplayedOnly,
                                                       const QStringList &playedSources) const {
    if (count < 1 || count > 5000 || item(showId).value("kind") != "show" ||
        (order != "newest" && order != "oldest")) return {};
    auto *db = reader();
    if (!db) return {};
    SpokenStore::EpisodeQuery q;
    q.show = showId;
    q.identityTies = true;
    q.newestFirst = order == "newest";
    q.limit = count;
    if (unplayedOnly) {
        q.filter = "unplayed";
        q.playedSources = playedSources;
    }
    QString error;
    QVariantList selected;
    for (const auto &row : db->episodes(q, &error))
        selected.append(listEpisode(row.toMap()));
    return error.isEmpty() ? selected : QVariantList();
}
bool SpokenMediaService::podcastSourcesAmbiguous(const QString &showId) const {
    auto *db = reader();
    QString error;
    return !db || db->ambiguousSources(showId, &error) > 0 || !error.isEmpty();
}
void SpokenMediaService::setPodcastSyncSelection(const QString &showId, const QString &order,
                                                 int count, bool unplayedOnly) {
    run("podcast sync selection", [showId, order, count, unplayedOnly](SpokenStore &s, Result &r,
                                                                     const std::atomic_bool *) {
        r.id = showId;
        auto row = s.get(showId, &r.error);
        if (!r.error.isEmpty())
            return;
        if (row.value("kind") != "show" || (order != "newest" && order != "oldest") ||
            count < 1 || count > 5000) {
            r.error = "Choose a podcast show, newest or oldest, and 1 to 5,000 episodes";
            return;
        }
        row["syncOrder"] = order;
        row["syncCount"] = count;
        row["syncUnplayedOnly"] = unplayedOnly;
        s.put(&row, &r.error);
    });
}

// Queue admission reads the already-loaded library only. Downloads, probes and
// source fingerprints belong to prepareTransfer, after SyncEngine starts sync.
void SpokenMediaService::queuePodcasts(const QStringList &ids) {
    if (!m_canTransfer || !m_canTransfer()) {
        emit notification("Connect a Zune before adding episodes.");
        return;
    }
    // Selections can now span pages of a show.
    if (ids.size() > 5000) {
        emit notification("Add up to 5,000 episodes at a time.");
        return;
    }
    if (ids.isEmpty())
        return;
    QVariantList rows;
    QSet<QString> seen;
    for (const auto &id : ids) {
        if (seen.contains(id))
            continue;
        seen.insert(id);
        const auto episode = item(id);
        const auto show = item(episode.value("parent").toString());
        if (episode.value("kind") != "episode" || show.value("kind") != "show") {
            emit notification("Only podcast episodes can use podcast sync.");
            return;
        }
        QString art = episode.value("customArt").toString();
        if (art.isEmpty())
            art = show.value("customArt").toString();
        if (art.isEmpty())
            art = episode.value("autoArt").toString();
        if (art.isEmpty())
            art = show.value("autoArt").toString();
        const auto date = QDateTime::fromString(episode.value("published").toString(), Qt::ISODate);
        rows.append(QVariantMap{
            {"type", "podcast"},
            {"needsPreparation", true},
            {"keepOnDevice", episode.value("keepOnDevice").toBool()},
            {"devicePlayedSerials", episode.value("devicePlayedSerials").toMap()},
            {"spokenId", id},
            {"identity", episode.value("identity")},
            {"feedUrl", show.value("identity")},
            {"sourceUrl", episode.value("deviceSourceUrl", episode.value("sourceUrl"))},
            {"filepath", episode.value("path")},
            {"title", episode.value("title")},
            {"artist", show.value("author")},
            {"album", show.value("title")},
            {"description", episode.value("description")},
            {"durationMs", episode.value("durationMs")},
            {"sourceBytes", episode.value("bytes")},
            {"codec", episode.value("codec")},
            {"posterPath", QUrl(art).toLocalFile()},
            {"dateAuthored",
             date.isValid() ? date.toUTC().toString("yyyyMMdd'T'HHmmss'Z'") : QString()}});
    }
    emit podcastTransferReady(rows);
}

void SpokenMediaService::queueAudiobooks(const QStringList &ids) {
    if (!m_canTransfer || !m_canTransfer()) {
        emit notification("Connect a Zune before adding books.");
        return;
    }
    if (ids.isEmpty() || ids.size() > 5000)
        return;
    QStringList bookIds;
    for (const auto &id : ids) {
        const auto row = item(id);
        if (row.value("kind") != "part" && row.value("kind") != "book") {
            emit notification("Choose an audiobook to sync.");
            return;
        }
        const auto bookId = row.value("kind") == "book" ? id : row.value("parent").toString();
        if (!bookIds.contains(bookId))
            bookIds.append(bookId);
    }
    QVariantList rows;
    for (const auto &bookId : bookIds) {
        const auto book = item(bookId);
        auto parts = m_children.value(bookId);
        std::sort(parts.begin(), parts.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value("order").toInt() < b.toMap().value("order").toInt();
        });
        QVariantList manifest;
        for (const auto &value : parts) {
            const auto part = value.toMap();
            manifest.append(QVariantMap{{"id", part.value("id")},
                                        {"path", part.value("path")},
                                        {"bytes", part.value("bytes")},
                                        {"mtime", part.value("mtime")},
                                        {"durationMs", part.value("durationMs")},
                                        {"chapters", part.value("chapters")}});
        }
        const auto bookKey =
            hash(QJsonDocument::fromVariant(QVariantMap{{"bookId", bookId},
                                                        {"title", book.value("title")},
                                                        {"author", book.value("author")},
                                                        {"parts", manifest}})
                     .toJson(QJsonDocument::Compact));
        QString art = book.value("customArt").toString();
        if (art.isEmpty())
            art = book.value("autoArt").toString();
        int number = 0;
        for (const auto &value : parts) {
            const auto part = value.toMap();
            const auto chapters = part.value("chapters").toList();
            const int count = chapters.isEmpty() ? 1 : chapters.size();
            for (int chapter = 0; chapter < count; ++chapter) {
                const bool ranged = !chapters.isEmpty();
                const qint64 start =
                    chapter == 0 ? 0 : chapters[chapter].toMap().value("startMs").toLongLong();
                const qint64 end = chapter + 1 < count
                                       ? chapters[chapter + 1].toMap().value("startMs").toLongLong()
                                       : part.value("durationMs").toLongLong();
                if (end <= start || end - start > INT_MAX || rows.size() >= 5000) {
                    emit notification("This book's chapter lengths or part count exceed "
                                      "the device transfer limits.");
                    return;
                }
                QString title = ranged
                                    ? chapters[chapter].toMap().value("title").toString().trimmed()
                                    : part.value("title").toString();
                if (title.isEmpty())
                    title = QStringLiteral("Chapter %1").arg(number + 1);
                const auto identity =
                    part.value("id").toString() +
                    (ranged ? QStringLiteral(":chapter:%1:%2").arg(start).arg(end) : QString());
                QVariantMap row{{"type", "audiobook"},
                                {"needsPreparation", true},
                                {"bookId", bookId},
                                {"bookKey", bookKey},
                                {"spokenId", part.value("id")},
                                {"identity", identity},
                                {"partNumber", ++number},
                                {"title", title},
                                {"album", book.value("title")},
                                {"artist", book.value("author")},
                                {"filepath", part.value("path")},
                                {"durationMs", end - start},
                                {"codec", part.value("codec")},
                                {"sourceBytes", part.value("bytes")},
                                {"sourceMtime", part.value("mtime")},
                                {"posterPath", QUrl(art).toLocalFile()}};
                if (ranged) {
                    row["rangeStartMs"] = start;
                    row["rangeEndMs"] = end;
                    row["sourceDurationMs"] = part.value("durationMs");
                }
                rows.append(row);
            }
        }
    }
    if (!rows.isEmpty())
        emit audiobookTransferReady(rows);
}

// Each sync job has its own cancellation flag. Cancelling a transfer must not
// cancel a foreground import/download or another queued transfer.
void SpokenMediaService::cancelTransfer(const QString &token) {
    const auto cancel = m_transferCancels.value(token);
    if (cancel)
        cancel->store(true);
}

void SpokenMediaService::prepareTransfer(const QString &token, const QVariantMap &queuedRow) {
    if (token.isEmpty() || m_transferCancels.contains(token))
        return;
    m_transferCancels.insert(token, std::make_shared<std::atomic_bool>(false));
    run(
        "prepare spoken transfer",
        [queuedRow](SpokenStore &s, Result &r, const std::atomic_bool *cancel) {
            if (queuedRow.value("type") == "audiobook") {
                const QStringList selected{queuedRow.value("bookId").toString()};
                const auto all = s.collectionRows(&r.error);
                if (!r.error.isEmpty())
                    return;
                QMap<QString, QVariantList> books;
                for (const auto &id : selected) {
                    const auto part = s.get(id, &r.error);
                    if (part.value("kind") != "part" && part.value("kind") != "book") {
                        r.error = "An audiobook is missing";
                        return;
                    }
                    books[part.value("kind") == "book" ? id : part.value("parent").toString()] = {};
                }
                for (auto b = books.begin(); b != books.end(); ++b) {
                    const auto book = s.get(b.key(), &r.error);
                    if (book.value("kind") != "book" || !r.error.isEmpty()) {
                        r.error = "The audiobook is missing";
                        return;
                    }
                    QVariantList ordered;
                    for (const auto &v : all)
                        if (v.toMap().value("kind") == "part" &&
                            v.toMap().value("parent") == b.key())
                            ordered.append(v);
                    std::sort(ordered.begin(), ordered.end(),
                              [](const QVariant &a, const QVariant &b) {
                                  return a.toMap().value("order").toInt() <
                                         b.toMap().value("order").toInt();
                              });
                    if (ordered.isEmpty() || ordered.size() > 65535) {
                        r.error = "This book has too many parts for the Zune";
                        return;
                    }
                    QCryptographicHash bookHash(QCryptographicHash::Sha256);
                    bookHash.addData(QJsonDocument(QJsonArray{book.value("title").toString(),
                                                              book.value("author").toString()})
                                         .toJson(QJsonDocument::Compact));
                    int number = 0;
                    for (const auto &v : ordered) {
                        if (cancel->load()) {
                            r.error = "Cancelled";
                            return;
                        }
                        auto part = v.toMap();
                        QFileInfo file(part.value("path").toString());
                        if (!file.isFile() || file.size() != part.value("bytes").toLongLong() ||
                            file.lastModified().toMSecsSinceEpoch() !=
                                part.value("mtime").toLongLong()) {
                            r.error = "An audiobook file changed since import. Review and "
                                      "import the book again before syncing.";
                            return;
                        }
                        AudiobookPart probe;
                        if (!Audiobooks::probePart(file.filePath(), &probe, &r.error, cancel))
                            return;
                        QFile source(file.filePath());
                        if (!source.open(QIODevice::ReadOnly)) {
                            r.error = "Cannot read audiobook audio";
                            return;
                        }
                        QCryptographicHash hash(QCryptographicHash::Sha256);
                        while (!source.atEnd()) {
                            if (cancel->load()) {
                                r.error = "Cancelled";
                                return;
                            }
                            const auto chunk = source.read(1024 * 1024);
                            if (chunk.isEmpty() && source.error() != QFile::NoError) {
                                r.error = "Could not read the complete audiobook";
                                return;
                            }
                            hash.addData(chunk);
                        }
                        file.refresh();
                        if (file.size() != part.value("bytes").toLongLong() ||
                            file.lastModified().toMSecsSinceEpoch() !=
                                part.value("mtime").toLongLong()) {
                            r.error = "Audiobook changed during preparation";
                            return;
                        }
                        const auto fingerprint = hash.result();
                        bookHash.addData(fingerprint);
                        QString art = book.value("customArt").toString();
                        if (art.isEmpty())
                            art = book.value("autoArt").toString();
                        // Transfer every sample once, including introductions, gaps
                        // between chapter metadata and closing credits. Chapter starts
                        // are the cuts.
                        const int count = probe.chapters.isEmpty() ? 1 : probe.chapters.size();
                        for (int chapter = 0; chapter < count; ++chapter) {
                            const bool ranged = !probe.chapters.isEmpty();
                            const qint64 start = chapter == 0 ? 0 : probe.chapters[chapter].startMs;
                            const qint64 end = chapter + 1 < count
                                                   ? probe.chapters[chapter + 1].startMs
                                                   : probe.durationMs;
                            if (end <= start || end - start > INT_MAX || number >= 5000) {
                                r.error = "This book's chapter lengths or part count exceed "
                                          "the device transfer limits.";
                                return;
                            }
                            QString title = ranged ? probe.chapters[chapter].title.trimmed()
                                                   : part.value("title").toString();
                            if (title.isEmpty())
                                title = QStringLiteral("Chapter %1").arg(number + 1);
                            const QString identity =
                                part.value("id").toString() +
                                (ranged ? QStringLiteral(":chapter:%1:%2").arg(start).arg(end)
                                        : QString());
                            QVariantMap row{
                                {"type", "audiobook"},
                                {"needsPreparation", false},
                                {"bookId", b.key()},
                                {"sourceBytes", part.value("bytes")},
                                {"sourceMtime", part.value("mtime")},
                                {"spokenId", part.value("id")},
                                {"identity", identity},
                                {"partNumber", ++number},
                                {"title", title},
                                {"album", book.value("title")},
                                {"artist", book.value("author")},
                                {"filepath", file.filePath()},
                                {"durationMs", end - start},
                                {"codec", probe.codec},
                                {"sourceHash", QString::fromLatin1(fingerprint.toHex())},
                                {"posterPath", QUrl(art).toLocalFile()}};
                            if (ranged) {
                                row["rangeStartMs"] = start;
                                row["rangeEndMs"] = end;
                                row["sourceDurationMs"] = probe.durationMs;
                            }
                            b.value().append(row);
                        }
                    }
                    const auto key = QString::fromLatin1(bookHash.result().toHex());
                    for (const auto &v : b.value()) {
                        auto row = v.toMap();
                        row["bookKey"] = key;
                        r.transfers.append(row);
                    }
                }
                if (r.transfers.size() > 5000)
                    r.error = "Choose fewer books for this transfer (maximum 5,000 "
                              "chapters).";
                if (!r.error.isEmpty())
                    r.transfers.clear();

                return;
            }
            if (queuedRow.value("type") != "podcast") {
                r.error = "Unknown spoken transfer type";
                return;
            }
            auto row = s.get(queuedRow.value("spokenId").toString(), &r.error);
            auto show = s.get(row.value("parent").toString(), &r.error);
            if (!r.error.isEmpty())
                return;
            if (row.value("kind") != "episode" || show.value("kind") != "show" ||
                row.value("identity") != queuedRow.value("identity") ||
                show.value("identity") != queuedRow.value("feedUrl")) {
                r.error = "The queued episode no longer matches your library.";
                return;
            }
            const auto source = queuedRow.value("sourceUrl").toString();
            if (source.isEmpty() || (row.contains("deviceSourceUrl") &&
                                     row.value("deviceSourceUrl").toString() != source)) {
                r.error = "The episode's saved device identity changed. Remove it "
                          "from the queue and add it again.";
                return;
            }
            if (s.sharingSource(row.value("parent").toString(), row.value("id").toString(), source,
                                &r.error) > 0 && r.error.isEmpty()) {
                r.error = "This feed reuses one audio URL for different episode "
                          "IDs; device identity needs review.";
                return;
            }
            if (!r.error.isEmpty())
                return;
            if (!QFileInfo(row.value("path").toString()).isFile()) {
                QDir().mkpath(s.root() + "/episodes");
                const QString path =
                    s.root() + "/episodes/" + row.value("id").toString() + ".audio";
                QSaveFile file(path);
                file.setDirectWriteFallback(false);
                if (!file.open(QIODevice::WriteOnly)) {
                    r.error = "Cannot create download file";
                    return;
                }
                const auto response = fetch(QUrl(row.value("sourceUrl").toString()),
                                            512LL * 1024 * 1024, cancel, &file);
                if (!response.error.isEmpty()) {
                    r.error = response.error;
                    return;
                }
                if (!file.commit()) {
                    r.error = "Cannot finish download";
                    return;
                }
                AudiobookPart audio;
                if (!Audiobooks::probePart(path, &audio, &r.error, cancel)) {
                    QFile::remove(path);
                    return;
                }
                row["path"] = path;
                row["bytes"] = audio.bytes;
                row["durationMs"] = audio.durationMs;
                row["codec"] = audio.codec;
                QString artError, art;
                if (!audio.embeddedArtwork.isEmpty())
                    art = prepareCover(s.root(), audio.embeddedArtwork, &artError);
                if (art.isEmpty())
                    art = remoteCover(s.root(), QUrl(row.value("artUrl").toString()), cancel);
                if (!art.isEmpty()) {
                    row["autoArt"] = art;
                    if (show.value("autoArt").toString().isEmpty()) {
                        show["autoArt"] = art;
                        if (!s.put(&show, &r.error))
                            return;
                    }
                }
            }
            if (cancel->load()) {
                r.error = "Cancelled; completed downloads were kept";
                return;
            }
            row["deviceSourceUrl"] = source;
            if (!s.put(&row, &r.error))
                return;
            QString art = row.value("customArt").toString();
            if (art.isEmpty())
                art = show.value("customArt").toString();
            if (art.isEmpty())
                art = row.value("autoArt").toString();
            if (art.isEmpty())
                art = show.value("autoArt").toString();
            auto prepared = queuedRow;
            prepared["needsPreparation"] = false;
            prepared["filepath"] = row.value("path");
            prepared["codec"] = row.value("codec");
            prepared["durationMs"] = row.value("durationMs");
            prepared["sourceBytes"] = row.value("bytes");
            prepared["posterPath"] = QUrl(art).toLocalFile();
            r.transfers.append(prepared);
        },
        -1, token);
}
