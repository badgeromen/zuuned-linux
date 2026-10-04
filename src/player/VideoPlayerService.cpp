#include "VideoPlayerService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "MpvController.h"

VideoPlayerService::VideoPlayerService(QObject *parent) : QObject(parent) {
    m_mpv = new MpvController(this);
    m_mpv->create(/*audioOnly=*/false);

    connect(m_mpv, &MpvController::positionChanged, this, [this](double s) {
        m_position = s;
        emit positionChanged();
        emit positionMsChanged();
    });
    connect(m_mpv, &MpvController::durationChanged, this, [this](double s) {
        m_duration = s;
        emit durationChanged();
        emit durationMsChanged();
    });
    connect(m_mpv, &MpvController::pauseChanged, this, [this](bool paused) {
        if (m_state == Stopped)
            return;
        m_state = paused ? Paused : Playing;
        emit stateChanged();
        emit playingChanged();
    });
    connect(m_mpv, &MpvController::fileEnded, this,
            &VideoPlayerService::handleFileEnded);

    // 5s position persistence (mac saveInterval)
    m_saveTimer.setInterval(5000);
    connect(&m_saveTimer, &QTimer::timeout, this,
            &VideoPlayerService::savePlaybackPosition);

    m_jumpGuard.setSingleShot(true);
    m_jumpGuard.setInterval(300);
    connect(&m_jumpGuard, &QTimer::timeout, this,
            [this] { m_isJumping = false; });
}

VideoPlayerService::~VideoPlayerService() {
    savePlaybackPosition();
    m_mpv->destroy();
}

double VideoPlayerService::volume() const { return m_mpv->volume(); }
void VideoPlayerService::setVolume(double v) {
    m_mpv->setVolume(qBound(0.0, v, 100.0));
    emit volumeChanged();
}
bool VideoPlayerService::muted() const { return m_mpv->isMuted(); }
int VideoPlayerService::chapterCount() const { return m_mpv->chapterCount(); }

QString VideoPlayerService::currentTitle() const {
    if (m_queueIndex >= 0 && m_queueIndex < m_queue.size())
        return m_queue[m_queueIndex].toMap()
            .value(QStringLiteral("title")).toString();
    return QString();
}

QString VideoPlayerService::currentPoster() const {
    if (m_queueIndex >= 0 && m_queueIndex < m_queue.size())
        return m_queue[m_queueIndex].toMap()
            .value(QStringLiteral("poster")).toString();
    return QString();
}

void VideoPlayerService::play(const QString &filepath, const QString &title,
                              double libraryId, int resumeMs) {
    QVariantMap item;
    item[QStringLiteral("filepath")] = filepath;
    item[QStringLiteral("title")] = title;
    item[QStringLiteral("libraryId")] = libraryId;
    setQueue({item}, 0, resumeMs);
}

void VideoPlayerService::setQueue(const QVariantList &items, int startIndex,
                                  int resumeMs) {
    if (items.isEmpty())
        return;
    savePlaybackPosition();  // whatever was playing before

    m_isJumping = true;
    m_jumpGuard.start();
    m_queue = items;
    m_queueIndex = qBound(0, startIndex, int(items.size()) - 1);

    m_mpv->clearPlaylist();
    for (int i = 0; i < items.size(); i++) {
        const QString path =
            items[i].toMap().value(QStringLiteral("filepath")).toString();
        if (i == 0)
            m_mpv->loadFile(path);
        else
            m_mpv->appendToPlaylist(path);
    }
    if (m_queueIndex > 0)
        m_mpv->setPlaylistIndex(m_queueIndex);

    m_currentLibraryId = qint64(m_queue[m_queueIndex].toMap()
                                    .value(QStringLiteral("libraryId"), -1)
                                    .toDouble());
    // Resume rules live in QML (the >60s-in / not-last-30s gate); the
    // service just executes the seek once the file loads.
    if (resumeMs > 1000) {
        const double resumeSec = resumeMs / 1000.0;
        auto *conn = new QMetaObject::Connection;
        *conn = connect(m_mpv, &MpvController::fileLoaded, this,
                        [this, resumeSec, conn] {
                            m_mpv->seekTo(resumeSec, true);
                            disconnect(*conn);
                            delete conn;
                        });
    }

    m_mpv->setActive(true);
    m_mpv->play();
    m_state = Playing;
    m_saveTimer.start();
    emit stateChanged();
    emit playingChanged();
    emit queueChanged();
}

void VideoPlayerService::togglePause() {
    if (m_state == Playing) {
        m_mpv->pause();
        savePlaybackPosition();
    } else if (m_state == Paused) {
        m_mpv->play();
    }
}

void VideoPlayerService::stop() {
    savePlaybackPosition();
    m_saveTimer.stop();
    m_mpv->stop();
    m_mpv->setActive(false);
    m_state = Stopped;
    m_position = 0;
    m_duration = 0;
    m_currentLibraryId = -1;
    m_queue.clear();
    m_queueIndex = -1;
    emit stateChanged();
    emit playingChanged();
    emit positionChanged();
    emit positionMsChanged();
    emit queueChanged();
}

void VideoPlayerService::seek(double seconds) { m_mpv->seekTo(seconds, true); }
void VideoPlayerService::scrubMs(double ms) { m_mpv->seekTo(ms / 1000.0, false); }

void VideoPlayerService::beginScrub() {
    m_resumeAfterScrub = (m_state == Playing);
    if (m_resumeAfterScrub)
        m_mpv->pause();
}

