#pragma once

#include "ArtworkPipeline.h"
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// One cheap subscription per artwork view; the pipeline shares actual work.
class ArtworkRequest : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(int detail READ detail WRITE setDetail NOTIFY detailChanged)
    Q_PROPERTY(int texture READ texture WRITE setTexture NOTIFY textureChanged)
    Q_PROPERTY(int dotSize READ dotSize WRITE setDotSize NOTIFY dotSizeChanged)
    Q_PROPERTY(bool monochrome READ monochrome WRITE setMonochrome NOTIFY monochromeChanged)
    Q_PROPERTY(QUrl result READ result NOTIFY resultChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit ArtworkRequest(QObject *parent = nullptr);
    explicit ArtworkRequest(ArtworkPrint::Pipeline &pipeline, QObject *parent = nullptr);
    ~ArtworkRequest() override;

    QUrl source() const { return m_source; }
    QString style() const { return m_style; }
    int detail() const { return m_detail; }
    int texture() const { return m_texture; }
    int dotSize() const { return m_dotSize; }
    bool monochrome() const { return m_monochrome; }
    QUrl result() const { return m_output.url; }
    bool busy() const { return m_busy; }
    QString error() const { return m_output.error; }
    void setSource(const QUrl &value);
    void setStyle(const QString &value);
    void setDetail(int value);
    void setTexture(int value);
    void setDotSize(int value);
    void setMonochrome(bool value);
    Q_INVOKABLE void refresh();
    // The view acknowledges the PNG only after its Image reaches Ready. This
    // keeps the previous visible image leased during asynchronous replacement.
    Q_INVOKABLE void displayed(const QUrl &url);

signals:
    void sourceChanged();
    void styleChanged();
    void detailChanged();
    void textureChanged();
    void dotSizeChanged();
    void monochromeChanged();
    void resultChanged();
    void busyChanged();
    void errorChanged();

private:
    void invalidate(bool refresh = false, bool keepDisplay = true);
    void start();
    void setBusy(bool value);
    void publish(ArtworkPrint::Pipeline::Output output);
    ArtworkPrint::Options options() const;
    QPointer<ArtworkPrint::Pipeline> m_pipeline;
    QTimer m_timer;
    QUrl m_source;
    QString m_style = QStringLiteral("original");
    int m_detail = 100;
    int m_texture = 14;
    int m_dotSize = 17;
    bool m_monochrome = false;
    bool m_busy = false;
    bool m_refresh = false;
    quint64 m_generation = 0;
    quint64 m_subscription = 0;
    ArtworkPrint::Pipeline::Output m_output;
    ArtworkPrint::Pipeline::Output m_displayed;
    ArtworkPrint::Pipeline::Output m_pendingDisplay;
    std::shared_ptr<void> m_sourceLease;
};
