#include "LocalTrackModel.h"
#include "LocalMusicCopies.h"

QVariant LocalTrackModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayRows.size())
        return {};
    const LibTrack &t = m_displayRows[index.row()];
    switch (role) {
    case LibraryIdRole:   return t.id;
    case FilepathRole:    return t.filepath;
    case TitleRole:       return t.title;
    case ArtistRole:      return t.artist;
    case AlbumArtistRole: return t.albumartist;
    case AlbumRole:       return t.album;
    case GenreRole:       return t.genre;
    case DurationMsRole:  return t.durationMs;
    case TrackNumberRole: return t.trackNumber;
    case YearRole:        return t.year;
    case DiscNumberRole:  return t.discNumber;
    }
    return {};
}

QHash<int, QByteArray> LocalTrackModel::roleNames() const {
    return {
        {LibraryIdRole, "libraryId"},
        {FilepathRole, "filepath"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumArtistRole, "albumartist"},
        {AlbumRole, "album"},
        {GenreRole, "genre"},
        {DurationMsRole, "durationMs"},
        {TrackNumberRole, "trackNumber"},
        {YearRole, "year"},
        {DiscNumberRole, "discNumber"},
    };
}

void LocalTrackModel::setRows(QVector<LibTrack> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    auto grouped = LocalMusicCopies::groupRows(m_rows);
    m_displayRows = std::move(grouped.rows);
    m_representatives = std::move(grouped.representatives);
    endResetModel();
}

QVariantList LocalTrackModel::rowsSnapshot() const {
    QVariantList out;
    out.reserve(m_displayRows.size());
    for (const LibTrack &t : m_displayRows) {
        QVariantMap m;
        m.insert(QStringLiteral("title"), t.title);
        m.insert(QStringLiteral("artist"), t.artist);
        m.insert(QStringLiteral("albumartist"), t.albumartist);
        m.insert(QStringLiteral("album"), t.album);
        m.insert(QStringLiteral("genre"), t.genre);
        m.insert(QStringLiteral("durationMs"), t.durationMs);
        m.insert(QStringLiteral("trackNumber"), t.trackNumber);
        m.insert(QStringLiteral("year"), t.year);
        m.insert(QStringLiteral("discNumber"), t.discNumber);
        m.insert(QStringLiteral("itemId"), 0);
        m.insert(QStringLiteral("libraryId"), t.id);
        m.insert(QStringLiteral("filepath"), t.filepath);
        out.append(m);
    }
    return out;
}
