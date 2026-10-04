#include "VideoBrowserModel.h"

#include "../LibraryService.h"
#include "VideoNaming.h"
#include "VideoIdentity.h"

#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {

QString fileUrl(const QString &path) {
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}

} // namespace

VideoBrowserModel::VideoBrowserModel(QObject *parent) : QObject(parent) {
    // One rebuild per actual change — the throttle coalesces the scan's
    // per-chunk republish bursts.
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(300);
    connect(&m_throttle, &QTimer::timeout, this, &VideoBrowserModel::rebuild);
}

QObject *VideoBrowserModel::library() const {
    return m_library;
}

void VideoBrowserModel::setLibrary(QObject *library) {
    auto *svc = qobject_cast<LibraryService *>(library);
    if (svc == m_library)
        return;
    if (m_library)
        disconnect(m_library, nullptr, this, nullptr);
    m_library = svc;
    if (m_library) {
        connect(m_library, &LibraryService::videosChanged, this,
                &VideoBrowserModel::scheduleRebuild);
        connect(m_library, &LibraryService::foldersChanged, this,
                &VideoBrowserModel::scheduleRebuild);
        // Surgical refresh: a field-only correction patches these rows
        // in place and the delegates read the override — no rebuild, so
        // scroll and frame-grab stills stay put.
        connect(m_library, &LibraryService::videoRowsChanged, this,
                [this](const QVariantList &ids) {
            const QVector<LibVideo> &vids = m_library->videos();
            QHash<qint64, const LibVideo *> byId;
            for (const LibVideo &v : vids)
                byId.insert(v.id, &v);
            for (const QVariant &idv : ids) {
                const qint64 id = qint64(idv.toDouble());
                const LibVideo *v = byId.value(id, nullptr);
                if (v)
                    m_rowOverrides.insert(QString::number(id), videoMap(*v));
            }
            emit rowOverridesChanged();
        });
        rebuild();
    }
    emit libraryChanged();
}

void VideoBrowserModel::setDeviceVideos(const QVariantList &v) {
    // DEVIATION from the mac (cleaned-filename set only): our sync
    // writes wire names in OUR format — "Series - S02E05 - Title.ext" —
    // so TV files parse to an exact per-episode key. The mac keyed
    // episodes on displayTitle, which is the SHOW title, so its episode
    // badges could never be per-episode. Movies keep the mac's cleaned-
    // title match.
    static const QRegularExpression tvWire(
        QStringLiteral("^(.*) - S(\\d{1,2})E(\\d{1,3})(?: - .*)?$"));

    m_deviceVideos = v;
    m_deviceKeys = deviceVideoKeys(v);
    // Matching is invisible when it silently fails — log a sample so a
    // "no badges" report is diagnosable from the app log.
    if (!m_deviceKeys.isEmpty()) {
        QStringList sample;
        for (const QString &k : std::as_const(m_deviceKeys)) {
            sample << k;
            if (sample.size() >= 6)
                break;
        }
        fprintf(stderr, "[videos] %d device keys, e.g.: %s\n",
                int(m_deviceKeys.size()),
                qPrintable(sample.join(QStringLiteral(" | "))));
    }
    emit deviceVideosChanged();
    scheduleRebuild();
}

QSet<QString> VideoBrowserModel::deviceVideoKeys(const QVariantList &videosList) {
    static const QRegularExpression tvWire(
        QStringLiteral("^(.*) - S(\\d{1,2})E(\\d{1,3})(?: - .*)?$"));
    QSet<QString> keys;
    for (const QVariant &entry : videosList) {
        const QVariantMap e = entry.toMap();
        QString name = e.value(QStringLiteral("name")).toString();

        // BEST source: the vendor props (0xDA9A/0xDAB5/0xDAB6) read at
        // connect — they survive whatever the file happens to be named
        // (mac-era syncs shipped scene-style filenames).
        const QString propSeries = e.value(QStringLiteral("series")).toString();
        const int propEpisode = e.value(QStringLiteral("episode")).toInt();
        const bool hasVendorIdentity = !propSeries.isEmpty() && propEpisode > 0
            && e.contains(QStringLiteral("season"));
        if (hasVendorIdentity)
            keys.insert(episodeKey(
                propSeries, e.value(QStringLiteral("season")).toInt(),
                propEpisode));

        // A clean episode title ("Pilot") is not globally unique.
        // Legacy filenames can recover a series/number key when vendor
        // metadata is unavailable; they never create title-only TV keys.
        bool isEpisode = e.value(QStringLiteral("metagenre")).toInt() == 0x26
            || (!propSeries.isEmpty() && propEpisode > 0);
        for (const QString &candidate : {e.value(QStringLiteral("filename")).toString(), name}) {
            if (hasVendorIdentity) break;
            const auto m = tvWire.match(VideoIdentity::fileStem(candidate));
            if (m.hasMatch()) {
                isEpisode = true;
                keys.insert(episodeKey(m.captured(1), m.captured(2).toInt(), m.captured(3).toInt()));
            }
        }
        if (!isEpisode && !name.isEmpty())
            keys.insert(VideoNaming::normalizeSeriesName(VideoIdentity::fileStem(name)).toLower());
    }
    return keys;
}

