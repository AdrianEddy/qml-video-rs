#include "MDKPlayer.h"
#include <map>
#include <string>

#include "mdk/Player.h"
#include "mdk/VideoFrame.h"
using namespace mdk;

void printMd(const std::map<std::string, std::string> &md) {
    for (auto &x : md) {
        qDebug2("printMd") << QString::fromStdString(x.first) << " = " << QString::fromStdString(x.second);
    }
}

MDKPlayer::MDKPlayer() { }

void MDKPlayer::initPlayer() {
    m_player = std::make_unique<mdk::Player>();
    
    QString overrideDecoders = QString(qgetenv("MDK_DECODERS")).trimmed();

    if (!overrideDecoders.isEmpty()) {
        std::vector<std::string> vec;
        for (const auto &x : overrideDecoders.split(",")) {
            vec.push_back(x.toStdString());
        }
        m_player->setDecoders(mdk::MediaType::Video, vec);
    } else {
        m_player->setDecoders(mdk::MediaType::Video, {
    #if (__APPLE__+0)
        "VT",
    #elif (__ANDROID__+0)
        "AMediaCodec:java=1:copy=0:surface=1:async=0",
    #elif (_WIN32+0)
        // "MFT:d3d=11",
        //"CUDA",
        //"NVDEC",
        //"CUVID",
        "D3D11",
        "DXVA",
    #elif (__linux__+0)
        "CUDA",
        "VDPAU",
        "MMAL",
        "VAAPI",
    #endif
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
        m_player->setRenderCallback(nullptr);
        m_player->onMediaStatusChanged(nullptr);
        m_player->onEvent(nullptr);
        m_player->onFrame<mdk::VideoFrame>(nullptr);
        m_player.reset();
    }
}

MDKPlayer::~MDKPlayer() {
    m_videoLoaded = false;
    m_firstFrameLoaded = false;
    m_processPixels = nullptr;

    destroyPlayer();
}

void MDKPlayer::setUrl(const QUrl &url) {
    m_overrideFps = 0.0;
    if (!m_item) {
        m_pendingUrl = url;
        return;
    }
    destroyPlayer();
    initPlayer();
    
    QString path = url.toLocalFile();
    m_player->setMedia(qUtf8Printable(path));
    m_player->prepare();
}

void MDKPlayer::setBackgroundColor(const QColor &color) {
    m_bgColor = color;
    if (m_player)
        m_player->setBackgroundColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), m_bgColor.alphaF());
}

void MDKPlayer::setMuted(bool v) {
    if (m_player)
        m_player->setMute(v);
}
bool MDKPlayer::getMuted() { return m_player? m_player->isMute() : false; }

void MDKPlayer::setupNode(QSGImageNode *node, QQuickItem *item, ProcessPixelsCb &&processPixels) {
    m_node = node;
    m_item = item;
    m_window = item->window();
    m_processPixels = processPixels;
    if (!m_pendingUrl.isEmpty()) {
        setUrl(m_pendingUrl);
        m_pendingUrl = QUrl();
    }
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
            m_firstFrameLoaded = evt.detail == "1st_frame";
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
}

void MDKPlayer::windowBeforeRendering() {
    if (!m_videoLoaded || !m_player) return;

    auto position = m_player->position();
    if (m_renderedPosition == position && m_renderedReturnCount++ > 100) {
        return;
    }

    auto context = static_cast<QSGDefaultRenderContext *>(QQuickItemPrivate::get(m_item)->sceneGraphRenderContext());
    auto cb = context->currentFrameCommandBuffer();

    cb->beginExternal();
    double timestamp = m_player->renderVideo(); 
    cb->endExternal();
    
    double fps = m_fps;
    if (m_overrideFps > 0.0) {
        timestamp *= m_fps / m_overrideFps;
        fps = m_overrideFps;
    }

    int frame = std::ceil(std::round(timestamp * fps * 100.0) / 100.0);

    bool processed = false;
    if (m_firstFrameLoaded.load()) {
        if (m_gpuProcessingInited && m_gpuProcessRender) {
            processed = m_gpuProcessRender(timestamp, frame, false);
            if (!processed) {
                qDebug2("MDKPlayer::windowBeforeRendering") << "Failed to run the GPU compute shader";
            }
        }
        if (!processed && m_processPixels) {
            auto img = toImage();
            if (!m_videoLoaded || !m_player) return;

            const auto img2 = m_processPixels(m_item, frame, timestamp * 1000.0, img);
            if (!img2.isNull() && img2.constBits()) {
                fromImage(img2);
            }
        }
    }

    if (m_renderedPosition != position)
        m_renderedReturnCount = 0;
    
    m_renderedPosition = position;

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

    if (m_gpuProcessCleanup) m_gpuProcessCleanup();
    if (m_gpuProcessInit) m_gpuProcessingInited = m_gpuProcessInit(m_size, m_item->size());
}

