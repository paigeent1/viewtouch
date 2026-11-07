import QtQuick
import QtMultimedia

// One video in the guest screen's turn (CustomerDisplay.qml, WidgetGuestMessages.qml): play(url)
// plays it once from the start, fitted whole; finished() at its end, failed() when it can't play
// (no such file, a kind this Pi can't play, nothing playing 20 s after it started). Its sound only
// when sound is on (the slide's "#sound") and muted is off (muted while a check is open or the
// turn is hidden: it keeps playing then, without sound). Only built where Qt Multimedia is
// installed (CMakeLists.txt); elsewhere its Loader fails and the video slides are skipped.
Item {
    id: v
    property bool sound: false
    property bool muted: false
    // 0-1 (a billboard's Volume, Store Settings > Billboards); the guest screen: full.
    property real volume: 1.0
    signal finished()
    signal failed()
    function play(url) {
        // Already playing it from the start (the Loader's onLoaded and the turn both ask): once.
        if (String(player.source) === String(url) && player.playbackState === MediaPlayer.PlayingState
                && player.position < 1500)
            return
        player.stop()
        player.source = url
        player.play()
        started.restart()
    }
    MediaPlayer {
        id: player
        loops: 1
        videoOutput: out
        audioOutput: AudioOutput {
            muted: !v.sound || v.muted
            volume: v.volume
        }
        onErrorOccurred: v.failed()
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia)
                v.finished()
            else if (mediaStatus === MediaPlayer.InvalidMedia)
                v.failed()
        }
    }
    // Asked to play and still nothing after 20 seconds: not this one.
    Timer {
        id: started
        interval: 20000
        onTriggered: if (player.playbackState !== MediaPlayer.PlayingState || player.position <= 0) v.failed()
    }
    VideoOutput {
        id: out
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }
}
