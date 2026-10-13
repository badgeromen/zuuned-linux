#include "SyncQueueModel.h"

#include <QUuid>

QVariant SyncQueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const SyncQueueEntry &e = m_entries[index.row()];
    switch (role) {
    case EntryIdRole:     return e.entryId;
    case TypeRole:        return e.type;
    case FilepathRole:    return e.filepath;
    case TitleRole:       return e.title;
    case ArtistRole:      return e.artist;
    case AlbumArtistRole: return e.albumartist;
    case AlbumRole:       return e.album;
    case GenreRole:       return e.genre;
    case TrackNumberRole: return e.trackNumber;
    case DiscNumberRole: return e.discNumber;
    case YearRole: return e.year;
    case DurationMsRole:  return e.durationMs;
    case LibraryIdRole:   return double(e.libraryId);
    case StatusRole:      return e.status;
    case ProgressRole:    return e.progress;
    case StatusNoteRole:  return e.statusNote;
    case PosterPathRole:  return e.videoPosterPath;
    case SeriesRole:      return e.videoSeries;
    case GroupKeyRole:    return groupKeyFor(e);
    }
    return {};
}

QString SyncQueueModel::groupKeyFor(const SyncQueueEntry &e) {
    if (e.type == QLatin1String("podcast-removal"))
        return QStringLiteral("podcast-removal\x01") + e.podcast.value("feedUrl").toString();
    if (e.type == QLatin1String("audiobook"))
        return QStringLiteral("audiobook\x01") + (e.audiobook.value("bookId").toString().isEmpty()
            ? e.audiobook.value("bookKey").toString() : e.audiobook.value("bookId").toString());
    if (e.type == QLatin1String("podcast"))
        return QStringLiteral("podcast\x01") + e.podcast.value("feedUrl").toString();
    if (e.type == QLatin1String("video") && !e.videoSeries.isEmpty()
        && e.videoEpisode > 0)
        return QStringLiteral("s\x01%1\x01%2")
            .arg(e.videoSeries.toLower()).arg(e.videoSeason);
    if (e.type == QLatin1String("track") && !e.album.isEmpty())
        return QStringLiteral("a\x01%1\x01%2")
            .arg(e.albumartist.toLower(), e.album.toLower());
    if (e.type == QLatin1String("photo"))
        return QStringLiteral("p\x01")
            + (e.album.isEmpty() ? QStringLiteral("photos")
                                 : e.album.toLower());
    return QStringLiteral("i\x01") + e.entryId;   // its own group of one
}

QHash<int, QByteArray> SyncQueueModel::roleNames() const {
    return {
        {EntryIdRole, "entryId"},
        {TypeRole, "type"},
        {FilepathRole, "filepath"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumArtistRole, "albumartist"},
        {AlbumRole, "album"},
        {GenreRole, "genre"},
        {TrackNumberRole, "trackNumber"},
        {DiscNumberRole, "discNumber"},
        {YearRole, "year"},
        {DurationMsRole, "durationMs"},
        {LibraryIdRole, "libraryId"},
        {StatusRole, "status"},
        {ProgressRole, "progress"},
        {StatusNoteRole, "statusNote"},
        {PosterPathRole, "posterPath"},
        {SeriesRole, "series"},
        {GroupKeyRole, "groupKey"},
    };
}

// Insert keeping groups CONTIGUOUS: after the last entry sharing the
// group key, else at the end. Contiguity is what lets the first member
// render the collapsible header (and makes syncs run show-by-show).
void SyncQueueModel::insertGrouped(SyncQueueEntry &&e) {
    const QString key = groupKeyFor(e);
    int at = m_entries.size();
    for (int i = m_entries.size() - 1; i >= 0; i--) {
        if (groupKeyFor(m_entries[i]) == key) {
            at = i + 1;
            break;
        }
    }
    if (e.type == QLatin1String("track") && !e.album.isEmpty()) {
        for (int i = 0; i < at; ++i) {
            const auto &other = m_entries[i];
            if (groupKeyFor(other) != key) continue;
            const int disc = e.discNumber > 0 ? e.discNumber : 1;
            const int otherDisc = other.discNumber > 0 ? other.discNumber : 1;
            if (disc < otherDisc || (disc == otherDisc && e.trackNumber > 0
                && other.trackNumber > 0 && e.trackNumber < other.trackNumber)) {
                at = i;
                break;
            }
        }
    }
    beginInsertRows({}, at, at);
    m_entries.insert(at, std::move(e));
    endInsertRows();
    if (m_batching) {
        m_batchDirty = true;
        return;
    }
    emit countChanged();
    emit estimateChanged();
}

