#include "MDKPlayer.h"
#include <map>
#include <string>
#include <thread>
#include <QTimer>
#include <QGuiApplication>
#if __has_include(<QX11Info>)
#include <QX11Info>
#endif

#include "mdk/Player.h"
#include "mdk/VideoFrame.h"
using namespace mdk;

void printMd(const std::map<std::string, std::string> &md) {
    for (auto &x : md) {
        qDebug2("printMd") << QString::fromStdString(x.first) << " = " << QString::fromStdString(x.second);
    }
}
std::string toStdString(const QString &str) { return std::string(qUtf8Printable(str), str.size()); }

MDKPlayer::MDKPlayer() {
#ifdef QX11INFO_X11_H
    SetGlobalOption("X11Display", QX11Info::display());
    qDebug("X11 display: %p", QX11Info::display());
#elif (QT_FEATURE_xcb + 0 == 1) && (QT_VERSION >= QT_VERSION_CHECK(6, 2, 0))
    const auto x = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (x) {
        const auto xdisp = x->display();
        SetGlobalOption("X11Display", xdisp);
        qDebug("X11 display: %p", xdisp);
    }
#endif
}

void MDKPlayer::initPlayer() {
    m_player = std::make_unique<mdk::Player>();

    QString overrideDecoders = QString(qgetenv("MDK_DECODERS")).trimmed();

    if (!overrideDecoders.isEmpty()) {
        std::vector<std::string> vec;
        for (const auto &x : overrideDecoders.split(",")) {
            vec.push_back(toStdString(x));
        }
        m_player->setDecoders(mdk::MediaType::Video, vec);
    } else {
        m_player->setDecoders(mdk::MediaType::Video, {
    #if (__APPLE__+0)
        "VT",
    #elif (__ANDROID__+0)
        "AMediaCodec:java=0:copy=0:surface=1:async=0:image=0",
    #elif (_WIN32+0)
        // "MFT:d3d=11",
        //"CUDA",
        //"NVDEC",
        //"CUVID",
        "D3D11:sw_fallback=1",
        "DXVA",
    #elif (__linux__+0)
        "CUDA",
        "VDPAU",
        "VAAPI:sw_fallback=1",
    #endif
        "BRAW:gpu=auto:scale=1920x1080",
        "R3D:gpu=auto:scale=1920x1080",
        "FFmpeg"});
    }

    if (m_item && m_node && m_window) {
        setupPlayer();
        if (m_size.width() > 0 && m_size.height() > 0) {
            m_syncNext = true;
        }
    }
}
void MDKPlayer::destroyPlayer() {
    m_videoLoaded = false;
    m_firstFrameLoaded = false;
    if (m_connectionBeforeRendering) QObject::disconnect(m_connectionBeforeRendering);
    if (m_connectionScreenChanged) QObject::disconnect(m_connectionScreenChanged);

    if (m_player) {
        stop();
        m_player->setRenderCallback([](void *) {});
        m_player->onMediaStatusChanged([](mdk::MediaStatus) -> bool { return false; });
        m_player->onStateChanged([](mdk::State) {});
        m_player->onEvent([](const mdk::MediaEvent &) -> bool { return false; });
        m_player->onFrame<mdk::VideoFrame>([](mdk::VideoFrame&, int) -> int { return 0; });
        auto ptr = m_player.release();
        QTimer::singleShot(1000, [ptr] { delete ptr; }); // delete later
    }
}

MDKPlayer::~MDKPlayer() {
    m_videoLoaded = false;
    m_firstFrameLoaded = false;
    m_processPixels = nullptr;
    m_processTexture = nullptr;

    if (m_userDataDestructor && m_userData) { m_userDataDestructor(m_userData); m_userData = nullptr; }

    destroyPlayer();
}

void MDKPlayer::setProperty(const QString &key, const QString &value) {
    if (m_player) {
        m_player->setProperty(toStdString(key), toStdString(value));
    }
}

