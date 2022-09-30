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

typedef std::function<bool(QQuickItem *item, uint32_t frame, double timestamp, uint32_t width, uint32_t height, uint32_t backend_id, uint64_t ptr1, uint64_t ptr2, uint64_t ptr3, uint64_t ptr4)> ProcessTextureCb;
typedef std::function<QImage(QQuickItem *item, uint32_t frame, double timestamp, const QImage &img)> ProcessPixelsCb;
typedef std::function<bool(QQuickItem *item)> ReadyForProcessingCb;
typedef std::function<bool(int32_t frame, double timestamp, uint32_t width, uint32_t height, const uint8_t *bits, uint64_t bitsSize)> VideoProcessCb;

namespace mdk { class Player; }

class MDKPlayer : public VideoTextureNodePriv {
public:
    MDKPlayer();
    void initPlayer();
    void destroyPlayer();

    ~MDKPlayer();

    void setUrl(const QUrl &url, const QString &customDecoder);

    void setBackgroundColor(const QColor &color);

    void setMuted(bool v);
    bool getMuted();

    inline QColor getBackgroundColor() { return m_bgColor; }

    void setupNode(QSGImageNode *node, QQuickItem *item);
    void setProcessPixelsCallback(ProcessPixelsCb &&cb);
    void setProcessTextureCallback(ProcessTextureCb &&cb);
    void setReadyForProcessingCallback(ReadyForProcessingCb &&cb);

    void setupPlayer();

    void windowBeforeRendering();

    void sync(QSize newSize, bool force = false);
    void forceRedraw() { m_renderedPosition = -1; m_playerPosition = 0; m_renderedReturnCount = 0; }

    void play();
    void pause();
    void stop();

    void seekToTimestamp(float timestampMs, bool keyframe = false);
    void seekToFrame(int64_t frame, int64_t currentFrame);

    void setFrameRate(float fps);

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

    void *userData() const;
    void setUserData(void *ptr);
    void setUserDataDestructor(std::function<void(void *)> &&cb);

private:
    QMetaObject::Connection m_connectionBeforeRendering;
    QMetaObject::Connection m_connectionScreenChanged;

    void *m_userData{nullptr};
    std::function<void(void *)> m_userDataDestructor;

    ProcessPixelsCb m_processPixels;
    ProcessTextureCb m_processTexture;
    ReadyForProcessingCb m_readyForProcessing;

    std::unique_ptr<mdk::Player> m_player;
    std::map<uint64_t, std::unique_ptr<mdk::Player>> m_processingPlayers;

    std::atomic<bool> m_videoLoaded{false};
    std::atomic<bool> m_firstFrameLoaded{false};

    int m_renderFailCounter{10};

    int64_t m_renderedPosition{-1};
    int64_t m_renderedReturnCount{0};
    double m_fps{0.0};
    double m_overrideFps{0.0};
    double m_duration{0.0};
    float m_playbackRate{1.0};
    bool m_syncNext{false};
    int64_t m_playerPosition{0};

    QSGImageNode *m_node{nullptr};
    QColor m_bgColor;
    QUrl m_pendingUrl;
    QString m_pendingCustomDecoder;
};

// Simple wrapper class to workaround class alignment issues when using it from Rust
class MDKPlayerWrapper {
public:
    MDKPlayerWrapper() { mdkplayer = new MDKPlayer(); }
    ~MDKPlayerWrapper() { delete mdkplayer; }
    MDKPlayer *mdkplayer{nullptr};
};

#endif
