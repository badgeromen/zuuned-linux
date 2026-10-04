#pragma once

#include "LibraryTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

// Folder scanner — port of the mac's performScan: per media type,
// 3 phases — (1) serial prep: enumerate + stat + exclusion check +
// mtime-unchanged fast-path + user_edited protection; (2) parallel
// metadata probe on QThreadPool (audio chunks of 64, photos 128) via
// zune_probe() (in-process libavformat); (3) serial chunked
// transactional upsert, emitting a refresh after each chunk so the UI
// streams in.
//
// Metadata fallback chain (mac parity): file tags → folder structure
// (WatchFolder/Artist/Album/file) → "Artist - Title" filename parse.
//
// Lives on its own QThread; owns its own LibraryDb connection (opened
// on first scan). All signals are emitted from the worker thread —
// connect queued.
class LibraryScanner : public QObject {
    Q_OBJECT

public:
    explicit LibraryScanner(QObject *parent = nullptr);
    // Explicit DB path for tests; default = the app DB.
    void setDbPath(const QString &path) { m_dbPath = path; }

public slots:
    // Full scan of every watch folder in the DB. Safe to call again
    // after completion; ignores calls while a scan runs.
    void scan();
    void cancel();

signals:
    // stage: "music" | "videos" | "photos"; current/total are per-stage
    // file counts
    void progress(const QString &stage, int current, int total,
                  const QString &currentFile);
    // Emitted after each committed chunk — the UI facade reloads.
    void chunkCommitted();
    void finished(int tracksAdded, int tracksUpdated, int photosAdded);
    void failed(const QString &error);
    // Quiet diagnostics for Settings; failed reads never become fake tracks.
    void musicProbeFailuresChanged();

private:
    QString m_dbPath;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};
};
