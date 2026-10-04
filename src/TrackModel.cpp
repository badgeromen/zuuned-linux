#include "TrackModel.h"

QVariant TrackModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const TrackRow &row = m_rows[index.row()];
    switch (role) {
    case TitleRole:       return row.title;
    case ArtistRole:      return row.artist;
    case AlbumRole:       return row.album;
    case DurationMsRole:  return row.durationMs;
    case TrackNumberRole: return row.trackNumber;
    case ItemIdRole:      return row.itemId;
    case DiscNumberRole: return row.discNumber;
    case TrackNumberReliableRole: return row.trackNumberReliable;
    case DiscNumberReliableRole: return row.discNumberReliable;
    case IdentityFromReadbackRole: return row.identityFromReadback;
    }
    return {};
}

QHash<int, QByteArray> TrackModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {DurationMsRole, "durationMs"},
        {TrackNumberRole, "trackNumber"},
        {ItemIdRole, "itemId"},
        {DiscNumberRole, "discNumber"},
        {TrackNumberReliableRole, "trackNumberReliable"},
        {DiscNumberReliableRole, "discNumberReliable"},
        {IdentityFromReadbackRole, "identityFromReadback"},
    };
}

void TrackModel::setRows(QVector<TrackRow> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

void TrackModel::removeByItemIds(const QSet<quint32> &ids) {
    for (int i = m_rows.size() - 1; i >= 0; i--) {
        if (ids.contains(m_rows[i].itemId)) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
}

void TrackModel::appendRow(const TrackRow &row) {
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows.append(row);
    endInsertRows();
}

QVariantList TrackModel::rowsSnapshot() const {
    QVariantList out;
    out.reserve(m_rows.size());
    for (const TrackRow &t : m_rows) {
        QVariantMap m;
        m.insert(QStringLiteral("title"), t.title);
        m.insert(QStringLiteral("artist"), t.artist);
        m.insert(QStringLiteral("albumartist"), QString());
        m.insert(QStringLiteral("album"), t.album);
        m.insert(QStringLiteral("genre"), t.genre);
        m.insert(QStringLiteral("durationMs"), t.durationMs);
        m.insert(QStringLiteral("trackNumber"), t.trackNumber);
        m.insert(QStringLiteral("discNumber"), t.discNumber);
        m.insert(QStringLiteral("trackNumberReliable"), t.trackNumberReliable);
        m.insert(QStringLiteral("discNumberReliable"), t.discNumberReliable);
        m.insert(QStringLiteral("identityFromReadback"), t.identityFromReadback);
        m.insert(QStringLiteral("year"), 0);
        m.insert(QStringLiteral("itemId"), qulonglong(t.itemId));
        m.insert(QStringLiteral("libraryId"), -1);
        m.insert(QStringLiteral("filepath"), QString());
        out.append(m);
    }
    return out;
}
