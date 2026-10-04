#pragma once

#include <QQuickAsyncImageProvider>

// Async provider for GrungeGlass alpha masks — the per-pixel spray
// stencil (GrungeMaskGenerator port) generated in native code on a pool
// thread. The QML Canvas version ran the same loop in the V4 JS
// interpreter on the UI thread: seconds per panel, frozen app on every
// resize. Here it's a few milliseconds and the UI thread never blocks.
//
// URL: image://grungemask/<w>x<h>?g=0|1&r=<roughness>&s=<splatter>
//      &cr=<cornerRadius>&seed=<seed>
class GrungeMaskProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(
        const QString &id, const QSize &requestedSize) override;
};
