#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class MpvController;

// Video player — port of VideoPlayerService.swift over the shared
// MpvController (video mode: vo=libmpv + hwdec=auto-safe; the render
// surface is MpvVideoItem's mpv_render_context).
//
// The load-bearing mac behaviors, kept exactly:
// - the binge queue is MIRRORED into mpv's playlist; mpv auto-advances
//   on EOF and handleTrackEnded only SYNCS queueIndex/metadata
// - `m_isJumping` guards EOF handling during manual jumps (cleared
//   0.3s after)
// - at EOF the FINISHED file's position is saved while currentLibraryId
//   still points at it (position ≈ duration → the library's 90% rule
//   marks it watched) BEFORE the index moves
// - prev() restarts the current file when >3s in
// - skipChapter falls back to ±60s seeks when the file has no chapters
// - a 5s timer persists playback position through the save hook
class VideoPlayerService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)   // seconds
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)   // seconds
    // VinylRecordView player interface (same shape as PlayerService —
    // the disc binds to either service unchanged).
    Q_PROPERTY(double positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(double durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY queueChanged)
    Q_PROPERTY(QString currentPoster READ currentPoster NOTIFY queueChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY queueChanged)
    Q_PROPERTY(int queueCount READ queueCount NOTIFY queueChanged)
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int chapterCount READ chapterCount NOTIFY durationChanged)

public:
    explicit VideoPlayerService(QObject *parent = nullptr);
    ~VideoPlayerService() override;

    bool active() const { return m_state != Stopped; }
    bool playing() const { return m_state == Playing; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double positionMs() const { return m_position * 1000.0; }
    double durationMs() const { return m_duration * 1000.0; }
    double volume() const;
    void setVolume(double v);
    bool muted() const;
    QString currentTitle() const;
    QString currentPoster() const;
    int queueIndex() const { return m_queueIndex; }
    int queueCount() const { return int(m_queue.size()); }
    QVariantList queue() const { return m_queue; }
    int chapterCount() const;

    // Play one file (movie / one-off). libraryId < 0 → no position save.
    Q_INVOKABLE void play(const QString &filepath, const QString &title,
                          double libraryId, int resumeMs = 0);
    // Binge queue: items are maps {filepath, title, libraryId}. The mac
    // setQueue — mirrored into mpv's playlist, started at startIndex.
    Q_INVOKABLE void setQueue(const QVariantList &items, int startIndex,
                              int resumeMs = 0);
    Q_INVOKABLE void togglePause();
    // Vinyl-interface aliases
    Q_INVOKABLE void playPause() { togglePause(); }
    Q_INVOKABLE void seekMs(double ms) { seek(ms / 1000.0); }
    // Fast keyframe seek for live scrubbing (vinyl ring drag) — exact
    // seeks on video decode from a keyframe each time and lag the hand.
    Q_INVOKABLE void scrubMs(double ms);
    // Scrub bracket: pause for the drag (seeking while PLAYING restarts
    // playback from the same keyframe every 150ms — the audible
    // stuck-repeat), then exact-seek + resume on release.
    Q_INVOKABLE void beginScrub();
    Q_INVOKABLE void endScrubMs(double ms);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double delta);
    Q_INVOKABLE void skipChapter(int delta);   // ±60s when chapterless
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    // Ribbon card click — jump straight to a queue index.
    Q_INVOKABLE void jumpTo(int index);
    Q_INVOKABLE void toggleMute();
    // Track pickers: [{id, kind, title, lang, selected}]
    Q_INVOKABLE QVariantList audioTracks() const;
    Q_INVOKABLE QVariantList subtitleTracks() const;
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id);

    // main.cpp wires this to LibraryService (id, positionMs, durationMs).
    void setSavePositionHook(
        std::function<void(qint64, int, int)> hook) { m_saveHook = std::move(hook); }

    MpvController *mpv() const { return m_mpv; }

signals:
    void stateChanged();
    void playingChanged();
    void positionChanged();
    void positionMsChanged();
    void durationChanged();
    void durationMsChanged();
    void volumeChanged();
    void queueChanged();
    void queueFinished();

private:
    enum State { Stopped, Playing, Paused };

    void handleFileEnded(bool isEOF);
    void savePlaybackPosition();
    void startSaveTimer();
    QVariantList tracksOfKind(const QString &kind) const;

    MpvController *m_mpv = nullptr;
    State m_state = Stopped;
    double m_position = 0;
    double m_duration = 0;
    QVariantList m_queue;   // maps: filepath, title, libraryId
    int m_queueIndex = -1;
    qint64 m_currentLibraryId = -1;
    bool m_isJumping = false;
    bool m_resumeAfterScrub = false;
    QTimer m_saveTimer;
    QTimer m_jumpGuard;
    std::function<void(qint64, int, int)> m_saveHook;
};
