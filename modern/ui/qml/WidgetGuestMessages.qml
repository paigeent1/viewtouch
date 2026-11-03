import QtQuick
import QtQuick.Layouts

// Customer Display page: the store's messages and pictures in turn (Store Settings > Customer
// display: between guests), with running promotions. props.title: the line above each message
// (default "Welcome!"; "" for none); props.seconds: how long each shows (default 7).
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
    property int index: 0
    clip: true

    Timer {
        interval: Math.max(2, w.props.seconds ?? 7) * 1000
        running: w.visible && w.slides.length > 1 && !(w.zone && w.zone.editing)
        repeat: true
        onTriggered: fade.restart()
    }
    SequentialAnimation {
        id: fade
        NumberAnimation { target: slide; property: "opacity"; to: 0; duration: 350 }
        ScriptAction { script: w.index = (w.index + 1) % Math.max(1, w.slides.length) }
        NumberAnimation { target: slide; property: "opacity"; to: 1; duration: 350 }
    }
    Item {
        id: slide
        anchors.fill: parent
        anchors.margins: w.unit
        readonly property string current: w.slides.length ? w.slides[w.index % w.slides.length] : ""
        readonly property bool picture: current.startsWith("image:")
        Image {
            anchors.fill: parent
            visible: slide.picture
            fillMode: Image.PreserveAspectFit
            source: !slide.picture ? "" : w.img(slide.current.slice(6))
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width
            visible: !slide.picture
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
