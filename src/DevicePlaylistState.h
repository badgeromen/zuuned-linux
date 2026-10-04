#pragma once

#include <QSet>
#include <QVariantList>
#include <QVariantMap>

// Apply only confirmed DeleteObject successes to the browse snapshot. This
// never changes the device; failed deletions and unrelated members stay put.
inline void pruneDevicePlaylists(QVariantList &playlists, const QSet<quint32> &purged) {
    for (qsizetype i = playlists.size(); i-- > 0;) {
        auto playlist = playlists[i].toMap();
        if (purged.contains(playlist.value("itemId").toUInt())) {
            playlists.removeAt(i);
            continue;
        }
        auto members = playlist.value("trackIds").toList();
        const qsizetype before = members.size();
        for (qsizetype j = members.size(); j-- > 0;)
            if (purged.contains(members[j].toUInt())) members.removeAt(j);
        if (members.size() == before) continue;
        playlist.insert("trackIds", members);
        playlist.insert("count", members.size());
        playlists[i] = playlist;
    }
}
