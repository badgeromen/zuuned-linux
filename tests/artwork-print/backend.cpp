// Isolated production renderer gate: generated pixels only, no library,
// providers, UI window, device, or external image assets.
#include "artwork/PrintRenderer.h"

#include <QCoreApplication>
#include <QImage>
#include <QVector>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *label) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    ++checks;
    if (!ok) ++failures;
}

ArtworkPrint::Options options(ArtworkPrint::Style style, double detail = .55, double texture = .35,
                             double dotSize = .35, bool monochrome = false) {
    ArtworkPrint::Options out;
    out.style = style; out.detail = detail; out.texture = texture;
    out.dotSize = dotSize; out.monochrome = monochrome;
    return out;
}

QImage texturedArt(bool grayscale = false, bool transparency = false) {
    QImage out(128, 96, QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        for (int x = 0; x < out.width(); ++x) {
            // Gradient, flat shapes, texture and an abrupt central edge.
            const int noise = int((unsigned(x * 7919 + y * 104729) ^ unsigned(x * y * 733)) % 25) - 12;
            const int value = std::clamp(30 + x * 170 / out.width() + y / 4 + noise, 0, 255);
            int r = value, g = value, b = value;
            if (!grayscale) {
                r = std::clamp(value + (x > 63 ? 25 : -10), 0, 255);
                g = std::clamp(180 - x + noise + y / 3, 0, 255);
                b = std::clamp(45 + y * 2 + noise, 0, 255);
                if (x > 32 && x < 90 && y > 25 && y < 68) {
                    r = std::clamp(205 + noise, 0, 255);
                    g = std::clamp(25 + noise, 0, 255);
                    b = std::clamp(75 + noise, 0, 255);
                }
            }
            const int alpha = transparency ? (x * 255 / (out.width() - 1)) : 255;
            out.setPixel(x, y, qRgba(r, g, b, alpha));
        }
    }
    return out;
}

bool sameAlpha(const QImage &a, const QImage &b) {
    if (a.size() != b.size()) return false;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixelColor(x, y).alpha() != b.pixelColor(x, y).alpha()) return false;
    return true;
}

double colorDifference(const QImage &a, const QImage &b) {
    if (a.size() != b.size() || a.isNull()) return 0;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const auto ac = a.pixelColor(x, y), bc = b.pixelColor(x, y);
            sum += std::abs(ac.red() - bc.red()) + std::abs(ac.green() - bc.green()) + std::abs(ac.blue() - bc.blue());
        }
    }
    return sum / (3.0 * a.width() * a.height());
}

bool neutral(const QImage &image) {
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto c = image.pixelColor(x, y);
            if (!c.alpha()) continue;
            if (std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()}) > 1) return false;
        }
    return !image.isNull();
}

bool strongCentralEdge(const QImage &image) {
    if (image.isNull()) return false;
    const int y = image.height() / 2, mid = image.width() / 2;
    if (qGray(image.pixel(mid + 8, y)) - qGray(image.pixel(mid - 8, y)) < 100) return false;
    int largest = 0, edgeAt = 0;
    for (int x = 1; x < image.width(); ++x) {
        const int change = std::abs(qGray(image.pixel(x, y)) - qGray(image.pixel(x - 1, y)));
        if (change > largest) { largest = change; edgeAt = x; }
    }
    return largest >= 50 && std::abs(edgeAt - mid) <= 2;
}

QVector<int> patchAreas(QVector<bool> inkPatch, int side) {
    QVector<int> areas;
    for (int origin = 0; origin < inkPatch.size(); ++origin) {
        if (!inkPatch[origin]) continue;
        QVector<int> pending{origin}; inkPatch[origin] = false;
        for (qsizetype next = 0; next < pending.size(); ++next) {
            const int x = pending[next] % side, y = pending[next] / side;
            for (const QPoint step : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)}) {
                const int xx = x + step.x(), yy = y + step.y();
                if (xx >= 0 && xx < side && yy >= 0 && yy < side && inkPatch[yy * side + xx]) {
                    inkPatch[yy * side + xx] = false; pending.append(yy * side + xx);
                }
            }
        }
        areas.append(int(pending.size()));
    }
    return areas;
}

int largestPatch(QVector<bool> inkPatch, int side) {
    const auto areas = patchAreas(std::move(inkPatch), side);
    return areas.isEmpty() ? 0 : *std::max_element(areas.cbegin(), areas.cend());
}

