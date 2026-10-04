#include "ApplicationIdentity.h"

#include <QGuiApplication>
#include <QIcon>
#include <cstdio>

namespace {
int failures = 0;

void check(bool condition, const char *message) {
    if (condition) {
        std::printf("PASS: %s\n", message);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    Zuuned::configureApplicationIdentity(app);
    check(QGuiApplication::desktopFileName() == QStringLiteral("zuuned"),
          "runtime desktop identity matches zuuned.desktop");
    check(!QGuiApplication::windowIcon().isNull(),
          "runtime window icon loads from the compiled resource");
    check(!QGuiApplication::windowIcon().pixmap(128, 128).isNull(),
          "runtime window icon renders at taskbar size");
    return failures == 0 ? 0 : 1;
}
