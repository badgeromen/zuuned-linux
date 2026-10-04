#pragma once

#include <QString>
#include <QStringList>

namespace Zuuned::Diagnostics {

struct RedactionContext {
    QString homePath;
    QString userName;
    QStringList secrets;
};

// Deliberately conservative diagnostic text filter, not a general anonymizer.
// The same filter runs before persistence and again during report export.
class Redactor {
public:
    using Context = RedactionContext;
    explicit Redactor(Context context = {});
    QString sanitize(const QString &text) const;

private:
    Context m_context;
};

} // namespace Zuuned::Diagnostics
