#pragma once

#include "LibraryTypes.h"

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class LibraryService;

// Memoized groupings for the video library — VideoBrowserModel.swift
// port. Recomputes ONCE per actual library change (300ms throttle), off
// the UI thread; QML only ever reads the published lists.
//
// FOLDER-TRUTH: every TV episode belongs to a series the user defined
// with a directory — series tiles show immediately, not only after a
// TMDB match lands a poster (unmatched tiles get frame-grab faces via
// FrameThumbnailService). Movies show only when FULLY ready (lookup done
// AND poster on disk) — the "pop pop pop" as items finish.
class VideoBrowserModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Wire from QML: library: LibraryService
    Q_PROPERTY(QObject *library READ library WRITE setLibrary NOTIFY libraryChanged)
    // Device video names (DeviceService.videosList) + connected — drives
    // on-device badges. Keys mirror the mac: cleanTitle(name).lower().
    Q_PROPERTY(QVariantList deviceVideos READ deviceVideos WRITE setDeviceVideos
                   NOTIFY deviceVideosChanged)
    Q_PROPERTY(bool deviceConnected READ deviceConnected WRITE setDeviceConnected
                   NOTIFY deviceVideosChanged)

    // Surgical refresh (2026-09-08): id-string → fresh videoMap for
    // rows corrected in place (fix-match / art). Delegates read this
    // over their frozen list row, so a correction repaints one tile
    // without the list — and its scroll/stills — resetting.
    Q_PROPERTY(QVariantMap rowOverrides READ rowOverrides
                   NOTIFY rowOverridesChanged)
    Q_PROPERTY(QVariantList movieGroups READ movieGroups NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList seriesList READ seriesList NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList animeList READ animeList NOTIFY groupsChanged)
    // Filed-by-hand shelves (grid study 2026-09-06): category
    // "music_video" / "other" — flat rows, no TMDB-readiness gate.
    Q_PROPERTY(QVariantList musicVideosList READ musicVideosList
                   NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList othersList READ othersList NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList unmatchedSeriesGroups READ unmatchedSeriesGroups
                   NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList unmatchedLoose READ unmatchedLoose NOTIFY groupsChanged)
    Q_PROPERTY(int unmatchedCount READ unmatchedCount NOTIFY groupsChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY groupsChanged)
    Q_PROPERTY(int movieCount READ movieCount NOTIFY groupsChanged)
    Q_PROPERTY(int totalEpisodeCount READ totalEpisodeCount NOTIFY groupsChanged)

public:
    explicit VideoBrowserModel(QObject *parent = nullptr);

    QObject *library() const;
    void setLibrary(QObject *library);
    QVariantList deviceVideos() const { return m_deviceVideos; }
    void setDeviceVideos(const QVariantList &v);
    bool deviceConnected() const { return m_deviceConnected; }
    void setDeviceConnected(bool c);

    QVariantMap rowOverrides() const { return m_rowOverrides; }
    QVariantList movieGroups() const { return m_movieGroups; }
    QVariantList seriesList() const { return m_seriesList; }
    QVariantList animeList() const { return m_animeList; }
    QVariantList musicVideosList() const { return m_musicVideos; }
    QVariantList othersList() const { return m_others; }
    QVariantList unmatchedSeriesGroups() const { return m_unmatchedSeriesGroups; }
    QVariantList unmatchedLoose() const { return m_unmatchedLoose; }
    int unmatchedCount() const { return m_unmatchedCount; }
    int pendingCount() const { return m_pendingCount; }
    int movieCount() const { return m_movieGroups.size(); }
    int totalEpisodeCount() const { return m_totalEpisodeCount; }

    // Detail lookups — live rows resolved on demand.
    static QString episodeKey(const QString &series, int season, int episode);
    // The device-presence key set for a DeviceService videosList —
    // shared with SyncEngine's sync-start re-dedup (B3) so the two
    // can never drift.
    static QSet<QString> deviceVideoKeys(const QVariantList &videosList);

    Q_INVOKABLE QVariantMap movieDetail(double id) const;      // row + versions
    Q_INVOKABLE QVariantMap seriesDetail(const QString &key) const;
    Q_INVOKABLE QVariantList episodesForSeason(const QString &key, int season) const;
    // All episode ids of one series (queue-all / delete-series).
    Q_INVOKABLE QVariantList seriesEpisodeIds(const QString &key) const;

signals:
    void rowOverridesChanged();
    void libraryChanged();
    void deviceVideosChanged();
    void groupsChanged();

private:
    void scheduleRebuild();
    void rebuild();
    QVariantMap videoMap(const LibVideo &v) const;
    static QString displayTitle(const LibVideo &v);
    bool isOnDevice(const QString &title) const;
    // Episode-exact when possible; cleaned-title fallback (mac rule).
    bool videoOnDevice(const LibVideo &v) const;

    LibraryService *m_library = nullptr;
    QVariantList m_deviceVideos;
    bool m_deviceConnected = false;
    QSet<QString> m_deviceKeys;

    QTimer m_throttle;

    QVariantMap m_rowOverrides;   // id-string → fresh videoMap
    QVariantList m_movieGroups, m_seriesList, m_animeList;
    QVariantList m_musicVideos, m_others;
    QVariantList m_unmatchedSeriesGroups, m_unmatchedLoose;
    int m_unmatchedCount = 0;
    int m_pendingCount = 0;
    int m_totalEpisodeCount = 0;
};
