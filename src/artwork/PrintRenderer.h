#pragma once

#include <QColor>
#include <QImage>
#include <QVector>

namespace ArtworkPrint {

enum class Style { Original, CleanInk, Halftone, WornPrint };

struct Options {
    Style style = Style::CleanInk;
    double detail = 0.55;
    double texture = 0.35;
    double dotSize = 0.35;
    bool monochrome = false;
};

struct Result {
    QImage image;
    QVector<QColor> palette;
};

// Pure, deterministic worker-thread operation. No resizing, I/O or shared
// mutable state. Detail controls palette size and edge-aware simplification;
// texture adds a print screen or content-seeded edge and surface wear. The wear
// pattern stays stable when controls change. At zero texture both print styles
// use the exact CleanInk base. Original is unchanged.
// dotSize changes Halftone screen spacing; monochrome selects black ink.
// Both controls affect Halftone only. Texture zero bypasses the ink treatment.
// Finite controls clamp to [0,1]; nonfinite values use the Options defaults.
Result render(const QImage &source, const Options &options);

} // namespace ArtworkPrint
