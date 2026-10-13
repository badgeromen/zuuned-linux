#pragma once
#include <QHash>
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
  public:
    explicit SpokenMediaService(QObject *parent = nullptr);
    ~SpokenMediaService() override;
    int revision() const { return m_revision; }
    bool busy() const { return m_pending > 0; }
    bool foregroundBusy() const { return m_foregroundPending > 0; }
    QString status() const { return m_status; }
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
    QVariantList orderedPodcastEpisodes(const QString &showId, const QString &order) const;
    Q_INVOKABLE QVariantList selectPodcastEpisodes(const QString &showId, const QString &order,
                                                   int count, bool unplayedOnly = true) const;
    Q_INVOKABLE void setPodcastSyncSelection(const QString &showId, const QString &order,
                                             int count, bool unplayedOnly);
    // SyncEngine calls these only after queue admission. Preparation is quiet,
    // serialized, and independently cancellable without affecting library work.
    Q_SLOT void prepareTransfer(const QString &token, const QVariantMap &queuedRow);
    Q_SLOT void cancelTransfer(const QString &token);
    Q_INVOKABLE QVariantList collections(const QString &kind) const;
    Q_INVOKABLE void setFavorite(const QString &id, bool favorite);
    Q_INVOKABLE void setPodcastKeepOnDevice(const QString &id, bool keep);
    Q_INVOKABLE QVariantList podcastCleanupCandidates() const;
    Q_INVOKABLE QVariantMap podcastDeviceIdentity(const QString &id) const;
    Q_SLOT void recordDevicePlayed(const QString &token, const QString &serial,
                                  const QVariantList &rows);
    Q_INVOKABLE QVariantList audiobookCollections() const;
    Q_INVOKABLE QVariantList collectionBooks(const QString &id) const;
    Q_INVOKABLE void saveAudiobookCollection(const QString &id, const QString &title,
                                             const QStringList &bookIds, int expectedRevision);
    Q_INVOKABLE void deleteAudiobookCollection(const QString &id, int expectedRevision);
    Q_INVOKABLE QVariantList entries(const QString &parent) const;
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
    };
    using Job = std::function<void(SpokenStore &, Result &, const std::atomic_bool *)>;
    void run(const QString &operation, Job job, int previewGeneration = -1,
             const QString &transferToken = {},
             std::function<void(const Result &)> finished = {});
    void savePosition(bool flush = false);
    void refreshFeedImpl(const QString &id, bool quiet, std::function<void(bool)> finished);
    void shutdown();
    std::function<bool()> m_canTransfer;
    std::deque<std::function<void()>> m_jobs;
    bool m_dispatching = false;
    bool m_stopping = false;
    QString m_root, m_status;
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_transferCancels;
    int m_foregroundPending = 0;
    QVariantList m_rows;
    QHash<QString, QVariantMap> m_byId;
    QHash<QString, QVariantList> m_children;
    QVariantMap m_preview;
    int m_revision = 0, m_pending = 0, m_previewGeneration = 0;
    PlayerService *m_player = nullptr;
    QTimer m_positionTimer;
    QString m_positionId;
    qint64 m_position = 0;
};
