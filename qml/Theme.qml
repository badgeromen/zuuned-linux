pragma Singleton
import QtQuick

// Zuuned design tokens — ported from Theme/ZuneColors.swift and
// Theme/ZuneStyles.swift in the macOS app. Every color and metric in the
// app references a token here; never use raw values in views.
QtObject {
    // ── Backgrounds — the world behind the glass ──
    readonly property color bg: "#0a0a0a"
    readonly property color navBg: "#101010"
    readonly property color surfaceBg: "#141414"

    // ── Brand — the identity ──
    readonly property color pink: "#d4367a"
    readonly property color orange: "#ff8c00"
    readonly property color activePink: "#e8567a"
    readonly property color green: "#4ade80"

    // ── Semantic ──
    readonly property color error: "#f24d4d"
    readonly property color warning: "#f2ad33"
    readonly property color info: "#6699e6"
    readonly property color purple: "#9e66e6"

    // ── Glass — see-through surfaces ──
    readonly property color glassBg: Qt.rgba(1, 1, 1, 0.05)
    readonly property color glassBorder: Qt.rgba(1, 1, 1, 0.06)
    readonly property color glassHighlight: Qt.rgba(1, 1, 1, 0.20)
    readonly property color glassBorderSubtle: Qt.rgba(1, 1, 1, 0.03)

    // ── Cards ──
    readonly property color cardBg: Qt.rgba(1, 1, 1, 0.04)
    readonly property color cardHover: Qt.rgba(1, 1, 1, 0.08)
    readonly property color cardActive: Qt.rgba(1, 1, 1, 0.12)
    readonly property color artworkPlaceholder: "#28252b"

    // ── Text — four tiers (contrast-checked against bg in the Swift source) ──
    readonly property color textPrimary: Qt.rgba(1, 1, 1, 0.92)
    readonly property color textMid: Qt.rgba(1, 1, 1, 0.70)
    readonly property color textSecondary: Qt.rgba(1, 1, 1, 0.55)
    readonly property color textSubtle: Qt.rgba(1, 1, 1, 0.40)
    readonly property color textDim: Qt.rgba(1, 1, 1, 0.35)
    readonly property color textGhost: Qt.rgba(1, 1, 1, 0.15)   // decorative only
    readonly property real disabledOpacity: 0.4
    readonly property int locationBadgeSize: 20
    readonly property int locationRowBadgeSize: 16
    readonly property int locationTooltipDelay: 450
    readonly property int hairline: 1
    readonly property int toastMaxWidth: 640
    readonly property int toastTextSize: 13
    readonly property int toastDuration: 5000
    readonly property int toastMaxTextHeight: 120
    readonly property int toastOverlayZ: 400
    readonly property int toastPhotoClearance: 104
    readonly property int toastModalClearance: 96

    // ── Borders & separators ──
    readonly property color border: Qt.rgba(1, 1, 1, 0.06)
    readonly property color borderLight: Qt.rgba(1, 1, 1, 0.10)
    readonly property color borderFaint: Qt.rgba(1, 1, 1, 0.03)
    readonly property color focusRing: Qt.rgba(0.83, 0.21, 0.48, 0.70)
    readonly property color separator: Qt.rgba(1, 1, 1, 0.03)
    readonly property color rowHoverPink: Qt.rgba(0.83, 0.21, 0.48, 0.04)
    readonly property color rowSelectedPink: Qt.rgba(0.83, 0.21, 0.48, 0.08)

    // ── Spacing (ZuneSpace) ──
    readonly property int spaceXxxs: 2
    readonly property int spaceXxs: 4
    readonly property int spaceXs: 6
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 16
    readonly property int spaceXl: 20
    readonly property int spaceXxl: 24
    readonly property int spaceXxxl: 32
    readonly property int spaceHuge: 48

    // ── Radii (ZuneRadius) ──
    readonly property int radiusSm: 4
    readonly property int radiusMd: 8
    readonly property int radiusLg: 12
    readonly property int radiusXl: 16
    readonly property int radiusXxl: 24

    // ── Motion (ZuneMotion durations, ms) ──
    readonly property int motionFast: 150
    readonly property int motionBase: 250
    readonly property int motionSlow: 400
    readonly property int motionDramatic: 600

    // Photo builder shares the mixtape island's stacked-card geometry.
    readonly property int photoTrayOverhang: 56
    readonly property int photoTrayInset: 18

    // ── Headroom (ux/7) — adaptive collapsing chrome ──
    // The cost of the giant header depends on WINDOW HEIGHT (house
    // convention: respond to the window, never the monitor — this is
    // a tiled desktop). Space-scarce windows (< compactBelow, i.e.
    // 1080p fullscreen and every short tile) get the GREETING: the
    // giant title says hello for greetingMs, then tucks itself, and
    // compact is the resting state. Tall windows rest on the big
    // header. Clicking the title toggles the chrome any time. NO
    // scroll coupling — tried and voided (Orson 2026-09-10). Mins
    // are the fully-collapsed values; the expanded values stay where
    // they always lived (Main.qml: title 86, header 110, margins 60).
    readonly property int headroomCompactBelow: 1100
    readonly property int headroomGreetingMs: 2500
    // Scarce windows also DENSIFY the galleries (Orson 2026-09-10):
    // cells scale toward this fraction of their ideal and column
    // caps rise, so a small screen shows MORE art, not less. This
    // deliberately inverts the grid study's "width buys bigger art"
    // on small windows only; tall windows keep the study untouched.
    readonly property real galleryScarceDensity: 0.78
    readonly property int galleryScarceExtraCols: 2
    readonly property int headroomTitleMin: 40
    readonly property int headroomHeaderMin: 46
    readonly property int headroomHeaderTopMin: 18
    readonly property int headroomIslandTopMin: 10

    // Sleeve customization — approved ZUUNED design study, September 2026.
    readonly property int customizeWidth: 1070
    readonly property int customizeHeight: 710
    readonly property int customizeCompactAt: 760
    readonly property int customizeHeader: 112
    readonly property int customizeFooter: 78
    readonly property int customizeArtSize: 300
    readonly property int customizeThumbSize: 126
    readonly property int customizeCompactArt: 96
    readonly property real customizeRecordInset: 0.14
    readonly property real customizeRecordScale: 0.96
    readonly property real cassetteAspect: 176 / 112
    readonly property int customizeFieldHeight: 62
    readonly property int customizeTitleSize: 38
    readonly property int customizeHeadingSize: 23
    readonly property int customizeBodySize: 13
    readonly property int customizeCaptionSize: 11
    readonly property int customizeLabelSize: 9
    readonly property color customizeSurface: "#19171a"
    readonly property color customizeShadow: Qt.rgba(0, 0, 0, 0.55)
    readonly property color transparent: "transparent"

    // Appearance print picker — quiet pivots and a live pair of artwork samples.
    readonly property int artworkSettingsPreviewSize: 180
    readonly property int artworkSettingsMaxWidth: 760
    readonly property int artworkSettingsControlHeight: 36
    readonly property int artworkSettingsSliderWidth: 300

    // ── Fonts ──
    readonly property FontLoader marker: FontLoader {
        source: "fonts/PermanentMarker-Regular.ttf"
    }
    readonly property FontLoader specimenAlien: FontLoader {
        source: "fonts/SpecimenAlien-Regular.ttf"
    }

    // The resolved display face for identity moments — Permanent Marker
    // by default, Specimen Alien only when chosen in Settings. Components
    // use THIS, never a FontLoader directly, so the setting reaches
    // every identity moment at once.
    readonly property string displayFamily:
        AppSettings.headerFont === "specimenAlien"
            ? specimenAlien.font.family
            : marker.font.family
}
