// Real libmpv queue gate. Generated silent WAVs, ao=null, isolated XDG paths.
#include "player/PlayerService.h"
#include "player/MpvController.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QVariantMap>
#include <mpv/client.h>
#include <cstdio>
#include <functional>

static bool until(const std::function<bool()> &condition) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return condition();
}
static bool silence(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray samples(8000 * 2 * 10, '\0');
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4); out << quint32(36 + samples.size());
    out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1)
        << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
    out.writeRawData("data", 4); out << quint32(samples.size());
    return out.writeRawData(samples.constData(), samples.size()) == samples.size();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString root = qEnvironmentVariable("CUSTOMIZE_BACKEND_ROOT");
    if (!root.startsWith("/tmp/zuuned-customize-backend-") || qgetenv("ZUUNED_MPV_AO") != "null") return 2;
    int failures = 0;
    std::function<void(const char *)> diagnostics;
    auto check = [&](bool result, const char *label) {
        printf("  %s %s\n", result ? "PASS" : "FAIL", label);
        if (!result) { ++failures; if (diagnostics) diagnostics(label); }
    };
    auto item = [&](const char *name) {
        const auto path = root + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".wav");
        if (!silence(path)) return QVariantMap{};
        return QVariantMap{{"filepath", path}, {"title", QString::fromLatin1(name)}, {"libraryId", -1}};
    };
    const auto a = item("A"), b = item("B"), c = item("C"), d = item("D"), e = item("E");
    if (a.isEmpty() || b.isEmpty() || c.isEmpty() || d.isEmpty() || e.isEmpty()) return 2;
    PlayerService player;
    auto *controller = player.findChild<MpvController *>();
    if (!controller || !controller->isCreated()) return 2;
    auto nativeString = [&](const char *property) {
        char *value = mpv_get_property_string(controller->handle(), property);
        const QString result = value ? QString::fromUtf8(value) : QString();
        mpv_free(value);
        return result;
    };
    auto diagnose = [&](const char *stage) {
        fprintf(stderr, "[player-test] %s: ui index=%d title=%s playing=%d pos=%.3f native index=%s path=%s pause=%s pos=%s duration=%s idle=%s\n",
            stage, player.queueIndex(), qPrintable(player.currentTitle()), player.playing(), player.positionMs(),
            qPrintable(nativeString("playlist-pos")), qPrintable(nativeString("path")),
            qPrintable(nativeString("pause")), qPrintable(nativeString("time-pos")),
            qPrintable(nativeString("duration")), qPrintable(nativeString("idle-active")));
    };
    diagnostics = diagnose;
    auto isCurrent = [&](const QVariantMap &expected, int index) {
        return nativeString("path") == expected.value("filepath").toString()
            && nativeString("playlist-pos").toInt() == index
            && player.queueIndex() == index && player.currentTitle() == expected.value("title").toString();
    };
    auto settle = [&](const QVariantMap &expected, int index) {
        return until([&] { return isCurrent(expected, index)
            && !controller->isPaused() && player.playing() && controller->position() > 0.4; });
    };
    auto mirrorMatches = [&] {
        for (int i = 0; i < player.queue().size(); ++i) {
            const auto key = QStringLiteral("playlist/%1/filename").arg(i).toUtf8();
            if (nativeString(key.constData()) != player.queue()[i].toMap().value("filepath").toString()) return false;
        }
        return nativeString("playlist-count").toInt() == player.queue().size();
    };
    int naturalEnds = 0;
    QObject::connect(controller, &MpvController::fileEnded, &player,
        [&](bool eof) { if (eof) ++naturalEnds; });
    player.setQueue({a, b, c}, 1);
    check(settle(b, 1),
          "fixture starts its selected current song");
    player.playPause();
    check(until([&] { return !player.playing() && controller->isPaused(); }), "fixture pauses playback");
    const double position = controller->position();
    player.playNext({d, e});
    check(player.queue() == QVariantList{a, b, d, e, c} && player.queueIndex() == 1
          && player.currentTitle() == QLatin1String("B"),
          "Play Next inserts in order after the current song, preserving history");
    check(mirrorMatches(), "libmpv playlist exactly mirrors the visible queue");
    check(controller->isPaused() && qAbs(controller->position() - position) < 0.05
          && nativeString("path") == b.value("filepath").toString(),
          "insertion preserves the current file, pause state and playback position");
    player.next();
    check(settle(d, 2),
          "Next actually plays the newly inserted song");

    player.toggleShuffle();
    player.setQueue({a, b, c}, 1);
    check(settle(b, 1), "selected playback stays aligned when replacing a live queue");
    player.playNext({d, e});
    player.next();
    check(settle(d, 2) && player.shuffled(), "manual Next honors the first requested song with shuffle enabled");
    player.next();
    check(settle(e, 3) && player.shuffled(), "manual Next honors the remaining requested order without disabling shuffle");

    player.setQueue({a, b, c}, 1);
    check(settle(b, 1), "shuffle EOF fixture starts its selected song");
    player.playNext({d, e});
    const int beforeEnds = naturalEnds;
    player.seekMs(9850);
    check(settle(d, 2), "actual EOF honors the first Play Next request under shuffle");
    player.seekMs(9850);
    check(settle(e, 3) && player.shuffled() && naturalEnds >= beforeEnds + 2,
          "actual EOF consumes requested order once and preserves shuffle");
    check(mirrorMatches(), "shuffle transitions preserve the native queue mirror");

    player.setQueue({a, b, c}, 1);
    check(settle(b, 1), "multiple-request fixture is ready");
    player.playNext({d, e});
    player.playNext({a});
    player.next();
    check(settle(a, 2), "a new Play Next request goes before older pending requests");
    player.next();
    check(settle(d, 3), "older pending requests keep their order after the newest request");
    player.next();
    check(settle(e, 4), "all pending requests complete in their requested order");

    player.setQueue({a, b, c}, 1);
    player.playNext({e});
    player.setQueue({d, c}, 1);
    player.playPause();
    check(until([&] { return isCurrent(c, 1); }) && controller->isPaused() && !player.playing(),
          "rapid replacement ignores old load completions and preserves a user pause during loading");
    check(mirrorMatches(), "replacement discards old pending requests and leaves an exact native mirror");
    player.playPause();
    check(settle(c, 1), "paused selected load resumes the intended song");
    player.setQueue({a, b, c}, 0);
    player.playNext({d});
    player.jumpTo(3);
    check(settle(c, 3), "an explicit jump during loading selects its new target");
    player.setQueue({a, b, c}, 2);
    player.previous();
    check(settle(b, 1), "Previous during initial loading updates the correlated target");

    const QVariantMap missing{{"filepath", root + QStringLiteral("/missing.wav")}, {"title", "Missing"}, {"libraryId", -1}};
    const auto corruptPath = root + QStringLiteral("/corrupt.wav");
    { QFile bad(corruptPath); if (!bad.open(QIODevice::WriteOnly)) return 2; bad.write("not a media file"); }
    const QVariantMap corrupt{{"filepath", corruptPath}, {"title", "Corrupt"}, {"libraryId", -1}};
    player.setQueue({a, missing, c}, 1);
    check(until([&] { return player.stopped(); }), "a missing selected file clears loading and stops safely");
    player.setQueue({a, corrupt, c}, 1);
    check(until([&] { return player.stopped(); }), "a selected decode failure clears loading and stops safely");
    player.setQueue({missing, a, b}, 2);
    check(settle(b, 2), "an unavailable first entry does not strand a valid selected song");
    player.setQueue({a, b}, 1);
    check(settle(b, 1), "a valid replacement plays after load failure");

    player.clearQueue();
    player.playNext({a});
    check(until([&] { return nativeString("path") == a.value("filepath").toString(); })
          && player.queue().size() == 1 && player.queueIndex() == 0 && player.playing(),
          "Play Next starts an empty queue without a Zune");
    player.clearQueue();
    printf("Playback queue integration: %s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
