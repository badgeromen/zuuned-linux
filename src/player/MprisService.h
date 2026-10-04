#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

class PlayerService;
class MprisService;

// C1 · MPRIS2 — the app on the desktop's media bus: media keys, sound
// applets, and now-playing widgets all speak org.mpris.MediaPlayer2.
// One service object owns the bus registration; two thin adaptors
// export the Root and Player interfaces off it. Property-change
// signalling is manual (QDBusAbstractAdaptor doesn't emit
// PropertiesChanged for you).

class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRoot(QObject *parent) : QDBusAbstractAdaptor(parent) {}
    bool canQuit() const { return false; }
    bool canRaise() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Zuuned"); }
    QString desktopEntry() const { return QStringLiteral("zuuned"); }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Raise() {}
    void Quit() {}
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canControl)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    MprisPlayer(PlayerService *player, MprisService *svc, QObject *parent);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &s);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong posUs);
    void OpenUri(const QString &) {}

signals:
    void Seeked(qlonglong positionUs);

private:
    PlayerService *m_player;
    MprisService *m_svc;
};

class MprisService : public QObject {
    Q_OBJECT

public:
    using ArtResolver = std::function<QString(const QString &artist,
                                              const QString &album)>;
    MprisService(PlayerService *player, ArtResolver artResolver,
                 QObject *parent = nullptr);

    QString artUrlFor(const QString &artist, const QString &album) const {
        return m_artResolver ? m_artResolver(artist, album) : QString();
    }
    QVariantMap buildMetadata() const;

private:
    void notifyProperties(const QVariantMap &props);

    PlayerService *m_player;
    ArtResolver m_artResolver;
    MprisPlayer *m_playerAdaptor = nullptr;
};
