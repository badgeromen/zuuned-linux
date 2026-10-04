#include "PreviewController.h"
#include "artwork/PrintRenderer.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cstdio>

namespace {
QString sourceCache, libraryPath, outputRoot;
QString initialSample;
int initialDetail = 55, initialTexture = 35, initialDotSize = 35;
bool initialMonochrome = false;
constexpr int renderSize = 800;
// Bump whenever filter output changes; preview files are derived, disposable art.
constexpr auto rendererRevision = "print-preview-10";
struct Batch { QVariantList variants, palette; QString error; };

QString hash(const QByteArray &bytes, QCryptographicHash::Algorithm algo = QCryptographicHash::Sha256) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, algo).toHex());
}
QString legacyKey(const QString &text) { return hash(text.toLower().toUtf8(), QCryptographicHash::Md5); }

QVariantList discover() {
    QVariantList samples;
    QSet<QString> seen;
    auto add = [&](const QString &name, const QString &kind, const QString &path) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || !QFileInfo(canonical).isFile() || seen.contains(canonical)) return;
        seen.insert(canonical);
        samples.append(QVariantMap{{"name", name}, {"kind", kind}, {"source", canonical}, {"sourceUrl", QUrl::fromLocalFile(canonical)}});
    };
    // Familiar examples first, then actual cached entries from the local library.
    for (const auto &name : {"Billie Eilish", "Christopher Larkin", "Adele"})
        add(QString::fromLatin1(name), "artist", sourceCache + "/artistart/" + legacyKey(name) + ".jpg");
    add("Skyfall", "album", sourceCache + "/art/" + legacyKey("Adele\nSkyfall") + ".jpg");
    if (QFileInfo::exists(libraryPath)) {
        const QString connection = "print-preview-read-only";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            db.setDatabaseName(libraryPath);
            if (db.open()) {
                QSqlQuery query(db);
                query.exec("SELECT tmdb_title,tmdb_poster FROM videos WHERE tmdb_title IN ('Alien','1917') ORDER BY tmdb_title");
                while (query.next()) add(query.value(0).toString(), "movie", query.value(1).toString());
                if (samples.size() < 6) {
                    query.exec("SELECT DISTINCT COALESCE(NULLIF(albumartist,''),artist),album FROM tracks ORDER BY album LIMIT 200");
                    while (query.next() && samples.size() < 8) {
                        const QString artist = query.value(0).toString(), album = query.value(1).toString();
                        add(album + " / " + artist, "album", sourceCache + "/art/" + legacyKey(artist + '\n' + album) + ".jpg");
                    }
                }
            }
        }
        QSqlDatabase::removeDatabase(connection);
    }
    // A portable fallback when no matching library DB is available.
    if (samples.isEmpty()) for (const QString &folder : {"artistart", "art", "vidthumbs"}) {
        const QDir dir(sourceCache + '/' + folder);
        for (const QString &name : dir.entryList({"*.jpg", "*.png"}, QDir::Files, QDir::Name)) {
            add("Cached " + folder + " " + QString::number(samples.size() + 1),
                folder == "artistart" ? "artist" : folder == "art" ? "album" : "movie", dir.filePath(name));
            if (samples.size() >= 6) return samples;
        }
    }
    return samples;
}

bool saveImage(const QImage &image, const QString &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QImageWriter writer(&file, "png");
    return writer.write(image) && file.commit();
}

