#include "AppSettings.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZuunedPrintSettingsTest");
    app.setApplicationName("IsolatedSettings");
    AppSettings settings;
    int failures = 0;
    const auto check = [&](bool passed, const char *name) {
        qInfo().noquote() << (passed ? "PASS" : "FAIL") << name;
        if (!passed) ++failures;
    };
    const bool restore = app.arguments().contains("--restore");
    if (restore) {
        check(settings.headerFont() == "specimenAlien", "replacement font survives restart");
        check(settings.artworkStyle() == "halftone" && settings.artworkCleanDetail() == 73
              && settings.artworkHalftoneTexture() == 46 && settings.artworkHalftoneDotSize() == 62
              && settings.artworkHalftoneMonochrome() && settings.artworkWornTexture() == 29,
              "all look preferences survive a fresh process");
        return failures ? 1 : 0;
    }

    check(settings.artworkStyle() == "original" && settings.artworkCleanDetail() == 100
          && settings.artworkHalftoneTexture() == 14 && settings.artworkHalftoneDotSize() == 17
          && !settings.artworkHalftoneMonochrome() && settings.artworkWornTexture() == 14,
          "Original is the default with approved opt-in print presets");

    settings.setArtworkCleanDetail(73);
    settings.setArtworkHalftoneTexture(46);
    settings.setArtworkHalftoneDotSize(62);
    settings.setArtworkHalftoneMonochrome(true);
    settings.setArtworkWornTexture(29);
    settings.setArtworkStyle("halftone");
    settings.setArtworkStyle("cleanInk");
    settings.setArtworkStyle("wornPrint");
    check(settings.artworkCleanDetail() == 73 && settings.artworkHalftoneTexture() == 46
          && settings.artworkHalftoneDotSize() == 62 && settings.artworkHalftoneMonochrome()
          && settings.artworkWornTexture() == 29,
          "switching styles preserves all independent preferences");

    check(settings.headerFont() == "permanentMarker", "default font remains Permanent Marker");
    QSettings legacyStore;
    legacyStore.setValue("headerFont", "misdemeanor");
    legacyStore.sync();
    check(settings.headerFont() == "specimenAlien", "legacy graffiti preference resolves to Specimen Alien");
    settings.setHeaderFont("misdemeanor");
    check(legacyStore.value("headerFont").toString() == "specimenAlien",
          "legacy writes persist the canonical font preference");
    settings.setArtworkStyle("halftone");
    int changes = 0;
    QObject::connect(&settings, &AppSettings::changed, [&] { ++changes; });
    settings.resetArtworkStyle();
    check(changes == 1 && settings.artworkStyle() == "halftone"
          && settings.artworkHalftoneTexture() == 14 && settings.artworkHalftoneDotSize() == 17
          && !settings.artworkHalftoneMonochrome() && settings.artworkCleanDetail() == 73
          && settings.artworkWornTexture() == 29 && settings.headerFont() == "specimenAlien",
          "reset selected Halftone atomically without touching other look or app settings");
    settings.setArtworkHalftoneTexture(46);
    settings.setArtworkHalftoneDotSize(62);
    settings.setArtworkHalftoneMonochrome(true);
    settings.setArtworkStyle("original");
    changes = 0;
    settings.resetArtworkStyle();
    check(changes == 0 && settings.artworkCleanDetail() == 73
          && settings.artworkHalftoneTexture() == 46 && settings.artworkHalftoneDotSize() == 62
          && settings.artworkHalftoneMonochrome() && settings.artworkWornTexture() == 29,
          "Original reset leaves all print preferences intact");

    settings.setArtworkStyle("wornPrint");
    settings.resetArtworkStyle();
    settings.setArtworkStyle("cleanInk");
    settings.resetArtworkStyle();
    check(settings.artworkWornTexture() == 14 && settings.artworkCleanDetail() == 100
          && settings.artworkHalftoneTexture() == 46 && settings.artworkHalftoneDotSize() == 62
          && settings.artworkHalftoneMonochrome(),
          "Clean and Worn reset only their selected preference");

    settings.setArtworkCleanDetail(-30);
    settings.setArtworkHalftoneTexture(450);
    settings.setArtworkHalftoneDotSize(-10);
    settings.setArtworkWornTexture(123);
    settings.setArtworkStyle("unknown");
    check(settings.artworkCleanDetail() == 0 && settings.artworkHalftoneTexture() == 100
          && settings.artworkHalftoneDotSize() == 0 && settings.artworkWornTexture() == 100
          && settings.artworkStyle() == "original", "invalid writes clamp controls and release to Original");

    QSettings store;
    store.setValue("artworkStyle", "unknown");
    store.setValue("artworkCleanDetail", "bad");
    store.setValue("artworkHalftoneTexture", -5);
    store.setValue("artworkHalftoneDotSize", 200);
    store.setValue("artworkWornTexture", "bad");
    store.setValue("artworkHalftoneMonochrome", "bad");
    store.sync();
    check(settings.artworkStyle() == "original" && settings.artworkCleanDetail() == 100
          && settings.artworkHalftoneTexture() == 0 && settings.artworkHalftoneDotSize() == 100
          && settings.artworkWornTexture() == 14 && !settings.artworkHalftoneMonochrome(),
          "malformed persisted preferences stay bounded and use safe fallbacks");

    settings.setArtworkCleanDetail(73);
    settings.setArtworkHalftoneTexture(46);
    settings.setArtworkHalftoneDotSize(62);
    settings.setArtworkHalftoneMonochrome(true);
    settings.setArtworkWornTexture(29);
    settings.setArtworkStyle("halftone");
    return failures ? 1 : 0;
}
