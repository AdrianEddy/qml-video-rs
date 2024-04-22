#ifndef VIDEO_TEXTURE_NODE_H
#define VIDEO_TEXTURE_NODE_H

#include <QQuickWindow>
#include <QSGImageNode>
#include <private/qquickitem_p.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
#   include <rhi/qrhi.h>
#   include <private/qrhigles2_p.h>
#else
#   include <private/qrhi_p.h>
#   include <private/qrhigles2_p_p.h>
#endif
#include <private/qsgrenderer_p.h>
#include <private/qsgdefaultrendercontext_p.h>

#if (_WIN32+0)
#   include <d3d11.h>
#   include <d3d12.h>
#   include <d3d11_4.h>
#endif
#if (__APPLE__+0)
#   include <private/qsgtexture_p.h>
#   include <Metal/Metal.h>
#endif
#if __has_include(<vulkan/vulkan_core.h>)
#   include <vulkan/vulkan_core.h>
#if QT_CONFIG(vulkan)
#include <QtGui/private/qrhivulkan_p.h>
#include <QVulkanInstance>
#endif
#endif

#define qDebug2(func) QMessageLogger(__FILE__, __LINE__, func).debug(QLoggingCategory("MDKPlayer"))

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
    QRhiTexture *m_workaroundTexture{nullptr};
    std::unique_ptr<QRhiTextureRenderTarget> m_rt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rtRp;

    QSGImageNode::TextureCoordinatesTransformMode m_tx{QSGImageNode::TextureCoordinatesTransformFlag::NoTransform};
    QQuickWindow *m_window{nullptr};
    QQuickItem *m_item{nullptr};

    QMatrix4x4 m_proj;
    QSize m_size;

#if (_WIN32+0)
    ID3D11Fence *m_fence{nullptr};
    HANDLE m_event{nullptr};
    uint64_t m_fenceValue{0};
#endif
};

#endif
