#include <QBitArray>
#include "AppSettings.h"
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class SettingsQuickSetup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable() {
        QCoreApplication::setOrganizationName("ZuunedPrintSettingsTest");
        QCoreApplication::setApplicationName("IsolatedQml");
    }
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("AppSettings", new AppSettings(engine));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(artwork_settings, SettingsQuickSetup)
#include "settings-quick.moc"
