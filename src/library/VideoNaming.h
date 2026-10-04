#pragma once

#include <QString>
#include <QStringList>

// Pure string helpers for the video library — ports of the mac
// LibraryService's filename/folder parsing statics. All functions are
// thread-safe (compiled regexes are function-local statics; the title
// memo is mutex-guarded).
//
// FOLDER-TRUTH doctrine: the user's directory names ARE the series
// identity. Parsing climbs season/episode container folders to the show
// folder and cleans scene junk from its name; TMDB later decorates but
// never renames.
namespace VideoNaming {

struct ParsedIdentity {
    QString category, series;
    int season = 0, episode = 0;
};
// Shared by scanning and Customize's reset-to-auto action.
ParsedIdentity parseIdentity(const QString &filepath, const QString &folderType);

// Canonical form for grouping + TMDB queries: strip scene/tracker
// prefixes ("www.site.xxx - ", "[Group] ", "(Group) "), dots/underscores
// → spaces, collapse whitespace. Idempotent.
QString normalizeSeriesName(const QString &raw);

// Truncate a SHOW folder's name at the first scene-junk token:
// "Adventure Time S01 1080p HMAX WEBRip …" → "Adventure Time".
QString cleanFolderSeriesName(const QString &raw);

// Climb past season-/episode-container folders (max 2 levels) to the
// show folder, then clean its name. "" when underivable.
QString deriveSeriesFromPath(const QString &filepath);

// Series name (usually folder-derived) → TMDB search query.
QString cleanSeriesNameForSearch(const QString &name);

// Filename → TMDB search query: drop extension, SxxEyy, bracket junk,
// release tags; preserve parenthesized year.
QString cleanFilenameForSearch(const QString &filename);

// Memoized cleanFilenameForSearch — call from per-render paths
// (displayTitle, on-device key sets).
QString cachedCleanTitle(const QString &filename);

// Squashed comparison form: lowercase alphanumerics only.
// "Bob's Burgers" == "bobsburgers".
QString squash(const QString &s);

// Lowercased alphanumeric tokens, "&"→"and", stopwords dropped.
QStringList tokenize(const QString &s);

// Candidate scoring (VideoMatcher.scoreCandidate): token overlap +
// squashed equality/containment rescue + year bonus + capped
// log-popularity tiebreak.
double scoreCandidate(const QString &query, const QString &year,
                      const QString &candidateTitle,
                      const QString &candidateYear, double popularity);

// Minimum acceptable score — below it the row goes to Needs Match.
inline constexpr double kAcceptThreshold = 0.55;

// Movie queries in falling order of trust: folder "Title (Year)",
// cleaned filename, cleaned filename truncated at its first year token.
// Deduped, order-preserving.
QStringList movieQueries(const QString &filepath, const QString &filename);

// True when the directory name is Plex/Jellyfin bonus-content
// convention (extras/featurettes/…).
bool isSkippedDir(const QString &dirName);

// True for sample/trailer stems or files under the per-folder-type size
// floor (5MB for tv/anime, 50MB otherwise; minSizeOverrideMB > 0 wins).
bool isSkippedVideoFile(const QString &filename, qint64 filesize,
                        const QString &folderType, int minSizeOverrideMB);

} // namespace VideoNaming
