#![allow(non_snake_case)]
#![allow(unused_variables)]

use cpp::*;
use qmetaobject::scenegraph::*;
use qmetaobject::*;
use crate::video_player::*;

type ProcessPixelsCb = Box<dyn Fn(u32, u32, u32, &mut [u8]) -> *mut u8>;
type ResizeCb = Box<dyn Fn(u32, u32)>;

pub enum QSGImageNode {}

#[derive(Default, QObject)]
pub struct MDKVideoItem {
    base: qt_base_class!(trait QQuickItem),

    pub play:  qt_method!(fn(&mut self)),
    pub pause: qt_method!(fn(&mut self)),
    pub stop:  qt_method!(fn(&mut self)),

    pub playing: qt_property!(bool; NOTIFY playingChanged),
    pub playingChanged: qt_signal!(),

    pub backgroundColor: qt_property!(QColor; WRITE setBackgroundColor READ getBackgroundColor),
    pub playbackRate: qt_property!(f32; WRITE setPlaybackRate READ getPlaybackRate),

    pub currentFrame:        qt_property!(i64; WRITE setCurrentFrame NOTIFY currentFrameChanged),
    pub setCurrentFrame:     qt_method!(fn(&mut self, frame: i64)),
    pub currentFrameChanged: qt_signal!(),

    pub timestamp:        qt_property!(f64; WRITE setTimestamp NOTIFY timestampChanged),
    pub setTimestamp:     qt_method!(fn(&mut self, timestamp: f64)),
    pub timestampChanged: qt_signal!(),

    pub url:    qt_property!(QUrl; WRITE setUrl),
    pub setUrl: qt_method!(fn(&mut self, url: QUrl)),

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
    
    pub surfaceSizeUpdated: qt_method!(fn(&mut self, width: u32, height: u32)),
    pub setPlaybackRange: qt_method!(fn(&mut self, from_ms: i64, to_ms: i64)),

    m_geometryChanged: bool,

    m_player: MDKPlayer,

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

    pub fn play (&mut self) { self.m_player.play(); self.playing = true; self.playingChanged(); }
    pub fn pause(&mut self) { self.m_player.pause(); self.playing = false; self.playingChanged(); }
    pub fn stop (&mut self) { self.m_player.stop(); self.playing = false; self.playingChanged(); }

    pub fn setBackgroundColor(&mut self, color: QColor) { self.m_player.set_background_color(color); }
    pub fn getBackgroundColor(&self) -> QColor { return self.m_player.get_background_color(); }

    pub fn setPlaybackRate(&mut self, rate: f32) { self.m_player.set_playback_rate(rate); }
    pub fn getPlaybackRate(&self) -> f32 { return self.m_player.get_playback_rate(); }

    pub fn setCurrentFrame(&mut self, frame: i64)  { self.m_player.seek_to_frame(frame, self.currentFrame); }
    pub fn setTimestamp(&mut self, timestamp: f64) { self.m_player.seek_to_timestamp(timestamp); }

    pub fn setUrl(&mut self, url: QUrl) {
        let prev_muted = self.getMuted();
        self.playing = false;
        self.playingChanged();
        self.url = url.clone();
        self.m_player.set_url(url);
        self.setMuted(prev_muted);
    }

    pub fn setMuted(&mut self, v: bool) { self.m_player.set_muted(v); self.mutedChanged(); }
    pub fn getMuted(&self) -> bool { self.m_player.get_muted() }

    fn frameRendered(&mut self, ts: f64) {
        self.timestamp = ts.max(0.0);
        self.currentFrame = ((ts / 1000.0) * self.frameRate).ceil() as i64; // ((self.timestamp / self.duration) * self.frameCount as f64).round().max(0.0) as i64;
        
        self.timestampChanged();
        self.currentFrameChanged();
    }

