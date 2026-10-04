#include "UdevSetup.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int passed = 0, failed = 0;
    const auto check = [&](bool ok, const char *name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        ok ? ++passed : ++failed;
    };
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    QTemporaryDir temp;
    const QString etc = temp.path() + QStringLiteral("/user's etc"), usr = temp.path() + "/usr";
    QDir().mkpath(etc); QDir().mkpath(usr);
    const QString rules = UdevSetup::rules();
    check(!rules.isEmpty() && rules.contains("MTP_NO_PROBE"), "packaged resource contains canonical rule");
    check(!UdevSetup::installed(rules, etc, usr), "missing pair is actionable");
    write(usr + "/99-zune.rules", rules.toUtf8());
    check(!UdevSetup::installed(rules, etc, usr), "legacy late rule does not hide repair");
    write(usr + "/68-zuuned.rules", rules.toUtf8());
    check(!UdevSetup::installed(rules, etc, usr), "early suppression alone does not claim complete setup");
    write(usr + "/72-zuuned.rules", rules.toUtf8());
    check(UdevSetup::installed(rules, etc, usr), "valid package pair is recognized");
    write(etc + "/68-zuuned.rules", {});
    check(!UdevSetup::installed(rules, etc, usr), "empty local override masks package rule");
    QFile::remove(etc + "/68-zuuned.rules");
    QFile::link("/dev/null", etc + "/68-zuuned.rules");
    check(!UdevSetup::installed(rules, etc, usr), "disabled local override masks package rule");
    QFile::remove(etc + "/68-zuuned.rules");
    write(etc + "/unrelated.rules", "untouched");
    const QString bin = temp.path() + "/bin";
    QDir().mkpath(bin);
    write(bin + "/udevadm", "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$ZUUNED_UDEV_TEST_LOG\"\nexit \"${ZUUNED_UDEV_TEST_EXIT:-0}\"\n");
    QFile::setPermissions(bin + "/udevadm", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH", bin + ":" + env.value("PATH"));
    env.insert("ZUUNED_UDEV_TEST_LOG", temp.path() + "/calls");
    process.setProcessEnvironment(env);
    process.start("sh", {"-c", UdevSetup::installScript(rules, etc)});
    check(process.waitForFinished() && process.exitCode() == 0,
          "installer succeeds through stubbed udev with quoted path");
    check(read(etc + "/68-zuuned.rules") == rules.toUtf8()
          && read(etc + "/72-zuuned.rules") == rules.toUtf8(), "installer writes both canonical copies verbatim");
    check(UdevSetup::installed(rules, etc, usr), "installed pair is recognized");
    check(read(etc + "/unrelated.rules") == "untouched", "unrelated rules are preserved");
    check(read(temp.path() + "/calls") == "control --reload-rules\ntrigger --action=add --subsystem-match=usb --attr-match=idVendor=045e\n",
          "reload precedes scoped device trigger");
    write(temp.path() + "/calls", {});
    env.insert("ZUUNED_UDEV_TEST_EXIT", "7"); process.setProcessEnvironment(env);
    process.start("sh", {"-c", UdevSetup::installScript(rules, etc)});
    check(process.waitForFinished() && process.exitCode() == 7
          && read(temp.path() + "/calls") == "control --reload-rules\n",
          "failed reload reports failure and does not trigger devices");
    check(UdevSetup::installScript({}).isEmpty(), "missing resource cannot install empty rules");
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
