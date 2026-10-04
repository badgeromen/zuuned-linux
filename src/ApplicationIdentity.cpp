#include "ApplicationIdentity.h"

#include <QGuiApplication>
#include <QIcon>

namespace Zuuned {

void configureApplicationIdentity(QGuiApplication &app) {
    QGuiApplication::setDesktopFileName(QStringLiteral("zuuned"));
    app.setWindowIcon(QIcon(QStringLiteral(":/zuuned/app/zuuned.png")));
}

} // namespace Zuuned
