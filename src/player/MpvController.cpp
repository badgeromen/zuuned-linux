#include "MpvController.h"

#include <QSettings>

#include <mpv/client.h>

#include <chrono>
#include <clocale>
#include <cstdio>
#include <vector>

MpvController::MpvController(QObject *parent) : QObject(parent) {}

MpvController::~MpvController() {
    destroy();
}

bool MpvController::create(bool audioOnly) {
    // libmpv refuses to create a context under a non-C numeric locale
    // (Qt applies the user's locale at app construction)
    setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        fprintf(stderr, "[mpv] failed to create context\n");
        return false;
    }

    if (audioOnly) {
        mpv_set_option_string(m_mpv, "vo", "null");
    } else {
        // Phase 7 video context: the render API (MpvVideoItem creates
        // the mpv_render_context on the Qt Quick render thread). voMUST
        // be libmpv — no wid embedding on Wayland/Hyprland.
        mpv_set_option_string(m_mpv, "vo", "libmpv");
        // VAAPI etc. where safe; falls back to software cleanly.
        mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    }

    mpv_set_option_string(m_mpv, "profile", "fast");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "idle", "yes");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");

    // Cache for network playback (kubeplex is a CIFS mount)
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "150MiB");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "75MiB");

    // Audio language preference (same key the mac reads)
    const QString lang = QSettings().value(
        QStringLiteral("preferredAudioLang"), QStringLiteral("eng")).toString();
    mpv_set_option_string(m_mpv, "alang", qPrintable(lang));

    // Audio output: auto (pipewire on this system). ZUUNED_MPV_AO
    // overrides — playertool passes "null" so gates never hijack audio.
    const QByteArray ao = qgetenv("ZUUNED_MPV_AO");
    if (!ao.isEmpty())
        mpv_set_option_string(m_mpv, "ao", ao.constData());

    const int err = mpv_initialize(m_mpv);
    if (err < 0) {
        fprintf(stderr, "[mpv] initialize failed: %s\n", mpv_error_string(err));
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return false;
    }

    mpv_observe_property(m_mpv, 1, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 2, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 3, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 4, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 5, "metadata/by-key/artist", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 6, "metadata/by-key/album", MPV_FORMAT_STRING);
    // end-of-queue detection under keep-open (no end-file on last entry)
    mpv_observe_property(m_mpv, 7, "eof-reached", MPV_FORMAT_FLAG);

    m_quit = false;
    m_thread = std::thread([this] { eventLoop(); });

    fprintf(stderr, "[mpv] initialized (%s context)\n",
            audioOnly ? "audio" : "video/render-API");
    return true;
}

void MpvController::destroy() {
    if (!m_mpv)
        return;
    m_quit = true;
    mpv_wakeup(m_mpv);
    if (m_thread.joinable())
        m_thread.join();
    // Sends quit + waits + destroys, safe now that no thread waits on it
    mpv_terminate_destroy(m_mpv);
    m_mpv = nullptr;
}

void MpvController::command(const QStringList &args) {
    if (!m_mpv)
        return;
    std::vector<QByteArray> bytes;
    bytes.reserve(args.size());
    std::vector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (const QString &a : args) {
        bytes.push_back(a.toUtf8());
        argv.push_back(bytes.back().constData());
    }
    argv.push_back(nullptr);
    mpv_command(m_mpv, argv.data());
}

void MpvController::setFlag(const char *name, bool value) {
    if (!m_mpv)
        return;
    int flag = value ? 1 : 0;
    mpv_set_property(m_mpv, name, MPV_FORMAT_FLAG, &flag);
}

// ── Playback ──

void MpvController::loadFile(const QString &path) {
    command({QStringLiteral("loadfile"), path});
}

void MpvController::play() { setFlag("pause", false); }
void MpvController::pause() { setFlag("pause", true); }
void MpvController::stop() { command({QStringLiteral("stop")}); }

void MpvController::seekTo(double seconds, bool exact) {
    command({QStringLiteral("seek"),
             QString::number(seconds, 'f', 2),
             exact ? QStringLiteral("absolute+exact") : QStringLiteral("absolute")});
}

void MpvController::seekRelative(double delta) {
    command({QStringLiteral("seek"), QString::number(delta, 'f', 2),
             QStringLiteral("relative")});
}

// ── Playlist ──

void MpvController::appendToPlaylist(const QString &path) {
    command({QStringLiteral("loadfile"), path, QStringLiteral("append")});
}

void MpvController::movePlaylistEntry(int from, int before) {
    command({QStringLiteral("playlist-move"), QString::number(from), QString::number(before)});
}

void MpvController::playlistNext() { command({QStringLiteral("playlist-next")}); }
void MpvController::playlistPrev() { command({QStringLiteral("playlist-prev")}); }
void MpvController::clearPlaylist() { command({QStringLiteral("playlist-clear")}); }

void MpvController::setPlaylistIndex(int index) {
    command({QStringLiteral("playlist-play-index"), QString::number(index)});
}

qint64 MpvController::playlistEntryId(int index) const {
    int64_t id = -1;
    if (m_mpv) {
        const QByteArray key = QStringLiteral("playlist/%1/id").arg(index).toUtf8();
        mpv_get_property(m_mpv, key.constData(), MPV_FORMAT_INT64, &id);
    }
    return id;
}

qint64 MpvController::playingPlaylistEntryId() const {
    int64_t index = -1;
    if (m_mpv)
        mpv_get_property(m_mpv, "playlist-playing-pos", MPV_FORMAT_INT64, &index);
    return index < 0 ? -1 : playlistEntryId(int(index));
}

