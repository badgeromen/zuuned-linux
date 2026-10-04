#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Persistent settings — every key from the mac app (@AppStorage) with the
// same names and defaults, so behavior matches screen-for-screen. QML
// binds reactively; writes hit QSettings immediately.
#define ZUNE_SETTING(TYPE, NAME, SETTER, KEY, DEFAULT, READER)               \
    Q_PROPERTY(TYPE NAME READ NAME WRITE SETTER NOTIFY changed)              \
public:                                                                      \
    TYPE NAME() const { return m_store.value(KEY, DEFAULT).READER(); }       \
    void SETTER(const TYPE &v) {                                             \
        if (NAME() == v) return;                                             \
        m_store.setValue(KEY, v);                                            \
        emit changed();                                                      \
    }

class AppSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    ZUNE_SETTING(bool, onboardingComplete, setOnboardingComplete,
                 "onboardingComplete", false, toBool)
    // Display font for identity moments (header, splash, empty states).
    // Preserve existing graffiti selections when replacing the retired font.
    Q_PROPERTY(QString headerFont READ headerFont WRITE setHeaderFont NOTIFY changed)
public:
    QString headerFont() const {
        const auto value = m_store.value("headerFont", "permanentMarker").toString();
        return value == "misdemeanor" ? QStringLiteral("specimenAlien") : value;
    }
    void setHeaderFont(const QString &value) {
        const auto canonical = value == "misdemeanor" ? QStringLiteral("specimenAlien") : value;
        if (m_store.value("headerFont", "permanentMarker").toString() == canonical) return;
        m_store.setValue("headerFont", canonical);
        emit changed();
    }
    ZUNE_SETTING(QString, backgroundImagePath, setBackgroundImagePath,
                 "backgroundImagePath", QString(), toString)
    // Backdrop preset behind the whole app when no custom image is set:
    // "none" | "grime" | "dusk" (the W15 mock's veil pills).
    ZUNE_SETTING(QString, backdropPreset, setBackdropPreset,
                 "backdropPreset", QStringLiteral("none"), toString)
    // OPT-IN A–Z section headers inside the gallery grids. Default OFF:
    // the clean unbroken grid is the design; the side rail FILTERS.
    // (Orson, 2026-09-06 — breaks fragment the grid.)
    ZUNE_SETTING(bool, letterBreaks, setLetterBreaks,
                 "letterBreaks", false, toBool)
    ZUNE_SETTING(double, backgroundOpacity, setBackgroundOpacity,
                 "backgroundOpacity", 0.3, toDouble)
    ZUNE_SETTING(double, backgroundBlurAmount, setBackgroundBlurAmount,
                 "backgroundBlurAmount", 25.0, toDouble)
    // -1=auto (from connected device model), 0=Zune30 WMV,
    // 1=classic H.264 320x240, 2=ZuneHD 480x272, 3=ZuneHD 720p
    ZUNE_SETTING(int, videoProfile, setVideoProfile,
                 "videoProfile", -1, toInt)
    ZUNE_SETTING(QString, preferredAudioLang, setPreferredAudioLang,
                 "preferredAudioLang", QStringLiteral("eng"), toString)
    ZUNE_SETTING(QString, posterSource, setPosterSource,
                 "posterSource", QStringLiteral("tmdb"), toString)
    // When an album ships FLAC + MP3 of the same tracks: "keepBoth" |
    // "flac" | "mp3". Read by the scanner's duplicate resolver.
    ZUNE_SETTING(QString, duplicateFormats, setDuplicateFormats,
                 "duplicateFormats", QStringLiteral("keepBoth"), toString)
    ZUNE_SETTING(QString, tmdbApiKey, setTmdbApiKey,
                 "tmdbApiKey", QString(), toString)
    ZUNE_SETTING(QString, fanarttvApiKey, setFanarttvApiKey,
                 "fanarttvApiKey", QString(), toString)
    ZUNE_SETTING(int, videoMinSizeMB, setVideoMinSizeMB,
                 "videoMinSizeMB", 0, toInt)
    // Transcode acceleration (Settings → Device pills): "auto" = NVDEC
    // when present with SW fallback, "nvenc" = NVIDIA, "cpu" = force
    // software. "amd" is reserved (VAAPI decode not implemented; its
    // pill is disabled). Supersedes the old bool "hwDecode" key —
    // SyncEngine still honors a legacy hwDecode=false as "cpu".
    ZUNE_SETTING(QString, transcodeAccel, setTranscodeAccel,
                 "transcodeAccel", QStringLiteral("auto"), toString)
    // Ambient background spray/bloom (the grime particles). Toggleable
    // in Settings → Appearance; default on.
    ZUNE_SETTING(bool, ambientSpray, setAmbientSpray,
                 "ambientSpray", true, toBool)
    ZUNE_SETTING(QString, miniPlayerFrame, setMiniPlayerFrame,
                 "miniPlayerFrame", QString(), toString)
    // Music player volume (0-100) — restored at launch, saved on change
    ZUNE_SETTING(double, playerVolume, setPlayerVolume,
                 "playerVolume", 100.0, toDouble)
    // Video player disc style: "vinyl" = full 400px multi-groove disc
    // (grooves = chapters), "disc" = smaller single-ring seeker (one
    // revolution = the whole file, the audio-disc look).
    ZUNE_SETTING(QString, videoControlsStyle, setVideoControlsStyle,
                 "videoControlsStyle", QStringLiteral("vinyl"), toString)

    // Artwork is a reversible display preference. Each print keeps its own
    // tuning; changing looks never rewrites source artwork or metadata.
    Q_PROPERTY(QString artworkStyle READ artworkStyle WRITE setArtworkStyle NOTIFY changed)
    Q_PROPERTY(int artworkCleanDetail READ artworkCleanDetail WRITE setArtworkCleanDetail NOTIFY changed)
    Q_PROPERTY(int artworkHalftoneTexture READ artworkHalftoneTexture WRITE setArtworkHalftoneTexture NOTIFY changed)
    Q_PROPERTY(int artworkHalftoneDotSize READ artworkHalftoneDotSize WRITE setArtworkHalftoneDotSize NOTIFY changed)
    Q_PROPERTY(bool artworkHalftoneMonochrome READ artworkHalftoneMonochrome WRITE setArtworkHalftoneMonochrome NOTIFY changed)
    Q_PROPERTY(int artworkWornTexture READ artworkWornTexture WRITE setArtworkWornTexture NOTIFY changed)

