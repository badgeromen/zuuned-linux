#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QtQml/qqmlregistration.h>

// Sync queue — port of SyncQueueService.swift (itself a port of
// src/sync_queue.c). Tracks (Phase 5) + videos (Phase 6); photo and
// playlist types land with their phases.
//
// Statuses: pending → transcoding → syncing → verified | skipped | failed
struct SyncQueueEntry {
    QString entryId;        // uuid
    QString type;           // "track" | "video"
    QString filepath;
    QString title;
    QString artist;
    QString albumartist;    // falls back to artist at add time (mac rule)
    QString album;
    QString genre;
    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    int durationMs = 0;
    qint64 libraryId = -1;
    QString status = QStringLiteral("pending");
    double progress = 0.0;
    QString statusNote;

    // Video-only (SyncQueueEntry.swift video fields)
    QString videoSeries;
    int videoSeason = 0;
    int videoEpisode = 0;
    QString videoDescription;
    QString videoPosterPath;   // local poster jpeg for device art
    QString videoCategory;     // "tv" | "movie" | "music_video" | "other"

    // Post-transcode size estimate (capacity gate + queue readout).
    qint64 estimatedBytes = 0;
};

class SyncQueueModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Sum of post-transcode estimates over entries that will actually
    // sync (pending + failed, the re-arm set). Drives the queue tab's
    // "~X GB queued" readout and the engine's capacity gate.
    Q_PROPERTY(double pendingEstimatedBytes READ pendingEstimatedBytes
                   NOTIFY estimateChanged)

public:
    enum Roles {
        EntryIdRole = Qt::UserRole + 1,
        TypeRole,
        FilepathRole,
        TitleRole,
        ArtistRole,
        AlbumArtistRole,
        AlbumRole,
        GenreRole,
        TrackNumberRole,
        DurationMsRole,
        LibraryIdRole,
        StatusRole,
        ProgressRole,
        StatusNoteRole,
        // Video entries (Phase 6): poster for the queue row art cell,
        // series for the subtitle line.
        PosterPathRole,
        SeriesRole,
        // Collapsible-group support: episodes share a key per
        // series+season, tracks per albumartist+album, singles are
        // their own key.
        GroupKeyRole,
        DiscNumberRole,
        YearRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_entries.size();
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }
    double pendingEstimatedBytes() const;

    // Mac dedup: libraryId>=0 match OR filepath OR title+artist.
    // Returns the new entry's id, or empty when deduplicated/invalid.
    QString addTrack(const QString &filepath, const QString &title,
                     const QString &artist, const QString &albumartist,
                     const QString &album, const QString &genre,
                     int trackNumber, int durationMs, qint64 libraryId,
                     qint64 estimatedBytes = 0, int discNumber = 0, int year = 0);

    // Photo entry (dedup: filepath). `album` = device album name AND
    // the queue group.
    QString addPhoto(const QString &filepath, const QString &filename,
                     const QString &album, qint64 libraryId = -1,
                     qint64 estimatedBytes = 0);

    // Video entry (dedup: filepath). displayName rides in `title`.
    QString addVideo(const QString &filepath, const QString &displayName,
                     const QString &description, const QString &posterPath,
                     const QString &series, int season, int episode,
                     const QString &category, qint64 libraryId,
                     qint64 estimatedBytes = 0);

    // Playlist forge entry (UX-2 ride-along sync) — the member list
    // lives in the ENGINE keyed by the returned entryId; this row is
    // the queue's visible representation. Dedup: playlist name.
    QString addPlaylistEntry(const QString &name, int trackCount);

    // A1 queue persistence: re-insert a saved entry verbatim (keeps its
    // entryId so engine-side playlist member lists stay wired).
    void restoreEntry(const SyncQueueEntry &e);

    void removeEntry(const QString &entryId);
    void clearQueue();
    // Post-sync sweep: drop verified/skipped entries, KEEP failed ones
    // visible for retry (pressing sync re-arms them).
    void removeCompleted();
    QVector<SyncQueueEntry> entries() const { return m_entries; }

    void setStatus(const QString &entryId, const QString &status,
                   const QString &note = QString());
    void setProgress(const QString &entryId, double frac);

    // Collapsible groups for the queue panel: one summary per group in
    // queue order — {key, kind: "series"|"album"|"single", title,
    // subtitle, count, doneCount, failedCount, firstEntryId, posterPath,
    // artist, album, filepath, entryIds}. Entries insert ADJACENT to
    // their group (see add*), so groups are always contiguous and the
    // first member can render the header.
    Q_INVOKABLE QVariantList groupSummary() const;
    // Remove a whole group (header ✕).
    Q_INVOKABLE void removeGroup(const QString &groupKey);
    static QString groupKeyFor(const SyncQueueEntry &e);

    // Bulk-add batching: countChanged/estimateChanged fire ONCE at
    // endBatch instead of per row. Without this, queueing a 599-episode
    // show rebuilt the QML group summary ~1200 times and froze the UI.
    void beginBatch() { m_batching = true; }
    void endBatch();

signals:
    void countChanged();
    void estimateChanged();

private:
    int indexOfEntry(const QString &entryId) const;
    void insertGrouped(SyncQueueEntry &&e);
    QVector<SyncQueueEntry> m_entries;
    bool m_batching = false;
    bool m_batchDirty = false;
};
