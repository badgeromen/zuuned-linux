#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

// Metadata identity is not a recording fingerprint. Unknown disc/track values
// stay unknown; a legacy match is usable only when both sides are unambiguous.
namespace MusicIdentity {
inline QString normalize(const QString &value) { return value.trimmed().toCaseFolded(); }
inline QString knownArtist(const QString &value) {
    const auto text = normalize(value);
    return text == QLatin1String("unknown artist") ? QString() : text;
}
inline QString knownAlbum(const QString &value) {
    const auto text = normalize(value);
    return text == QLatin1String("unknown album") ? QString() : text;
}
struct Track {
    QString artist, album, title;
    int discNumber = 0, trackNumber = 0;
    friend bool operator==(const Track &, const Track &) = default;
    friend size_t qHash(const Track &t, size_t seed = 0) noexcept {
        return qHashMulti(seed, t.artist, t.album, t.title, t.discNumber, t.trackNumber);
    }
};
inline Track normalized(Track value) {
    value.artist = knownArtist(value.artist);
    value.album = knownAlbum(value.album);
    value.title = normalize(value.title);
    value.discNumber = qMax(0, value.discNumber);
    value.trackNumber = qMax(0, value.trackNumber);
    return value;
}
inline Track fromMap(const QVariantMap &row) {
    QString artist = row.value(QStringLiteral("albumartist")).toString();
    if (knownArtist(artist).isEmpty()) artist = row.value(QStringLiteral("artist")).toString();
    return normalized({artist, row.value(QStringLiteral("album")).toString(),
        row.value(QStringLiteral("title")).toString(),
        row.value(QStringLiteral("discNumberReliable"), true).toBool() ? row.value(QStringLiteral("discNumber")).toInt() : 0,
        row.value(QStringLiteral("trackNumberReliable"), true).toBool() ? row.value(QStringLiteral("trackNumber")).toInt() : 0});
}
inline QVariantMap toMap(const Track &track) {
    return {{QStringLiteral("artist"),track.artist}, {QStringLiteral("album"),track.album},
        {QStringLiteral("title"),track.title}, {QStringLiteral("discNumber"),track.discNumber},
        {QStringLiteral("trackNumber"),track.trackNumber}};
}
inline bool compatibleNumbers(const Track &a, const Track &b) {
    return !(a.discNumber > 0 && b.discNumber > 0 && a.discNumber != b.discNumber)
        && !(a.trackNumber > 0 && b.trackNumber > 0 && a.trackNumber != b.trackNumber);
}
enum class MatchKind { Exact, UniqueLegacy, Ambiguous, Missing };
struct Match {
    MatchKind kind = MatchKind::Missing;
    qint64 itemId = 0;
    bool matched() const { return kind == MatchKind::Exact || kind == MatchKind::UniqueLegacy; }
};
class Index {
    struct Key {
        QString artist, album, title;
        friend bool operator==(const Key &, const Key &) = default;
        friend size_t qHash(const Key &key, size_t seed = 0) noexcept {
            return qHashMulti(seed, key.artist, key.album, key.title);
        }
    };
    struct Candidate { Track track; qint64 id; };
    QHash<Key,QVector<Candidate>> m_full;
    QHash<Key,QVector<Candidate>> m_short;
    qsizetype m_count = 0;
    QVector<Candidate> candidates(const Track &track, bool fallback) const {
        auto out = m_full.value({track.artist,track.album,track.title});
        if (!fallback || (!out.isEmpty() && !track.artist.isEmpty())) return out;
        out.clear();
        // A fallback may fill an absent artist, never contradict a known one.
        for (const auto &candidate : m_short.value({{},track.album,track.title}))
            if (track.artist.isEmpty() || candidate.track.artist.isEmpty()) out.append(candidate);
        return out;
    }
public:
    void clear() { m_full.clear(); m_short.clear(); m_count = 0; }
    qsizetype size() const { return m_count; }
    void add(const Track &raw, qint64 itemId) {
        const auto track = normalized(raw);
        if (itemId <= 0 || track.album.isEmpty() || track.title.isEmpty()) return;
        auto &bucket = m_full[{track.artist,track.album,track.title}];
        for (const auto &old : bucket) if (old.id == itemId && old.track == track) return;
        bucket.append({track,itemId});
        m_short[{{},track.album,track.title}].append({track,itemId});
        ++m_count;
    }
    QVector<Track> identitiesFor(const Track &raw, bool artistFallback = false) const {
        const auto track = normalized(raw);
        QVector<Track> out;
        for (const auto &candidate : candidates(track, artistFallback)) out.append(candidate.track);
        return out;
    }
    Match resolve(const Track &raw, const QVector<Track> &sourcePeers = {}, bool allowArtistFallback = false) const {
        const auto query = normalized(raw);
        if (query.album.isEmpty() || query.title.isEmpty() || (query.artist.isEmpty() && !allowArtistFallback)) return {};
        const auto bucket = candidates(query, allowArtistFallback);
        QVector<Candidate> compatible;
        QSet<qint64> exactIds;
        for (const auto &candidate : bucket) {
            if (!compatibleNumbers(query,candidate.track)) continue;
            compatible.append(candidate);
            if (!query.artist.isEmpty() && query.artist == candidate.track.artist
                && query.discNumber > 0 && query.trackNumber > 0
                && query.discNumber == candidate.track.discNumber
                && query.trackNumber == candidate.track.trackNumber) exactIds.insert(candidate.id);
        }
        if (exactIds.size() == 1) return {MatchKind::Exact,*exactIds.cbegin()};
        if (exactIds.size() > 1) return {MatchKind::Ambiguous,0};
        if (compatible.isEmpty()) return {};
        QSet<qint64> ids;
        for (const auto &candidate : compatible) ids.insert(candidate.id);
        if (ids.size() != 1) return {MatchKind::Ambiguous,0};
        for (const auto &peerRaw : sourcePeers) {
            const auto peer = normalized(peerRaw);
            if (peer.album != query.album || peer.title != query.title || peer.artist != query.artist) continue;
            if (peer.discNumber == query.discNumber && peer.trackNumber == query.trackNumber) continue;
            for (const auto &candidate : compatible)
                if (compatibleNumbers(peer,candidate.track)) return {MatchKind::Ambiguous,0};
        }
        return {MatchKind::UniqueLegacy,*ids.cbegin()};
    }
};
}