// ── Properties ──

bool MpvController::isPaused() const {
    if (!m_mpv)
        return true;
    int flag = 1;
    mpv_get_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
    return flag != 0;
}

double MpvController::position() const {
    if (!m_mpv)
        return 0;
    double pos = 0;
    mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    return pos;
}

double MpvController::duration() const {
    if (!m_mpv)
        return 0;
    double dur = 0;
    mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &dur);
    return dur;
}

double MpvController::volume() const {
    if (!m_mpv)
        return 100;
    double vol = 100;
    mpv_get_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    return vol;
}

void MpvController::setVolume(double v) {
    if (!m_mpv)
        return;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &v);
}

bool MpvController::isMuted() const {
    if (!m_mpv)
        return false;
    int flag = 0;
    mpv_get_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
    return flag != 0;
}

void MpvController::setMuted(bool m) { setFlag("mute", m); }

int MpvController::chapterCount() const {
    if (!m_mpv)
        return 0;
    int64_t count = 0;
    mpv_get_property(m_mpv, "chapters", MPV_FORMAT_INT64, &count);
    return int(count);
}

void MpvController::skipChapter(int delta) {
    command({QStringLiteral("add"), QStringLiteral("chapter"),
             QString::number(delta)});
}

// mpv's track-list as JSON (mac trackList() note: the JSON string is
// far less noisy than decoding a NODE tree from C for the same data).
QString MpvController::trackListJson() const {
    if (!m_mpv)
        return QStringLiteral("[]");
    char *cstr = mpv_get_property_string(m_mpv, "track-list");
    if (!cstr)
        return QStringLiteral("[]");
    const QString json = QString::fromUtf8(cstr);
    mpv_free(cstr);
    return json;
}

void MpvController::setAudioTrack(int id) {
    if (!m_mpv)
        return;
    mpv_set_property_string(m_mpv, "aid",
                            id < 0 ? "no" : qPrintable(QString::number(id)));
}

void MpvController::setSubtitleTrack(int id) {
    if (!m_mpv)
        return;
    mpv_set_property_string(m_mpv, "sid",
                            id < 0 ? "no" : qPrintable(QString::number(id)));
}

// ── Event loop (dedicated thread) ──

void MpvController::eventLoop() {
    using clock = std::chrono::steady_clock;
    clock::time_point lastTimePos{};  // 4 Hz throttle anchor
    qint64 loadingEntryId = -1;

    while (!m_quit) {
        mpv_event *event = mpv_wait_event(m_mpv, m_active ? 0.1 : 1.0);
        if (!event || m_quit)
            continue;

        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            if (!prop)
                break;
            const QByteArray name(prop->name);

            if (name == "time-pos") {
                // Rate-limit at the source — mpv fires per decoded
                // frame; 4 Hz is plenty for a readout + seek dot.
                const auto now = clock::now();
                if (now - lastTimePos < std::chrono::milliseconds(250))
                    break;
                lastTimePos = now;
                if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
                    emit positionChanged(*static_cast<double *>(prop->data));
            } else if (name == "duration") {
                if (prop->format == MPV_FORMAT_DOUBLE && prop->data)
                    emit durationChanged(*static_cast<double *>(prop->data));
            } else if (name == "pause") {
                if (prop->format == MPV_FORMAT_FLAG && prop->data)
                    emit pauseChanged(*static_cast<int *>(prop->data) != 0);
            } else if (name == "media-title") {
                if (prop->format == MPV_FORMAT_STRING && prop->data) {
                    const char *s = *static_cast<char **>(prop->data);
                    emit metadataChanged(QString::fromUtf8(s ? s : ""),
                                         QString(), QString());
                }
            } else if (name == "metadata/by-key/artist") {
                if (prop->format == MPV_FORMAT_STRING && prop->data) {
                    const char *s = *static_cast<char **>(prop->data);
                    emit metadataChanged(QString(),
                                         QString::fromUtf8(s ? s : ""), QString());
                }
            } else if (name == "metadata/by-key/album") {
                if (prop->format == MPV_FORMAT_STRING && prop->data) {
                    const char *s = *static_cast<char **>(prop->data);
                    emit metadataChanged(QString(), QString(),
                                         QString::fromUtf8(s ? s : ""));
                }
            } else if (name == "eof-reached") {
                if (prop->format == MPV_FORMAT_FLAG && prop->data
                    && *static_cast<int *>(prop->data) != 0)
                    emit endReached();
            }
            break;
        }

        case MPV_EVENT_END_FILE: {
            // Only a natural EOF advances the queue — not stop/error
            bool isEOF = true;
            if (event->data) {
                auto *ef = static_cast<mpv_event_end_file *>(event->data);
                isEOF = (ef->reason == MPV_END_FILE_REASON_EOF);
                if (ef->reason == MPV_END_FILE_REASON_ERROR)
                    emit fileFailedAt(ef->playlist_entry_id);
            }
            emit fileEnded(isEOF);
            break;
        }

        case MPV_EVENT_START_FILE:
            if (event->data)
                loadingEntryId = static_cast<mpv_event_start_file *>(event->data)->playlist_entry_id;
            break;

        case MPV_EVENT_FILE_LOADED:
            emit fileLoaded();
            emit fileLoadedAt(loadingEntryId);
            break;

        case MPV_EVENT_SHUTDOWN:
            return;

        default:
            break;
        }
    }
}
