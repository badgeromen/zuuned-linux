#include "DiagnosticsService.h"
#include "SessionLog.h"
#include "SupportReport.h"
#include "ZuunedBuildInfo.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

using namespace Zuuned::Diagnostics;

DiagnosticsService::DiagnosticsService(QObject *parent) : QObject(parent) {
    m_lastAvailable = loggingAvailable();
    m_lastLogError = logError();
    auto *status = new QTimer(this);
    status->setInterval(2000);
    connect(status, &QTimer::timeout, this, [this] {
        const bool available = loggingAvailable();
        const QString error = logError();
        if (available != m_lastAvailable || error != m_lastLogError) {
            m_lastAvailable = available;
            m_lastLogError = error;
            emit statusChanged();
        }
    });
    status->start();
}

DiagnosticsService::~DiagnosticsService() {
    // Finish the bounded file export before the process-wide logger shuts down.
    m_export.waitForFinished();
}

bool DiagnosticsService::loggingAvailable() const { return SessionLog::instance().isAvailable(); }
QString DiagnosticsService::logDirectory() const { return SessionLog::instance().directory(); }
QString DiagnosticsService::logError() const { return SessionLog::instance().error(); }

Redactor::Context DiagnosticsService::redactionContext() {
    Redactor::Context context;
    context.homePath = QDir::homePath();
    context.userName = qEnvironmentVariable("USER");
    QSettings settings;
    const QRegularExpression secretKey(QStringLiteral("key|token|password|secret|credential"),
                                      QRegularExpression::CaseInsensitiveOption);
    // Values only reach the scrubber, never the report metadata or a log line.
    for (const auto &key : settings.allKeys()) {
        if (!secretKey.match(key).hasMatch()) continue;
        const QString secret = settings.value(key).toString();
        if (!secret.isEmpty()) context.secrets.append(secret);
    }
    return context;
}

QUrl DiagnosticsService::suggestedReportUrl() const {
    QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QDir::homePath();
    return QUrl::fromLocalFile(QDir(folder).filePath(QStringLiteral("zuuned-debug-%1.txt")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")))));
}

void DiagnosticsService::openLogFolder() const {
    if (QDir(logDirectory()).exists() && !logDirectory().isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDirectory()));
}

void DiagnosticsService::exportReport(const QUrl &destination) {
    if (m_busy) return;
    m_busy = true;
    emit busyChanged();
    emit statusChanged();
    const auto redaction = redactionContext();
    SessionLog::instance().setRedactionContext(redaction);
    QJsonObject appContext = m_contextProvider ? m_contextProvider() : QJsonObject();
    appContext.insert(QStringLiteral("build"), QString::fromLatin1(ZuunedBuild::version));
    appContext.insert(QStringLiteral("app_revision"), QString::fromLatin1(ZuunedBuild::appRevision));
    appContext.insert(QStringLiteral("libzune_revision"), QString::fromLatin1(ZuunedBuild::libzuneRevision));
    appContext.insert(QStringLiteral("platform"), QGuiApplication::platformName());
    appContext.insert(QStringLiteral("logging_available"), loggingAvailable());
    QJsonArray screens;
    for (const auto *screen : QGuiApplication::screens())
        screens.append(QJsonObject {{QStringLiteral("width"), screen->size().width()},
                                    {QStringLiteral("height"), screen->size().height()},
                                    {QStringLiteral("scale"), screen->devicePixelRatio()}});
    appContext.insert(QStringLiteral("displays"), screens);
    QStringList protectedDirectories;
    for (const auto location : {QStandardPaths::AppDataLocation, QStandardPaths::AppConfigLocation,
                                QStandardPaths::CacheLocation, QStandardPaths::StateLocation})
        protectedDirectories.append(QStandardPaths::writableLocation(location));
    protectedDirectories.append(logDirectory());
    auto *watcher = new QFutureWatcher<ReportResult>(this);
    connect(watcher, &QFutureWatcher<ReportResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (result.success) {
            m_lastReportUrl = result.url;
            emit reportChanged();
        }
        emit busyChanged();
        emit statusChanged();
        emit exportFinished(result.success, result.url, result.error);
    });
    m_export = QtConcurrent::run([destination, redaction, appContext, protectedDirectories] {
        auto info = systemInformation();
        info.insert(QStringLiteral("application"), appContext);
        const auto logs = SessionLog::instance().snapshot();
        return saveReport(destination, formatReport(info, logs, redaction), protectedDirectories);
    });
    watcher->setFuture(m_export);
}
