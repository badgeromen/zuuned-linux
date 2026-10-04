#include "VideoTitleRepair.h"

#include "LibraryService.h"
#include "library/VideoNaming.h"

#include <QHash>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <algorithm>

namespace {
QString seriesKey(const QString &name) {
    return VideoNaming::normalizeSeriesName(name).toCaseFolded().simplified();
}

QString episodeKey(const QString &series, int season, int episode) {
    const QString normalized = seriesKey(series);
    if (normalized.isEmpty() || season < 0 || episode <= 0) return {};
    return normalized + QLatin1Char('\n') + QString::number(season)
        + QLatin1Char('\n') + QString::number(episode);
}

QString currentTitle(const QVariantMap &video) {
    for (const auto *field : {"title", "episodeTitle", "name", "filename"}) {
        const QString title = video.value(QLatin1String(field)).toString().trimmed();
        if (!title.isEmpty()) return title;
    }
    return {};
}

// The existing cleaner assumes a filename and strips the final suffix.
// Append a sentinel suffix for display titles so "Mr. Robot" stays whole.
QString movieKey(QString value) {
    static const QRegularExpression extension(
        QStringLiteral("\\.(?:wmv|mp4|m4v|mkv|avi|mov|mpg|mpeg|asf|webm|vob|ts|m2ts)$"),
        QRegularExpression::CaseInsensitiveOption);
    value = value.trimmed();
    if (value.isEmpty()) return {};
    value.replace(QLatin1Char('/'), QLatin1Char(' '));
    value.replace(QLatin1Char('\\'), QLatin1Char(' '));
    if (!extension.match(value).hasMatch()) value += QStringLiteral(".wmv");
    QString key = VideoNaming::cleanFilenameForSearch(value).toCaseFolded();
    // A legacy FAT32 wire name replaced these title characters. Keep the
    // same equivalence here; any collision still needs one library row.
    static const QRegularExpression wirePunctuation(QStringLiteral("[<>:\"/\\\\|?*]"));
    key.replace(wirePunctuation, QStringLiteral(" "));
    return key.simplified();
}

bool consistentMovieYear(const QVariantMap &device, const LibVideo &local) {
    static const QRegularExpression year(QStringLiteral("\\b(?:19|20)\\d{2}\\b"));
    auto years = [&](const QString &value) {
        QSet<QString> result;
        auto found = year.globalMatch(movieKey(value));
        while (found.hasNext()) result.insert(found.next().captured());
        return result;
    };
    // A number in the film's actual title (1917, Blade Runner 2049) is
    // not a release-year hint. Other explicit years must agree, including
    // when the contradictory remake is absent from the local library.
    const auto titleYears = years(local.tmdbTitle);
    QSet<QString> deviceYears;
    for (const auto *field : {"title", "name", "filename"})
        deviceYears.unite(years(device.value(QLatin1String(field)).toString()));
    deviceYears.subtract(titleYears);
    if (deviceYears.isEmpty()) return true;
    if (deviceYears.size() != 1) return false;
    QSet<QString> expected = years(local.tmdbYear);
    if (expected.isEmpty()) {
        expected = years(local.filename);
        expected.subtract(titleYears);
    }
    return expected.size() == 1 && expected == deviceYears;
}

bool hasIdentity(const LibVideo &video) {
    return video.userEdited || (video.tmdbCached && video.tmdbId != 0);
}

bool usableEpisodeTitle(const QString &title) {
    // A filename-parser placeholder is not an authoritative episode name.
    static const QRegularExpression placeholder(
        QStringLiteral("^(?:s\\d+e\\d+|episode\\s*\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    return !title.trimmed().isEmpty() && !placeholder.match(title.trimmed()).hasMatch();
}

QVariantMap snapshot(const QVariantMap &video) {
    QVariantMap result;
    for (const auto *field : {"itemId", "title", "name", "filename", "episodeTitle",
                             "series", "season", "episode", "metagenre", "kind", "sizeMB"})
        result.insert(QLatin1String(field), video.value(QLatin1String(field)));
    return result;
}

bool pending(const QVariantMap &row) {
    return row.value(QStringLiteral("status")).toString() == QLatin1String("pending");
}
}

VideoTitleRepair::Plan VideoTitleRepair::buildPlan(
    const QVariantList &deviceVideos, const QVector<LibVideo> &libraryVideos) {
    Plan plan;
    // Keep every row, including unidentified duplicates, in the identity
    // index. Conflicting library rows must make a match ambiguous rather
    // than letting the one row with cached metadata win silently.
    QHash<QString, QVector<int>> episodes, movies;
    for (int i = 0; i < libraryVideos.size(); ++i) {
        const auto &video = libraryVideos[i];
        if (video.isTV()) {
            const QString key = episodeKey(video.series, video.season, video.episode);
            if (!key.isEmpty()) episodes[key].append(i);
        } else if (video.category == QLatin1String("movie")) {
            QSet<QString> keys{movieKey(video.filename)};
            if (hasIdentity(video) && !video.tmdbTitle.trimmed().isEmpty()) {
                keys.insert(movieKey(video.tmdbTitle));
                if (!video.tmdbYear.isEmpty())
                    keys.insert(movieKey(video.tmdbTitle + QLatin1Char(' ') + video.tmdbYear));
            }
            keys.remove(QString());
            for (const QString &key : keys) movies[key].append(i);
        }
    }
    QHash<quint32, int> handles;
    for (const QVariant &entry : deviceVideos)
        ++handles[entry.toMap().value(QStringLiteral("itemId")).toUInt()];

    for (const QVariant &entry : deviceVideos) {
        const QVariantMap video = entry.toMap();
        const quint32 id = video.value(QStringLiteral("itemId")).toUInt();
        if (!id || handles.value(id) != 1) { ++plan.ambiguous; continue; }
        const int genre = video.value(QStringLiteral("metagenre")).toInt();
        const QString series = video.value(QStringLiteral("series")).toString();
        bool seasonKnown = false, episodeKnown = false;
        const int season = video.value(QStringLiteral("season")).toInt(&seasonKnown);
        const int episode = video.value(QStringLiteral("episode")).toInt(&episodeKnown);
        const bool tv = genre == 0x26 || !series.trimmed().isEmpty() || episode > 0;
        QSet<int> matches;
        if (tv) {
            const QString key = episodeKey(series, season, episode);
            if (seasonKnown && episodeKnown && !key.isEmpty())
                for (int candidate : episodes.value(key)) matches.insert(candidate);
        } else if (genre == 0x25 || video.value(QStringLiteral("kind")) == QLatin1String("movie")) {
            for (const auto *field : {"title", "name", "filename"}) {
                const QString key = movieKey(video.value(QLatin1String(field)).toString());
                if (!key.isEmpty())
                    for (int candidate : movies.value(key)) matches.insert(candidate);
            }
        }
        if (matches.isEmpty()) { ++plan.unmatched; continue; }
        if (matches.size() != 1) { ++plan.ambiguous; continue; }
        const auto &local = libraryVideos[*matches.cbegin()];
        const QString after = (tv ? local.episodeTitle : local.tmdbTitle).trimmed();
        if (!hasIdentity(local) || after.isEmpty() || (tv && !usableEpisodeTitle(after))
            || (!tv && !consistentMovieYear(video, local))) {
            ++plan.unmatched; continue;
        }
        const QString before = currentTitle(video);
        if (before == after) { ++plan.unchanged; continue; }
        const QString detail = tv
            ? QStringLiteral("%1 · S%2E%3").arg(series).arg(season, 2, 10, QLatin1Char('0'))
                .arg(episode, 2, 10, QLatin1Char('0'))
            : (local.tmdbYear.isEmpty() ? QStringLiteral("movie") : local.tmdbYear);
        plan.rows.append(QVariantMap{{"itemId", id}, {"before", before}, {"after", after},
            {"kind", tv ? QStringLiteral("episode") : QStringLiteral("movie")},
            {"detail", detail}, {"selected", true}, {"status", QStringLiteral("pending")},
            {"error", QString()}, {"snapshot", snapshot(video)}, {"libraryId", local.id}});
    }
    return plan;
}

VideoTitleRepair::VideoTitleRepair(QObject *parent) : QObject(parent) {}

void VideoTitleRepair::setLibrary(QObject *value) {
    if (m_library == value || m_busy) return;
    m_library = value;
    m_previewGeneration = 0;
    reconnectInputs();
}

void VideoTitleRepair::setDevice(QObject *value) {
    if (m_device == value || m_busy) return;
    m_device = value;
    ++m_generation;
    m_wasConnected = value && value->property("connected").toBool();
    reconnectInputs();
}

void VideoTitleRepair::setSync(QObject *value) {
    if (m_sync == value || m_busy) return;
    m_sync = value;
    reconnectInputs();
}

void VideoTitleRepair::reconnectInputs() {
    for (const auto &connection : std::as_const(m_connections)) QObject::disconnect(connection);
    m_connections.clear();
    const QMetaMethod slot = metaObject()->method(metaObject()->indexOfSlot("onStateChanged()"));
    auto watch = [this, &slot](QObject *object, const char *property) {
        if (!object) return;
        const int index = object->metaObject()->indexOfProperty(property);
        if (index < 0) return;
        const QMetaProperty prop = object->metaObject()->property(index);
        if (prop.hasNotifySignal())
            m_connections.append(QObject::connect(object, prop.notifySignal(), this, slot));
    };
    // Several properties share a notifier. Duplicate notifications are
    // harmless, but keep only one connection for each signal below.
    QSet<QPair<QObject *, int>> watched;
    auto once = [&](QObject *object, const char *property) {
        if (!object) return;
        const int index = object->metaObject()->indexOfProperty(property);
        if (index < 0) return;
        const int signal = object->metaObject()->property(index).notifySignalIndex();
        const auto key = qMakePair(object, signal);
        if (watched.contains(key)) return;
        watched.insert(key); watch(object, property);
    };
    for (const char *property : {"connected", "busy", "purging", "pulling", "videosList"})
        once(m_device, property);
    once(m_sync, "isSyncing");
    once(m_library, "videoCount");
    for (QObject *object : {m_library.data(), m_device.data(), m_sync.data()}) {
        if (object) m_connections.append(connect(object, &QObject::destroyed, this,
            [this] { onStateChanged(); }));
    }
    if (m_device)
        m_connections.append(connect(m_device, SIGNAL(renameFinished(quint32,QString,bool)),
            this, SLOT(onRenameFinished(quint32,QString,bool))));
    emit inputsChanged();
    emit changed();
}

int VideoTitleRepair::selectedCount() const {
    int count = 0;
    for (const QVariant &entry : m_plan.rows) {
        const auto row = entry.toMap();
        if (pending(row) && row.value(QStringLiteral("selected")).toBool()) ++count;
    }
    return count;
}

QString VideoTitleRepair::blockedReason() const {
    if (!m_device || !m_device->property("connected").toBool())
        return QStringLiteral("Connect your Zune to review its titles.");
    if (!qobject_cast<LibraryService *>(m_library.data()))
        return QStringLiteral("Your library is not available.");
    if (!m_sync)
        return QStringLiteral("Transfer status is not available.");
    if (m_device->property("busy").toBool() || m_device->property("purging").toBool()
        || m_device->property("pulling").toBool() || m_sync->property("isSyncing").toBool())
        return QStringLiteral("Finish the current device operation before changing titles.");
    if (m_previewGeneration != m_generation)
        return QStringLiteral("Refresh the preview before applying titles.");
    return {};
}

bool VideoTitleRepair::canApply() const {
    return !m_busy && blockedReason().isEmpty() && selectedCount() > 0;
}

void VideoTitleRepair::rebuildPreview() {
    if (m_busy) return;
    m_error.clear(); m_plan = {};
    m_previewGeneration = m_generation;
    const auto *service = qobject_cast<LibraryService *>(m_library.data());
    if (blockedReason().isEmpty() && service)
        m_plan = buildPlan(m_device->property("videosList").toList(), service->videos());
    emit changed();
}

void VideoTitleRepair::setSelected(quint32 itemId, bool selected) {
    if (m_busy) return;
    for (QVariant &entry : m_plan.rows) {
        auto row = entry.toMap();
        if (row.value(QStringLiteral("itemId")).toUInt() == itemId && pending(row)) {
            row.insert(QStringLiteral("selected"), selected); entry = row; break;
        }
    }
    emit changed();
}

void VideoTitleRepair::selectAll(bool selected) {
    if (m_busy) return;
    for (QVariant &entry : m_plan.rows) {
        auto row = entry.toMap();
        if (pending(row)) { row.insert(QStringLiteral("selected"), selected); entry = row; }
    }
    emit changed();
}

void VideoTitleRepair::applySelected() {
    if (!canApply()) return;
    m_error.clear(); m_queue.clear();
    for (int i = 0; i < m_plan.rows.size(); ++i) {
        const auto row = m_plan.rows[i].toMap();
        if (pending(row) && row.value(QStringLiteral("selected")).toBool()) m_queue.append(i);
    }
    m_cursor = 0; m_current = -1;
    m_updated = m_failed = m_skipped = 0;
    m_busy = true;
    emit changed();
    sendNext();
}

void VideoTitleRepair::setRowStatus(int index, const QString &status, const QString &error) {
    auto row = m_plan.rows[index].toMap();
    row.insert(QStringLiteral("status"), status);
    row.insert(QStringLiteral("error"), error);
    m_plan.rows[index] = row;
}

void VideoTitleRepair::stopRemaining(const QString &reason) {
    m_error = reason;
    while (m_cursor < m_queue.size()) {
        setRowStatus(m_queue[m_cursor++], QStringLiteral("skipped"), reason); ++m_skipped;
    }
    finish();
}

void VideoTitleRepair::sendNext() {
    if (!m_busy || m_current >= 0) return;
    if (!blockedReason().isEmpty()) { stopRemaining(blockedReason()); return; }
    const auto *library = qobject_cast<LibraryService *>(m_library.data());
    const QVariantList currentVideos = m_device->property("videosList").toList();
    const Plan currentPlan = buildPlan(currentVideos, library->videos());
    while (m_cursor < m_queue.size()) {
        const int index = m_queue[m_cursor++];
        const auto row = m_plan.rows[index].toMap();
        const quint32 id = row.value(QStringLiteral("itemId")).toUInt();
        const auto matching = std::find_if(currentPlan.rows.cbegin(), currentPlan.rows.cend(),
            [id](const QVariant &value) { return value.toMap().value(QStringLiteral("itemId")).toUInt() == id; });
        const auto now = matching == currentPlan.rows.cend() ? QVariantMap() : matching->toMap();
        if (now.isEmpty() || now.value(QStringLiteral("snapshot")) != row.value(QStringLiteral("snapshot"))
            || now.value(QStringLiteral("after")) != row.value(QStringLiteral("after"))
            || now.value(QStringLiteral("libraryId")) != row.value(QStringLiteral("libraryId"))) {
            setRowStatus(index, QStringLiteral("skipped"), QStringLiteral("This item changed. Refresh to review it again."));
            ++m_skipped;
            continue;
        }
        m_current = index;
        setRowStatus(index, QStringLiteral("saving"));
        const QString title = row.value(QStringLiteral("after")).toString();
        emit changed();
        if (!QMetaObject::invokeMethod(m_device, "renameItem", Qt::DirectConnection,
            Q_ARG(quint32, id), Q_ARG(QString, title))) {
            m_current = -1;
            setRowStatus(index, QStringLiteral("failed"), QStringLiteral("Could not request the title change."));
            ++m_failed;
            continue;
        }
        return;
    }
    finish();
}

void VideoTitleRepair::onRenameFinished(quint32 itemId, const QString &newName, bool ok) {
    if (!m_busy || m_current < 0) return;
    const auto row = m_plan.rows[m_current].toMap();
    if (row.value(QStringLiteral("itemId")).toUInt() != itemId
        || row.value(QStringLiteral("after")).toString() != newName) return;
    setRowStatus(m_current, ok ? QStringLiteral("saved") : QStringLiteral("failed"),
        ok ? QString() : QStringLiteral("Your Zune could not save this title."));
    if (ok) ++m_updated;
    else ++m_failed;
    m_current = -1;
    emit changed();
    // Yield so an ensuing disconnect/sync/pull notification is observed
    // before another device operation is requested.
    QTimer::singleShot(0, this, &VideoTitleRepair::sendNext);
}

void VideoTitleRepair::onStateChanged() {
    const bool connected = m_device && m_device->property("connected").toBool();
    if (connected != m_wasConnected) {
        m_wasConnected = connected;
        ++m_generation;
    }
    if (m_busy && !connected) {
        if (m_current >= 0) {
            setRowStatus(m_current, QStringLiteral("failed"), QStringLiteral("Disconnected before the change was confirmed."));
            m_current = -1; ++m_failed;
        }
        stopRemaining(QStringLiteral("Your Zune disconnected. Refresh the preview after reconnecting."));
    }
    emit changed();
}

void VideoTitleRepair::finish() {
    m_busy = false;
    if (m_error.isEmpty() && (m_failed || m_skipped))
        m_error = QStringLiteral("%1 saved · %2 failed · %3 skipped. Refresh to review the remaining titles.")
            .arg(m_updated).arg(m_failed).arg(m_skipped);
    emit changed();
    emit finished(m_updated, m_failed, m_skipped);
}
