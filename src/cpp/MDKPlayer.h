#ifndef MDK_PLAYER_H
#define MDK_PLAYER_H

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGImageNode>
#include <QtCore/QJsonObject>
#include <future>
#include <chrono>
#include <queue>
#include <atomic>
#include <functional>

#include "VideoTextureNode.h"

typedef std::function<QImage(QQuickItem *item, uint32_t frame, double timestamp, const QImage &img)> ProcessPixelsCb;
typedef std::function<void(int32_t frame, double timestamp, uint32_t width, uint32_t height, const uint8_t *bits, uint64_t bitsSize)> VideoProcessCb;

namespace mdk { class Player; }

class MDKPlayer : public VideoTextureNodePriv {
public:
    MDKPlayer();
    void initPlayer();
    void destroyPlayer();

    ~MDKPlayer();

    void setUrl(const QUrl &url);

    void setBackgroundColor(const QColor &color);

    void setMuted(bool v);
    bool getMuted();

    inline QColor getBackgroundColor() { return m_bgColor; }

    void setupNode(QSGImageNode *node, QQuickItem *item, ProcessPixelsCb &&processPixels);

    void setupGpuCompute(std::function<bool(QSize texSize, QSizeF itemSize)> &&initCb, std::function<bool(double, int32_t, bool)> &&renderCb, std::function<void()> &&cleanupCb);
    void cleanupGpuCompute();

    void setupPlayer();

    void windowBeforeRendering();

    void sync(QSize newSize, bool force = false);

    void play();
    void pause();
    void stop();

    void seekToTimestamp(float timestampMs, bool keyframe = false);

    void seekToFrame(int64_t frame, int64_t currentFrame);

    void setPlaybackRate(float rate);
    float playbackRate();

    void setPlaybackRange(int64_t from_ms, int64_t to_ms);

    void setRotation(int v);
    int getRotation();

    void initProcessingPlayer(uint64_t id, uint64_t width, uint64_t height, bool yuv, const std::vector<std::pair<uint64_t, uint64_t>> &ranges, VideoProcessCb &&cb);

    std::map<std::string, std::string> getMediaInfo(const MediaInfo &mi);

    QSGDefaultRenderContext *rhiContext();
    QRhiTexture *rhiTexture();
    QRhiTextureRenderTarget *rhiRenderTarget();
    QRhiRenderPassDescriptor *rhiRenderPassDescriptor();
    QQuickWindow *qmlWindow();
    QQuickItem *qmlItem();
    QSize textureSize();
    QMatrix4x4 textureMatrix();

private:
    QMetaObject::Connection m_connectionBeforeRendering;
    QMetaObject::Connection m_connectionScreenChanged;

    ProcessPixelsCb m_processPixels;

    bool m_gpuProcessingInited{false};
    std::function<bool(QSize, QSizeF)> m_gpuProcessInit;
    std::function<bool(double, int32_t, bool)> m_gpuProcessRender;
    std::function<void()> m_gpuProcessCleanup;

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

#endif
