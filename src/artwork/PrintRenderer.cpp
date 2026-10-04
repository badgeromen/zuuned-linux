#include "PrintRenderer.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ArtworkPrint {
namespace {

struct Rgb { double r = 0, g = 0, b = 0; };
struct Lab { double l = 0, a = 0, b = 0; };
struct Swatch { Rgb rgb; Lab lab; double weight = 0; };

double control(double value, double fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}

int channel(double value) {
    return int(std::clamp(std::lround(value), 0L, 255L));
}

// OKLab is used for distances only; palette colors are weighted averages of
// actual source RGB. This avoids adding unrelated hues to a limited palette.
double linear(double channelValue) {
    static const auto table = [] {
        std::array<double, 256> values{};
        for (int i = 0; i < 256; ++i) {
            const double v = i / 255.0;
            values[i] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        }
        return values;
    }();
    const double value = std::clamp(channelValue, 0.0, 255.0);
    const int index = int(value);
    return index == 255 ? table[255]
        : table[index] + (table[index + 1] - table[index]) * (value - index);
}

Lab perceptual(Rgb color) {
    const double r = linear(color.r), g = linear(color.g), b = linear(color.b);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

double distance(Lab a, Lab b) {
    const double l = a.l - b.l, u = a.a - b.a, v = a.b - b.b;
    return l * l + u * u + v * v;
}

std::vector<Swatch> sourcePalette(const QImage &source, double detail) {
    // A bounded RGB histogram makes palette work independent of image size.
    // Actual component means, rather than bin centers, preserve neutral grays
    // and flat artwork colors exactly. Transparent pixels contribute nothing.
    std::vector<Swatch> histogram(32 * 32 * 32);
    for (int y = 0; y < source.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = row[x];
            const int alpha = qAlpha(pixel);
            if (!alpha) continue;
            const int r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel);
            auto &bin = histogram[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
            bin.weight += alpha;
            bin.rgb.r += r * alpha;
            bin.rgb.g += g * alpha;
            bin.rgb.b += b * alpha;
        }
    }
    std::vector<Swatch> colors;
    for (auto &bin : histogram) {
        if (!bin.weight) continue;
        bin.rgb = {bin.rgb.r / bin.weight, bin.rgb.g / bin.weight, bin.rgb.b / bin.weight};
        bin.lab = perceptual(bin.rgb);
        colors.push_back(bin);
    }
    if (colors.empty()) return {};

    const int count = std::min(int(colors.size()), 4 + int(std::lround(detail * 10)));
    std::vector<Swatch> palette;
    const auto mostCommon = std::max_element(colors.begin(), colors.end(),
        [](const Swatch &a, const Swatch &b) { return a.weight < b.weight; });
    palette.push_back(*mostCommon);
    std::vector<double> nearest(colors.size(), std::numeric_limits<double>::max());
    while (int(palette.size()) < count) {
        double best = -1;
        size_t chosen = 0;
        for (size_t i = 0; i < colors.size(); ++i) {
            nearest[i] = std::min(nearest[i], distance(colors[i].lab, palette.back().lab));
            // Softer-than-frequency weighting retains small lettering and
            // accents instead of spending every swatch on the background.
            const double score = nearest[i] * std::sqrt(std::sqrt(colors[i].weight));
            if (score > best) { best = score; chosen = i; }
        }
        palette.push_back(colors[chosen]);
    }

    for (int round = 0; round < 6; ++round) {
        std::vector<Swatch> totals(palette.size());
        for (const auto &color : colors) {
            size_t chosen = 0;
            double best = std::numeric_limits<double>::max();
            for (size_t i = 0; i < palette.size(); ++i) {
                const double score = distance(color.lab, palette[i].lab);
                if (score < best) { best = score; chosen = i; }
            }
            auto &sum = totals[chosen];
            sum.weight += color.weight;
            sum.rgb.r += color.rgb.r * color.weight;
            sum.rgb.g += color.rgb.g * color.weight;
            sum.rgb.b += color.rgb.b * color.weight;
        }
        for (size_t i = 0; i < palette.size(); ++i) {
            const auto &sum = totals[i];
            if (!sum.weight) continue;
            palette[i].rgb = {sum.rgb.r / sum.weight, sum.rgb.g / sum.weight, sum.rgb.b / sum.weight};
            palette[i].lab = perceptual(palette[i].rgb);
        }
    }
    std::stable_sort(palette.begin(), palette.end(), [](const auto &a, const auto &b) { return a.lab.l < b.lab.l; });
    for (auto &color : palette) {
        color.rgb = {double(channel(color.rgb.r)), double(channel(color.rgb.g)), double(channel(color.rgb.b))};
        color.lab = perceptual(color.rgb);
    }
    return palette;
}

Rgb simplify(const QImage &image, int x, int y, double detail, const std::vector<double> &range) {
    const auto center = reinterpret_cast<const QRgb *>(image.constScanLine(y))[x];
    const Rgb original{double(qRed(center)), double(qGreen(center)), double(qBlue(center))};
    if (detail >= 1.0) return original;
    const int radius = detail < 0.3 ? 2 : 1;
    static constexpr int spatial[] = {1, 4, 6, 4, 1};
    Rgb sum;
    double weights = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= image.height()) continue;
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(yy));
        for (int dx = -radius; dx <= radius; ++dx) {
            const int xx = x + dx;
            if (xx < 0 || xx >= image.width()) continue;
            const QRgb p = row[xx];
            const int dr = qRed(p) - qRed(center), dg = qGreen(p) - qGreen(center), db = qBlue(p) - qBlue(center);
            const int difference = (dr * dr + 2 * dg * dg + db * db) / 4;
            const double weight = range[difference] * qAlpha(p) * spatial[dx + 2] * spatial[dy + 2];
            weights += weight;
            sum.r += qRed(p) * weight; sum.g += qGreen(p) * weight; sum.b += qBlue(p) * weight;
        }
    }
    if (weights <= 0) return original;
    const double amount = 0.8 * (1.0 - detail);
    return {original.r + (sum.r / weights - original.r) * amount,
            original.g + (sum.g / weights - original.g) * amount,
            original.b + (sum.b / weights - original.b) * amount};
}

