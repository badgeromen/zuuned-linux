#pragma once

#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class PreviewController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PrintPreview)
    QML_SINGLETON
    Q_PROPERTY(QVariantList samples READ samples NOTIFY changed)
    Q_PROPERTY(int index READ index WRITE setIndex NOTIFY changed)
    Q_PROPERTY(int detail READ detail WRITE setDetail NOTIFY changed)
    Q_PROPERTY(int texture READ texture WRITE setTexture NOTIFY changed)
    Q_PROPERTY(int dotSize READ dotSize WRITE setDotSize NOTIFY changed)
    Q_PROPERTY(bool monochrome READ monochrome WRITE setMonochrome NOTIFY changed)
    Q_PROPERTY(QVariantList variants READ variants NOTIFY changed)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit PreviewController(QObject *parent = nullptr);
    static void configure(QString cacheRoot, QString libraryPath, QString output,
                          int detail = 55, int texture = 35, QString sample = {},
                          int dotSize = 35, bool monochrome = false);
    static int exportSamples();
    QVariantList samples() const { return m_samples; }
    int index() const { return m_index; }
    int detail() const { return m_detail; }
    int texture() const { return m_texture; }
    int dotSize() const { return m_dotSize; }
    bool monochrome() const { return m_monochrome; }
    QVariantList variants() const { return m_variants; }
    QVariantList palette() const { return m_palette; }
    bool busy() const { return m_running || m_timer.isActive(); }
    QString error() const { return m_error; }
    void setIndex(int value);
    void setDetail(int value);
    void setTexture(int value);
    void setDotSize(int value);
    void setMonochrome(bool value);
    Q_INVOKABLE void addImage(const QUrl &url);
signals:
    void changed();
private:
    void schedule();
    void start();
    QVariantList m_samples, m_variants, m_palette;
    int m_index = 0, m_detail = 55, m_texture = 35, m_dotSize = 35;
    quint64 m_generation = 0;
    bool m_running = false;
    bool m_monochrome = false;
    QString m_error;
    QTimer m_timer;
};
