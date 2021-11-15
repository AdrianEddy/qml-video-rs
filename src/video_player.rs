use cpp::*;
use qmetaobject::*;

cpp! {{
    struct TraitObject2 { void *data; void *vtable; };
    #include "src/cpp/MDKPlayer.cpp"
}}
cpp_class! { pub unsafe struct MDKPlayer as "MDKPlayer" }

impl MDKPlayer {
    pub fn play (&mut self) { cpp!(unsafe [self as "MDKPlayer *"] { self->play();  }) }
    pub fn pause(&mut self) { cpp!(unsafe [self as "MDKPlayer *"] { self->pause(); }) }
    pub fn stop (&mut self) { cpp!(unsafe [self as "MDKPlayer *"] { self->stop();  }) }

    pub fn seek_to_timestamp(&mut self, timestamp: f64) {
        cpp!(unsafe [self as "MDKPlayer *", timestamp as "double"] {
            self->seekToTimestamp(timestamp);
        })
    }
    pub fn seek_to_frame(&mut self, frame: i64, current_frame: i64) {
        cpp!(unsafe [self as "MDKPlayer *", frame as "int64_t", current_frame as "int64_t"] {
            self->seekToFrame(frame, current_frame);
        })
    }

    pub fn set_url(&mut self, url: QUrl) {
        cpp!(unsafe [self as "MDKPlayer *", url as "QUrl"] {
            self->setUrl(url);
        })
    }

    pub fn set_background_color(&mut self, color: QColor) {
        cpp!(unsafe [self as "MDKPlayer *", color as "QColor"] {
            self->setBackgroundColor(color);
        })
    }
    
    pub fn get_background_color(&self) -> QColor {
        cpp!(unsafe [self as "MDKPlayer *"] -> QColor as "QColor" {
            return self->getBackgroundColor();
        })
    }
    
    pub fn set_playback_rate(&mut self, rate: f32) {
        cpp!(unsafe [self as "MDKPlayer *", rate as "float"] {
            self->setPlaybackRate(rate);
        })
    }
    pub fn get_playback_rate(&self) -> f32 {
        cpp!(unsafe [self as "MDKPlayer *"] -> f32 as "float" {
            return self->playbackRate();
        })
    }

    pub fn set_muted(&mut self, v: bool) {
        cpp!(unsafe [self as "MDKPlayer *", v as "bool"] {
            self->setMuted(v);
        })
    }
    
    pub fn get_muted(&self) -> bool {
        cpp!(unsafe [self as "MDKPlayer *"] -> bool as "bool" {
            return self->getMuted();
        })
    }
    pub fn set_playback_range(&mut self, from_ms: i64, to_ms: i64) {
        cpp!(unsafe [self as "MDKPlayer *", from_ms as "int64_t", to_ms as "int64_t"] {
            self->setPlaybackRange(from_ms, to_ms);
        })
    }

    pub fn start_processing<F: FnMut(i32, u32, u32, &mut [u8]) + 'static>(&mut self, id: usize, width: usize, height: usize, yuv: bool, ranges_ms: Vec<(usize, usize)>, cb: F) {
        
        // assert!(to_ms > from_ms);
        let func: Box<dyn FnMut(i32, u32, u32, &mut [u8])> = Box::new(cb);

        let cb_ptr = Box::into_raw(func);
        let ranges_ptr = ranges_ms.as_ptr();
        let ranges_len = ranges_ms.len();

        cpp!(unsafe [self as "MDKPlayer *", id as "uint64_t", width as "uint64_t", height as "uint64_t", yuv as "bool", ranges_ptr as "std::pair<uint64_t, uint64_t>*", ranges_len as "uint64_t", cb_ptr as "TraitObject2"] {
            std::vector<std::pair<uint64_t, uint64_t>> ranges(ranges_ptr, ranges_ptr + ranges_len);
            self->initProcessingPlayer(id, width, height, yuv, ranges, [cb_ptr](int frame, int width, int height, const uint8_t *bits, uint64_t bitsSize) {
                rust!(Rust_MDKPlayer_videoProcess [cb_ptr: *mut dyn FnMut(i32, u32, u32, &mut [u8]) as "TraitObject2", frame: i32 as "int", width: u32 as "uint32_t", height: u32 as "uint32_t", bitsSize: u64 as "uint64_t", bits: *mut u8 as "const uint8_t *"] {
                    let pixels = unsafe { std::slice::from_raw_parts_mut(bits, bitsSize as usize) };
                    
                    let mut cb = unsafe { Box::from_raw(cb_ptr) };

                    cb(frame, width, height, pixels);
                    if frame >= 0 {
                        let _ = Box::into_raw(cb); // leak again so it doesn't get deleted here
                    }
                });
            });
        })
    }
}
