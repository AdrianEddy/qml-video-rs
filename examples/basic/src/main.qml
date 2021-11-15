import QtQuick
import QtQuick.Controls
import MDKVideo

ApplicationWindow {
    width: 1000;
    height: 660;
    visible: true;

    Column {
        anchors.fill: parent;
        anchors.margins: 20;
        spacing: 20;

        MDKVideo {
            id: vid;
            url: "file:///C:/test.mp4";
            width: 960;
            height: 540;
            onTimestampChanged: {
                if (!slider.pressed) {
                    slider.preventChange = true;
                    slider.value = timestamp;
                    slider.preventChange = false;
                }
            }
            backgroundColor: "black";
            onDurationChanged: slider.to = duration;
            onMetadataLoaded: (metadata) => console.log(JSON.stringify(metadata));
        }

        Row {
            spacing: 10;
            Button { text: " Play "; onClicked: vid.play(); }
            Button { text: " Pause "; onClicked: vid.pause(); }
            Button { text: " < "; onClicked: vid.currentFrame--; }
            Button { text: " > "; onClicked: vid.currentFrame++; }
            Button { text: " + "; onClicked: vid.playbackRate += 0.25; }
            Button { text: " - "; onClicked: vid.playbackRate -= 0.25; }
            Text { text: (vid.currentFrame + 1) + "/" + vid.frameCount + " @ " + vid.frameRate.toFixed(3) + " / " + vid.playbackRate.toFixed(2); }
        }

        Slider {
            id: slider;
            width: parent.width;
            property bool preventChange: false;
            onValueChanged: {
                if (!preventChange) {
                    vid.timestamp = value;
                }
            }
        }
    }
}