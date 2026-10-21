#include "HdArtistArtStore.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace {
constexpr qint64 sourceLimit = 16 * 1024 * 1024;
constexpr qint64 originalTotalLimit = 64 * 1024 * 1024;
constexpr qint64 jpegLimit = 128 * 1024;
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool save(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray read(const QString &path, qint64 limit, QString *error) {
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > limit) {
        fail(error, QStringLiteral("Choose a readable image no larger than 16 MiB.")); return {};
    }
    auto bytes = file.read(limit + 1);
    if (bytes.size() > limit || bytes.size() != file.size()) {
        fail(error, QStringLiteral("An image changed while it was being read. Try again.")); return {};
    }
    return bytes;
}
bool biographyValid(const QString &text) {
    if (text.toUtf8().size() > 4096) return false;
    for (const auto c : text.toUcs4()) {
        if ((c < 32 && c != 9 && c != 10 && c != 13) || c == 0xfffe || c == 0xffff
            || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
QString pathFor(const QString &source) {
    const QUrl url(source);
    return url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? source : QString();
}
QVariantMap image(const QString &source, const QString &directory, const QString &role,
                  const QString &forcedId, const QVariantMap &previous, qint64 *total,
                  QString *error) {
    const auto path = pathFor(source);
    if (path.isEmpty()) { fail(error, QStringLiteral("HD artist artwork must come from a local image.")); return {}; }
    const auto original = read(path, sourceLimit, error);
    if (original.isEmpty()) return {};
    *total += original.size();
    if (*total > originalTotalLimit) { fail(error, QStringLiteral("The selected original images exceed 64 MiB together.")); return {}; }
    QBuffer input;
    input.setData(original); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 16000000
        || size.width() > 16384 || size.height() > 16384) {
        fail(error, QStringLiteral("Use images with at most 16 million pixels.")); return {};
    }
    auto decoded = reader.read();
    if (decoded.isNull()) { fail(error, QStringLiteral("That file could not be decoded as an image.")); return {}; }
    if (decoded.colorSpace().isValid()) decoded.convertToColorSpace(QColorSpace::SRgb);
    const QSize target = role == QLatin1String("background") ? QSize(480, 480)
        : role == QLatin1String("thumbnail") ? QSize(160, 120) : QSize(480, 720);
    const bool crop = role == QLatin1String("background") || role == QLatin1String("thumbnail");
    // Crop before scaling: a narrow input must never create a giant expanded
    // intermediate merely to crop it back down to a small device image.
    if (crop) {
        const double ratio = double(target.width()) / target.height();
        int width = decoded.width(), height = decoded.height();
        if (double(width) / height > ratio) width = qMax(1, int(height * ratio));
        else height = qMax(1, int(width / ratio));
        decoded = decoded.copy((decoded.width() - width) / 2, (decoded.height() - height) / 2, width, height);
    }
    auto scaled = decoded.scaled(target, crop ? Qt::IgnoreAspectRatio : Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    QImage flat(scaled.size(), QImage::Format_RGB32);
    flat.fill(Qt::black);
    { QPainter painter(&flat); painter.drawImage(0, 0, scaled); }
    QByteArray jpeg;
    for (int quality = 90; quality >= 30; quality -= 10) {
        jpeg.clear(); QBuffer output(&jpeg); output.open(QIODevice::WriteOnly);
        if (!flat.save(&output, "JPEG", quality)) break;
        if (jpeg.size() <= jpegLimit) break;
    }
    if (jpeg.isEmpty() || jpeg.size() > jpegLimit) {
        fail(error, QStringLiteral("This image could not be prepared within the device transfer limit.")); return {};
    }
    const auto jpegHash = hash(jpeg);
    const auto id = !forcedId.isEmpty() ? forcedId
        : previous.value("jpegHash").toString() == jpegHash ? previous.value("id").toString() : uuid();
    const QString originalPath = directory + "/" + role + ".source";
    const QString jpegPath = directory + "/" + role + ".jpg";
    if (!save(originalPath, original) || !save(jpegPath, jpeg)) {
        fail(error, QStringLiteral("Could not save the prepared artist artwork.")); return {};
    }
    return {{"id", id}, {"path", jpegPath}, {"original", originalPath},
            {"sourceHash", hash(original)}, {"jpegHash", jpegHash},
            {"width", flat.width()}, {"height", flat.height()}, {"bytes", jpeg.size()}};
}
}

QVariantMap HdArtistArtStore::prepare(const QString &root, const QVariantMap &draft,
                                     const QVariantMap &previous, QString *error) {
    if (error) error->clear();
    if (draft.value("expectedRevision").toString() != previous.value("revision").toString()) {
        fail(error, QStringLiteral("HD artist artwork changed. Reopen Customize before saving.")); return {};
    }
    const auto backgroundSource = draft.value("background").toString();
    const auto gallerySources = draft.value("gallery").toList();
    const auto biography = draft.value("biography").toString().trimmed();
    if (backgroundSource.isEmpty()) {
        if (!gallerySources.isEmpty() || !draft.value("thumbnail").toString().isEmpty() || !biography.isEmpty())
            fail(error, QStringLiteral("Choose a Now Playing background before adding other HD artist content."));
        return {};
    }
    if (gallerySources.size() > 6 || !biographyValid(biography)) {
        fail(error, QStringLiteral("Choose up to six gallery images and a plain-text biography of at most 4096 UTF-8 bytes.")); return {};
    }
    const auto setId = previous.value("setId", uuid()).toString();
    const auto catalog = previous.value("catalogGuid", uuid()).toString();
    if (QUuid(setId).isNull() || QUuid(catalog).isNull()
        || QUuid(setId).toString(QUuid::WithoutBraces) != setId
        || QUuid(catalog).toString(QUuid::WithoutBraces) != catalog) {
        fail(error, QStringLiteral("The saved HD artist identity is invalid.")); return {};
    }
    const auto revision = uuid();
    const auto directory = QDir(root).absoluteFilePath(setId + "/" + revision);
    if (!QDir().mkpath(directory)) { fail(error, QStringLiteral("Could not create the artist artwork store.")); return {}; }
    QString backgroundId = catalog;
    bool valid = false;
    const auto nibble = backgroundId.right(1).toInt(&valid, 16);
    if (!valid) { fail(error, QStringLiteral("The saved catalog identity is invalid.")); return {}; }
    backgroundId[backgroundId.size() - 1] = QString::number(nibble ^ 1, 16).front();
    qint64 total = 0;
    auto background = image(backgroundSource, directory, "background", backgroundId,
                            previous.value("background").toMap(), &total, error);
    auto thumbnailSource = draft.value("thumbnail").toString();
    if (thumbnailSource.isEmpty()) thumbnailSource = backgroundSource;
    auto thumbnail = background.isEmpty() ? QVariantMap() : image(thumbnailSource, directory, "thumbnail", {},
                            previous.value("thumbnail").toMap(), &total, error);
    QVariantList gallery;
    QSet<QString> seen;
    for (qsizetype index = 0; !thumbnail.isEmpty() && index < gallerySources.size(); ++index) {
        const auto source = gallerySources.at(index).toString();
        QVariantMap old;
        for (const auto &candidate : previous.value("gallery").toList()) {
            const auto asset = candidate.toMap();
            if (pathFor(source) == asset.value("original").toString()) old = asset;
        }
        auto asset = image(source, directory, QStringLiteral("gallery-%1").arg(index), {}, old, &total, error);
        if (asset.isEmpty()) break;
        if (seen.contains(asset.value("jpegHash").toString())) {
            fail(error, QStringLiteral("Choose different pictures for each gallery position.")); break;
        }
        seen.insert(asset.value("jpegHash").toString()); gallery.append(asset);
    }
    if (background.isEmpty() || thumbnail.isEmpty() || gallery.size() != gallerySources.size() || (error && !error->isEmpty())) {
        QDir(directory).removeRecursively(); return {};
    }
    return {{"schema", 1}, {"profile", 1}, {"setId", setId}, {"catalogGuid", catalog},
            {"revision", revision}, {"background", background}, {"thumbnail", thumbnail},
            {"gallery", gallery}, {"biography", biography}};
}

QVariantMap HdArtistArtStore::readSnapshot(const QVariantMap &manifest, QString *error) {
    if (error) error->clear();
    if (manifest.isEmpty()) return {};
    if (manifest.value("schema").toInt() != 1 || manifest.value("profile").toInt() != 1
        || QUuid(manifest.value("catalogGuid").toString()).isNull()
        || !biographyValid(manifest.value("biography").toString())
        || manifest.value("gallery").toList().size() > 6) {
        fail(error, QStringLiteral("The saved HD artist artwork format is invalid.")); return {};
    }
    QVariantMap snapshot = manifest;
    const auto catalog = manifest.value("catalogGuid").toString();
    if (QUuid(catalog).toString(QUuid::WithoutBraces) != catalog) {
        fail(error, QStringLiteral("The saved catalog identity is invalid.")); return {};
    }
    QString expectedBackground = catalog;
    expectedBackground[expectedBackground.size() - 1] = QString::number(catalog.right(1).toInt(nullptr, 16) ^ 1, 16).front();
    const auto bg = manifest.value("background").toMap();
    const auto thumb = manifest.value("thumbnail").toMap();
    if (bg.value("id").toString() != expectedBackground || bg.value("width").toInt() != 480
        || bg.value("height").toInt() != 480 || thumb.value("width").toInt() != 160
        || thumb.value("height").toInt() != 120) {
        fail(error, QStringLiteral("The saved artist artwork roles are invalid.")); return {};
    }
    qint64 total = 0;
    QSet<QString> ids{manifest.value("catalogGuid").toString()};
    auto load = [&](QVariantMap asset) {
        const auto id = asset.value("id").toString();
        if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id || ids.contains(id)) return QVariantMap();
        ids.insert(id);
        auto bytes = read(asset.value("path").toString(), jpegLimit, error);
        total += bytes.size();
        if (bytes.isEmpty() || bytes.size() != asset.value("bytes").toLongLong()
            || hash(bytes) != asset.value("jpegHash").toString() || total > 1024 * 1024) return QVariantMap();
        QBuffer input(&bytes); input.open(QIODevice::ReadOnly);
        QImageReader reader(&input, "JPEG");
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || dimensions.width() > 480 || dimensions.height() > 720
            || dimensions.width() != asset.value("width").toInt()
            || dimensions.height() != asset.value("height").toInt() || reader.read().isNull()) return QVariantMap();
        asset.insert("jpeg", bytes); return asset;
    };
    auto background = load(manifest.value("background").toMap());
    auto thumbnail = load(manifest.value("thumbnail").toMap());
    QVariantList gallery;
    for (const auto &asset : manifest.value("gallery").toList()) {
        auto item = load(asset.toMap()); if (item.isEmpty()) break; gallery.append(item);
    }
    if (background.isEmpty() || thumbnail.isEmpty() || gallery.size() != manifest.value("gallery").toList().size()) {
        fail(error, QStringLiteral("Saved HD artist artwork is missing or changed. Reopen Customize to repair it.")); return {};
    }
    snapshot.insert("background", background); snapshot.insert("thumbnail", thumbnail); snapshot.insert("gallery", gallery);
    return snapshot;
}