QString VideoBrowserModel::episodeKey(const QString &series, int season,
                                      int episode) {
    // Squashed series so "Bob's Burgers" == "Bobs Burgers" — device
    // props and folder names disagree about punctuation constantly.
    return QStringLiteral("tv:%1|s%2e%3")
        .arg(VideoNaming::squash(series)).arg(season).arg(episode);
}

void VideoBrowserModel::setDeviceConnected(bool c) {
    if (m_deviceConnected == c)
        return;
    m_deviceConnected = c;
    emit deviceVideosChanged();
    scheduleRebuild();
}

void VideoBrowserModel::scheduleRebuild() {
    if (!m_throttle.isActive())
        m_throttle.start();
}

QString VideoBrowserModel::displayTitle(const LibVideo &v) {
    // TMDB title > episode title > cleaned filename.
    if (v.tmdbCached && !v.tmdbTitle.isEmpty())
        return v.tmdbTitle;
    if (!v.episodeTitle.isEmpty())
        return v.episodeTitle;
    return VideoNaming::normalizeSeriesName(
        VideoNaming::cachedCleanTitle(v.filename));
}

bool VideoBrowserModel::isOnDevice(const QString &title) const {
    return m_deviceConnected
        && m_deviceKeys.contains(VideoNaming::normalizeSeriesName(VideoIdentity::fileStem(title)).toLower());
}

bool VideoBrowserModel::videoOnDevice(const LibVideo &v) const {
    if (!m_deviceConnected)
        return false;
    // Episodes: exact key from series/season/episode (our wire format).
    if (v.isTV() && !v.series.isEmpty() && v.episode > 0
        && m_deviceKeys.contains(episodeKey(v.series, v.season, v.episode)))
        return true;
    return !v.isTV() && isOnDevice(displayTitle(v));
}

QVariantMap VideoBrowserModel::videoMap(const LibVideo &v) const {
    const QString title = displayTitle(v);
    return QVariantMap{
        {"id", double(v.id)},
        {"filepath", v.filepath},
        {"filename", v.filename},
        {"filesize", double(v.filesize)},
        {"durationMs", v.durationMs},
        {"width", v.width},
        {"height", v.height},
        {"title", title},
        {"description", v.description},
        {"category", v.category},
        {"series", v.series},
        {"season", v.season},
        {"episode", v.episode},
        {"episodeTitle", v.episodeTitle},
        {"tmdbId", v.tmdbId},
        {"tmdbTitle", v.tmdbTitle},
        {"poster", fileUrl(v.tmdbPoster)},
        {"posterPath", v.tmdbPoster},
        {"cast", v.tmdbCast},
        {"director", v.tmdbDirector},
        {"rating", v.tmdbRating},
        {"genres", v.tmdbGenres},
        {"year", v.tmdbYear},
        {"tmdbCached", v.tmdbCached},
        {"watched", v.watched},
        {"still", fileUrl(v.episodeStill)},
        {"airDate", v.episodeAirDate},
        {"episodeRating", v.episodeRating},
        {"episodeRuntime", v.episodeRuntime},
        {"lastPositionMs", v.lastPositionMs},
        {"isOnDevice", videoOnDevice(v)},
        {"cleanedQuery", VideoNaming::cachedCleanTitle(v.filename)},
        {"isTV", v.isTV()},
    };
}