Batch prepare(const QString &source, int detail, int texture, int dotSize, bool monochrome) {
    Batch batch;
    QFile file(source);
    if (file.size() > 32 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
        batch.error = "Choose a readable image smaller than 32 MB."; return batch;
    }
    const QByteArray bytes = file.read(32 * 1024 * 1024 + 1);
    if (bytes.size() > 32 * 1024 * 1024) { batch.error = "Choose an image smaller than 32 MB."; return batch; }
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64000000) {
        batch.error = "This image is too large or could not be decoded."; return batch;
    }
    if (size.width() > renderSize || size.height() > renderSize)
        reader.setScaledSize(size.scaled(renderSize, renderSize, Qt::KeepAspectRatio));
    const QImage original = reader.read();
    if (original.isNull()) { batch.error = "Could not decode this image."; return batch; }
    const QString cache = outputRoot + "/variants";
    if (!QDir().mkpath(cache)) { batch.error = "Could not create the preview cache."; return batch; }
    const QString key = hash(bytes + rendererRevision + QByteArray::number(detail) + ':' + QByteArray::number(texture));
    const QString palettePath = cache + '/' + key + "-palette.json";
    QFile paletteFile(palettePath);
    if (paletteFile.open(QIODevice::ReadOnly)) {
        const auto colors = QJsonDocument::fromJson(paletteFile.read(4096)).array();
        for (const auto &value : colors) if (QColor(value.toString()).isValid()) batch.palette.append(value.toString());
    }
    const std::array<QString, 4> titles{"Original", "Clean ink", "Halftone", "Worn print"};
    const std::array<ArtworkPrint::Style, 4> styles{ArtworkPrint::Style::Original, ArtworkPrint::Style::CleanInk,
        ArtworkPrint::Style::Halftone, ArtworkPrint::Style::WornPrint};
    QStringList keep;
    for (int i = 0; i < 4; ++i) {
        // These controls affect Halftone only. Keep the other three variants
        // and source palette reusable while the user adjusts its print screen.
        const QString variantKey = i == 2
            ? hash(key.toUtf8() + ":dots:" + QByteArray::number(dotSize)
                + ":mono:" + (monochrome ? "1" : "0")) : key;
        const QString path = cache + '/' + variantKey + '-' + QString::number(i) + ".png";
        keep.append(path);
        QElapsedTimer timer; timer.start();
        const bool cached = QFileInfo::exists(path) && QImageReader(path).canRead();
        if (!cached || (i == 1 && batch.palette.isEmpty())) {
            const auto result = ArtworkPrint::render(original,
                {styles[i], detail / 100.0, texture / 100.0, dotSize / 100.0, monochrome});
            if (i == 1) {
                batch.palette.clear();
                for (const auto &color : result.palette) batch.palette.append(color.name());
                QSaveFile paletteOutput(palettePath);
                const QByteArray colors = QJsonDocument(QJsonArray::fromVariantList(batch.palette)).toJson(QJsonDocument::Compact);
                if (paletteOutput.open(QIODevice::WriteOnly) && paletteOutput.write(colors) == colors.size()) paletteOutput.commit();
            }
            if (!cached && (result.image.isNull() || !saveImage(result.image, path))) {
                batch.error = "Could not save the preview image."; return batch;
            }
        }
        batch.variants.append(QVariantMap{{"title", titles[i]}, {"url", QUrl::fromLocalFile(path)},
            {"milliseconds", timer.elapsed()}, {"cached", cached}});
    }
    static const QRegularExpression variantName("^[0-9a-f]{64}-[0-3]\\.png$");
    static const QRegularExpression paletteName("^[0-9a-f]{64}-palette\\.json$");
    QFileInfoList entries;
    for (const auto &entry : QDir(cache).entryInfoList({"*.png"}, QDir::Files, QDir::Time | QDir::Reversed))
        if (variantName.match(entry.fileName()).hasMatch()) entries.append(entry);
    int excess = int(entries.size()) - 160;
    for (const auto &entry : entries) {
        if (excess <= 0) break;
        if (!keep.contains(entry.absoluteFilePath()) && entry.canonicalFilePath() != QFileInfo(source).canonicalFilePath()
            && QFile::remove(entry.absoluteFilePath())) --excess;
    }
    for (const auto &name : QDir(cache).entryList({"*-palette.json"}, QDir::Files)) {
        if (!paletteName.match(name).hasMatch()) continue;
        const QString stem = name.left(name.size() - QStringLiteral("-palette.json").size());
        if (!QFileInfo::exists(cache + '/' + stem + "-1.png")) QFile::remove(cache + '/' + name);
    }
    return batch;
}

