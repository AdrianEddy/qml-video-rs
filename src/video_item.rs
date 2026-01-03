#![allow(non_snake_case)]
#![allow(unused_variables)]

use cpp::*;
use qmetaobject::scenegraph::*;
use qmetaobject::*;
use crate::video_player::*;

type ProcessPixelsCb = Box<dyn Fn(u32, f64, u32, u32, u32, &mut [u8]) -> (u32, u32, u32, *mut u8)>;
type ProcessTextureCb = Box<dyn Fn(u32, f64, u32, u32, u32, u64, u64, u64, u64, u64) -> bool>;
type ReadyForProcessingCb = Box<dyn Fn() -> bool>;
type ResizeCb = Box<dyn Fn(u32, u32)>;

pub enum QSGImageNode {}

cpp! {{
    #include <private/qquickshadereffect_p.h>
    #ifdef Q_OS_ANDROID
    #   include <QJniEnvironment>
    #   include <QtCore/private/qandroidextras_p.h>
    #endif
}}

#[derive(Default, QObject)]
pub struct MDKVideoItem {
    base: qt_base_class!(trait QQuickItem),

    pub play:  qt_method!(fn(&mut self)),
    pub pause: qt_method!(fn(&mut self)),
    pub stop:  qt_method!(fn(&mut self)),

    pub playing: qt_property!(bool; NOTIFY playingChanged),
    pub playingChanged: qt_signal!(),

    pub backgroundColor: qt_property!(QColor; WRITE setBackgroundColor READ getBackgroundColor),
    pub videoRotation: qt_property!(i32; WRITE setRotation READ getRotation),
    pub playbackRate: qt_property!(f32; WRITE setPlaybackRate READ getPlaybackRate),

    pub currentFrame:        qt_property!(i64; WRITE setCurrentFrame NOTIFY currentFrameChanged),
    pub setCurrentFrame:     qt_method!(fn(&mut self, frame: i64)),
    pub currentFrameChanged: qt_signal!(),

    pub timestamp:        qt_property!(f64; WRITE setTimestamp NOTIFY timestampChanged),
    pub setTimestamp:     qt_method!(fn(&mut self, timestamp: f64)),
    pub timestampChanged: qt_signal!(),

    pub seekToFrame:      qt_method!(fn(&mut self, frame: i64, exact: bool)),
    pub seekToFrameDelta: qt_method!(fn(&mut self, frame_delta: i64)),
    pub seekToTimestamp:  qt_method!(fn(&mut self, timestamp: f64, exact: bool)),

    pub setFrameRate: qt_method!(fn(&mut self, fps: f64)),

    pub url:    qt_property!(QUrl; CONST),
    pub setUrl: qt_method!(fn(&mut self, url: QUrl, custom_decoder: QString)),
    pub setProperty: qt_method!(fn(&mut self, key: QString, value: QString)),
    pub setDefaultProperty: qt_method!(fn(&mut self, key: QString, value: QString)),

    pub forceRedraw: qt_method!(fn(&mut self)),

    pub muted: qt_property!(bool; READ getMuted WRITE setMuted NOTIFY mutedChanged),
    pub mutedChanged: qt_signal!(),

    pub volume: qt_property!(f32; READ getVolume WRITE setVolume NOTIFY volumeChanged),
    pub volumeChanged: qt_signal!(),

    pub videoWidth: qt_property!(u32; NOTIFY metadataChanged),
    pub videoHeight: qt_property!(u32; NOTIFY metadataChanged),

    pub surfaceWidth: qt_property!(u32; NOTIFY surfaceSizeChanged),
    pub surfaceHeight: qt_property!(u32; NOTIFY surfaceSizeChanged),

    pub duration:   qt_property!(f64; NOTIFY metadataChanged),
    pub frameCount: qt_property!(i64; NOTIFY metadataChanged),
    pub frameRate:  qt_property!(f64; NOTIFY metadataChanged),
    pub metadataChanged: qt_signal!(),
    pub surfaceSizeChanged: qt_signal!(),

    pub buffering: qt_property!(bool; NOTIFY bufferingChanged),
    pub bufferingChanged: qt_signal!(),
    pub bufferedRanges: qt_property!(QJsonArray; NOTIFY bufferingChanged),

    pub setBufferedRanges: qt_method!(fn(&mut self, v: QJsonArray)),
    pub setBuffering:      qt_method!(fn(&mut self, v: bool)),

    pub metadataLoaded: qt_signal!(md: QJsonObject),

    pub frameRendered: qt_method!(fn(&mut self, timestamp: f64, frame: i32)),
    pub videoLoaded:   qt_method!(fn(&mut self, duration: f64, frameCount: i64, frameRate: f64, width: u32, height: u32)),
    pub stateChanged:  qt_method!(fn(&mut self, state: i32)),