double noise(int x, int y, uint32_t seed) {
    uint32_t value = uint32_t(x) * 0x1f123bb5U ^ uint32_t(y) * 0x5f356495U ^ seed;
    value ^= value >> 16; value *= 0x7feb352dU;
    value ^= value >> 15; value *= 0x846ca68bU;
    value ^= value >> 16;
    return double(value & 0x00ffffffU) / 16777215.0;
}

double screenedInk(double ink, double x, double y, double period, double cosine, double sine) {
    if (ink <= 0) return 0;
    if (ink >= 1) return 1;
    // Invert the area of a circle clipped to its square cell. Dots merge in
    // shadows instead of losing tonal range at a fixed maximum dot radius.
    static const auto radii = [] {
        std::array<double, 1025> values{};
        for (size_t i = 1; i < values.size(); ++i) {
            const double target = double(i) / (values.size() - 1);
            double low = 0, high = std::sqrt(0.5);
            for (int step = 0; step < 20; ++step) {
                const double radius = (low + high) / 2;
                double area = 3.14159265359 * radius * radius;
                if (radius > 0.5)
                    area -= 4.0 * (radius * radius * std::acos(0.5 / radius)
                                  - 0.5 * std::sqrt(radius * radius - 0.25));
                if (area < target) low = radius; else high = radius;
            }
            values[i] = (low + high) / 2;
        }
        return values;
    }();
    const double sx = (cosine * x + sine * y) / period;
    const double sy = (-sine * x + cosine * y) / period;
    const double px = sx - std::floor(sx) - 0.5, py = sy - std::floor(sy) - 0.5;
    const double radius = radii[size_t(std::lround(ink * 1024))];
    return std::clamp((radius - std::sqrt(px * px + py * py)) * period + 0.5, 0.0, 1.0);
}

