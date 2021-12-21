#ifndef VIDEO_TEXTURE_NODE_H
#define VIDEO_TEXTURE_NODE_H

#include <QQuickWindow>
#include <QSGImageNode>
#include <private/qquickitem_p.h>
#include <private/qrhi_p.h>
#include <private/qrhigles2_p_p.h>
#include <private/qsgrenderer_p.h>
#include <private/qsgdefaultrendercontext_p.h>

#if (_WIN32+0)
#   include <d3d11.h>
#endif
#if (__APPLE__+0)
#   include <private/qsgtexture_p.h>
#   include <Metal/Metal.h>
#endif
#if __has_include(<vulkan/vulkan_core.h>)
#   include <vulkan/vulkan_core.h>
#endif

namespace mdk { class Player; }

class VideoTextureNodePriv {
public:
    QSGTexture *createTexture(mdk::Player *player, const QSize &size);

    // Read texture to QImage. This copies data from GPU to CPU
    QImage toImage(bool normalized = false);

    // Upload QImage to texture. This copies data from CPU to GPU
    bool fromImage(const QImage &img, bool normalized = false);

    void releaseResources();

    QRhiReadbackResult *m_readbackResult{nullptr};

    QRhiTexture *m_texture{nullptr};
    QRhiTextureRenderTarget *m_rt{nullptr};
    QRhiRenderPassDescriptor *m_rtRp{nullptr};
    
    QSGImageNode::TextureCoordinatesTransformMode m_tx{QSGImageNode::TextureCoordinatesTransformFlag::NoTransform};
    QQuickWindow *m_window{nullptr};
    QQuickItem *m_item{nullptr};
    
    QMatrix4x4 m_proj;
    QSize m_size;
};

#endif