void MDKPlayer::setUrl(const QUrl &url, const QString &customDecoder) {
    m_overrideFps = 0.0;
    if (!m_item) {
        m_pendingUrl = url;
        m_pendingCustomDecoder = customDecoder;
        return;
    }
    destroyPlayer();
    initPlayer();

    QString additionalUrl;
    if (!customDecoder.isEmpty()) {
        qDebug2("setUrl") << "MDK decoder:" << customDecoder;
        if (customDecoder.startsWith("FFmpeg:avformat_options=")) {
            additionalUrl = "?mdkopt=avformat&" + customDecoder.mid(24);
        } else {
            m_player->setDecoders(mdk::MediaType::Video, { toStdString(customDecoder) });
        }
    }

    QString path = url.toEncoded() + additionalUrl;

    // Workaround because avdevice:// is not a valid URL according to QUrl
    if (path.startsWith("http://avdevice/")) {
        path = "avdevice://" + path.mid(strlen("http://avdevice/")).replace("%20", " ");
    } else {
        if (url.scheme() == "file") {
            path = url.toLocalFile() + additionalUrl;
        } else if (path.contains(' ')) {
            path.replace(' ', "%20");
        }
    }
    qDebug2("setUrl") << "Final url:" << path;
    m_player->setMedia(qUtf8Printable(path));
    m_player->prepare();
}

void MDKPlayer::setBackgroundColor(const QColor &color) {
    m_bgColor = color;
    if (m_player)
        m_player->setBackgroundColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), m_bgColor.alphaF());
    forceRedraw();
}

void MDKPlayer::setMuted(bool v) {
    if (m_player)
        m_player->setMute(v);
}
bool MDKPlayer::getMuted() { return m_player? m_player->isMute() : false; }

void MDKPlayer::setVolume(float v) { if (m_player) m_player->setVolume(v); }
float MDKPlayer::getVolume() { return m_player? m_player->volume() : 0.0; }

void MDKPlayer::setupNode(QSGImageNode *node, QQuickItem *item) {
    m_node = node;
    m_item = item;
    m_window = item->window();
    if (!m_pendingUrl.isEmpty()) {
        setUrl(m_pendingUrl, m_pendingCustomDecoder);
        m_pendingUrl = QUrl();
    }
}

void MDKPlayer::setProcessPixelsCallback(ProcessPixelsCb &&cb) {
    m_processPixels = cb;
}
void MDKPlayer::setProcessTextureCallback(ProcessTextureCb &&cb) {
    m_processTexture = cb;
}
void MDKPlayer::setReadyForProcessingCallback(ReadyForProcessingCb &&cb) {
    m_readyForProcessing = cb;
}