Rgb textureAt(Rgb base, int x, int y, int shortSide, const Options &options) {
    if (options.texture == 0 || options.style != Style::Halftone) return base;
    // Dot size changes spatial scale; Texture independently controls the amount
    // of printed ink. At full strength the dots form the image on white stock.
    const double period = std::max(2.5, shortSide / 286.0 * (2.0 + 10.0 * options.dotSize));
    const double px = x + 0.5, py = y + 0.5;
    Rgb printed;
    if (options.monochrome) {
        const double gray = 0.2126 * base.r + 0.7152 * base.g + 0.0722 * base.b;
        const double black = screenedInk(1.0 - gray / 255.0, px, py, period, 0.7071067812, 0.7071067812);
        base = {gray, gray, gray};
        printed = {255.0 * (1.0 - black), 255.0 * (1.0 - black), 255.0 * (1.0 - black)};
    } else {
        // Separate process-color screens make the comic-print rosette, rather
        // than placing a transparent black-dot pattern over a photograph.
        const double maximum = std::max({base.r, base.g, base.b}) / 255.0;
        const double black = screenedInk(1.0 - maximum, px, py, period, 0.7071067812, 0.7071067812);
        const double cyan = maximum > 0 ? screenedInk((maximum - base.r / 255.0) / maximum,
            px, py, period, 0.9659258263, 0.2588190451) : 0;
        const double magenta = maximum > 0 ? screenedInk((maximum - base.g / 255.0) / maximum,
            px, py, period, 0.2588190451, 0.9659258263) : 0;
        const double yellow = maximum > 0 ? screenedInk((maximum - base.b / 255.0) / maximum,
            px, py, period, 1.0, 0.0) : 0;
        printed = {255.0 * (1.0 - cyan) * (1.0 - black),
                   255.0 * (1.0 - magenta) * (1.0 - black),
                   255.0 * (1.0 - yellow) * (1.0 - black)};
    }
    return {base.r + (printed.r - base.r) * options.texture,
            base.g + (printed.g - base.g) * options.texture,
            base.b + (printed.b - base.b) * options.texture};
}

double smoothNoise(double x, double y, uint32_t seed) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const double sx = fx * fx * (3.0 - 2.0 * fx), sy = fy * fy * (3.0 - 2.0 * fy);
    const double a = noise(ix, iy, seed), b = noise(ix + 1, iy, seed);
    const double c = noise(ix, iy + 1, seed), d = noise(ix + 1, iy + 1, seed);
    return (a + (b - a) * sx) * (1.0 - sy) + (c + (d - c) * sx) * sy;
}

double paperNoise(double x, double y, uint32_t seed) {
    // Gradients on triangular cells produce rounded, irregular abrasion.
    // Thresholding interpolated square cells made the old scuffs look tiled.
    constexpr double skew = 0.3660254037844386, unskew = 0.2113248654051871;
    const double s = (x + y) * skew;
    const int ix = int(std::floor(x + s)), iy = int(std::floor(y + s));
    const double t = (ix + iy) * unskew;
    const double u = x - (ix - t), v = y - (iy - t);
    const int ox = u > v ? 1 : 0, oy = 1 - ox;
    static constexpr std::array<QPointF, 8> gradients{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {0.7071067812, 0.7071067812}, {-0.7071067812, 0.7071067812},
        {0.7071067812, -0.7071067812}, {-0.7071067812, -0.7071067812}}};
    auto contribution = [&](int gx, int gy, double dx, double dy) {
        const double falloff = std::max(0.0, 0.5 - dx * dx - dy * dy);
        const auto index = std::min(gradients.size() - 1, size_t(noise(gx, gy, seed) * gradients.size()));
        const auto &gradient = gradients[index];
        return falloff * falloff * falloff * falloff * (gradient.x() * dx + gradient.y() * dy);
    };
    const double value = contribution(ix, iy, u, v)
        + contribution(ix + ox, iy + oy, u - ox + unskew, v - oy + unskew)
        + contribution(ix + 1, iy + 1, u - 1 + 2 * unskew, v - 1 + 2 * unskew);
    return std::clamp(0.5 + 49.0 * value, 0.0, 1.0);
}

double ramp(double low, double high, double value) {
    const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

uint32_t artworkSeed(const QImage &source) {
    // Hash original pixels, not quantized ink or control values. This is local
    // identity for a repeatable physical wear pattern, not a security hash.
    uint32_t seed = 2166136261U;
    for (int y = 0; y < source.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x)
            seed = (seed ^ (qAlpha(row[x]) ? row[x] : 0U)) * 16777619U;
    }
    return (seed ^ uint32_t(source.width())) * 16777619U ^ uint32_t(source.height());
}

