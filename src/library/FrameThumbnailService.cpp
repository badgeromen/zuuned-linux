#include "FrameThumbnailService.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

extern "C" {
#include "libav_transcode.h"
}

FrameThumbnailService::FrameThumbnailService(QObject *parent)
    : QObject(parent) {
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/stills");
    QDir().mkpath(m_dir);
}

void FrameThumbnailService::requestStill(double videoId, const QString &filepath,
                                         int durationMs) {
    const qint64 id = qint64(videoId);
    const QString key = QString::number(id);
    if (m_stills.contains(key) || m_inFlight.contains(id) || m_failed.contains(id))
        return;

    const QString dest = m_dir + QStringLiteral("/frame_%1.jpg").arg(id);
    if (QFile::exists(dest)) {
        m_stills.insert(key, QUrl::fromLocalFile(dest).toString());
        emit stillsChanged();
        return;
    }

    m_inFlight.insert(id);
    m_queue.append({id, filepath, durationMs});
    pump();
}

void FrameThumbnailService::pump() {
    while (m_active < 2 && !m_queue.isEmpty()) {
        const Pending p = m_queue.takeFirst();
        m_active++;

        const QString dest = m_dir + QStringLiteral("/frame_%1.jpg").arg(p.id);
        const double seconds = p.durationMs > 0
            ? qMin(600.0, double(p.durationMs) / 1000.0 * 0.1)
            : 10.0;

        auto future = QtConcurrent::run([filepath = p.filepath, seconds, dest] {
            return zuuned_extract_frame(filepath.toUtf8().constData(), seconds,
                                        dest.toUtf8().constData(), 400) == 0;
        });
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this,
                [this, watcher, id = p.id, dest] {
            watcher->deleteLater();
            m_active--;
            m_inFlight.remove(id);
            if (watcher->result()) {
                m_stills.insert(QString::number(id),
                                QUrl::fromLocalFile(dest).toString());
                emit stillsChanged();
            } else {
                // One attempt per session — a failed row never re-launches
                // a full open+seek+decode on every scroll-back.
                m_failed.insert(id);
            }
            pump();
        });
        watcher->setFuture(future);
    }
}
