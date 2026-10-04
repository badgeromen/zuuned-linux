#include "VideoMatcher.h"

#include "TmdbClient.h"
#include "VideoNaming.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <climits>
#include <sqlite3.h>

namespace {

// Pick the best-scoring candidate; -1 when none clears the bar.
int bestCandidate(const QString &query, const QVector<TmdbResult> &results) {
    static const QRegularExpression trailingYear(
        QStringLiteral("\\b(19\\d{2}|20\\d{2})\\b\\s*$"));
    QString year;
    const auto m = trailingYear.match(query);
    if (m.hasMatch())
        year = m.captured(1);

    int bestIdx = -1;
    double bestScore = 0;
    for (int i = 0; i < results.size(); i++) {
        const double s = VideoNaming::scoreCandidate(
            query, year, results[i].title, results[i].year, results[i].popularity);
        if (s > bestScore) {
            bestScore = s;
            bestIdx = i;
        }
    }
    return bestScore >= VideoNaming::kAcceptThreshold ? bestIdx : -1;
}

void bindQ(sqlite3_stmt *stmt, int idx, const QString &s) {
    const QByteArray utf8 = s.toUtf8();
    sqlite3_bind_text(stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

} // namespace

VideoMatcher::VideoMatcher(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath) {
    const QString cacheRoot =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_thumbsDir = cacheRoot + QStringLiteral("/vidthumbs");
    m_stillsDir = cacheRoot + QStringLiteral("/stills");
    QDir().mkpath(m_thumbsDir);
    QDir().mkpath(m_stillsDir);
}

VideoMatcher::~VideoMatcher() {
    if (m_db)
        sqlite3_close(m_db);
    delete m_fanart;
    delete m_tmdb;
}

sqlite3 *VideoMatcher::connection() {
    if (m_db)
        return m_db;
    if (sqlite3_open(m_dbPath.toUtf8().constData(), &m_db) != SQLITE_OK) {
        m_db = nullptr;
        return nullptr;
    }
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(m_db, 5000);
    return m_db;
}

bool VideoMatcher::mapAbsoluteEpisode(int absolute, const QHash<int, int> &counts,
                                      int *seasonOut, int *episodeOut) {
    QList<int> seasons = counts.keys();
    std::sort(seasons.begin(), seasons.end());
    int remaining = absolute;
    for (int season : seasons) {
        if (season <= 0)
            continue;
        const int count = counts.value(season);
        if (count <= 0)
            continue;
        if (remaining <= count) {
            *seasonOut = season;
            *episodeOut = remaining;
            return true;
        }
        remaining -= count;
    }
    return false;
}

void VideoMatcher::matchBatch(const QVector<LibVideo> &videos,
                              const QString &posterSource) {
    if (!m_tmdb) {
        // Created lazily ON THIS THREAD so QNetworkAccessManager gets the
        // right thread affinity.
        m_tmdb = new TmdbClient();
        m_fanart = new FanartClient(m_tmdb);
    }

    int written = 0;

    // Route: TV when the row says so or a series was parsed; movie otherwise.
    QHash<QString, QVector<LibVideo>> tvGroups;
    QVector<LibVideo> movies;
    for (LibVideo v : videos) {
        if (v.category == QLatin1String("tv") || !v.series.isEmpty()) {
            QString series = v.series;
            if (series.isEmpty()) {
                series = VideoNaming::deriveSeriesFromPath(v.filepath);
                if (series.isEmpty()) {
                    writeNoMatch(v.id);
                    written++;
                    continue;
                }
                v.series = series;
            }
            tvGroups[series.toLower()].append(v);
        } else {
            movies.append(v);
        }
    }

    // Commit each series as soon as it resolves — a big show's rows land
    // in the DB without waiting for the rest of the batch.
    for (auto it = tvGroups.begin(); it != tvGroups.end(); ++it) {
        matchSeries(it.key(), it.value(), posterSource, &written);
        emit updatesCommitted(written);
        written = 0;
    }

    for (const LibVideo &movie : movies)
        matchMovie(movie, posterSource, &written);
    if (written > 0)
        emit updatesCommitted(written);
    emit matchBatchFinished();
}

// ── TV ──

void VideoMatcher::matchSeries(const QString &key, QVector<LibVideo> episodes,
                               const QString &posterSource, int *written) {
    const QString seriesName = episodes.isEmpty() ? QString() : episodes.first().series;

    SeriesResolution res;
    const auto memo = m_seriesMemo.constFind(key);
    if (memo != m_seriesMemo.constEnd()) {
        res = memo.value();
    } else {
        bool transient = false;
        res = resolveSeries(seriesName, posterSource, &transient);
        if (transient) {
            // Network/API failure — do NOT memoize, do NOT mark cached.
            fprintf(stderr, "[matcher] TV lookup error for %s (transient)\n",
                    qPrintable(seriesName));
            sqlite3_exec(connection(), "BEGIN;", nullptr, nullptr, nullptr);
            for (const LibVideo &ep : episodes) {
                writeAttempt(ep.id);
                (*written)++;
            }
            sqlite3_exec(connection(), "COMMIT;", nullptr, nullptr, nullptr);
            return;
        }
        m_seriesMemo.insert(key, res);
    }

    if (res.failed) {
        sqlite3_exec(connection(), "BEGIN;", nullptr, nullptr, nullptr);
        for (const LibVideo &ep : episodes) {
            writeNoMatch(ep.id);
            (*written)++;
        }
        sqlite3_exec(connection(), "COMMIT;", nullptr, nullptr, nullptr);
        return;
    }

    // Episode map for the seasons present in this batch (season 0 has no
    // canonical TMDB numbering for local files — show-level only).
    QHash<QString, TmdbEpisode> episodeMap;
    QSet<int> seasons;
    for (const LibVideo &ep : episodes)
        if (ep.season > 0)
            seasons.insert(ep.season);
    for (int seasonNumber : seasons)
        for (const TmdbEpisode &ep : seasonEpisodes(res.tmdbId, seasonNumber))
            episodeMap.insert(QStringLiteral("S%1E%2")
                                  .arg(seasonNumber, 2, 10, QLatin1Char('0'))
                                  .arg(ep.episodeNumber, 3, 10, QLatin1Char('0')),
                              ep);

    struct Resolved {
        LibVideo ep;
        int season = 0;
        int episode = 0;
        bool hasTmdbEp = false;
        TmdbEpisode tmdbEp;
    };
    QVector<Resolved> resolved;
    for (const LibVideo &ep : episodes) {
        Resolved r;
        r.ep = ep;
        r.season = ep.season;
        r.episode = ep.episode;
        if (ep.season > 0 || ep.episode > 0) {
            const auto it = episodeMap.constFind(
                QStringLiteral("S%1E%2")
                    .arg(ep.season, 2, 10, QLatin1Char('0'))
                    .arg(ep.episode, 3, 10, QLatin1Char('0')));
            if (it != episodeMap.constEnd()) {
                r.hasTmdbEp = true;
                r.tmdbEp = it.value();
            }
        }

        // Absolute-numbered anime: "One Piece - 1043" parses as S1E1043.
        // When the number exceeds season 1's count, walk the counts.
        if (!r.hasTmdbEp && ep.season <= 1 && !ep.userEdited
            && ep.episode > res.seasonEpisodeCounts.value(1, INT_MAX)) {
            int realSeason = 0, realEpisode = 0;
            if (mapAbsoluteEpisode(ep.episode, res.seasonEpisodeCounts,
                                   &realSeason, &realEpisode)) {
                for (const TmdbEpisode &cand : seasonEpisodes(res.tmdbId, realSeason)) {
                    if (cand.episodeNumber == realEpisode) {
                        r.season = realSeason;
                        r.episode = realEpisode;
                        r.hasTmdbEp = true;
                        r.tmdbEp = cand;
                        break;
                    }
                }
            }
        }
        resolved.append(r);
    }

    // Stills (image CDN, unthrottled) then one write transaction.
    sqlite3_exec(connection(), "BEGIN;", nullptr, nullptr, nullptr);
    for (const Resolved &r : resolved) {
        QString still;
        if (r.hasTmdbEp && !r.tmdbEp.stillPath.isEmpty())
            still = downloadStill(r.tmdbEp, res.tmdbId, r.season);

        const bool changed = (r.season != r.ep.season || r.episode != r.ep.episode);
        writeTvRow(r.ep, res, r.hasTmdbEp ? &r.tmdbEp : nullptr, still,
                   changed ? r.season : -1, changed ? r.episode : -1);
        (*written)++;
    }
    sqlite3_exec(connection(), "COMMIT;", nullptr, nullptr, nullptr);

    fprintf(stderr, "[matcher] TV: %s (%s) — %d episodes committed\n",
            qPrintable(res.title), qPrintable(res.year), int(resolved.size()));
}

VideoMatcher::SeriesResolution VideoMatcher::resolveSeries(
    const QString &seriesName, const QString &posterSource, bool *transient) {
    *transient = false;
    SeriesResolution out;

    const QString query = VideoNaming::cleanSeriesNameForSearch(seriesName);
    bool ok = false;
    const QVector<TmdbResult> results = m_tmdb->searchTV(query, &ok);
    if (!ok) {
        *transient = true;
        return out;
    }

    const int best = bestCandidate(query, results);
    if (best < 0) {
        fprintf(stderr, "[matcher] no acceptable TV match for: %s (%d candidates)\n",
                qPrintable(query), int(results.size()));
        return out; // failed (permanent) — Needs Match
    }

    TmdbDetails details;
    if (!m_tmdb->tvDetails(results[best].id, &details)) {
        *transient = true;
        return out;
    }

    out.failed = false;
    out.tmdbId = details.id;
    out.title = details.title;
    out.year = details.year;
    out.overview = details.overview;
    out.cast = details.cast;
    out.director = details.director;
    out.rating = details.rating;
    out.genres = details.genres;
    out.seasonEpisodeCounts = details.seasonEpisodeCounts;

    // Poster — Fanart.tv first when chosen, TMDB fallback.
    const QString destPath =
        m_thumbsDir + QStringLiteral("/tv_%1.jpg").arg(details.id);
    if (posterSource == QLatin1String("fanart")) {
        const int tvdbId = m_tmdb->tvdbIdFor(details.id);
        if (tvdbId > 0) {
            const QStringList urls = m_fanart->tvPosterUrls(tvdbId);
            if (!urls.isEmpty() && m_tmdb->downloadUrl(urls.first(), destPath))
                out.posterLocalPath = destPath;
        }
    }
    if (out.posterLocalPath.isEmpty() && !details.posterPath.isEmpty()
        && m_tmdb->downloadPoster(details.posterPath, destPath))
        out.posterLocalPath = destPath;

    return out;
}

QVector<TmdbEpisode> VideoMatcher::seasonEpisodes(int showId, int season) {
    const QString key = QStringLiteral("%1#%2").arg(showId).arg(season);
    const auto it = m_seasonMemo.constFind(key);
    if (it != m_seasonMemo.constEnd())
        return it.value();
    bool ok = false;
    const QVector<TmdbEpisode> eps = m_tmdb->seasonEpisodes(showId, season, &ok);
    if (ok)
        m_seasonMemo.insert(key, eps); // don't memoize failures
    return eps;
}

QString VideoMatcher::downloadStill(const TmdbEpisode &ep, int showId, int season) {
    // Keyed on (show, season, episode) — a rematch to a different show
    // naturally produces a new file; no invalidation needed.
    const QString dest = m_stillsDir
        + QStringLiteral("/ep_%1_s%2e%3.jpg")
              .arg(showId)
              .arg(season, 2, 10, QLatin1Char('0'))
              .arg(ep.episodeNumber, 3, 10, QLatin1Char('0'));
    if (QFile::exists(dest))
        return dest;
    return m_tmdb->downloadStill(ep.stillPath, dest) ? dest : QString();
}

// ── Movies ──

void VideoMatcher::matchMovie(const LibVideo &video, const QString &posterSource,
                              int *written) {
    const QStringList queries =
        VideoNaming::movieQueries(video.filepath, video.filename);
    if (queries.isEmpty()) {
        writeNoMatch(video.id);
        (*written)++;
        return;
    }

    int bestId = 0;
    for (const QString &query : queries) {
        bool ok = false;
        const QVector<TmdbResult> results = m_tmdb->searchMovie(query, &ok);
        if (!ok) {
            writeAttempt(video.id);
            (*written)++;
            return;
        }
        const int best = bestCandidate(query, results);
        if (best >= 0) {
            bestId = results[best].id;
            break;
        }
    }
    if (bestId == 0) {
        fprintf(stderr, "[matcher] no acceptable movie match for: %s\n",
                qPrintable(queries.join(QStringLiteral(" | "))));
        writeNoMatch(video.id);
        (*written)++;
        return;
    }

    TmdbDetails details;
    if (!m_tmdb->movieDetails(bestId, &details)) {
        writeAttempt(video.id);
        (*written)++;
        return;
    }

    SeriesResolution meta;
    meta.failed = false;
    meta.tmdbId = details.id;
    meta.title = details.title;
    meta.year = details.year;
    meta.overview = details.overview;
    meta.cast = details.cast;
    meta.director = details.director;
    meta.rating = details.rating;
    meta.genres = details.genres;

    const QString destPath = m_thumbsDir + QStringLiteral("/%1.jpg").arg(details.id);
    if (posterSource == QLatin1String("fanart")) {
        const QStringList urls = m_fanart->moviePosterUrls(details.id);
        if (!urls.isEmpty() && m_tmdb->downloadUrl(urls.first(), destPath))
            meta.posterLocalPath = destPath;
    }
    if (meta.posterLocalPath.isEmpty() && !details.posterPath.isEmpty()
        && m_tmdb->downloadPoster(details.posterPath, destPath))
        meta.posterLocalPath = destPath;

    fprintf(stderr, "[matcher] movie: %s (%s)\n",
            qPrintable(details.title), qPrintable(details.year));
    writeMovieRow(video.id, meta);
    (*written)++;
}

// ── SQL ──

void VideoMatcher::writeTvRow(const LibVideo &v, const SeriesResolution &show,
                              const TmdbEpisode *ep, const QString &still,
                              int fixedSeason, int fixedEpisode) {
    sqlite3 *db = connection();
    if (!db)
        return;

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE videos SET tmdb_id=?, tmdb_title=?,"
        " tmdb_poster=CASE WHEN custom_poster=1 THEN tmdb_poster ELSE ? END, tmdb_cast=?,"
        " tmdb_director=?, tmdb_rating=?, tmdb_genres=?, tmdb_year=?,"
        " tmdb_cached=1, category='tv', lookup_attempts=lookup_attempts+1,"
        " last_lookup_at=strftime('%s','now') WHERE id=? AND COALESCE(user_edited,0)=0;", -1, &stmt, nullptr);
    if (stmt) {
        sqlite3_bind_int(stmt, 1, show.tmdbId);
        bindQ(stmt, 2, show.title);
        bindQ(stmt, 3, show.posterLocalPath);
        bindQ(stmt, 4, show.cast);
        bindQ(stmt, 5, show.director);
        sqlite3_bind_double(stmt, 6, show.rating);
        bindQ(stmt, 7, show.genres);
        bindQ(stmt, 8, show.year);
        sqlite3_bind_int64(stmt, 9, v.id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    if (ep) {
        sqlite3_prepare_v2(db,
            "UPDATE videos SET episode_title=?, description=?, episode_tmdb_id=?,"
            " episode_still=?, episode_air_date=?, episode_rating=?,"
            " episode_runtime=? WHERE id=? AND COALESCE(user_edited,0)=0;", -1, &stmt, nullptr);
        if (stmt) {
            bindQ(stmt, 1, ep->name);
            bindQ(stmt, 2, ep->overview);
            sqlite3_bind_int(stmt, 3, ep->tmdbId);
            bindQ(stmt, 4, still);
            bindQ(stmt, 5, ep->airDate);
            sqlite3_bind_double(stmt, 6, ep->rating);
            sqlite3_bind_int(stmt, 7, ep->runtime);
            sqlite3_bind_int64(stmt, 8, v.id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    if (fixedSeason >= 0 && fixedEpisode >= 0) {
        // Anime absolute-number fix — never over a user's hand edit.
        sqlite3_prepare_v2(db,
            "UPDATE videos SET series=?, season=?, episode=?"
            " WHERE id=? AND COALESCE(user_edited,0)=0;", -1, &stmt, nullptr);
        if (stmt) {
            bindQ(stmt, 1, v.series);
            sqlite3_bind_int(stmt, 2, fixedSeason);
            sqlite3_bind_int(stmt, 3, fixedEpisode);
            sqlite3_bind_int64(stmt, 4, v.id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else if (!v.series.isEmpty()) {
        // Canonical (folder-truth) series name; keeps season/episode.
        sqlite3_prepare_v2(db,
            "UPDATE videos SET series=? WHERE id=? AND COALESCE(user_edited,0)=0;",
            -1, &stmt, nullptr);
        if (stmt) {
            bindQ(stmt, 1, v.series);
            sqlite3_bind_int64(stmt, 2, v.id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void VideoMatcher::writeMovieRow(qint64 id, const SeriesResolution &meta) {
    sqlite3 *db = connection();
    if (!db)
        return;
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE videos SET tmdb_id=?, tmdb_title=?,"
        " tmdb_poster=CASE WHEN custom_poster=1 THEN tmdb_poster ELSE ? END, tmdb_cast=?,"
        " tmdb_director=?, tmdb_rating=?, tmdb_genres=?, tmdb_year=?,"
        " tmdb_cached=1, category='movie', description=?,"
        " lookup_attempts=lookup_attempts+1,"
        " last_lookup_at=strftime('%s','now') WHERE id=? AND COALESCE(user_edited,0)=0;", -1, &stmt, nullptr);
    if (!stmt)
        return;
    sqlite3_bind_int(stmt, 1, meta.tmdbId);
    bindQ(stmt, 2, meta.title);
    bindQ(stmt, 3, meta.posterLocalPath);
    bindQ(stmt, 4, meta.cast);
    bindQ(stmt, 5, meta.director);
    sqlite3_bind_double(stmt, 6, meta.rating);
    bindQ(stmt, 7, meta.genres);
    bindQ(stmt, 8, meta.year);
    bindQ(stmt, 9, meta.overview);
    sqlite3_bind_int64(stmt, 10, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void VideoMatcher::writeNoMatch(qint64 id) {
    sqlite3 *db = connection();
    if (!db)
        return;
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE videos SET tmdb_cached=1, lookup_attempts=lookup_attempts+1,"
        " last_lookup_at=strftime('%s','now') WHERE id=? AND COALESCE(user_edited,0)=0;", -1, &stmt, nullptr);
    if (!stmt)
        return;
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void VideoMatcher::writeAttempt(qint64 id) {
    sqlite3 *db = connection();
    if (!db)
        return;
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE videos SET lookup_attempts=lookup_attempts+1,"
        " last_lookup_at=strftime('%s','now') WHERE id=? AND COALESCE(user_edited,0)=0;", -1, &stmt, nullptr);
    if (!stmt)
        return;
    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