void MDKPlayer::play() {
    if (!m_videoLoaded || !m_player) return;
    m_player->set(mdk::PlaybackState::Playing);
}
void MDKPlayer::pause() {
    if (!m_videoLoaded || !m_player) return;
    m_player->set(mdk::PlaybackState::Paused);
}
void MDKPlayer::stop() {
    if (!m_videoLoaded || !m_player) return;
    m_player->set(mdk::PlaybackState::Stopped);
}
void MDKPlayer::setFrameRate(float fps) {
    if (!m_player) return;
    m_player->setFrameRate(fps);
    m_overrideFps = fps;
}

void MDKPlayer::seekToTimestamp(float timestampMs, bool keyframe) {
    if (!m_videoLoaded || !m_player) return;

    m_player->seek(timestampMs, keyframe? mdk::SeekFlag::FromStart | mdk::SeekFlag::KeyFrame : mdk::SeekFlag::FromStart);
}

void MDKPlayer::seekToFrame(int64_t frame, int64_t currentFrame) {
    if (!m_videoLoaded || !m_player) return;
    
    auto delta = frame - currentFrame;
    if (delta > 0 && delta < 10) {
        m_player->seek(delta, mdk::SeekFlag::FromNow | mdk::SeekFlag::Frame);
    } else {
        auto md = m_player->mediaInfo();
        if (!md.video.empty()) {
            auto v = md.video[0];
            seekToTimestamp((frame / v.codec.frame_rate) * 1000.0);
        }
    }
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
}
int MDKPlayer::getRotation() {
    if (!m_videoLoaded || !m_player) return 0;
    
    auto md = m_player->mediaInfo();
    if (!md.video.empty()) {
        return md.video[0].rotation;
    }
    return 0;
}

void MDKPlayer::initProcessingPlayer(uint64_t id, uint64_t width, uint64_t height, bool yuv, const std::vector<std::pair<uint64_t, uint64_t>> &ranges, VideoProcessCb &&cb) { // ms
    m_processingPlayers[id] = std::make_unique<mdk::Player>();
    auto player = m_processingPlayers[id].get();
    player->setDecoders(MediaType::Video, { "FFmpeg" });

    player->setMedia(m_player->url());
    
    player->setDecoders(MediaType::Audio, { });
    player->onSync([] { return DBL_MAX; });

    auto range_id = new uint(0);

    player->onFrame<mdk::VideoFrame>([cb, width, height, id, range_id, yuv, ranges, this](mdk::VideoFrame &v, int) {
        if (!v || v.timestamp() == mdk::TimestampEOS) { // AOT frame(1st frame, seek end 1st frame) is not valid, but format is valid. eof frame format is invalid
            cb(-1, -1.0, 0, 0, 0, 0);
            m_processingPlayers[id].reset();
            delete range_id;
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
            m_processingPlayers[id]->set(State::Stopped);
            cb(-1, -1.0, 0, 0, 0, 0);
            m_processingPlayers[id].reset();
            delete range_id;
            return 0;
        }

        auto md = m_processingPlayers[id]->mediaInfo();
        if (!md.video.empty()) {
            auto vmd = md.video[0];
           
            auto frame_num = std::ceil(std::round(v.timestamp() * vmd.codec.frame_rate * 100) / 100.0);

            auto vscaled = v.to(yuv? mdk::PixelFormat::YUV420P : mdk::PixelFormat::RGBA, width, height);
            auto ptr = vscaled.bufferData();
            auto ptr_size = vscaled.bytesPerLine() * vscaled.height();

            cb(frame_num, timestamp_ms, vscaled.width(), vscaled.height(), ptr, ptr_size);
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

void MDKPlayer::setupGpuCompute(std::function<bool(QSize texSize, QSizeF itemSize)> &&initCb, std::function<bool(double, int32_t, bool)> &&renderCb, std::function<void()> &&cleanupCb) {
    m_gpuProcessInit = initCb;
    m_gpuProcessRender = renderCb;
    m_gpuProcessCleanup = cleanupCb;
    m_syncNext = true;
}
void MDKPlayer::cleanupGpuCompute() {
    m_gpuProcessingInited = false;
    if (m_gpuProcessCleanup) m_gpuProcessCleanup();
}
QSGDefaultRenderContext *MDKPlayer::rhiContext() {
    return static_cast<QSGDefaultRenderContext *>(QQuickItemPrivate::get(m_item)->sceneGraphRenderContext());
}
QRhiTexture *MDKPlayer::rhiTexture() {
    return m_texture;
}
QRhiTexture *MDKPlayer::rhiTexture2() {
    return m_texture2;
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
