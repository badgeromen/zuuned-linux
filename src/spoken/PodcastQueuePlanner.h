#pragma once
#include <QObject>
#include <QSet>
#include <QHash>
#include <QVariantMap>
#include <functional>

class SpokenMediaService;

// Coordinates local RSS work with a snapshot of the serialized device owner.
// No USB or downloads happen here. Queue admission and Sync still own transfers.
class PodcastQueuePlanner : public QObject {
    Q_OBJECT
public:
    struct Context {
        QString serial;
        quint64 generation = 0;
        bool connected = false, syncing = false, queueMismatch = false;
        bool inventoryReady = false, inventoryLoading = false;
        QString inventoryError;
        bool enabled = false;
        int count = 10;
    };
    struct Ports {
        std::function<Context()> context;
        std::function<QVariantMap(const QString &, const QString &)> status;
        std::function<void()> ensureInventory;
    };
    PodcastQueuePlanner(SpokenMediaService *service, Ports ports, QObject *parent = nullptr);
public slots:
    void update();
    void requestSelection(const QString &showId);
    void dismissPending();
private:
    struct Request {
        QString showId, order;
        int count = 10;
        bool unplayedOnly = true, automatic = false;
        quint64 epoch = 0, starVersion = 0;
    };
    bool valid(const Request &request) const;
    void begin(const Request &request);
    void admit(const Request &request);
    void schedule();
    QString attemptKey(const QString &showId, int count) const;
    SpokenMediaService *m_service;
    Ports m_ports;
    Context m_context;
    quint64 m_epoch = 0;
    bool m_fetching = false, m_scheduled = false, m_updating = false;
    bool m_hasReady = false;
    Request m_ready, m_active;
    QHash<QString, quint64> m_starVersions;
    QList<Request> m_manual;
    QSet<QString> m_attempted, m_starred;
    QString m_inventoryNotice;
};
