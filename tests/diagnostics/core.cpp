#include "diagnostics/SessionLog.h"
#include "diagnostics/Redactor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Zuuned::Diagnostics;
namespace {
int checks = 0;
void check(bool ok, const char *description)
{
    ++checks;
    if (!ok) { std::printf("FAIL: %s\n", description); std::exit(1); }
    std::printf("PASS: %s\n", description);
}
QByteArray diskLogs(const QString &path)
{
    QByteArray result;
    for (const auto &name : QDir(path).entryList({"zuuned-*.log"}, QDir::Files, QDir::Name)) {
        QFile f(path + '/' + name);
        if (f.open(QIODevice::ReadOnly)) result += f.readAll();
    }
    return result;
}
SessionLog::Limits quietLimits()
{
    SessionLog::Limits limits;
    limits.mirrorConsole = false;
    return limits;
}
void testRedaction()
{
    Redactor redactor({"/home/real-user", "real-user", {"top secret/+", "a-short-known-key"}});
    const QStringList cases {
        "https://name:network-pass@server.invalid/path?api_key=url-secret&query=private+title#private-fragment",
        "HTTP/1 Authorization: Bearer ABC.DEFG.123\nCookie: account=private-cookie; Session=yes",
        "{\"api_key\": \"json-secret\", \"password\": \"with spaces\", \"userName\": \"hidden-person\"}",
        "--api-key cli-secret --password cli-pass",
        "password='single quote secret' token=raw-secret&foo=bar",
        "token=top secret/+ encoded=top%20secret%2F%2B exact=a-short-known-key",
        "device_serial='serial-secret' filename=/home/real-user/Music/test.mp3 real-user",
        "file:///home/real-user/Photos/a.png /Users/old-person/Library/hello",
        "[mtpz] SessionInitiatorInfo data (384 bytes): 112233aabbcc...\n[mtpz] first 32: 102030aabbcc\n[mtpz] last 16: 203040aabbcc",
        "    61 00 6c 00 69 00 63 00 65 00 20 00 | secret-ascii",
        "[libzune] GetDeviceInfo: model='Zune' serial='serial-secret'",
        "[libzune] connected: A private device (Zune), battery: 98%, storage: 123/456",
        "-----BEGIN PRIVATE KEY-----\nbase64-private-material\n-----END PRIVATE KEY-----",
        "Authorization:\x1b[31m Basic aGVsbG9zZWNyZXQ=\x1b[0m",
    };
    const auto out = redactor.sanitize(cases.join('\n'));
    for (const auto *hidden : {"network-pass", "url-secret", "private+title", "private-fragment", "ABC.DEFG", "private-cookie",
                              "json-secret", "with spaces", "hidden-person", "cli-secret", "cli-pass", "single quote secret",
                              "raw-secret", "top secret/+", "top%20secret", "a-short-known-key", "serial-secret", "real-user",
                              "old-person", "112233aabbcc", "102030aabbcc", "203040aabbcc", "secret-ascii", "A private device",
                              "base64-private-material", "aGVsbG9zZWNyZXQ"})
        check(!out.contains(QLatin1String(hidden)), hidden);
    check(out.contains("https://[redacted]@server.invalid/path?[redacted]"), "URL provider and path retained without credentials");
    check(out.contains("battery: 98%, storage: 123/456"), "Device capacity retained without personal name");
    const QString errors = "[mtpz] SessionInitiatorInfo failed (rc=0x2005)\n[libusb] bulk_write failed: timeout\n[ptp] sent 23 MB (15.0 MB/s)";
    check(redactor.sanitize(errors) == errors, "Transport errors and protocol codes stay useful");
    check(redactor.sanitize("[provider] api_key=provider-secret failure=503").contains("failure=503"),
          "Sensitive value removal preserves following failure fields");
    check(!redactor.sanitize("deadbeef0102030405060708090a0b0c\nrevision=6dbf632212a1509d16101f0ff944a75b3ae6bdca")
               .contains("deadbeef")
          && redactor.sanitize("revision=6dbf632212a1509d16101f0ff944a75b3ae6bdca")
               .contains("6dbf632212a1509d16101f0ff944a75b3ae6bdca"),
          "Unlabelled auth hex fragments excluded while build revisions survive");
}
void testCapture()
{
    QTemporaryDir root;
    const auto path = root.path() + "/logs";
    SessionLog log;
    log.setRedactionContext({"/home/test-person", "test-person", {"known-secret"}});
    check(log.start(path, quietLimits()), "Native capture starts");
    std::fprintf(stderr, "[native] native stderr /home/test-person/song.wav token=known-secret\n");
    qWarning().noquote() << "QML warning test /home/test-person/scene.qml";
    const QByteArray direct = "[syscall] write fd2 rc=0x2005\n";
    ::write(STDERR_FILENO, direct.constData(), direct.size());
    const auto current = log.snapshot();
    check(current.contains("[native]") && current.contains("[qt.warning]") && current.contains("[syscall]"),
          "C stdio, Qt warnings and direct writes captured before export");
    check(current.contains("session started") && current.contains("rc=0x2005"), "Session marker and exact failure codes recorded");
    check(!current.contains("test-person") && !current.contains("known-secret"), "Secrets removed from live snapshot");
    check(!diskLogs(path).contains("known-secret") && !diskLogs(path).contains("test-person"), "Secrets removed before disk persistence");
    check(QRegularExpression("^\\d{4}-\\d{2}-\\d{2}T[^ ]+Z ").match(QString::fromUtf8(current)).hasMatch(), "Records have UTC timestamps");
    struct stat info {};
    ::stat(path.toUtf8().constData(), &info);
    check((info.st_mode & 0777) == 0700, "Log directory is private");
    bool permissions = true;
    for (const auto &name : QDir(path).entryList(QDir::Files)) {
        ::stat((path + '/' + name).toUtf8().constData(), &info);
        permissions &= (info.st_mode & 0777) == 0600;
    }
    check(permissions, "Log files are owner-only");
    std::fprintf(stderr, "[partial] api_key=unfinished-secret");
    const auto partial = log.snapshot();
    check(partial.contains("incomplete line:") && !partial.contains("unfinished-secret"), "Snapshot safely includes an unfinished line");
    std::fprintf(stderr, "-continued\n");
    log.stop();
    check(log.snapshot().contains("session ended"), "Shutdown drains stderr and leaves evidence readable");
    check(!diskLogs(path).contains("unfinished-secret") && !diskLogs(path).contains("-continued"), "Partial credential is never split across redaction");
    check(!log.isAvailable(), "Stopped logger reports unavailable");
    check(log.start(path, quietLimits()), "Second session starts safely");
    std::fprintf(stderr, "[next-session] hello\n");
    const auto next = log.snapshot();
    check(next.contains("[native]") && next.contains("[next-session]"), "Report includes current and previous session logs");
    log.stop();
}
void testConcurrencyAndBounds()
{
    QTemporaryDir root;
    SessionLog log;
    auto limits = quietLimits();
    limits.lineBytes = 256;
    check(log.start(root.path(), limits), "Concurrent capture starts");
    std::vector<std::thread> workers;
    for (int thread = 0; thread < 4; ++thread)
        workers.emplace_back([thread] {
            for (int line = 0; line < 120; ++line)
                std::fprintf(stderr, "[parallel] thread=%d line=%d\n", thread, line);
        });
    for (auto &worker : workers) worker.join();
    const auto output = log.snapshot();
    check(output.count("[parallel]") == 480, "All atomic lines from four threads retained");
    QByteArray giant = "[huge] token=" + QByteArray(100000, 'x') + "tail-secret\n[sentinel] alive\n";
    // Blocking pipe writes intentionally preserve writer order/backpressure;
    // the reader discards the oversized tail without growing its buffer.
    qsizetype offset = 0;
    while (offset < giant.size()) {
        const auto size = ::write(STDERR_FILENO, giant.constData() + offset, giant.size() - offset);
        if (size <= 0) break;
        offset += size;
    }
    const auto huge = log.snapshot();
    check(huge.contains("[sentinel] alive") && !huge.contains("tail-secret") && !huge.contains(QByteArray(40, 'x')),
          "Oversized secret line is bounded and subsequent messages survive");
    std::fprintf(stderr, "-----BEGIN PRIVATE KEY-----\nbase64privatebody\n-----END PRIVATE KEY-----\n[after-key] fine\n");
    const auto key = log.snapshot();
    check(!key.contains("base64privatebody") && !diskLogs(root.path()).contains("base64privatebody") && key.contains("[after-key]"),
          "Multiline private-key material never reaches disk");
    check(log.snapshot(600).size() <= 600, "Export snapshot byte bound enforced");
    log.stop();

    QTemporaryDir rotated;
    limits.fileBytes = 512; limits.files = 3;
    check(log.start(rotated.path(), limits), "Small rotating capture starts");
    for (int i = 0; i < 80; ++i) std::fprintf(stderr, "[rotation] line=%d abcdefghijklmnopqrstuvwxyz\n", i);
    log.stop();
    const auto files = QDir(rotated.path()).entryInfoList({"zuuned-*.log"}, QDir::Files);
    bool bounded = files.size() <= 3;
    for (const auto &file : files) bounded &= file.size() <= 512;
    check(bounded && files.size() == 3, "Rotation limits both file count and individual bytes");
    const auto tail = log.snapshot();
    check(tail.contains("line=79") && !tail.contains("line=0 "), "Rotation preserves newest evidence");
}
void testFailures()
{
    SessionLog log;
    struct stat before {}, after {};
    ::fstat(STDERR_FILENO, &before);
    check(!log.start("/proc/zuuned-cannot-create/logs", quietLimits()) && !log.error().isEmpty(), "Unavailable storage fails without aborting");
    ::fstat(STDERR_FILENO, &after);
    check(before.st_dev == after.st_dev && before.st_ino == after.st_ino, "Startup failure leaves stderr untouched");
    QTemporaryDir root;
    const auto target = root.path() + "/target";
    QDir().mkdir(target);
    ::symlink(target.toUtf8().constData(), (root.path() + "/symlink").toUtf8().constData());
    check(!log.start(root.path() + "/symlink", quietLimits()), "Symlink log directory refused");
    auto limits = quietLimits(); limits.fileBytes = 512;
    const auto removable = root.path() + "/removed";
    check(log.start(removable, limits), "Disk failure rehearsal starts");
    QDir(removable).removeRecursively();
    for (int i = 0; i < 60; ++i) std::fprintf(stderr, "[disk-error] native output %d\n", i);
    log.snapshot();
    check(!log.isAvailable() && !log.error().isEmpty(), "Rotation write failure is visible while application continues");
    log.stop();
    check(log.start(target, quietLimits()), "Capture can recover on a later session");
    SessionLog second;
    check(!second.start(root.path() + "/other", quietLimits()), "Concurrent logger ownership refused");
    QFile secret(root.path() + "/private.txt");
    check(secret.open(QIODevice::WriteOnly), "External file fixture opens");
    secret.write("external secret must not be exported"); secret.close();
    ::symlink(secret.fileName().toUtf8().constData(), (target + "/zuuned-0000.log").toUtf8().constData());
    check(!log.snapshot().contains("external secret"), "Snapshot excludes symlink log files");
    log.stop();
}
void testConsole(const QString &executable)
{
    QTemporaryDir root;
    QFile mirror(root.path() + "/console.txt"), stdoutFile(root.path() + "/stdout.txt");
    check(mirror.open(QIODevice::ReadWrite) && stdoutFile.open(QIODevice::ReadWrite), "Console fixtures open");
    std::fflush(stdout); std::fflush(stderr);
    const int savedError = ::dup(STDERR_FILENO), savedOutput = ::dup(STDOUT_FILENO);
    ::dup2(mirror.handle(), STDERR_FILENO);
    ::dup2(stdoutFile.handle(), STDOUT_FILENO);
    SessionLog log;
    bool started = log.start(root.path() + "/logs");
    std::fprintf(stderr, "[mirror] message password=console-secret\n");
    std::printf("version-style stdout remains unchanged\n");
    std::fflush(stdout);
    const auto snapshot = log.snapshot();
    log.stop();
    ::dup2(savedError, STDERR_FILENO); ::close(savedError);
    ::dup2(savedOutput, STDOUT_FILENO); ::close(savedOutput);
    mirror.seek(0); stdoutFile.seek(0);
    const auto console = mirror.readAll();
    check(started && console.contains("[mirror] message") && !console.contains("console-secret"), "Console mirror preserves useful output and redacts secrets");
    check(stdoutFile.readAll() == "version-style stdout remains unchanged\n" && !snapshot.contains("version-style"), "stdout is untouched by logging");

    check(log.start(root.path() + "/logs", quietLimits()), "Cross-process lock rehearsal starts");
    QProcess child;
    child.start(executable, {"--lock-child", root.path() + "/logs"});
    check(child.waitForFinished(10000) && child.exitCode() == 0, "A second process cannot rotate the active log");
    std::fprintf(stderr, "[lock-owner] still logging\n");
    check(log.snapshot().contains("[lock-owner]"), "Directory lock rejection leaves owner's capture intact");
    log.stop();
}
void testFatal(const QString &executable)
{
    QTemporaryDir root;
    QProcess child;
    child.start(executable, {"--fatal-child", root.path()});
    check(child.waitForFinished(10000), "Fatal logging subprocess exits");
    const auto evidence = diskLogs(root.path());
    check(evidence.contains("[qt.fatal]") && evidence.contains("fatal-evidence") && !evidence.contains("fatal-secret"),
          "Qt fatal message reaches sanitized disk log before abort");
    SessionLog next;
    check(next.start(root.path(), quietLimits()), "Restart after fatal session succeeds");
    check(next.snapshot().contains("fatal-evidence"), "Previous failed session is available after restart");
    next.stop();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QByteArray(argv[1]) == "--fatal-child") {
        // Exercise Qt's fatal route without making a core dump/desktop alert.
        ::signal(SIGABRT, [](int) { ::_exit(86); });
        SessionLog log;
        if (!log.start(QString::fromLocal8Bit(argv[2]), quietLimits())) return 2;
        qFatal("fatal-evidence password=fatal-secret");
    }
    if (argc == 3 && QByteArray(argv[1]) == "--lock-child") {
        SessionLog log;
        return log.start(QString::fromLocal8Bit(argv[2]), quietLimits()) ? 3 : 0;
    }
    testRedaction(); testCapture(); testConcurrencyAndBounds(); testFailures();
    testConsole(app.applicationFilePath());
    testFatal(app.applicationFilePath());
    std::printf("%d diagnostic core checks passed\n", checks);
}
