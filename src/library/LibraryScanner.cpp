#include "LibraryScanner.h"
#include "LibraryDb.h"
#include "VideoNaming.h"
#include "AudioCopyFingerprint.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <sys/stat.h>
#include <dirent.h>
#include <cerrno>
#include <unistd.h>

extern "C" {
#include "zune.h"
}

// ── Extension sets (LibraryService.swift) ──

static const QSet<QString> &audioExtensions() {
    static const QSet<QString> exts = {
        "mp3", "flac", "ogg", "m4a", "wma", "wav", "aac", "opus", "wv", "ape"
    };
    return exts;
}

static const QSet<QString> &videoExtensions() {
    static const QSet<QString> exts = {
        "mp4", "m4v", "mkv", "avi", "wmv", "mov", "mpeg", "mpg", "webm",
        "flv", "ts", "mts", "m2ts", "vob", "ogv", "3gp", "3g2", "divx",
        "asf", "rm", "rmvb", "f4v", "xvid"
    };
    return exts;
}

static const QSet<QString> &photoExtensions() {
    static const QSet<QString> exts = {
        "jpg", "jpeg", "png", "gif", "bmp", "heic"
    };
    return exts;
}

// ── Metadata resolution (resolveMetadata / extractFolderMetadata /
//    parseFilenameArtistTitle — LibraryService.swift ports) ──

