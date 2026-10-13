#include "PodcastQueuePlanner.h"
#include "SpokenMediaService.h"
#include <QPointer>
#include <QTimer>
#include <QScopedValueRollback>
#include <algorithm>

PodcastQueuePlanner::PodcastQueuePlanner(SpokenMediaService *service, Ports ports, QObject *parent)
    : QObject(parent), m_service(service), m_ports(std::move(ports)) {
    connect(service, &SpokenMediaService::changed, this, &PodcastQueuePlanner::schedule);
    connect(service, &SpokenMediaService::busyChanged, this, &PodcastQueuePlanner::schedule);
    connect(service, &SpokenMediaService::podcastSelectionRequested,
            this, &PodcastQueuePlanner::requestSelection);
    schedule();
}
void PodcastQueuePlanner::schedule() {
    if (m_scheduled) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] { m_scheduled = false; update(); });
}
QString PodcastQueuePlanner::attemptKey(const QString &id, int count) const {
    return id + QLatin1Char(':') + QString::number(count);
}
bool PodcastQueuePlanner::valid(const Request &r) const {
    const auto c = m_ports.context();
    if (r.epoch != m_epoch || !c.connected || c.syncing || c.queueMismatch ||
        c.serial != m_context.serial || c.generation != m_context.generation) return false;
    const auto show = m_service->item(r.showId);
    if (show.value("kind") != "show") return false;
    if (r.automatic) return c.enabled && show.value("favorite").toBool() &&
        r.starVersion == m_starVersions.value(r.showId) &&
        r.count == std::clamp(show.value("syncCount", c.count).toInt(), 1, 5000);
    return true;
}
void PodcastQueuePlanner::update() {
    if (m_updating) { schedule(); return; }
    QScopedValueRollback<bool> updating(m_updating, true);
    const auto c = m_ports.context();
    const bool sessionChanged = c.connected != m_context.connected || c.serial != m_context.serial ||
        c.generation != m_context.generation;
    const bool policyChanged = c.enabled != m_context.enabled || c.count != m_context.count;
    const bool transferStarted = c.syncing && !m_context.syncing;
    const bool queueChangedDevice = c.queueMismatch != m_context.queueMismatch;
    if (sessionChanged || policyChanged || transferStarted || queueChangedDevice) {
        if (transferStarted) {
            if (m_fetching && m_active.automatic) m_attempted.remove(attemptKey(m_active.showId, m_active.count));
            if (m_hasReady && m_ready.automatic) m_attempted.remove(attemptKey(m_ready.showId, m_ready.count));
        }
        ++m_epoch;
        m_hasReady = false;
        m_manual.clear();
        // Completing a sync does not refill a queue the user has already reviewed.
        if (sessionChanged || policyChanged || queueChangedDevice) m_attempted.clear();
        m_inventoryNotice.clear();
    }
    m_context = c;
    QSet<QString> starred;
    for (const auto &v : m_service->collections("show")) {
        const auto show = v.toMap();
        if (show.value("favorite").toBool()) starred.insert(show.value("id").toString());
    }
    for (const auto &id : m_starred - starred) {
        ++m_starVersions[id];
        for (auto it = m_attempted.begin(); it != m_attempted.end();) {
            if (it->startsWith(id + QLatin1Char(':'))) it = m_attempted.erase(it);
            else ++it;
        }
    }
    m_starred = starred;
    if (!c.connected || c.serial.isEmpty() || c.syncing || c.queueMismatch || m_fetching || m_service->busy()) return;
    if (m_hasReady) {
        if (!valid(m_ready)) m_hasReady = false;
        else if (c.inventoryReady && !c.inventoryLoading) {
            const auto ready = m_ready;
            m_hasReady = false;
            admit(ready);
        } else if (!c.inventoryLoading && !c.inventoryError.isEmpty()) {
            if (m_inventoryNotice != c.inventoryError) {
                m_inventoryNotice = c.inventoryError;
                emit m_service->notification("Podcast queue unchanged: " + c.inventoryError);
            }
            return;
        } else { m_ports.ensureInventory(); return; }
    }
    if (!m_manual.isEmpty()) {
        auto request = m_manual.takeFirst();
        const auto show = m_service->item(request.showId);
        request.count = std::clamp(show.value("syncCount", c.count).toInt(), 1, 5000);
        request.order = show.value("syncOrder") == "oldest" ? "oldest" : "newest";
        request.unplayedOnly = show.value("syncUnplayedOnly", true).toBool();
        if (valid(request)) { begin(request); return; }
        schedule();
    }
    if (!c.enabled) return;
    QStringList shows = starred.values();
    std::sort(shows.begin(), shows.end());
    for (const auto &id : shows) {
        const auto show = m_service->item(id);
        const int count = std::clamp(show.value("syncCount", c.count).toInt(), 1, 5000);
        const auto key = attemptKey(id, count);
        if (m_attempted.contains(key)) continue;
        m_attempted.insert(key); // Before refresh emits changed; exactly one pass per policy/session.
        begin({id, "newest", count, true, true, m_epoch, m_starVersions.value(id)});
        return;
    }
}
void PodcastQueuePlanner::requestSelection(const QString &id) {
    update();
    const auto c = m_ports.context();
    if (!c.connected || c.syncing || c.queueMismatch || c.serial.isEmpty()) {
        emit m_service->notification("Connect the queue's Zune and finish the current sync before adding episodes.");
        return;
    }
    const auto show = m_service->item(id);
    if (show.value("kind") != "show") return;
    for (const auto &r : m_manual) if (r.showId == id) return;
    const int count = std::clamp(show.value("syncCount", c.count).toInt(), 1, 5000);
    m_manual.append({id, show.value("syncOrder") == "oldest" ? "oldest" : "newest", count,
                     show.value("syncUnplayedOnly", true).toBool(), false, m_epoch});
    schedule();
}
void PodcastQueuePlanner::dismissPending() {
    ++m_epoch;
    m_manual.clear();
    m_hasReady = false;
    const auto c = m_ports.context();
    for (const auto &v : m_service->collections("show")) {
        const auto show = v.toMap();
        if (show.value("favorite").toBool())
            m_attempted.insert(attemptKey(show.value("id").toString(),
                std::clamp(show.value("syncCount", c.count).toInt(), 1, 5000)));
    }
}
void PodcastQueuePlanner::begin(const Request &request) {
    m_fetching = true;
    m_active = request;
    m_ports.ensureInventory();
    QPointer<PodcastQueuePlanner> self(this);
    m_service->refreshFeedForQueue(request.showId, [self, request](bool ok) {
        if (!self) return;
        self->m_fetching = false;
        if (ok && self->valid(request)) { self->m_ready = request; self->m_hasReady = true; }
        self->schedule();
    });
}
void PodcastQueuePlanner::admit(const Request &request) {
    if (!valid(request)) return;
    QStringList additions;
    int selected = 0;
    QSet<QString> sources;
    // Source ambiguity must not create failed duplicate queue rows, including
    // when the conflicting entry falls outside the selected range.
    for (const auto &v : m_service->entries(request.showId)) {
        const auto identity = m_service->podcastDeviceIdentity(v.toMap().value("id").toString());
        const auto source = identity.value("sourceUrl").toString();
        if (source.isEmpty() || sources.contains(source)) {
            emit m_service->notification("Podcast queue unchanged: this feed has ambiguous audio identities.");
            return;
        }
        sources.insert(source);
    }
    // Select from all episodes, including feed-only rows. Existing episodes
    // count toward N; only their upload is omitted. Never infer absent/played.
    for (const auto &v : m_service->orderedPodcastEpisodes(request.showId, request.order)) {
        const auto row = v.toMap();
        const auto id = row.value("id").toString();
        const auto identity = m_service->podcastDeviceIdentity(id);
        const auto status = m_ports.status(identity.value("feedUrl").toString(), identity.value("sourceUrl").toString());
        if (!status.value("known").toBool()) {
            emit m_service->notification("Podcast queue unchanged: refresh the Zune's podcast inventory first.");
            return;
        }
        const bool played = row.value("played").toBool() ||
            (status.value("playedKnown").toBool() && status.value("played").toBool()) ||
            row.value("devicePlayedSerials").toMap().value(m_context.serial).toBool();
        if (request.unplayedOnly && played) continue;
        ++selected;
        if (!status.value("onDevice").toBool()) additions.append(id);
        if (selected >= request.count) break;
    }
    if (valid(request) && !additions.isEmpty()) m_service->queuePodcasts(additions);
}
