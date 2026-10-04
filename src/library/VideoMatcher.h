#pragma once

#include "LibraryTypes.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

struct sqlite3;
class TmdbClient;
class FanartClient;

// Off-UI-thread TMDB matcher — VideoMatcher.swift port. Lives on its own
// QThread (queued invokes only). Owns its own SQLite connection (WAL
// side-connection) and a cross-batch series memo, so a 200-episode show
// discovered across many scan chunks resolves with ONE TMDB search.
//
// FOLDER-TRUTH: the matcher finds the show on TMDB (posters, episode
// titles, stills) but never renames the series — the user's directory
// name IS the identity; TMDB's title lives in tmdb_title for display.
//
// All SQL commits on this thread; `updatesCommitted` tells the facade to
// reload the videos table.
class VideoMatcher : public QObject {
    Q_OBJECT

public:
    explicit VideoMatcher(const QString &dbPath, QObject *parent = nullptr);
    ~VideoMatcher() override;

    // Walk season episode counts mapping an absolute episode number
    // ("One Piece - 1043" parses as S1E1043) to (season, episodeInSeason).
    // Returns false when the number exceeds every season.
    static bool mapAbsoluteEpisode(int absolute, const QHash<int, int> &counts,
                                   int *seasonOut, int *episodeOut);

public slots:
    // Match a batch of unmatched videos. posterSource: "tmdb" | "fanart".
    void matchBatch(const QVector<LibVideo> &videos, const QString &posterSource);

signals:
    // count = rows written this round (match, no-match, or attempt).
    void updatesCommitted(int count);
    // The whole batch is done — the consumer may dispatch the next one.
    void matchBatchFinished();

private:
    friend struct VideoMatcherCustomizationTest;
    struct SeriesResolution {
        bool failed = true;
        // resolved show meta
        int tmdbId = 0;
        QString title;
        QString year;
        QString overview;
        QString cast;
        QString director;
        double rating = 0;
        QString genres;
        QString posterLocalPath;
        QHash<int, int> seasonEpisodeCounts;
    };

    void matchSeries(const QString &key, QVector<LibVideo> episodes,
                     const QString &posterSource, int *written);
    void matchMovie(const LibVideo &video, const QString &posterSource,
                    int *written);
    SeriesResolution resolveSeries(const QString &seriesName,
                                   const QString &posterSource, bool *transient);
    QVector<struct TmdbEpisode> seasonEpisodes(int showId, int season);
    QString downloadStill(const struct TmdbEpisode &ep, int showId, int season);

    // SQL (own side-connection, opened lazily on this thread)
    sqlite3 *connection();
    void writeTvRow(const LibVideo &v, const SeriesResolution &show,
                    const struct TmdbEpisode *ep, const QString &still,
                    int fixedSeason, int fixedEpisode);
    void writeMovieRow(qint64 id, const SeriesResolution &meta);
    void writeNoMatch(qint64 id);
    void writeAttempt(qint64 id);

    QString m_dbPath;
    sqlite3 *m_db = nullptr;
    TmdbClient *m_tmdb = nullptr;
    FanartClient *m_fanart = nullptr;

    QString m_thumbsDir;   // vidthumbs cache (posters)
    QString m_stillsDir;   // episode stills

    // normalized-lowercased series key → resolution (process lifetime)
    QHash<QString, SeriesResolution> m_seriesMemo;
    // "showId#season" → episodes
    QHash<QString, QVector<struct TmdbEpisode>> m_seasonMemo;
};