struct InkGeometry { int components = 0, medianArea = 0, transitions = 0; };
InkGeometry dotGeometry(const QImage &image) {
    if (image.isNull() || image.width() != image.height()) return {};
    const int side = image.width(), margin = side / 10;
    QVector<bool> ink(side * side, false);
    InkGeometry result;
    for (int y = margin; y < side - margin; ++y) {
        for (int x = margin; x < side - margin; ++x) {
            const bool dark = qGray(image.pixel(x, y)) < 120;
            ink[y * side + x] = dark;
            if (x > margin && dark != (qGray(image.pixel(x - 1, y)) < 120)) ++result.transitions;
            if (y > margin && dark != (qGray(image.pixel(x, y - 1)) < 120)) ++result.transitions;
        }
    }
    auto areas = patchAreas(std::move(ink), side);
    std::sort(areas.begin(), areas.end());
    result.components = int(areas.size());
    result.medianArea = areas.isEmpty() ? 0 : areas[areas.size() / 2];
    return result;
}

std::array<double, 3> meanColor(const QImage &image) {
    std::array<double, 3> mean{};
    if (image.isNull()) return mean;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            mean[0] += qRed(pixel); mean[1] += qGreen(pixel); mean[2] += qBlue(pixel);
        }
    for (double &channel : mean) channel /= double(image.width()) * image.height();
    return mean;
}

struct WearMetrics {
    double totalDifference = 0;
    double centerDifference = 0, surfaceDifference = 0;
    int centerSamples = 0, centerStrongPixels = 0;
    int surfaceSamples = 0, surfacePatch = 0;
    int edgeSpread = 0, edgePatch = 0, fadedPatch = 0;
};
WearMetrics displayedWear(const QImage &clean, const QImage &worn, int side) {
    if (clean.isNull() || worn.isNull() || clean.size() != worn.size()) return {};
    const QImage before = clean.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage after = worn.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    WearMetrics result;
    QVector<int> edgeTones;
    QVector<bool> chips(side * side, false), fades(side * side, false), surface(side * side, false);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const QRgb a = before.pixel(x, y), b = after.pixel(x, y);
            const int difference = std::max({std::abs(qRed(a) - qRed(b)),
                std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            const int edgeDistance = std::min({x, y, side - 1 - x, side - 1 - y});
            result.totalDifference += difference;
            if (x >= side * 3 / 10 && x < side * 7 / 10
                && y >= side * 3 / 10 && y < side * 7 / 10) {
                ++result.centerSamples;
                result.centerDifference += difference;
                if (difference > 48) ++result.centerStrongPixels;
            }
            if (edgeDistance >= side / 5) {
                ++result.surfaceSamples;
                result.surfaceDifference += difference;
                surface[y * side + x] = difference >= 2;
            }
            if (edgeDistance < side / 10) {
                edgeTones.append(qGray(b));
                chips[y * side + x] = difference >= 8;
            }
            // A connected, gently changed patch away from the very rim is
            // evidence of rubbed color, distinct from only a chipped border.
            fades[y * side + x] = edgeDistance >= side / 20 && edgeDistance < side / 4
                && difference >= 2 && difference <= 24;
        }
    }
    std::sort(edgeTones.begin(), edgeTones.end());
    result.edgeSpread = edgeTones[edgeTones.size() * 99 / 100] - edgeTones[edgeTones.size() * 5 / 100];
    result.edgePatch = largestPatch(chips, side);
    result.fadedPatch = largestPatch(fades, side);
    result.surfacePatch = largestPatch(surface, side);
    return result;
}

double correlation(const QVector<double> &a, const QVector<double> &b) {
    if (a.size() != b.size() || a.size() < 2) return 0;
    double sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        sumA += a[i]; sumB += b[i];
        sumAA += a[i] * a[i]; sumBB += b[i] * b[i]; sumAB += a[i] * b[i];
    }
    const double numerator = a.size() * sumAB - sumA * sumB;
    const double variance = (a.size() * sumAA - sumA * sumA) * (b.size() * sumBB - sumB * sumB);
    return variance > 0 ? numerator / std::sqrt(variance) : 0;
}

