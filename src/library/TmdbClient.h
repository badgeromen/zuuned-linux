#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;

// Blocking TMDB client — TMDBService.swift port. Designed for WORKER
// threads (the matcher's QThread): each call runs a request through a
// thread-local QEventLoop and returns the decoded result. Never call
// from the UI thread.
//
// Throttle: 100ms between API requests (TMDB allows ~50/s; 10/s is
// polite). Image CDN downloads are NOT throttled.
//
// API key: Settings "tmdbApiKey" override, else the bundled key (same
// key the mac app ships).

struct TmdbResult {
    int id = 0;
    QString title;
    QString year;
    QString posterPath;  // TMDB path like "/abc.jpg", "" if none
    QString overview;
    double popularity = 0;
    int voteCount = 0;
};

struct TmdbEpisode {
    int episodeNumber = 0;
    QString name;
    QString overview;
    QString airDate;
    int tmdbId = 0;        // the episode's own id
    QString stillPath;     // "" when none
    double rating = 0;
    int runtime = 0;       // minutes
};

struct TmdbDetails {
    int id = 0;
    QString title;
    QString year;
    QString overview;
    QString posterPath;
    QString cast;          // top 5, comma-separated
    QString director;      // movie director / TV creators
    double rating = 0;
    QString genres;        // up to 5, comma-separated
    // TV only: season number → episode count (absolute-numbered anime
    // mapping). Empty for movies.
    QHash<int, int> seasonEpisodeCounts;
};

struct TmdbPoster {
    QString filePath;
    QString language;
    int width = 0;
    int height = 0;
    double voteAverage = 0;
};

class TmdbClient {
public:
    TmdbClient();
    ~TmdbClient();

    // ok=false means transient failure (network/HTTP) — callers must NOT
    // treat it as "no results".
    QVector<TmdbResult> searchMovie(const QString &query, bool *ok = nullptr);
    QVector<TmdbResult> searchTV(const QString &query, bool *ok = nullptr);
    bool movieDetails(int tmdbId, TmdbDetails *out);
    bool tvDetails(int tmdbId, TmdbDetails *out);
    QVector<TmdbEpisode> seasonEpisodes(int showId, int season, bool *ok = nullptr);
    // Fanart.tv needs TVDB ids for TV; 0 when absent. Cached per client.
    int tvdbIdFor(int tmdbId);
    QVector<TmdbPoster> moviePosters(int tmdbId, bool *ok = nullptr);
    QVector<TmdbPoster> tvPosters(int tmdbId, bool *ok = nullptr);

    // Image CDN (unthrottled). w500 posters / w300 stills — the sizes the
    // mac ships.
    bool downloadPoster(const QString &posterPath, const QString &destPath);
    bool downloadStill(const QString &stillPath, const QString &destPath);
    // Absolute-URL download (Fanart posters ride through here too).
    bool downloadUrl(const QString &url, const QString &destPath);

private:
    QByteArray fetch(const QString &url, bool *ok, bool throttled = true,
                     const QByteArray &apiKeyHeader = QByteArray());
    QString apiKey() const;

    QNetworkAccessManager *m_nam = nullptr;
    QElapsedTimer m_lastRequest;

    QHash<int, int> m_tvdbCache;

    friend class FanartClient;
};

// Blocking Fanart.tv client — FanartTVService.swift port. Shares the
// TmdbClient's network manager and thread rules.
class FanartClient {
public:
    explicit FanartClient(TmdbClient *tmdb) : m_tmdb(tmdb) {}

    // Best-first poster URLs (English preferred, then by likes).
    QStringList moviePosterUrls(int tmdbId, bool *ok = nullptr);
    QStringList tvPosterUrls(int tvdbId, bool *ok = nullptr);

private:
    QStringList posterUrls(const QString &path, const char *jsonKey, bool *ok);
    TmdbClient *m_tmdb;
};
