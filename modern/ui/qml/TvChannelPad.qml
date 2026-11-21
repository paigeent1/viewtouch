import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// TV channel number pad (owner at HCA 2026-11-21; the directvKeypad command: the DIRECTV pages' CH # key,
// or any button given it): type a channel (up to 4 digits, on the keys or a keyboard), Tune (or Enter)
// sends the directv command the channel buttons send, for the button's box, and the pad closes; the TV's
// answer shows on the status line. Tap outside the pad, Close or Esc: nothing sent.
Rectangle {
    id: pad
    property PosService pos
    property var controller
    readonly property bool open: controller ? controller.tvPadOpen === true : false
    readonly property int maxDigits: 4
    readonly property real u: Math.max(14, Math.min(width, height) / 28)
    property string digits: ""
    readonly property string channelName: controller && digits !== "" ? controller.tvChannelName(digits) : ""
    visible: open && (pos === null || pos.loggedIn)
    color: Qt.rgba(0.04, 0.05, 0.07, 0.80)
    focus: visible

    onOpenChanged: {
        digits = ""
        if (open) Qt.callLater(() => pad.forceActiveFocus())
    }
    onVisibleChanged: if (!visible && open && controller) controller.closeTvPad()   // logged out

    function typeDigit(d) {
        if (digits.length < maxDigits) digits += d
    }
    function backspace() { digits = digits.slice(0, -1) }
    function tune() {
        if (controller && digits !== "") controller.tvPadTune(digits)
    }
    function close() { if (controller) controller.closeTvPad() }

    // A keyboard: digits, Backspace, Delete (clear), Enter (tune), Esc (close).
    Keys.onPressed: (event) => {
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
            typeDigit(String(event.key - Qt.Key_0))
        } else if (event.key === Qt.Key_Backspace) {
            backspace()
        } else if (event.key === Qt.Key_Delete) {
            digits = ""
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            tune()
        } else if (event.key === Qt.Key_Escape) {
            close()
        } else {
            return
        }
        event.accepted = true
    }

    // A tap outside the pad closes it (touches never reach the page behind).
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: pad.close()
    }

    component PadKey: Button {
        property real unit: 20
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 5 * unit
        Layout.preferredHeight: 3.6 * unit
        font.pixelSize: 1.7 * unit
        font.bold: true
        focusPolicy: Qt.NoFocus
    }

    Rectangle {
        id: box
        objectName: "tvPadBox"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * pad.u, 19 * pad.u)
        height: Math.min(parent.height - 2 * pad.u, column.implicitHeight + 2 * pad.u)
        radius: pad.u * 0.6
        color: "#111827"
        border.color: "#334155"
        border.width: 2

        // Touches on the pad stay on it.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: pad.u
            spacing: pad.u * 0.5

            RowLayout {
                Layout.fillWidth: true
                spacing: pad.u * 0.5
                Label {
                    objectName: "tvPadTitle"
                    Layout.fillWidth: true
                    text: pad.controller ? pad.controller.tvPadTitle : ""
                    color: "#fbbf24"
                    font.pixelSize: 1.3 * pad.u
                    font.bold: true
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "tvPadClose"
                    implicitHeight: 2.4 * pad.u
                    implicitWidth: 5.5 * pad.u
                    font.pixelSize: pad.u
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Close")
                    onClicked: pad.close()
                }
            }

            // What's typed, and the channel's name when it's one of ours.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 4.2 * pad.u
                radius: pad.u * 0.3
                color: "#020617"
                border.color: "#475569"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: pad.u * 0.2
                    spacing: 0
                    Label {
                        objectName: "tvPadDigits"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: pad.digits !== "" ? pad.digits : qsTr("Channel #")
                        color: pad.digits !== "" ? "white" : "#64748b"
                        font.pixelSize: (pad.digits !== "" ? 2.2 : 1.4) * pad.u
                        font.bold: pad.digits !== ""
                    }
                    Label {
                        objectName: "tvPadChannelName"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: pad.channelName
                        visible: text !== ""
                        color: "#86efac"
                        font.pixelSize: pad.u
                        font.bold: true
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 3
                rowSpacing: pad.u * 0.35
                columnSpacing: pad.u * 0.35
                Repeater {
                    model: ["1", "2", "3", "4", "5", "6", "7", "8", "9"]
                    delegate: PadKey {
                        required property string modelData
                        objectName: "tvPadKey" + modelData
                        unit: pad.u
                        text: modelData
                        enabled: pad.digits.length < pad.maxDigits
                        onClicked: pad.typeDigit(modelData)
                    }
                }
                PadKey {
                    objectName: "tvPadClear"
                    unit: pad.u
                    font.pixelSize: pad.u
                    text: qsTr("Clear")
                    onClicked: pad.digits = ""
                }
                PadKey {
                    objectName: "tvPadKey0"
                    unit: pad.u
                    text: "0"
                    enabled: pad.digits.length < pad.maxDigits
                    onClicked: pad.typeDigit("0")
                }
                PadKey {
                    objectName: "tvPadBackspace"
                    unit: pad.u
                    text: "⌫"
                    onClicked: pad.backspace()
                }
            }

            Button {
                objectName: "tvPadTune"
                Layout.fillWidth: true
                implicitHeight: 3.6 * pad.u
                font.pixelSize: 1.6 * pad.u
                font.bold: true
                highlighted: true
                focusPolicy: Qt.NoFocus
                enabled: pad.digits !== ""
                text: pad.digits !== "" ? qsTr("Tune %1").arg(pad.digits) : qsTr("Tune")
                palette.button: "#1f8a4c"
                onClicked: pad.tune()
            }
        }
    }
}
