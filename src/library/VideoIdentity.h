#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QVariantMap>

// Display text is never a filesystem path. Only known media suffixes may
// be removed; a period in a real title ("Dr. Strangelove") is meaningful.
namespace VideoIdentity {
inline QString fileStem(QString name) {
    static const QRegularExpression suffix(
        QStringLiteral("\\.(?:wmv|mp4|m4v|mkv|avi|mov|asf|mpeg|mpg|webm|3gp)$"),
        QRegularExpression::CaseInsensitiveOption);
    name.remove(suffix);
    return name;
}

inline QString displayTitle(const QString &title, const QString &series,
                            int season, int episode) {
    Q_UNUSED(season);
    const QString clean = title.trimmed();
    static const QRegularExpression episodeLabel(
        QStringLiteral("^S\\d+E\\d+$"), QRegularExpression::CaseInsensitiveOption);
    if (!clean.isEmpty() && !(episode > 0 && !series.isEmpty()
                             && episodeLabel.match(clean).hasMatch()))
        return clean;
    return episode > 0 && !series.isEmpty() ? QStringLiteral("Episode %1").arg(episode)
                       : QStringLiteral("Untitled video");
}

inline bool matchesInterruptedFile(const QVariantMap &video, const QString &filename) {
    const QString actual = video.value(QStringLiteral("filename")).toString();
    return !filename.isEmpty() && !actual.isEmpty()
        && actual.compare(filename, Qt::CaseInsensitive) == 0;
}

inline QString safeFilePart(QString name) {
    static const QRegularExpression illegal(QStringLiteral("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}]"));
    name.replace(illegal, QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.'))) name.chop(1);
    if (name.isEmpty() || name == QLatin1String("..")) name = QStringLiteral("video");
    return name;
}

inline QString importFilename(const QVariantMap &video) {
    const QString filename = video.value(QStringLiteral("filename")).toString();
    if (!filename.isEmpty()) return safeFilePart(filename);
    QString title = video.value(QStringLiteral("name")).toString();
    const QString series = video.value(QStringLiteral("series")).toString();
    const int episode = video.value(QStringLiteral("episode")).toInt();
    title = fileStem(title);
    if (!series.isEmpty() && episode > 0) {
        title = QStringLiteral("%1 - S%2E%3 - %4").arg(series)
            .arg(video.value(QStringLiteral("season")).toInt(), 2, 10, QLatin1Char('0'))
            .arg(episode, 2, 10, QLatin1Char('0')).arg(title);
    }
    // ZMDB may not expose a filename. Object format is still known.
    const int format = video.value(QStringLiteral("format"), 0).toInt();
    const QString extension = format == 0xB981 || format == 0x300D
        ? QStringLiteral(".wmv") : QStringLiteral(".mp4");
    return safeFilePart(title) + extension;
}
}
