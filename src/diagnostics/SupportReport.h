#pragma once

#include "Redactor.h"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace Zuuned::Diagnostics {

// System inspection reads an explicit allowlist of fields, without probing USB
// or starting external commands. The caller supplies UI-thread app state.
QJsonObject systemInformation();
QByteArray formatReport(const QJsonObject &information, const QByteArray &logs,
                        const Redactor::Context &redaction);
struct ReportResult {
    bool success = false;
    QUrl url;
    QString error;
};
ReportResult saveReport(const QUrl &destination, const QByteArray &report,
                        const QStringList &protectedDirectories);

} // namespace Zuuned::Diagnostics
