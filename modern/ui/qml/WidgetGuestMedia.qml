import QtQuick

// Customer Display page: a video (MP4, where this screen can play one), a picture or an
// animated GIF. props.image / props.video: this zone's own; else Store Settings > Customer
// display (left side between guests / video there instead); with neither, the store's logo.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var prompt: pos ? pos.customerPrompt : ({})
    readonly property var props: zone && zone.props ? zone.props : ({})
    readonly property string picture: props.image || prompt.panel || prompt.logo || ""
    readonly property string video: props.video || prompt.video || ""
    property bool videoFailed: false
    onVideoChanged: videoFailed = false
    function img(ref) { return pos ? (pos.imageRevision < 0 ? undefined : pos.imageUrl(ref)) : ref }
    // props.scale: the picture this percent of the box, centered, always whole (made smaller: not cut).
    readonly property real scale: Math.max(0.1, Math.min(1, (props.scale ?? 100) / 100))
    // Fill the box (edges cut off) unless props.fit is "fit" or it's made smaller.
    readonly property bool crop: props.fit !== "fit" && scale >= 1
    clip: true

    Loader {
        id: player
        anchors.fill: parent
        active: w.visible && w.video !== "" && !w.videoFailed && !(w.zone && w.zone.editing)
        source: active ? "VideoPanel.qml" : ""
        onStatusChanged: if (status === Loader.Error) w.videoFailed = true
        onLoaded: {
            item.source = w.video.startsWith("/") ? "file://" + w.video : w.video
            item.failed.connect(() => w.videoFailed = true)
        }
    }
    AnimatedImage {
        anchors.centerIn: parent
        width: parent.width * w.scale
        height: parent.height * w.scale
        visible: player.status !== Loader.Ready && w.picture !== ""
        fillMode: w.crop ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        playing: visible
        source: visible ? w.img(w.picture) : ""
    }
}