void SyncQueueModel::endBatch() {
    if (!m_batching)
        return;
    m_batching = false;
    if (m_batchDirty) {
        m_batchDirty = false;
        emit countChanged();
        emit estimateChanged();
    }
}

QVariantList SyncQueueModel::groupSummary() const {
    QVariantList out;
    QHash<QString, int> indexByKey;
    for (const SyncQueueEntry &e : m_entries) {
        const QString key = groupKeyFor(e);
        const bool done = e.status == QLatin1String("verified")
            || e.status == QLatin1String("sent")
            || e.status == QLatin1String("skipped")
            || e.status == QLatin1String("removed");
        const bool failed = e.status == QLatin1String("failed")
            || e.status == QLatin1String("removal-unconfirmed");

        const auto it = indexByKey.constFind(key);
        if (it != indexByKey.constEnd()) {
            QVariantMap g = out[it.value()].toMap();
            g[QStringLiteral("count")] = g.value(QStringLiteral("count")).toInt() + 1;
            if (done)
                g[QStringLiteral("doneCount")] =
                    g.value(QStringLiteral("doneCount")).toInt() + 1;
            if (failed)
                g[QStringLiteral("failedCount")] =
                    g.value(QStringLiteral("failedCount")).toInt() + 1;
            QVariantList ids = g.value(QStringLiteral("entryIds")).toList();
            ids.append(e.entryId);
            g[QStringLiteral("entryIds")] = ids;
            out[it.value()] = g;
            continue;
        }

        QString kind = QStringLiteral("single");
        QString title = e.title;
        QString subtitle;
        if (e.type == "podcast-removal") {
            kind = "podcast-removals";
            title = e.album.isEmpty() ? QStringLiteral("Played podcasts") : e.album;
            subtitle = "remove from Zune";
        } else if (e.type == "audiobook") {
            kind = "audiobooks"; title = e.album; subtitle = "audiobook";
        } else if (e.type == "podcast") {
            kind = "podcasts"; title = e.album; subtitle = "podcast";
        } else if (key.startsWith(QLatin1String("s\x01"))) {
            kind = QStringLiteral("series");
            // Keep the season at the start of the second line. Narrow
            // panels can elide a long show name without hiding its season.
            title = e.videoSeries;
            subtitle = e.videoSeason > 0
                ? QStringLiteral("Season %1").arg(e.videoSeason)
                : QStringLiteral("Specials");
        } else if (key.startsWith(QLatin1String("a\x01"))) {
            kind = QStringLiteral("album");
            title = e.album;
            subtitle = e.albumartist;
        } else if (key.startsWith(QLatin1String("p\x01"))) {
            kind = QStringLiteral("photos");
            title = e.album.isEmpty() ? QStringLiteral("photos") : e.album;
            subtitle = QStringLiteral("photo album");
        }
        indexByKey.insert(key, out.size());
        out.append(QVariantMap{
            {"key", key},
            {"kind", kind},
            {"title", title},
            {"subtitle", subtitle},
            {"count", 1},
            {"doneCount", done ? 1 : 0},
            {"failedCount", failed ? 1 : 0},
            {"firstEntryId", e.entryId},
            {"posterPath", e.videoPosterPath},
            {"artist", e.albumartist.isEmpty() ? e.artist : e.albumartist},
            {"album", e.album},
            {"filepath", e.filepath},
            {"entryIds", QVariantList{e.entryId}},
        });
    }
    return out;
}