    pub surfaceSizeUpdated: qt_method!(fn(&mut self, width: u32, height: u32)),
    pub setPlaybackRange: qt_method!(fn(&mut self, from_ms: i64, to_ms: i64)),

    m_geometryChanged: bool,

    m_player: MDKPlayerWrapper,

    m_processPixelsCb: Option<ProcessPixelsCb>,
    m_processTextureCb: Option<ProcessTextureCb>,
    m_readyForProcessingCb: Option<ReadyForProcessingCb>,
    m_resizeCb: Option<ResizeCb>
}

impl MDKVideoItem {
    pub fn onProcessPixels(&mut self, cb: ProcessPixelsCb) {
        self.m_processPixelsCb = Some(cb);
    }
    pub fn onProcessTexture(&mut self, cb: ProcessTextureCb) {
        self.m_processTextureCb = Some(cb);
    }
    pub fn readyForProcessing(&mut self, cb: ReadyForProcessingCb) {
        self.m_readyForProcessingCb = Some(cb);
    }
    pub fn onResize(&mut self, cb: ResizeCb) {
        self.m_resizeCb = Some(cb);
    }

    pub fn play (&mut self) { self.m_player.play(); }
    pub fn pause(&mut self) { self.m_player.pause(); }
    pub fn stop (&mut self) { self.m_player.stop(); }

    pub fn setBackgroundColor(&mut self, color: QColor) { self.m_player.set_background_color(color); self.forceRedraw(); }
    pub fn getBackgroundColor(&self) -> QColor { self.m_player.get_background_color() }

    pub fn setFrameRate(&mut self, fps: f64) { self.m_player.set_frame_rate(fps); self.forceRedraw(); }
    pub fn setPlaybackRate(&mut self, rate: f32) { self.m_player.set_playback_rate(rate); self.forceRedraw(); }
    pub fn getPlaybackRate(&self) -> f32 { self.m_player.get_playback_rate() }

    pub fn setCurrentFrame(&mut self, frame: i64)  { self.m_player.seek_to_frame(frame, self.currentFrame, true); self.forceRedraw(); }
    pub fn setTimestamp(&mut self, timestamp: f64) { self.m_player.seek_to_timestamp(timestamp, true); self.forceRedraw(); }

    pub fn seekToFrame(&mut self, frame: i64, exact: bool) { self.m_player.seek_to_frame(frame, self.currentFrame, exact); self.forceRedraw(); }
    pub fn seekToFrameDelta(&mut self, frame_delta: i64) { self.m_player.seek_to_frame_delta(frame_delta); self.forceRedraw(); }
    pub fn seekToTimestamp(&mut self, timestamp: f64, exact: bool) { self.m_player.seek_to_timestamp(timestamp, exact); self.forceRedraw(); }

    pub fn setRotation(&mut self, v: i32) { self.m_player.set_rotation(v); self.forceRedraw(); }
    pub fn getRotation(&self) -> i32 { self.m_player.get_rotation() }

    pub fn setUrl(&mut self, url: QUrl, custom_decoder: QString) {
        let prev_muted = self.getMuted();
        self.playing = false;
        self.playingChanged();
        self.url = url.clone();
        self.m_player.set_url(url, custom_decoder);
        self.setMuted(prev_muted);
        self.forceRedraw();
    }
    pub fn setProperty(&mut self, key: QString, value: QString) {
        self.m_player.set_property(key, value);
    }
    /// This property is applied to MDKPlayer after each time it's (re)created and before loading any media.
    pub fn setDefaultProperty(&mut self, key: QString, value: QString) {
        self.m_player.set_default_property(key, value);
    }

    pub fn setMuted(&mut self, v: bool) { self.m_player.set_muted(v); self.mutedChanged(); }
    pub fn getMuted(&self) -> bool { self.m_player.get_muted() }

    pub fn setVolume(&mut self, v: f32) { self.m_player.set_volume(v); self.volumeChanged(); }
    pub fn getVolume(&self) -> f32 { self.m_player.get_volume() }

    fn frameRendered(&mut self, ts: f64, frame: i32) {
        let nts = ts.max(0.0);

        if nts != self.timestamp || frame as i64 != self.currentFrame {
            self.timestamp = nts;
            self.currentFrame = frame as i64;

            self.timestampChanged();
            self.currentFrameChanged();
        }
    }

    fn videoLoaded(&mut self, duration: f64, frameCount: i64, frameRate: f64, width: u32, height: u32) {
        self.duration     = duration;
        self.frameCount   = frameCount;
        self.frameRate    = (frameRate * 10000.0).round() / 10000.0;
        self.videoWidth   = width;
        self.videoHeight  = height;

        self.metadataChanged();
    }
    fn stateChanged(&mut self, state: i32) {
        self.playing = state == 1;
        self.playingChanged();
    }
    pub fn surfaceSizeUpdated(&mut self, width: u32, height: u32) {
        self.setSurfaceSize(width, height);

        if let Some(ref mut cb) = self.m_resizeCb {
            cb(width, height)
        }
    }

