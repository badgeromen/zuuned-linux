#pragma once

#include <QFileInfo>
#include <QSet>
#include <QString>

namespace TrackImportPath {

inline QString key(const QString &path) {
    return QFileInfo(path).absoluteFilePath().toCaseFolded();
}

// Keep every object from the Zune. Two device rows may legitimately carry
// the same artist, album and title, and a burst of queued extracts checks the
// destination before the first worker operation has created its file.
inline QString available(const QString &requested,
                         const QSet<QString> &reserved) {
    auto occupied = [&reserved](const QString &candidate) {
        return QFileInfo::exists(candidate) || reserved.contains(key(candidate));
    };
    if (!occupied(requested))
        return requested;

    const QFileInfo info(requested);
    const QString suffix = info.completeSuffix();
    const QString extension = suffix.isEmpty() ? QString()
        : QStringLiteral(".") + suffix;
    const QString stem = suffix.isEmpty() ? info.fileName()
        : info.fileName().left(info.fileName().size() - extension.size());
    const QString directory = info.absolutePath();
    for (int copy = 2; ; ++copy) {
        const QString candidate = directory + QLatin1Char('/') + stem
            + QStringLiteral(" (%1)").arg(copy) + extension;
        if (!occupied(candidate))
            return candidate;
    }
}

} // namespace TrackImportPath
