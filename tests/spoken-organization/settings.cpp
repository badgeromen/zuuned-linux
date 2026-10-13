#include "AppSettings.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("ZuunedSpokenSettingsTest");
    app.setApplicationName("IsolatedSettings");
    AppSettings settings;
    int failed = 0;
    auto check = [&](bool ok, const char *name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failed += !ok;
    };
    if (app.arguments().contains("--restore")) {
        check(settings.podcastAutoQueue() && settings.podcastAutoQueueCount() == 17,
              "starred podcast queue preferences survive a fresh process");
        check(!settings.podcastAutoCleanup(), "queue preference does not enable deletion");
        return failed ? 1 : 0;
    }
    check(!settings.podcastAutoQueue() && settings.podcastAutoQueueCount() == 10,
          "automatic queueing is opt-in with ten episodes by default");
    int changes = 0;
    QObject::connect(&settings, &AppSettings::changed, [&] { ++changes; });
    settings.setPodcastAutoQueue(true);
    settings.setPodcastAutoQueueCount(5);
    check(changes == 2 && settings.podcastAutoQueueCount() == 5,
          "queue controls publish changes for reactive UI");
    settings.setPodcastAutoQueueCount(5);
    check(changes == 2, "unchanged count does not trigger another refresh");
    settings.setPodcastAutoQueueCount(-4);
    check(settings.podcastAutoQueueCount() == 1, "negative counts clamp to one");
    settings.setPodcastAutoQueueCount(9000);
    check(settings.podcastAutoQueueCount() == 5000, "large counts clamp to the picker limit");
    QSettings store;
    store.setValue("podcastAutoQueueCount", "bad");
    check(settings.podcastAutoQueueCount() == 10, "malformed saved counts use the default");
    store.setValue("podcastAutoQueueCount", -5);
    check(settings.podcastAutoQueueCount() == 1, "saved out-of-range counts stay bounded");
    settings.setPodcastAutoQueueCount(17);
    check(!settings.podcastAutoCleanup(), "queue settings leave cleanup independent");
    return failed ? 1 : 0;
}
