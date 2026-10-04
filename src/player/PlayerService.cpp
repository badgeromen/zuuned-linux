#include "PlayerService.h"
#include "MpvController.h"

#include <QRandomGenerator>
#include <QVariantMap>

PlayerService::PlayerService(QObject *parent) : QObject(parent) {
    m_mpv = new MpvController(this);
    m_mpv->create(/*audioOnly=*/true);

    // MpvController signals arrive from its event thread — these
    // connections are automatically queued.
    connect(m_mpv, &MpvController::positionChanged, this, [this](double sec) {
        if (m_loadingQueue) return;
        m_positionSec = sec;
        emit positionChanged();
    });
    connect(m_mpv, &MpvController::durationChanged, this, [this](double sec) {
        if (m_loadingQueue) return;
        m_durationSec = sec;
        emit durationChanged();
    });
    connect(m_mpv, &MpvController::pauseChanged, this, [this](bool paused) {
        if (m_loadingQueue) return;
        if (paused) {
            if (m_state == Playing)
                setState(Paused);
        } else if (m_state != Stopped) {
            setState(Playing);
        }
    });
    connect(m_mpv, &MpvController::fileLoadedAt, this, [this](qint64 entryId) {
        if (!m_loadingQueue || entryId < 0) return;
        // A completion from a replaced queue must never start or unpause
        // the new one. Native playlist entry IDs survive reordering.
        if (entryId != m_selectedEntryId) {
            if (entryId == m_mpv->playingPlaylistEntryId())
                m_mpv->setPlaylistIndex(m_queueIndex);
            return;
        }
        m_loadingQueue = false;
        m_positionSec = m_mpv->position();
        m_durationSec = m_mpv->duration();
        emit positionChanged();
        emit durationChanged();
        if (m_state == Playing) m_mpv->play();
    });
    connect(m_mpv, &MpvController::fileFailedAt, this, [this](qint64 entryId) {
        if (m_loadingQueue && entryId == m_selectedEntryId)
            stop();
    });
    connect(m_mpv, &MpvController::fileEnded, this, [this](bool isEOF) {
        if (isEOF)
            handleTrackEnded();
    });
    // keep-open=yes: the LAST playlist entry never fires end-file (mpv
    // pauses on the final frame instead) — this is where repeat-all
    // wraps and repeat-one repeats at the end of the queue.
    connect(m_mpv, &MpvController::endReached, this, [this] {
        handleEndReached();
    });
    connect(m_mpv, &MpvController::metadataChanged, this,
            [this](const QString &title, const QString &artist, const QString &album) {
        // Only fall back to mpv metadata when there's no queue metadata
        if (m_queueIndex >= 0 && m_queueIndex < m_queue.size())
            return;
        if (!title.isEmpty()) m_title = title;
        if (!artist.isEmpty()) m_artist = artist;
        if (!album.isEmpty()) m_album = album;
        emit metadataChanged();
    });

    m_saveTimer.setInterval(5000);
    connect(&m_saveTimer, &QTimer::timeout, this,
            &PlayerService::savePlaybackPosition);

    m_volume = m_mpv->volume();
}

PlayerService::~PlayerService() {
    savePlaybackPosition();
    m_saveTimer.stop();
    m_mpv->destroy();
}

