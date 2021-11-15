use qmetaobject::*;

qrc!(my_resource, "/" { "src/main.qml" } );

fn main() {
    my_resource();
    qml_video_rs::register_qml_types();

    let mut engine = QmlEngine::new();
    engine.load_file("qrc:/src/main.qml".into());
    engine.exec();
}
