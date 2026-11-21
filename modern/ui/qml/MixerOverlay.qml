import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The bar: a spirit was just rung; what goes with it? Its mixer from the store's Mixers modifier group (priced
// there: it goes on the drink as a choice, prints and charges), or No mixer; and the free notes (Neat, Rocks,
// Up...). The server decides when it asks (pos_bar_menu.cpp: Store Settings -> Bar menu); a group with nothing
// in it never asks. Staff screens only (the register and the tablets).
Rectangle {
    id: ov
    objectName: "mixerOverlay"
    anchors.fill: parent
    z: 92
    color: "#cc0b0f17"
    property var controller: null
    readonly property var pos: controller ? controller.pos : null
    readonly property var ask: pos && pos.mixerAsk ? pos.mixerAsk : ({})
    readonly property bool asking: ask.active === true
    visible: asking
    property var picked: []
    onAskingChanged: picked = (ask.notes ?? []).filter(n => n.chosen).map(n => n.name)
    function toggle(name) {
        const p = picked.slice()
        const at = p.indexOf(name)
        if (at >= 0) p.splice(at, 1)
        else p.push(name)
        picked = p
    }
    MouseArea { anchors.fill: parent }   // nothing under it takes the touch

    Rectangle {
        width: Math.min(parent.width * 0.94, 980)
        height: Math.min(parent.height * 0.96, col.implicitHeight + 40)
        anchors.centerIn: parent
        radius: 16
        color: "#1e2430"
        border.color: "#2f7f86"
        border.width: 3

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14
            Text {
                objectName: "mixerTitle"
                Layout.fillWidth: true
                text: qsTr("%1: with a mixer?").arg(ov.ask.item ?? "")
                color: "white"
                font.pixelSize: 32
                font.bold: true
                elide: Text.ElideRight
            }
            Flow {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: ov.ask.mixers ?? []
                    delegate: WidgetKey {
                        required property var modelData
                        objectName: "mixer-" + modelData.index
                        width: 210
                        height: 100
                        enabled: !modelData.soldOut
                        opacity: enabled ? 1 : 0.45
                        accent: modelData.chosen === true
                        baseColor: "#2f7f86"
                        fontScale: 0.24
                        text: modelData.name + (modelData.price ? "\n" + modelData.price : "")
                        onClicked: ov.pos.answerMixer(modelData.index, ov.picked)
                    }
                }
            }
            Flow {
                visible: (ov.ask.notes ?? []).length > 0
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: ov.ask.notes ?? []
                    delegate: WidgetKey {
                        required property var modelData
                        objectName: "mixerNote-" + modelData.name
                        width: 150
                        height: 72
                        accent: ov.picked.includes(modelData.name)
                        fontScale: 0.3
                        text: modelData.name + (modelData.price ? " " + modelData.price : "")
                        onClicked: ov.toggle(modelData.name)
                    }
                }
            }
            WidgetKey {
                objectName: "noMixer"
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                baseColor: "#475569"
                fontScale: 0.3
                text: qsTr("No mixer")
                onClicked: ov.pos.answerMixer(-1, ov.picked)
            }
        }
    }
}
