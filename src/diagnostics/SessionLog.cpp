#include "SessionLog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QUuid>
#include <QtLogging>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace Zuuned::Diagnostics {
namespace {
QString systemError(const QString &action)
{
    return action + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno));
}
bool writeAll(int fd, const QByteArray &bytes)
{
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const auto written = ::write(fd, bytes.constData() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += written;
    }
    return true;
}
void closeFd(int &fd)
{
    if (fd >= 0) ::close(fd);
    fd = -1;
}
}

struct SessionLog::Impl {
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::condition_variable drained;
    std::thread reader;
    Redactor redactor;
    Limits limits;
    QString folder, failure, session, filename;
    int directoryFd = -1, fileFd = -1, originalStderr = -1, consoleFd = -1;
    int captureRead = -1, controlRead = -1, controlWrite = -1;
    qint64 fileSize = 0;
    int part = 0;
    bool running = false, stopping = false, truncating = false, privateKeyBlock = false;
    quint64 requested = 0, acknowledged = 0;
    QByteArray pending;
    QtMessageHandler previousHandler = nullptr;
    static std::atomic<Impl *> active;

    static void qtMessage(QtMsgType type, const QMessageLogContext &context,
                          const QString &message)
    {
        const char *level = type == QtDebugMsg ? "debug" : type == QtInfoMsg ? "info"
                           : type == QtWarningMsg ? "warning" : type == QtCriticalMsg ? "critical" : "fatal";
        const auto bytes = qFormatLogMessage(type, context, message).toUtf8();
        std::fprintf(stderr, "[qt.%s] %s\n", level, bytes.constData());
        if (type == QtFatalMsg)
            if (auto *log = active.load(std::memory_order_acquire)) log->requestDrain();
    }

    QList<QFileInfo> logFiles() const
    {
        QList<QFileInfo> result;
        const auto entries = QDir(folder).entryInfoList({QStringLiteral("zuuned-*.log")},
                                                       QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const auto &entry : entries) {
            struct stat info {};
            const auto name = entry.fileName().toUtf8();
            if (::fstatat(directoryFd, name.constData(), &info, AT_SYMLINK_NOFOLLOW) == 0
                && S_ISREG(info.st_mode) && info.st_uid == ::geteuid())
                result.append(entry);
        }
        return result;
    }

    void prune()
    {
        auto files = logFiles();
        // Lexical UTC session order is stable when filesystem mtimes tie.
        while (files.size() > limits.files) {
            auto oldest = std::find_if(files.begin(), files.end(), [&](const auto &entry) {
                return entry.fileName() != filename;
            });
            if (oldest == files.end()) break;
            const auto name = oldest->fileName().toUtf8();
            if (::unlinkat(directoryFd, name.constData(), 0) != 0) {
                failure = systemError(QStringLiteral("Could not rotate diagnostic logs"));
                closeFd(fileFd);
                return;
            }
            files.erase(oldest);
        }
    }

