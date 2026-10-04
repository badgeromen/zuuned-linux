#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "LibraryTypes.h"

// Local library tracks for QML — same idiom as TrackModel (device), with
// library identity + filepath so rows can be played/queued/synced.
class LocalTrackModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        LibraryIdRole = Qt::UserRole + 1,
        FilepathRole,
        TitleRole,
        ArtistRole,
        AlbumArtistRole,
        AlbumRole,
        GenreRole,
        DurationMsRole,
        TrackNumberRole,
        YearRole,
        DiscNumberRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_displayRows.size();
    }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<LibTrack> rows);
    const QVector<LibTrack> &rows() const { return m_rows; }
    const QVector<LibTrack> &displayRows() const { return m_displayRows; }
    qint64 representativeId(qint64 id) const { return m_representatives.value(id, id); }

    // Whole-library snapshot for QML in ONE call. Replaces the QML
    // Instantiator that materialized a QObject PER ROW — at 40k tracks
    // that froze the UI thread and ballooned memory unboundedly (the
    // deleteLater backlog never drained). Same keys as the device
    // TrackModel's snapshot so MusicPage stays source-agnostic.
    Q_INVOKABLE QVariantList rowsSnapshot() const;

private:
    QVector<LibTrack> m_rows;
    // Keep source IDs available to saved playlists and metadata editing.
    QVector<LibTrack> m_displayRows;
    QHash<qint64, qint64> m_representatives;
};