public:
    explicit AppSettings(QObject *parent = nullptr) : QObject(parent) {}

    QString artworkStyle() const {
        return validArtworkStyle(m_store.value("artworkStyle", "original").toString());
    }
    void setArtworkStyle(const QString &value) {
        const auto style = validArtworkStyle(value);
        if (artworkStyle() == style) return;
        m_store.setValue("artworkStyle", style);
        emit changed();
    }
    int artworkCleanDetail() const { return artworkPercent("artworkCleanDetail", 100); }
    int artworkHalftoneTexture() const { return artworkPercent("artworkHalftoneTexture", 14); }
    int artworkHalftoneDotSize() const { return artworkPercent("artworkHalftoneDotSize", 17); }
    int artworkWornTexture() const { return artworkPercent("artworkWornTexture", 14); }
    bool artworkHalftoneMonochrome() const {
        const auto value = m_store.value("artworkHalftoneMonochrome", false).toString().toLower();
        return value == "true" || value == "1";
    }
    void setArtworkCleanDetail(int value) { setArtworkPercent("artworkCleanDetail", value, 100); }
    void setArtworkHalftoneTexture(int value) { setArtworkPercent("artworkHalftoneTexture", value, 14); }
    void setArtworkHalftoneDotSize(int value) { setArtworkPercent("artworkHalftoneDotSize", value, 17); }
    void setArtworkWornTexture(int value) { setArtworkPercent("artworkWornTexture", value, 14); }
    void setArtworkHalftoneMonochrome(bool value) {
        if (artworkHalftoneMonochrome() == value) return;
        m_store.setValue("artworkHalftoneMonochrome", value);
        emit changed();
    }
    Q_INVOKABLE void resetArtworkStyle() {
        const auto style = artworkStyle();
        // A reset is one settings change, so consumers never render a partly
        // reset Halftone. Original has no tuning and changes nothing.
        if (style == "cleanInk") {
            m_store.remove("artworkCleanDetail");
        } else if (style == "halftone") {
            m_store.remove("artworkHalftoneTexture");
            m_store.remove("artworkHalftoneDotSize");
            m_store.remove("artworkHalftoneMonochrome");
        } else if (style == "wornPrint") {
            m_store.remove("artworkWornTexture");
        } else {
            return;
        }
        emit changed();
    }

signals:
    void changed();

private:
    static QString validArtworkStyle(const QString &value) {
        if (value == "cleanInk" || value == "halftone" || value == "wornPrint") return value;
        return QStringLiteral("original");
    }
    int artworkPercent(const char *key, int fallback) const {
        bool valid = false;
        const auto value = m_store.value(key, fallback).toInt(&valid);
        return valid ? qBound(0, value, 100) : fallback;
    }
    void setArtworkPercent(const char *key, int value, int fallback) {
        value = qBound(0, value, 100);
        if (artworkPercent(key, fallback) == value) return;
        m_store.setValue(key, value);
        emit changed();
    }
    QSettings m_store;
};

#undef ZUNE_SETTING