void MDKPlayer::setupPlayer() {
    m_player->setRenderCallback([this](void *) { QMetaObject::invokeMethod(m_item, "update"); });
    m_player->setProperty("continue_at_end", "1");
    m_player->setBufferRange(0);
    m_player->onEvent([this](const mdk::MediaEvent &evt) -> bool {
        if (evt.detail == "1st_frame") {
            auto md = m_player->mediaInfo();

            QJsonObject obj;
            for (const auto &x : getMediaInfo(md)) {
                obj.insert(QString::fromUtf8(x.first.c_str(), x.first.size()), QString::fromUtf8(x.second.c_str(), x.second.size()));
            }
            QMetaObject::invokeMethod(m_item, "metadataLoaded", Qt::QueuedConnection, Q_ARG(QJsonObject, obj));
            m_firstFrameLoaded = true;
        }
        qDebug2("m_player->onEvent") << QString::fromUtf8(evt.category.c_str(), evt.category.size()) << QString::fromUtf8(evt.detail.c_str(), evt.detail.size());
        return true;
    });

    m_player->setBackgroundColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), m_bgColor.alphaF());
    m_player->setPlaybackRate(m_playbackRate);

    m_player->onStateChanged([this](mdk::State state) {
        // qDebug2("m_player->onStateChanged") <<
        //     QString(state == mdk::State::NotRunning?  "NotRunning"  : "") +
        //     QString(state == mdk::State::Running?     "Running"     : "") +
        //     QString(state == mdk::State::Paused?      "Paused"      : "");

        QMetaObject::invokeMethod(m_item, "stateChanged", Q_ARG(int, int(state)));
    });

    m_player->onMediaStatusChanged([this](mdk::MediaStatus status) -> bool {
        if (!m_player) return false;

        // qDebug2("m_player->onMediaStatusChanged") <<
        //     QString(status & mdk::MediaStatus::Unloaded?  "Unloaded | "  : "") +
        //     QString(status & mdk::MediaStatus::Loading?   "Loading | "   : "") +
        //     QString(status & mdk::MediaStatus::Loaded?    "Loaded | "    : "") +
        //     QString(status & mdk::MediaStatus::Prepared?  "Prepared | "  : "") +
        //     QString(status & mdk::MediaStatus::Stalled?   "Stalled | "   : "") +
        //     QString(status & mdk::MediaStatus::Buffering? "Buffering | " : "") +
        //     QString(status & mdk::MediaStatus::Buffered?  "Buffered | "  : "") +
        //     QString(status & mdk::MediaStatus::End?       "End | "       : "") +
        //     QString(status & mdk::MediaStatus::Seeking?   "Seeking | "   : "") +
        //     QString(status & mdk::MediaStatus::Invalid?   "Invalid | "   : "");

        if (!m_videoLoaded && (status & mdk::MediaStatus::Loaded) && (status & mdk::MediaStatus::Prepared)) {
            auto md = m_player->mediaInfo();

            /*QJsonObject obj;
            for (const auto &x : getMediaInfo(md)) {
                obj.insert(QString::fromStdString(x.first), QString::fromStdString(x.second));
            }
            QMetaObject::invokeMethod(m_item, "metadataLoaded", Q_ARG(QJsonObject, obj));*/

            if (!md.video.empty()) {
                auto v = md.video[0];
                if (!v.frames && v.duration > 0 && v.codec.frame_rate > 0) {
                    v.frames = (double(v.duration) / 1000.0) * v.codec.frame_rate;
                }
                m_fps = v.codec.frame_rate;
                double fps = m_fps;
                m_duration = v.duration;
                if (m_overrideFps > 0.0) {
                    m_duration *= m_fps / m_overrideFps;
                    fps = m_overrideFps;
                }

                QMetaObject::invokeMethod(m_item, "videoLoaded", Q_ARG(double, m_duration), Q_ARG(qlonglong, v.frames), Q_ARG(double, fps), Q_ARG(uint, v.codec.width), Q_ARG(uint, v.codec.height));
            }
            m_player->setLoop(9999999);
            m_videoLoaded = true;

            if (!m_connectionBeforeRendering)
                m_connectionBeforeRendering = QObject::connect(m_window, &QQuickWindow::beforeRendering, [this] { this->windowBeforeRendering(); });
            if (!m_connectionScreenChanged)
                m_connectionScreenChanged = QObject::connect(m_window, &QQuickWindow::screenChanged, [this](QScreen *) { m_item->update(); });
        }
        if (status & mdk::MediaStatus::Invalid) {
            QMetaObject::invokeMethod(m_item, "videoLoaded", Q_ARG(double, 0), Q_ARG(qlonglong, 0), Q_ARG(double, 0), Q_ARG(uint, 0), Q_ARG(uint, 0));
            QMetaObject::invokeMethod(m_item, "metadataLoaded", Q_ARG(QJsonObject, QJsonObject()));
        }

        return true;
    });

    /*m_player->onFrame<mdk::VideoFrame>([this](mdk::VideoFrame &frame, int track) -> int {
        //QMetaObject::invokeMethod(m_item, "frameRendered", Q_ARG(double, frame.timestamp() * 1000.0));
        // qDebug2("m_player->onFrame") << "frame" << track << frame.timestamp() << (int)frame.format() << frame.planeCount() << frame.bufferData(0);
        return 0;
    });*/

    if (m_size.width() > 0 && m_size.height() > 0) {
        m_player->setVideoSurfaceSize(m_size.width(), m_size.height());
        m_syncNext = true;
    }
    forceRedraw();
}

