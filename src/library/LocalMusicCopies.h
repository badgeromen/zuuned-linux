#pragma once

#include "LibraryTypes.h"
#include "MusicIdentity.h"

// Audio establishes copy identity across the whole library. Metadata only
// preserves distinct album memberships and positions within an album.
namespace LocalMusicCopies {
struct Context {
    QString album;
    int disc = 0, track = 0;
    friend bool operator==(const Context &, const Context &) = default;
    friend size_t qHash(const Context &value, size_t seed = 0) noexcept {
        return qHashMulti(seed, value.album, value.disc, value.track);
    }
};
inline Context context(const LibTrack &t) {
    return {MusicIdentity::knownAlbum(t.album), qMax(0, t.discNumber), qMax(0, t.trackNumber)};
}
inline bool compatible(const Context &a, const Context &b) {
    return (a.album.isEmpty() || b.album.isEmpty() || a.album == b.album)
        && (!a.disc || !b.disc || a.disc == b.disc)
        && (!a.track || !b.track || a.track == b.track);
}
inline void combine(Context &a, const Context &b) {
    if (a.album.isEmpty()) a.album = b.album;
    if (!a.disc) a.disc = b.disc;
    if (!a.track) a.track = b.track;
}
inline bool preferSource(const LibTrack &candidate, const LibTrack &current) {
    const auto quality = [](const LibTrack &t) {
        return (!MusicIdentity::knownAlbum(t.album).isEmpty() ? 8 : 0)
            + (t.trackNumber > 0 ? 4 : 0) + (t.discNumber > 0 ? 2 : 0)
            + (!MusicIdentity::knownArtist(t.artist).isEmpty() ? 1 : 0);
    };
    if (quality(candidate) != quality(current)) return quality(candidate) > quality(current);
    // No filesystem I/O on the GUI thread. Prefer a normal path over a GVFS
    // mirror; otherwise preserve the oldest imported ID for stable selections.
    const bool candidateGvfs = candidate.filepath.contains(QStringLiteral("/gvfs/"));
    const bool currentGvfs = current.filepath.contains(QStringLiteral("/gvfs/"));
    if (candidateGvfs != currentGvfs) return !candidateGvfs;
    return candidate.id < current.id;
}

struct GroupedTracks {
    QVector<LibTrack> rows;
    QHash<qint64, qint64> representatives;
};
inline GroupedTracks groupRows(const QVector<LibTrack> &rows) {
    GroupedTracks result;
    for (const auto &row : rows) result.representatives.insert(row.id, row.id);
    QHash<QString, QVector<qsizetype>> buckets;
    for (qsizetype i = 0; i < rows.size(); ++i)
        if (!rows[i].audioFingerprint.isEmpty()) buckets[rows[i].audioFingerprint].append(i);
    QSet<qsizetype> hidden;
    for (const auto &indices : buckets) {
        if (indices.size() < 2) continue;
        QVector<Context> contexts;
        QHash<Context, qsizetype> contextIds;
        QVector<qsizetype> rowContexts;
        for (auto index : indices) {
            const auto value = context(rows[index]);
            if (!contextIds.contains(value)) {
                contextIds.insert(value, contexts.size());
                contexts.append(value);
            }
            rowContexts.append(contextIds.value(value));
        }
        QVector<bool> ambiguous(contexts.size(), false);
        // Examine all peers before grouping. An untagged copy must never
        // bridge standard/deluxe editions or competing disc/track positions.
        for (qsizetype i = 0; i < contexts.size(); ++i) {
            Context evidence;
            for (const auto &peer : contexts) {
                if (!compatible(contexts[i], peer)) continue;
                if (!compatible(evidence, peer)) { ambiguous[i] = true; break; }
                combine(evidence, peer);
            }
        }
        struct Group { qsizetype first, representativeIndex; LibTrack representative; Context evidence; bool ambiguous; QVector<qint64> ids; };
        QVector<Group> groups;
        for (qsizetype i = 0; i < indices.size(); ++i) {
            const auto index = indices[i];
            const auto c = rowContexts[i];
            bool joined = false;
            for (auto &group : groups) {
                if (ambiguous[c] != group.ambiguous) continue;
                if (ambiguous[c] ? contexts[c] != group.evidence : !compatible(contexts[c], group.evidence)) continue;
                hidden.insert(index);
                group.ids.append(rows[index].id);
                combine(group.evidence, contexts[c]);
                if (preferSource(rows[index], group.representative)) {
                    group.representative = rows[index];
                    group.representativeIndex = index;
                }
                joined = true;
                break;
            }
            if (!joined) groups.append({index, index, rows[index], contexts[c], ambiguous[c], {rows[index].id}});
        }
        for (const auto &group : groups) {
            // Keep the representative's actual input position. Replacing an
            // earlier unnumbered row in-place would disturb album track order.
            hidden.insert(group.first);
            hidden.remove(group.representativeIndex);
            for (auto id : group.ids) result.representatives.insert(id, group.representative.id);
        }
    }
    auto &out = result.rows;
    out.reserve(rows.size() - hidden.size());
    for (qsizetype i = 0; i < rows.size(); ++i)
        if (!hidden.contains(i)) out.append(rows[i]);
    return result;
}
inline QVector<LibTrack> displayRows(const QVector<LibTrack> &rows) {
    return groupRows(rows).rows;
}
}
