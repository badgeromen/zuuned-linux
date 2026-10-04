#pragma once

#include <QString>
#include <QVector>

// Row types mirroring the mac's LibraryService schema.

struct LibTrack {
    qint64 id = -1;
    QString filepath;
    QString title;
    QString artist;
    QString albumartist;   // canonical album grouping: COALESCE(NULLIF(albumartist,''), artist)
    QString album;
    QString genre;
    int durationMs = 0;
    qint64 filesize = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    qint64 mtime = 0;
    qint64 probeMtimeNs = 0;
    qint64 probeCtimeNs = 0;
    int probeVersion = 0;
    QString audioFingerprint; // Verified full encoded audio payload, tags excluded.
    bool userEdited = false;
    int lastPositionMs = 0;
};

struct TrackProbeState {
    qint64 id = -1;
    qint64 filesize = 0;
    qint64 mtimeNs = 0;
    qint64 ctimeNs = 0;
    int version = 0;
};

// LibraryVideo.swift — one video file row. season 0 = unknown/special,
// episode 0 = movie. tmdbId holds the SHOW id for TV (episodeTmdbId is
// the episode's own).
struct LibVideo {
    qint64 id = -1;
    QString filepath;
    QString filename;
    qint64 filesize = 0;
    qint64 mtime = 0;
    int durationMs = 0;
    int width = 0;
    int height = 0;
    QString description;
    QString category;       // "tv", "movie", "music_video", "other", ""
    QString series;
    int season = 0;
    int episode = 0;
    QString episodeTitle;
    int tmdbId = 0;
    QString tmdbTitle;
    QString tmdbPoster;     // local cached path ("" if none)
    bool customPoster = false; // art pin independent of identity/userEdited
    QString tmdbCast;
    QString tmdbDirector;
    double tmdbRating = 0;
    QString tmdbGenres;
    QString tmdbYear;
    bool tmdbCached = false;
    int lastPositionMs = 0;
    qint64 lastPlayedAt = 0;
    bool watched = false;
    QString episodeStill;
    QString episodeAirDate;
    double episodeRating = 0;
    int episodeRuntime = 0;
    int episodeTmdbId = 0;
    bool userEdited = false;
    int lookupAttempts = 0;
    qint64 lastLookupAt = 0;
    qint64 probedAt = 0;

    bool isTV() const {
        return category == QLatin1String("tv")
            || (episode > 0 && !series.isEmpty());
    }
    bool isMovie() const {
        return category == QLatin1String("movie")
            || (!isTV() && category != QLatin1String("music_video"));
    }
};

struct LibPhoto {
    qint64 id = -1;
    QString filepath;
    QString filename;
    qint64 filesize = 0;
    qint64 mtime = 0;
};

struct WatchFolder {
    qint64 id = -1;
    QString path;
    QString type;   // music | movies | tv | anime | photos | all
};
