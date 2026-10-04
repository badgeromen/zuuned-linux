#include <QtQuickTest/quicktest.h>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QCryptographicHash>
#include "artwork/PrintRenderer.h"

class Fixture : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl first READ first CONSTANT)
    Q_PROPERTY(QUrl second READ second CONSTANT)
    Q_PROPERTY(QUrl captureSource READ captureSource CONSTANT)
public:
    Fixture() {
        for (int n = 0; n < 2; ++n) {
            QImage image(320, 400, QImage::Format_ARGB32);
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    image.setPixel(x, y, qRgba((x * 2 + n * 99) % 256,
                        (y + n * 77) % 256, (x + y / 2) % 256, x < 12 ? 100 : 255));
            image.save(path(n));
            m_originals.append(bytes(path(n)));
        }
    }
    QUrl first() const { return QUrl::fromLocalFile(path(0)); }
    QUrl second() const { return QUrl::fromLocalFile(path(1)); }
    QUrl captureSource() const {
        const auto file = qEnvironmentVariable("ARTWORK_CAPTURE_SOURCE");
        return file.isEmpty() ? first() : QUrl::fromLocalFile(file);
    }
    Q_INVOKABLE bool originalsUnchanged() const {
        return bytes(path(0)) == m_originals[0] && bytes(path(1)) == m_originals[1];
    }
    Q_INVOKABLE QUrl expected(int style, int detail, int texture, int dots, bool mono) {
        const auto result = ArtworkPrint::render(QImage(path(0)),
            {ArtworkPrint::Style(style), detail / 100.0, texture / 100.0, dots / 100.0, mono});
        const auto output = m_dir.filePath(QString("expected-%1.png").arg(m_count++));
        result.image.save(output);
        return QUrl::fromLocalFile(output);
    }
private:
    QString path(int index) const { return m_dir.filePath(QString("source-%1.png").arg(index)); }
    static QByteArray bytes(const QString &path) {
        QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll();
    }
    QTemporaryDir m_dir;
    QList<QByteArray> m_originals;
    int m_count = 0;
};

class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        auto *fixture = new Fixture;
        fixture->setParent(engine);
        engine->rootContext()->setContextProperty("artworkFixture", fixture);
    }
};
QUICK_TEST_MAIN_WITH_SETUP(artwork_display, Setup)
#include "main.moc"
