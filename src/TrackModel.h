#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVector>
#include <QtQml/qqmlregistration.h>

struct TrackRow {
    quint32 itemId = 0;
    QString title;
    QString artist;
    QString album;
    QString genre;
    int durationMs = 0;
    int trackNumber = 0;
    int discNumber = 0;
    bool trackNumberReliable = false; // classic ZMDB track slot is not trustworthy
    bool discNumberReliable = true;
    bool identityFromReadback = true;
    qint64 filesize = 0;   // ZMDB size — feeds the storage breakdown
};

// Device library tracks for the QML ListView. Filled wholesale after a
// ZMDB scan (zune_infiltrate) — rows never mutate individually.
class TrackModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        DurationMsRole,
        TrackNumberRole,
        ItemIdRole,
        DiscNumberRole,
        TrackNumberReliableRole,
        DiscNumberReliableRole,
        IdentityFromReadbackRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QVector<TrackRow> rows);
    // Snapshot twin of LocalTrackModel::rowsSnapshot (see there).
    Q_INVOKABLE QVariantList rowsSnapshot() const;
    // W3: total ZMDB bytes for a set of item ids (freed-space credit).
    qint64 sumFilesizeForIds(const QSet<quint32> &ids) const {
        qint64 total = 0;
        for (const TrackRow &t : m_rows)
            if (ids.contains(t.itemId))
                total += t.filesize;
        return total;
    }
    void removeByItemIds(const QSet<quint32> &ids);
    void appendRow(const TrackRow &row);

private:
    QVector<TrackRow> m_rows;
};
