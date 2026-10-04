#include "GrungeMaskProvider.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRunnable>
#include <QThreadPool>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ── Noise engine — exact port of the QML port of GrungeNoise
//    (GrungeFrame.swift / ui_grunge.c). All 32-bit unsigned math. ──

static inline double noiseAt(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = seed ^ (uint32_t(x) * 374761393u) ^ (uint32_t(y) * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return double(h & 0xFFFF) / 65535.0;
}

static inline double smoothNoise1D(double t, int octave, uint32_t seed) {
    const double i = std::floor(t);
    const double frac = t - i;
    const double s = frac * frac * (3.0 - 2.0 * frac);
    const double a = noiseAt(int32_t(i), octave, seed);
    const double b = noiseAt(int32_t(i) + 1, octave, seed);
    return a + s * (b - a);
}

static inline double fbmNoise1D(double t, int octave, uint32_t seed) {
    double val = 0.0, amp = 1.0, freq = 1.0, totalAmp = 0.0;
    for (int o = 0; o < 3; o++) {
        val += amp * (smoothNoise1D(t * freq, octave + o * 7, seed) - 0.5);
        totalAmp += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return val / totalAmp;
}

static inline double rectSDF(double px, double py, double w, double h, double inset) {
    return std::min(std::min(px - inset, (w - inset) - px),
                    std::min(py - inset, (h - inset) - py));
}

static inline double roundedRectSDF(double px, double py, double w, double h,
                                    double inset, double cornerRadius) {
    const double r = std::min(cornerRadius, std::min(w, h) * 0.5 - inset);
    if (r <= 0)
        return rectSDF(px, py, w, h, inset);
    const double dLeft = px - inset, dRight = (w - inset) - px;
    const double dTop = py - inset, dBottom = (h - inset) - py;
    const bool inL = dLeft < r, inR = dRight < r, inT = dTop < r, inB = dBottom < r;
    double cx, cy;
    if (inL && inT)      { cx = inset + r;     cy = inset + r; }
    else if (inR && inT) { cx = w - inset - r; cy = inset + r; }
    else if (inL && inB) { cx = inset + r;     cy = h - inset - r; }
    else if (inR && inB) { cx = w - inset - r; cy = h - inset - r; }
    else return std::min(std::min(dLeft, dRight), std::min(dTop, dBottom));
    const double dx = px - cx, dy = py - cy;
    return r - std::sqrt(dx * dx + dy * dy);
}

static inline int nearestEdge(double px, double py, double w, double h) {
    const double d[4] = { py, w - px, h - py, px };
    int minI = 0;
    for (int i = 1; i < 4; i++)
        if (d[i] < d[minI])
            minI = i;
    return minI;
}

static inline double edgeParam(double px, double py, double w, double h, int edge) {
    if (edge == 0) return px / w;
    if (edge == 1) return py / h;
    if (edge == 2) return 1.0 - px / w;
    return 1.0 - py / h;
}

// ── Mask generation — GrungeMaskGenerator.generate ──

static QImage generateMask(int w, int h, bool grungy, int roughness,
                           int splatter, int cr, uint32_t seed,
                           bool edgeOnly = false, bool circle = false) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    if (!grungy) {
        // Clean: the mask is just the rounded rect
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        if (circle)
            path.addEllipse(0, 0, w, h);
        else
            path.addRoundedRect(0, 0, w, h, cr, cr);
        p.fillPath(path, Qt::white);
        return img;
    }

    const double inset = roughness * 0.5;
    const double noiseScale = 0.08;
    const double sprayBand = roughness * 1.8;

    std::vector<double> alphaBuf(size_t(w) * h, 0.0);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double sdf, t, edgeLen;
            int edge;
            if (circle) {
                // True circle: rounded-rect SDF can't make one (cr
                // clamps to half-size minus inset → flat squircle
                // segments). Radial SDF; boundary param = angle, so
                // the FBM displacement wanders AROUND the rim.
                const double cx = w * 0.5, cy = h * 0.5;
                const double R = std::min(w, h) * 0.5 - inset;
                const double dx = x - cx, dy = y - cy;
                sdf = R - std::sqrt(dx * dx + dy * dy);
                t = (std::atan2(dy, dx) + M_PI) / (2.0 * M_PI);
                edgeLen = 2.0 * M_PI * R;
                edge = 0;
            } else {
                sdf = roundedRectSDF(x, y, w, h, inset, cr);
                edge = nearestEdge(x, y, w, h);
                t = edgeParam(x, y, w, h, edge);
                edgeLen = (edge == 0 || edge == 2) ? w : h;
            }
            const double disp =
                fbmNoise1D(t * edgeLen * noiseScale, edge * 13, seed) * roughness;
            const double dist = sdf + disp;

            if (edgeOnly) {
                // Colored spray rim. The ENTIRE band — speckle zone,
                // falloff, and feather tail — is scaled by a slow noise
                // along the edge (0.35x–2.1x, ~130px wavelength): thick
                // painted clumps in some stretches, thin whisps in
                // others. Feather fades the tint into the interior over
                // ~0.9 band-widths with grain riding it — no hard stop.
                const double widthMod = 0.35 + 1.4 * smoothNoise1D(
                    t * edgeLen * 0.008, edge * 29, seed + 777u);
                const double d = dist / widthMod;
                double a;
                if (d > sprayBand * 0.4) {
                    // Shorter inward feather (0.6 band) — the deep tail
                    // was flooding under panel text
                    const double fade = 1.0 - std::clamp(
                        (d - sprayBand * 0.4) / (sprayBand * 0.6), 0.0, 1.0);
                    a = fade <= 0.0 ? 0.0
                        : fade * fade * (0.35 + 0.65 * noiseAt(x, y, seed + 1234u));
                } else if (d < -sprayBand * 0.6) {
                    a = 0.0;
                } else {
                    const double falloff =
                        1.0 / (1.0 + std::exp(-d / (roughness * 0.3)));
                    const double particle = noiseAt(x * 3, y * 3, seed + 777u);
                    a = (particle < falloff)
                        ? falloff * (0.5 + 0.5 * noiseAt(x, y, seed + 999u))
                        : 0.0;
                }
                alphaBuf[size_t(y) * w + x] = std::clamp(a, 0.0, 1.0);
                continue;
            }

            double alpha;
            if (dist > sprayBand * 0.4) {
                const double grain = noiseAt(x, y, seed + 1234u);
                alpha = 0.97 + grain * 0.03;
                const double hole = noiseAt(x, y, seed + 4444u);
                if (hole > 0.995)
                    alpha *= 0.5;
            } else if (dist < -sprayBand * 0.6) {
                alpha = 0.0;
            } else {
                const double falloff = 1.0 / (1.0 + std::exp(-dist / (roughness * 0.3)));
                const double particle = noiseAt(x * 3, y * 3, seed + 777u);
                if (particle < falloff)
                    alpha = falloff * (0.5 + 0.5 * noiseAt(x, y, seed + 999u));
                else
                    alpha = 0.0;
                const double cluster = noiseAt(x / 3, y / 3, seed + 333u);
                if (cluster > 0.85 && dist > -sprayBand * 0.3) {
                    const double ca = (1.0 - (0.85 - cluster) / 0.15) * falloff;
                    if (ca > alpha)
                        alpha = ca;
                }
            }
            alphaBuf[size_t(y) * w + x] = std::clamp(alpha, 0.0, 1.0);
        }
    }

    // Splatter dots pass
    for (int i = 0; i < splatter; i++) {
        const double nx = noiseAt(i, 50, seed);
        const double nr = noiseAt(i, 52, seed);
        const double na = noiseAt(i, 53, seed);
        const double nd = noiseAt(i, 54, seed);
        const int side = i % 4;
        const double edgeDist = nd * sprayBand;
        double x, y;
        if (circle) {
            const double ang = nx * 2.0 * M_PI;
            const double de = inset + roughness * 0.5 - edgeDist;
            const double rr = std::min(w, h) * 0.5 - de;
            x = w * 0.5 + std::cos(ang) * rr;
            y = h * 0.5 + std::sin(ang) * rr;
        }
        else if (side == 0) { x = nx * w; y = inset + roughness * 0.5 - edgeDist; }
        else if (side == 1) { y = nx * h; x = w - inset - roughness * 0.5 + edgeDist; }
        else if (side == 2) { x = nx * w; y = h - inset - roughness * 0.5 + edgeDist; }
        else                { y = nx * h; x = inset + roughness * 0.5 - edgeDist; }

        const double distFactor = 1.0 - edgeDist / sprayBand;
        if (distFactor < 0.05)
            continue;
        const double radius = 0.5 + nr * 1.5 * distFactor;
        const double dotAlpha = (0.3 + na * 0.7) * distFactor;
        const double r2 = radius * radius;
        const int minX = std::max(0, int(std::floor(x - radius)));
        const int maxX = std::min(w - 1, int(std::ceil(x + radius)));
        const int minY = std::max(0, int(std::floor(y - radius)));
        const int maxY = std::min(h - 1, int(std::ceil(y + radius)));
        for (int dy = minY; dy <= maxY; dy++) {
            for (int dx = minX; dx <= maxX; dx++) {
                const double ddx = dx - x, ddy = dy - y;
                if (ddx * ddx + ddy * ddy <= r2) {
                    double &existing = alphaBuf[size_t(dy) * w + dx];
                    existing = std::min(existing + dotAlpha * (1.0 - existing), 1.0);
                }
            }
        }
    }

    for (int y = 0; y < h; y++) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; x++) {
            const int a = int(std::lround(alphaBuf[size_t(y) * w + x] * 255.0));
            line[x] = qRgba(255, 255, 255, a);
        }
    }
    return img;
}