    bool openPart()
    {
        closeFd(fileFd);
        filename = QStringLiteral("zuuned-%1-%2.log").arg(session).arg(part++, 4, 10, QLatin1Char('0'));
        const auto name = filename.toUtf8();
        fileFd = ::openat(directoryFd, name.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        fileSize = 0;
        if (fileFd < 0) {
            failure = systemError(QStringLiteral("Could not create diagnostic log"));
            return false;
        }
        prune();
        return fileFd >= 0;
    }

    void append(const QByteArray &line, bool console = true)
    {
        // Called under mutex; sanitized bytes are the only persistent content.
        if (line.contains("-----BEGIN ")
            && (line.contains("PRIVATE KEY-----") || line.contains("CERTIFICATE-----")))
            privateKeyBlock = true;
        const bool hiddenKey = privateKeyBlock;
        if (privateKeyBlock && line.contains("-----END ")) privateKeyBlock = false;
        const auto clean = hiddenKey ? QByteArray("[diagnostics] key material omitted")
                                    : redactor.sanitize(QString::fromUtf8(line)).toUtf8();
        if (console && consoleFd >= 0) {
            pollfd output {consoleFd, POLLOUT, 0};
            if (::poll(&output, 1, 0) > 0 && (output.revents & POLLOUT)
                && !(output.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                const QByteArray message = clean + '\n';
                // The independently reopened descriptor is nonblocking. A slow
                // terminal may lose mirrored output without stalling capture.
                (void)::write(consoleFd, message.constData(), message.size());
            }
        }
        if (fileFd < 0) return;
        QByteArray record = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
                            + ' ' + clean + '\n';
        if (record.size() > limits.fileBytes) {
            record = record.left(limits.fileBytes - 28) + " [diagnostic line clipped]\n";
        }
        if (fileSize + record.size() > limits.fileBytes && !openPart()) return;
        if (!writeAll(fileFd, record)) {
            failure = systemError(QStringLiteral("Could not write diagnostic log"));
            closeFd(fileFd);
            return;
        }
        fileSize += record.size();
    }

    void consume(const char *data, qsizetype size)
    {
        std::lock_guard guard(mutex);
        for (qsizetype i = 0; i < size; ++i) {
            const char byte = data[i];
            if (byte == '\n' || byte == '\r') {
                if (!pending.isEmpty() || truncating) {
                    if (truncating) pending += " [remainder of oversized line omitted]";
                    append(pending);
                }
                pending.clear();
                truncating = false;
            } else if (!truncating) {
                if (pending.size() < limits.lineBytes) pending += byte;
                else truncating = true;
            }
        }
    }

    void readAvailable()
    {
        char buffer[8192];
        // A continuously noisy child must not starve a snapshot/stop request.
        for (int i = 0; i < 128; ++i) {
            const auto count = ::read(captureRead, buffer, sizeof buffer);
            if (count > 0) consume(buffer, count);
            else if (count < 0 && errno == EINTR) --i;
            else break;
        }
    }

    void loop()
    {
        // Only this thread mirrors to a potentially closed terminal pipe.
        // A raced EPIPE must never abort the application.
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
        while (true) {
            pollfd inputs[] {{captureRead, POLLIN, 0}, {controlRead, POLLIN, 0}};
            const auto result = ::poll(inputs, 2, 100);
            if (result < 0 && errno == EINTR) continue;
            quint64 target;
            { std::lock_guard guard(mutex); target = requested; }
            if (inputs[1].revents & POLLIN) {
                char commands[64];
                while (::read(controlRead, commands, sizeof commands) > 0) {}
            }
            // Read after capturing the requested cut, including requests that
            // arrived just after poll returned. Never acknowledge future cuts.
            readAvailable();
            std::lock_guard guard(mutex);
            acknowledged = target;
            drained.notify_all();
            if (stopping) {
                if (!pending.isEmpty()) append(pending + " [unterminated line]");
                pending.clear();
                append("[diagnostics] session ended", false);
                break;
            }
        }
    }

    void requestDrain()
    {
        std::unique_lock guard(mutex);
        if (!running || stopping) return;
        const auto target = ++requested;
        const char command = 'f';
        (void)::write(controlWrite, &command, 1);
        drained.wait_for(guard, std::chrono::seconds(2), [&] { return acknowledged >= target || !running; });
    }
};

std::atomic<SessionLog::Impl *> SessionLog::Impl::active = nullptr;

SessionLog &SessionLog::instance()
{
    static SessionLog log;
    return log;
}
SessionLog::SessionLog() : d(std::make_unique<Impl>()) {}
SessionLog::~SessionLog() { stop(); closeFd(d->directoryFd); }
bool SessionLog::start(const QString &directory) { return start(directory, Limits{}); }

bool SessionLog::start(const QString &directory, Limits limits)
{
    std::lock_guard lifecycle(d->lifecycle);
    std::lock_guard guard(d->mutex);
    if (d->running) return true;
    closeFd(d->directoryFd);
    d->failure.clear();
    if (limits.fileBytes < 256 || limits.fileBytes > 16 * 1024 * 1024
        || limits.files < 2 || limits.files > 16 || limits.lineBytes < 64
        || limits.lineBytes > 64 * 1024) {
        d->failure = QStringLiteral("Invalid diagnostic log limits");
        return false;
    }
    Impl *expected = nullptr;
    if (!Impl::active.compare_exchange_strong(expected, d.get())) {
        d->failure = QStringLiteral("Another diagnostic capture is active");
        return false;
    }
    auto failed = [&] {
        closeFd(d->fileFd); closeFd(d->directoryFd); closeFd(d->consoleFd);
        closeFd(d->originalStderr); closeFd(d->captureRead);
        closeFd(d->controlRead); closeFd(d->controlWrite);
        Impl::active.store(nullptr);
        return false;
    };
    d->folder = QDir::cleanPath(directory);
    d->limits = limits;
    const QFileInfo dirInfo(d->folder);
    if (directory.isEmpty() || !dirInfo.isAbsolute() || dirInfo.isSymLink()
        || !QDir().mkpath(d->folder)) {
        d->failure = QStringLiteral("Could not create a private diagnostic directory");
        return failed();
    }
    const auto path = d->folder.toUtf8();
    d->directoryFd = ::open(path.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info {};
    if (d->directoryFd < 0 || ::fstat(d->directoryFd, &info) != 0
        || info.st_uid != ::geteuid() || ::fchmod(d->directoryFd, 0700) != 0) {
        d->failure = QStringLiteral("Diagnostic directory is not private or writable");
        return failed();
    }
    if (::flock(d->directoryFd, LOCK_EX | LOCK_NB) != 0) {
        d->failure = QStringLiteral("Another process is already recording diagnostics in this directory");
        return failed();
    }
    d->session = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"))
                 + QStringLiteral("-%1-").arg(::getpid())
                 + QUuid::createUuid().toString(QUuid::Id128).left(8);
    d->part = 0;
    if (!d->openPart()) return failed();

    int capture[2] {-1, -1}, control[2] {-1, -1};
    if (::pipe2(capture, O_CLOEXEC) != 0 || ::pipe2(control, O_CLOEXEC | O_NONBLOCK) != 0) {
        d->failure = systemError(QStringLiteral("Could not start diagnostic capture"));
        closeFd(capture[0]); closeFd(capture[1]); closeFd(control[0]); closeFd(control[1]);
        return failed();
    }
    d->captureRead = capture[0];
    ::fcntl(d->captureRead, F_SETFL, O_NONBLOCK);
    d->controlRead = control[0]; d->controlWrite = control[1];
    std::fflush(stderr);
    d->originalStderr = ::fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    // Reopening avoids changing O_NONBLOCK on the caller's shared descriptor.
    if (limits.mirrorConsole)
        d->consoleFd = ::open("/proc/self/fd/2", O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_APPEND);
    if (d->originalStderr < 0 || ::dup2(capture[1], STDERR_FILENO) < 0) {
        d->failure = systemError(QStringLiteral("Could not redirect diagnostic output"));
        closeFd(capture[1]);
        return failed();
    }
    closeFd(capture[1]);
    d->pending.clear(); d->truncating = false; d->privateKeyBlock = false;
    d->stopping = false; d->requested = d->acknowledged = 0;
    d->running = true;
    d->append("[diagnostics] session started", false);
    try {
        d->reader = std::thread([state = d.get()] { state->loop(); });
    } catch (...) {
        ::dup2(d->originalStderr, STDERR_FILENO);
        d->running = false;
        d->failure = QStringLiteral("Could not start diagnostic reader thread");
        return failed();
    }
    d->previousHandler = qInstallMessageHandler(Impl::qtMessage);
    return true;
}

void SessionLog::stop()
{
    std::lock_guard lifecycle(d->lifecycle);
    {
        std::lock_guard guard(d->mutex);
        if (!d->running) return;
    }
    qInstallMessageHandler(d->previousHandler);
    std::fflush(stderr);
    // Restore fd2 before asking the reader to stop: concurrent late writers
    // continue to the original destination instead of receiving SIGPIPE.
    ::dup2(d->originalStderr, STDERR_FILENO);
    {
        std::lock_guard guard(d->mutex);
        d->stopping = true;
        const char command = 's';
        (void)::write(d->controlWrite, &command, 1);
    }
    if (d->reader.joinable()) d->reader.join();
    std::lock_guard guard(d->mutex);
    d->running = false;
    closeFd(d->fileFd); closeFd(d->consoleFd); closeFd(d->originalStderr);
    closeFd(d->captureRead); closeFd(d->controlRead); closeFd(d->controlWrite);
    // Keep the directory descriptor for previous-session snapshots after stop.
    ::flock(d->directoryFd, LOCK_UN);
    Impl::active.store(nullptr);
    d->drained.notify_all();
}

bool SessionLog::isAvailable() const
{
    std::lock_guard guard(d->mutex);
    return d->running && d->fileFd >= 0 && d->failure.isEmpty();
}
QString SessionLog::directory() const { std::lock_guard guard(d->mutex); return d->folder; }
QString SessionLog::error() const { std::lock_guard guard(d->mutex); return d->failure; }
void SessionLog::setRedactionContext(RedactionContext context)
{
    std::lock_guard guard(d->mutex);
    d->redactor = Redactor(std::move(context));
}

QByteArray SessionLog::snapshot(qint64 maxBytes)
{
    if (maxBytes <= 0) return {};
    maxBytes = std::min(maxBytes, qint64(8 * 1024 * 1024));
    d->requestDrain();
    QByteArray result;
    QByteArray incomplete;
    Redactor redactor;
    struct FileCut {
        int fd;
        qint64 count, offset;
    };
    std::vector<FileCut> cuts;
    {
        std::lock_guard guard(d->mutex);
        if (d->directoryFd < 0) return result;
        redactor = d->redactor;
        if (!d->pending.isEmpty() && !d->privateKeyBlock)
            incomplete = "[diagnostics] incomplete line: " + d->pending + '\n';
        const auto files = d->logFiles();
        qint64 budget = maxBytes;
        // Pin only a bounded descriptor/size cut. Rotation may unlink a file
        // afterwards; its open descriptor stays valid without blocking logging.
        for (auto i = files.crbegin(); i != files.crend() && budget > 0; ++i) {
            const auto name = i->fileName().toUtf8();
            int fd = ::openat(d->directoryFd, name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
            struct stat info {};
            if (fd < 0) continue;
            if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()) {
                closeFd(fd);
                continue;
            }
            const qint64 count = std::min({budget, qint64(info.st_size), d->limits.fileBytes});
            cuts.push_back({fd, count, qint64(info.st_size) - count});
            budget -= count;
        }
    }
    // Reading and redacting up to several MiB must never hold the reader's
    // mutex: a report export otherwise fills stderr's pipe and stalls writers.
    for (auto &cut : cuts) {
        QByteArray bytes(cut.count, Qt::Uninitialized);
        qsizetype read = 0;
        while (read < bytes.size()) {
            const auto n = ::pread(cut.fd, bytes.data() + read, bytes.size() - read, cut.offset + read);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            read += n;
        }
        closeFd(cut.fd);
        bytes.resize(read);
        if (cut.offset > 0) { // Never export a clipped credential without its label.
            const auto newline = bytes.indexOf('\n');
            bytes = newline < 0 ? QByteArray{} : bytes.mid(newline + 1);
        }
        result.prepend(bytes);
    }
    result += incomplete;
    result = redactor.sanitize(QString::fromUtf8(result)).toUtf8();
    if (result.size() > maxBytes) {
        result = result.right(maxBytes);
        const auto newline = result.indexOf('\n');
        result = newline < 0 ? QByteArray{} : result.mid(newline + 1);
    }
    return result;
}
} // namespace Zuuned::Diagnostics