QVector<double> flatSurfaceMask(const QImage &clean, const QImage &worn) {
    constexpr int side = 280;
    const QImage before = clean.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage after = worn.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QVector<double> mask;
    if (before.isNull() || after.isNull()) return mask;
    for (int y = side / 5; y < side * 4 / 5; ++y) {
        for (int x = side / 5; x < side * 4 / 5; ++x) {
            const QRgb a = before.pixel(x, y), b = after.pixel(x, y);
            // Exclude composition boundaries: a changed value here must come
            // from the surface treatment on an otherwise uniform source patch.
            if (before.pixel(x - 2, y) != a || before.pixel(x + 2, y) != a
                || before.pixel(x, y - 2) != a || before.pixel(x, y + 2) != a) continue;
            mask.append(std::max({std::abs(qRed(a) - qRed(b)),
                std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))}));
        }
    }
    return mask;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace ArtworkPrint;
    const QVector<Style> printStyles{Style::CleanInk, Style::Halftone, Style::WornPrint};
    const QImage source = texturedArt();
    const QImage sourceBefore = source.copy();
    const QImage alphaSource = texturedArt(false, true);
    const QImage gray = texturedArt(true);

    check(render(alphaSource, options(Style::Original, 0, 1)).image == alphaSource,
          "Original preserves exact input pixels, transparency and dimensions");
    bool dimensionsAlpha = true, deterministic = true, grayNeutral = true, validPalettes = true;
    QVector<QImage> rendered;
    for (Style style : printStyles) {
        const auto first = render(source, options(style));
        const auto again = render(source, options(style));
        rendered.append(first.image);
        const auto withAlpha = render(alphaSource, options(style));
        dimensionsAlpha &= first.image.size() == source.size() && sameAlpha(alphaSource, withAlpha.image);
        deterministic &= first.image == again.image && first.palette == again.palette;
        grayNeutral &= neutral(render(gray, options(style)).image);
        validPalettes &= !first.palette.isEmpty();
        for (const auto &color : first.palette) validPalettes &= color.isValid();
    }
    check(dimensionsAlpha, "all print styles preserve output dimensions and each pixel's alpha");
    check(deterministic, "identical input and settings produce identical images and palettes");
    check(grayNeutral, "grayscale source stays neutral in every print style");
    check(validPalettes, "opaque artwork yields a usable palette of valid colors");
    check(source == sourceBefore, "rendering leaves the caller's shared source image unchanged");
    check(colorDifference(rendered[0], rendered[1]) > .5 && colorDifference(rendered[0], rendered[2]) > .5
          && colorDifference(rendered[1], rendered[2]) > .5,
          "Clean Ink, Halftone and Worn Print produce numerically distinct outputs");

    bool zeroTexture = true;
    for (double detail : {0.0, .55, 1.0}) {
        const auto clean = render(source, options(Style::CleanInk, detail, 0));
        for (Style style : {Style::Halftone, Style::WornPrint}) {
            const auto noTexture = render(source, options(style, detail, 0));
            zeroTexture &= clean.image == noTexture.image && clean.palette == noTexture.palette;
        }
        for (double dotSize : {0.0, 1.0}) {
            const auto monoOff = render(source, options(Style::Halftone, detail, 0, dotSize, true));
            zeroTexture &= clean.image == monoOff.image && clean.palette == monoOff.palette;
        }
    }
    check(zeroTexture, "zero texture yields exact Clean Ink at every detail, including monochrome and dot-size extremes");

    bool monoNeutral = true, monoAlpha = true;
    for (double texture : {.15, .35, 1.0}) {
        const auto mono = render(alphaSource, options(Style::Halftone, .55, texture, .7, true));
        monoNeutral &= neutral(mono.image);
        monoAlpha &= sameAlpha(alphaSource, mono.image);
    }
    check(monoNeutral && monoAlpha,
          "positive-texture monochrome halftone stays neutral and preserves each pixel's alpha");

    QImage midtoneDots(600, 600, QImage::Format_ARGB32);
    midtoneDots.fill(qRgb(160, 160, 160));
    const QImage smallDots = render(midtoneDots, options(Style::Halftone, .55, 1, 0, true)).image;
    const QImage largeDots = render(midtoneDots, options(Style::Halftone, .55, 1, 1, true)).image;
    const auto smallGeometry = dotGeometry(smallDots), largeGeometry = dotGeometry(largeDots);
    printf("Halftone dots: small %d components, median %dpx, %d transitions; large %d components, median %dpx, %d transitions\n",
           smallGeometry.components, smallGeometry.medianArea, smallGeometry.transitions,
           largeGeometry.components, largeGeometry.medianArea, largeGeometry.transitions);
    check(smallGeometry.components > 10 && largeGeometry.components > 10
          && largeGeometry.medianArea >= 2 * smallGeometry.medianArea
          && largeGeometry.transitions < .7 * smallGeometry.transitions,
          "larger dot size forms larger connected ink dots with wider spacing");
    int darkInk = 0, lightPaper = 0;
    for (int y = 0; y < largeDots.height(); ++y)
        for (int x = 0; x < largeDots.width(); ++x) {
            const int value = qGray(largeDots.pixel(x, y));
            if (value < 50) ++darkInk;
            if (value > 210) ++lightPaper;
        }
    check(darkInk > largeDots.width() * largeDots.height() / 10
          && lightPaper > largeDots.width() * largeDots.height() / 10,
          "full-strength monochrome halftone visibly separates dark ink from light paper");

    bool opticalColor = true;
    double largestMeanError = 0;
    for (const QRgb color : {qRgb(32, 32, 32), qRgb(96, 96, 96), qRgb(160, 160, 160), qRgb(224, 224, 224),
                            qRgb(210, 55, 60), qRgb(40, 160, 70), qRgb(40, 80, 205), qRgb(205, 140, 65)}) {
        QImage swatch(400, 400, QImage::Format_ARGB32); swatch.fill(color);
        const QImage printed = render(swatch, options(Style::Halftone, .55, 1, .55)).image;
        const auto mean = meanColor(printed);
        const double error = std::max({std::abs(mean[0] - qRed(color)), std::abs(mean[1] - qGreen(color)),
                                       std::abs(mean[2] - qBlue(color))});
        largestMeanError = std::max(largestMeanError, error);
        opticalColor &= !printed.isNull() && error <= 30;
    }
    printf("Halftone optical swatches: largest mean-channel error %.2f/255\n", largestMeanError);
    check(opticalColor, "full-strength color screens retain approximate source tone and color when averaged");

    bool otherStylesUnchanged = true;
    for (Style style : {Style::Original, Style::CleanInk, Style::WornPrint}) {
        const auto baseline = render(source, options(style));
        for (double dotSize : {0.0, 1.0}) {
            const auto changed = render(source, options(style, .55, .35, dotSize, true));
            otherStylesUnchanged &= changed.image == baseline.image && changed.palette == baseline.palette;
        }
    }
    check(otherStylesUnchanged, "halftone dot size and monochrome controls leave Original, Clean Ink and Worn Print unchanged");

    // A bright visor/highlight must not put white speckles into the subject's
    // interior shadows. The outer edges may now expose lighter paper by design.
    QImage darkArt(160, 120, QImage::Format_ARGB32);
    darkArt.fill(qRgb(9, 13, 17));
    const QRect highlight(106, 24, 28, 20);
    for (int y = highlight.top(); y <= highlight.bottom(); ++y)
        for (int x = highlight.left(); x <= highlight.right(); ++x)
            darkArt.setPixel(x, y, qRgb(231, 242, 245));
    const QImage cleanDark = render(darkArt, options(Style::CleanInk)).image;
    const QRect excludeHighlight = highlight.adjusted(-12, -12, 12, 12);
    for (double texture : {.35, 1.0}) {
        const QImage wornDark = render(darkArt, options(Style::WornPrint, .55, texture)).image;
        int brightOutliers = 0, inspected = 0, maximumLift = 0;
        if (wornDark.size() == darkArt.size() && cleanDark.size() == darkArt.size()) {
            for (int y = darkArt.height() * 3 / 10; y < darkArt.height() * 7 / 10; ++y) {
                for (int x = darkArt.width() * 3 / 10; x < darkArt.width() * 7 / 10; ++x) {
                    if (excludeHighlight.contains(x, y)) continue;
                    const int lift = qGray(wornDark.pixel(x, y)) - qGray(cleanDark.pixel(x, y));
                    maximumLift = std::max(maximumLift, lift);
                    if (lift > 32) ++brightOutliers;
                    ++inspected;
                }
            }
        }
        printf("Dark-region texture %.2f: %d bright outliers across %d pixels; maximum lift %d/255\n",
               texture, brightOutliers, inspected, maximumLift);
        check(inspected > 2000 && brightOutliers == 0,
              texture == .35 ? "default Worn Print protects interior shadows from bright speckle outliers"
                             : "maximum Worn Print texture protects interior shadows from bright speckle outliers");
    }

    // A flat colored surround distinguishes physical surface variation from a
    // palette shift. Wear may reach the subject now, but must remain restrained
    // while being visible beyond the rim at normal display sizes.
    QImage cover(800, 800, QImage::Format_ARGB32);
    cover.fill(qRgb(52, 98, 140));
    for (int y = 240; y < 560; ++y)
        for (int x = 280; x < 520; ++x) cover.setPixel(x, y, qRgb(184, 126, 92));
    const QImage cleanCover = render(cover, options(Style::CleanInk)).image;
    QVector<WearMetrics> cardWear, thumbnailWear;
    QVector<QImage> wornCovers;
    for (double texture : {0.0, .35, 1.0}) {
        const QImage worn = render(cover, options(Style::WornPrint, .55, texture)).image;
        wornCovers.append(worn);
        const auto card = displayedWear(cleanCover, worn, 280), thumbnail = displayedWear(cleanCover, worn, 64);
        cardWear.append(card); thumbnailWear.append(thumbnail);
        printf("Wear texture %.2f: 280px edge spread %d/255, chip %dpx, fade %dpx, center mean %.2f/255, "
               "strong center pixels %d/%d, surface mean %.2f/255, surface patch %dpx; "
               "64px spread %d/255, chip %dpx\n", texture, card.edgeSpread, card.edgePatch, card.fadedPatch,
               card.centerSamples ? card.centerDifference / card.centerSamples : 0,
               card.centerStrongPixels, card.centerSamples,
               card.surfaceSamples ? card.surfaceDifference / card.surfaceSamples : 0, card.surfacePatch,
               thumbnail.edgeSpread, thumbnail.edgePatch);
    }
    check(cardWear[1].edgeSpread >= 12 && cardWear[1].edgePatch >= 16
          && cardWear[2].edgeSpread >= 12 && cardWear[2].edgePatch >= 16,
          "default and maximum Worn texture produce connected edge chips at 280px card size");
    check(thumbnailWear[1].edgeSpread >= 6 && thumbnailWear[1].edgePatch >= 4
          && thumbnailWear[2].edgeSpread >= 6 && thumbnailWear[2].edgePatch >= 4,
          "default and maximum edge wear survive reduction to a 64px thumbnail");
    check(cardWear[0].totalDifference == 0 && cardWear[1].totalDifference > 0
          && cardWear[2].totalDifference > cardWear[1].totalDifference,
          "texture strength increases localized wear while zero texture stays clean");
    check(cardWear[1].surfaceSamples > 0 && cardWear[2].surfaceSamples > 0
          && cardWear[1].surfaceDifference / cardWear[1].surfaceSamples >= .15
          && cardWear[2].surfaceDifference / cardWear[2].surfaceSamples >= .15
          && cardWear[1].surfacePatch >= 16 && cardWear[2].surfacePatch >= 16,
          "default and maximum wear leave connected surface marks beyond the outer rim");
    check(cardWear[1].fadedPatch >= 16 && cardWear[2].fadedPatch >= 16,
          "rubbed color forms connected faded patches beyond the chipped rim");

    check(cardWear[1].centerSamples > 0 && cardWear[2].centerSamples > 0
          && cardWear[1].centerDifference / cardWear[1].centerSamples <= 12
          && cardWear[2].centerDifference / cardWear[2].centerSamples <= 12
          && cardWear[1].centerStrongPixels <= cardWear[1].centerSamples / 200
          && cardWear[2].centerStrongPixels <= cardWear[2].centerSamples / 200,
          "surface wear keeps central contrast restrained without dense high-contrast speckles");

    // A moved color marker changes content while preserving dimensions and
    // every color's count. The observed center is identical in both originals,
    // so a different mask there cannot be explained by a different palette or
    // nearby image edges. Strength must preserve placement for one source.
    QImage coverA = cover.copy(), coverB = cover.copy();
    for (int y = 80; y < 128; ++y)
        for (int x = 40; x < 88; ++x) {
            coverA.setPixel(x, y, qRgb(184, 126, 92));
            coverB.setPixel(x + 80, y, qRgb(184, 126, 92));
        }
    const auto cleanA = render(coverA, options(Style::CleanInk));
    const auto cleanB = render(coverB, options(Style::CleanInk));
    const auto wornA = render(coverA, options(Style::WornPrint, .55, 1));
    const auto wornB = render(coverB, options(Style::WornPrint, .55, 1));
    const auto maskA = flatSurfaceMask(cleanA.image, wornA.image), maskB = flatSurfaceMask(cleanB.image, wornB.image);
    int differentMaskPixels = 0;
    if (maskA.size() == maskB.size())
        for (qsizetype i = 0; i < maskA.size(); ++i)
            if (std::abs(maskA[i] - maskB[i]) >= 2) ++differentMaskPixels;
    const QRect unchangedInterior(160, 160, 480, 480);
    check(cleanA.palette == cleanB.palette && cleanA.image.copy(unchangedInterior) == cleanB.image.copy(unchangedInterior)
          && maskA.size() > 10000 && maskA.size() == maskB.size() && differentMaskPixels > maskA.size() / 100,
          "different content with the same palette produces a different wear mask on unchanged flat regions");
    const auto defaultMask = flatSurfaceMask(cleanCover, wornCovers[1]);
    const auto maximumMask = flatSurfaceMask(cleanCover, wornCovers[2]);
    const double strengthCorrelation = correlation(defaultMask, maximumMask);
    printf("Surface mask: %d/%lld pixels differ between equal-palette sources; strength correlation %.4f\n",
           differentMaskPixels, static_cast<long long>(maskA.size()), strengthCorrelation);
    check(strengthCorrelation >= .75,
          "raising texture strength retains the same source's surface-wear placement");
    const auto lowDetailMask = flatSurfaceMask(cleanA.image, render(coverA, options(Style::WornPrint, 0, 1)).image);
    const auto highDetailMask = flatSurfaceMask(cleanA.image, render(coverA, options(Style::WornPrint, 1, 1)).image);
    const double detailCorrelation = correlation(lowDetailMask, highDetailMask);
    printf("Surface mask detail correlation %.4f\n", detailCorrelation);
    check(detailCorrelation >= .75,
          "changing detail retains wear placement on the same flat source regions");

    bool smallSafe = true;
    for (Style style : {Style::Original, Style::CleanInk, Style::Halftone, Style::WornPrint}) {
        const auto empty = render({}, options(style));
        smallSafe &= empty.image.isNull() && empty.palette.isEmpty();
        for (const QSize size : {QSize(1, 1), QSize(1, 9), QSize(9, 1), QSize(2, 2)}) {
            QImage tiny(size, QImage::Format_ARGB32);
            tiny.fill(qRgba(90, 130, 180, 127));
            const auto first = render(tiny, options(style));
            smallSafe &= sameAlpha(tiny, first.image) && first.image == render(tiny, options(style)).image;
        }
    }
    // This input seeds a paper-noise gradient with exactly 1.0. Under ASan it
    // catches treating that endpoint as an index past the gradient table.
    QImage gradientEndpoint(1, 1, QImage::Format_ARGB32);
    gradientEndpoint.fill(QRgb(0xf842418e));
    const auto endpointResult = render(gradientEndpoint, options(Style::WornPrint));
    smallSafe &= !endpointResult.image.isNull() && sameAlpha(gradientEndpoint, endpointResult.image);
    check(smallSafe, "empty and tiny images are deterministic with valid dimensions and alpha");
    QImage transparent(11, 7, QImage::Format_ARGB32);
    transparent.fill(qRgba(200, 100, 50, 0));
    bool transparentSafe = true;
    for (Style style : printStyles) {
        const auto first = render(transparent, options(style));
        transparentSafe &= sameAlpha(transparent, first.image) && first.image == render(transparent, options(style)).image;
    }
    check(transparentSafe, "fully transparent inputs stay transparent and stable");

    QImage preciseAlpha(5, 2, QImage::Format_RGBA64);
    const quint16 coverages[] = {0, 1, 12345, 32769, 65535};
    for (int y = 0; y < preciseAlpha.height(); ++y)
        for (int x = 0; x < preciseAlpha.width(); ++x)
            preciseAlpha.setPixelColor(x, y, QColor::fromRgba64(42000, 14000, 31000, coverages[x]));
    bool alphaPrecision = render(preciseAlpha, options(Style::Original)).image == preciseAlpha;
    bool premultipliedSafe = true;
    const QImage premultiplied = alphaSource.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (Style style : printStyles) {
        const QImage result = render(preciseAlpha, options(style)).image;
        alphaPrecision &= result.size() == preciseAlpha.size();
        if (result.size() == preciseAlpha.size())
            for (int y = 0; y < result.height(); ++y)
                for (int x = 0; x < result.width(); ++x)
                    alphaPrecision &= result.pixelColor(x, y).rgba64().alpha() == coverages[x];
        premultipliedSafe &= sameAlpha(premultiplied, render(premultiplied, options(style)).image);
    }
    check(alphaPrecision, "higher-depth inputs preserve exact 16-bit alpha, including nearly transparent coverage");
    check(premultipliedSafe, "premultiplied input keeps its original alpha coverage in every print style");

    QImage floatingAlpha(4, 2, QImage::Format_RGBA32FPx4);
    const float fractionalCoverages[] = {0.0f, 0.00000001f, 0.123456789f, 1.0f};
    for (int y = 0; y < floatingAlpha.height(); ++y) {
        auto *row = reinterpret_cast<float *>(floatingAlpha.scanLine(y));
        for (int x = 0; x < floatingAlpha.width(); ++x) {
            row[x * 4] = .2f; row[x * 4 + 1] = .4f; row[x * 4 + 2] = .8f;
            row[x * 4 + 3] = fractionalCoverages[x];
        }
    }
    bool fractionalAlpha = render(floatingAlpha, options(Style::Original)).image == floatingAlpha;
    for (Style style : printStyles) {
        const auto result = render(floatingAlpha, options(style)).image.convertToFormat(QImage::Format_RGBA32FPx4);
        fractionalAlpha &= result.size() == floatingAlpha.size();
        if (result.size() == floatingAlpha.size())
            for (int y = 0; y < result.height(); ++y) {
                const auto *row = reinterpret_cast<const float *>(result.constScanLine(y));
                for (int x = 0; x < result.width(); ++x) fractionalAlpha &= row[x * 4 + 3] == fractionalCoverages[x];
            }
    }
    check(fractionalAlpha, "floating-point input retains fractional alpha without integer quantization");

    bool clamped = true, finiteFallback = true;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (Style style : printStyles) {
        clamped &= render(source, options(style, -4, 3)).image == render(source, options(style, 0, 1)).image;
        clamped &= render(source, options(style, 5, -2)).image == render(source, options(style, 1, 0)).image;
        clamped &= render(source, options(style, .55, 1, -3, true)).image
            == render(source, options(style, .55, 1, 0, true)).image;
        clamped &= render(source, options(style, .55, 1, 4, true)).image
            == render(source, options(style, .55, 1, 1, true)).image;
        for (double invalid : {nan, infinity, -infinity}) {
            finiteFallback &= render(source, options(style, invalid, .6)).image == render(source, options(style, .55, .6)).image;
            finiteFallback &= render(source, options(style, .2, invalid)).image == render(source, options(style, .2, .35)).image;
            finiteFallback &= render(source, options(style, .55, 1, invalid, true)).image
                == render(source, options(style, .55, 1, .35, true)).image;
        }
    }
    check(clamped, "finite controls outside their range clamp to the nearest endpoint");
    check(finiteFallback, "NaN and infinite controls use the documented finite defaults");

    const QImage lowDetail = render(source, options(Style::CleanInk, 0, 0)).image;
    const QImage highDetail = render(source, options(Style::CleanInk, 1, 0)).image;
    check(colorDifference(lowDetail, highDetail) > .5, "detail control materially changes the simplification");
    QImage edge(64, 48, QImage::Format_ARGB32);
    for (int y = 0; y < edge.height(); ++y)
        for (int x = 0; x < edge.width(); ++x)
            edge.setPixel(x, y, x < edge.width() / 2 ? qRgb(25, 35, 45) : qRgb(220, 230, 240));
    bool edgesRetained = true;
    for (double detail : {0.0, .55, 1.0})
        edgesRetained &= strongCentralEdge(render(edge, options(Style::CleanInk, detail, 0)).image);
    check(edgesRetained, "simplification retains a strong boundary in its original position");

    QVector<QFuture<Result>> pending;
    for (Style style : printStyles)
        pending.append(QtConcurrent::run([source, style] { return render(source, options(style)); }));
    bool concurrentStable = true;
    for (qsizetype i = 0; i < pending.size(); ++i)
        concurrentStable &= pending[i].result().image == rendered[i];
    check(concurrentStable && source == sourceBefore,
          "concurrent worker renders match serial results without mutating shared source pixels");
    printf("Artwork print fixture: %d checks, %d failures; generated pixels only\n", checks, failures);
    return failures ? 1 : 0;
}