void SyncQueueModel::removeCompleted() {
    bool removed = false;
    for (int i = m_entries.size() - 1; i >= 0; i--) {
        const QString &st = m_entries[i].status;
        if (st == QLatin1String("verified") || st == QLatin1String("skipped")) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            removed = true;
        }
    }
    if (removed) {
        emit countChanged();
        emit estimateChanged();
    }
}

void SyncQueueModel::removeGroup(const QString &groupKey) {
    bool removed = false;
    for (int i = m_entries.size() - 1; i >= 0; i--) {
        if (groupKeyFor(m_entries[i]) == groupKey) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            removed = true;
        }
    }
    if (removed) {
        emit countChanged();
        emit estimateChanged();
    }
}

double SyncQueueModel::pendingEstimatedBytes() const {
    qint64 sum = 0;
    for (const SyncQueueEntry &e : m_entries)
        if (e.type != QLatin1String("podcast-removal") &&
            (e.status == QLatin1String("pending")
            || e.status == QLatin1String("failed")))
            sum += e.estimatedBytes;
    return double(sum);
}

QString SyncQueueModel::addTrack(const QString &filepath, const QString &title,
                                 const QString &artist, const QString &albumartist,
                                 const QString &album, const QString &genre,
                                 int trackNumber, int durationMs, qint64 libraryId,
                                 qint64 estimatedBytes, int discNumber, int year) {
    if (filepath.isEmpty())
        return {};

    // Mac dedup rules (SyncQueueService.isDuplicate)
    for (const SyncQueueEntry &e : std::as_const(m_entries)) {
        if (libraryId >= 0 && e.type == QStringLiteral("track")
            && e.libraryId == libraryId)
            return {};
        if (e.filepath == filepath)
            return {};
        if (e.type == QStringLiteral("track")
            && !title.isEmpty() && !artist.isEmpty()
            && (libraryId < 0 || e.libraryId < 0)
            && e.album == album && e.discNumber == discNumber && e.trackNumber == trackNumber
            && e.title == title && e.artist == artist)
            return {};
    }

    SyncQueueEntry e;
    e.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.type = QStringLiteral("track");
    e.filepath = filepath;
    e.title = title;
    e.artist = artist;
    e.albumartist = albumartist.isEmpty() ? artist : albumartist; // mac rule
    e.album = album;
    e.genre = genre;
    e.trackNumber = trackNumber;
    e.discNumber = qMax(0, discNumber);
    e.year = qMax(0, year);
    e.durationMs = durationMs;
    e.libraryId = libraryId;
    e.estimatedBytes = estimatedBytes;

    const QString id = e.entryId;
    insertGrouped(std::move(e));
    return id;
}

QString SyncQueueModel::addVideo(const QString &filepath, const QString &displayName,
                                 const QString &description, const QString &posterPath,
                                 const QString &series, int season, int episode,
                                 const QString &category, qint64 libraryId,
                                 qint64 estimatedBytes) {
    if (filepath.isEmpty())
        return {};

    // Mac dedup rule for videos: filepath only.
    for (const SyncQueueEntry &e : std::as_const(m_entries))
        if (e.filepath == filepath)
            return {};

    SyncQueueEntry e;
    e.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.type = QStringLiteral("video");
    e.filepath = filepath;
    // Untitled episode → "S04E12" (row-legible); untitled movie → file
    // name. Never the show's title on every row.
    if (!displayName.isEmpty())
        e.title = displayName;
    else if (!series.isEmpty() && episode > 0)
        e.title = QStringLiteral("S%1E%2")
                      .arg(season, 2, 10, QLatin1Char('0'))
                      .arg(episode, 2, 10, QLatin1Char('0'));
    else
        e.title = filepath.section(QLatin1Char('/'), -1);
    e.libraryId = libraryId;
    e.videoSeries = series;
    e.videoSeason = season;
    e.videoEpisode = episode;
    e.videoDescription = description;
    e.videoPosterPath = posterPath;
    e.videoCategory = category;
    e.estimatedBytes = estimatedBytes;

    const QString id = e.entryId;
    insertGrouped(std::move(e));
    return id;
}

