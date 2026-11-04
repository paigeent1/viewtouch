import QtQuick
import QtQuick.Layouts

// ID check: a tobacco or alcohol item waits until the buyer's ID is scanned (the barcode on the back of
// the license). An ID that won't scan: its date of birth typed by hand, the exception (logged). On the
// POS and on the tablets (a 2D scanner paired to the tablet types the license in).
Rectangle {
    id: ov
    anchors.fill: parent
    z: 93
    color: "#cc0b0f17"
    property var controller: null
    readonly property var pos: controller ? controller.pos : null
    readonly property var ask: pos ? pos.ageAsk : ({})
    readonly property bool asking: (ask.age ?? 0) > 0
    visible: asking
    property bool typing: false
    readonly property bool android: Qt.platform.os === "android"
    property string dob: ""            // MMDDYYYY as typed
    onAskingChanged: { typing = false; dob = "" }
    readonly property string shown: {
        const d = dob + "________".slice(dob.length)
        return d.slice(0, 2) + " / " + d.slice(2, 4) + " / " + d.slice(4)
    }
    MouseArea { anchors.fill: parent }   // nothing under it takes the touch

    Rectangle {
        objectName: "ageCheck"
        width: Math.min(parent.width * 0.92, 620)
        height: col.implicitHeight + 48
        anchors.centerIn: parent
        radius: 16
        color: "#1e2430"
        border.color: "#f0b429"
        border.width: 3
        ColumnLayout {
            id: col
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 24
            spacing: 14
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("ID check: %1+").arg(ov.ask.age ?? 21)
                color: "#f0b429"; font.pixelSize: 30; font.bold: true
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: ov.ask.name ?? ""
                color: "white"; font.pixelSize: 20
            }
            Text {
                Layout.fillWidth: true
                visible: !ov.typing
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: ov.android ? qsTr("Scan the back of the ID: the camera, or a Bluetooth scanner.\nLook at the photo: is it them?")
                                 : qsTr("Scan the barcode on the back of the ID.\nLook at the photo: is it them?")
                color: "#d9dee7"; font.pixelSize: 22
            }
            // --- an ID that won't scan: the date of birth by hand ---
            ColumnLayout {
                Layout.fillWidth: true
                visible: ov.typing
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Date of birth from the ID (month / day / year)")
                    color: "#d9dee7"; font.pixelSize: 17
                }
                Text {
                    objectName: "ageDob"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: ov.shown
                    color: "white"; font.pixelSize: 34; font.bold: true; font.family: "DejaVu Sans Mono"
                }
                GridLayout {
                    Layout.alignment: Qt.AlignHCenter
                    columns: 3
                    rowSpacing: 8; columnSpacing: 8
                    Repeater {
                        model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "OK"]
                        delegate: Rectangle {
                            id: key
                            required property string modelData
                            implicitWidth: 120; implicitHeight: 62
                            radius: 10
                            color: k.pressed ? "#4c8dff" : modelData === "OK" ? (ov.dob.length === 8 ? "#1f8a4c" : "#2a313d") : "#343c49"
                            Text { anchors.centerIn: parent; text: key.modelData; color: "white"; font.pixelSize: 24; font.bold: true }
                            TapHandler {
                                id: k
                                onTapped: {
                                    if (key.modelData === "⌫") ov.dob = ov.dob.slice(0, -1)
                                    else if (key.modelData === "OK") { if (ov.dob.length === 8) ov.pos.verifyAgeHand(ov.dob) }
                                    else if (ov.dob.length < 8) ov.dob += key.modelData
                                }
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: (ov.ask.handNeedsManager ?? false) ? qsTr("Typed by hand: logged, and a manager's PIN is needed.")
                                                             : qsTr("Typed by hand: logged as an exception.")
                    color: "#f0b429"; font.pixelSize: 15
                }
            }
            // A tablet: its camera reads the license (a paired Bluetooth 2D scanner works too).
            Rectangle {
                objectName: "ageCameraScan"
                Layout.fillWidth: true
                visible: ov.android && !ov.typing
                implicitHeight: 72; radius: 12
                color: camTap.pressed ? "#15803d" : "#1f8a4c"
                Text { anchors.centerIn: parent; text: qsTr("📷 Scan ID with camera"); color: "white"; font.pixelSize: 24; font.bold: true }
                TapHandler { id: camTap; onTapped: { Qt.inputMethod.hide(); ov.controller.command("scanCameraId") } }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    implicitHeight: 60; radius: 10
                    color: cancelTap.pressed ? "#5a2222" : "#7a2e2e"
                    Text { anchors.centerIn: parent; text: qsTr("Not sold"); color: "white"; font.pixelSize: 20; font.bold: true }
                    TapHandler { id: cancelTap; onTapped: ov.pos.cancelAgeCheck() }
                }
                Rectangle {
                    objectName: "ageTypeDob"
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    implicitHeight: 60; radius: 10
                    visible: !ov.typing
                    color: typeTap.pressed ? "#3b4352" : "#2a313d"
                    Text { anchors.centerIn: parent; text: qsTr("Won't scan: type date"); color: "white"; font.pixelSize: 18 }
                    TapHandler { id: typeTap; onTapped: { ov.typing = true; ov.dob = "" } }
                }
            }
        }
    }
}
