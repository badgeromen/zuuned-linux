#pragma once

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// One canonical rule is shared by packages, the password-prompt installer,
// and the manual command. Keep it before libmtp's probe and seat-late's ACLs.
namespace UdevSetup {
inline QString rules() {
    QFile file(QStringLiteral(":/zuuned/packaging/68-zuuned.rules"));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

inline QString shellQuote(QString text) {
    text.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + text + QLatin1Char('\'');
}

inline QString installScript(const QString &content,
                             const QString &directory = QStringLiteral("/etc/udev/rules.d")) {
    if (content.trimmed().isEmpty()) return {};
    const QString early = shellQuote(directory + QStringLiteral("/68-zuuned.rules"));
    const QString late = shellQuote(directory + QStringLiteral("/72-zuuned.rules"));
    return QStringLiteral("install -d -m 0755 %1 && printf '%s' %2 > %3 "
                          "&& install -m 0644 %3 %4 && chmod 0644 %3 "
                          "&& udevadm control --reload-rules "
                          "&& udevadm trigger --action=add --subsystem-match=usb --attr-match=idVendor=045e")
        .arg(shellQuote(directory), shellQuote(content), early, late);
}

inline QString effectiveRules(const QString &content) {
    QStringList lines;
    for (const auto &line : content.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')))
            lines.append(trimmed);
    }
    return lines.join(QLatin1Char('\n'));
}

inline bool installed(const QString &expected,
                      const QString &etcDirectory = QStringLiteral("/etc/udev/rules.d"),
                      const QString &usrDirectory = QStringLiteral("/usr/lib/udev/rules.d")) {
    const QString wanted = effectiveRules(expected);
    if (wanted.isEmpty()) return false;
    for (const auto &name : {QStringLiteral("68-zuuned.rules"), QStringLiteral("72-zuuned.rules")}) {
        bool matched = false;
        // /etc shadows an identically named distribution rule, even if empty.
        for (const auto &directory : {etcDirectory, usrDirectory}) {
            QFile file(directory + QLatin1Char('/') + name);
            if (!file.exists() && !QFileInfo(file).isSymLink()) continue;
            matched = file.open(QIODevice::ReadOnly)
                && effectiveRules(QString::fromUtf8(file.readAll())) == wanted;
            break;
        }
        if (!matched) return false;
    }
    return true;
}
}