bool drawSheet(const QVariantMap &sample, const Batch &batch, const QString &path) {
    // Export-only canvas metrics. The interactive view uses the app's Theme.qml.
    const bool poster = sample.value("kind").toString() == "movie";
    const int width = 1440, margin = 44, gap = 24, tile = (width - 2 * margin - 3 * gap) / 4;
    const int artHeight = poster ? tile * 3 / 2 : tile;
    QImage sheet(width, artHeight + 300, QImage::Format_RGB32); sheet.fill(QColor("#141414"));
    QPainter p(&sheet); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(QColor("#929094")); p.setFont(QFont("sans-serif", 10));
    p.drawText(margin, 30, "ZUUNED / LOCAL PRINT PREVIEW");
    QFont heading("Permanent Marker", 28); p.setFont(heading);
    QLinearGradient gradient(margin, 0, width / 2, 0); gradient.setColorAt(0, QColor("#ff8c00")); gradient.setColorAt(1, QColor("#d4367a"));
    p.setPen(QPen(QBrush(gradient), 1)); p.drawText(margin, 84, "YOUR ART. YOUR INK.");
    p.setFont(QFont("sans-serif", 13)); p.setPen(QColor("#dedbdf"));
    p.drawText(margin, 119, sample.value("name").toString());
    for (int i = 0; i < batch.variants.size(); ++i) {
        const auto variant = batch.variants[i].toMap();
        const int x = margin + i * (tile + gap), y = 174;
        p.setPen(i ? QColor("#d4367a") : QColor("#929094"));
        p.drawText(x, y - 18, variant.value("title").toString().toLower());
        const QImage image(variant.value("url").toUrl().toLocalFile());
        QSize scaled = image.size().scaled(tile, artHeight, Qt::KeepAspectRatio);
        p.drawImage(QRect(x + (tile - scaled.width()) / 2, y + (artHeight - scaled.height()) / 2,
            scaled.width(), scaled.height()), image);
        p.save();
        QRectF thumb(x, y + artHeight + 20, 64, 64);
        p.setClipRect(thumb);
        if (sample.value("kind").toString() == "artist") { QPainterPath clip; clip.addEllipse(thumb); p.setClipPath(clip); }
        QSize small = image.size().scaled(64, 64, Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRectF(thumb.center().x() - small.width() / 2.0, thumb.center().y() - small.height() / 2.0,
            small.width(), small.height()), image);
        p.restore();
        p.setFont(QFont("sans-serif", 9)); p.setPen(QColor("#929094"));
        p.drawText(x + 80, y + artHeight + 48, i == 0 ? "source artwork" : QString::number(variant.value("milliseconds").toLongLong()) + " ms");
        p.setFont(QFont("sans-serif", 13));
    }
    p.setFont(QFont("sans-serif", 9)); p.setPen(QColor("#929094"));
    p.drawText(margin, sheet.height() - 16,
        QString("Actual local filter output / detail %1 / texture %2 / halftone dots %3 / monochrome %4 / originals preserved")
            .arg(initialDetail).arg(initialTexture).arg(initialDotSize)
            .arg(initialMonochrome ? "on" : "off"));
    p.end(); return saveImage(sheet, path);
}
}

void PreviewController::configure(QString cacheRoot, QString db, QString output,
                                  int detail, int texture, QString sample, int dotSize, bool monochrome) {
    sourceCache = std::move(cacheRoot); libraryPath = std::move(db); outputRoot = std::move(output);
    initialDetail = std::clamp(detail, 0, 100); initialTexture = std::clamp(texture, 0, 100); initialSample = std::move(sample);
    initialDotSize = std::clamp(dotSize, 0, 100); initialMonochrome = monochrome;
}

