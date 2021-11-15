use qmetaobject::*;

pub mod video_player;
pub mod video_item;

pub fn register_qml_types() {
    qml_register_type::<video_item::MDKVideoItem>(cstr::cstr!("MDKVideo"), 1, 0, cstr::cstr!("MDKVideo"));
}
