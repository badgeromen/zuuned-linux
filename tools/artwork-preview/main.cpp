#include "PreviewController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QThreadPool>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFileInfo>
#include <functional>
#include <cstdio>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ZuunedPreview");
    app.setApplicationName("Artwork Print Lab");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"cache-root", "Read artwork from this ZUUNED cache.", "path",
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/Zuuned/Zuuned"});
    parser.addOption({"library-db", "Read sample titles from this library, read-only.", "path",
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Zuuned/Zuuned/library.db"});
    parser.addOption({"output", "Write derived previews here.", "path", QDir::tempPath() + "/zuuned-artwork-preview"});
    parser.addOption({"export", "Export all sample comparisons and timings, then exit."});
    parser.addOption({"detail", "Starting detail, 0 to 100.", "number", "55"});
    parser.addOption({"texture", "Starting texture, 0 to 100.", "number", "35"});
    parser.addOption({"dot-size", "Halftone dot size, 0 to 100.", "number", "35"});
    parser.addOption({"monochrome", "Render Halftone in monochrome."});
    parser.addOption({"sample", "Start with or export only this cached sample name.", "name"});
    parser.addOption({"capture", "Save the live preview window once all four images are ready.", "path"});
    parser.addOption({"exit-after-capture", "Close this preview after saving its window capture."});
    parser.process(app);
    bool detailValid = false, textureValid = false, dotSizeValid = false;
    const int detail = parser.value("detail").toInt(&detailValid), texture = parser.value("texture").toInt(&textureValid);
    const int dotSize = parser.value("dot-size").toInt(&dotSizeValid);
    if (!detailValid || !textureValid || !dotSizeValid || detail < 0 || detail > 100
            || texture < 0 || texture > 100 || dotSize < 0 || dotSize > 100) {
        fprintf(stderr, "Detail, texture, and dot size must be whole numbers from 0 to 100.\n"); return 2;
    }
    PreviewController::configure(parser.value("cache-root"), parser.value("library-db"), parser.value("output"),
        detail, texture, parser.value("sample"), dotSize, parser.isSet("monochrome"));
    // This isolated executable has no DeviceService or provider clients.
    QThreadPool::globalInstance()->setMaxThreadCount(2);
    if (parser.isSet("export")) return PreviewController::exportSamples();
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("ZuunedPrintPreview", "Preview");
    QTimer capture;
    int captureTicks = 0;
    if (parser.isSet("capture")) {
        capture.setInterval(250);
        QObject::connect(&capture, &QTimer::timeout, &app, [&] {
            auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            int ready = 0;
            std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
                if (item->objectName().startsWith("printVariant") && item->property("status").toInt() == 1) ++ready;
                for (auto *child : item->childItems()) visit(child);
            };
            if (window) visit(window->contentItem());
            if (ready == 4) {
                const QString path = parser.value("capture");
                QDir().mkpath(QFileInfo(path).absolutePath());
                const bool saved = window->grabWindow().save(path);
                fprintf(stderr, "[print-preview] capture %s: %s\n", saved ? "saved" : "failed", qPrintable(path));
                capture.stop();
                if (parser.isSet("exit-after-capture")) app.exit(saved ? 0 : 1);
            } else if (++captureTicks >= 120) {
                fprintf(stderr, "[print-preview] capture timed out\n"); capture.stop();
                if (parser.isSet("exit-after-capture")) app.exit(1);
            }
        });
        capture.start();
    }
    const int result = app.exec();
    QThreadPool::globalInstance()->waitForDone();
    return result;
}
