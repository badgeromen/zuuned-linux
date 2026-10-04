#pragma once

#include "Redactor.h"
#include "SupportReport.h"
#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <functional>

class DiagnosticsService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool loggingAvailable READ loggingAvailable NOTIFY statusChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QString logError READ logError NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QUrl lastReportUrl READ lastReportUrl NOTIFY reportChanged)
public:
    explicit DiagnosticsService(QObject *parent = nullptr);
    ~DiagnosticsService() override;
    bool loggingAvailable() const;
    QString logDirectory() const;
    QString logError() const;
    bool busy() const { return m_busy; }
    QUrl lastReportUrl() const { return m_lastReportUrl; }
    static Zuuned::Diagnostics::Redactor::Context redactionContext();
    void setContextProvider(std::function<QJsonObject()> provider) { m_contextProvider = std::move(provider); }
    Q_INVOKABLE QUrl suggestedReportUrl() const;
    Q_INVOKABLE void exportReport(const QUrl &destination);
    Q_INVOKABLE void openLogFolder() const;
signals:
    void statusChanged();
    void busyChanged();
    void reportChanged();
    void exportFinished(bool success, const QUrl &reportUrl, const QString &error);
private:
    bool m_busy = false;
    bool m_lastAvailable = false;
    QString m_lastLogError;
    QUrl m_lastReportUrl;
    std::function<QJsonObject()> m_contextProvider;
    QFuture<Zuuned::Diagnostics::ReportResult> m_export;
};