void PlayerService::setState(State s) {
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

qint64 PlayerService::currentLibraryId() const {
    if (m_queueIndex < 0 || m_queueIndex >= m_queue.size())
        return -1;
    return qint64(m_queue[m_queueIndex].toMap()
                      .value(QStringLiteral("libraryId"), -1).toDouble());
}

// ── Playback controls ──

void PlayerService::playFile(const QString &path) {
    m_playNextCount = 0;
    m_loadingQueue = false;
    m_mpv->setActive(true);
    m_mpv->loadFile(path);
    m_mpv->play();
    setState(Playing);
    startSaveTimer();
}

void PlayerService::playPause() {
    switch (m_state) {
    case Playing:
        m_mpv->pause();
        setState(Paused);
        savePlaybackPosition();
        stopSaveTimer();
        break;
    case Paused:
        m_mpv->setActive(true);
        if (!m_loadingQueue) m_mpv->play();
        setState(Playing);
        startSaveTimer();
        break;
    case Stopped:
        if (!m_queue.isEmpty()) {
            const int idx = m_queueIndex >= 0 ? m_queueIndex : 0;
            beginJumpGuard();
            m_queueIndex = idx;
            updateMetadataFromQueue(idx);
            m_mpv->setActive(true);
            m_mpv->setPlaylistIndex(idx);
            m_mpv->play();
            setState(Playing);
            startSaveTimer();
            emit queueChanged();
        }
        break;
    }
}

void PlayerService::stop() {
    m_playNextCount = 0;
    m_loadingQueue = false;
    savePlaybackPosition();
    stopSaveTimer();
    m_mpv->stop();
    m_mpv->setActive(false);
    setState(Stopped);
    m_positionSec = 0;
    m_durationSec = 0;
    m_title.clear();
    m_artist.clear();
    m_album.clear();
    emit positionChanged();
    emit durationChanged();
    emit metadataChanged();
}

void PlayerService::seekMs(double ms) {
    m_mpv->seekTo(ms / 1000.0, /*exact=*/true);
}

// ── Volume / modes ──

void PlayerService::setVolume(double v) {
    const double clamped = qBound(0.0, v, 100.0);
    m_volume = clamped;
    m_mpv->setVolume(clamped);
    emit volumeChanged();
}

void PlayerService::toggleMute() {
    m_muted = !m_muted;
    m_mpv->setMuted(m_muted);
    emit volumeChanged();
}

void PlayerService::toggleShuffle() {
    m_shuffled = !m_shuffled;
    emit modeChanged();
}

void PlayerService::cycleRepeat() {
    m_repeatMode = (m_repeatMode + 1) % 3;
    emit modeChanged();
}

// ── Queue management ──

void PlayerService::setQueue(const QVariantList &items, int startIndex) {
    if (items.isEmpty()) {
        clearQueue();
        return;
    }
    m_playNextCount = 0;
    m_loadingQueue = true;
    beginJumpGuard();
    m_mpv->setActive(true);

    m_queue = items;
    m_queueIndex = qBound(0, startIndex, int(items.size()) - 1);
    m_positionSec = 0;
    m_durationSec = 0;
    emit positionChanged();
    emit durationChanged();

    // mpv can settle its first load at entry zero even after an immediate
    // index change. Keep it paused until the selected entry's load completes.
    m_mpv->pause();
    m_mpv->clearPlaylist();
    for (int i = 0; i < items.size(); i++) {
        const QString path =
            items[i].toMap().value(QStringLiteral("filepath")).toString();
        if (i == 0) m_mpv->loadFile(path);
        else m_mpv->appendToPlaylist(path);
    }

    m_selectedEntryId = m_mpv->playlistEntryId(m_queueIndex);
    m_mpv->setPlaylistIndex(m_queueIndex);

    if (m_queueIndex >= 0 && m_queueIndex < m_queue.size()) {
        updateMetadataFromQueue(m_queueIndex);
        setState(Playing);
        startSaveTimer();
    }
    emit queueChanged();
}

void PlayerService::appendToQueue(const QVariantList &items) {
    if (items.isEmpty())
        return;
    if (m_queue.isEmpty()) {
        setQueue(items, 0);
        return;
    }
    m_queue.append(items);
    for (const QVariant &item : items)
        m_mpv->appendToPlaylist(
            item.toMap().value(QStringLiteral("filepath")).toString());
    emit queueChanged();
}

void PlayerService::playNext(const QVariantList &items) {
    if (items.isEmpty()) return;
    if (m_queue.isEmpty()) {
        setQueue(items, 0);
        return;
    }
    const int insertion = qBound(0, m_queueIndex + 1, int(m_queue.size()));
    for (int i = 0; i < items.size(); ++i) {
        const int tail = int(m_queue.size());
        m_mpv->appendToPlaylist(items[i].toMap().value(QStringLiteral("filepath")).toString());
        // mpv moves BEFORE the target entry. Moving an appended item
        // backwards keeps the current file, position and pause state intact.
        if (insertion + i < tail)
            m_mpv->movePlaylistEntry(tail, insertion + i);
        m_queue.insert(insertion + i, items[i]);
    }
    m_playNextCount += int(items.size());
    emit queueChanged();
}

void PlayerService::clearQueue() {
    stop();
    m_mpv->clearPlaylist();
    m_queue.clear();
    m_queueIndex = -1;
    emit queueChanged();
}

void PlayerService::next() {
    if (m_queue.isEmpty())
        return;

    if (m_playNextCount > 0 && m_queueIndex + 1 < m_queue.size()) {
        --m_playNextCount;
        startQueueIndex(m_queueIndex + 1);
        return;
    }
    if (m_shuffled) {
        if (m_queue.size() < 2)
            return;
        int pick = m_queueIndex;
        while (pick == m_queueIndex)
            pick = int(QRandomGenerator::global()->bounded(m_queue.size()));
        jumpTo(pick);
        return;
    }

    const int nextIndex = m_queueIndex + 1;
    if (nextIndex >= m_queue.size()) {
        if (m_repeatMode == 1) { // all
            const int hold = 0;
            m_queueIndex = -2;   // force jumpTo past the same-index guard
            jumpTo(hold);
            return;
        }
        m_queueIndex = m_queue.size() - 1;
        stop();
        emit queueChanged();
        return;
    }

    startQueueIndex(nextIndex);
}

void PlayerService::previous() {
    if (m_queue.isEmpty())
        return;

    // Past 3 seconds → restart current track (mac behavior)
    if (m_positionSec > 3.0) {
        seekMs(0);
        return;
    }

    m_playNextCount = 0;
    if (m_queueIndex <= 0) {
        seekMs(0);
        return;
    }

    startQueueIndex(m_queueIndex - 1);
}

void PlayerService::jumpTo(int index) {
    if (m_queue.isEmpty() || index < 0 || index >= m_queue.size())
        return;
    if (index == m_queueIndex)
        return;
    m_playNextCount = 0;
    startQueueIndex(index);
}

void PlayerService::startQueueIndex(int index) {
    // Guard: mpv fires end-file for the OLD track when playlist-pos
    // changes — without this, handleTrackEnded double-advances.
    beginJumpGuard();
    m_queueIndex = index;
    if (m_loadingQueue)
        m_selectedEntryId = m_mpv->playlistEntryId(index);
    updateMetadataFromQueue(index);
    m_mpv->setActive(true);
    m_mpv->setPlaylistIndex(index);
    if (!m_loadingQueue) m_mpv->play();
    setState(Playing);
    startSaveTimer();
    emit queueChanged();
}

// mpv has ALREADY advanced its internal playlist on natural EOF — sync
// our queueIndex + metadata only; playlist-next here would double-skip.
void PlayerService::handleTrackEnded() {
    if (m_isJumping || m_loadingQueue)
        return;
    if (m_queue.isEmpty())
        return;

    if (m_repeatMode == 2) { // one
        // mpv has ALREADY advanced to the next entry — a bare seek
        // would "repeat" the WRONG track. Pull the playlist back.
        beginJumpGuard();
        m_mpv->setPlaylistIndex(m_queueIndex);
        m_mpv->play();
        setState(Playing);
        return;
    }

    if (m_playNextCount > 0 && m_queueIndex + 1 < m_queue.size()) {
        --m_playNextCount;
        // mpv has already advanced to this requested entry. Preserve the
        // remaining priority entries instead of drawing a shuffled index.
        ++m_queueIndex;
        updateMetadataFromQueue(m_queueIndex);
        setState(Playing);
        startSaveTimer();
        emit queueChanged();
        return;
    }
    if (m_shuffled) {
        if (m_queue.size() >= 2) {
            int pick = m_queueIndex;
            while (pick == m_queueIndex)
                pick = int(QRandomGenerator::global()->bounded(m_queue.size()));
            jumpTo(pick);
        }
        return;
    }

    m_queueIndex += 1;
    if (m_queueIndex >= m_queue.size()) {
        if (m_repeatMode == 1) { // all
            m_queueIndex = -2;   // force jumpTo past the same-index guard
            jumpTo(0);
            return;
        }
        m_queueIndex = m_queue.size() - 1;
        stop();
        emit queueChanged();
        return;
    }

    updateMetadataFromQueue(m_queueIndex);
    setState(Playing);
    startSaveTimer();
    emit queueChanged();
}

// End of the QUEUE under keep-open: mpv never fires end-file for the
// last playlist entry — it pauses on the final frame and raises
// eof-reached. mpv has NOT advanced here; the current entry is still
// m_queueIndex.
void PlayerService::handleEndReached() {
    if (m_isJumping || m_loadingQueue)
        return;
    if (m_queue.isEmpty())
        return;

    if (m_repeatMode == 2) { // one — replay the same (still-loaded) track
        seekMs(0);
        m_mpv->play();
        setState(Playing);
        return;
    }

    if (m_playNextCount > 0 && m_queueIndex + 1 < m_queue.size()) {
        --m_playNextCount;
        startQueueIndex(m_queueIndex + 1);
        return;
    }
    if (m_shuffled) { // shuffle never stops (same rule as mid-queue)
        if (m_queue.size() >= 2) {
            int pick = m_queueIndex;
            while (pick == m_queueIndex)
                pick = int(QRandomGenerator::global()->bounded(m_queue.size()));
            jumpTo(pick);
        } else {
            seekMs(0);
            m_mpv->play();
            setState(Playing);
        }
        return;
    }

    if (m_repeatMode == 1) { // all — wrap to the top
        m_queueIndex = -2;   // force jumpTo past the same-index guard
        jumpTo(0);
        return;
    }

    // repeat off: honest stop at the end of the queue
    m_queueIndex = m_queue.size() - 1;
    stop();
    emit queueChanged();
}

// ── Helpers ──

void PlayerService::updateMetadataFromQueue(int index) {
    if (index < 0 || index >= m_queue.size())
        return;
    const QVariantMap item = m_queue[index].toMap();
    m_title = item.value(QStringLiteral("title")).toString();
    m_artist = item.value(QStringLiteral("artist")).toString();
    m_album = item.value(QStringLiteral("album")).toString();
    emit metadataChanged();
}

void PlayerService::beginJumpGuard() {
    m_isJumping = true;
    QTimer::singleShot(300, this, [this] { m_isJumping = false; });
}

void PlayerService::startSaveTimer() {
    m_saveTimer.start();
}

void PlayerService::stopSaveTimer() {
    m_saveTimer.stop();
}

void PlayerService::savePlaybackPosition() {
    const qint64 id = currentLibraryId();
    if (id < 0 || !m_saveHook)
        return;
    // Don't store bogus zero positions before mpv reports real time
    if (m_positionSec <= 0)
        return;
    m_saveHook(id, int(m_positionSec * 1000.0));
}