// ── Async plumbing ──

namespace {

class MaskResponse : public QQuickImageResponse, public QRunnable {
public:
    explicit MaskResponse(const QString &id) : m_id(id) {
        setAutoDelete(false);
    }

    void run() override {
        // id: "<w>x<h>?g=..&r=..&s=..&cr=..&seed=.."
        const int q = m_id.indexOf('?');
        const QString dims = q >= 0 ? m_id.left(q) : m_id;
        const QUrlQuery query(q >= 0 ? m_id.mid(q + 1) : QString());
        const int xi = dims.indexOf('x');
        const int w = std::clamp(dims.left(xi).toInt(), 1, 4096);
        const int h = std::clamp(dims.mid(xi + 1).toInt(), 1, 4096);
        const bool grungy = query.queryItemValue("g").toInt() != 0;
        const bool edgeOnly = query.queryItemValue("edge").toInt() != 0;
        const bool circle = query.queryItemValue("circle").toInt() != 0;
        const int roughness = std::clamp(query.queryItemValue("r").toInt(), 1, 64);
        const int splatter = std::clamp(query.queryItemValue("s").toInt(), 0, 1000);
        const int cr = std::clamp(query.queryItemValue("cr").toInt(), 0, 64);
        uint32_t seed = query.queryItemValue("seed").toUInt();
        if (seed == 0)
            seed = 42;

        m_image = generateMask(w, h, grungy, roughness, splatter, cr,
                               seed, edgeOnly, circle);
        emit finished();
    }

    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QString m_id;
    QImage m_image;
};

} // namespace

QQuickImageResponse *GrungeMaskProvider::requestImageResponse(
    const QString &id, const QSize &) {
    auto *response = new MaskResponse(id);
    QThreadPool::globalInstance()->start(response);
    return response;
}
