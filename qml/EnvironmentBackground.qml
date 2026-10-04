import QtQuick
import Qt5Compat.GraphicalEffects

// Environmental background — port of ZuneEnvironmentBackground +
// SprayGroup.swift. The "world" behind the glass panels.
//
// Layers (bottom to top):
//   1. Theme.bg — near-black base
//   2. Spray layer — seeded graffiti atmosphere (clouds, dots, particles,
//      streaks), blurred; device mood color bleeds into the palette
//   3. Device warmth glow — brand/mood radial halo from the trailing edge
//
// sprayIntensity is continuous 0…1 mapping onto the mac's three presets:
//   0.0 ≈ .ambient   (3 clouds,  5 dots, 15 particles, 1 streak, blur 60)
//   0.5 ≈ .present   (~7 clouds, ~18 dots, ~55 particles, ~5 streaks)
//   1.0 ≈ .intense   (12 clouds, 30 dots, 90 particles, 8 streaks, blur 20)
// The spray Canvas repaints only when size or parameters change.
Item {
    id: root

    property real sprayIntensity: 0.0
    property int spraySeed: 42
    property real warmth: 0.0
    property color deviceColor: "transparent"

    readonly property bool _hasMood: deviceColor.a > 0
    readonly property real _s: Math.max(0, Math.min(1, sprayIntensity))

    // Preset interpolation (ambient → intense)
    readonly property int _cloudCount: Math.round(3 + _s * 9)
    readonly property int _dotCount: Math.round(5 + _s * 25)
    readonly property int _particleCount: Math.round(15 + _s * 75)
    readonly property int _streakCount: Math.round(1 + _s * 7)
    readonly property real _opacityLo: 0.02 + _s * 0.04
    readonly property real _opacityHi: 0.05 + _s * 0.10
    readonly property real _blurRadius: 60 - _s * 40

    // Layer 1: base
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    // Layer 1.5: wallpaper (Settings → Appearance). Sits under the spray
    // so the graffiti atmosphere reads as painted OVER the image, same
    // stacking as the mac's ZuneEnvironmentBackground.
    Image {
        anchors.fill: parent
        visible: AppSettings.backgroundImagePath.length > 0
        source: AppSettings.backgroundImagePath.length > 0
                ? "file://" + AppSettings.backgroundImagePath : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        opacity: AppSettings.backgroundOpacity
        // Mac blur is a Gaussian 0–50; FastBlur radii read ~2× a Gaussian's,
        // but 100 is FastBlur's ceiling and 2× washes out — 1.6 tracks the
        // mac look closely across the range.
        layer.enabled: AppSettings.backgroundBlurAmount > 0.5
        layer.effect: FastBlur {
            radius: Math.min(100, AppSettings.backgroundBlurAmount * 1.6)
        }
    }

    // Layer 1.6: backdrop presets (Settings → Appearance) — the mock's
    // Grime / Dusk veils. Only when no custom image; they sit under the
    // spray like the wallpaper does.
    readonly property bool _presetActive:
        AppSettings.backgroundImagePath.length === 0

    // Grime — the grunge generator's speckle, tiled wall-scale. Fixed
    // request size so resizes never re-generate; at this opacity the
    // tile seam is invisible.
    Image {
        anchors.fill: parent
        visible: root._presetActive && AppSettings.backdropPreset === "grime"
        source: visible
                ? "image://grungemask/1024x1024?g=1&r=0.6&s=0.9&cr=0&seed="
                  + root.spraySeed
                : ""
        fillMode: Image.Tile
        // Grunge = DevicePresence: the veil stays faint on its own and
        // deepens while a Zune is connected (warmth), like the spray.
        opacity: 0.03 + 0.045 * root.warmth
        Behavior on opacity { NumberAnimation { duration: 300 } }
        asynchronous: true
        cache: true
    }

    // Dusk — the mock's 160° purple-to-black gradient veil
    LinearGradient {
        anchors.fill: parent
        visible: root._presetActive && AppSettings.backdropPreset === "dusk"
        start: Qt.point(width * 0.35, 0)
        end: Qt.point(0, height)
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(30 / 255, 20 / 255, 40 / 255, 0.55) }
            GradientStop { position: 0.72; color: Qt.rgba(0, 0, 0, 0.5) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.5) }
        }
    }

    // Layer 2: spray — drawn once per size/param change, blurred via effect
    Canvas {
        id: sprayCanvas
        anchors.fill: parent
        layer.enabled: true
        layer.effect: FastBlur {
            radius: root._blurRadius
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            if (w <= 0 || h <= 0)
                return
            const rand = root._rng(root.spraySeed)

            // Spray palette: brand pink + orange, + mood color when present
            const palette = [Theme.pink, Theme.orange]
            if (root._hasMood)
                palette.push(root.deviceColor)

            const opRange = () => root._opacityLo + rand() * (root._opacityHi - root._opacityLo)
            const withAlpha = (c, a) => Qt.rgba(c.r, c.g, c.b, a)

            // Large spray clouds — atmospheric wash (brand colors only)
            for (let i = 0; i < root._cloudCount; i++) {
                const x = rand() * w, y = rand() * h
                const radius = 40 + rand() * 80
                const color = palette[Math.floor(rand() * 2)]
                ctx.fillStyle = withAlpha(color, opRange())
                ctx.beginPath()
                ctx.ellipse(x - radius, y - radius, radius * 2, radius * 2)
                ctx.fill()
            }

            // Medium spray dots — full palette (mood bleeds in here)
            for (let i = 0; i < root._dotCount; i++) {
                const x = rand() * w, y = rand() * h
                const radius = 8 + rand() * 22
                const color = palette[Math.floor(rand() * palette.length)]
                ctx.fillStyle = withAlpha(color, opRange())
                ctx.beginPath()
                ctx.ellipse(x - radius, y - radius, radius * 2, radius * 2)
                ctx.fill()
            }

            // Fine spray particles (brand colors, slightly hotter ceiling)
            for (let i = 0; i < root._particleCount; i++) {
                const x = rand() * w, y = rand() * h
                const radius = 2 + rand() * 4
                const color = palette[Math.floor(rand() * 2)]
                const hi = Math.min(root._opacityHi * 1.5, 0.45)
                const op = root._opacityLo + rand() * (hi - root._opacityLo)
                ctx.fillStyle = withAlpha(color, op)
                ctx.beginPath()
                ctx.ellipse(x - radius, y - radius, radius * 2, radius * 2)
                ctx.fill()
            }

            // Spray streaks — a can dragged across the wall
            for (let i = 0; i < root._streakCount; i++) {
                const x = rand() * w, y = rand() * h
                const rw = 20 + rand() * 60
                const rh = 5 + rand() * 10
                const angle = (-45 + rand() * 90) * Math.PI / 180
                const color = palette[Math.floor(rand() * 2)]
                ctx.save()
                ctx.translate(x, y)
                ctx.rotate(angle)
                ctx.fillStyle = withAlpha(color, root._opacityLo + 0.05)
                ctx.beginPath()
                ctx.ellipse(-rw / 2, -rh / 2, rw, rh)
                ctx.fill()
                ctx.restore()
            }
        }
    }

    // Layer 3: device warmth — brand glow + mood halo from the right edge
    RadialGradient {
        anchors.fill: parent
        visible: root.warmth > 0
        horizontalOffset: parent.width / 2
        horizontalRadius: 700
        verticalRadius: 700
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: root._hasMood
                       ? Qt.rgba(root.deviceColor.r, root.deviceColor.g,
                                 root.deviceColor.b, 0.10 * root.warmth)
                       : Qt.rgba(0.83, 0.21, 0.48, 0.06 * root.warmth)
            }
            GradientStop {
                position: 0.35
                color: Qt.rgba(1.0, 0.55, 0.0, 0.04 * root.warmth)
            }
            GradientStop {
                position: 0.7
                color: Qt.rgba(root.deviceColor.r, root.deviceColor.g,
                               root.deviceColor.b, 0.06 * root.warmth * root.deviceColor.a)
            }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Repaint plumbing — once per size/param change, never per frame
    Timer {
        id: repaintDebounce
        interval: 100
        onTriggered: sprayCanvas.requestPaint()
    }
    function _schedule() { repaintDebounce.restart() }

    onWidthChanged: _schedule()
    onHeightChanged: _schedule()
    onSprayIntensityChanged: _schedule()
    onSpraySeedChanged: _schedule()
    onDeviceColorChanged: _schedule()
    Component.onCompleted: _schedule()

    // Seeded PRNG (mulberry32) — deterministic per seed. (The mac uses a
    // 64-bit LCG; exact 64-bit multiplies aren't representable in JS
    // numbers, so the sequence differs but determinism is preserved.)
    function _rng(seedIn) {
        let a = seedIn >>> 0
        return function () {
            a = (a + 0x6D2B79F5) | 0
            let t = Math.imul(a ^ (a >>> 15), 1 | a)
            t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296
        }
    }
}
