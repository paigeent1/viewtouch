import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The bar: one bottle's pours (1.25 oz $9, 2 oz $13, Glass, Bottle...), each with its price; the free notes
// first if wanted (Neat, Rocks, Up...: Store Settings -> Bar menu's notes group). One touch on a size rings
// that pour, exactly as its own button would (pos_bar_menu.cpp: orderPour).
Popup {
    id: pp
    objectName: "pourPicker"
    property var pos: null
    property var controller: null
    property var bottle: ({})
    property var picked: []
    readonly property var pours: bottle.pours ?? []
    readonly property var notes: pos && pos.mixerAsk ? (pos.mixerAsk.notes ?? []) : []
    function show(b) {
        bottle = b
        picked = []
        open()
    }
    function toggle(name) {
        const p = picked.slice()
        const at = p.indexOf(name)
        if (at >= 0) p.splice(at, 1)
        else p.push(name)
        picked = p
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 32 : 900, 900)
    modal: true
    padding: 20
    background: Rectangle { color: "#1e2430"; radius: 16; border.color: "#8a4b16"; border.width: 3 }

    contentItem: ColumnLayout {
        spacing: 14
        Text {
            objectName: "pourPickerTitle"
            Layout.fillWidth: true
            text: qsTranslate("Page", pp.bottle.name ?? "")
            color: "white"
            font.pixelSize: 34
            font.bold: true
            elide: Text.ElideRight
        }
        Flow {
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: pp.pours
                delegate: WidgetKey {
                    required property var modelData
                    objectName: "pour-" + modelData.id
                    width: Math.max(180, (pp.availableWidth - 12 * 2) / 3)
                    height: 120
                    enabled: modelData.available
                    opacity: enabled ? 1 : 0.45
                    baseColor: "#8a4b16"
                    fontScale: 0.24
                    text: (modelData.pourName || modelData.name) + "\n"
                          + (modelData.available ? modelData.price : qsTr("SOLD OUT"))
                    onClicked: {
                        if (pp.controller)
                            pp.controller.orderPour(modelData.id, pp.picked)
                        pp.close()
                    }
                }
            }
        }
        Text {
            visible: pp.notes.length > 0
            text: qsTr("Notes (touch before the size):")
            color: "#cbd5e1"
            font.pixelSize: 22
        }
        Flow {
            visible: pp.notes.length > 0
            Layout.fillWidth: true
            spacing: 10
            Repeater {
                model: pp.notes
                delegate: WidgetKey {
                    required property var modelData
                    objectName: "pourNote-" + modelData.name
                    width: 150
                    height: 72
                    accent: pp.picked.includes(modelData.name)
                    fontScale: 0.3
                    text: modelData.name + (modelData.price ? " " + modelData.price : "")
                    onClicked: pp.toggle(modelData.name)
                }
            }
        }
        WidgetKey {
            objectName: "pourPickerCancel"
            Layout.alignment: Qt.AlignRight
            Layout.preferredWidth: 200
            Layout.preferredHeight: 72
            text: qsTr("Cancel")
            fontScale: 0.3
            onClicked: pp.close()
        }
    }
}