void VideoBrowserModel::rebuild() {
    if (!m_library)
        return;
    // A full rebuild rebakes every list with fresh data — the
    // per-row overrides are now redundant and would only go stale.
    if (!m_rowOverrides.isEmpty()) {
        m_rowOverrides.clear();
        emit rowOverridesChanged();
    }
    const QVector<LibVideo> &videos = m_library->videos();

    // Episodes under anime-typed watch folders split onto their own
    // pivot; everything downstream treats both lists identically.
    QStringList animePrefixes;
    for (const QVariant &fv : m_library->watchFolders()) {
        const QVariantMap f = fv.toMap();
        if (f.value(QStringLiteral("type")).toString() == QLatin1String("anime")) {
            QString p = f.value(QStringLiteral("path")).toString();
            if (!p.endsWith(QLatin1Char('/')))
                p += QLatin1Char('/');
            animePrefixes.append(p);
        }
    }
    const auto isAnime = [&animePrefixes](const LibVideo &v) {
        for (const QString &p : animePrefixes)
            if (v.filepath.startsWith(p))
                return true;
        return false;
    };

    // ── Movies: only FULLY ready items (lookup done AND poster) ──
    struct MovieAgg {
        QVector<const LibVideo *> versions;
        const LibVideo *primary = nullptr;
    };
    QHash<int, MovieAgg> byTmdb;
    QVector<const LibVideo *> looseMovies;
    QVector<const LibVideo *> mvRows, otherRows;
    int pending = 0, unmatchedTotal = 0;

    // ── TV: group EVERY episode by series (folder-truth) ──
    struct SeriesAgg {
        QString name;
        QVector<const LibVideo *> episodes;
    };
    QHash<QString, SeriesAgg> seriesAgg, animeAgg;

    QHash<QString, QPair<QString, QVariantList>> unmatchedBySeries;
    QVector<const LibVideo *> unmatchedLooseRows;

    for (const LibVideo &v : videos) {
        if (v.isTV()) {
            const QString key = v.series.toLower();
            auto &agg = (isAnime(v) ? animeAgg : seriesAgg)[key];
            if (agg.name.isEmpty())
                agg.name = v.series;
            agg.episodes.append(&v);
        }

        // Filed by hand (music_video / other): they get their own
        // pivots, skip TMDB-readiness gates, and stay out of Needs
        // Match — a music video is never a TMDB movie.
        const bool filed = !v.isTV()
            && (v.category == QLatin1String("music_video")
                || v.category == QLatin1String("other"));
        if (filed)
            (v.category == QLatin1String("music_video")
                 ? mvRows : otherRows).append(&v);

        if (filed) {
            // no lookup bookkeeping for filed rows
        } else if (!v.tmdbCached) {
            pending++;
        } else if (v.tmdbId == 0) {
            unmatchedTotal++;
            if (v.isTV() && !v.series.isEmpty()) {
                auto &entry = unmatchedBySeries[v.series.toLower()];
                if (entry.first.isEmpty())
                    entry.first = v.series;
                entry.second.append(double(v.id));
            } else {
                unmatchedLooseRows.append(&v);
            }
        }

        // Identity and art are independent: a chosen match with no
        // poster is still a movie (the placeholder stands in).
        if (!v.isTV() && !filed
            && (v.tmdbId < 0 || (v.tmdbCached && v.tmdbId > 0))) {
            if (v.tmdbId > 0) {
                MovieAgg &agg = byTmdb[v.tmdbId];
                agg.versions.append(&v);
            } else {
                looseMovies.append(&v);
            }
        }
    }

    // Movie groups: primary = highest resolution, then largest file.
    const auto better = [](const LibVideo *a, const LibVideo *b) {
        const qint64 ra = qint64(a->width) * a->height;
        const qint64 rb = qint64(b->width) * b->height;
        if (ra != rb)
            return ra > rb;
        return a->filesize > b->filesize;
    };
    QVariantList movieList;
    for (auto it = byTmdb.begin(); it != byTmdb.end(); ++it) {
        auto &agg = it.value();
        std::sort(agg.versions.begin(), agg.versions.end(), better);
        QVariantMap m = videoMap(*agg.versions.first());
        m.insert(QStringLiteral("versionCount"), agg.versions.size());
        QVariantList ids;
        for (const LibVideo *v : agg.versions)
            ids.append(double(v->id));
        m.insert(QStringLiteral("versionIds"), ids);
        movieList.append(m);
    }
    for (const LibVideo *v : looseMovies) {
        QVariantMap m = videoMap(*v);
        m.insert(QStringLiteral("versionCount"), 1);
        m.insert(QStringLiteral("versionIds"), QVariantList{double(v->id)});
        movieList.append(m);
    }
    std::sort(movieList.begin(), movieList.end(),
              [](const QVariant &a, const QVariant &b) {
        return QString::compare(a.toMap().value(QStringLiteral("title")).toString(),
                                b.toMap().value(QStringLiteral("title")).toString(),
                                Qt::CaseInsensitive) < 0;
    });

    // Series tiles (shared by TV and Anime pivots).
    const auto buildSeriesList = [this](const QHash<QString, SeriesAgg> &aggs) {
        QVariantList out;
        int episodeTotal = 0;
        for (auto it = aggs.cbegin(); it != aggs.cend(); ++it) {
            const SeriesAgg &agg = it.value();
            episodeTotal += agg.episodes.size();
            QSet<int> seasons;
            QString poster, year;
            const LibVideo *firstEp = nullptr;
            int onDeviceCount = 0;
            for (const LibVideo *ep : agg.episodes) {
                if (ep->season != 0)
                    seasons.insert(ep->season);
                if (poster.isEmpty() && ep->tmdbCached && !ep->tmdbPoster.isEmpty())
                    poster = ep->tmdbPoster;
                if (year.isEmpty() && ep->tmdbCached)
                    year = ep->tmdbYear;
                if (!firstEp)
                    firstEp = ep;
                if (videoOnDevice(*ep))
                    onDeviceCount++;
            }
            out.append(QVariantMap{
                {"key", it.key()},
                {"name", agg.name},
                {"episodeCount", agg.episodes.size()},
                {"seasonCount", qMax(seasons.size(), 1)},
                {"poster", fileUrl(poster)},
                {"year", year},
                // Badge = the WHOLE series is on the Zune (mac rule);
                // the tile subtitle shows partial counts.
                {"isOnDevice", m_deviceConnected
                               && onDeviceCount == agg.episodes.size()
                               && onDeviceCount > 0},
                {"onDeviceCount", onDeviceCount},
                {"firstEpisodeId", firstEp ? double(firstEp->id) : -1.0},
                {"firstEpisodePath", firstEp ? firstEp->filepath : QString()},
                {"firstEpisodeDurationMs", firstEp ? firstEp->durationMs : 0},
            });
        }
        std::sort(out.begin(), out.end(),
                  [](const QVariant &a, const QVariant &b) {
            return QString::compare(
                a.toMap().value(QStringLiteral("name")).toString(),
                b.toMap().value(QStringLiteral("name")).toString(),
                Qt::CaseInsensitive) < 0;
        });
        m_totalEpisodeCount += episodeTotal;
        return out;
    };

    m_totalEpisodeCount = 0;
    // Flat filed shelves, alphabetical by display title (cleaned
    // filename when no title was matched).
    const auto displayName = [](const LibVideo *v) {
        return v->tmdbTitle.isEmpty()
            ? VideoNaming::cachedCleanTitle(v->filename) : v->tmdbTitle;
    };
    const auto flatList = [&](QVector<const LibVideo *> &rows) {
        std::sort(rows.begin(), rows.end(),
                  [&](const LibVideo *a, const LibVideo *b) {
            return displayName(a).compare(displayName(b),
                                          Qt::CaseInsensitive) < 0;
        });
        QVariantList out;
        for (const LibVideo *v : rows) {
            QVariantMap m = videoMap(*v);
            m.insert(QStringLiteral("displayTitle"), displayName(v));
            m.insert(QStringLiteral("versionCount"), 1);
            m.insert(QStringLiteral("versionIds"),
                     QVariantList{double(v->id)});
            out.append(m);
        }
        return out;
    };

    m_seriesList = buildSeriesList(seriesAgg);
    m_animeList = buildSeriesList(animeAgg);
    m_movieGroups = movieList;
    m_musicVideos = flatList(mvRows);
    m_others = flatList(otherRows);

    // Needs Match: whole shows first — one fix repairs every episode.
    QVariantList seriesGroups;
    for (auto it = unmatchedBySeries.cbegin(); it != unmatchedBySeries.cend(); ++it)
        seriesGroups.append(QVariantMap{
            {"key", it.key()},
            {"name", it.value().first},
            {"episodeIds", it.value().second},
            {"episodeCount", it.value().second.size()},
        });
    std::sort(seriesGroups.begin(), seriesGroups.end(),
              [](const QVariant &a, const QVariant &b) {
        return QString::compare(a.toMap().value(QStringLiteral("name")).toString(),
                                b.toMap().value(QStringLiteral("name")).toString(),
                                Qt::CaseInsensitive) < 0;
    });
    m_unmatchedSeriesGroups = seriesGroups;

    std::sort(unmatchedLooseRows.begin(), unmatchedLooseRows.end(),
              [](const LibVideo *a, const LibVideo *b) {
        return QString::compare(a->filename, b->filename, Qt::CaseInsensitive) < 0;
    });
    QVariantList loose;
    for (const LibVideo *v : unmatchedLooseRows)
        loose.append(videoMap(*v));
    m_unmatchedLoose = loose;
    m_unmatchedCount = unmatchedTotal;
    m_pendingCount = pending;

    emit groupsChanged();
}

