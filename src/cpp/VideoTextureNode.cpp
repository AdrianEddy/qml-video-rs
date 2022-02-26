#include "VideoTextureNode.h"

#include "mdk/Player.h"
#include "mdk/RenderAPI.h"
using namespace mdk;

QSGTexture *VideoTextureNodePriv::createTexture(mdk::Player *player, const QSize &size) {
    auto sgrc = QQuickItemPrivate::get(m_item)->sceneGraphRenderContext();
    auto rhi = sgrc->rhi();

    m_texture = rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    m_texture2 = rhi->newTexture(QRhiTexture::RGBA8, QSize(16, 16), 1, QRhiTexture::UsedAsTransferSource);
    if (!m_texture->create()) {
        releaseResources();
        return nullptr;
    }
    if (!m_texture2->create()) {
        releaseResources();
        return nullptr;
    }
    m_proj = rhi->clipSpaceCorrMatrix();

    QRhiColorAttachment color0(m_texture);
    m_rt = rhi->newTextureRenderTarget({color0});
    if (!m_rt) {
        releaseResources();
        return nullptr;
    }

    m_rtRp = m_rt->newCompatibleRenderPassDescriptor();
    if (!m_rtRp) {
        releaseResources();
        return nullptr;
    }

    m_rt->setRenderPassDescriptor(m_rtRp);
    if (!m_rt->create()) {
        releaseResources();
        return nullptr;
    }

    QSGRendererInterface *rif = m_window->rendererInterface();
    switch (rif->graphicsApi()) {
        case QSGRendererInterface::OpenGLRhi: {
            qDebug2("VideoTextureNodePriv::createTexture") << "QSGRendererInterface::OpenGL";
#if QT_CONFIG(opengl)
            m_tx = QSGImageNode::TextureCoordinatesTransformFlag::MirrorVertically;
            auto glrt = static_cast<QGles2TextureRenderTarget*>(m_rt);
            GLRenderAPI ra;
            ra.fbo = glrt->framebuffer;
            player->setRenderAPI(&ra);
            auto tex = GLuint(m_texture->nativeTexture().object);
            if (tex)
                return QNativeInterface::QSGOpenGLTexture::fromNative(tex, m_window, size);
#endif // if QT_CONFIG(opengl)
        } break;
        case QSGRendererInterface::MetalRhi: {
            qDebug2("VideoTextureNodePriv::createTexture") << "QSGRendererInterface::Metal";
#if (__APPLE__+0)
            auto dev = rif->getResource(m_window, QSGRendererInterface::DeviceResource);
            Q_ASSERT(dev);

            MetalRenderAPI ra{};
            ra.texture = reinterpret_cast<const void*>(quintptr(m_texture->nativeTexture().object)); // 5.15+
            ra.device = dev;
            ra.cmdQueue = rif->getResource(m_window, QSGRendererInterface::CommandQueueResource);
            player->setRenderAPI(&ra);
            if (ra.texture)
                return QNativeInterface::QSGMetalTexture::fromNative((__bridge id<MTLTexture>)ra.texture, m_window, size);
#endif // (__APPLE__+0)
        } break;
        case QSGRendererInterface::Direct3D11Rhi: {
            qDebug2("VideoTextureNodePriv::createTexture") << "QSGRendererInterface::Direct3D11";
#if (_WIN32+0)
            D3D11RenderAPI ra;
            ra.rtv = reinterpret_cast<ID3D11DeviceChild*>(quintptr(m_texture->nativeTexture().object));
            player->setRenderAPI(&ra);
            if (ra.rtv)
                return QNativeInterface::QSGD3D11Texture::fromNative(ra.rtv, m_window, size);
#endif // (_WIN32)
        } break;
        case QSGRendererInterface::VulkanRhi: {
            qDebug2("VideoTextureNodePriv::createTexture") << "QSGRendererInterface::Vulkan";
#if (VK_VERSION_1_0+0) && QT_CONFIG(vulkan)
            VulkanRenderAPI ra{};
            ra.device = *static_cast<VkDevice *>(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
            ra.phy_device = *static_cast<VkPhysicalDevice *>(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
            ra.opaque = this;
            ra.rt = VkImage(m_texture->nativeTexture().object);
            ra.renderTargetInfo = [](void* opaque, int* w, int* h, VkFormat* fmt, VkImageLayout* layout) -> int {
                auto node = static_cast<VideoTextureNodePriv*>(opaque);
                *w = node->m_size.width();
                *h = node->m_size.height();
                *fmt = VK_FORMAT_R8G8B8A8_UNORM;
                *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                return 1;
            };
            ra.currentCommandBuffer = [](void* opaque) -> VkCommandBuffer {
                auto node = static_cast<VideoTextureNodePriv*>(opaque);
                QSGRendererInterface *rif = node->m_window->rendererInterface();
                auto cmdBuf = *static_cast<VkCommandBuffer *>(rif->getResource(node->m_window, QSGRendererInterface::CommandListResource));
                return cmdBuf;
            };
            player->setRenderAPI(&ra);
            if (ra.rt)
                return QNativeInterface::QSGVulkanTexture::fromNative(ra.rt, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_window, size);
#else
            qDebug2("VideoTextureNodePriv::createTexture") << "Vulkan support not compiled";
#endif // (VK_VERSION_1_0+0) && QT_CONFIG(vulkan)
        } break;
        default: break;
    }
    return nullptr;
}

// Read texture to QImage. This copies data from GPU to CPU
QImage VideoTextureNodePriv::toImage(bool normalized) {
    if (!m_item || !m_texture || !m_item->window()) return QImage();
    auto context = static_cast<QSGDefaultRenderContext *>(QQuickItemPrivate::get(m_item)->sceneGraphRenderContext());
    auto rhi = context->rhi();

    QRhiCommandBuffer *cb = context->currentFrameCommandBuffer();
    QRhiResourceUpdateBatch *resourceUpdates = rhi->nextResourceUpdateBatch();
    if (!m_readbackResult) m_readbackResult = new QRhiReadbackResult();
    resourceUpdates->readBackTexture({ m_texture }, m_readbackResult);

    cb->resourceUpdate(resourceUpdates);

    // We need the results right away.
    rhi->finish();

    if (m_readbackResult->data.isEmpty()) {
        qWarning("Layer grab failed");
        return QImage();
    }

    // There is no room for negotiation here, the texture is RGBA8, and the readback happens with GL_RGBA on GL, so RGBA8888 is the only option.
    // Also, Quick is always premultiplied alpha.
    const QImage::Format imageFormat = QImage::Format_RGBA8888_Premultiplied;

    const uchar *p = reinterpret_cast<const uchar *>(m_readbackResult->data.constData());
    QImage ret(p, m_readbackResult->pixelSize.width(), m_readbackResult->pixelSize.height(), imageFormat);

    if (normalized && rhi->isYUpInFramebuffer())
        ret.mirror();
    
    return ret;
}

// Upload QImage to texture. This copies data from CPU to GPU
bool VideoTextureNodePriv::fromImage(const QImage &img, bool normalized) {
    if (!m_item || !m_texture || !m_item->window()) return false;
    auto context = static_cast<QSGDefaultRenderContext *>(QQuickItemPrivate::get(m_item)->sceneGraphRenderContext());
    auto rhi = context->rhi();

    if (normalized && rhi->isYUpInFramebuffer())
        const_cast<QImage&>(img).mirror();

    QRhiCommandBuffer *cb = context->currentFrameCommandBuffer();
    QRhiResourceUpdateBatch *resourceUpdates = rhi->nextResourceUpdateBatch();
    resourceUpdates->uploadTexture(m_texture, img.scaled(m_texture->pixelSize())); // TODO: use second texture instead of .scaled()

    cb->resourceUpdate(resourceUpdates);

    rhi->finish();

    return true;
}

void VideoTextureNodePriv::releaseResources() {
    if (m_rt) {
        m_rt->destroy();
        delete m_rt;
        m_rt = nullptr;
    }
    if (m_rtRp) {
        m_rtRp->destroy();
        delete m_rtRp;
        m_rtRp = nullptr;
    }
    if (m_texture) {
        m_texture->destroy();
        delete m_texture;
        m_texture = nullptr;
    }
    if (m_texture2) {
        m_texture2->destroy();
        delete m_texture2;
        m_texture2 = nullptr;
    }
    delete m_readbackResult;
    m_readbackResult = nullptr;
}
