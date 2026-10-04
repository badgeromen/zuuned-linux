#include "ArtworkRequest.h"
#include <algorithm>

ArtworkRequest::ArtworkRequest(QObject *parent)
    : ArtworkRequest(*ArtworkPrint::Pipeline::shared(), parent) {}

ArtworkRequest::ArtworkRequest(ArtworkPrint::Pipeline &pipeline, QObject *parent)
    : QObject(parent), m_pipeline(&pipeline)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ArtworkRequest::start);
    connect(&pipeline, &QObject::destroyed, this, [this] {
        m_timer.stop();
        m_subscription = 0;
        ++m_generation;
        m_displayed = {};
        m_pendingDisplay = {};
        publish({});
        setBusy(false);
    });
}

ArtworkRequest::~ArtworkRequest()
{
    m_output = {};
    m_displayed = {};
    m_pendingDisplay = {};
    m_sourceLease.reset();
    if (m_pipeline) {
        m_pipeline->cancel(m_subscription);
        m_pipeline->releaseUnused();
    }
}

ArtworkPrint::Options ArtworkRequest::options() const
{
    ArtworkPrint::Style selected = ArtworkPrint::Style::Original;
    if (m_style == "cleanInk") selected = ArtworkPrint::Style::CleanInk;
    else if (m_style == "halftone") selected = ArtworkPrint::Style::Halftone;
    else if (m_style == "wornPrint") selected = ArtworkPrint::Style::WornPrint;
    return {selected, m_detail / 100.0, m_texture / 100.0,
            m_dotSize / 100.0, m_monochrome};
}

void ArtworkRequest::setSource(const QUrl &value)
{
    if (value == m_source) return;
    m_source = value;
    m_sourceLease = m_pipeline ? m_pipeline->protectSource(value) : nullptr;
    invalidate(false, false);
    emit sourceChanged();
}

void ArtworkRequest::setStyle(const QString &value)
{
    QString next = value.trimmed().toLower();
    if (next == "clean" || next == "cleanink") next = "cleanInk";
    else if (next == "worn" || next == "wornprint") next = "wornPrint";
    else if (next != "halftone") next = "original";
    if (next == m_style) return;
    m_style = next;
    invalidate(false, next != "original");
    emit styleChanged();
}

void ArtworkRequest::setDetail(int value)
{
    value = std::clamp(value, 0, 100);
    if (m_detail == value) return;
    m_detail = value;
    invalidate();
    emit detailChanged();
}

void ArtworkRequest::setTexture(int value)
{
    value = std::clamp(value, 0, 100);
    if (m_texture == value) return;
    m_texture = value;
    if (m_style != "cleanInk") invalidate();
    emit textureChanged();
}

void ArtworkRequest::setDotSize(int value)
{
    value = std::clamp(value, 0, 100);
    if (m_dotSize == value) return;
    m_dotSize = value;
    if (m_style == "halftone") invalidate();
    emit dotSizeChanged();
}

void ArtworkRequest::setMonochrome(bool value)
{
    if (m_monochrome == value) return;
    m_monochrome = value;
    if (m_style == "halftone") invalidate();
    emit monochromeChanged();
}

void ArtworkRequest::refresh() { invalidate(true); }

void ArtworkRequest::displayed(const QUrl &url)
{
    if (url.isEmpty()) {
        m_displayed = {};
        m_pendingDisplay = {};
        if (m_pipeline) m_pipeline->releaseUnused();
        return;
    }
    if (url == m_output.url) m_displayed = m_output;
    else if (url == m_pendingDisplay.url) m_displayed = m_pendingDisplay;
    if (m_pipeline) m_pipeline->releaseUnused();
    // Unknown/stale image acknowledgements cannot acquire a new lease.
}

void ArtworkRequest::invalidate(bool refresh, bool keepDisplay)
{
    ++m_generation;
    if (m_pipeline) m_pipeline->cancel(m_subscription);
    m_subscription = 0;
    m_refresh = m_refresh || refresh;
    m_timer.stop();
    if (keepDisplay && !m_output.url.isEmpty()) m_pendingDisplay = m_output;
    if (!keepDisplay) {
        m_displayed = {};
        m_pendingDisplay = {};
    }
    publish({});
    const bool eligible = m_pipeline && options().style != ArtworkPrint::Style::Original
        && !ArtworkPrint::Pipeline::localPath(m_source).isEmpty();
    setBusy(eligible);
    if (eligible) m_timer.start(12); // Fold QML binding/slider bursts together.
}

void ArtworkRequest::start()
{
    if (!m_pipeline || !m_busy) return;
    const auto generation = m_generation;
    m_subscription = m_pipeline->submit(m_source, options(), this,
        [this, generation](ArtworkPrint::Pipeline::Output output) {
            if (generation != m_generation) return;
            m_subscription = 0;
            publish(std::move(output));
            setBusy(false);
        }, m_refresh);
    if (m_subscription) m_refresh = false;
    else m_timer.start(160); // Bounded admission; visible overflow eventually retries.
}

void ArtworkRequest::setBusy(bool value)
{
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}

void ArtworkRequest::publish(ArtworkPrint::Pipeline::Output output)
{
    const bool newResult = m_output.url != output.url;
    const bool newError = m_output.error != output.error;
    if (!output.url.isEmpty()) m_pendingDisplay = output;
    if (!output.error.isEmpty()) {
        m_displayed = {};
        m_pendingDisplay = {};
    }
    m_output = std::move(output);
    if (m_pipeline) m_pipeline->releaseUnused();
    if (newResult) emit resultChanged();
    if (newError) emit errorChanged();
}
