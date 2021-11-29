#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGImageNode>
#include <QtGui/QPainter>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtCore/QJsonObject>
#include <future>
#include <chrono>
#include <queue>
#include <atomic>
#include <functional>

#include "VideoTextureNode.cpp"

#include "mdk/Player.h"
#include "mdk/VideoFrame.h"

typedef std::function<uint8_t*(QQuickItem *item, uint32_t frame, uint32_t width, uint32_t height, const uint8_t *bits, uint64_t bitsSize)> ProcessPixelsCb;
typedef std::function<void(int32_t frame, uint32_t width, uint32_t height, const uint8_t *bits, uint64_t bitsSize)> VideoProcessCb;

void printMd(const std::map<std::string, std::string> &md) {
    for (auto &x : md) {
        qDebug() << QString::fromStdString(x.first) << " = " << QString::fromStdString(x.second);
    }
}


class MDKPlayer : public VideoTextureNodePriv {
public:
    MDKPlayer() {
    }
    void initPlayer() {
        m_player = std::make_unique<mdk::Player>();
        m_player->setDecoders(mdk::MediaType::Video, {
#if (__APPLE__+0)
        "VT",
#elif (__ANDROID__+0)
        "AMediaCodec:java=1:copy=0:surface=1:async=0",
#elif (_WIN32+0)
        "MFT:d3d=11",
        //"CUDA",
        //"NVDEC",
        //"CUVID",
        "D3D11",
        "DXVA",
#elif (__linux__+0)
        "VAAPI",
        "VDPAU",
        "CUDA",
        "MMAL",
#endif
        "FFmpeg"});

        if (m_item && m_node && m_window) {
            setupPlayer();
            if (m_size.width() > 0 && m_size.height() > 0) {
                m_syncNext = true;
            }
        }
    }
    void destroyPlayer() {
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

    ~MDKPlayer() {
        m_videoLoaded = false;
        m_firstFrameLoaded = false;
        m_processPixels = nullptr;

        destroyPlayer();
    }

    void setUrl(const QUrl &url) {
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

    void setBackgroundColor(const QColor &color) {
        m_bgColor = color;
        if (m_player)
            m_player->setBackgroundColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), m_bgColor.alphaF());
    }

    void setMuted(bool v) {
        if (m_player)
            m_player->setMute(v);
    }
    bool getMuted() { return m_player? m_player->isMute() : false; }

    inline QColor getBackgroundColor() { return m_bgColor; }

