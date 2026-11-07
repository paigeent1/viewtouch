import QtQuick
import QtQuick.Layouts

// What a billboard shows (Billboard.qml on the TV; Store Settings > Billboards' Preview): the store's
// ribbon across the top (its colour and logo, or its name), then its messages, pictures and videos
// in turn - each message 7 seconds, each picture 10, a video to its end (its sound only if its line
// has #sound and it isn't muted). No slides: the logo (or the store's name). Dark: nothing drawn,
// no video. The same timing as the guest screen (WidgetGuestMessages.qml).
Item {
    id: show
    property var slides: []
    property string logoUrl: ""
    property string storeName: ""
    property color accent: "#2f6fd6"
    property bool dark: false
    property bool mute: false
    property real volume: 1.0
    // A picture's URL for its ref ("store:sale.jpg"), and a video line's ("video:<path>[#sound]"):
    // "" while it isn't here yet.
    property var imageFor: function (ref) { return "" }
    property var videoFor: function (line) { return "" }
    // Changes when a picture or video has arrived: the pictures and videos are looked at again.
    property int revision: 0
    // One unit: 18 px on a 720p TV, 27 on 1080p.
    readonly property real u: Math.max(6, Math.min(width / 71, height / 40))

    property int index: 0
    property bool videoOff: false      // this build can't play video: every video is skipped
    property var badVideos: ({})       // videos that failed here, tried again every 10 minutes
    function videoUrl(s) { return show.revision < 0 ? "" : (show.videoFor(s) ?? "") }
    // A video shows once it is on this device (a billboard gets it from the store a part at a time).
    function usable(s) { return !s.startsWith("video:") || (!videoOff && !badVideos[s] && videoUrl(s) !== "") }
    function next() {
        const n = slides.length
        for (let k = 1; k <= n; ++k) {
            const j = (index + k) % n
            if (usable(slides[j])) { index = j; return }
        }
    }
    function videoFailed() {
        const bad = Object.assign({}, badVideos)
        bad[slide.current] = true
        badVideos = bad
        fade.restart()
    }
    onSlidesChanged: if (index >= slides.length) index = 0
    Timer {
        interval: slide.picture ? 10000 : 7000
        running: !show.dark && show.slides.length > 1 && !slide.video
        repeat: true
        onTriggered: fade.restart()
    }
    // The only slide was a video still on its way: look again now and then.
    Timer {
        interval: 5000
        running: !show.dark && slide.current === "" && show.slides.length > 0
        repeat: true
        onTriggered: show.next()
    }
    Timer {
        id: videoCap
        interval: 10 * 60 * 1000
        running: !show.dark && slide.video
        onTriggered: fade.restart()
    }
    Timer {
        interval: 10 * 60 * 1000
        running: Object.keys(show.badVideos).length > 0
        repeat: true
        onTriggered: show.badVideos = ({})
    }
    SequentialAnimation {
        id: fade
        NumberAnimation { target: slide; property: "opacity"; to: 0; duration: 350 }
        ScriptAction {
            script: {
                show.next()
                videoCap.restart()
                if (slide.video && videoSlide.item)
                    videoSlide.item.play(slide.videoUrl)
            }
        }
        NumberAnimation { target: slide; property: "opacity"; to: 1; duration: 350 }
    }

    // The store's ribbon across the top, as on the guest screen: its colour, its logo (or its name).
    Rectangle {
        id: ribbon
        visible: !show.dark
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: show.u * 3.4
        color: show.accent
        Image {
            id: ribbonLogo
            visible: source != ""
            anchors { left: parent.left; leftMargin: show.u; verticalCenter: parent.verticalCenter }
            height: parent.height * 0.75
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            source: show.logoUrl
        }
        Text {
            visible: !ribbonLogo.visible
            anchors { left: parent.left; leftMargin: show.u; verticalCenter: parent.verticalCenter }
            text: show.storeName
            color: "white"
            font.family: "DejaVu Sans"
            font.pixelSize: show.u * 1.6
            font.bold: true
        }
    }

    Item {
        id: slide
        anchors { left: parent.left; right: parent.right; top: ribbon.bottom; bottom: parent.bottom }
        visible: !show.dark
        readonly property string shown: show.slides.length ? show.slides[show.index % show.slides.length] : ""
        readonly property string current: shown !== "" && show.usable(shown) ? shown : ""
        readonly property bool picture: current.startsWith("image:")
        readonly property bool video: current.startsWith("video:")
        readonly property bool sound: video && current.endsWith("#sound") && !show.mute
        readonly property string videoUrl: video ? show.videoUrl(current) : ""
        readonly property bool message: current !== "" && !picture && !video

        Image {
            anchors.fill: parent
            visible: slide.picture
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            source: slide.picture && show.revision >= 0 ? (show.imageFor(slide.current.slice(6)) ?? "") : ""
        }
        Loader {
            id: videoSlide
            anchors.fill: parent
            active: !show.dark && slide.video && !show.videoOff
            source: active ? "GuestVideoSlide.qml" : ""
            onStatusChanged: if (status === Loader.Error) { show.videoOff = true; fade.restart() }
            onLoaded: {
                item.sound = Qt.binding(() => slide.sound)
                item.muted = Qt.binding(() => show.mute)
                item.volume = Qt.binding(() => show.volume)
                item.finished.connect(() => fade.restart())
                item.failed.connect(() => show.videoFailed())
                item.play(slide.videoUrl)
            }
        }
        // A message: big, centred, in the store's colour.
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - show.u * 6
            visible: slide.message
            spacing: show.u
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: slide.message ? slide.current : ""
                color: "white"
                font.family: "DejaVu Sans"
                font.pixelSize: show.u * 3.4
                font.bold: true
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: show.u * 8
                Layout.preferredHeight: Math.max(3, show.u * 0.3)
                radius: height / 2
                color: show.accent
            }
        }
        // Nothing to show yet (no slides, or only a video on its way): the logo, or the store's name.
        Image {
            id: logo
            anchors.centerIn: parent
            width: parent.width * 0.6
            height: parent.height * 0.6
            visible: slide.current === "" && source != ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            source: show.logoUrl
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - show.u * 6
            visible: slide.current === "" && !logo.visible
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: show.storeName
            color: "white"
            font.family: "DejaVu Sans"
            font.pixelSize: show.u * 3.4
            font.bold: true
        }
    }
}
