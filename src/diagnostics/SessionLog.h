#pragma once

#include "Redactor.h"
#include <QByteArray>
#include <QString>
#include <memory>

namespace Zuuned::Diagnostics {

// Linux stderr capture. Start before QGuiApplication and stop only after all
// application services are destroyed. Does not alter stdout or Qt filtering.
class SessionLog {
public:
    struct Limits {
        qint64 fileBytes = 2 * 1024 * 1024;
        int files = 8;
        int lineBytes = 16 * 1024;
        bool mirrorConsole = true;
    };

    static SessionLog &instance();
    SessionLog();
    ~SessionLog();
    SessionLog(const SessionLog &) = delete;
    SessionLog &operator=(const SessionLog &) = delete;

    bool start(const QString &directory);
    bool start(const QString &directory, Limits limits);
    void stop();
    bool isAvailable() const;
    QString directory() const;
    QString error() const;
    void setRedactionContext(RedactionContext context);
    // Waits briefly for queued stderr. A live stream may continue after this
    // cut. The snapshot includes a sanitized copy of any unterminated line.
    QByteArray snapshot(qint64 maxBytes = 4 * 1024 * 1024);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Zuuned::Diagnostics
