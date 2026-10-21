#pragma once

#include <QString>
#include <QVariantMap>

// Worker-only image IO. The caller commits the returned immutable manifest in
// its library transaction. Preparation never mutates an earlier revision.
namespace HdArtistArtStore {
QVariantMap prepare(const QString &root, const QVariantMap &draft,
                    const QVariantMap &previous, QString *error);
// Bounded file reads, hash and JPEG validation before taking USB ownership.
// The returned role maps contain a QByteArray named "jpeg".
QVariantMap readSnapshot(const QVariantMap &manifest, QString *error);
}
