#include "VideoNaming.h"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <cstdlib>
#include "zune.h"

namespace VideoNaming {

ParsedIdentity parseIdentity(const QString &filepath, const QString &folderType) {
    ParsedIdentity result;
    char *series = nullptr;
    const int tv = zune_decode_filename(filepath.toUtf8().constData(), &series,
                                         &result.season, &result.episode);
    if (tv != 0 && series) {
        result.series = normalizeSeriesName(QString::fromUtf8(series));
        result.category = QStringLiteral("tv");
    }
    free(series);
    if (folderType == QLatin1String("tv") || folderType == QLatin1String("anime")) {
        result.category = QStringLiteral("tv");
        const QString folderSeries = deriveSeriesFromPath(filepath);
        if (!folderSeries.isEmpty()) result.series = folderSeries;
    } else if (folderType == QLatin1String("movies")) {
        result.category = QStringLiteral("movie");
    }
    if (result.category != QLatin1String("tv")) {
        result.series.clear(); result.season = 0; result.episode = 0;
    }
    return result;
}

// ── Regexes (compiled once; QRegularExpression is thread-safe) ──

static const QRegularExpression &seasonFolderRegex() {
    static const QRegularExpression re(
        QStringLiteral("^(season\\s*\\d+([\\s(].*)?|s\\d{1,2}|specials?)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static const QRegularExpression &episodeFolderRegex() {
    static const QRegularExpression re(
        QStringLiteral("\\bS\\d{1,2}(?:-?E\\d{1,3})+\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static const QRegularExpression &folderJunkRegex() {
    static const QRegularExpression re(
        QStringLiteral("\\b(s\\d{1,2}(-s?\\d{1,2})?\\b|seasons?\\s+\\d|complete\\b|"
                       "\\d{3,4}p\\b|web-?(dl|rip)|bluray|bdrip|brrip|hdtv|x26[45]|"
                       "hevc|hmax|amzn|nf\\s|ddp?[\\s\\d]|aac|updated\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Tags to strip — word boundaries, all occurrences, case-insensitive.
// Deliberately ABSENT single-word traps that destroyed real titles on the
// mac: `ts`, `ma`/`master`, `web`, and standalone language codes (those
// strip only as runs of 2+).
static const QVector<QRegularExpression> &tagRegexes() {
    static const QVector<QRegularExpression> res = [] {
        const QStringList patterns = {
            // Resolution
            QStringLiteral("\\b(?:4k|2160p|1080p|720p|480p|360p|uhd)\\b"),
            // Source
            QStringLiteral("\\b(?:bluray|blu-ray|bdrip|brrip|webrip|web-?dl|hdrip|dvdrip|dvdscr|hdcam)\\b"),
            // Codec
            QStringLiteral("\\b(?:x26[45]|h\\.?26[45]|hevc|avc|xvid|divx|av1)\\b"),
            QStringLiteral("\\b(?:h\\s+26[45])\\b"),
            QStringLiteral("\\b(?:10\\s*bit|8\\s*bit|hdr10?\\+?|hdr|sdr|dovi)\\b"),
            QStringLiteral("\\b(?:10\\s*bit\\s+encode)\\b"),
            // Audio
            QStringLiteral("\\b(?:aac\\d*|ac3|dts(?:-hd)?|flac|opus|truehd|atmos)\\b"),
            QStringLiteral("\\b(?:eac3|ddp\\d*\\s*\\d*|ch\\d*)\\b"),
            QStringLiteral("\\b(?:5\\s*1|7\\s*1|2ch|6ch|5\\.1|7\\.1)\\b"),
            // Streaming service / remux / proper / etc.
            QStringLiteral("\\b(?:amzn|dsnp|hmax|atvp|mhd|remux|proper|internal|repack|rerip|hybrid)\\b"),
            // Edition
            QStringLiteral("\\b(?:remastered|extended|unrated|directors?\\s+cut|theatrical|"
                           "final\\s+cut|ultimate\\s+cut|collectors?\\s+edition|special\\s+edition|"
                           "anniversary(?:\\s+edition)?|\\d+th\\s+anniversary)\\b"),
            QStringLiteral("\\b(?:dual\\s+audio|dubbed|multisub|subbed)\\b"),
            // Release groups
            QStringLiteral("\\b(?:yts(?:\\.(?:mx|ag|am|lt))?|yify|rarbg|eztv|ettv|psa|evo|fgt|"
                           "rovers|anoxmous|mulvacoded)\\b"),
            QStringLiteral("\\b(?:bokutox|sartre|framestor|epsilon|sm737|apex|flux|ebp|etrg|gaz|"
                           "ddr|utr|mvgroup|royalty|vialle|vyndros|hodl|tigole|qxr|sparks|dxva|p4l)\\b"),
            // TV-only season/series suffixes
            QStringLiteral("\\b(?:season|series)\\s+\\d+\\b"),
            // Language codes: only RUNS of 2+ consecutive codes
            QStringLiteral("\\b(?:eng|rus|por|pol|cze|thai|hun|chi|tur|ita|latino|spa|ger|fra|jpn|"
                           "kor|ben|dut|swe|nor|dan|fin|gre|ara)(?:\\s+(?:eng|rus|por|pol|cze|thai|"
                           "hun|chi|tur|ita|latino|spa|ger|fra|jpn|kor|ben|dut|swe|nor|dan|fin|gre|"
                           "ara))+\\b"),
            // Stray markers
            QStringLiteral("\\b(?:hdtv|pdtv|mkv|v2)\\b"),
            // File sizes ("1.16GB" arrives as "1 16GB" after dot-replacement)
            QStringLiteral("\\b\\d+(?:\\s+\\d+)?\\s*[gm]b\\b"),
            // "Edition" stranded after its qualifier was stripped
            QStringLiteral("\\bedition\\b"),
            // MakeMKV disc-rip artifacts
            QStringLiteral("\\b[tT]\\d{2,}\\b"),
            QStringLiteral("\\bmainfeature\\b"),
            QStringLiteral("\\bfpl\\b"),
        };
        QVector<QRegularExpression> out;
        out.reserve(patterns.size());
        for (const QString &p : patterns)
            out.append(QRegularExpression(p, QRegularExpression::CaseInsensitiveOption));
        return out;
    }();
    return res;
}

static const QRegularExpression &sxxExxRegex() {
    static const QRegularExpression re(
        QStringLiteral("\\bS\\d{1,2}(?:-?E\\d{1,3})+\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static const QRegularExpression &releaseGroupRegex() {
    static const QRegularExpression re(QStringLiteral("\\s*-\\s*([A-Za-z0-9&]+)\\s*$"));
    return re;
}

// ── normalizeSeriesName ──

QString normalizeSeriesName(const QString &raw) {
    static QHash<QString, QString> memo;
    static QMutex memoLock;
    {
        QMutexLocker lock(&memoLock);
        const auto it = memo.constFind(raw);
        if (it != memo.constEnd())
            return it.value();
    }

    static const QRegularExpression wwwPrefix(QStringLiteral("^\\s*www\\.[^\\s]+\\s*-\\s*"));
    static const QRegularExpression bracketPrefix(QStringLiteral("^\\s*\\[[^\\]]+\\]\\s*"));
    static const QRegularExpression parenPrefix(QStringLiteral("^\\s*\\([^\\)]+\\)\\s*"));
    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));

    QString s = raw;
    s.remove(wwwPrefix);
    s.remove(bracketPrefix);
    s.remove(parenPrefix);
    s.replace(QLatin1Char('.'), QLatin1Char(' '));
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    s.replace(multiSpace, QStringLiteral(" "));
    const QString result = s.trimmed();

    QMutexLocker lock(&memoLock);
    if (memo.size() > 20000)
        memo.clear();
    memo.insert(raw, result);
    return result;
}

// ── cleanFolderSeriesName ──

QString cleanFolderSeriesName(const QString &raw) {
    const QString normalized = normalizeSeriesName(raw);
    const auto m = folderJunkRegex().match(normalized);
    if (!m.hasMatch())
        return normalized;
    QString prefix = normalized.left(m.capturedStart());
    // Trim whitespace plus stranded separators/brackets
    static const QString trimChars = QStringLiteral(" \t-–[](");
    while (!prefix.isEmpty() && trimChars.contains(prefix.back()))
        prefix.chop(1);
    return prefix.size() >= 2 ? prefix : normalized;
}

// ── deriveSeriesFromPath ──

QString deriveSeriesFromPath(const QString &filepath) {
    QString dir = QFileInfo(filepath).absolutePath();
    QString name = QFileInfo(dir).fileName();

    int climbs = 0;
    while (climbs < 2 && (seasonFolderRegex().match(name).hasMatch()
                          || episodeFolderRegex().match(name).hasMatch())) {
        dir = QFileInfo(dir).absolutePath();
        name = QFileInfo(dir).fileName();
        climbs++;
    }

    if (name.isEmpty() || name == QLatin1String("/"))
        return QString();
    return cleanFolderSeriesName(name);
}

// ── Search-query cleaning ──

QString cleanSeriesNameForSearch(const QString &name) {
    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));
    QString s = normalizeSeriesName(name);
    for (const QRegularExpression &re : tagRegexes())
        s.replace(re, QStringLiteral(" "));
    s.replace(multiSpace, QStringLiteral(" "));
    s = s.trimmed();
    while (!s.isEmpty() && (s.back() == QLatin1Char('-') || s.back() == QLatin1Char(' ')))
        s.chop(1);
    while (!s.isEmpty() && (s.front() == QLatin1Char('-') || s.front() == QLatin1Char(' ')))
        s.remove(0, 1);
    return s.isEmpty() ? normalizeSeriesName(name) : s;
}

QString cleanFilenameForSearch(const QString &filename) {
    static const QRegularExpression squareBrackets(QStringLiteral("\\[.*?\\]"));
    static const QRegularExpression curlyBrackets(QStringLiteral("\\{.*?\\}"));
    static const QRegularExpression parenYear(QStringLiteral("\\(\\s*(\\d{4})\\s*\\)"));
    static const QRegularExpression parens(QStringLiteral("\\(.*?\\)"));
    static const QRegularExpression yearToken(QStringLiteral("\\b(19\\d{2}|20\\d{2})\\b"));
    static const QRegularExpression trailingIndex(QStringLiteral("\\s+\\d{1,2}\\s*$"));
    static const QRegularExpression multiSpace(QStringLiteral("\\s+"));

    QString name = QFileInfo(filename).completeBaseName();

    name.replace(QLatin1Char('.'), QLatin1Char(' '));
    name.replace(QLatin1Char('_'), QLatin1Char(' '));

    // Strip SxxEyy TV notation. NOTE: "Episode N" stays — "Star Wars
    // Episode 4" is a movie title.
    name.remove(sxxExxRegex());

    name.remove(squareBrackets);
    name.remove(curlyBrackets);

    // Preserve a parenthesized year, drop other paren content
    QString preservedYear;
    auto it = parenYear.globalMatch(name);
    while (it.hasNext())
        preservedYear = it.next().captured(1);
    name.remove(parens);
    if (!preservedYear.isEmpty())
        name += QLatin1Char(' ') + preservedYear;

    // Trailing "-GROUPNAME" only when it looks like a release group
    // (has a digit, or ALL-CAPS ≥4 chars) — an unconditional strip ate
    // "Mission Impossible - Fallout".
    const auto rg = releaseGroupRegex().match(name);
    if (rg.hasMatch()) {
        const QString token = rg.captured(1);
        bool hasDigit = false;
        for (const QChar c : token)
            if (c.isDigit()) { hasDigit = true; break; }
        const bool isAllCaps = token.size() >= 4
            && token == token.toUpper() && token != token.toLower();
        if (hasDigit || isAllCaps)
            name.truncate(rg.capturedStart());
    }

    for (const QRegularExpression &re : tagRegexes())
        name.replace(re, QStringLiteral(" "));

    // Trailing 1–2 digit disc index: only when a year precedes it, so
    // "Ocean's 11" / "Apollo 13" stay intact.
    if (yearToken.match(name).hasMatch())
        name.remove(trailingIndex);

    name.replace(QLatin1Char('-'), QLatin1Char(' '));
    name.replace(multiSpace, QStringLiteral(" "));
    return name.trimmed();
}

QString cachedCleanTitle(const QString &filename) {
    static QHash<QString, QString> memo;
    static QMutex memoLock;
    {
        QMutexLocker lock(&memoLock);
        const auto it = memo.constFind(filename);
        if (it != memo.constEnd())
            return it.value();
    }
    const QString cleaned = cleanFilenameForSearch(filename);
    QMutexLocker lock(&memoLock);
    if (memo.size() > 20000)
        memo.clear();
    memo.insert(filename, cleaned);
    return cleaned;
}

// ── Candidate scoring ──

QString squash(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (const QChar c : s.toLower())
        if (c.isLetterOrNumber())
            out.append(c);
    return out;
}

QStringList tokenize(const QString &s) {
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9]+"));
    QString lowered = s.toLower();
    lowered.replace(QLatin1Char('&'), QStringLiteral("and"));
    QStringList out;
    for (const QString &t : lowered.split(nonAlnum, Qt::SkipEmptyParts)) {
        if (t == QLatin1String("the") || t == QLatin1String("a")
            || t == QLatin1String("an"))
            continue;
        out.append(t);
    }
    return out;
}

double scoreCandidate(const QString &query, const QString &year,
                      const QString &candidateTitle,
                      const QString &candidateYear, double popularity) {
    const QString qn = squash(query);
    const QString cn = squash(candidateTitle);

    // Same name modulo punctuation/spacing/case — certain match. Scored
    // ABOVE any token-overlap result (2.0 base + tiebreaks): stopword
    // stripping makes "THE ONE PIECE" (the remake) a perfect token
    // match for "One Piece", and popularity then picked the hyped wrong
    // show over the exact-named right one. Popularity/year still split
    // ties BETWEEN exact matches (1999 anime vs 2023 live action).
    if (!qn.isEmpty() && qn == cn) {
        double s = 2.0;
        bool yOk = false, cOk = false;
        const int yi = year.toInt(&yOk);
        const int ci = candidateYear.toInt(&cOk);
        if (yOk && cOk && std::abs(yi - ci) <= 1)
            s += 0.15;
        s += qMin(std::log10(qMax(popularity, 1.0)) / 10.0, 0.1);
        return s;
    }

    const QStringList qTok = tokenize(query);
    const QStringList cTok = tokenize(candidateTitle);
    QSet<QString> q(qTok.begin(), qTok.end());
    const QSet<QString> c(cTok.begin(), cTok.end());
    if (q.isEmpty() || c.isEmpty())
        return 0;

    const int qCount = q.size();
    const double overlap = double(QSet<QString>(q).intersect(c).size());
    double score = overlap / double(qMax(qCount, c.size()));

    // One squashed name containing the other clears the bar even when
    // tokenization disagrees about word boundaries.
    if (!qn.isEmpty() && qn.size() >= 5 && (cn.contains(qn) || qn.contains(cn)))
        score = qMax(score, 0.7);

    bool yOk = false, cOk = false;
    const int yi = year.toInt(&yOk);
    const int ci = candidateYear.toInt(&cOk);
    if (yOk && cOk && std::abs(yi - ci) <= 1)
        score += 0.15;

    // log-popularity tiebreak, capped so it can never rescue a bad title.
    score += qMin(std::log10(qMax(popularity, 1.0)) / 10.0, 0.1);
    return score;
}

// ── Movie queries ──

QStringList movieQueries(const QString &filepath, const QString &filename) {
    static const QRegularExpression titleYearDir(
        QStringLiteral("^(.+?)\\s*\\(((?:19|20)\\d{2})\\)$"));
    static const QRegularExpression yearToken(QStringLiteral("\\b(19\\d{2}|20\\d{2})\\b"));

    QStringList queries;

    // 1. FOLDER-TRUTH: parent dir named "Title (Year)".
    const QString parent = QFileInfo(QFileInfo(filepath).absolutePath()).fileName();
    const auto dm = titleYearDir.match(parent);
    if (dm.hasMatch()) {
        const QString title = dm.captured(1).trimmed();
        if (!title.isEmpty())
            queries.append(title + QLatin1Char(' ') + dm.captured(2));
    }

    // 2. Cleaned filename; 3. truncated at its FIRST year token.
    const QString cleaned = cleanFilenameForSearch(filename);
    if (!cleaned.isEmpty()) {
        queries.append(cleaned);
        const auto ym = yearToken.match(cleaned);
        if (ym.hasMatch()) {
            const QString title = cleaned.left(ym.capturedStart()).trimmed();
            if (!title.isEmpty())
                queries.append(title + QLatin1Char(' ') + ym.captured(1));
        }
    }

    QSet<QString> seen;
    QStringList out;
    for (const QString &q : queries)
        if (!seen.contains(q.toLower())) {
            seen.insert(q.toLower());
            out.append(q);
        }
    return out;
}

// ── Scan skip rules ──

bool isSkippedDir(const QString &dirName) {
    static const QSet<QString> names = {
        QStringLiteral("extras"), QStringLiteral("featurettes"),
        QStringLiteral("behind the scenes"), QStringLiteral("deleted scenes"),
        QStringLiteral("interviews"), QStringLiteral("scenes"),
        QStringLiteral("shorts"), QStringLiteral("trailers"),
        QStringLiteral("other"), QStringLiteral("samples"),
        QStringLiteral("subs"), QStringLiteral("subtitles"),
    };
    return names.contains(dirName.toLower());
}

bool isSkippedVideoFile(const QString &filename, qint64 filesize,
                        const QString &folderType, int minSizeOverrideMB) {
    static const QSet<QString> stems = {
        QStringLiteral("sample"), QStringLiteral("trailer"),
        QStringLiteral("behindthescenes"), QStringLiteral("featurette"),
    };
    const QString stem = QFileInfo(filename.toLower()).completeBaseName();
    if (stems.contains(stem))
        return true;
    if (stem.startsWith(QLatin1String("sample.")) || stem.startsWith(QLatin1String("sample-"))
        || stem.startsWith(QLatin1String("sample_")))
        return true;

    const bool tvLike = (folderType == QLatin1String("tv")
                         || folderType == QLatin1String("anime"));
    const qint64 minMB = minSizeOverrideMB > 0 ? minSizeOverrideMB : (tvLike ? 5 : 50);
    if (filesize > 0 && filesize < minMB * 1024 * 1024)
        return true;

    return false;
}

} // namespace VideoNaming
