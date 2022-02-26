#![allow(non_snake_case)]
#![allow(unused_variables)]

use cpp::*;
use qmetaobject::scenegraph::*;
use qmetaobject::*;
use crate::video_player::*;

type ProcessPixelsCb = Box<dyn Fn(u32, f64, u32, u32, u32, &mut [u8]) -> (u32, u32, u32, *mut u8)>;
type ResizeCb = Box<dyn Fn(u32, u32)>;

pub enum QSGImageNode {}

cpp! {{
    #include <private/qquickshadereffect_p.h>
    #ifdef Q_OS_ANDROID
    #   include <QJniEnvironment>
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
    pub rotation: qt_property!(i32; WRITE setRotation READ getRotation),
    pub playbackRate: qt_property!(f32; WRITE setPlaybackRate READ getPlaybackRate),

    pub currentFrame:        qt_property!(i64; WRITE setCurrentFrame NOTIFY currentFrameChanged),
    pub setCurrentFrame:     qt_method!(fn(&mut self, frame: i64)),
    pub currentFrameChanged: qt_signal!(),

    pub timestamp:        qt_property!(f64; WRITE setTimestamp NOTIFY timestampChanged),
    pub setTimestamp:     qt_method!(fn(&mut self, timestamp: f64)),
    pub timestampChanged: qt_signal!(),

    pub setFrameRate: qt_method!(fn(&mut self, fps: f64)),

    pub url:    qt_property!(QUrl; WRITE setUrl),
    pub setUrl: qt_method!(fn(&mut self, url: QUrl)),

    pub forceRedraw: qt_method!(fn(&mut self)),

    pub muted: qt_property!(bool; READ getMuted WRITE setMuted NOTIFY mutedChanged),
    pub mutedChanged: qt_signal!(),

    pub videoWidth: qt_property!(u32; NOTIFY metadataChanged),
    pub videoHeight: qt_property!(u32; NOTIFY metadataChanged),

    pub surfaceWidth: qt_property!(u32; NOTIFY metadataChanged),
    pub surfaceHeight: qt_property!(u32; NOTIFY metadataChanged),

    pub duration:   qt_property!(f64; NOTIFY metadataChanged),
    pub frameCount: qt_property!(i64; NOTIFY metadataChanged),
    pub frameRate:  qt_property!(f64; NOTIFY metadataChanged),
    pub metadataChanged: qt_signal!(),

    pub metadataLoaded: qt_signal!(md: QJsonObject),

    pub frameRendered: qt_method!(fn(&mut self, timestamp: f64)),
    pub videoLoaded:   qt_method!(fn(&mut self, duration: f64, frameCount: i64, frameRate: f64, width: u32, height: u32)),
    pub stateChanged:  qt_method!(fn(&mut self, state: i32)),
    
    pub surfaceSizeUpdated: qt_method!(fn(&mut self, width: u32, height: u32)),
    pub setPlaybackRange: qt_method!(fn(&mut self, from_ms: i64, to_ms: i64)),

    m_geometryChanged: bool,

    m_player: MDKPlayerWrapper,

    m_processPixelsCb: Option<ProcessPixelsCb>,
    m_resizeCb: Option<ResizeCb>
}

impl MDKVideoItem {
    pub fn onProcessPixels(&mut self, cb: ProcessPixelsCb) {
        self.m_processPixelsCb = Some(cb);
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

    pub fn setCurrentFrame(&mut self, frame: i64)  { self.m_player.seek_to_frame(frame, self.currentFrame); self.forceRedraw(); }
    pub fn setTimestamp(&mut self, timestamp: f64) { self.m_player.seek_to_timestamp(timestamp); self.forceRedraw(); }

    pub fn setRotation(&mut self, v: i32) { self.m_player.set_rotation(v); self.forceRedraw(); }
    pub fn getRotation(&self) -> i32 { self.m_player.get_rotation() }

    pub fn setUrl(&mut self, url: QUrl) {
        let prev_muted = self.getMuted();
        self.playing = false;
        self.playingChanged();
        self.url = url.clone();
        self.m_player.set_url(url);
        self.setMuted(prev_muted);
        self.forceRedraw();
    }

    pub fn setMuted(&mut self, v: bool) { self.m_player.set_muted(v); self.mutedChanged(); }
    pub fn getMuted(&self) -> bool { self.m_player.get_muted() }

    fn frameRendered(&mut self, ts: f64) {
        let nts = ts.max(0.0);
        let ncf = ((nts / 1000.0) * self.frameRate).ceil() as i64; // ((self.timestamp / self.duration) * self.frameCount as f64).round().max(0.0) as i64;

        if nts != self.timestamp || ncf != self.currentFrame {
            self.timestamp = nts;
            self.currentFrame = ncf;

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

    fn process_pixels(&mut self, frame: u32, timestamp: f64, width: u32, height: u32, stride: u32, pixels: &mut [u8]) -> (u32, u32, u32, *mut u8) {
        if let Some(ref mut proc) = self.m_processPixelsCb {
            proc(frame, timestamp, width, height, stride, pixels)
        } else {
            (width, height, stride, pixels.as_mut_ptr())
        }
    }

    pub fn setSurfaceSize(&mut self, width: u32, height: u32) {
        self.surfaceWidth = width;
        self.surfaceHeight = height;
        self.metadataChanged();
        self.forceRedraw();
    }
    
    pub fn setPlaybackRange(&mut self, from_ms: i64, to_ms: i64) {
        self.m_player.set_playback_range(from_ms, to_ms);
    }

    pub fn startProcessing<F: FnMut(i32, f64, u32, u32, &mut [u8]) + 'static>(&mut self, id: usize, width: usize, height: usize, yuv: bool, ranges_ms: Vec<(usize, usize)>, cb: F) {
        self.m_player.start_processing(id, width, height, yuv, ranges_ms, cb);
    }

    pub fn get_mdkplayer(&mut self) -> &mut MDKPlayerWrapper {
        &mut self.m_player
    }

    pub fn forceRedraw(&mut self) { self.m_player.force_redraw(); }

    pub fn setGlobalOption(key: &str, val: &str) { MDKPlayerWrapper::set_global_option(QString::from(key), QString::from(val)); }
    pub fn setLogHandler<F: Fn(i32, String) + 'static>(cb: F) { MDKPlayerWrapper::set_log_handler(cb); }
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
}}

impl QQuickItem for MDKVideoItem {
    fn component_complete(&mut self) {
        let obj = self.get_cpp_object();
        cpp!(unsafe [obj as "QQuickItem *"] {
            obj->setFlag(QQuickItem::ItemHasContents);

            #ifdef Q_OS_ANDROID
                SetGlobalOption("jvm", QJniEnvironment::javaVM());
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
                        player->mdkplayer->setupNode(*image_node, item, processPixelsCb);
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
