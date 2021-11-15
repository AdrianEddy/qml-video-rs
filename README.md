# qml-video-rs
Video player component for QML using Qt6 and [`mdk-sdk`](https://github.com/wang-bin/mdk-sdk)

This crate uses [`qmetaobject-rs`](https://github.com/woboq/qmetaobject-rs) and supports Qt 6.0 and up. 

It should automatically download and extract the `mdk-sdk` library at build time, it will try to use `7z` so make sure you have that in `PATH`.

# Example usage:
Check out `examples/basic`

This component also supports pixels processing (`video_item::onProcessPixels`) and offscreen video frame dump at max decoding speed (`video_item::startProcessing`). TODO: add examples for both options