    fn setBuffering(&mut self, v: bool) {
        self.buffering = v;
        self.bufferingChanged();
    }
    fn setBufferedRanges(&mut self, v: QJsonArray) {
        self.bufferedRanges = v;
        self.bufferingChanged();
    }

    fn process_pixels(&mut self, frame: u32, timestamp: f64, width: u32, height: u32, stride: u32, pixels: &mut [u8]) -> (u32, u32, u32, *mut u8) {
        if let Some(ref mut proc) = self.m_processPixelsCb {
            proc(frame, timestamp, width, height, stride, pixels)
        } else {
            (width, height, stride, pixels.as_mut_ptr())
        }
    }
    fn process_texture(&mut self, frame: u32, timestamp: f64, width: u32, height: u32, backend_id: u32, ptr1: u64, ptr2: u64, ptr3: u64, ptr4: u64, ptr5: u64) -> bool {
        if let Some(ref mut proc) = self.m_processTextureCb {
            proc(frame, timestamp, width, height, backend_id, ptr1, ptr2, ptr3, ptr4, ptr5)
        } else {
            false
        }
    }
    fn ready_for_processing(&mut self) -> bool {
        if let Some(ref mut proc) = self.m_readyForProcessingCb {
            proc()
        } else {
            true
        }
    }

    pub fn setSurfaceSize(&mut self, width: u32, height: u32) {
        self.surfaceWidth = width;
        self.surfaceHeight = height;

        self.surfaceSizeChanged();
        self.forceRedraw();
    }

    pub fn setPlaybackRange(&mut self, from_ms: i64, to_ms: i64) {
        self.m_player.set_playback_range(from_ms, to_ms);
    }

    pub fn startProcessing<F: FnMut(i32, f64, u32, u32, u32, u32, f64, f64, u32, &mut [u8]) -> bool + 'static>(&mut self, id: usize, width: usize, height: usize, yuv: bool, custom_decoder: &str, ranges_ms: Vec<(usize, usize)>, cb: F) {
        self.m_player.start_processing(id, width, height, custom_decoder, yuv, ranges_ms, cb);
    }
    pub fn stopProcessing(&mut self, id: usize) {
        self.m_player.stop_processing(id);
    }

    pub fn get_mdkplayer_mut(&mut self) -> &mut MDKPlayerWrapper {
        &mut self.m_player
    }
    pub fn get_mdkplayer(&self) -> &MDKPlayerWrapper {
        &self.m_player
    }

    pub fn forceRedraw(&mut self) { self.m_player.force_redraw(); }

    pub fn setGlobalOption(key: &str, val: &str) { MDKPlayerWrapper::set_global_option(QString::from(key), QString::from(val)); }
    pub fn setLogHandler<F: Fn(i32, &str) + 'static>(cb: F) { MDKPlayerWrapper::set_log_handler(cb); }
}

fn qimage_from_parts(parts: (u32, u32, u32, *mut u8)) -> QImage {
    let (w, h, s, ptr) = parts;
    cpp!(unsafe [w as "uint32_t", h as "uint32_t", s as "uint32_t", ptr as "const uint8_t *"] -> QImage as "QImage" {
        return QImage(ptr, w, h, s, QImage::Format_RGBA8888_Premultiplied);
    })
}

