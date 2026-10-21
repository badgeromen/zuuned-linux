#pragma once

#include <QByteArray>
#include <QString>

// Covers a Zune can show. The device declares the largest artwork it accepts
// for each object format (zune_get_brand_limits; a Zune 30 declares 240x240).
// Firmware stores larger JPEGs byte for byte without showing them reliably:
// a Zune 80 showed none of its 1600x1600 audiobook covers and an HD loaded
// them slowly (October 19, 2026).
namespace DeviceCover {

// No Zune screen is more than 480 pixels wide; larger covers only cost time.
constexpr int ScreenEdge = 480;
// When the device declares no limit: the Zune 30's declared 240x240 and 1 MiB.
constexpr int FallbackEdge = 240;
constexpr qint64 FallbackBytes = 1024 * 1024;

// Pixel limit for one declared dimension; 0 means the device declared none.
int edge(quint32 declared);

// A baseline JPEG of the cover that fits maxWidth x maxHeight and maxBytes,
// keeping its aspect ratio and orientation; never enlarged. Transparency
// becomes black. Empty, with *error set, when the file cannot be read,
// decoded or fitted.
QByteArray prepare(const QString &path, int maxWidth, int maxHeight, qint64 maxBytes,
                   QString *error);

} // namespace DeviceCover
