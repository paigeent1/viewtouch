import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// A billboard (ui/cpp/billboard.hh): a TV around the store showing the guest screen's turn of
// messages, pictures and videos, full screen, and nothing else - signage only, never a check or a
// customer. The same timing as the guest screen (WidgetGuestMessages.qml): each message 7 seconds,
// each picture 10, a video to its end (its sound only if its slide has #sound and this billboard
// isn't muted). No slides: the store's logo (or its name). Outside its hours (Store
// Settings > Billboards, by the store's clock): black, nothing drawn, no video. Across the top
// while showing: the store's ribbon (its colour and logo), as on the guest screen.
// The only way out: five taps in the top-left corner, or five presses of the remote's Back or OK
// within 4 seconds, then a manager's PIN (the remote's arrows and OK, or its number keys).
ApplicationWindow {
    id: root
    required property BillboardController billboard
    width: 1280
    height: 720
    visible: true
    title: qsTr("ViewTouch billboard")
    color: "black"

    // One unit: 18 px on a 720p TV, 27 on 1080p.
    readonly property real u: Math.max(12, Math.min(width / 71, height / 40))
    readonly property bool dark: !billboard.showing
    readonly property var slides: billboard.slides
    readonly property color accent: billboard.accent || "#2f6fd6"

    // Identify (Store Settings > Billboards): awake for its 10 seconds even outside its hours.
    readonly property bool asleep: dark && !billboard.identify
    onAsleepChanged: billboard.setDark(asleep)
    Component.onCompleted: billboard.setDark(asleep)
    // A manager's PIN closes the app (main.cpp, BillboardController::closeRequested).

    // The ribbon and the turn of messages, pictures and videos (also the page's Preview).
    BillboardShow {
        id: show
        anchors.fill: parent
        slides: root.slides
        dark: root.dark
        mute: root.billboard.mute
        volume: root.billboard.volume
        accent: root.accent
        storeName: root.billboard.storeName
        revision: root.billboard.revision
        logoUrl: root.billboard.revision < 0 ? "" : root.billboard.logoUrl
        imageFor: ref => root.billboard.imageUrl(ref)
        videoFor: line => root.billboard.videoUrl(line)
    }

    // A small sign in the corner only while the store is out of reach, or a video is arriving.
    Text {
        anchors { right: parent.right; bottom: parent.bottom; margins: root.u }
        visible: !root.dark && (!root.billboard.online || root.billboard.videoProgress >= 0)
        text: !root.billboard.online ? qsTr("Connecting to the store…")
                                     : qsTr("Getting a video: %1%").arg(root.billboard.videoProgress)
        color: "#80ffffff"
        font.pixelSize: root.u * 0.8
    }

    // --- the way out: five taps in the corner (or the remote), then a manager's PIN -------------
    property var presses: []
    function knock() {
        const now = Date.now()
        presses = presses.filter(t => now - t < 4000).concat([now])
        if (presses.length >= 5) {
            presses = []
            pad.open()
        }
    }
    Item {
        anchors { left: parent.left; top: parent.top }
        width: Math.max(96, root.u * 6)
        height: width
        z: 10
        TapHandler { onTapped: root.knock() }
    }
    // The TV's remote: Back and OK count as knocks (and Back never closes the billboard by itself).
    Item {
        id: keys
        anchors.fill: parent
        focus: !pad.visible
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape || event.key === Qt.Key_Return
                    || event.key === Qt.Key_Enter || event.key === Qt.Key_Select || event.key === Qt.Key_Menu) {
                root.knock()
                event.accepted = true
            }
        }
    }

    Rectangle {
        id: pad
        anchors.fill: parent
        z: 20
        visible: false
        color: "#e0101318"
        property string pin: ""
        property int cursor: 0   // the key the remote's arrows are on
        readonly property var labels: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "Cancel", "0", "OK"]
        function open() { pin = ""; cursor = 0; visible = true; padKeys.forceActiveFocus(); padTimeout.restart() }
        function close() { visible = false; pin = ""; keys.forceActiveFocus() }
        function press(label) {
            padTimeout.restart()
            if (label === "Cancel") { close(); return }
            if (label === "OK") { const p = pin; close(); root.billboard.leave(p); return }
            if (pin.length < 8) pin += label
        }
        // Nobody there: back to the slides after 30 seconds.
        Timer { id: padTimeout; interval: 30000; onTriggered: pad.close() }
        Item {
            id: padKeys
            anchors.fill: parent
            Keys.onPressed: (event) => {
                const k = event.key
                if (k >= Qt.Key_0 && k <= Qt.Key_9) pad.press(String(k - Qt.Key_0))
                else if (k === Qt.Key_Left) pad.cursor = (pad.cursor + 11) % 12
                else if (k === Qt.Key_Right) pad.cursor = (pad.cursor + 1) % 12
                else if (k === Qt.Key_Up) pad.cursor = (pad.cursor + 9) % 12
                else if (k === Qt.Key_Down) pad.cursor = (pad.cursor + 3) % 12
                else if (k === Qt.Key_Return || k === Qt.Key_Enter || k === Qt.Key_Select) pad.press(pad.labels[pad.cursor])
                else if (k === Qt.Key_Back || k === Qt.Key_Escape) pad.close()
                else return
                event.accepted = true
            }
        }
        ColumnLayout {
            anchors.centerIn: parent
            spacing: root.u * 0.6
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Manager PIN to close the billboard")
                color: "white"
                font.pixelSize: root.u * 1.3
                font.bold: true
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: pad.pin.length ? "●".repeat(pad.pin.length) : " "
                color: "white"
                font.pixelSize: root.u * 1.6
            }
            GridLayout {
                Layout.alignment: Qt.AlignHCenter
                columns: 3
                rowSpacing: root.u * 0.4
                columnSpacing: root.u * 0.4
                Repeater {
                    model: pad.labels
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        Layout.preferredWidth: Math.max(96, root.u * 6)
                        Layout.preferredHeight: Math.max(64, root.u * 3.6)
                        radius: root.u * 0.4
                        color: modelData === "OK" ? "#1f6b40" : modelData === "Cancel" ? "#6b2a2a" : "#343c49"
                        border.width: pad.cursor === index ? Math.max(3, root.u * 0.25) : 0
                        border.color: "#ffd60a"
                        Text {
                            anchors.centerIn: parent
                            text: modelData === "Cancel" ? qsTr("Cancel") : modelData === "OK" ? qsTr("OK") : modelData
                            color: "white"
                            font.pixelSize: root.u * (modelData.length > 1 ? 1.1 : 1.6)
                            font.bold: true
                        }
                        TapHandler { onTapped: { pad.cursor = index; pad.press(modelData) } }
                    }
                }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: root.billboard.message !== ""
                text: root.billboard.message
                color: "#ff9a9e"
                font.pixelSize: root.u * 0.9
            }
        }
    }

    // Outside its hours: black, nothing drawn (the slides above are hidden too, and no video plays).
    Rectangle {
        anchors.fill: parent
        z: 15
        visible: root.dark && !pad.visible
        color: "black"
    }

    // Identify: which TV this is, big, for the manager looking around the room.
    Rectangle {
        objectName: "billboardIdentify"
        anchors.centerIn: parent
        z: 16
        visible: root.billboard.identify && !pad.visible
        width: Math.min(parent.width - root.u * 2, idName.implicitWidth + root.u * 6)
        height: idName.implicitHeight + root.u * 4
        radius: root.u
        color: "#ffd60a"
        border.color: "black"
        border.width: Math.max(4, root.u * 0.3)
        SequentialAnimation on opacity {
            running: root.billboard.identify
            loops: Animation.Infinite
            NumberAnimation { to: 0.35; duration: 500 }
            NumberAnimation { to: 1; duration: 500 }
        }
        Text {
            id: idName
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - root.u * 6)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.billboard.name
            color: "black"
            font.family: "DejaVu Sans"
            font.pixelSize: root.u * 4
            font.bold: true
        }
    }
}
