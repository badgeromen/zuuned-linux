#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <optional>

// Groups the synchronous burst of saveTrackToLibrary calls into one completed
// import. Device extraction remains serial, so the count reaches zero only
// after every track queued by an album/artist action has reported its result.
class TrackImportBatch {
public:
    struct Album {
        quint32 itemId = 0;
        QString artist;
        QString title;
    };
    struct Result {
        bool hasSuccessfulTrack = false;
        QStringList artists;
        QList<Album> albums;
    };

    void started(quint32 itemId, const QString &artist,
                 const QString &album = QString(),
                 const QString &albumArtist = QString()) {
        const QString trimmed = artist.trimmed();
        const QString trimmedAlbum = album.trimmed();
        const QString owner = albumArtist.trimmed().isEmpty()
            ? trimmed : albumArtist.trimmed();
        m_items[itemId].append({trimmed, trimmedAlbum, owner});
        if (!trimmed.isEmpty()
            && trimmed.compare(QStringLiteral("Unknown Artist"),
                               Qt::CaseInsensitive) != 0) {
            const QString key = trimmed.toCaseFolded();
            if (!m_displayArtists.contains(key))
                m_displayArtists.insert(key, trimmed);
        }
        ++m_pending;
    }

    std::optional<Result> finished(quint32 itemId, bool ok) {
        auto item = m_items.find(itemId);
        if (item == m_items.end() || item->isEmpty())
            return std::nullopt;

        const Entry entry = item->takeFirst();
        if (item->isEmpty())
            m_items.erase(item);
        --m_pending;

        if (ok) {
            ++m_successful;
            if (!entry.artist.isEmpty()
                && entry.artist.compare(QStringLiteral("Unknown Artist"),
                                  Qt::CaseInsensitive) != 0) {
                const QString key = entry.artist.toCaseFolded();
                if (!m_successfulArtists.contains(key))
                    m_successfulArtists.insert(key,
                                               m_displayArtists.value(key, entry.artist));
            }
            if (!entry.album.isEmpty()) {
                const QString key = entry.albumArtist.toCaseFolded()
                    + QLatin1Char('\n') + entry.album.toCaseFolded();
                if (!m_successfulAlbums.contains(key))
                    m_successfulAlbums.insert(key,
                        Album{itemId, entry.albumArtist, entry.album});
            }
        }

        if (m_pending > 0)
            return std::nullopt;

        Result result;
        result.hasSuccessfulTrack = m_successful > 0;
        result.artists = m_successfulArtists.values();
        result.artists.sort(Qt::CaseInsensitive);
        result.albums = m_successfulAlbums.values();
        std::sort(result.albums.begin(), result.albums.end(),
                  [](const Album &a, const Album &b) {
            const int artist = a.artist.compare(b.artist, Qt::CaseInsensitive);
            return artist != 0 ? artist < 0
                               : a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        });
        m_successful = 0;
        m_successfulArtists.clear();
        m_successfulAlbums.clear();
        m_displayArtists.clear();
        return result;
    }

    int pending() const { return m_pending; }

private:
    struct Entry {
        QString artist;
        QString album;
        QString albumArtist;
    };
    int m_pending = 0;
    int m_successful = 0;
    QHash<quint32, QList<Entry>> m_items;
    QHash<QString, QString> m_displayArtists;
    QHash<QString, QString> m_successfulArtists;
    QHash<QString, Album> m_successfulAlbums;
};