namespace {

constexpr int musicParserVersion = 1;

struct WorkItem {
    QString filepath;
    qint64 filesize = 0;
    qint64 mtime = 0;
    qint64 mtimeNs = 0;
    qint64 ctimeNs = 0;
    bool isNew = true;
};

bool fingerprint(const QString &path, WorkItem &out) {
    struct stat info {};
    if (::stat(QFile::encodeName(path).constData(), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    out.filepath = path;
    out.filesize = info.st_size;
    out.mtime = info.st_mtim.tv_sec;
    out.mtimeNs = qint64(info.st_mtim.tv_sec) * 1000000000LL + info.st_mtim.tv_nsec;
    out.ctimeNs = qint64(info.st_ctim.tv_sec) * 1000000000LL + info.st_ctim.tv_nsec;
    return true;
}

bool accessibleDirectory(const QString &path, struct stat *identity = nullptr) {
    const QByteArray encoded = QFile::encodeName(path);
    DIR *directory = ::opendir(encoded.constData());
    if (!directory) return false;
    struct stat current {};
    const bool ok = ::access(encoded.constData(), R_OK | X_OK) == 0
        && ::fstat(::dirfd(directory), &current) == 0;
    ::closedir(directory);
    if (ok && identity) *identity = current;
    return ok;
}

struct FolderMetadata { QString artist, album; int disc = 0; };
struct TrackResult { LibTrack track; QString error; };

QString normalizePath(QString p) {
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

// Root/Artist/Album/file → (Artist, Album); Root/Album/file → (∅, Album).
// A parent folder that is itself a disc marker ("CD 1", "Disc 2") is
// skipped so the album name comes from the grandparent.
FolderMetadata extractFolderMetadata(const QString &filepath,
                                              const QSet<QString> &roots) {
    static const QRegularExpression discDir(
        QStringLiteral("^(?:cd|disc|disk)\\s*([1-9]\\d*)$"),
        QRegularExpression::CaseInsensitiveOption);

    QString parentDir = QFileInfo(filepath).absolutePath();
    if (roots.contains(normalizePath(parentDir)))
        return {};

    const auto match = discDir.match(QFileInfo(parentDir).fileName());
    const int disc = match.hasMatch() ? match.captured(1).toInt() : 0;
    if (disc > 0) {
        parentDir = QFileInfo(parentDir).absolutePath();
        if (roots.contains(normalizePath(parentDir)))
            return {QString(), QString(), disc};
    }

    const QString albumCandidate = QFileInfo(parentDir).fileName();
    const QString grandparentDir = QFileInfo(parentDir).absolutePath();
    if (roots.contains(normalizePath(grandparentDir)))
        return {QString(), albumCandidate, disc};

    const QString greatGrandparentDir = QFileInfo(grandparentDir).absolutePath();
    if (roots.contains(normalizePath(greatGrandparentDir)))
        return {QFileInfo(grandparentDir).fileName(), albumCandidate, disc};

    return {QString(), albumCandidate, disc};
}

// "Artist - Title" → (artist, title); "01 - Title" → (∅, title).
QPair<QString, QString> parseFilenameArtistTitle(const QString &nameNoExt) {
    const int idx = nameNoExt.indexOf(QStringLiteral(" - "));
    if (idx < 0)
        return {};
    const QString before = nameNoExt.left(idx);
    const QString after = nameNoExt.mid(idx + 3);
    bool allDigits = !before.isEmpty();
    for (const QChar c : before)
        if (!c.isDigit() && c != QLatin1Char('.')) { allDigits = false; break; }
    if (allDigits)
        return {QString(), after};
    return {before, after};
}

TrackResult resolveTrack(const WorkItem &work, const QSet<QString> &roots) {
    LibTrack t;
    t.filepath = work.filepath;
    t.filesize = work.filesize;
    t.mtime = work.mtime;
    t.probeMtimeNs = work.mtimeNs;
    t.probeCtimeNs = work.ctimeNs;
    t.probeVersion = musicParserVersion;

    ZuneMetadata meta {};
    if (zune_probe(work.filepath.toUtf8().constData(), &meta) == 0) {
        if (meta.title) t.title = QString::fromUtf8(meta.title).trimmed();
        if (meta.artist) t.artist = QString::fromUtf8(meta.artist).trimmed();
        if (meta.albumartist) t.albumartist = QString::fromUtf8(meta.albumartist).trimmed();
        if (meta.album) t.album = QString::fromUtf8(meta.album).trimmed();
        if (meta.genre) t.genre = QString::fromUtf8(meta.genre).trimmed();
        t.trackNumber = meta.tracknumber;
        t.discNumber = meta.discnumber;
        t.year = meta.year;
        t.durationMs = int(meta.duration_ms);
        zune_free_metadata(&meta);
    } else {
        zune_free_metadata(&meta);
        return {{}, QStringLiteral("Could not read this song. The next scan will retry.")};
    }
    WorkItem after;
    if (!fingerprint(work.filepath, after) || after.filesize != work.filesize
        || after.mtimeNs != work.mtimeNs || after.ctimeNs != work.ctimeNs)
        return {{}, QStringLiteral("This song changed while it was being read. The next scan will retry.")};

    // A reliable tagged performer outranks an unverified folder owner.
    if (t.albumartist.isEmpty() && !t.artist.isEmpty()) t.albumartist = t.artist;
    const auto folder = extractFolderMetadata(work.filepath, roots);
    if (t.artist.isEmpty()) t.artist = folder.artist;
    if (t.albumartist.isEmpty()) t.albumartist = folder.artist;
    if (t.album.isEmpty()) t.album = folder.album;
    if (t.discNumber <= 0) t.discNumber = folder.disc;

    // Filename "Artist - Title" fallback
    const QString nameNoExt = QFileInfo(work.filepath).completeBaseName();
    if (t.artist.isEmpty() || t.title.isEmpty()) {
        const auto [fnArtist, fnTitle] = parseFilenameArtistTitle(nameNoExt);
        if (t.title.isEmpty()) t.title = fnTitle.isEmpty() ? nameNoExt : fnTitle;
        if (t.artist.isEmpty()) t.artist = fnArtist;
    }

    if (t.title.isEmpty()) t.title = nameNoExt;
    if (t.artist.isEmpty()) t.artist = QStringLiteral("Unknown Artist");
    if (t.album.isEmpty()) t.album = QStringLiteral("Unknown Album");

    // Multi-disc Pattern B: strip trailing disc suffix from album names
    static const QRegularExpression discSuffix(
        QStringLiteral("(?:\\s*[\\(\\[]\\s*(?:cd|disc|disk)\\s*([1-9]\\d*)\\s*[\\)\\]]|\\s+(?:[-–]\\s*)?(?:cd|disc|disk)\\s*([1-9]\\d*))\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto suffix = discSuffix.match(t.album);
    const int suffixDisc = (suffix.captured(1).isEmpty() ? suffix.captured(2) : suffix.captured(1)).toInt();
    if (suffixDisc > 0 && !t.album.left(suffix.capturedStart()).trimmed().isEmpty()) {
        if (t.discNumber <= 0) t.discNumber = suffixDisc;
        t.album = t.album.left(suffix.capturedStart()).trimmed();
    }

    // Canonical: no TPE2 → track artist IS the album artist
    if (t.albumartist.isEmpty()) t.albumartist = t.artist;

    return {t, {}};
}

} // namespace

// ── Scanner ──

LibraryScanner::LibraryScanner(QObject *parent) : QObject(parent) {}

void LibraryScanner::cancel() { m_cancel = true; }

void LibraryScanner::scan() {
    if (m_running.exchange(true))
        return;
    m_cancel = false;

    LibraryDb db;
    if (!db.open(m_dbPath)) {
        emit failed(QStringLiteral("library db open failed: ") + db.lastError());
        m_running = false;
        return;
    }

    QSet<QString> musicRoots, photoRoots;
    // Video roots keep their folder TYPE — it forces category at insert
    // time and picks the size floor (5MB tv/anime, 50MB movies).
    QVector<WatchFolder> videoRoots;
    for (const WatchFolder &f : db.watchFolders()) {
        const QString norm = normalizePath(f.path);
        if (f.type == QLatin1String("music") || f.type == QLatin1String("all"))
            musicRoots.insert(norm);
        if (f.type == QLatin1String("photos") || f.type == QLatin1String("all"))
            photoRoots.insert(norm);
        if (f.type == QLatin1String("movies") || f.type == QLatin1String("tv")
            || f.type == QLatin1String("anime") || f.type == QLatin1String("all")) {
            WatchFolder vf = f;
            vf.path = norm;
            videoRoots.append(vf);
        }
    }

    int tracksAdded = 0, tracksUpdated = 0, photosAdded = 0;

    // Video work item — hoisted to function scope so the single unified
    // processing pass (further down) can still see it after every walk
    // has run.
    struct VideoWork {
        QString filepath;
        QString filename;
        qint64 filesize = 0;
        qint64 mtime = 0;
        qint64 existingId = -1;
        qint64 existingSize = 0;
        QString folderType;
    };
    const int videoMinSizeOverrideMB =
        QSettings().value(QStringLiteral("videoMinSizeMB"), 0).toInt();

    // ── Walk every root FIRST, collecting the work for all three media
    // kinds. The walks are cheap (enumerate + stat + mtime fast-path);
    // doing them up front yields a real GRAND TOTAL, so the progress bar
    // below is ONE continuous sweep instead of a per-phase bar that fills
    // to 100%, resets, and reads as "frozen / found nothing" — worst of
    // all on a music-heavy library, where music is the slow part. ──

    // Music work (Phase 1 — serial prep: enumerate + stat + excluded +
    // mtime fast-path; user_edited protection lives in the upsert SQL).
    QVector<WorkItem> musicWork;
    QHash<QString, QPair<quint64, quint64>> completedMusicRoots;
    {
        const auto index = db.trackProbeIndex();
        QSet<QString> retryPaths;
        for (const auto &failure : db.musicProbeFailures())
            retryPaths.insert(failure.toMap().value(QStringLiteral("filepath")).toString());
        QSet<QString> seen;
        for (const QString &root : musicRoots) {
            struct stat initialRoot {};
            if (m_cancel || !accessibleDirectory(root, &initialRoot)) continue;
            bool complete = true;
            QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (m_cancel) break;
                const QString path = it.next();
                const QFileInfo fi = it.fileInfo();
                if (fi.isDir()) {
                    // A partial walk cannot establish that an old failure is
                    // gone. Retain diagnostics if any traversed directory is
                    // inaccessible, even when its parent root is readable.
                    if (!fi.isSymLink() && !accessibleDirectory(path)) complete = false;
                    continue;
                }
                if (!audioExtensions().contains(fi.suffix().toLower()) || seen.contains(path))
                    continue;
                seen.insert(path);
                if (db.isExcluded(path))
                    continue;
                WorkItem w;
                if (!fingerprint(path, w)) continue;
                const auto known = index.constFind(path);
                if (known != index.constEnd() && !retryPaths.contains(path) && known->version == musicParserVersion
                    && known->mtimeNs == w.mtimeNs && known->ctimeNs == w.ctimeNs
                    && known->filesize == w.filesize)
                    continue; // unchanged fast-path
                w.isNew = (known == index.constEnd());
                musicWork.append(w);
            }
            struct stat finalRoot {};
            if (!m_cancel && complete && accessibleDirectory(root, &finalRoot)
                && initialRoot.st_dev == finalRoot.st_dev && initialRoot.st_ino == finalRoot.st_ino)
                completedMusicRoots.insert(root, {quint64(finalRoot.st_dev), quint64(finalRoot.st_ino)});
        }
    }

    // ═══ Videos (filename-decode only; libav probe is DEFERRED to the
    //     app's probe consumer — SMB header reads are 200-500ms/file) ═══
    QVector<VideoWork> videoWork;
    if (!videoRoots.isEmpty() && !m_cancel) {
        const auto index = db.videoPathIndex();
        const auto sizes = db.videoSizeIndex();
        for (const WatchFolder &root : videoRoots) {
            QDirIterator it(root.path, QDir::Files | QDir::Readable,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (m_cancel) break;
                const QString path = it.next();
                const QFileInfo fi = it.fileInfo();
                if (!videoExtensions().contains(fi.suffix().toLower()))
                    continue;
                // Plex/Jellyfin bonus-content dirs anywhere under the root
                const QString rel = path.mid(root.path.size() + 1);
                const QStringList parts = rel.split(QLatin1Char('/'));
                bool inSkippedDir = false;
                for (int i = 0; i < parts.size() - 1; i++)
                    if (VideoNaming::isSkippedDir(parts[i])) { inSkippedDir = true; break; }
                if (inSkippedDir)
                    continue;
                if (VideoNaming::isSkippedVideoFile(fi.fileName(), fi.size(),
                                                    root.type, videoMinSizeOverrideMB))
                    continue;
                if (db.isExcluded(path))
                    continue;
                const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
                const auto known = index.constFind(path);
                if (known != index.constEnd() && known->second == mtime)
                    continue; // unchanged fast-path
                VideoWork w;
                w.filepath = path;
                w.filename = fi.fileName();
                w.filesize = fi.size();
                w.mtime = mtime;
                if (known != index.constEnd()) {
                    w.existingId = known->first;
                    w.existingSize = sizes.value(known->first, 0);
                }
                w.folderType = root.type;
                videoWork.append(w);
            }
        }
    }

    // ═══ Photos (stat-only) ═══
    QVector<LibPhoto> photoWork;
    {
        const auto index = db.photoPathIndex();
        for (const QString &root : photoRoots) {
            QDirIterator it(root, QDir::Files | QDir::Readable,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (m_cancel) break;
                const QString path = it.next();
                const QFileInfo fi = it.fileInfo();
                if (!photoExtensions().contains(fi.suffix().toLower()))
                    continue;
                if (db.isExcluded(path))
                    continue;
                const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
                const auto known = index.constFind(path);
                if (known != index.constEnd() && known->second == mtime)
                    continue;
                LibPhoto p;
                p.filepath = path;
                p.filename = fi.fileName();
                p.filesize = fi.size();
                p.mtime = mtime;
                photoWork.append(p);
            }
        }
    }

    // ── One continuous progress sweep across every phase. `grandTotal`
    // is the real amount of work up front; `globalDone` only ever climbs,
    // so the bar fills once, 0→100%, weighted by where the work actually
    // is (music-heavy libraries spend the bar mostly on music). ──
    const int grandTotal =
        int(musicWork.size()) + int(videoWork.size()) + int(photoWork.size());
    int globalDone = 0;

    // ═══ Music — parallel probe in chunks of 64, serial transactional
    //     upsert per chunk, streaming refresh. ═══
    {
        const int chunkSize = 64;
        for (int off = 0; off < musicWork.size() && !m_cancel; off += chunkSize) {
            const int n = qMin(chunkSize, int(musicWork.size()) - off);
            QVector<WorkItem> chunk(musicWork.begin() + off,
                                    musicWork.begin() + off + n);

            const QVector<TrackResult> resolved = QtConcurrent::blockingMapped(
                chunk, std::function<TrackResult(const WorkItem &)>(
                    [&musicRoots](const WorkItem &w) {
                        return resolveTrack(w, musicRoots);
                    }));

            db.begin();
            bool changed = false;
            for (int i = 0; i < resolved.size(); i++) {
                if (!resolved[i].error.isEmpty()) {
                    bool copyInvalidated = false;
                    db.recordMusicProbeFailure(chunk[i].filepath, resolved[i].error, &copyInvalidated);
                    changed |= copyInvalidated;
                    fprintf(stderr, "[scanner] retry song '%s': %s\n", qPrintable(chunk[i].filepath), qPrintable(resolved[i].error));
                    continue;
                }
                if (db.upsertTrack(resolved[i].track)) {
                    db.clearMusicProbeFailure(chunk[i].filepath);
                    changed = true;
                    if (chunk[i].isNew) tracksAdded++;
                    else tracksUpdated++;
                }
            }
            db.commit();

            globalDone += n;
            emit progress(QStringLiteral("music"), globalDone, grandTotal,
                          chunk.last().filepath);
            if (changed) emit chunkCommitted();
            emit musicProbeFailuresChanged();
        }
    }

    // ═══ Videos — filename decode + insert/repair, chunks of 64. ═══
    {
        const int chunkSize = 64;
        for (int off = 0; off < videoWork.size() && !m_cancel; off += chunkSize) {
            const int n = qMin(chunkSize, int(videoWork.size()) - off);
            db.begin();
            for (int i = off; i < off + n; i++) {
                const VideoWork &w = videoWork[i];

                const auto parsed = VideoNaming::parseIdentity(w.filepath, w.folderType);
                const QString &seriesName = parsed.series;
                const QString &category = parsed.category;
                const int parsedSeason = parsed.season, parsedEpisode = parsed.episode;

                if (w.existingId >= 0) {
                    if (w.filesize != w.existingSize)
                        db.touchVideoChanged(w.existingId, w.filesize, w.mtime);
                    else
                        db.touchVideoMtime(w.existingId, w.mtime);
                    // Rename-in-place repair — guarded by user_edited in SQL
                    if (!category.isEmpty())
                        db.updateVideoParse(w.existingId, seriesName,
                                            parsedSeason, parsedEpisode, category);
                } else {
                    LibVideo v;
                    v.filepath = w.filepath;
                    v.filename = w.filename;
                    v.filesize = w.filesize;
                    v.mtime = w.mtime;
                    v.series = seriesName;
                    v.season = parsedSeason;
                    v.episode = parsedEpisode;
                    v.category = category;
                    db.insertVideo(v);
                }
            }
            db.commit();

            globalDone += n;
            emit progress(QStringLiteral("videos"), globalDone, grandTotal,
                          videoWork[off + n - 1].filepath);
            emit chunkCommitted();
        }
    }

    // ═══ Photos — stat-only upsert, chunks of 128. ═══
    {
        const int chunkSize = 128;
        for (int off = 0; off < photoWork.size() && !m_cancel; off += chunkSize) {
            const int n = qMin(chunkSize, int(photoWork.size()) - off);
            db.begin();
            for (int i = off; i < off + n; i++)
                if (db.upsertPhoto(photoWork[i]))
                    photosAdded++;
            db.commit();
            globalDone += n;
            emit progress(QStringLiteral("photos"), globalDone, grandTotal,
                          photoWork[off + n - 1].filepath);
            emit chunkCommitted();
        }
    }

    // Reconcile quiet retry diagnostics only after a completed, accessible
    // scan. ENOENT/ENOTDIR means a file went away; permission/network errors
    // do not. The most specific watched root wins when roots overlap, so an
    // offline nested watch folder cannot be cleared by its readable parent.
    if (!m_cancel) {
        QStringList staleFailures;
        for (const auto &failure : db.musicProbeFailures()) {
            if (m_cancel) break;
            const QString path = failure.toMap().value(QStringLiteral("filepath")).toString();
            QString owner;
            for (const QString &root : musicRoots) {
                const QString prefix = root == QLatin1String("/") ? root : root + QLatin1Char('/');
                if (path.startsWith(prefix) && root.size() > owner.size()) owner = root;
            }
            struct stat currentRoot {};
            if (owner.isEmpty() || !completedMusicRoots.contains(owner) || !accessibleDirectory(owner, &currentRoot)) continue;
            if (completedMusicRoots.value(owner) != qMakePair(quint64(currentRoot.st_dev), quint64(currentRoot.st_ino))) continue;
            struct stat file {};
            const bool absent = ::stat(QFile::encodeName(path).constData(), &file) != 0
                && (errno == ENOENT || errno == ENOTDIR);
            if (absent || db.isExcluded(path)) staleFailures.append(path);
        }
        if (!m_cancel && !staleFailures.isEmpty() && db.begin()) {
            bool ok = true;
            for (const QString &path : staleFailures) {
                if (m_cancel || !db.clearMusicProbeFailure(path)) { ok = false; break; }
            }
            if (ok && !m_cancel && db.commit()) emit musicProbeFailuresChanged();
            else db.rollback();
        }
    }

    // Inspect every indexed source: missing or inconsistent tags must not
    // prevent payload evidence from discovering a copy in another folder.
    // allTracks is deterministically ordered; unchanged sources reuse the
    // persisted hash, while new/changed sources pay one worker-side full read.
    if (!m_cancel) {
        const auto sources = db.allTracks();
        const int copyTotal = int(sources.size());
        int copiesVisited = 0;
        QString lastCopyPath;
        bool changed = false;
        for (const LibTrack &track : sources) {
            if (m_cancel) break;
            lastCopyPath = track.filepath;
            emit progress(QStringLiteral("checking copies"), copiesVisited++, copyTotal, lastCopyPath);
            WorkItem before;
            if (!fingerprint(track.filepath, before)) {
                if (!track.audioFingerprint.isEmpty())
                    changed |= db.updateAudioFingerprint(track, {});
                continue;
            }
            // A failed metadata probe must not reuse obsolete copy evidence.
            if (before.filesize != track.filesize || before.mtimeNs != track.probeMtimeNs
                || before.ctimeNs != track.probeCtimeNs) {
                if (!track.audioFingerprint.isEmpty())
                    changed |= db.updateAudioFingerprint(track, {});
                continue;
            }
            if (!track.audioFingerprint.isEmpty()) continue;
            const QString hash = AudioCopyFingerprint::read(track.filepath, m_cancel);
            WorkItem after;
            if (m_cancel || hash.isEmpty() || !fingerprint(track.filepath, after)
                || before.filesize != after.filesize || before.mtimeNs != after.mtimeNs
                || before.ctimeNs != after.ctimeNs) continue;
            changed |= db.updateAudioFingerprint(track, hash);
        }
        if (!m_cancel && copyTotal > 0)
            emit progress(QStringLiteral("checking copies"), copiesVisited, copyTotal, lastCopyPath);
        if (changed) emit chunkCommitted();
    }

    emit finished(tracksAdded, tracksUpdated, photosAdded);
    m_running = false;
}