void VideoPlayerService::endScrubMs(double ms) {
    m_mpv->seekTo(ms / 1000.0, true);
    if (m_resumeAfterScrub) {
        m_mpv->play();
        m_resumeAfterScrub = false;
    }
}
void VideoPlayerService::seekRelative(double delta) { m_mpv->seekRelative(delta); }

void VideoPlayerService::skipChapter(int delta) {
    if (m_mpv->chapterCount() > 0)
        m_mpv->skipChapter(delta);
    else
        m_mpv->seekRelative(delta * 60.0);   // mac chapterless fallback
}

void VideoPlayerService::next() {
    if (m_queue.isEmpty())
        return;
    savePlaybackPosition();
    if (m_queueIndex + 1 >= m_queue.size()) {
        stop();
        emit queueFinished();
        return;
    }
    m_isJumping = true;
    m_jumpGuard.start();
    m_queueIndex++;
    m_currentLibraryId = qint64(m_queue[m_queueIndex].toMap()
                                    .value(QStringLiteral("libraryId"), -1)
                                    .toDouble());
    m_mpv->playlistNext();
    m_state = Playing;
    emit stateChanged();
    emit playingChanged();
    emit queueChanged();
}

void VideoPlayerService::prev() {
    if (m_queue.isEmpty())
        return;
    // mac rule: >3s in restarts the current file
    if (m_position > 3.0 || m_queueIndex <= 0) {
        seek(0);
        return;
    }
    savePlaybackPosition();
    m_isJumping = true;
    m_jumpGuard.start();
    m_queueIndex--;
    m_currentLibraryId = qint64(m_queue[m_queueIndex].toMap()
                                    .value(QStringLiteral("libraryId"), -1)
                                    .toDouble());
    m_mpv->playlistPrev();
    m_state = Playing;
    emit stateChanged();
    emit playingChanged();
    emit queueChanged();
}

void VideoPlayerService::jumpTo(int index) {
    if (index < 0 || index >= m_queue.size() || index == m_queueIndex)
        return;
    savePlaybackPosition();
    m_isJumping = true;
    m_jumpGuard.start();
    m_queueIndex = index;
    m_currentLibraryId = qint64(m_queue[m_queueIndex].toMap()
                                    .value(QStringLiteral("libraryId"), -1)
                                    .toDouble());
    m_mpv->setPlaylistIndex(index);
    m_mpv->play();
    m_state = Playing;
    m_saveTimer.start();
    emit stateChanged();
    emit playingChanged();
    emit queueChanged();
}

void VideoPlayerService::toggleMute() {
    m_mpv->setMuted(!m_mpv->isMuted());
    emit volumeChanged();
}

QVariantList VideoPlayerService::tracksOfKind(const QString &kind) const {
    QVariantList out;
    const QJsonArray arr =
        QJsonDocument::fromJson(m_mpv->trackListJson().toUtf8()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != kind)
            continue;
        QVariantMap t;
        t[QStringLiteral("id")] = o.value(QStringLiteral("id")).toInt();
        t[QStringLiteral("kind")] = kind;
        t[QStringLiteral("title")] = o.value(QStringLiteral("title")).toString();
        t[QStringLiteral("lang")] = o.value(QStringLiteral("lang")).toString();
        t[QStringLiteral("selected")] =
            o.value(QStringLiteral("selected")).toBool();
        out.append(t);
    }
    return out;
}

QVariantList VideoPlayerService::audioTracks() const {
    return tracksOfKind(QStringLiteral("audio"));
}
QVariantList VideoPlayerService::subtitleTracks() const {
    return tracksOfKind(QStringLiteral("sub"));
}
void VideoPlayerService::setAudioTrack(int id) { m_mpv->setAudioTrack(id); }
void VideoPlayerService::setSubtitleTrack(int id) { m_mpv->setSubtitleTrack(id); }

void VideoPlayerService::handleFileEnded(bool isEOF) {
    fprintf(stderr, "[video] END_FILE: isEOF=%d isJumping=%d queue=%d/%d\n",
            isEOF, m_isJumping, m_queueIndex, int(m_queue.size()));
    if (!isEOF || m_isJumping)
        return;

    // Save the FINISHED file's final position while m_currentLibraryId
    // still points at it — position ≈ duration marks it watched via
    // the library's 90% rule (mac comment ported verbatim in spirit).
    savePlaybackPosition();

    if (m_queue.isEmpty()) {
        emit queueFinished();
        return;
    }
    if (m_queueIndex + 1 >= m_queue.size()) {
        stop();
        emit queueFinished();
        return;
    }

    // mpv auto-advanced its playlist — only sync index + metadata.
    m_queueIndex++;
    m_currentLibraryId = qint64(m_queue[m_queueIndex].toMap()
                                    .value(QStringLiteral("libraryId"), -1)
                                    .toDouble());
    m_state = Playing;
    m_saveTimer.start();
    fprintf(stderr, "[video] auto-advance -> %d/%d\n", m_queueIndex + 1,
            int(m_queue.size()));
    emit stateChanged();
    emit playingChanged();
    emit queueChanged();
}

void VideoPlayerService::savePlaybackPosition() {
    if (m_currentLibraryId < 0 || m_position <= 0 || !m_saveHook)
        return;
    m_saveHook(m_currentLibraryId, int(m_position * 1000),
               int(m_duration * 1000));
}

void VideoPlayerService::startSaveTimer() { m_saveTimer.start(); }
