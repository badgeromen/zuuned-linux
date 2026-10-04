#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

// Album art extract-and-cache — port of the mac's AlbumArtService.
// Resolution order: known folder filenames (cover/folder/artwork/album/
// front .jpg/.png, case-insensitive) → any image in the track's folder
// → embedded art via zuuned_extract_art (transcode lib) at ≤400px.
//
// Cache: <cache>/art/<md5>.jpg where md5 = MD5 of
// lowercase(artist) + "\n" + lowercase(album), UTF-8. THIS KEY IS
// LOAD-BEARING — the device art sync reads the same scheme; keep it
// byte-identical to the mac.
//
// Runs requests on QThreadPool; one in-flight fetch per key; artReady
// is emitted from worker threads — connect queued.
class AlbumArtService : public QObject {
    Q_OBJECT

public:
    explicit AlbumArtService(QObject *parent = nullptr);

    // Cache key + path helpers (pure).
    static QString cacheKey(const QString &artist, const QString &album);
    QString cachePathFor(const QString &artist, const QString &album) const;

    // Returns the cached file path if it exists, else empty — never
    // blocks. UI-thread safe.
    QString cachedArtPath(const QString &artist, const QString &album) const;

    // Async: resolve art for (artist, album) given a representative
    // track filepath (for folder search + embedded extraction). No-op
    // if cached or this source has already been tried. Additional sources
    // for the same album are queued behind its active extraction.
    void requestArt(const QString &artist, const QString &album,
                    const QString &trackFilepath);
    // Queue the complete album before starting a worker, so a local miss
    // means every known source has been checked before online fallback.
    void requestSources(const QString &artist, const QString &album,
                        const QStringList &trackFilepaths);
    // Promote a JPEG already fetched from a Zune album object into the local
    // cache. Validation/normalization runs off-thread and never replaces a
    // cover that Customize or another resolver has already installed.
    void importDeviceArt(const QString &artist, const QString &album,
                         const QString &sourcePath);
    bool isResolving(const QString &artist, const QString &album) const;
    void forgetFailure(const QString &artist, const QString &album);

signals:
    // ok=false → no art found anywhere (negative result NOT cached on
    // disk; session-level memo prevents refetch storms).
    void artReady(const QString &artist, const QString &album,
                  const QString &path, bool ok);

private:
    struct State;
    std::shared_ptr<State> m_state;
    QString m_cacheDir;
};
