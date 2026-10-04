#include "MpvVideoItem.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include "MpvController.h"
#include "VideoPlayerService.h"

// get_proc_address thunk: resolved through the CURRENT GL context —
// only callable on the render thread inside createFramebufferObject/
// render, which is exactly where mpv calls it.
static void *mpvGetProcAddress(void *, const char *name) {
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    return glctx ? reinterpret_cast<void *>(glctx->getProcAddress(name))
                 : nullptr;
}

class MpvVideoRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvVideoRenderer(const MpvVideoItem *item) : m_item(item) {}

    ~MpvVideoRenderer() override {
        // Render thread, GL context current — the required place to
        // free the render context (docs: must be freed on the thread
        // that created it, before mpv_destroy on the handle).
        if (m_item->m_renderCtx) {
            mpv_render_context_free(m_item->m_renderCtx);
            m_item->m_renderCtx = nullptr;
        }
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        mpv_handle *mpv = m_item->m_service && m_item->m_service->mpv()
                              ? m_item->m_service->mpv()->handle()
                              : nullptr;
        if (!m_item->m_renderCtx && mpv) {
            mpv_opengl_init_params glInit{mpvGetProcAddress, nullptr};
            mpv_render_param params[]{
                {MPV_RENDER_PARAM_API_TYPE,
                 const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            mpv_render_context *ctx = nullptr;
            const int err = mpv_render_context_create(&ctx, mpv, params);
            if (err < 0) {
                fprintf(stderr, "[mpv-video] render_context_create failed: %s\n",
                        mpv_error_string(err));
            } else {
                m_item->m_renderCtx = ctx;
                mpv_render_context_set_update_callback(
                    ctx, &MpvVideoItem::onMpvRedraw,
                    const_cast<MpvVideoItem *>(m_item));
                fprintf(stderr, "[mpv-video] render context ready (OpenGL)\n");
            }
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    }

    void render() override {
        if (!m_item->m_renderCtx)
            return;
        QOpenGLFramebufferObject *fbo = framebufferObject();
        mpv_opengl_fbo mpfbo{int(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flipY = 0;  // Quick's FBO is already y-down for item content
        mpv_render_param params[]{
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_item->m_renderCtx, params);
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
    const MpvVideoItem *m_item;
};

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent) {
    // No setMirrorVertically: with FLIP_Y=0 in render() the FBO
    // orientation already matches Quick's expectation — adding the
    // mirror double-flips and the video plays upside down (seen live).
}

MpvVideoItem::~MpvVideoItem() {
    // Renderer (and with it the render context) is destroyed by Quick
    // on the render thread; nothing to free here. Detach the redraw
    // callback if a context is still alive to avoid a callback into a
    // dead item during teardown races.
    if (m_renderCtx)
        mpv_render_context_set_update_callback(m_renderCtx, nullptr, nullptr);
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const {
    return new MpvVideoRenderer(this);
}

QObject *MpvVideoItem::player() const {
    return reinterpret_cast<QObject *>(m_service);
}

void MpvVideoItem::setPlayer(QObject *player) {
    auto *svc = qobject_cast<VideoPlayerService *>(player);
    if (svc == m_service)
        return;
    m_service = svc;
    emit playerChanged();
    update();
}

void MpvVideoItem::onMpvRedraw(void *ctx) {
    // mpv thread → queue a frame on the item (update() is thread-safe
    // to invoke queued; it schedules Quick rendering).
    QMetaObject::invokeMethod(static_cast<MpvVideoItem *>(ctx),
                              &QQuickFramebufferObject::update,
                              Qt::QueuedConnection);
}