PreviewController::PreviewController(QObject *parent) : QObject(parent), m_samples(discover()),
    m_detail(initialDetail), m_texture(initialTexture), m_dotSize(initialDotSize), m_monochrome(initialMonochrome) {
    m_timer.setSingleShot(true); m_timer.setInterval(160);
    connect(&m_timer, &QTimer::timeout, this, &PreviewController::start);
    if (m_samples.isEmpty()) m_error = "Choose an image from your computer to begin.";
    else {
        for (int i = 0; i < m_samples.size(); ++i)
            if (!initialSample.isEmpty() && m_samples[i].toMap().value("name").toString().compare(initialSample, Qt::CaseInsensitive) == 0) m_index = i;
        schedule();
    }
}
void PreviewController::setIndex(int value) {
    value = std::clamp(value, 0, std::max(0, int(m_samples.size()) - 1));
    if (value == m_index) return;
    m_index = value; m_variants.clear(); m_palette.clear(); schedule();
}
void PreviewController::setDetail(int value) {
    value = std::clamp(value, 0, 100); if (value == m_detail) return; m_detail = value; schedule();
}
void PreviewController::setTexture(int value) {
    value = std::clamp(value, 0, 100); if (value == m_texture) return; m_texture = value; schedule();
}
void PreviewController::setDotSize(int value) {
    value = std::clamp(value, 0, 100); if (value == m_dotSize) return; m_dotSize = value; schedule();
}
void PreviewController::setMonochrome(bool value) {
    if (value == m_monochrome) return;
    m_monochrome = value;
    schedule();
}
void PreviewController::addImage(const QUrl &url) {
    if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) {
        m_error = "Choose an image on your computer."; emit changed(); return;
    }
    const QString path = QFileInfo(url.toLocalFile()).canonicalFilePath();
    for (int i = 0; i < m_samples.size(); ++i) if (m_samples[i].toMap().value("source").toString() == path) {
        m_index = i; m_variants.clear(); m_palette.clear(); schedule(); return;
    }
    m_samples.append(QVariantMap{{"name", QFileInfo(path).completeBaseName()}, {"kind", "album"}, {"source", path}, {"sourceUrl", QUrl::fromLocalFile(path)}});
    m_index = int(m_samples.size()) - 1; m_variants.clear(); m_palette.clear(); schedule();
}
void PreviewController::schedule() {
    ++m_generation;
    if (m_samples.isEmpty()) {
        m_timer.stop(); m_error = "Choose an image from your computer to begin.";
    } else { m_error.clear(); m_timer.start(); }
    emit changed();
}
void PreviewController::start() {
    if (m_running || m_samples.isEmpty()) return;
    m_running = true; const auto generation = m_generation; emit changed();
    auto *watcher = new QFutureWatcher<Batch>(this);
    connect(watcher, &QFutureWatcher<Batch>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); watcher->deleteLater(); m_running = false;
        if (generation != m_generation) { start(); return; }
        m_error = result.error;
        if (m_error.isEmpty()) { m_variants = result.variants; m_palette = result.palette; }
        else { m_variants.clear(); m_palette.clear(); }
        emit changed();
    });
    const QString source = m_samples[m_index].toMap().value("source").toString();
    watcher->setFuture(QtConcurrent::run([source, detail = m_detail, texture = m_texture,
                                         dotSize = m_dotSize, monochrome = m_monochrome] {
        return prepare(source, detail, texture, dotSize, monochrome);
    }));
}
int PreviewController::exportSamples() {
    if (!QDir().mkpath(outputRoot)) return 1;
    QFontDatabase::addApplicationFont(":/qt/qml/ZuunedPrintPreview/fonts/PermanentMarker-Regular.ttf");
    const auto samples = discover();
    if (samples.isEmpty()) { fprintf(stderr, "No cached artwork found. Supply --cache-root and --library-db.\n"); return 1; }
    QJsonArray records; int failures = 0;
    for (int i = 0; i < samples.size(); ++i) {
        const auto sample = samples[i].toMap();
        if (!initialSample.isEmpty() && sample.value("name").toString().compare(initialSample, Qt::CaseInsensitive) != 0) continue;
        const auto batch = prepare(sample.value("source").toString(), initialDetail, initialTexture,
                                   initialDotSize, initialMonochrome);
        const QString sheet = outputRoot + "/" + QString::number(i + 1).rightJustified(2, '0') + "-comparison.png";
        const bool ok = batch.error.isEmpty() && drawSheet(sample, batch, sheet);
        if (!ok) ++failures;
        printf("%s %s: %s\n", ok ? "READY" : "FAILED", qPrintable(sample.value("name").toString()), qPrintable(ok ? sheet : batch.error));
        QVariantMap record = sample; record.insert("variants", batch.variants); record.insert("palette", batch.palette);
        record.insert("detail", initialDetail); record.insert("texture", initialTexture);
        record.insert("dotSize", initialDotSize); record.insert("monochrome", initialMonochrome);
        record.insert("rendererRevision", QString::fromLatin1(rendererRevision));
        record.insert("sheet", sheet); record.insert("error", batch.error); records.append(QJsonObject::fromVariantMap(record));
    }
    if (records.isEmpty()) { fprintf(stderr, "No cached sample matches --sample.\n"); return 1; }
    QSaveFile manifest(outputRoot + "/manifest.json");
    const QByteArray json = QJsonDocument(records).toJson();
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(json) != json.size() || !manifest.commit()) ++failures;
    return failures ? 1 : 0;
}
