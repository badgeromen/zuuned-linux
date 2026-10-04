#include "MprisService.h"
#include "PlayerService.h"

#include <QDBusConnection>
#include <QDBusMessage>

// ── Player adaptor ──

MprisPlayer::MprisPlayer(PlayerService *player, MprisService *svc,
                         QObject *parent)
    : QDBusAbstractAdaptor(parent), m_player(player), m_svc(svc) {}

QString MprisPlayer::playbackStatus() const {
    if (m_player->playing())
        return QStringLiteral("Playing");
    if (m_player->stopped())
        return QStringLiteral("Stopped");
    return QStringLiteral("Paused");
}

QString MprisPlayer::loopStatus() const {
    switch (m_player->repeatMode()) {
    case 1:  return QStringLiteral("Playlist");
    case 2:  return QStringLiteral("Track");
    default: return QStringLiteral("None");
    }
}

void MprisPlayer::setLoopStatus(const QString &s) {
    const int want = s == QLatin1String("Playlist") ? 1
                   : s == QLatin1String("Track")    ? 2 : 0;
    // repeatMode cycles 0→1→2; step until it matches (≤2 steps).
    for (int guard = 0; guard < 3 && m_player->repeatMode() != want; guard++)
        m_player->cycleRepeat();
}

bool MprisPlayer::shuffle() const { return m_player->shuffled(); }

void MprisPlayer::setShuffle(bool on) {
    if (m_player->shuffled() != on)
        m_player->toggleShuffle();
}

QVariantMap MprisPlayer::metadata() const { return m_svc->buildMetadata(); }

double MprisPlayer::volume() const { return m_player->volume() / 100.0; }

void MprisPlayer::setVolume(double v) {
    m_player->setVolume(qBound(0.0, v, 1.0) * 100.0);
}

qlonglong MprisPlayer::position() const {
    return qlonglong(m_player->positionMs()) * 1000;
}

void MprisPlayer::Next() { m_player->next(); }
void MprisPlayer::Previous() { m_player->previous(); }

void MprisPlayer::Pause() {
    if (m_player->playing())
        m_player->playPause();
}

void MprisPlayer::PlayPause() { m_player->playPause(); }
void MprisPlayer::Stop() { m_player->stop(); }

void MprisPlayer::Play() {
    if (!m_player->playing())
        m_player->playPause();
}

void MprisPlayer::Seek(qlonglong offsetUs) {
    const double target =
        m_player->positionMs() + double(offsetUs) / 1000.0;
    m_player->seekMs(target < 0 ? 0 : target);
    emit Seeked(qlonglong(m_player->positionMs()) * 1000);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &, qlonglong posUs) {
    m_player->seekMs(double(posUs) / 1000.0);
    emit Seeked(posUs);
}

// ── Service ──

MprisService::MprisService(PlayerService *player, ArtResolver artResolver,
                           QObject *parent)
    : QObject(parent), m_player(player), m_artResolver(std::move(artResolver)) {
    new MprisRoot(this);
    m_playerAdaptor = new MprisPlayer(player, this, this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this)
        || !bus.registerService(
               QStringLiteral("org.mpris.MediaPlayer2.zuuned"))) {
        fprintf(stderr, "[mpris] session bus registration failed — media "
                        "keys won't reach us\n");
        return;
    }
    fprintf(stderr, "[mpris] registered org.mpris.MediaPlayer2.zuuned\n");

    // Property-change fan-out. MPRIS wants PropertiesChanged on the
    // standard Properties interface; adaptors don't emit it for us.
    connect(player, &PlayerService::stateChanged, this, [this] {
        notifyProperties({{QStringLiteral("PlaybackStatus"),
                           m_playerAdaptor->playbackStatus()}});
    });
    connect(player, &PlayerService::metadataChanged, this, [this] {
        notifyProperties({{QStringLiteral("Metadata"), buildMetadata()}});
    });
    connect(player, &PlayerService::queueChanged, this, [this] {
        notifyProperties({{QStringLiteral("Metadata"), buildMetadata()}});
    });
    connect(player, &PlayerService::volumeChanged, this, [this] {
        notifyProperties({{QStringLiteral("Volume"),
                           m_playerAdaptor->volume()}});
    });
    connect(player, &PlayerService::modeChanged, this, [this] {
        notifyProperties({{QStringLiteral("LoopStatus"),
                           m_playerAdaptor->loopStatus()},
                          {QStringLiteral("Shuffle"),
                           m_playerAdaptor->shuffle()}});
    });
}

QVariantMap MprisService::buildMetadata() const {
    QVariantMap m;
    m.insert(QStringLiteral("mpris:trackid"),
             QVariant::fromValue(QDBusObjectPath(
                 QStringLiteral("/com/zuuned/track/%1")
                     .arg(qMax(0, m_player->queueIndex())))));
    if (m_player->durationMs() > 0)
        m.insert(QStringLiteral("mpris:length"),
                 qlonglong(m_player->durationMs()) * 1000);
    if (!m_player->currentTitle().isEmpty())
        m.insert(QStringLiteral("xesam:title"), m_player->currentTitle());
    if (!m_player->currentArtist().isEmpty())
        m.insert(QStringLiteral("xesam:artist"),
                 QStringList{m_player->currentArtist()});
    if (!m_player->currentAlbum().isEmpty())
        m.insert(QStringLiteral("xesam:album"), m_player->currentAlbum());
    const QString art =
        artUrlFor(m_player->currentArtist(), m_player->currentAlbum());
    if (!art.isEmpty())
        m.insert(QStringLiteral("mpris:artUrl"), art);
    return m;
}

void MprisService::notifyProperties(const QVariantMap &props) {
    QDBusMessage msg = QDBusMessage::createSignal(
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << props
        << QStringList();
    QDBusConnection::sessionBus().send(msg);
}
