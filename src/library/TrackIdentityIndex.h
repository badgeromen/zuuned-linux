#pragma once

#include "LibraryTypes.h"
#include "MusicIdentity.h"

// Rebuilt once per library refresh. Identical format copies share membership;
// genuinely distinct disc/track identities remain separate candidates.
class TrackIdentityIndex {
public:
    bool replaceRows(const QVector<LibTrack> &rows) {
        QHash<MusicIdentity::Track,qint64> representatives;
        for (const LibTrack &row : rows) {
            for (const auto &artist : {row.artist,row.albumartist}) {
                const auto track = MusicIdentity::normalized({artist,row.album,row.title,row.discNumber,row.trackNumber});
                if (track.artist.isEmpty() || track.album.isEmpty() || track.title.isEmpty()) continue;
                if (!representatives.contains(track) || row.id < representatives.value(track)) representatives.insert(track,row.id);
            }
        }
        QSet<MusicIdentity::Track> identities;
        for (auto it=representatives.cbegin();it!=representatives.cend();++it) identities.insert(it.key());
        const bool changed=identities!=m_identities;
        m_identities=std::move(identities);
        if (representatives!=m_representatives) {
            m_representatives=std::move(representatives);
            m_index.clear();
            for (auto it=m_representatives.cbegin();it!=m_representatives.cend();++it) m_index.add(it.key(),it.value());
        }
        return changed;
    }
    MusicIdentity::Match resolve(const MusicIdentity::Track &track, const QVector<MusicIdentity::Track> &peers={}) const {
        return m_index.resolve(track,peers);
    }
    bool contains(const QString &artist, const QString &album, const QString &title,
                  int discNumber = 0, int trackNumber = 0) const {
        return resolve({artist,album,title,discNumber,trackNumber}).matched();
    }
    QVariantList peers(const QVariantMap &row) const {
        QVariantList out;
        for (const auto &track : m_index.identitiesFor(MusicIdentity::fromMap(row))) out.append(MusicIdentity::toMap(track));
        return out;
    }
private:
    QSet<MusicIdentity::Track> m_identities;
    QHash<MusicIdentity::Track,qint64> m_representatives;
    MusicIdentity::Index m_index;
};
