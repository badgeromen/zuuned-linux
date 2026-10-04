// Exercise the real asynchronous controller with disposable images only.
#include "PreviewController.h"
#include "artwork/PrintRenderer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <cstdio>
#include <functional>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *label) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    ++checks; if (!ok) ++failures;
}
bool waitUntil(const std::function<bool()> &done, int timeout = 10000) {
    QElapsedTimer elapsed; elapsed.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (done()) return true;
        QThread::msleep(2);
    } while (elapsed.elapsed() < timeout);
    return false;
}
bool idle(PreviewController &controller) { return waitUntil([&] { return !controller.busy(); }); }
QByteArray bytes(const QString &path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray digest(const QString &path) { return QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256); }
void write(const QString &path, const QByteArray &data) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) std::abort();
}
QImage artwork(int accent, QSize size = QSize(96, 80)) {
    QImage image(size, QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            image.setPixel(x, y, qRgba((x * 3 + accent) % 256, (y * 5 + accent * 2) % 256,
                (x + y + accent * 3) % 256, 100 + (x + y) % 156));
    return image;
}
void save(const QString &path, const QImage &image) {
    if (!image.save(path, "PNG")) std::abort();
}
QString variantPath(const PreviewController &controller, int index = 0) {
    return controller.variants().value(index).toMap().value("url").toUrl().toLocalFile();
}
bool samePixels(const QImage &left, const QImage &right) {
    return !left.isNull() && left.size() == right.size()
        && left.convertToFormat(QImage::Format_RGBA8888) == right.convertToFormat(QImage::Format_RGBA8888);
}
int ownedVariants(const QString &directory) {
    int count = 0;
    const QRegularExpression pattern("^[0-9a-f]{64}-[0-3]\\.png$");
    for (const QString &name : QDir(directory).entryList(QDir::Files))
        if (pattern.match(name).hasMatch()) ++count;
    return count;
}
void makeOld(const QString &path, qint64 seconds) {
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)
        || !file.setFileTime(QDateTime::fromSecsSinceEpoch(seconds), QFileDevice::FileModificationTime)) std::abort();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir fixture;
    if (!fixture.isValid()) return 2;
    const QString emptyCache = fixture.filePath("empty-cache");
    const QString output = fixture.filePath("preview-output");
    QDir().mkpath(emptyCache);
    PreviewController::configure(emptyCache, fixture.filePath("absent-library.db"), output);
    PreviewController controller;
    check(controller.samples().isEmpty() && !controller.busy() && !controller.error().isEmpty(),
          "empty preview starts idle with a choose-image message");
    const bool initialHalftoneControls = controller.dotSize() == 35 && !controller.monochrome();
    controller.setDotSize(-10);
    const bool lowDotClamp = controller.dotSize() == 0;
    controller.setDotSize(120); controller.setMonochrome(true);
    check(initialHalftoneControls && lowDotClamp && controller.dotSize() == 100 && controller.monochrome(),
          "halftone controls start with documented defaults and dot size clamps to its range");
    controller.setDetail(10); controller.setTexture(90);
    waitUntil([] { return false; }, 350);
    check(!controller.busy() && !controller.error().isEmpty() && controller.variants().isEmpty(),
          "empty-state sliders keep guidance visible and never strand Printing status");

    const QString first = fixture.filePath("source.png");
    save(first, artwork(20));
    const QByteArray initialDigest = digest(first);
    controller.addImage(QUrl::fromLocalFile(first));
    check(idle(controller) && controller.error().isEmpty() && controller.variants().size() == 4
          && !controller.palette().isEmpty() && samePixels(QImage(variantPath(controller)), QImage(first)),
          "valid local image publishes four real variants, palette and exact decoded original");
    const QString firstVariant = variantPath(controller);
    check(digest(first) == initialDigest && firstVariant.startsWith(output + "/variants/")
          && !QFile::exists(fixture.filePath("absent-library.db")),
          "controller writes derived files only and does not create a library database");

    save(first, artwork(90));
    const QByteArray rewrittenDigest = digest(first);
    controller.addImage(QUrl::fromLocalFile(first));
    check(controller.variants().isEmpty() && controller.palette().isEmpty(),
          "explicit same-source refresh clears the previously displayed revision");
    check(idle(controller) && controller.samples().size() == 1 && controller.error().isEmpty()
          && variantPath(controller) != firstVariant && samePixels(QImage(variantPath(controller)), QImage(first)),
          "reselecting a rewritten source rerenders new bytes without adding a duplicate sample");

    const QString bad = fixture.filePath("repairable.png");
    write(bad, "This is not an image.");
    controller.addImage(QUrl::fromLocalFile(bad));
    check(controller.variants().isEmpty() && controller.palette().isEmpty(),
          "changing to another sample immediately removes old artwork and palette");
    check(idle(controller) && !controller.error().isEmpty() && controller.variants().isEmpty() && controller.palette().isEmpty(),
          "failed decode reports an error without retaining another sample's artwork");
    save(bad, artwork(150));
    const QByteArray repairedDigest = digest(bad);
    controller.addImage(QUrl::fromLocalFile(bad));
    check(idle(controller) && controller.error().isEmpty() && controller.variants().size() == 4
          && controller.samples().size() == 2 && samePixels(QImage(variantPath(controller)), QImage(bad)),
          "reselecting a repaired current file recovers from its earlier decode error");

    const QString second = fixture.filePath("worker-source.png");
    save(second, artwork(220, QSize(320, 240)));
    const QByteArray secondDigest = digest(second);
    controller.addImage(QUrl::fromLocalFile(second));
    const bool active = waitUntil([&] { return QThreadPool::globalInstance()->activeThreadCount() > 0; });
    controller.setDetail(0); controller.setTexture(100); controller.setDetail(41);
    controller.setDotSize(0); controller.setMonochrome(false);
    controller.setIndex(0); controller.setTexture(0); controller.setDetail(73); controller.setTexture(29);
    controller.setDotSize(67); controller.setMonochrome(true);
    bool latest = active && idle(controller) && controller.error().isEmpty() && controller.index() == 0
        && controller.detail() == 73 && controller.texture() == 29 && controller.dotSize() == 67
        && controller.monochrome() && controller.variants().size() == 4;
    const QImage expectedInput(first);
    const ArtworkPrint::Style styles[]{ArtworkPrint::Style::Original, ArtworkPrint::Style::CleanInk,
        ArtworkPrint::Style::Halftone, ArtworkPrint::Style::WornPrint};
    for (int i = 0; i < 4; ++i)
        latest &= samePixels(QImage(variantPath(controller, i)), ArtworkPrint::render(expectedInput, {styles[i], .73, .29, .67, true}).image);
    check(latest, "sample and slider changes during active work publish only the final source and controls");
    const QString latestVariant = variantPath(controller);
    controller.addImage(QUrl::fromLocalFile(first));
    bool cacheHit = idle(controller) && variantPath(controller) == latestVariant && controller.variants().size() == 4;
    for (const auto &variant : controller.variants()) cacheHit &= variant.toMap().value("cached").toBool();
    check(cacheHit, "reselecting unchanged bytes and controls reuses their existing variant cache");

    QStringList baselinePaths;
    for (int i = 0; i < 4; ++i) baselinePaths.append(variantPath(controller, i));
    controller.setDotSize(91);
    bool dotCache = idle(controller) && controller.variants().size() == 4
        && variantPath(controller, 2) != baselinePaths[2]
        && samePixels(QImage(variantPath(controller, 2)),
            ArtworkPrint::render(expectedInput, {styles[2], .73, .29, .91, true}).image);
    for (int i : {0, 1, 3})
        dotCache &= variantPath(controller, i) == baselinePaths[i]
            && controller.variants().value(i).toMap().value("cached").toBool();
    check(dotCache, "dot-size changes create the correct Halftone variant and reuse other styles unchanged");
    controller.setDotSize(67);
    bool returnedDotCache = idle(controller) && variantPath(controller, 2) == baselinePaths[2]
        && controller.variants().value(2).toMap().value("cached").toBool();
    controller.setMonochrome(false);
    bool monoCache = idle(controller) && controller.variants().size() == 4
        && variantPath(controller, 2) != baselinePaths[2]
        && samePixels(QImage(variantPath(controller, 2)),
            ArtworkPrint::render(expectedInput, {styles[2], .73, .29, .67, false}).image);
    for (int i : {0, 1, 3})
        monoCache &= variantPath(controller, i) == baselinePaths[i]
            && controller.variants().value(i).toMap().value("cached").toBool();
    check(monoCache, "monochrome changes create the correct Halftone variant and reuse other styles unchanged");
    controller.setMonochrome(true);
    check(returnedDotCache && idle(controller) && variantPath(controller, 2) == baselinePaths[2]
          && controller.variants().value(2).toMap().value("cached").toBool(),
          "returning both halftone controls to previous settings reuses the exact cached screen");

    const QImage secondInput(second);
    const QImage desiredScreen = ArtworkPrint::render(secondInput, {styles[2], .73, .29, .23, false}).image;
    controller.setIndex(2);
    const bool screenActive = waitUntil([&] { return QThreadPool::globalInstance()->activeThreadCount() > 0; });
    controller.setDotSize(23); controller.setMonochrome(false);
    bool staleScreenPublished = false;
    const auto observation = QObject::connect(&controller, &PreviewController::changed, &controller, [&] {
        if (!controller.variants().isEmpty()
            && !samePixels(QImage(variantPath(controller, 2)), desiredScreen)) staleScreenPublished = true;
    });
    const bool finalScreen = idle(controller) && controller.error().isEmpty() && controller.index() == 2
        && controller.dotSize() == 23 && !controller.monochrome()
        && samePixels(QImage(variantPath(controller, 2)), desiredScreen);
    QObject::disconnect(observation);
    check(screenActive && finalScreen && !staleScreenPublished,
          "halftone-only changes during active rendering never publish the obsolete dot size or color mode");

    const QString variants = output + "/variants";
    const QByteArray smallPng = bytes(first);
    for (int i = 0; i < 180; ++i) {
        const QString path = variants + '/' + QString::number(i, 16).rightJustified(64, '0') + "-0.png";
        write(path, smallPng); makeOld(path, 2000 + i);
    }
    const QString unrelated = variants + "/unrelated.png";
    const QString unrelatedPalette = variants + "/unrelated-palette.json";
    write(unrelated, smallPng); makeOld(unrelated, 1);
    write(unrelatedPalette, "Preserve this unrelated file too.");
    const QString selectedOwned = variants + '/' + QString(64, 'f') + "-3.png";
    save(selectedOwned, artwork(35)); makeOld(selectedOwned, 1);
    const QByteArray selectedDigest = digest(selectedOwned), unrelatedDigest = digest(unrelated);
    const int beforePrune = ownedVariants(variants);
    controller.addImage(QUrl::fromLocalFile(selectedOwned));
    const bool pruned = idle(controller) && controller.error().isEmpty()
        && ownedVariants(variants) < beforePrune && ownedVariants(variants) <= 160;
    check(pruned && digest(unrelated) == unrelatedDigest
          && bytes(unrelatedPalette) == QByteArray("Preserve this unrelated file too."),
          "pruning exceeds the retention boundary without deleting unrelated PNG or palette files");
    check(QFile::exists(selectedOwned) && digest(selectedOwned) == selectedDigest
          && samePixels(QImage(variantPath(controller)), QImage(selectedOwned)),
          "pruning preserves the selected source even when its old filename matches generated variants");
    check(digest(first) == rewrittenDigest && digest(bad) == repairedDigest && digest(second) == secondDigest,
          "rendering, failed loads, repeated controls and pruning leave all original source bytes unchanged");

    if (!QThreadPool::globalInstance()->waitForDone(10000)) return 2;
    printf("Artwork preview controller: %d checks, %d failures; isolated images and output only\n", checks, failures);
    return failures ? 1 : 0;
}