QVariantMap VideoBrowserModel::movieDetail(double id) const {
    QVariantMap out;
    if (!m_library)
        return out;
    const qint64 vid = qint64(id);
    const LibVideo *row = nullptr;
    for (const LibVideo &v : m_library->videos())
        if (v.id == vid) {
            row = &v;
            break;
        }
    if (!row)
        return out;
    out = videoMap(*row);

    // All files sharing the same tmdbId — versions list.
    QVector<const LibVideo *> versions;
    if (row->tmdbId > 0) {
        for (const LibVideo &v : m_library->videos())
            if (v.tmdbId == row->tmdbId && !v.isTV())
                versions.append(&v);
        std::sort(versions.begin(), versions.end(),
                  [](const LibVideo *a, const LibVideo *b) {
            return qint64(a->width) * a->height > qint64(b->width) * b->height;
        });
    } else {
        versions.append(row);
    }
    QVariantList vlist;
    for (const LibVideo *v : versions) {
        QVariantMap m = videoMap(*v);
        // "1080p Extended BluRay"-style label from dimensions + filename.
        QStringList parts;
        if (v->height >= 2000) parts << QStringLiteral("4K");
        else if (v->height >= 1000) parts << QStringLiteral("1080p");
        else if (v->height >= 700) parts << QStringLiteral("720p");
        else if (v->height >= 400) parts << QStringLiteral("480p");
        const QString lower = v->filename.toLower();
        if (lower.contains(QLatin1String("extended"))) parts << QStringLiteral("Extended");
        else if (lower.contains(QLatin1String("director"))) parts << QStringLiteral("Director's Cut");
        else if (lower.contains(QLatin1String("unrated"))) parts << QStringLiteral("Unrated");
        else if (lower.contains(QLatin1String("theatrical"))) parts << QStringLiteral("Theatrical");
        else if (lower.contains(QLatin1String("remastered"))) parts << QStringLiteral("Remastered");
        else if (lower.contains(QLatin1String("remux"))) parts << QStringLiteral("Remux");
        if (lower.contains(QLatin1String("bluray")) || lower.contains(QLatin1String("blu-ray"))
            || lower.contains(QLatin1String("bdrip")) || lower.contains(QLatin1String("brrip")))
            parts << QStringLiteral("BluRay");
        else if (lower.contains(QLatin1String("web-dl")) || lower.contains(QLatin1String("webdl"))
                 || lower.contains(QLatin1String("webrip")))
            parts << QStringLiteral("Web-DL");
        m.insert(QStringLiteral("versionLabel"), parts.join(QLatin1Char(' ')));
        vlist.append(m);
    }
    out.insert(QStringLiteral("versions"), vlist);
    return out;
}