void MDKPlayer::windowBeforeRendering() {
    // QElapsedTimer t; t.start();

    if (!m_videoLoaded || !m_player) return;

    if (m_renderedPosition == m_playerPosition && m_renderedReturnCount++ > 100) {
        return;
    }
    if (m_readyForProcessing && !m_readyForProcessing(m_item)) return;

    auto context = static_cast<QSGDefaultRenderContext *>(QQuickItemPrivate::get(m_item)->sceneGraphRenderContext());
    auto cb = context->currentFrameCommandBuffer();

    bool doRenderPass = m_rt && m_window->rendererInterface()->graphicsApi() != QSGRendererInterface::MetalRhi;

    if (doRenderPass) {
        QRhiResourceUpdateBatch *u = context->rhi()->nextResourceUpdateBatch();
        cb->beginPass(m_rt, QColor(Qt::black), { 1.0f, 0 }, u, QRhiCommandBuffer::ExternalContent);
    }

    cb->beginExternal();
    double timestamp = m_player->renderVideo();
    cb->endExternal();

    m_playerPosition = timestamp * 1000;

    if (doRenderPass) {
        cb->endPass();
    }

    double fps = m_fps;
    if (m_overrideFps > 0.0) {
        timestamp *= m_fps / m_overrideFps;
        fps = m_overrideFps;
    }

    int frame = std::ceil(std::round(timestamp * fps * 100.0) / 100.0);

    bool processed = false;
    if (m_firstFrameLoaded.load()) {
        if (!m_videoLoaded || !m_player) return;
        if (m_processTexture) {
            uint64_t backend_id = 0;
            uint64_t ptr1 = m_texture->nativeTexture().object;
            uint64_t ptr2 = 0;
            uint64_t ptr3 = 0;
            uint64_t ptr4 = 0;
            uint64_t ptr5 = 0;

            QSGRendererInterface *rif = m_window->rendererInterface();
            switch (rif->graphicsApi()) {
                case QSGRendererInterface::OpenGLRhi: {
                    backend_id = 1;
                    ptr2 = uint64_t(rif->getResource(m_window, QSGRendererInterface::OpenGLContextResource));
                } break;
                case QSGRendererInterface::MetalRhi: {
                    backend_id = 2;
                    ptr2 = uint64_t(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
                    ptr3 = uint64_t(rif->getResource(m_window, QSGRendererInterface::CommandQueueResource));
                } break;
                case QSGRendererInterface::Direct3D11Rhi: {
                    backend_id = 3;
                    ptr2 = uint64_t(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
                    ptr3 = uint64_t(rif->getResource(m_window, QSGRendererInterface::DeviceContextResource));
                } break;
                case QSGRendererInterface::VulkanRhi: {
                    context->rhi()->finish();
                    backend_id = 4;
                    ptr2 = uint64_t(rif->getResource(m_window, QSGRendererInterface::DeviceResource));
                    ptr3 = uint64_t(rif->getResource(m_window, QSGRendererInterface::CommandListResource));
                    ptr4 = uint64_t(rif->getResource(m_window, QSGRendererInterface::PhysicalDeviceResource));
#if __has_include(<vulkan/vulkan_core.h>)
#  if QT_CONFIG(vulkan)
                    auto inst = reinterpret_cast<QVulkanInstance *>(rif->getResource(m_window, QSGRendererInterface::VulkanInstanceResource));
                    ptr5 = inst? uint64_t(inst->vkInstance()) : 0;
#  endif
#endif
                } break;
                default: break;
            }

#if (_WIN32+0)
    // On DirectX we need to wait until MDK finishes rendering the video before we do any further processing with it
    if (rif->graphicsApi() == QSGRendererInterface::Direct3D11Rhi) {
        auto dev = (ID3D11Device *)       rif->getResource(m_window, QSGRendererInterface::DeviceResource);
        auto ctx = (ID3D11DeviceContext *)rif->getResource(m_window, QSGRendererInterface::DeviceContextResource);

        ID3D11Device5 *dev5 = nullptr;
        ID3D11DeviceContext4 *ctx4 = nullptr;
        if (dev->QueryInterface(IID_ID3D11Device5, (void**)&dev5) == S_OK) {
            if (ctx->QueryInterface(IID_ID3D11DeviceContext4, (void**)&ctx4) == S_OK) {
                if (!m_fence) {
                    if (dev5->CreateFence(m_fenceValue, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void**)&m_fence) == S_OK) {
                        m_event = CreateEventW(NULL, FALSE, FALSE, NULL);
                    }
                }
                if (m_fence && m_event) {
                    ctx4->Signal(m_fence, ++m_fenceValue);
                    m_fence->SetEventOnCompletion(m_fenceValue, m_event);
                    WaitForSingleObject(m_event, 5000);
                }
            }
        }
    }
#endif
            processed = m_processTexture(m_item, frame, timestamp * 1000.0, m_size.width(), m_size.height(), backend_id, ptr1, ptr2, ptr3, ptr4, ptr5);

            // -------------- Readback workaround --------------
            // if (processed && rif->graphicsApi() == QSGRendererInterface::Direct3D11Rhi) {
            //     if (!m_readbackResult) m_readbackResult = new QRhiReadbackResult();
            //     QRhiResourceUpdateBatch *u = context->rhi()->nextResourceUpdateBatch();
            //     u->readBackTexture({ m_workaroundTexture }, m_readbackResult);
            //     cb->resourceUpdate(u);
            // }
            // -------------- Readback workaround --------------

            if (processed) m_renderFailCounter = 0;
            else           m_renderFailCounter++;
        }

        if (!processed && m_processPixels) {
            if (!m_processTexture || m_renderFailCounter > 10) {
                auto img = toImage();
                if (!m_videoLoaded || !m_player) return;

                const auto img2 = m_processPixels(m_item, frame, timestamp * 1000.0, img);
                if (!img2.isNull() && img2.constBits()) {
                    fromImage(img2);
                }
            }
        }
    }

    if (m_renderedPosition != m_playerPosition)
        m_renderedReturnCount = 0;

    m_renderedPosition = m_playerPosition;
    // printf("render: %.3f\n", double(t.nsecsElapsed()) / 1000000.0);

    QMetaObject::invokeMethod(m_item, "frameRendered", Q_ARG(double, timestamp * 1000.0));
}

void MDKPlayer::sync(QSize newSize, bool force) {
    if (m_syncNext) {
        force = true;
        m_syncNext = false;
    }
    if (!m_player) { m_size = newSize; return; }
    if (!force && m_node->texture() && newSize == m_size)
        return;

    if (newSize.width() < 32 || newSize.height() < 32)
        newSize = QSize(32, 32);

    m_size = newSize;

    releaseResources();
    auto tex = createTexture(m_player.get(), m_size);
    if (!tex)
        return;
    qDebug2("MDKPlayer::sync") << "created texture" << tex << m_size;
    QMetaObject::invokeMethod(m_item, "surfaceSizeUpdated", Q_ARG(uint, m_size.width()), Q_ARG(uint, m_size.height()));
    m_node->setTexture(tex);
    m_node->setOwnsTexture(true);
    m_node->setTextureCoordinatesTransform(m_tx); // MUST set when texture() is available
    m_node->setFiltering(QSGTexture::Linear);
    m_node->setRect(0, 0, m_item->width(), m_item->height());
    m_player->setVideoSurfaceSize(m_size.width(), m_size.height());
}

void MDKPlayer::play() {
    if (!m_videoLoaded || !m_player) return;
    m_player->set(mdk::PlaybackState::Playing);
    forceRedraw();
}
void MDKPlayer::pause() {
    if (!m_videoLoaded || !m_player) return;
    m_player->set(mdk::PlaybackState::Paused);
    forceRedraw();
}
void MDKPlayer::stop() {
    if (!m_videoLoaded || !m_player) return;
    m_player->set(mdk::PlaybackState::Stopped);
    m_player->waitFor(mdk::PlaybackState::Stopped);
}
void MDKPlayer::setFrameRate(float fps) {
    if (!m_player) return;
    m_player->setFrameRate(fps);
    m_overrideFps = fps;
}

void MDKPlayer::seekToTimestamp(float timestampMs, bool exact) {
    if (!m_videoLoaded || !m_player) return;

    m_player->seek(timestampMs, exact? mdk::SeekFlag::FromStart : mdk::SeekFlag::FromStart | mdk::SeekFlag::KeyFrame);
    forceRedraw();
}

void MDKPlayer::seekToFrameDelta(int64_t frameDelta) {
    if (!m_videoLoaded || !m_player) return;

    m_player->seek(frameDelta, mdk::SeekFlag::FromNow | mdk::SeekFlag::Frame);
    forceRedraw();
}

void MDKPlayer::seekToFrame(int64_t frame, int64_t currentFrame, bool exact) {
    if (!m_videoLoaded || !m_player) return;

    auto md = m_player->mediaInfo();
    if (!md.video.empty()) {
        auto v = md.video[0];
        seekToTimestamp((frame / v.codec.frame_rate) * 1000.0, exact);
    }
    forceRedraw();
}

void MDKPlayer::setPlaybackRate(float rate) { m_playbackRate = rate; if (m_player) m_player->setPlaybackRate(rate); }
float MDKPlayer::playbackRate() { return m_player? m_player->playbackRate() : m_playbackRate; }

void MDKPlayer::setPlaybackRange(int64_t from_ms, int64_t to_ms) {
    if (m_overrideFps > 0.0) {
        from_ms /= m_fps / m_overrideFps;
        to_ms   /= m_fps / m_overrideFps;
    }
    if (m_player) m_player->setRange(from_ms, to_ms);
}

void MDKPlayer::setRotation(int v) {
    if (!m_videoLoaded || !m_player) return;

    m_player->rotate(v);
    forceRedraw();
}
int MDKPlayer::getRotation() {
    if (!m_videoLoaded || !m_player) return 0;

    auto md = m_player->mediaInfo();
    if (!md.video.empty()) {
        return md.video[0].rotation;
    }
    return 0;
}

void MDKPlayer::initProcessingPlayer(uint64_t id, uint64_t width, uint64_t height, bool yuv, std::string custom_decoder, const std::vector<std::pair<uint64_t, uint64_t>> &ranges, VideoProcessCb &&cb) { // ms
    m_processingPlayers[id] = std::make_unique<mdk::Player>();
    auto player = m_processingPlayers[id].get();
    if (!custom_decoder.empty()) {
        player->setDecoders(MediaType::Video, { custom_decoder });
    } else {
        player->setDecoders(MediaType::Video, { "FFmpeg", "BRAW:gpu=auto", "R3D:gpu=auto" });
    }


    player->setMedia(m_player? m_player->url() : qUtf8Printable(m_pendingUrl.toLocalFile()));

    player->setDecoders(MediaType::Audio, { });
    player->setMute(true);
    player->onSync([] { return DBL_MAX; });

    if (ranges.empty()) {
        const_cast<std::vector<std::pair<uint64_t, uint64_t>> &>(ranges).push_back({ 0, UINT64_MAX });
    }

    auto range_id = std::make_shared<uint>(0);
    auto finished = std::make_shared<bool>(false);

    player->onFrame<mdk::VideoFrame>([cb, width, height, id, range_id = std::move(range_id), finished = std::move(finished), yuv, ranges, this](mdk::VideoFrame &v, int) -> int {
        if (*finished) return 0;
        if (!v || v.timestamp() == mdk::TimestampEOS) { // AOT frame(1st frame, seek end 1st frame) is not valid, but format is valid. eof frame format is invalid
            cb(-1, -1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            auto ptr = m_processingPlayers[id].release();
            std::thread([ptr] {
                ptr->set(mdk::PlaybackState::Stopped);
                ptr->waitFor(mdk::PlaybackState::Stopped);
                delete ptr;
            }).detach();
            *finished = true;
            return 0;
        }
        if (!v.format()) {
            printf("error occured!\n");
            return 0;
        }

        auto timestamp_ms = v.timestamp() * 1000.0;

        if (timestamp_ms >= ranges[*range_id].second) {
            if (*range_id + 1 < ranges.size()) {
                *range_id += 1;
                m_processingPlayers[id]->seek(ranges[*range_id].first, mdk::SeekFlag::FromStart);
                return 0;
            }
            auto ptr = m_processingPlayers[id].release();
            ptr->set(mdk::PlaybackState::Paused);
            ptr->waitFor(mdk::PlaybackState::Paused);
            cb(-1, -1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            std::thread([ptr] {
                ptr->set(mdk::PlaybackState::Stopped);
                ptr->waitFor(mdk::PlaybackState::Stopped);
                delete ptr;
            }).detach();
            *finished = true;
            return 0;
        }

        auto md = m_processingPlayers[id]->mediaInfo();
        if (!md.video.empty()) {
            auto vmd = md.video[0];

            auto frame_num = std::ceil(std::round(v.timestamp() * vmd.codec.frame_rate * 100) / 100.0);

            if (width == 0) const_cast<uint64_t&>(width) = v.width();
            if (height == 0) const_cast<uint64_t&>(height) = v.height();

            auto format = yuv? mdk::PixelFormat::YUV420P : mdk::PixelFormat::RGBA;
            if (!strcmp(md.format, "r3d")) format = mdk::PixelFormat::BGRA;

            /*switch (v.format()) {
                case mdk::PixelFormat::YUV420P:     qDebug() << "YUV420P";     break;
                case mdk::PixelFormat::NV12:        qDebug() << "NV12";        break;
                case mdk::PixelFormat::YUV422P:     qDebug() << "YUV422P";     break;
                case mdk::PixelFormat::YUV444P:     qDebug() << "YUV444P";     break;
                case mdk::PixelFormat::P010LE:      qDebug() << "P010LE";      break;
                case mdk::PixelFormat::P016LE:      qDebug() << "P016LE";      break;
                case mdk::PixelFormat::YUV420P10LE: qDebug() << "YUV420P10LE"; break;
                case mdk::PixelFormat::UYVY422:     qDebug() << "UYVY422";     break;
                case mdk::PixelFormat::RGB24:       qDebug() << "RGB24";       break;
                case mdk::PixelFormat::RGBA:        qDebug() << "RGBA";        break;
                case mdk::PixelFormat::RGBX:        qDebug() << "RGBX";        break;
                case mdk::PixelFormat::BGRA:        qDebug() << "BGRA";        break;
                case mdk::PixelFormat::BGRX:        qDebug() << "BGRX";        break;
                case mdk::PixelFormat::RGB565LE:    qDebug() << "RGB565LE";    break;
                case mdk::PixelFormat::RGB48LE:     qDebug() << "RGB48LE";     break;
                case mdk::PixelFormat::GBRP:        qDebug() << "GBRP";        break;
                case mdk::PixelFormat::GBRP10LE:    qDebug() << "GBRP10LE";    break;
                case mdk::PixelFormat::XYZ12LE:     qDebug() << "XYZ12LE";     break;
                case mdk::PixelFormat::YUVA420P:    qDebug() << "YUVA420P";    break;
                case mdk::PixelFormat::BC1:         qDebug() << "BC1";         break;
                case mdk::PixelFormat::BC3:         qDebug() << "BC3";         break;
                case mdk::PixelFormat::RGBA64:      qDebug() << "RGBA64";      break;
                case mdk::PixelFormat::BGRA64:      qDebug() << "BGRA64";      break;
                case mdk::PixelFormat::RGBP16:      qDebug() << "RGBP16";      break;
                case mdk::PixelFormat::RGBPF32:     qDebug() << "RGBPF32";     break;
                case mdk::PixelFormat::BGRAF32:     qDebug() << "BGRAF32";     break;
            }*/

            auto vscaled = v.to(format, width, height);
            auto ptr = vscaled.bufferData();
            auto ptr_size = vscaled.bytesPerLine() * vscaled.height();

            if (!cb(frame_num, timestamp_ms, vscaled.width(), vscaled.height(), vmd.codec.width, vmd.codec.height, vmd.codec.frame_rate, vmd.duration, vmd.frames, ptr, ptr_size)) {
                // If cb returns false - stop the processing
                auto pptr = m_processingPlayers[id].release();
                pptr->set(mdk::PlaybackState::Paused);
                pptr->waitFor(mdk::PlaybackState::Paused);
                cb(-1, -1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
                std::thread([pptr] {
                    pptr->set(mdk::PlaybackState::Stopped);
                    pptr->waitFor(mdk::PlaybackState::Stopped);
                    delete pptr;
                }).detach();
                *finished = true;
                return 0;
            }
        }
        return 0;
    });
    player->setVideoSurfaceSize(64, 64);

    player->prepare(ranges[0].first);
    player->set(State::Running);
}

std::map<std::string, std::string> MDKPlayer::getMediaInfo(const MediaInfo &mi) {
    std::map<std::string, std::string> ret;
    ret["start_time"] = std::to_string(mi.start_time);
    ret["bit_rate"]   = std::to_string(mi.bit_rate);
    ret["size"]       = std::to_string(mi.size);
    ret["format"]     = std::string(mi.format);
    ret["streams"]    = std::to_string(mi.streams);
    for (const auto &x : mi.metadata) {
        ret["metadata." + x.first] = x.second;
    }
    int i = 0;
    for (const auto &v : mi.video) {
        std::string key = "stream.video[" + std::to_string(i) + "].";
        ret[key + "index"] = std::to_string(v.index);
        ret[key + "start_time"] = std::to_string(v.start_time);
        ret[key + "duration"] = std::to_string(m_duration);
        ret[key + "frames"] = std::to_string(v.frames);
        ret[key + "rotation"] = std::to_string(v.rotation);
        for (const auto &x : v.metadata) {
            ret[key + "metadata." + x.first] = x.second;
        }

        ret[key + "codec.name"]         = std::string(v.codec.codec);
        ret[key + "codec.tag"]          = std::to_string(v.codec.codec_tag);
        //ret[key + "codec.extra_data"] = std::to_string(v.codec.extra_data, v.codec.extra_data_size);
        ret[key + "codec.bit_rate"]     = std::to_string(v.codec.bit_rate);
        ret[key + "codec.profile"]      = std::to_string(v.codec.profile);
        ret[key + "codec.level"]        = std::to_string(v.codec.level);
        ret[key + "codec.frame_rate"]   = std::to_string(m_overrideFps > 0.0? m_overrideFps : v.codec.frame_rate);
        ret[key + "codec.format"]       = std::to_string(v.codec.format);
        ret[key + "codec.format_name"]  = std::string(v.codec.format_name);
        ret[key + "codec.width"]        = std::to_string(v.codec.width);
        ret[key + "codec.height"]       = std::to_string(v.codec.height);
        ret[key + "codec.b_frames"]     = std::to_string(v.codec.b_frames);

        ++i;
    }
    i = 0;
    for (const auto &a : mi.audio) {
        std::string key = "stream.audio[" + std::to_string(i) + "].";
        ret[key + "index"] = std::to_string(a.index);
        ret[key + "start_time"] = std::to_string(a.start_time);
        ret[key + "duration"] = std::to_string(a.duration);
        ret[key + "frames"] = std::to_string(a.frames);
        for (const auto &x : a.metadata) {
            ret[key + "metadata." + x.first] = x.second;
        }

        ret[key + "codec.name"]            = std::string(a.codec.codec);
        ret[key + "codec.tag"]             = std::to_string(a.codec.codec_tag);
        //ret[key + "codec.extra_data"]    = std::to_string(a.codec.extra_data, a.codec.extra_data_size);
        ret[key + "codec.bit_rate"]        = std::to_string(a.codec.bit_rate);
        ret[key + "codec.profile"]         = std::to_string(a.codec.profile);
        ret[key + "codec.level"]           = std::to_string(a.codec.level);
        ret[key + "codec.frame_rate"]      = std::to_string(a.codec.frame_rate);
        ret[key + "codec.is_float"]        = a.codec.is_float?    "true" : "false";
        ret[key + "codec.is_unsigned"]     = a.codec.is_unsigned? "true" : "false";
        ret[key + "codec.is_planar"]       = a.codec.is_planar?   "true" : "false";
        ret[key + "codec.raw_sample_size"] = std::to_string(a.codec.raw_sample_size);
        ret[key + "codec.channels"]        = std::to_string(a.codec.channels);
        ret[key + "codec.sample_rate"]     = std::to_string(a.codec.sample_rate);
        ret[key + "codec.block_align"]     = std::to_string(a.codec.block_align);
        ret[key + "codec.frame_size"]      = std::to_string(a.codec.frame_size);

        ++i;
    }
    return ret;
}

QSGDefaultRenderContext *MDKPlayer::rhiContext() {
    return static_cast<QSGDefaultRenderContext *>(QQuickItemPrivate::get(m_item)->sceneGraphRenderContext());
}
QRhiTexture *MDKPlayer::rhiTexture() {
    return m_texture;
}
QRhiTextureRenderTarget *MDKPlayer::rhiRenderTarget() {
    return m_rt;
}
QRhiRenderPassDescriptor *MDKPlayer::rhiRenderPassDescriptor() {
    return m_rtRp;
}
QQuickWindow *MDKPlayer::qmlWindow() {
    return m_window;
}
QQuickItem *MDKPlayer::qmlItem() {
    return m_item;
}
QSize MDKPlayer::textureSize() {
    return m_size;
}
QMatrix4x4 MDKPlayer::textureMatrix() {
    return m_proj;
}
void *MDKPlayer::userData() const {
    return m_userData;
}
void MDKPlayer::setUserData(void *ptr) {
    m_userData = ptr;
}
void MDKPlayer::setUserDataDestructor(std::function<void(void *)> &&cb) {
    m_userDataDestructor = cb;
}