QString SyncQueueModel::addPhoto(const QString &filepath, const QString &filename,
                                 const QString &album, qint64 libraryId,
                                 qint64 estimatedBytes) {
    if (filepath.isEmpty())
        return {};
    // Photo dedup: filepath only (same rule as videos).
    for (const SyncQueueEntry &e : std::as_const(m_entries))
        if (e.filepath == filepath)
            return {};

    SyncQueueEntry e;
    e.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.type = QStringLiteral("photo");
    e.filepath = filepath;
    e.title = filename.isEmpty()
        ? filepath.section(QLatin1Char('/'), -1) : filename;
    e.album = album;   // device album name — also the queue group
    e.libraryId = libraryId;
    e.estimatedBytes = estimatedBytes;

    const QString id = e.entryId;
    insertGrouped(std::move(e));
    return id;
}

void SyncQueueModel::restoreEntry(const SyncQueueEntry &e) {
    if (e.type == QLatin1String("podcast-removal")) return;
    if (e.entryId.isEmpty())
        return;
    // No dedup here — the saved queue IS the truth being restored.
    SyncQueueEntry copy = e;
    insertGrouped(std::move(copy));
}

QString SyncQueueModel::addPlaylistEntry(const QString &name, int trackCount) {
    if (name.isEmpty())
        return {};
    // Dedup: one queued forge per playlist name.
    for (const SyncQueueEntry &e : std::as_const(m_entries))
        if (e.type == QLatin1String("playlist") && e.title == name)
            return {};

    SyncQueueEntry e;
    e.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.type = QStringLiteral("playlist");
    e.title = name;
    // The queue row's subtitle slot — membership count, not an artist.
    e.artist = QStringLiteral("playlist · %1 tracks").arg(trackCount);

    const QString id = e.entryId;
    insertGrouped(std::move(e));
    return id;
}

void SyncQueueModel::removeEntry(const QString &entryId) {
    const int i = indexOfEntry(entryId);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_entries.removeAt(i);
    endRemoveRows();
    emit countChanged();
    emit estimateChanged();
}

void SyncQueueModel::clearQueue() {
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    emit estimateChanged();
}

void SyncQueueModel::setStatus(const QString &entryId, const QString &status,
                               const QString &note) {
    const int i = indexOfEntry(entryId);
    if (i < 0)
        return;
    m_entries[i].status = status;
    m_entries[i].statusNote = note;
    const QModelIndex idx = index(i, 0);
    emit dataChanged(idx, idx, {StatusRole, StatusNoteRole});
    // Status moves entries in/out of the pending+failed estimate scope.
    emit estimateChanged();
}

void SyncQueueModel::setProgress(const QString &entryId, double frac) {
    const int i = indexOfEntry(entryId);
    if (i < 0)
        return;
    m_entries[i].progress = std::clamp(frac, 0.0, 1.0);
    const QModelIndex idx = index(i, 0);
    emit dataChanged(idx, idx, {ProgressRole});
}

int SyncQueueModel::indexOfEntry(const QString &entryId) const {
    for (int i = 0; i < m_entries.size(); i++)
        if (m_entries[i].entryId == entryId)
            return i;
    return -1;
}

QString SyncQueueModel::addPodcast(const QVariantMap &episode, qint64 bytes) {
    const auto feed = episode.value("feedUrl").toString();
    const auto identity = episode.value("identity").toString();
    if (feed.isEmpty() || identity.isEmpty() || (episode.value("filepath").toString().isEmpty() && !episode.value("needsPreparation").toBool())) return {};
    for (const auto &e : std::as_const(m_entries))
        if (e.type == "podcast" && e.podcast.value("feedUrl") == feed &&
            (e.podcast.value("identity") == identity || e.podcast.value("sourceUrl") == episode.value("sourceUrl"))) return {};
    SyncQueueEntry e;
    e.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.type = "podcast"; e.podcast = episode;
    e.filepath = episode.value("filepath").toString();
    e.title = episode.value("title").toString();
    e.artist = episode.value("artist").toString();
    e.album = episode.value("album").toString();
    e.durationMs = episode.value("durationMs").toInt();
    e.videoPosterPath = episode.value("posterPath").toString();
    e.estimatedBytes = bytes;
    const auto id = e.entryId;
    insertGrouped(std::move(e));
    return id;
}