    fn videoLoaded(&mut self, duration: f64, frameCount: i64, frameRate: f64, width: u32, height: u32) {
        self.duration     = duration;
        self.frameCount   = frameCount;
        self.frameRate    = frameRate;
        self.videoWidth   = width;
        self.videoHeight  = height;
        
        self.metadataChanged();
    }
    pub fn surfaceSizeUpdated(&mut self, width: u32, height: u32) {
        self.setSurfaceSize(width, height);

        if let Some(ref mut cb) = self.m_resizeCb {
            cb(width, height)
        }
    }

    fn process_pixels(&mut self, frame: u32, width: u32, height: u32, pixels: &mut [u8]) -> *mut u8 {
        if let Some(ref mut proc) = self.m_processPixelsCb {
            proc(frame, width, height, pixels)
        } else {
            pixels.as_mut_ptr()
        }
    }

    pub fn setSurfaceSize(&mut self, width: u32, height: u32) {
        self.surfaceWidth = width;
        self.surfaceHeight = height;
        self.metadataChanged();
    }
    
    pub fn setPlaybackRange(&mut self, from_ms: i64, to_ms: i64) {
        self.m_player.set_playback_range(from_ms, to_ms);
    }

    pub fn startProcessing<F: FnMut(i32, u32, u32, &mut [u8]) + 'static>(&mut self, id: usize, width: usize, height: usize, yuv: bool, ranges_ms: Vec<(usize, usize)>, cb: F) {
        self.m_player.start_processing(id, width, height, yuv, ranges_ms, cb);
    }
}

cpp! {{
    uint8_t *processPixelsCb(QQuickItem *item, uint32_t frame, uint32_t width, uint32_t height, const uint8_t *bits, uint64_t bitsSize) {
        return rust!(Rust_MDKPlayerItem_processPixels [item: *mut std::os::raw::c_void as "QQuickItem *", frame: u32 as "uint32_t", width: u32 as "uint32_t", height: u32 as "uint32_t", bitsSize: u64 as "uint64_t", bits: *mut u8 as "const uint8_t *"] -> *mut u8 as "uint8_t *" {
            let slice = unsafe { std::slice::from_raw_parts_mut(bits, bitsSize as usize) };
            
            let mut vid_item = MDKVideoItem::get_from_cpp(item);
            let mut vid_item = unsafe { &mut *vid_item.as_ptr() }; // vid_item.borrow_mut()

            vid_item.process_pixels(frame, width, height, slice)
        });
    };
}}

impl QQuickItem for MDKVideoItem {
    fn component_complete(&mut self) {
        let obj = self.get_cpp_object();
        cpp!(unsafe [obj as "QQuickItem *"] { obj->setFlag(QQuickItem::ItemHasContents); });
    }
    fn release_resources(&mut self) {
        let player = &self.m_player;
        cpp!(unsafe [player as "MDKPlayer*"] {
            qDebug() << "release_resources" << player;
            player->destroyPlayer();
        });
    }
    fn geometry_changed(&mut self, new_geometry: QRectF, old_geometry: QRectF) {
        let obj = self.get_cpp_object();
        cpp!(unsafe [obj as "QQuickItem *"] { obj->setFlag(QQuickItem::ItemHasContents); });

        self.m_geometryChanged = true;
        (self as &dyn QQuickItem).update();
    }

    fn update_paint_node(&mut self, mut node: SGNode<ContainerNode>) -> SGNode<ContainerNode> {
        node.update_static(
            |mut n| -> SGNode<QSGImageNode> {
                let image_node = &mut n;
                let item = self.get_cpp_object();
                let player = &self.m_player;
                let (w, h) = (self.surfaceWidth, self.surfaceHeight);
                
                cpp!(unsafe [image_node as "QSGImageNode**", item as "QQuickItem*", player as "MDKPlayer*", w as "uint32_t", h as "uint32_t"] {
                    if (!item) return;
                    if (!*image_node) {
                        *image_node = item->window()->createImageNode();
                        player->setupNode(*image_node, item, processPixelsCb);
                    }
                    
                    QSize newSize = QSizeF(item->size() * item->window()->effectiveDevicePixelRatio()).toSize();
                    if (w != 0 && h != 0) {
                        newSize = QSize(w, h);
                    }
                    player->sync(newSize);
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