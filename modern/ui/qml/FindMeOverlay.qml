import QtQuick
import QtQuick.Controls.Fusion

// Find a Tablet: another screen is looking for this one. Over everything, flashing, with loud beeps
// (LayoutController::findMeAlarm), until it's tapped here (the other screen is told), Stop is
// touched there, or the time is up (about 2 minutes).
Rectangle {
    id: o
    property var controller: null
    readonly property var pos: controller ? controller.pos : null
    property string from: ""
    property string by: ""
    property bool flash: false
    visible: false
    z: 10000
    color: flash ? "#c62828" : "#14171c"

    function start(info) {
        from = info.from ?? ""
        by = info.by ?? ""
        const secs = Math.max(5, Math.min(600, info.seconds ?? 120))
        ends.interval = secs * 1000
        ends.restart()
        visible = true
        if (controller)
            controller.findMeAlarm(true, secs)
    }
    function stop(found) {
        if (!visible)
            return
        visible = false
        ends.stop()
        if (controller)
            controller.findMeAlarm(false)
        if (found && pos)
            pos.foundTablet()
    }

    Connections {
        target: o.pos
        ignoreUnknownSignals: true
        function onFindMe(info) {
            if (info.on)
                o.start(info)
            else
                o.stop(false)
        }
    }
    Timer { id: ends; repeat: false; onTriggered: o.stop(false) }
    Timer { running: o.visible; interval: 500; repeat: true; onTriggered: o.flash = !o.flash }

    Column {
        anchors.centerIn: parent
        width: parent.width * 0.9
        spacing: Math.max(12, o.height * 0.03)
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "white"
            font.bold: true
            font.pixelSize: Math.max(40, Math.min(o.width * 0.09, o.height * 0.14))
            text: qsTr("Here I am — tap to stop")
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: Math.max(20, Math.min(o.width * 0.035, o.height * 0.05))
            text: (o.by !== "" ? qsTr("%1 on %2 is looking for this screen.").arg(o.by).arg(o.from)
                               : qsTr("%1 is looking for this screen.").arg(o.from))
        }
    }
    // The whole screen stops it (and nothing under it is touched).
    MouseArea {
        anchors.fill: parent
        onClicked: o.stop(true)
    }
}