QString SyncQueueModel::addAudiobook(const QVariantMap &episode, qint64 bytes) {
    const auto feed = episode.value("bookKey").toString();
    const auto identity = episode.value("identity").toString();
    if (feed.isEmpty() || identity.isEmpty() || (episode.value("filepath").toString().isEmpty() && !episode.value("needsPreparation").toBool())) return {};
    if (containsAudiobook(episode)) return {};
    SyncQueueEntry e;
    e.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.type = "audiobook"; e.audiobook = episode;
    e.filepath = episode.value("filepath").toString();
    e.title = episode.value("title").toString();
    e.artist = episode.value("artist").toString();
    e.album = episode.value("album").toString();
    e.durationMs = episode.value("durationMs").toInt();
    e.videoPosterPath = episode.value("posterPath").toString();
    e.estimatedBytes = bytes;
    const auto id = e.entryId;
    insertGrouped(std::move(e));
    return id;
}


void SyncQueueModel::updateSpokenPayload(const QString &entryId, const QVariantMap &payload, qint64 bytes) {
    const int i = indexOfEntry(entryId);
    if (i < 0 || (m_entries[i].type != "podcast" && m_entries[i].type != "audiobook")) return;
    auto &entry = m_entries[i];
    if (entry.type == "audiobook") entry.audiobook = payload;
    else entry.podcast = payload;
    entry.filepath = payload.value("filepath").toString();
    entry.durationMs = payload.value("durationMs").toInt();
    entry.videoPosterPath = payload.value("posterPath").toString();
    entry.estimatedBytes = bytes;
    emit dataChanged(index(i,0), index(i,0));
    emit estimateChanged();
}


bool SyncQueueModel::containsAudiobook(const QVariantMap &part) const {
    for (const auto &entry : m_entries) {
        if (entry.type != "audiobook") continue;
        const auto saved = entry.audiobook;
        const bool sameBook = saved.value("bookKey") == part.value("bookKey") ||
            (!part.value("bookId").toString().isEmpty() && saved.value("bookId") == part.value("bookId"));
        if (sameBook && (saved.value("identity")==part.value("identity") || saved.value("partNumber")==part.value("partNumber"))) return true;
        // Queues saved before immediate admission have content keys but no
        // bookId. Exact source/range and metadata identify those existing rows.
        if (saved.value("bookId").toString().isEmpty() && !part.value("identity").toString().isEmpty() &&
            saved.value("identity")==part.value("identity") && saved.value("album")==part.value("album") &&
            saved.value("artist")==part.value("artist")) return true;
    }
    return false;
}


QString SyncQueueModel::addPodcastRemoval(const QVariantMap &episode) {
    const auto itemId = episode.value("itemId").toUInt();
    if (!itemId || itemId == 0xffffffffu) return {};
    for (const auto &entry : std::as_const(m_entries))
        if (entry.type == "podcast-removal" && entry.podcast.value("itemId").toUInt() == itemId)
            return entry.entryId;
    SyncQueueEntry entry;
    entry.entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.type = "podcast-removal";
    entry.title = episode.value("deviceTitle", episode.value("title")).toString();
    if (entry.title.isEmpty()) entry.title = "Podcast episode";
    entry.artist = episode.value("deviceAuthor", episode.value("artist")).toString();
    entry.album = episode.value("album", episode.value("showTitle")).toString();
    entry.videoPosterPath = episode.value("posterPath").toString();
    entry.status = "pending-removal";
    entry.statusNote = "Will remove on sync";
    entry.podcast = episode;
    const auto id = entry.entryId;
    insertGrouped(std::move(entry));
    return id;
}

void SyncQueueModel::clearPodcastRemovals() {
    bool removed = false;
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries[i].type != "podcast-removal") continue;
        beginRemoveRows({}, i, i);
        m_entries.removeAt(i);
        endRemoveRows();
        removed = true;
    }
    if (removed) { emit countChanged(); emit estimateChanged(); }
}