QVariantMap VideoBrowserModel::seriesDetail(const QString &key) const {
    QVariantMap out;
    if (!m_library)
        return out;

    QVector<const LibVideo *> eps;
    for (const LibVideo &v : m_library->videos())
        if (v.isTV() && v.series.toLower() == key)
            eps.append(&v);
    if (eps.isEmpty())
        return out;
    std::sort(eps.begin(), eps.end(), [](const LibVideo *a, const LibVideo *b) {
        if (a->season != b->season)
            return a->season < b->season;
        return a->episode < b->episode;
    });

    QSet<int> seasonsSet;
    QHash<int, int> unwatched;
    QString poster, tmdbTitle, year, description, cast, genres;
    double rating = 0;
    for (const LibVideo *ep : eps) {
        seasonsSet.insert(ep->season);
        if (!ep->watched)
            unwatched[ep->season]++;
        if (poster.isEmpty() && ep->tmdbCached && !ep->tmdbPoster.isEmpty())
            poster = ep->tmdbPoster;
        if (tmdbTitle.isEmpty() && !ep->tmdbTitle.isEmpty())
            tmdbTitle = ep->tmdbTitle;
        if (year.isEmpty() && ep->tmdbCached)
            year = ep->tmdbYear;
        if (cast.isEmpty() && ep->tmdbCached && !ep->tmdbCast.isEmpty())
            cast = ep->tmdbCast;
        if (genres.isEmpty() && ep->tmdbCached && !ep->tmdbGenres.isEmpty())
            genres = ep->tmdbGenres;
        if (rating == 0 && ep->tmdbCached)
            rating = ep->tmdbRating;
        if (description.isEmpty() && ep->tmdbCached && !ep->description.isEmpty()
            && ep->episode == 0)
            description = ep->description;
    }
    // Series-level overview: the show description isn't stored per-row
    // (rows carry episode overviews) — surface the first episode's series
    // description only if nothing else. The mac has the same limitation.

    // Numeric seasons ascending, Specials (0) last.
    QList<int> seasons = seasonsSet.values();
    std::sort(seasons.begin(), seasons.end());
    QVariantList seasonList;
    for (int s : seasons)
        if (s > 0)
            seasonList.append(s);
    if (seasonsSet.contains(0))
        seasonList.append(0);

    // Default season: first with an unwatched episode; Season 1 fallback.
    int defaultSeason = 0;
    for (const QVariant &sv : seasonList) {
        const int s = sv.toInt();
        if (s > 0 && unwatched.value(s, 0) > 0) {
            defaultSeason = s;
            break;
        }
    }
    if (defaultSeason == 0 && !seasonList.isEmpty())
        defaultSeason = seasonList.first().toInt();

    bool hasUnwatched = false;
    for (auto it = unwatched.cbegin(); it != unwatched.cend(); ++it)
        if (it.key() > 0 && it.value() > 0) {
            hasUnwatched = true;
            break;
        }

    out = QVariantMap{
        {"key", key},
        {"name", eps.first()->series},
        {"tmdbTitle", tmdbTitle},
        {"poster", fileUrl(poster)},
        {"year", year},
        {"rating", rating},
        {"cast", cast},
        {"genres", genres},
        {"description", description},
        {"seasonCount", qMax(int(seasonsSet.size()) - (seasonsSet.contains(0) ? 1 : 0), 1)},
        {"episodeCount", eps.size()},
        {"seasons", seasonList},
        {"defaultSeason", defaultSeason},
        {"hasUnwatched", hasUnwatched},
    };
    return out;
}

QVariantList VideoBrowserModel::episodesForSeason(const QString &key,
                                                  int season) const {
    QVariantList out;
    if (!m_library)
        return out;
    QVector<const LibVideo *> eps;
    for (const LibVideo &v : m_library->videos())
        if (v.isTV() && v.season == season && v.series.toLower() == key)
            eps.append(&v);
    std::sort(eps.begin(), eps.end(), [](const LibVideo *a, const LibVideo *b) {
        return a->episode < b->episode;
    });
    for (const LibVideo *v : eps)
        out.append(videoMap(*v));
    return out;
}

QVariantList VideoBrowserModel::seriesEpisodeIds(const QString &key) const {
    QVariantList out;
    if (!m_library)
        return out;
    for (const LibVideo &v : m_library->videos())
        if (v.isTV() && v.series.toLower() == key)
            out.append(double(v.id));
    return out;
}