    void setupNode(QSGImageNode *node, QQuickItem *item, ProcessPixelsCb &&processPixels) {
        m_node = node;
        m_item = item;
        m_window = item->window();
        m_processPixels = processPixels;
        if (!m_pendingUrl.isEmpty()) {
            setUrl(m_pendingUrl);
            m_pendingUrl = QUrl();
        }
    }
    void setupPlayer() {
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
                QMetaObject::invokeMethod(m_item, "metadataLoaded", Q_ARG(QJsonObject, obj));
                m_firstFrameLoaded = true;
            }
            qDebug() << QString::fromUtf8(evt.category.c_str(), evt.category.size()) << QString::fromUtf8(evt.detail.c_str(), evt.detail.size());
            return true;
        });

        m_player->setBackgroundColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), m_bgColor.alphaF());
        m_player->setPlaybackRate(m_playbackRate);

        m_player->onMediaStatusChanged([this](mdk::MediaStatus status) -> bool {
            
            qDebug() << "onMediaStatusChanged" <<
                QString(status & mdk::MediaStatus::Unloaded?  "Unloaded | "  : "") +       
                QString(status & mdk::MediaStatus::Loading?   "Loading | "   : "") +    
                QString(status & mdk::MediaStatus::Loaded?    "Loaded | "    : "") +  
                QString(status & mdk::MediaStatus::Prepared?  "Prepared | "  : "") +      
                QString(status & mdk::MediaStatus::Stalled?   "Stalled | "   : "") +    
                QString(status & mdk::MediaStatus::Buffering? "Buffering | " : "") +        
                QString(status & mdk::MediaStatus::Buffered?  "Buffered | "  : "") +      
                QString(status & mdk::MediaStatus::End?       "End | "       : "") +    
                QString(status & mdk::MediaStatus::Seeking?   "Seeking | "   : "") +     
                QString(status & mdk::MediaStatus::Invalid?   "Invalid | "   : "");

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
                    QMetaObject::invokeMethod(m_item, "videoLoaded", Q_ARG(double, v.duration), Q_ARG(qlonglong, v.frames), Q_ARG(double, v.codec.frame_rate), Q_ARG(uint, v.codec.width), Q_ARG(uint, v.codec.height));
                }
                m_player->setLoop(9999999);
                m_videoLoaded = true;

                if (!m_connectionBeforeRendering) 
                    m_connectionBeforeRendering = QObject::connect(m_window, &QQuickWindow::beforeRendering, [this] { this->windowBeforeRendering(); });
                if (!m_connectionScreenChanged) 
                    m_connectionScreenChanged = QObject::connect(m_window, &QQuickWindow::screenChanged, [this](QScreen *) { m_item->update(); });
            }

            return true;
        });

        /*m_player->onFrame<mdk::VideoFrame>([this](mdk::VideoFrame &frame, int track) -> int {
            
            //QMetaObject::invokeMethod(m_item, "frameRendered", Q_ARG(double, frame.timestamp() * 1000.0));
            // qDebug() << "frame" << track << frame.timestamp() << (int)frame.format() << frame.planeCount() << frame.bufferData(0);
            return 0;
        });*/
    }

    void windowBeforeRendering() {
        if (!m_videoLoaded || !m_player) return;
        double timestamp = m_player->renderVideo();

        if (m_processPixels && m_firstFrameLoaded.load()) {
            auto img = toImage();
            if (!m_videoLoaded || !m_player) return;

            int frame = std::ceil(timestamp * m_fps);

            const auto ptr = m_processPixels(m_item, frame, img.width(), img.height(), img.scanLine(0), img.sizeInBytes());
            if (ptr != img.scanLine(0)) {
                fromImage(QImage(ptr, img.width(), img.height(), img.format()));
            } else {
                fromImage(img);
            }
        }

        QMetaObject::invokeMethod(m_item, "frameRendered", Q_ARG(double, timestamp * 1000.0));
    }

    void sync(QSize newSize, bool force = false) {
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
        qDebug() << "created texture" << tex << m_size;
        QMetaObject::invokeMethod(m_item, "surfaceSizeUpdated", Q_ARG(uint, m_size.width()), Q_ARG(uint, m_size.height()));
        m_node->setTexture(tex);
        m_node->setOwnsTexture(true);
        m_node->setTextureCoordinatesTransform(m_tx); // MUST set when texture() is available
        m_node->setFiltering(QSGTexture::Linear);
        m_node->setRect(0, 0, m_item->width(), m_item->height());
        m_player->setVideoSurfaceSize(m_size.width(), m_size.height());
    }

    void play() {
        if (!m_videoLoaded || !m_player) return;
        m_player->set(mdk::PlaybackState::Playing);
    }
    void pause() {
        if (!m_videoLoaded || !m_player) return;
        m_player->set(mdk::PlaybackState::Paused);
    }
    void stop() {
        if (!m_videoLoaded || !m_player) return;
        m_player->set(mdk::PlaybackState::Stopped);
    }

    void seekToTimestamp(float timestampMs, bool keyframe = false) {
        if (!m_videoLoaded || !m_player) return;

        m_player->seek(timestampMs, keyframe? mdk::SeekFlag::FromStart | mdk::SeekFlag::KeyFrame : mdk::SeekFlag::FromStart);
    }

    void seekToFrame(int64_t frame, int64_t currentFrame) {
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

    void setPlaybackRate(float rate) { m_playbackRate = rate; if (m_player) m_player->setPlaybackRate(rate); }
    float playbackRate() { return m_player? m_player->playbackRate() : m_playbackRate; }

    void setPlaybackRange(int64_t from_ms, int64_t to_ms) {
        if (m_player) m_player->setRange(from_ms, to_ms);
    }

    void setRotation(int v) {
        if (!m_videoLoaded || !m_player) return;

        m_player->rotate(v);
    }
    int getRotation() {
        if (!m_videoLoaded || !m_player) return 0;
        
        auto md = m_player->mediaInfo();
        if (!md.video.empty()) {
            return md.video[0].rotation;
        }
        return 0;
    }

    std::map<std::string, std::string> getMediaInfo(const MediaInfo &mi) {
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
            ret[key + "duration"] = std::to_string(v.duration);
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
            ret[key + "codec.frame_rate"]   = std::to_string(v.codec.frame_rate);
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

    void initProcessingPlayer(uint64_t id, uint64_t width, uint64_t height, bool yuv, const std::vector<std::pair<uint64_t, uint64_t>> &ranges, VideoProcessCb &&cb) { // ms
        m_processingPlayers[id] = std::make_unique<mdk::Player>();
        auto player = m_processingPlayers[id].get();
        player->setDecoders(MediaType::Video, { "FFmpeg" });

        player->setMedia(m_player->url());
        
        player->setDecoders(MediaType::Audio, { });
        player->onSync([] { return DBL_MAX; });

        auto range_id = new uint(0);

        player->onFrame<mdk::VideoFrame>([cb, width, height, id, range_id, yuv, ranges, this](mdk::VideoFrame &v, int) {
            if (!v || v.timestamp() == mdk::TimestampEOS) { // AOT frame(1st frame, seek end 1st frame) is not valid, but format is valid. eof frame format is invalid
                cb(-1, 0, 0, 0, 0);
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
                cb(-1, 0, 0, 0, 0);
                m_processingPlayers[id].reset();
                delete range_id;
                return 0;
            }

            auto md = m_processingPlayers[id]->mediaInfo();
            if (!md.video.empty()) {
                auto vmd = md.video[0];
                auto frame_num = std::ceil(v.timestamp() * vmd.codec.frame_rate);

                auto vscaled = v.to(yuv? mdk::PixelFormat::YUV420P : mdk::PixelFormat::RGBA, width, height);
                auto ptr = vscaled.bufferData();
                auto ptr_size = vscaled.bytesPerLine() * vscaled.height();

                cb(frame_num, vscaled.width(), vscaled.height(), ptr, ptr_size);
            }
            return 0;
        });
        player->setVideoSurfaceSize(64, 64);

        player->prepare(ranges[0].first);
        player->set(State::Running);
    }

private:
    QMetaObject::Connection m_connectionBeforeRendering;
    QMetaObject::Connection m_connectionScreenChanged;

    ProcessPixelsCb m_processPixels;

    std::unique_ptr<mdk::Player> m_player;
    std::map<uint64_t, std::unique_ptr<mdk::Player>> m_processingPlayers;

    std::atomic<bool> m_videoLoaded{false};
    std::atomic<bool> m_firstFrameLoaded{false};

    double m_fps{0.0};
    float m_playbackRate{1.0};
    bool m_syncNext{false};

    QSGImageNode *m_node{nullptr};
    QColor m_bgColor;
    QUrl m_pendingUrl;
};
