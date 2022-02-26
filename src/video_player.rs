use cpp::*;
use qmetaobject::*;

cpp! {{
    struct TraitObject2 { void *data; void *vtable; };
    #include "src/cpp/VideoTextureNode.h"
    #include "src/cpp/VideoTextureNode.cpp"
    #include "src/cpp/MDKPlayer.h"
    #include "src/cpp/MDKPlayer.cpp"
}}
cpp_class! { pub unsafe struct MDKPlayerWrapper as "MDKPlayerWrapper" }

impl MDKPlayerWrapper {
    pub fn play (&mut self) { cpp!(unsafe [self as "MDKPlayerWrapper *"] { self->mdkplayer->play();  }) }
    pub fn pause(&mut self) { cpp!(unsafe [self as "MDKPlayerWrapper *"] { self->mdkplayer->pause(); }) }
    pub fn stop (&mut self) { cpp!(unsafe [self as "MDKPlayerWrapper *"] { self->mdkplayer->stop();  }) }

    pub fn force_redraw(&mut self) {
        cpp!(unsafe [self as "MDKPlayerWrapper *"] {
            self->mdkplayer->forceRedraw();
        })
    }
    pub fn set_frame_rate(&mut self, fps: f64) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", fps as "double"] {
            self->mdkplayer->setFrameRate(fps);
        })
    }
    
    pub fn seek_to_timestamp(&mut self, timestamp: f64) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", timestamp as "double"] {
            self->mdkplayer->seekToTimestamp(timestamp);
        })
    }
    pub fn seek_to_frame(&mut self, frame: i64, current_frame: i64) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", frame as "int64_t", current_frame as "int64_t"] {
            self->mdkplayer->seekToFrame(frame, current_frame);
        })
    }

    pub fn set_url(&mut self, url: QUrl) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", url as "QUrl"] {
            self->mdkplayer->setUrl(url);
        })
    }

    pub fn set_background_color(&mut self, color: QColor) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", color as "QColor"] {
            self->mdkplayer->setBackgroundColor(color);
        })
    }
    
    pub fn get_background_color(&self) -> QColor {
        cpp!(unsafe [self as "MDKPlayerWrapper *"] -> QColor as "QColor" {
            return self->mdkplayer->getBackgroundColor();
        })
    }
    
    pub fn set_playback_rate(&mut self, rate: f32) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", rate as "float"] {
            self->mdkplayer->setPlaybackRate(rate);
        })
    }
    pub fn get_playback_rate(&self) -> f32 {
        cpp!(unsafe [self as "MDKPlayerWrapper *"] -> f32 as "float" {
            return self->mdkplayer->playbackRate();
        })
    }

    pub fn set_muted(&mut self, v: bool) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", v as "bool"] {
            self->mdkplayer->setMuted(v);
        })
    }
    
    pub fn set_rotation(&self, v: i32) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", v as "int"] {
            return self->mdkplayer->setRotation(v);
        })
    }
    pub fn get_rotation(&self) -> i32 {
        cpp!(unsafe [self as "MDKPlayerWrapper *"] -> i32 as "int" {
            return self->mdkplayer->getRotation();
        })
    }

    pub fn get_muted(&self) -> bool {
        cpp!(unsafe [self as "MDKPlayerWrapper *"] -> bool as "bool" {
            return self->mdkplayer->getMuted();
        })
    }
    pub fn set_playback_range(&mut self, from_ms: i64, to_ms: i64) {
        cpp!(unsafe [self as "MDKPlayerWrapper *", from_ms as "int64_t", to_ms as "int64_t"] {
            self->mdkplayer->setPlaybackRange(from_ms, to_ms);
        })
    }

    pub fn set_global_option(key: QString, val: QString) {
        cpp!(unsafe [key as "QString", val as "QString"] {
            SetGlobalOption(qUtf8Printable(key), qUtf8Printable(val));
        })
    }

    pub fn set_log_handler<F: Fn(i32, String) + 'static>(cb: F) {
        let func: Box<dyn Fn(i32, String)> = Box::new(cb);
        let cb_ptr = Box::into_raw(func);

        #[cfg(target_os = "android")]
        type TextPtr = *const u8;
        #[cfg(not(target_os = "android"))]
        type TextPtr = *mut i8;

        cpp!(unsafe [cb_ptr as "TraitObject2"] {
            setLogHandler([cb_ptr](LogLevel level, const char *text) {
                rust!(Rust_MDKPlayer_logHandler [cb_ptr: *mut dyn FnMut(i32, String) as "TraitObject2", level: i32 as "int", text: TextPtr as "const char *"] {
                    let text = unsafe { std::ffi::CStr::from_ptr(text) }.to_string_lossy().to_string();
                    
                    let mut cb = unsafe { Box::from_raw(cb_ptr) };

                    cb(level, text);
                    let _ = Box::into_raw(cb); // leak again so it doesn't get deleted here
                });
            });
        })
    }

    pub fn start_processing<F: FnMut(i32, f64, u32, u32, &mut [u8]) + 'static>(&mut self, id: usize, width: usize, height: usize, yuv: bool, ranges_ms: Vec<(usize, usize)>, cb: F) {
        
        // assert!(to_ms > from_ms);
        let func: Box<dyn FnMut(i32, f64, u32, u32, &mut [u8])> = Box::new(cb);

        let cb_ptr = Box::into_raw(func);
        let ranges_ptr = ranges_ms.as_ptr();
        let ranges_len = ranges_ms.len();

        cpp!(unsafe [self as "MDKPlayerWrapper *", id as "uint64_t", width as "uint64_t", height as "uint64_t", yuv as "bool", ranges_ptr as "std::pair<uint64_t, uint64_t>*", ranges_len as "uint64_t", cb_ptr as "TraitObject2"] {
            std::vector<std::pair<uint64_t, uint64_t>> ranges(ranges_ptr, ranges_ptr + ranges_len);
            self->mdkplayer->initProcessingPlayer(id, width, height, yuv, ranges, [cb_ptr](int frame, double timestamp, int width, int height, const uint8_t *bits, uint64_t bitsSize) {
                rust!(Rust_MDKPlayer_videoProcess [cb_ptr: *mut dyn FnMut(i32, f64, u32, u32, &mut [u8]) as "TraitObject2", frame: i32 as "int", timestamp: f64 as "double", width: u32 as "uint32_t", height: u32 as "uint32_t", bitsSize: u64 as "uint64_t", bits: *mut u8 as "const uint8_t *"] {
                    let pixels = unsafe { std::slice::from_raw_parts_mut(bits, bitsSize as usize) };
                    
                    let mut cb = unsafe { Box::from_raw(cb_ptr) };

                    cb(frame, timestamp, width, height, pixels);
                    if frame >= 0 {
                        let _ = Box::into_raw(cb); // leak again so it doesn't get deleted here
                    }
                });
            });
        })
    }
}
