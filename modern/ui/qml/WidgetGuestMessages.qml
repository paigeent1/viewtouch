import QtQuick
import QtQuick.Layouts

// Customer Display page: the store's messages and pictures in turn (Store Settings > Customer
// display: between guests), with running promotions. props.title: the line above each message
// (default "Welcome!"; "" for none); props.seconds: how long each message shows (default 7;
// pictures 10; a video plays to its end, its sound only if Store Settings says so and no check is open).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var prompt: pos ? pos.customerPrompt : ({})
    readonly property var slides: prompt.slides ?? []
    readonly property var props: zone && zone.props ? zone.props : ({})
    readonly property string face: zone && zone.st.font ? zone.st.font : "DejaVu Sans"
    readonly property color ink: zone && zone.st.textColor ? zone.st.textColor : "white"
    readonly property color accent: prompt.accent || "#2f6fd6"
    readonly property real unit: Math.max(12, Math.min(width, height * 1.6) / 27)
    function img(ref) { return pos ? (pos.imageRevision < 0 ? undefined : pos.imageUrl(ref)) : ref }
    // The slides (guestslides.hh): a message; "image:<ref>" a picture; "video:<path>" a video
    // played once to its end, muted; "video:<path>#sound" the same with its sound.
    property int index: 0
    clip: true
    readonly property bool editing: !!(w.zone && w.zone.editing)
    // A check open on the guest screen: a video plays on, without its sound (the turn goes on).
    readonly property bool checkOpen: zone && zone.checkShown !== undefined ? zone.checkShown : !!pos && pos.hasCheck
    // Sound only on the guest's own screen (the Customer Display page), never on the register's.
    readonly property bool guestScreen: !!zone && zone.checkShown !== undefined
    readonly property bool quiet: checkOpen || !w.visible || !guestScreen
    // Videos this screen can't play (no Qt Multimedia: all; else the ones that failed, tried again
    // every 10 minutes) are skipped, so the turn never sticks on one.
    property bool videoOff: false
    property var badVideos: ({})
    function usable(s) { return !s.startsWith("video:") || (!videoOff && !badVideos[s]) }
    // The next slide that can show (the same one when it's the only one).
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

    // Each message props.seconds (7), each picture 10; a video until it ends. Hidden (a check
    // covers it on this page) the turn goes on all the same.
    Timer {
        interval: slide.picture ? 10000 : Math.max(2, w.props.seconds ?? 7) * 1000   // a picture: 10 seconds
        running: w.slides.length > 1 && !w.editing && !slide.video
        repeat: true
        onTriggered: fade.restart()
    }
    // A video that never says it ended: on after 10 minutes anyway.
    Timer {
        id: videoCap
        interval: 10 * 60 * 1000
        running: slide.video && !w.editing
        onTriggered: fade.restart()
    }
    Timer {
        interval: 10 * 60 * 1000
        running: Object.keys(w.badVideos).length > 0
        repeat: true
        onTriggered: w.badVideos = ({})
    }
    SequentialAnimation {
        id: fade
        NumberAnimation { target: slide; property: "opacity"; to: 0; duration: 350 }
        ScriptAction {
            script: {
                w.next()
                videoCap.restart()
                // The same video again (the only one that can show): from the start.
                if (slide.video && videoSlide.item)
                    videoSlide.item.play(slide.videoUrl)
            }
        }
        NumberAnimation { target: slide; property: "opacity"; to: 1; duration: 350 }
    }
    Item {
        id: slide
        anchors.fill: parent
        anchors.margins: w.unit
        readonly property string shown: w.slides.length ? w.slides[w.index % w.slides.length] : ""
        // One that can't show (a video this screen can't play): the store's name instead.
        readonly property string current: w.usable(shown) ? shown : ""
        readonly property bool picture: current.startsWith("image:")
        readonly property bool video: current.startsWith("video:")
        readonly property bool sound: video && current.endsWith("#sound")
        // "file://" and the path, each part spelled for a link (a name with a space or a "?").
        readonly property string videoUrl: !video ? "" : "file://" + current.slice(6, sound ? -6 : undefined).split("/").map(encodeURIComponent).join("/")
        Image {
            anchors.fill: parent
            visible: slide.picture
            fillMode: Image.PreserveAspectFit
            source: !slide.picture ? "" : w.img(slide.current.slice(6))
        }
        // A video: GuestVideoSlide.qml, where this screen has Qt Multimedia (not while editing).
        Loader {
            id: videoSlide
            anchors.fill: parent
            active: slide.video && !w.videoOff && !w.editing
            source: active ? "GuestVideoSlide.qml" : ""
            onStatusChanged: if (status === Loader.Error) { w.videoOff = true; fade.restart() }
            onLoaded: {
                item.sound = Qt.binding(() => slide.sound)
                item.muted = Qt.binding(() => w.quiet)
                item.finished.connect(() => fade.restart())
                item.failed.connect(() => w.videoFailed())
                item.play(slide.videoUrl)
            }
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width
            visible: !slide.picture && !slide.video
            spacing: w.unit
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                horizontalAlignment: Text.AlignHCenter
                text: w.props.title !== undefined ? w.props.title : qsTr("Welcome!")
                color: Qt.lighter(w.accent, 1.5)
                font.family: w.face
                font.pixelSize: w.unit * 1.6
            }
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 1   // wrapped text: its width from the layout, not its own (no recursive rearrange)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: slide.current !== "" ? slide.current : (w.pos ? w.pos.storeName : "")
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit * 2.4
                font.bold: true
            }
        }
    }
}
