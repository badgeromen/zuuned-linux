#pragma once

#include <QQuickFramebufferObject>
#include <QtQml/qqmlregistration.h>

struct mpv_handle;
struct mpv_render_context;
class VideoPlayerService;

// Qt Quick surface for the video player — the canonical libmpv
// QQuickFramebufferObject pattern (mpv-examples/libmpv/qml), the Linux
// answer to the mac's CAMetalLayer + wid embedding (no wid on
// Wayland/Hyprland; the render API is the supported path).
//
// Ownership: VideoPlayerService owns the mpv handle (MpvController in
// video mode). This item owns only the mpv_render_context, created
// lazily on the Qt Quick RENDER thread inside the Renderer and freed
// there when the renderer dies. QML binds the service in:
//
//   MpvVideoItem { player: VideoPlayerService }
//
// The render context must exist before the first frame can decode —
// mpv blocks video decoding until a render target exists — so the item
// should be instantiated (even hidden) before/at play start. The view
// keeps it mounted for the overlay's stable-identity rule anyway.
class MpvVideoItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject *player READ player WRITE setPlayer NOTIFY playerChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);
    ~MpvVideoItem() override;

    Renderer *createRenderer() const override;

    QObject *player() const;
    void setPlayer(QObject *player);

signals:
    void playerChanged();

private:
    friend class MpvVideoRenderer;

    // mpv wakeup → schedule a Quick frame (called from an mpv thread).
    static void onMpvRedraw(void *ctx);

    VideoPlayerService *m_service = nullptr;
    // Written by the Renderer on the render thread once created; the
    // item only reads it for the service's frame-swap reporting.
    mutable mpv_render_context *m_renderCtx = nullptr;
};