cpp! {{
    QImage processPixelsCb(QQuickItem *item, uint32_t frame, double timestamp, const QImage &img) {
        uint32_t width = img.width();
        uint32_t height = img.height();
        uint32_t stride = img.bytesPerLine();
        const uint8_t *bits = img.constBits();
        uint64_t bitsSize = img.sizeInBytes();
        return rust!(Rust_MDKPlayerItem_processPixels [item: *mut std::os::raw::c_void as "QQuickItem *", frame: u32 as "uint32_t", timestamp: f64 as "double", width: u32 as "uint32_t", height: u32 as "uint32_t", stride: u32 as "uint32_t", bitsSize: u64 as "uint64_t", bits: *mut u8 as "const uint8_t *"] -> QImage as "QImage" {
            let slice = unsafe { std::slice::from_raw_parts_mut(bits, bitsSize as usize) };

            let mut vid_item = MDKVideoItem::get_from_cpp(item);
            let mut vid_item = unsafe { &mut *vid_item.as_ptr() }; // vid_item.borrow_mut()

            qimage_from_parts(vid_item.process_pixels(frame, timestamp, width, height, stride, slice))
        });
    };
    bool processTextureCb(QQuickItem *item, uint32_t frame, double timestamp, uint32_t width, uint32_t height, uint32_t backend_id, uint64_t ptr1, uint64_t ptr2, uint64_t ptr3, uint64_t ptr4, uint64_t ptr5) {
        return rust!(Rust_MDKPlayerItem_processTexture [item: *mut std::os::raw::c_void as "QQuickItem *", frame: u32 as "uint32_t", timestamp: f64 as "double", width: u32 as "uint32_t", height: u32 as "uint32_t", backend_id: u32 as "uint32_t", ptr1: u64 as "uint64_t", ptr2: u64 as "uint64_t", ptr3: u64 as "uint64_t", ptr4: u64 as "uint64_t", ptr5: u64 as "uint64_t"] -> bool as "bool" {
            let mut vid_item = MDKVideoItem::get_from_cpp(item);
            let mut vid_item = unsafe { &mut *vid_item.as_ptr() }; // vid_item.borrow_mut()

            vid_item.process_texture(frame, timestamp, width, height, backend_id, ptr1, ptr2, ptr3, ptr4, ptr5)
        });
    };
    bool readyForProcessingCb(QQuickItem *item) {
        return rust!(Rust_MDKPlayerItem_readyForProcessing [item: *mut std::os::raw::c_void as "QQuickItem *"] -> bool as "bool" {
            let mut vid_item = MDKVideoItem::get_from_cpp(item);
            let mut vid_item = unsafe { &mut *vid_item.as_ptr() }; // vid_item.borrow_mut()

            vid_item.ready_for_processing()
        });
    };
}}

impl QQuickItem for MDKVideoItem {
    fn component_complete(&mut self) {
        let obj = self.get_cpp_object();
        cpp!(unsafe [obj as "QQuickItem *"] {
            obj->setFlag(QQuickItem::ItemHasContents);

            #ifdef Q_OS_ANDROID
                static bool activitySet = false;
                if (!activitySet) {
                    activitySet = true;
                    SetGlobalOption("JavaVM", QJniEnvironment::javaVM());
                    #if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
                        jobject ctx = QJniEnvironment::getJniEnv()->NewGlobalRef(QNativeInterface::QAndroidApplication::context().object());
                    #else
                        jobject ctx = QNativeInterface::QAndroidApplication::context();
                    #endif
                    SetGlobalOption("android.app.Application", ctx);
                    SetGlobalOption("android.content.Context", ctx);
                }
            #endif
        });
    }
    fn release_resources(&mut self) {
        let player = &self.m_player;
        cpp!(unsafe [player as "MDKPlayerWrapper*"] {
            // qDebug() << "release_resources" << player;
            player->mdkplayer->destroyPlayer();
        });
    }
    fn geometry_changed(&mut self, new_geometry: QRectF, old_geometry: QRectF) {
        let obj = self.get_cpp_object();
        cpp!(unsafe [obj as "QQuickItem *"] { obj->setFlag(QQuickItem::ItemHasContents); });

        self.m_geometryChanged = true;
        self.forceRedraw();
        (self as &dyn QQuickItem).update();
    }

    fn update_paint_node(&mut self, mut node: SGNode<ContainerNode>) -> SGNode<ContainerNode> {
        node.update_static(
            |mut n| -> SGNode<QSGImageNode> {
                let image_node = &mut n;
                let item = self.get_cpp_object();
                let player = &self.m_player;
                let (w, h) = (self.surfaceWidth, self.surfaceHeight);

                cpp!(unsafe [image_node as "QSGImageNode**", item as "QQuickItem*", player as "MDKPlayerWrapper*", w as "uint32_t", h as "uint32_t"] {
                    if (!item) return;
                    if (!*image_node) {
                        *image_node = item->window()->createImageNode();
                        player->mdkplayer->setupNode(*image_node, item);
                        player->mdkplayer->setProcessPixelsCallback(processPixelsCb);
                        player->mdkplayer->setProcessTextureCallback(processTextureCb);
                        player->mdkplayer->setReadyForProcessingCallback(readyForProcessingCb);
                    }

                    QSize newSize = QSizeF(item->size() * item->window()->effectiveDevicePixelRatio()).toSize();
                    if (w != 0 && h != 0) {
                        newSize = QSize(w, h);
                    }
                    player->mdkplayer->sync(newSize);
                    (*image_node)->markDirty(QSGImageNode::DirtyMaterial);
                });
                if self.m_geometryChanged {
                    let rect = (self as &dyn QQuickItem).bounding_rect();
                    cpp!(unsafe [image_node as "QSGImageNode**", rect as "QRectF"] {
                        if (*image_node) (*image_node)->setRect(rect);
                    });
                }
                n
            }
        );

        self.m_geometryChanged = false;
        node
    }
}
