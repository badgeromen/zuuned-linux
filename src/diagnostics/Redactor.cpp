#include "Redactor.h"

#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <functional>

namespace Zuuned::Diagnostics {
namespace {
QString replaceMatches(QString value, const QRegularExpression &pattern,
                       const std::function<QString(const QRegularExpressionMatch &)> &replacement)
{
    const auto matches = pattern.globalMatch(value);
    QList<QRegularExpressionMatch> captured;
    for (auto i = matches; i.hasNext();)
        captured.append(i.next());
    for (auto i = captured.crbegin(); i != captured.crend(); ++i)
        value.replace(i->capturedStart(), i->capturedLength(), replacement(*i));
    return value;
}
}

Redactor::Redactor(Context context) : m_context(std::move(context))
{
    // Replace longer values first when one configured secret contains another.
    std::sort(m_context.secrets.begin(), m_context.secrets.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
}

QString Redactor::sanitize(const QString &input) const
{
    QString text = input;
    // Prevent terminal control sequences from concealing labels before matching.
    static const QRegularExpression ansi(QStringLiteral("\\x1b(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1b\\\\))"));
    text.remove(ansi);
    text.replace(QChar::Null, QLatin1Char(' '));

    for (const auto &secret : m_context.secrets) {
        if (secret.isEmpty()) continue;
        text.replace(secret, QStringLiteral("[redacted]"));
        const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(secret));
        if (encoded != secret)
            text.replace(encoded, QStringLiteral("[redacted]"), Qt::CaseInsensitive);
    }

    // These are live libzune debug dumps, not errors. Never persist their
    // payloads. Unlabelled hex/hex+ASCII continuation rows are excluded too.
    static const QRegularExpression dumps(QStringLiteral(
        "^.*(?:\\[mtpz\\].*(?:SessionInitiatorInfo data|first\\s+32|last\\s+16)|"
        "(?:CMD|DATA)\\s+hdr\\s*:|(?:raw|hex)\\s+dump\\s*:).*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    text.replace(dumps, QStringLiteral("[diagnostics] protocol payload omitted"));
    static const QRegularExpression hexRows(QStringLiteral(
        "^[ \\t]*(?:(?:[0-9a-f]{4,8})[ \\t]*[:|][ \\t]*)?(?:[0-9a-f]{2}[ \\t]+){8,}[^\\r\\n]*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    text.replace(hexRows, QStringLiteral("[diagnostics] protocol payload omitted"));
    static const QRegularExpression hexFragments(QStringLiteral("^[ \\t]*[0-9a-f]{16,}[. \\t]*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    text.replace(hexFragments, QStringLiteral("[diagnostics] protocol payload omitted"));
    static const QRegularExpression longHex(QStringLiteral("\\b[0-9a-f]{64,}\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    text.replace(longHex, QStringLiteral("[redacted hex]"));
    static const QRegularExpression deviceName(QStringLiteral(
        "(\\[libzune\\] connected: )[^\\r\\n]*?(?=\\s\\([^\\r\\n]*\\), battery:)"));
    text.replace(deviceName, QStringLiteral("\\1[device name]"));

    // Keep provider/host/error context, but discard all URL query values and
    // fragments: even a harmless-looking query can contain a private title.
    static const QRegularExpression urls(QStringLiteral("[a-z][a-z0-9+.-]*://[^\\s<>\\\"']+"),
                                          QRegularExpression::CaseInsensitiveOption);
    text = replaceMatches(text, urls, [](const auto &match) {
        QString url = match.captured();
        const auto scheme = url.indexOf(QStringLiteral("://")) + 3;
        auto authorityEnd = url.indexOf(QRegularExpression(QStringLiteral("[/?#]")), scheme);
        if (authorityEnd < 0) authorityEnd = url.size();
        const auto at = url.lastIndexOf(QLatin1Char('@'), authorityEnd - 1);
        if (at >= scheme)
            url.replace(scheme, at - scheme + 1, QStringLiteral("[redacted]@"));
        const auto hash = url.indexOf(QLatin1Char('#'));
        if (hash >= 0) url = url.left(hash) + QStringLiteral("#[redacted]");
        const auto query = url.indexOf(QLatin1Char('?'));
        if (query >= 0) url = url.left(query) + QStringLiteral("?[redacted]");
        return url;
    });

    // Match quoted/unquoted JSON, headers, INI, command-line and diagnostic
    // spellings. A quoted value may contain spaces, escapes or '&'.
    static const QRegularExpression headers(QStringLiteral(
        "\\b((?:Proxy-)?Authorization|(?:Set-)?Cookie)[ \\t]*:[^\\r\\n]*"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(headers, QStringLiteral("\\1: [redacted]"));
    static const QRegularExpression credentials(QStringLiteral(
        "(?<![\\w])((?:[\\\"']?)(?:"
        "(?:x[-_])?api[-_ ]?key|access[-_ ]?token|refresh[-_ ]?token|auth[-_ ]?token|"
        "token|secret|client[-_ ]?secret|password|passwd|passphrase|authorization|"
        "proxy[-_ ]authorization|cookie|set[-_ ]cookie|"
        "(?:device[-_ ]?)?serial(?:[-_ ]?(?:number|no))?|"
        "(?:private|encryption|session|mtpz)[-_ ]?key|"
        "(?:device[-_ ]?|friendly[-_ ]?)name|username|user[-_ ]?name"
        ")(?:[\\\"']?)[ \\t]*(?:[:=][ \\t]*|[ \\t]+))"
        "(?:\\\"(?:\\\\.|[^\\\"\\r\\n])*\\\"|'(?:\\\\.|[^'\\r\\n])*'|"
        "[^\\r\\n,;}]*?(?=[ \\t]+(?:--)?[\\w.-]+[ \\t]*[:=]|[,;}\\r\\n]|$))"),
        QRegularExpression::CaseInsensitiveOption);
    text = replaceMatches(text, credentials, [](const auto &match) {
        return match.captured(1) + QStringLiteral("[redacted]");
    });
    static const QRegularExpression bearer(QStringLiteral("\\b(?:Bearer|Basic)[ \\t]+[a-z0-9._~+/=-]+"),
                                            QRegularExpression::CaseInsensitiveOption);
    text.replace(bearer, QStringLiteral("[redacted authorization]"));
    static const QRegularExpression privateKey(QStringLiteral(
        "-----BEGIN (?:[A-Z ]*PRIVATE KEY|CERTIFICATE)-----[\\s\\S]*?(?:-----END [A-Z ]+-----|$)"));
    text.replace(privateKey, QStringLiteral("[redacted key material]"));

    if (!m_context.homePath.isEmpty() && m_context.homePath != QStringLiteral("/")) {
        QString home = m_context.homePath;
        while (home.endsWith(QLatin1Char('/'))) home.chop(1);
        text.replace(home, QStringLiteral("~"));
        text.replace(QString::fromLatin1(QUrl::toPercentEncoding(home, "/")),
                     QStringLiteral("~"), Qt::CaseInsensitive);
    }
    // Include other conventional home-directory names that may occur in old
    // session logs copied from a different profile.
    static const QRegularExpression homes(QStringLiteral("/(?:home|Users)/[^/\\s\\\"']+"));
    text.replace(homes, QStringLiteral("~"));
    if (!m_context.userName.isEmpty()) {
        const QRegularExpression user(QStringLiteral("(?<![\\w])%1(?![\\w])")
                                          .arg(QRegularExpression::escape(m_context.userName)),
                                      QRegularExpression::CaseInsensitiveOption);
        text.replace(user, QStringLiteral("[user]"));
    }
    return text;
}
} // namespace Zuuned::Diagnostics
