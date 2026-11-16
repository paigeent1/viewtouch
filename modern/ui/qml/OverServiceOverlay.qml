import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Responsible service: a drink that brings a guest to the store's caution or stop level waits here
// (Store Settings -> Responsible service; the server decides, pos_over_service.cpp).
//  - Caution (yellow): the bartender acknowledges it with one tap, or doesn't ring it in.
//  - Stop (flashing red): a manager's PIN and a reason (picked or typed) ring it in, or it isn't.
// Staff screens only: the POS and the tablets. The guest's screens never show it.
Rectangle {
    id: ov
    anchors.fill: parent
    z: 94
    color: "#cc0b0f17"
    property var controller: null
    readonly property var pos: controller ? controller.pos : null
    readonly property var ask: pos ? pos.serviceAsk : ({})
    readonly property string level: ask.level ?? ""
    readonly property bool asking: level === "caution" || level === "stop"
    readonly property bool stop: level === "stop"
    visible: asking
    property string pin: ""
    property string reason: ""
    onAskingChanged: { pin = ""; reason = ""; typedReason.text = "" }
    MouseArea { anchors.fill: parent }   // nothing under it takes the touch

    Rectangle {
        id: card
        objectName: "overService"
        width: Math.min(parent.width * 0.94, 660)
        height: Math.min(parent.height * 0.96, col.implicitHeight + 40)
        anchors.centerIn: parent
        radius: 16
        color: "#1e2430"
        border.color: ov.stop ? "#ff3b3b" : "#f0b429"
        border.width: 4
        // The stop flashes (about twice a second).
        SequentialAnimation on border.color {
            running: ov.visible && ov.stop
            loops: Animation.Infinite
            ColorAnimation { to: "#ff3b3b"; duration: 220 }
            ColorAnimation { to: "#5a1010"; duration: 220 }
        }
        Flickable {
            anchors.fill: parent
            anchors.margins: 20
            contentHeight: col.implicitHeight
            clip: true
            ColumnLayout {
                id: col
                width: parent.width
                spacing: 12
                Rectangle {
                    id: banner
                    objectName: "overServiceBanner"
                    Layout.fillWidth: true
                    implicitHeight: 64
                    radius: 10
                    color: ov.stop ? "#b91c1c" : "#f0b429"
                    SequentialAnimation on color {
                        running: ov.visible && ov.stop
                        loops: Animation.Infinite
                        ColorAnimation { to: "#ff2d2d"; duration: 220 }
                        ColorAnimation { to: "#5a1010"; duration: 220 }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: ov.stop ? qsTr("STOP: responsible service") : qsTr("Caution: responsible service")
                        color: ov.stop ? "white" : "#1e2430"
                        font.pixelSize: 26; font.bold: true
                    }
                }
                Text {
                    objectName: "overServiceCount"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("%1: %2 standard drinks with this one").arg(ov.ask.guest ?? "").arg(ov.ask.drinks ?? "")
                    color: "white"; font.pixelSize: 24; font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("%1 (+%2). Before it: %3, within %4 hours.").arg(ov.ask.item ?? "").arg(ov.ask.adding ?? "")
                              .arg(ov.ask.before ?? "").arg(ov.ask.windowHours ?? "")
                          + (ov.ask.check ? "  ·  " + ov.ask.check : "")
                    color: "#d9dee7"; font.pixelSize: 18
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: ov.stop ? qsTr("This drink isn't rung in until a manager overrides it.")
                                  : qsTr("Check on the guest, then acknowledge to ring it in.")
                    color: ov.stop ? "#ffb4b4" : "#f0b429"; font.pixelSize: 18
                }

                // --- stop: the manager's reason and PIN ---
                Text {
                    visible: ov.stop
                    Layout.fillWidth: true
                    text: qsTr("Reason")
                    color: "#d9dee7"; font.pixelSize: 16; font.bold: true
                }
                Flow {
                    visible: ov.stop
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: ov.stop ? (ov.ask.reasons ?? []) : []
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            objectName: "overServiceReason-" + index
                            width: Math.min(col.width, reasonText.implicitWidth + 28); height: 52
                            radius: 10
                            color: ov.reason === modelData ? "#2563eb" : (rTap.pressed ? "#3b4352" : "#2a313d")
                            Text {
                                id: reasonText
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, col.width - 28)
                                elide: Text.ElideRight
                                text: modelData
                                color: "white"; font.pixelSize: 16
                            }
                            TapHandler { id: rTap; onTapped: { ov.reason = modelData; typedReason.text = "" } }
                        }
                    }
                }
                TextField {
                    id: typedReason
                    objectName: "overServiceTypedReason"
                    visible: ov.stop
                    Layout.fillWidth: true
                    placeholderText: qsTr("Or type the reason")
                    font.pixelSize: 18
                    onTextEdited: ov.reason = text
                }
                Text {
                    visible: ov.stop
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Manager's PIN: %1").arg(ov.pin.length > 0 ? "•".repeat(ov.pin.length) : "—")
                    color: "white"; font.pixelSize: 20; font.bold: true
                }
                GridLayout {
                    visible: ov.stop
                    Layout.alignment: Qt.AlignHCenter
                    columns: 3
                    rowSpacing: 8; columnSpacing: 8
                    Repeater {
                        model: ov.stop ? ["1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "C"] : []
                        delegate: Rectangle {
                            id: key
                            required property string modelData
                            objectName: "overServicePin-" + modelData
                            implicitWidth: 104; implicitHeight: 54
                            radius: 10
                            color: k.pressed ? "#4c8dff" : "#343c49"
                            Text { anchors.centerIn: parent; text: key.modelData; color: "white"; font.pixelSize: 22; font.bold: true }
                            TapHandler {
                                id: k
                                onTapped: {
                                    if (key.modelData === "⌫") ov.pin = ov.pin.slice(0, -1)
                                    else if (key.modelData === "C") ov.pin = ""
                                    else if (ov.pin.length < 12) ov.pin += key.modelData
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        objectName: "overServiceCancel"
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        implicitHeight: 64; radius: 10
                        color: cancelTap.pressed ? "#3b4352" : "#2a313d"
                        Text { anchors.centerIn: parent; text: qsTr("Not rung in"); color: "white"; font.pixelSize: 20; font.bold: true }
                        TapHandler { id: cancelTap; onTapped: ov.pos.cancelService() }
                    }
                    Rectangle {
                        objectName: "overServiceAcknowledge"
                        visible: !ov.stop
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        implicitHeight: 64; radius: 10
                        color: ackTap.pressed ? "#c98f10" : "#f0b429"
                        Text { anchors.centerIn: parent; text: qsTr("Acknowledge"); color: "#1e2430"; font.pixelSize: 22; font.bold: true }
                        TapHandler { id: ackTap; onTapped: ov.pos.acknowledgeService() }
                    }
                    Rectangle {
                        objectName: "overServiceOverride"
                        visible: ov.stop
                        readonly property bool ready: ov.pin.length > 0 && ov.reason.trim() !== ""
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        implicitHeight: 64; radius: 10
                        opacity: ready ? 1 : 0.45
                        color: overTap.pressed ? "#7f1d1d" : "#b91c1c"
                        Text { anchors.centerIn: parent; text: qsTr("Manager override"); color: "white"; font.pixelSize: 20; font.bold: true }
                        TapHandler {
                            id: overTap
                            onTapped: if (parent.ready) { ov.pos.overrideService(ov.pin, ov.reason.trim()); ov.pin = "" }
                        }
                    }
                }
            }
        }
    }
}
