#include "diagnostics/DiagnosticsService.h"
#include "diagnostics/SessionLog.h"
#include "diagnostics/SupportReport.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <sys/stat.h>

using namespace Zuuned::Diagnostics;
int failures = 0, checks = 0;
void check(bool ok, const char *name) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
int main(int argc, char **argv) {
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(temp.filePath("config")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(temp.filePath("data")));
    qputenv("XDG_STATE_HOME", QFile::encodeName(temp.filePath("state")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(temp.filePath("cache")));
    qputenv("UNRELATED_PRIVATE_ENV", "never-include-this-canary");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Zuuned");
    app.setApplicationName("Zuuned");
    const Redactor::Context redaction {"/home/testperson", "testperson", {"known-secret-987654"}};
    const auto report = formatReport({{"build", "test-build"}},
        "[device] error=0x2005 serial='private-device-id'\n"
        "[provider] https://user:pass@art.test/image?api_key=known-secret-987654\n"
        "[scanner] /home/testperson/Music/album.mp3\n", redaction);
    check(report.contains("ZUUNED DEBUG REPORT") && report.contains("RECENT SESSION LOGS"), "readable report sections");
    check(report.contains("0x2005") && report.contains("test-build"), "diagnostic error and build retained");
    check(!report.contains("known-secret") && !report.contains("user:pass") && !report.contains("private-device-id"), "secrets and serial masked");
    check(!report.contains("/home/testperson"), "home path masked");
    const auto system = QJsonDocument(systemInformation()).toJson();
    check(!system.contains("never-include-this-canary") && !system.contains("machine_id") && !system.contains("hostname"), "system fields are allowlisted");
    check(system.contains("zune_usb_devices") && system.contains("graphics_devices"), "hardware inventory fields present");
    const auto output = QUrl::fromLocalFile(temp.filePath("report.txt"));
    auto saved = saveReport(output, report, {});
    check(saved.success && read(output.toLocalFile()) == report, "UTF-8 report saved exactly");
    struct stat st {};
    ::stat(QFile::encodeName(output.toLocalFile()).constData(), &st);
    check((st.st_mode & 0777) == 0600, "report private to owner");
    saved = saveReport(output, "replacement", {});
    check(saved.success && read(output.toLocalFile()) == "replacement", "atomic overwrite completes");
    check(!saveReport(QUrl("https://example.test/report.txt"), report, {}).success, "remote destination refused");
    check(!saveReport(QUrl("file://other-host/tmp/report.txt"), report, {}).success, "remote file host refused");
    check(!saveReport(QUrl::fromLocalFile(temp.filePath("missing/report.txt")), report, {}).success, "missing parent reports failure");
    check(!saveReport(QUrl::fromLocalFile(temp.path()), report, {}).success, "directory destination refused");
    const auto fifo = temp.filePath("report-pipe");
    ::mkfifo(QFile::encodeName(fifo).constData(), 0600);
    check(!saveReport(QUrl::fromLocalFile(fifo), report, {}).success, "special file destination refused");
    check(!saveReport(output, QByteArray(6 * 1024 * 1024 + 1, 'x'), {}).success
              && read(output.toLocalFile()) == "replacement", "oversized report refused without overwriting");
    const auto link = temp.filePath("symlink.txt");
    QFile::link(output.toLocalFile(), link);
    check(!saveReport(QUrl::fromLocalFile(link), report, {}).success && read(output.toLocalFile()) == "replacement", "symlink target preserved");
    QDir().mkpath(temp.filePath("protected"));
    check(!saveReport(QUrl::fromLocalFile(temp.filePath("protected/file.txt")), report, {temp.filePath("protected")}).success, "working folder protected");
    QFile::link(temp.filePath("protected"), temp.filePath("alias"));
    check(!saveReport(QUrl::fromLocalFile(temp.filePath("alias/file.txt")), report, {temp.filePath("protected")}).success, "aliased working folder protected");

    auto &logger = SessionLog::instance();
    logger.setRedactionContext(redaction);
    check(logger.start(temp.filePath("logs")), "session logging starts for integration");
    {
        QSettings settings;
        settings.setValue("tmdbApiKey", "settings-secret-canary-43210");
        settings.sync();
        QQmlEngine engine;
        QStringList qmlWarnings;
        QObject::connect(&engine, &QQmlEngine::warnings, [&](const auto &warnings) {
            for (const auto &warning : warnings) qmlWarnings.append(warning.toString());
        });
        auto *service = engine.singletonInstance<DiagnosticsService *>("Zuuned", "DiagnosticsService");
        check(service != nullptr, "native diagnostics singleton registered");
        service->setContextProvider([] { return QJsonObject {{"zune_connected", false}}; });
        std::fprintf(stderr, "[provider] api_key=settings-secret-canary-43210 failure=503\n");
        QSignalSpy finished(service, &DiagnosticsService::exportFinished);
        const auto asyncUrl = QUrl::fromLocalFile(temp.filePath("async-report.txt"));
        service->exportReport(asyncUrl);
        check(service->busy() && !QFile::exists(asyncUrl.toLocalFile()), "export stages off-thread");
        service->exportReport(QUrl::fromLocalFile(temp.filePath("duplicate.txt")));
        check(finished.wait(10000), "export completion signaled");
        check(!service->busy() && finished.size() == 1 && finished.at(0).at(0).toBool(), "single truthful success completion");
        const auto exported = read(asyncUrl.toLocalFile());
        check(!exported.contains("settings-secret-canary") && exported.contains("failure=503"), "settings secret scrubbed from exported logs");
        check(exported.contains("app-fixture") && exported.contains("libzune-fixture") && exported.contains("zune_connected"), "build and captured app state included");
        check(service->lastReportUrl() == asyncUrl && !QFile::exists(temp.filePath("duplicate.txt")), "completed report exposed and duplicate suppressed");
        finished.clear();
        service->exportReport(QUrl::fromLocalFile(temp.filePath("missing/failure.txt")));
        check(finished.wait(10000), "export failure completes");
        check(!finished.at(0).at(0).toBool() && !service->busy() && service->lastReportUrl() == asyncUrl, "failure preserves last good report");
        check(service->suggestedReportUrl().isLocalFile() && service->suggestedReportUrl().path().endsWith(".txt"), "suggested local report destination");

        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/Zuuned/DiagnosticsSettings.qml"));
        auto *item = qobject_cast<QQuickItem *>(component.create());
        check(item != nullptr, "production Health diagnostics section loads");
        if (item) {
            QQuickWindow window;
            window.setColor(QColor("#141414"));
            window.resize(660, 360);
            item->setParentItem(window.contentItem());
            item->setPosition(QPointF(24, 24));
            item->setWidth(612);
            window.show();
            QTest::qWait(150);
            check(item->findChild<QObject *>("diagnosticsExportButton") != nullptr, "export control available");
            check(item->implicitHeight() < window.height() - 48, "Health report controls fit section");
            check(window.grabWindow().save("/tmp/zuuned-diagnostics-health.png"), "native report section captured");
            delete item;
        } else {
            for (const auto &error : component.errors()) std::printf("QML: %s\n", qUtf8Printable(error.toString()));
        }
        check(qmlWarnings.isEmpty(), "no QML warnings");
    }
    logger.stop();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
