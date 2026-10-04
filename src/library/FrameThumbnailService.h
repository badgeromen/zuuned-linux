#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

// Frame-grab stills for episodes/series with no TMDB still — port of
// FrameThumbnailService.swift. Extraction decodes one frame via
// zuuned_extract_frame (full open + seek — expensive over SMB), so
// requests are bounded to 2 concurrent and duplicates coalesce.
//
// Cache is filesystem-only: stills/frame_<videoId>.jpg existing IS the
// hit. Results land in `stills` keyed by the videoId as a string,
// value = file:// url ("" for permanent failures).
class FrameThumbnailService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantMap stills READ stills NOTIFY stillsChanged)

public:
    explicit FrameThumbnailService(QObject *parent = nullptr);

    QVariantMap stills() const { return m_stills; }

    // 10% in dodges intros/recaps (capped at 10 min); unprobed files
    // fall back to 10s. No-op when cached, in flight, or failed before.
    Q_INVOKABLE void requestStill(double videoId, const QString &filepath,
                                  int durationMs);

signals:
    void stillsChanged();

private:
    void pump();

    struct Pending {
        qint64 id;
        QString filepath;
        int durationMs;
    };
    QVector<Pending> m_queue;
    QSet<qint64> m_inFlight;
    QSet<qint64> m_failed;
    QVariantMap m_stills;
    QString m_dir;
    int m_active = 0;
};