QImage scrapeMask(QSize size, uint32_t seed) {
    QImage mask(size, QImage::Format_ARGB32_Premultiplied);
    if (mask.isNull()) return {};
    mask.fill(Qt::transparent);
    QPainter painter(&mask);
    if (!painter.isActive()) return {};
    painter.setRenderHint(QPainter::Antialiasing);
    const double scale = std::min(size.width(), size.height()) / 286.0;
    int sample = 0;
    auto random = [&] { return noise(sample++, 7, seed ^ 0x165667b1U); };
    // A handful of short, curved abrasions, each with its own direction and
    // broken ends. There is no tiled scratch texture or uniform diagonal field.
    for (int i = 0; i < 10; ++i) {
        const QPointF start(size.width() * (0.04 + 0.92 * random()), size.height() * (0.04 + 0.92 * random()));
        const double angle = random() * 6.28318530718;
        const double length = (7.0 + 25.0 * random()) * scale;
        const QPointF direction(std::cos(angle), std::sin(angle));
        const QPointF normal(-direction.y(), direction.x());
        const QPointF bend = normal * ((random() - 0.5) * length * 0.2);
        QPainterPath path;
        path.moveTo(start);
        path.quadTo(start + direction * (length * 0.24) + bend, start + direction * (length * 0.46));
        path.moveTo(start + direction * (length * 0.56));
        path.quadTo(start + direction * (length * 0.78) - bend, start + direction * length);
        painter.setPen(QPen(QColor(255, 255, 255, 130 + int(100 * random())),
                            scale * (0.28 + 0.35 * random()), Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(path);
    }
    // One bent handling crease starts at a different edge on each print.
    const int side = int(random() * 4) % 4;
    const double along = 0.16 + 0.67 * random();
    const double lean = (random() - 0.5) * 0.30;
    auto point = [&](double across, double inward) {
        switch (side) {
        case 0: return QPointF(across * size.width(), inward * size.height());
        case 1: return QPointF((1.0 - inward) * size.width(), across * size.height());
        case 2: return QPointF((1.0 - across) * size.width(), (1.0 - inward) * size.height());
        default: return QPointF(inward * size.width(), (1.0 - across) * size.height());
        }
    };
    const double depth = 0.27 + 0.18 * random();
    QPainterPath crease;
    crease.moveTo(point(along, 0));
    crease.lineTo(point(along + lean * 0.45, depth * 0.44));
    crease.lineTo(point(along - lean * 0.20, depth));
    painter.setPen(QPen(QColor(255, 255, 255, 210), scale * (0.42 + 0.24 * random()), Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(crease);
    return mask;
}

bool wearInk(QImage &image, double amount, Rgb lightInk, uint32_t seed) {
    if (amount == 0) return true;
    // Muted stock, as exposed by a handled printed carton. Edge chips, rubbing
    // and sparse creases have different scales and strengths.
    const double light = 0.2126 * lightInk.r + 0.7152 * lightInk.g + 0.0722 * lightInk.b;
    const double paperLight = std::clamp(light + 38.0, 145.0, 215.0);
    const Rgb paper{0.22 * lightInk.r + 0.78 * paperLight,
                    0.22 * lightInk.g + 0.78 * paperLight,
                    0.22 * lightInk.b + 0.78 * paperLight};
    const double shortSide = std::min(image.width(), image.height());
    const double scale = shortSide / 286.0;
    const double strength = std::pow(amount, 0.65);
    struct Rub { double x, y, rx, ry, cosine, sine; };
    std::array<Rub, 8> rubs;
    int sample = 0;
    auto random = [&] { return noise(sample++, 3, seed ^ 0x85ebca6bU); };
    for (size_t i = 0; i < rubs.size(); ++i) {
        const double angle = random() * 6.28318530718;
        // Two broad surface rubs plus six patches reaching different edges.
        double x = 0.20 + 0.60 * random(), y = 0.20 + 0.60 * random();
        if (i >= 2) {
            const double across = random(), inset = 0.03 + 0.12 * random();
            switch (int(random() * 4) % 4) {
            case 0: x = across; y = inset; break;
            case 1: x = 1.0 - inset; y = across; break;
            case 2: x = across; y = 1.0 - inset; break;
            default: x = inset; y = across; break;
            }
        }
        rubs[i] = {x, y, 0.12 + 0.13 * random(), 0.07 + 0.10 * random(), std::cos(angle), std::sin(angle)};
    }
    const QImage scrapes = scrapeMask(image.size(), seed);
    if (scrapes.isNull()) return false;
    for (int y = 0; y < image.height(); ++y) {
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        const auto *scrapeRow = reinterpret_cast<const QRgb *>(scrapes.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = out[x];
            if (!qAlpha(pixel)) continue;
            const double dx = std::min(x + 0.5, image.width() - x - 0.5) / scale;
            const double dy = std::min(y + 0.5, image.height() - y - 0.5) / scale;
            const double distance = std::min(dx, dy);
            const double central = ramp(0.13, 0.32, distance / 286.0);
            const double px = (x + 0.5) / scale, py = (y + 0.5) / scale;
            const double corner = 1.0 - ramp(0.0, 30.0, std::max(dx, dy));
            const double patches = smoothNoise(px / 18.0, py / 18.0, seed ^ 0x85ebca6bU);
            const double rough = smoothNoise(px / 2.2, py / 2.2, seed ^ 0x27d4eb2fU);
            const double flecks = smoothNoise(px / 0.65, py / 0.65, seed ^ 0x9e3779b9U);
            // Broken, uneven chips grow inward from the edge and corners as
            // wear increases. Large intact sections prevent a decorative frame.
            const double edgePatch = ramp(0.40, 0.78, patches);
            const double handling = std::max(edgePatch, corner);
            const double chippedWidth = strength * (5.0 * edgePatch
                + 5.0 * corner) * (0.25 + 1.15 * rough);
            const double chip = (1.0 - ramp(chippedWidth - 0.45, chippedWidth + 0.45,
                distance + 0.8 * (flecks - 0.5))) * strength * 0.92 * handling;
            // Warp several scales of abrasion so adjacent marks can join into
            // rubbed coating, without revealing a regular grid or a repeated tile.
            const double wx = px + 9.0 * (paperNoise(px / 37.0, py / 37.0, seed ^ 0x68bc21ebU) - 0.5);
            const double wy = py + 9.0 * (paperNoise(px / 37.0, py / 37.0, seed ^ 0x02e5be93U) - 0.5);
            const double coating = paperNoise(wx / 13.0, wy / 13.0, seed ^ 0x967a889bU);
            const double abrasion = 0.58 * paperNoise(wx / 3.8, wy / 3.8, seed ^ 0xb5297a4dU)
                + 0.28 * paperNoise(wx / 1.6, wy / 1.6, seed ^ 0x1b56c4e9U)
                + 0.14 * paperNoise(wx / 0.65, wy / 0.65, seed ^ 0x7f4a7c15U);
            double rubbed = 0, alongRub = 0, acrossRub = 0;
            const double nx = wx * scale / image.width(), ny = wy * scale / image.height();
            for (const auto &rub : rubs) {
                const double ux = nx - rub.x, uy = ny - rub.y;
                const double rx = (ux * rub.cosine + uy * rub.sine) / rub.rx;
                const double ry = (-ux * rub.sine + uy * rub.cosine) / rub.ry;
                const double falloff = std::max(0.0, 1.0 - rx * rx - ry * ry);
                if (falloff * falloff > rubbed) {
                    rubbed = falloff * falloff;
                    alongRub = (ux * rub.cosine + uy * rub.sine) * shortSide / scale;
                    acrossRub = (-ux * rub.sine + uy * rub.cosine) * shortSide / scale;
                }
            }
            const double dragged = paperNoise(alongRub / 11.0, acrossRub / 1.4, seed ^ 0x94d049bbU);
            const double fade = strength * 0.72 * rubbed * (0.45 + 0.55 * coating);
            const double scuff = rubbed * ramp(0.34, 0.76, 0.7 * abrasion + 0.3 * dragged)
                * (0.045 + 0.16 * (1.0 - central));
            const double scrape = qAlpha(scrapeRow[x]) / 255.0 * (0.14 + 0.43 * (1.0 - central));
            const double surface = strength * (scuff + scrape);
            const double exposure = 1.0 - (1.0 - chip) * (1.0 - surface);
            const double gray = 0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) + 0.0722 * qBlue(pixel);
            auto worn = [&](double value, double paperTone) {
                const double faded = value + fade * (0.72 * (gray - value) + 0.24 * value);
                const double stock = strength * (flecks - 0.5) * 2.0;
                const double worn = faded + (paperTone - faded) * exposure + stock;
                // Interior wear stays subordinate to the image. Strong exposed
                // paper belongs to the rim and the beginning of a crease.
                const double limit = 32.0 + 190.0 * (1.0 - central);
                return channel(value + std::clamp(worn - value, -limit, limit));
            };
            out[x] = qRgba(worn(qRed(pixel), paper.r), worn(qGreen(pixel), paper.g),
                          worn(qBlue(pixel), paper.b), qAlpha(pixel));
        }
    }
    return true;
}

} // namespace

Result render(const QImage &source, const Options &requested) {
    if (source.isNull()) return {};
    Options options = requested;
    options.detail = control(options.detail, 0.55);
    options.texture = control(options.texture, 0.35);
    options.dotSize = control(options.dotSize, 0.35);
    const QImage input = source.convertToFormat(QImage::Format_ARGB32);
    if (input.isNull()) return {};
    // Every print starts from the same ink. Wear has a content-derived identity
    // independent of the controls; it adds material aging without moving detail.
    const double inkDetail = options.detail;
    const auto palette = sourcePalette(input, options.detail);
    // Comic printing needs a usable paper-to-ink range even for a dim portrait.
    // Expand its existing tone range before screening, without adding hues or
    // changing the base used by the other styles. Flat-color sources stay flat.
    auto luminance = [](Rgb color) { return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b; };
    const double inkBlack = palette.empty() ? 0.0 : luminance(palette.front().rgb);
    const double inkWhite = palette.empty() ? 255.0 : luminance(palette.back().rgb);
    const bool comicContrast = options.style == Style::Halftone && options.texture > 0
        && inkWhite - inkBlack >= 48.0;
    Result result;
    for (const auto &color : palette) {
        const QColor value(channel(color.rgb.r), channel(color.rgb.g), channel(color.rgb.b));
        if (!result.palette.contains(value)) result.palette.append(value);
    }
    if (options.style == Style::Original || palette.empty()) { result.image = source; return result; }

    result.image = input.copy();
    if (result.image.isNull()) return {};
    std::vector<double> range(65026);
    const double sigma = 16.0 + 24.0 * (1.0 - inkDetail);
    if (inkDetail < 1.0)
        for (size_t i = 0; i < range.size(); ++i) range[i] = std::exp(-double(i) / (2.0 * sigma * sigma));
    for (int y = 0; y < input.height(); ++y) {
        const auto *original = reinterpret_cast<const QRgb *>(input.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.image.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            const int alpha = qAlpha(original[x]);
            if (!alpha) continue;
            const Lab target = perceptual(simplify(input, x, y, inkDetail, range));
            double best = std::numeric_limits<double>::max();
            size_t chosen = 0;
            for (size_t i = 0; i < palette.size(); ++i) {
                const double score = distance(target, palette[i].lab);
                if (score < best) { best = score; chosen = i; }
            }
            Rgb ink = palette[chosen].rgb;
            if (comicContrast) {
                auto stretch = [&](double value) {
                    const double expanded = std::clamp((value - inkBlack) * 255.0 / (inkWhite - inkBlack), 0.0, 255.0);
                    return value + (expanded - value) * options.texture;
                };
                ink = {stretch(ink.r), stretch(ink.g), stretch(ink.b)};
            }
            const Rgb color = textureAt(ink, x, y, std::min(input.width(), input.height()), options);
            out[x] = qRgba(channel(color.r), channel(color.g), channel(color.b), alpha);
        }
    }
    if (options.style == Style::WornPrint
        && !wearInk(result.image, options.texture, palette.back().rgb, artworkSeed(input))) return {};
    if (source.pixelFormat().typeInterpretation() == QPixelFormat::FloatingPoint) {
        QImage precise = source.convertToFormat(QImage::Format_RGBA32FPx4);
        if (precise.isNull()) return {};
        for (int y = 0; y < precise.height(); ++y) {
            auto *row = reinterpret_cast<float *>(precise.scanLine(y));
            const auto *painted = reinterpret_cast<const QRgb *>(result.image.constScanLine(y));
            for (int x = 0; x < precise.width(); ++x) {
                if (!qAlpha(painted[x])) continue;
                row[x * 4] = qRed(painted[x]) / 255.0f;
                row[x * 4 + 1] = qGreen(painted[x]) / 255.0f;
                row[x * 4 + 2] = qBlue(painted[x]) / 255.0f;
                // The fourth component retains the source's floating alpha.
            }
        }
        result.image = precise;
    } else if (source.depth() > 32) {
        // Do not reduce a PNG/TIFF's 16-bit alpha to the palette's 8-bit RGB.
        // Processing is deliberately bounded to 8-bit color; coverage comes
        // directly from the source at its higher precision.
        QImage precise = source.convertToFormat(QImage::Format_RGBA64);
        if (precise.isNull()) return {};
        for (int y = 0; y < precise.height(); ++y) {
            auto *row = reinterpret_cast<QRgba64 *>(precise.scanLine(y));
            const auto *painted = reinterpret_cast<const QRgb *>(result.image.constScanLine(y));
            for (int x = 0; x < precise.width(); ++x) {
                if (!qAlpha(painted[x])) continue;
                row[x] = QRgba64::fromRgba64(quint16(qRed(painted[x]) * 257), quint16(qGreen(painted[x]) * 257),
                                           quint16(qBlue(painted[x]) * 257), row[x].alpha());
            }
        }
        result.image = precise;
    }
    return result;
}

} // namespace ArtworkPrint
