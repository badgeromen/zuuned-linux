#include "DeviceCover.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {
// Largest size within box with the same aspect ratio; unchanged if it fits.
QSize fit(const QSize &size, const QSize &box) {
    if (size.width() <= box.width() && size.height() <= box.height()) return size;
    const double scale = std::min(double(box.width()) / size.width(),
                                  double(box.height()) / size.height());
    return {std::clamp(int(std::lround(size.width() * scale)), 1, box.width()),
            std::clamp(int(std::lround(size.height() * scale)), 1, box.height())};
}
} // namespace

int DeviceCover::edge(quint32 declared) {
    return declared ? int(std::min<quint32>(declared, ScreenEdge)) : FallbackEdge;
}

QByteArray DeviceCover::prepare(const QString &path, int maxWidth, int maxHeight,
                                qint64 maxBytes, QString *error) {
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return QByteArray();
    };
    if (maxWidth < 1 || maxHeight < 1 || maxBytes < 1) return fail(QStringLiteral("Invalid cover limits"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot read the cover"));
    if (file.size() <= 0 || file.size() > 64 * 1024 * 1024)
        return fail(QStringLiteral("The cover file is empty or larger than 64 MiB"));
    QImageReader reader(&file);
    reader.setAutoTransform(true);
    const QSize stored = reader.size();
    if (!stored.isValid() || qint64(stored.width()) * stored.height() > 40000000)
        return fail(QStringLiteral("The cover has no usable dimensions"));
    // Scale while decoding, in the file's stored orientation.
    QSize box(maxWidth, maxHeight);
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) box.transpose();
    if (fit(stored, box) != stored) reader.setScaledSize(fit(stored, box));
    QImage image = reader.read();
    if (image.isNull()) return fail(QStringLiteral("Cannot decode the cover: ") + reader.errorString());
    if (image.colorSpace().isValid()) image.convertToColorSpace(QColorSpace::SRgb);
    const QSize target = fit(image.size(), QSize(maxWidth, maxHeight));
    if (target != image.size())
        image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::black);
    {
        QPainter painter(&flat);
        painter.drawImage(0, 0, image);
    }
    // At quality 90 and below Qt writes baseline 4:2:0 JPEG.
    for (int quality : {90, 85, 80, 70, 60, 50, 40}) {
        QByteArray jpeg;
        QBuffer output(&jpeg);
        output.open(QIODevice::WriteOnly);
        QImageWriter writer(&output, "jpeg");
        writer.setQuality(quality);
        if (!writer.write(flat)) return fail(QStringLiteral("Cannot encode the cover: ") + writer.errorString());
        if (jpeg.size() <= maxBytes) return jpeg;
    }
    return fail(QStringLiteral("The cover does not fit the device's %1-byte limit").arg(maxBytes));
}
