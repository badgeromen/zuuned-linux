#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class MpvController;

// Music player — port of PlayerService.swift over the libmpv backend.
//
// The load-bearing mac behaviors, kept exactly:
// - the queue is MIRRORED into mpv's internal playlist (loadfile +
//   loadfile append); mpv auto-advances on EOF and handleTrackEnded only
//   SYNCS queueIndex/metadata — calling playlist-next there double-skips
// - `m_isJumping` guards handleTrackEnded during manual jumps (mpv fires
//   end-file for the old track when playlist-pos changes); cleared 0.3s
//   after the jump
// - explicit Play Next selections take priority over shuffle on next/EOF;
//   otherwise shuffle picks a random non-current index (no shuffled
//   order list); repeat-one seeks 0 and resumes; previous restarts the
//   track when position > 3s
// - a 5s timer persists playback position through a settable hook
//   (AppState wires it to the library on the mac; the session owner
//   wires LibraryDb here)
class PlayerService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool stopped READ stopped NOTIFY stateChanged)
    Q_PROPERTY(double positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(double durationMs READ durationMs NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffled READ shuffled NOTIFY modeChanged)
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY modeChanged)   // 0 off · 1 all · 2 one
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY queueChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY metadataChanged)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY metadataChanged)
    Q_PROPERTY(QString currentAlbum READ currentAlbum NOTIFY metadataChanged)

public:
    explicit PlayerService(QObject *parent = nullptr);
    ~PlayerService() override;

    bool playing() const { return m_state == Playing; }
    bool stopped() const { return m_state == Stopped; }
    double positionMs() const { return m_positionSec * 1000.0; }
    double durationMs() const { return m_durationSec * 1000.0; }
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool shuffled() const { return m_shuffled; }
    int repeatMode() const { return m_repeatMode; }
    QVariantList queue() const { return m_queue; }
    int queueIndex() const { return m_queueIndex; }
    QString currentTitle() const { return m_title; }
    QString currentArtist() const { return m_artist; }
    QString currentAlbum() const { return m_album; }

    // Position-persistence hook: (libraryId, positionMs). Set by the
    // integration layer; items with libraryId < 0 are never saved.
    void setSavePositionHook(std::function<void(qint64, int)> hook) {
        m_saveHook = std::move(hook);
    }

    // Queue items: {filepath, title, artist, album, libraryId}
    Q_INVOKABLE void setQueue(const QVariantList &items, int startIndex);
    Q_INVOKABLE void appendToQueue(const QVariantList &items);
    // Insert after the current song without restarting or unpausing it.
    Q_INVOKABLE void playNext(const QVariantList &items);
    Q_INVOKABLE void playFile(const QString &path);
    Q_INVOKABLE void jumpTo(int index);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekMs(double ms);
    Q_INVOKABLE void setVolume(double v);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void clearQueue();

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void modeChanged();
    void queueChanged();
    void metadataChanged();

private:
    enum State { Stopped, Playing, Paused };

    void handleTrackEnded();
    void handleEndReached();
    void updateMetadataFromQueue(int index);
    void startQueueIndex(int index);
    void beginJumpGuard();
    void startSaveTimer();
    void stopSaveTimer();
    void savePlaybackPosition();
    void setState(State s);
    qint64 currentLibraryId() const;

    MpvController *m_mpv = nullptr;
    State m_state = Stopped;
    double m_positionSec = 0;
    double m_durationSec = 0;
    double m_volume = 100;
    bool m_muted = false;
    bool m_shuffled = false;
    int m_repeatMode = 0;
    QVariantList m_queue;
    int m_queueIndex = -1;
    // Requested items occupy consecutive entries immediately after current.
    // New Play Next requests are inserted ahead of earlier requests.
    int m_playNextCount = 0;
    bool m_loadingQueue = false;
    qint64 m_selectedEntryId = -1;
    QString m_title, m_artist, m_album;
    bool m_isJumping = false;
    QTimer m_saveTimer;
    std::function<void(qint64, int)> m_saveHook;
};
