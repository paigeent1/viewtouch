import QtQuick
import QtMultimedia

// The customer display's left third between guests: a video (MP4) playing on a loop,
// muted. Only built where Qt Multimedia is installed (the Pi); elsewhere the
// customer display shows the picture instead.
Item {
    id: v
    property url source
    signal failed()
    MediaPlayer {
        id: player
        source: v.source
        loops: MediaPlayer.Infinite
        videoOutput: out
        onErrorOccurred: v.failed()
        Component.onCompleted: play()
    }
    VideoOutput {
        id: out
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
    }
}
