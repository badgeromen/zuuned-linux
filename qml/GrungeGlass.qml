import QtQuick
import Qt5Compat.GraphicalEffects

// Glass + Grunge panel — port of GlassGrungeView.swift + GrungeFrame.swift.
//
//   Glass = the structure. Clean, functional, where data lives.
//   Grunge = the soul. The human layer on the machine.
//
// Clean mode (grungy: false): pristine glass, smooth rounded corners.
// Dirty mode (grungy: true, device connected): the grunge mask eats into
// the panel edges (per-pixel FBM spray stencil, ported from the Swift
// noise engine) and oil smears appear on the glass. Grunge level
// auto-escalates one tier, exactly like the mac's `escalated`.
//
// Usage:
//   GrungeGlass {
//       tier: "accent"; grungeLevel: "medium"; seed: 42
//       grungy: DeviceService.connected
//       ... content children ...
//   }
Item {
    id: root

    // "primary" | "secondary" | "accent"
    property string tier: "primary"
    // "subtle" | "medium" | "heavy" | "feral"
    property string grungeLevel: "medium"
    property int seed: 42
    // Device connected — escalate grunge, rough the edges, smear the glass
    property bool grungy: false
    // Device mood color (accent tier tint + smear staining)
    property color deviceColor: "transparent"

    property int cornerRadius: tier === "primary" ? Theme.radiusXl : Theme.radiusLg

    default property alias contentData: contentItem.data

    // ── GrungeLevel tables (GlassGrungeView.swift) ──
    readonly property var _levels: ({
        subtle: { roughness: 6,  splatter: 30 },
        medium: { roughness: 12, splatter: 80 },
        heavy:  { roughness: 22, splatter: 180 },
        feral:  { roughness: 32, splatter: 300 }
    })
    readonly property var _escalate: ({
        subtle: "heavy", medium: "feral", heavy: "feral", feral: "feral"
    })
    readonly property string _activeLevel: grungy ? _escalate[grungeLevel] : grungeLevel
    readonly property int _roughness: _levels[_activeLevel].roughness
    readonly property int _splatter: _levels[_activeLevel].splatter
    // The eaten-edge depth scales with roughness (band ≈ 1.8×) — at
    // heavy/feral that reaches 40–60px, under the content. Cap the
    // MASK's roughness so damage stays at the rim where text never
    // sits; splatter density still comes from the full level.
    // Grunge derates with surface size: roughness/splatter are
    // absolute pixels, so a 64px rail with heavy roughness 22 wears
    // ~40px of spray PER SIDE — the edges overlap and the whole strip
    // reads as stretched grime. Basis uses the LATCHED buckets so the
    // params hold still while frozen (mid-animation) and settle once.
    readonly property int _sizeBasis: Math.min(_bucketW, _bucketH)
    readonly property int _effRoughness:
        Math.min(_roughness, Math.max(4, Math.round(_sizeBasis / 10)))
    readonly property int _effSplatter:
        Math.round(_splatter * Math.min(1.0, _sizeBasis / 200))
    readonly property int _maskRoughness: Math.min(_effRoughness, 14)
    readonly property bool _hasMood: deviceColor.a > 0

    // ── Tier-resolved colors (borderGradient / glowColor / accentTint) ──
    readonly property var _borderStops: {
        if (tier === "accent") {
            if (_hasMood)
                return [Qt.rgba(deviceColor.r, deviceColor.g, deviceColor.b, 0.65),
                        Qt.rgba(deviceColor.r, deviceColor.g, deviceColor.b, 0.30),
                        Qt.rgba(1.0, 0.55, 0.0, 0.15)]
            return [Qt.rgba(0.83, 0.21, 0.48, 0.45), Qt.rgba(1.0, 0.55, 0.0, 0.20)]
        }
        if (tier === "secondary")
            return [Qt.rgba(1, 1, 1, 0.12), Qt.rgba(1, 1, 1, 0.04)]
        return [Theme.glassHighlight, Theme.glassBorder, Theme.glassBorderSubtle]
    }
    // Accent border runs diagonally (topLeading→bottomTrailing), others vertical
    readonly property bool _borderDiagonal: tier === "accent"

    readonly property color _glowColor: {
        if (tier === "accent")
            return _hasMood ? Qt.rgba(deviceColor.r, deviceColor.g, deviceColor.b, 0.30)
                            : Qt.rgba(0.83, 0.21, 0.48, 0.25)
        return tier === "primary" ? Qt.rgba(0, 0, 0, 0.5) : Qt.rgba(0, 0, 0, 0.3)
    }

    // ── Layer 0: shadow (glow) ──
    // The effect stage is PADDED beyond the panel so the blur can
    // breathe — an effect item sized exactly to the source truncates
    // the glow at the rect and reads as harsh square edges. The proxy
    // inside is centered at true panel size (source and effect share
    // geometry, so nothing rescales).
    Item {
        id: shadowProxy
        anchors.fill: parent
        anchors.margins: -36
        visible: false
        Rectangle {
            anchors.centerIn: parent
            width: root.width
            height: root.height
            radius: root.cornerRadius
            color: "black"
        }
    }
    DropShadow {
        anchors.fill: shadowProxy
        source: shadowProxy
        radius: root.grungy ? 20 : 12
        samples: 25
        verticalOffset: 4
        color: root._glowColor
        cached: true
    }

    // ── Layer 1: dark fill behind the glass ──
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.bg
        // Grungy: more translucent than clean so the environment's spray
        // colors seep through the eaten edges (the mac gets this bleed
        // from its blurred material stack)
        opacity: root.grungy ? 0.72 : 0.6
    }

    // ── Layer 2: glass fill (+ accent tint, + oil smears when dirty) ──
    Item {
        id: glassLayer
        anchors.fill: parent
        visible: false // rendered through the OpacityMask

        // Material stand-in: translucent fill with a faint top-lit gradient.
        // (True backdrop blur needs knowledge of what's behind the panel —
        // a self-contained component can't sample it; the mac's material
        // reads nearly like this over zuneBg.)
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, root.tier === "primary" ? 0.09 : 0.05) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, root.tier === "primary" ? 0.05 : 0.03) }
            }
        }

        // Accent tint (GlassAccentModifier)
        Rectangle {
            anchors.fill: parent
            visible: root.tier === "accent"
            rotation: 0
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: root._hasMood
                           ? Qt.rgba(root.deviceColor.r, root.deviceColor.g, root.deviceColor.b, 0.30)
                           : Qt.rgba(0.83, 0.21, 0.48, 0.15)
                }
                GradientStop {
                    position: 0.5
                    color: root._hasMood
                           ? Qt.rgba(root.deviceColor.r, root.deviceColor.g, root.deviceColor.b, 0.15)
                           : Qt.rgba(0.83, 0.21, 0.48, 0.12)
                }
                GradientStop {
                    position: 1.0
                    color: root._hasMood
                           ? Qt.rgba(1.0, 0.55, 0.0, 0.08)
                           : Qt.rgba(1.0, 0.55, 0.0, 0.10)
                }
            }
        }

        // Oil smears (OilSmearOverlay) — dirty mode only
        Canvas {
            id: smearCanvas
            anchors.fill: parent
            visible: root.grungy
            layer.enabled: root.grungy
            layer.effect: FastBlur { radius: 8 }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                if (!root.grungy)
                    return
                const w = width, h = height
                if (w <= 0 || h <= 0)
                    return
                const rand = root._lcg(root.seed + 5555)

                const baseCount = Math.floor(Math.max(3, Math.min(8, (w + h) / 150)))
                const smearCount = baseCount * 3   // aggressive (dirty mode)

                for (let i = 0; i < smearCount; i++) {
                    const side = Math.floor(rand() * 4)
                    const t = 0.05 + rand() * 0.90
                    // Smears hug the rim — deeper placement put dark
                    // blobs under panel text
                    const edgeMin = 5, edgeMax = 28
                    let cx, cy, angle
                    if (side === 0) {
                        cx = t * w; cy = edgeMin + rand() * (edgeMax - edgeMin)
                        angle = -0.4 + rand() * 0.8
                    } else if (side === 1) {
                        cx = w - (edgeMin + rand() * (edgeMax - edgeMin)); cy = t * h
                        angle = Math.PI / 2 + (-0.4 + rand() * 0.8)
                    } else if (side === 2) {
                        cx = t * w; cy = h - (edgeMin + rand() * (edgeMax - edgeMin))
                        angle = -0.4 + rand() * 0.8
                    } else {
                        cx = edgeMin + rand() * (edgeMax - edgeMin); cy = t * h
                        angle = Math.PI / 2 + (-0.4 + rand() * 0.8)
                    }

                    const len = 50 + rand() * 90       // aggressive: 50…140
                    const wid = 6 + rand() * 12        // aggressive: 6…18

                    const useMood = root._hasMood && rand() < 0.6
                    let color
                    if (useMood) {
                        const op = 0.03 + rand() * 0.04
                        color = Qt.rgba(root.deviceColor.r, root.deviceColor.g,
                                        root.deviceColor.b, op)
                    } else {
                        const op = 0.04 + rand() * 0.05
                        color = Qt.rgba(0.05 + rand() * 0.07,
                                        0.04 + rand() * 0.04,
                                        0.02 + rand() * 0.03, op)
                    }

                    ctx.save()
                    ctx.translate(cx, cy)
                    ctx.rotate(angle)
                    ctx.fillStyle = color
                    ctx.beginPath()
                    ctx.ellipse(-len / 2, -wid / 2, len, wid)
                    ctx.fill()
                    ctx.restore()
                }
            }
        }
    }

    // ── Layer 3: grunge alpha mask over the glass ──
    // Generated in C++ on a pool thread (GrungeMaskProvider) — the
    // per-pixel stencil in QML JS froze the UI on every resize. Size is
    // bucketed to 32px so a resize causes at most a few regenerations,
    // each off-thread; the mask stretches ≤32px to fit, invisible for
    // spray noise.
    // While frozen (host is ANIMATING our size) the mask keeps its
    // last buckets and just stretches — regenerating per 32px bucket
    // mid-flight is the "flashy glitchy" collapse. Unfreeze → settle.
    property bool frozen: false
    // Headroom (ux/7): a glass the host resizes VERTICALLY every scroll
    // frame (island growth) must neither regenerate per 32px bucket nor
    // stretch its rim bite. The host passes fullHeight — the glass's
    // collapse-0 height. The mask generates ONCE at that size and the
    // BorderImage below renders it nine-patch: top/bottom caps 1:1
    // (all bite + corner detail lives at the rim), only the uniform
    // interior scales. BorderImage IS QML's native nine-patch.
    property real fullHeight: 0
    // Cap must cover the bite band (≤ _maskRoughness×1.8 ≈ 25px) plus
    // the corner radius; engages only once the glass is tall enough
    // that the two caps can't collide.
    readonly property int _maskCap:
        fullHeight > 0 && height > 140 ? 48 : 0
    readonly property int _liveBW: Math.max(32, Math.ceil(width / 32) * 32)
    readonly property int _liveBH: Math.max(32, Math.ceil(
        (fullHeight > 0 ? fullHeight : height) / 32) * 32)
    property int _bucketW: 32
    property int _bucketH: 32
    on_LiveBWChanged: if (!frozen) _bucketW = _liveBW
    on_LiveBHChanged: if (!frozen) _bucketH = _liveBH
    onFrozenChanged: {
        if (!frozen) {
            _bucketW = _liveBW
            _bucketH = _liveBH
            _schedule()
        }
    }

    // With no fullHeight hint the caps are 0 and a BorderImage is a
    // plain stretch — exactly the old Image.Stretch behavior.
    BorderImage {
        id: maskImage
        // Preserve the last stencil while the async provider builds the next
        // size bucket; otherwise resizing briefly blanks the masked layer.
        retainWhileLoading: true
        anchors.fill: parent
        visible: false
        border.top: root._maskCap
        border.bottom: root._maskCap
        source: root.width > 4 && root.height > 4
                ? "image://grungemask/" + root._bucketW + "x" + root._bucketH
                  + "?g=" + (root.grungy ? 1 : 0)
                  + "&r=" + root._maskRoughness + "&s=" + root._effSplatter
                  + "&cr=" + root.cornerRadius
                  + "&seed=" + (root.seed === 0 ? 42 : root.seed)
                : ""
    }

    // Layer 3.5: COLORED spray band. The chipped edges are spray PAINT,
    // not cutouts: brand pink-to-orange (or the mood color) tinted
    // through an edge-only variant of the grunge mask. This is the mac's
    // color bleed in the grunge borders.
    BorderImage {
        id: edgeMaskImage
        // Preserve the last stencil while the async provider builds the next
        // size bucket; otherwise resizing briefly blanks the masked layer.
        retainWhileLoading: true
        anchors.fill: parent
        visible: false
        border.top: root._maskCap
        border.bottom: root._maskCap
        source: root.width > 4 && root.height > 4 && root.grungy
                ? "image://grungemask/" + root._bucketW + "x" + root._bucketH
                  + "?g=1&edge=1&r=" + root._maskRoughness + "&s=" + root._effSplatter
                  + "&cr=" + root.cornerRadius
                  + "&seed=" + (root.seed === 0 ? 42 : root.seed)
                : ""
    }
    Rectangle {
        id: edgeTint
        anchors.fill: parent
        visible: false
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: root._hasMood
                       ? Qt.rgba(root.deviceColor.r, root.deviceColor.g,
                                 root.deviceColor.b, 0.9)
                       : Qt.rgba(0.83, 0.21, 0.48, 0.9)
            }
            GradientStop {
                position: 0.55
                color: root._hasMood
                       ? Qt.rgba(root.deviceColor.r * 0.7 + 0.3,
                                 root.deviceColor.g * 0.7 + 0.16,
                                 root.deviceColor.b * 0.7, 0.8)
                       : Qt.rgba(0.9, 0.35, 0.28, 0.8)
            }
            GradientStop { position: 1.0; color: Qt.rgba(1.0, 0.55, 0.0, 0.75) }
        }
    }
    OpacityMask {
        anchors.fill: parent
        visible: root.grungy
        opacity: 0.45
        source: edgeTint
        maskSource: edgeMaskImage
        cached: false
    }

    OpacityMask {
        anchors.fill: parent
        source: glassLayer
        maskSource: maskImage
        // NOT cached: the mask Canvas paints asynchronously (150ms
        // debounce) and a cached effect snapshots the first — empty —
        // frame and never invalidates on Canvas repaints. That was the
        // "panel blacks out" bug: the whole glass+tint layer masked to
        // nothing, leaving only the dark backing fill.
        cached: false
    }

    // ── Layer 4: border stroke (gradient, via Canvas) ──
    Canvas {
        id: borderCanvas
        anchors.fill: parent

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            if (w <= 1 || h <= 1)
                return
            const stops = root._borderStops
            const grad = root._borderDiagonal
                ? ctx.createLinearGradient(0, 0, w, h)
                : ctx.createLinearGradient(0, 0, 0, h)
            for (let i = 0; i < stops.length; i++)
                grad.addColorStop(stops.length === 1 ? 0 : i / (stops.length - 1), stops[i])
            const lw = root.grungy ? 1.5 : 1.0
            ctx.strokeStyle = grad
            ctx.lineWidth = lw
            root._roundedRectPath(ctx, lw / 2, lw / 2, w - lw, h - lw, root.cornerRadius)
            ctx.stroke()
        }
    }

    // ── Layer 5: content (on top — always readable, like the mac).
    //    Explicit z so no decorative layer can ever stack above it. ──
    Item {
        id: contentItem
        anchors.fill: parent
        clip: true
        z: 10
    }

    // ── Repaint plumbing — debounced so resize animations don't regenerate
    //    the per-pixel mask every frame (the mac used a 32pt-snap cache for
    //    the same reason) ──
    Timer {
        id: repaintDebounce
        interval: 250
        onTriggered: {
            smearCanvas.requestPaint()
            borderCanvas.requestPaint()
        }
    }
    function _schedule() {
        if (!frozen)
            repaintDebounce.restart()
    }

    onWidthChanged: {
        _schedule()
        borderCanvas.requestPaint()   // cheap stroke — track every frame
    }
    onHeightChanged: {
        _schedule()
        borderCanvas.requestPaint()
    }
    onGrungyChanged: _schedule()
    onGrungeLevelChanged: _schedule()
    onSeedChanged: _schedule()
    onTierChanged: _schedule()
    onDeviceColorChanged: _schedule()
    Component.onCompleted: {
        _bucketW = _liveBW
        _bucketH = _liveBH
        // First paint immediately — the debounce is for resize storms
        smearCanvas.requestPaint()
        borderCanvas.requestPaint()
    }

    // ═══ Noise engine — direct port of GrungeNoise (GrungeFrame.swift,
    //     itself ported from ui_grunge.c). All 32-bit unsigned arithmetic. ═══

    function _noiseAt(x, y, seed) {
        let h = (seed ^ (Math.imul(x, 374761393) >>> 0)
                      ^ (Math.imul(y, 668265263) >>> 0)) >>> 0
        h = Math.imul((h ^ (h >>> 13)) >>> 0, 1274126177) >>> 0
        h = (h ^ (h >>> 16)) >>> 0
        return (h & 0xFFFF) / 65535.0
    }

    function _smoothNoise1D(t, octave, seed) {
        const i = Math.floor(t)
        const frac = t - i
        const s = frac * frac * (3.0 - 2.0 * frac)
        const a = _noiseAt(i, octave, seed)
        const b = _noiseAt(i + 1, octave, seed)
        return a + s * (b - a)
    }

    function _fbmNoise1D(t, octave, seed) {
        let val = 0.0, amp = 1.0, freq = 1.0, totalAmp = 0.0
        for (let o = 0; o < 3; o++) {
            val += amp * (_smoothNoise1D(t * freq, octave + o * 7, seed) - 0.5)
            totalAmp += amp
            amp *= 0.5
            freq *= 2.0
        }
        return val / totalAmp
    }

    function _rectSDF(px, py, w, h, inset) {
        return Math.min(px - inset, (w - inset) - px, py - inset, (h - inset) - py)
    }

    function _roundedRectSDF(px, py, w, h, inset, cornerRadius) {
        const r = Math.min(cornerRadius, Math.min(w, h) * 0.5 - inset)
        if (r <= 0)
            return _rectSDF(px, py, w, h, inset)
        const dLeft = px - inset, dRight = (w - inset) - px
        const dTop = py - inset, dBottom = (h - inset) - py
        const inL = dLeft < r, inR = dRight < r, inT = dTop < r, inB = dBottom < r
        let cx, cy
        if (inL && inT)      { cx = inset + r;     cy = inset + r }
        else if (inR && inT) { cx = w - inset - r; cy = inset + r }
        else if (inL && inB) { cx = inset + r;     cy = h - inset - r }
        else if (inR && inB) { cx = w - inset - r; cy = h - inset - r }
        else return Math.min(dLeft, dRight, dTop, dBottom)
        const dx = px - cx, dy = py - cy
        return r - Math.sqrt(dx * dx + dy * dy)
    }

    function _nearestEdge(px, py, w, h) {
        const d = [py, w - px, h - py, px]
        let minI = 0
        for (let i = 1; i < 4; i++)
            if (d[i] < d[minI])
                minI = i
        return minI
    }

    function _edgeParam(px, py, w, h, edge) {
        if (edge === 0) return px / w
        if (edge === 1) return py / h
        if (edge === 2) return 1.0 - px / w
        return 1.0 - py / h
    }

    // Rounded-rect path helper for Canvas
    function _roundedRectPath(ctx, x, y, w, h, r) {
        const rr = Math.min(r, Math.min(w, h) / 2)
        ctx.beginPath()
        ctx.moveTo(x + rr, y)
        ctx.lineTo(x + w - rr, y)
        ctx.arcTo(x + w, y, x + w, y + rr, rr)
        ctx.lineTo(x + w, y + h - rr)
        ctx.arcTo(x + w, y + h, x + w - rr, y + h, rr)
        ctx.lineTo(x + rr, y + h)
        ctx.arcTo(x, y + h, x, y + h - rr, rr)
        ctx.lineTo(x, y + rr)
        ctx.arcTo(x, y, x + rr, y, rr)
        ctx.closePath()
    }

    // Small seeded PRNG (mulberry32) for the smear layer — deterministic
    // per seed. (The mac uses a 64-bit LCG; JS numbers can't multiply
    // 64-bit exactly, so sequences differ but determinism is preserved.)
    function _lcg(seedIn) {
        let a = seedIn >>> 0
        return function () {
            a = (a + 0x6D2B79F5) | 0
            let t = Math.imul(a ^ (a >>> 15), 1 | a)
            t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296
        }
    }
}
