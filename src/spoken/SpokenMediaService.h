#pragma once
#include <QHash>
#include <QSet>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
class SpokenStore;
class PlayerService;

class SpokenMediaService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool foregroundBusy READ foregroundBusy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    // True while older pages of a feed's back catalogue are being read.
    Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY busyChanged)
  public:
    // Episodes per page in the show and picker lists.
    static constexpr int EpisodePageSize = 100;
    explicit SpokenMediaService(QObject *parent = nullptr);
    ~SpokenMediaService() override;
    int revision() const { return m_revision; }
    // Reading older feed pages runs in the background and does not count.
    bool busy() const { return m_pending - m_backgroundPending > 0; }
    bool loadingOlder() const { return m_backgroundPending > 0 || !m_walks.isEmpty(); }
    bool readingOlder(const QString &showId) const { return m_walks.contains(showId); }
    bool foregroundBusy() const { return m_foregroundPending > 0; }
    QString status() const { return m_status; }
    // A feed preview carries episodeCount, not the episodes themselves.
    QVariantMap preview() const { return m_preview; }
    void attachPlayer(PlayerService *player);
    void attachDeviceQueue(std::function<bool()> available) {
        m_canTransfer = std::move(available);
    }
    Q_INVOKABLE void queueAudiobooks(const QStringList &ids);
    Q_INVOKABLE void queuePodcasts(const QStringList &ids);
    Q_INVOKABLE void queuePodcastSelection(const QString &showId) { emit podcastSelectionRequested(showId); }
    // Native coordinator callback is correlated to this particular refresh.
    void refreshFeedForQueue(const QString &id, std::function<void(bool)> finished);
    QVariantList orderedPodcastEpisodes(const QString &showId, const QString &order,
                                        int offset = 0, int limit = 100000) const;
    // playedSources: audio URLs the Zune reports played, for unplayedOnly.
    Q_INVOKABLE QVariantList selectPodcastEpisodes(const QString &showId, const QString &order,
                                                   int count, bool unplayedOnly = true,
                                                   const QStringList &playedSources = {}) const;
    // Episodes of the show whose device audio URL is missing or shared.
    bool podcastSourcesAmbiguous(const QString &showId) const;
    Q_INVOKABLE void setPodcastSyncSelection(const QString &showId, const QString &order,
                                             int count, bool unplayedOnly);
    // SyncEngine calls these only after queue admission. Preparation is quiet,
    // serialized, and independently cancellable without affecting library work.
    Q_SLOT void prepareTransfer(const QString &token, const QVariantMap &queuedRow);
    Q_SLOT void cancelTransfer(const QString &token);
    Q_INVOKABLE QVariantList collections(const QString &kind) const;
    Q_INVOKABLE void setFavorite(const QString &id, bool favorite);
    Q_INVOKABLE void setPodcastKeepOnDevice(const QString &id, bool keep);
    // Local episodes using these device audio URLs (on the Zune or queued);
    // everything when the Zune's inventory is not known.
    QVariantList podcastCleanupCandidates(const QStringList &sources, bool everything = false) const;
    Q_INVOKABLE QVariantMap podcastDeviceIdentity(const QString &id) const;
    Q_SLOT void recordDevicePlayed(const QString &token, const QString &serial,
                                  const QVariantList &rows);
    Q_INVOKABLE QVariantList audiobookCollections() const;
    Q_INVOKABLE QVariantList collectionBooks(const QString &id) const;
    Q_INVOKABLE void saveAudiobookCollection(const QString &id, const QString &title,
                                             const QStringList &bookIds, int expectedRevision);
    Q_INVOKABLE void deleteAudiobookCollection(const QString &id, int expectedRevision);
    // Parts of a book; for a show, every episode (lists use episodePage).
    Q_INVOKABLE QVariantList entries(const QString &parent) const;
    // Part of a show's episode list. Options: filter (all, unplayed,
    // downloaded, starred), search, playedSources, starred (only starred
    // episodes), oldest (oldest first), and either offset + limit or page +
    // pageSize. An empty showId with starred lists the starred episodes of
    // every show. Returns rows, total, page, pages and pageSize.
    Q_INVOKABLE QVariantMap episodePage(const QString &showId, const QVariantMap &options) const;
    // The IDs of the same list's first episodes (at most 5,000), for "select all".
    Q_INVOKABLE QStringList episodeIds(const QString &showId, const QVariantMap &options, int limit) const;
    // count, downloaded, olderState, olderError and readingOlder for a show.
    Q_INVOKABLE QVariantMap showSummary(const QString &showId) const;
    // The episode "resume" plays: newest downloaded unplayed, else newest downloaded.
    Q_INVOKABLE QString resumeEpisode(const QString &showId) const;
    // Whether any episode of the show uses one of these device audio URLs.
    Q_INVOKABLE bool showOnDevice(const QString &showId, const QStringList &deviceSources) const;
    Q_INVOKABLE QVariantMap item(const QString &id) const;
    Q_INVOKABLE QString cover(const QString &id) const;
    Q_INVOKABLE void previewFeed(const QUrl &url);
    Q_INVOKABLE void subscribePreview();
    Q_INVOKABLE void refreshFeed(const QString &id);
    Q_INVOKABLE void previewBook(const QList<QUrl> &files);
    Q_INVOKABLE void previewBookFolder(const QUrl &folder, bool collection);
    Q_INVOKABLE void importBookFolder(const QVariantList &reviewedBooks);
    Q_INVOKABLE void importBook(const QString &title, const QString &author,
                                const QString &narrator, const QStringList &orderedPaths);
    Q_INVOKABLE void download(const QStringList &ids);
    Q_INVOKABLE void saveCover(const QString &id, const QUrl &source, bool reset,
                               int expectedRevision);
    Q_INVOKABLE void play(const QString &id, double positionMs = -1);
    Q_INVOKABLE void markPlayed(const QString &id, bool played);
    Q_INVOKABLE void addBookmark(const QString &id, double positionMs, const QString &note);
    Q_INVOKABLE void cancelWork();
    Q_INVOKABLE void skipChapter(int direction);
  signals:
    void podcastSelectionRequested(const QString &showId);
    void transferPrepared(const QString &token, const QVariantList &rows, const QString &error);
    void audiobookTransferReady(const QVariantList &parts);
    void podcastTransferReady(const QVariantList &episodes);
    void devicePlayedRecorded(const QString &token, const QString &serial,
                              const QVariantList &rows, const QString &error);
    void changed();
    void busyChanged();
    void previewChanged();
    void notification(const QString &message);
    void operationFinished(const QString &operation, const QString &id, bool ok,
                           const QString &error);

  private:
    struct Result {
        QVariantList rows, transfers;
        QVariantMap preview;
        QString error, message, id;
        bool hasPreview = false;
        // Feed jobs: the next older page, and how many episodes were new.
        QString older;
        int added = 0;
        QSet<QString> touched;
        QHash<QString, QPair<int, int>> stats;
        bool allStats = false;
    };
    // One walk through a feed's older pages, a page per background job.
    struct Walk {
        QString url, resume;
        bool catchUp = false, secure = false; // secure: an HTTPS feed
        int pages = 0, added = 0;
        QSet<QString> base, visited; // base: the feed's own URLs
    };
    using Job = std::function<void(SpokenStore &, Result &, const std::atomic_bool *)>;
    void run(const QString &operation, Job job, int previewGeneration = -1,
             const QString &transferToken = {},
             std::function<void(const Result &)> finished = {}, bool background = false);
    SpokenStore *reader() const;
    QVariantMap listEpisode(QVariantMap row) const;
    // Feed downloads never run on the database worker. Each lane is a thread
    // fetching one feed document at a time, in order: fetch() runs a nested
    // event loop, and a second fetch posted meanwhile would run inside it.
    struct NetLane {
        QThread thread;
        QObject *worker = nullptr;
        std::deque<std::function<void()>> tasks;
        bool running = false;
    };
    void netFetch(NetLane &lane, const QString &url, bool withCover,
                  std::shared_ptr<std::atomic_bool> cancel,
                  std::function<void(const QVariantMap &feed, const QString &error)> done);
    void nextNet(NetLane &lane);
    // Foreground work outside the job queue (a feed download) counts as busy.
    void beginForeground(const QString &operation);
    void endForeground();
    // manual: the user asked (refresh button or subscribe), so a walk that
    // keeps failing is tried again.
    void planOlder(const QString &showId, const QString &next, int added, bool manual);
    void readOlder(const QString &showId);
    void saveOlder(const QString &showId, const QString &url, const QVariantMap &feed,
                   const QString &fetchError);
    void savePosition(bool flush = false);
    void refreshFeedImpl(const QString &id, bool quiet, std::function<void(bool)> finished);
    void shutdown();
    std::function<bool()> m_canTransfer;
    std::deque<std::function<void()>> m_jobs;
    bool m_dispatching = false;
    bool m_stopping = false;
    QString m_root, m_status;
    QThread m_thread; // database worker
    QObject *m_worker = nullptr;
    NetLane m_feedLane, m_olderLane; // previews and refreshes; older pages
    std::shared_ptr<std::atomic_bool> m_cancel, m_olderCancel;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_transferCancels;
    int m_foregroundPending = 0;
    // Everything but podcast episodes, which stay in SQLite.
    QVariantList m_rows;
    QHash<QString, QVariantMap> m_byId;
    QHash<QString, QVariantList> m_children;
    QHash<QString, QPair<int, int>> m_stats; // show -> {episodes, downloaded}
    mutable std::unique_ptr<SpokenStore> m_reader;
    mutable QHash<QString, QVariantMap> m_episodeCache; // cleared on every change
    QHash<QString, Walk> m_walks;
    int m_backgroundPending = 0;
    bool m_positionPending = false;
    QVariantMap m_preview, m_previewFeed;
    int m_revision = 0, m_pending = 0, m_previewGeneration = 0;
    PlayerService *m_player = nullptr;
    QTimer m_positionTimer;
    QString m_positionId;
    qint64 m_position = 0;
};
