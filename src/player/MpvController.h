#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <thread>

struct mpv_handle;

// Thin wrapper around libmpv's C API — port of MPVPlayer.swift minus the
// macOS Metal/MoltenVK video path (video embedding arrives in Phase 7
// via the render API; the audio context needs no surface at all).
//
// Threading: a dedicated event thread loops mpv_wait_event (0.1s while
// active, 1s idle) and emits the signals below FROM THAT THREAD — the
// owner connects queued (automatic for cross-thread connections).
// time-pos is throttled to 4 Hz at the source, exactly like the mac
// build, so UI property churn stays cheap.
class MpvController : public QObject {
    Q_OBJECT

public:
    explicit MpvController(QObject *parent = nullptr);
    ~MpvController() override;

    // audioOnly → vo=null. Environment override ZUUNED_MPV_AO sets the
    // audio output driver (playertool uses "null" so the CLI gate never
    // hijacks the user's audio session).
    bool create(bool audioOnly);
    void destroy();

    bool isCreated() const { return m_mpv != nullptr; }

    // Event-loop cadence hint (mac isActive): 0.1s timeout while
    // playing, 1s when idle.
    void setActive(bool active) { m_active = active; }

    // ── Playback ──
    void loadFile(const QString &path);
    void play();
    void pause();
    void stop();
    void seekTo(double seconds, bool exact = true);
    void seekRelative(double delta);

    // ── Playlist (queue mirror) ──
    void appendToPlaylist(const QString &path);
    void movePlaylistEntry(int from, int before);
    void playlistNext();
    void playlistPrev();
    void clearPlaylist();
    // Explicit playback command; restarting the same selected entry is
    // intentional (repeat-one and queue restarts depend on it).
    void setPlaylistIndex(int index);
    qint64 playlistEntryId(int index) const;
    qint64 playingPlaylistEntryId() const;

    // ── Properties ──
    bool isPaused() const;
    double position() const;
    double duration() const;
    double volume() const;
    void setVolume(double v);
    bool isMuted() const;
    void setMuted(bool m);

    // ── Video (Phase 7) ──
    int chapterCount() const;
    void skipChapter(int delta);      // no-op if the file has no chapters
    QString trackListJson() const;    // mpv track-list property, raw JSON
    void setAudioTrack(int id);       // mpv track id; -1 = "no"
    void setSubtitleTrack(int id);    // mpv track id; -1 = "no"
    // Raw handle for MpvVideoItem's mpv_render_context. Valid between
    // create() and destroy().
    mpv_handle *handle() const { return m_mpv; }

signals:
    // Emitted from the event thread — connect queued.
    void positionChanged(double seconds);   // ≤4 Hz
    void durationChanged(double seconds);
    void pauseChanged(bool paused);
    void fileEnded(bool isEOF);             // true only for natural EOF
    // keep-open=yes: the LAST playlist entry never fires end-file — mpv
    // pauses on the final frame and sets eof-reached instead. This is
    // the end-of-queue signal (rising edge only).
    void endReached();
    void fileLoaded();
    void fileLoadedAt(qint64 playlistEntryId);
    void fileFailedAt(qint64 playlistEntryId);
    void metadataChanged(const QString &title, const QString &artist,
                         const QString &album);

private:
    void command(const QStringList &args);
    void setFlag(const char *name, bool value);
    void eventLoop();

    mpv_handle *m_mpv = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_active{false};
};
