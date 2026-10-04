#pragma once

#include "PrintRenderer.h"

#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>

namespace ArtworkPrint {

// Shared local-only service behind ArtworkRequest. All methods are called on
// the owning (GUI) thread; file I/O, decoding, rendering and pruning run on its
// own two-worker pool. Config/Renderer injection keeps native tests isolated.
class Pipeline final : public QObject {
public:
    struct Config {
        QString cacheDirectory;
        int workers = 2;
        int pendingJobs = 96;
        int memoryKiB = 64 * 1024;
        int diskEntries = 512;
        qint64 diskBytes = 256 * 1024 * 1024;
    };
    struct Output {
        QUrl url;
        QString error;
        // Keep displayed/in-flight files out of eviction. Idle cache bounds
        // exclude these leases and currently-read source files.
        std::shared_ptr<void> lease;
    };
    using Renderer = std::function<Result(const QImage &, const Options &)>;
    using Completion = std::function<void(Output)>;

    explicit Pipeline(Config config, QObject *parent = nullptr, Renderer renderer = {});
    ~Pipeline() override;
    static Pipeline *shared();
    static QString localPath(const QUrl &source);

    // Zero means capacity is temporarily full: callers retry, retaining busy.
    // refresh bypasses a job whose input snapshot predates an explicit refresh.
    quint64 submit(const QUrl &source, const Options &options, QObject *receiver,
                   Completion completion, bool refresh = false);
    void cancel(quint64 subscription);
    std::shared_ptr<void> protectSource(const QUrl &source);
    void releaseUnused();

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace ArtworkPrint
