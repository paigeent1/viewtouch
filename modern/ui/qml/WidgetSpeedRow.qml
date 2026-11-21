import QtQuick
import QtQuick.Layouts

// The speed row (bar): the store's 12 best sellers (past four weeks and today: pos.topSellers), one
// touch each, with REPEAT ROUND (the drinks of this check's last Send again, with their choices),
// SAME AGAIN (the newest drink once more, with its choices) and DRAFT (the Draft page; only while a
// keg is on a draft line). Sold-out items are grey and can't be touched. props.draft: false leaves
// the Draft key off (the Draft page itself); props.count: how many best sellers (default 12).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property int count: zone && zone.props && zone.props.count > 0 ? zone.props.count : 12
    readonly property bool draftKey: !(zone && zone.props && zone.props.draft === false)
                                     && (pos ? (pos.draftLines.lines ?? []).some(l => !!l.keg) : false)
    readonly property var items: pos ? pos.orderItems.filter(i => !i.modifier) : []
    readonly property var top: {
        const out = []
        for (const id of (pos ? pos.topSellers : [])) {
            const i = items.find(x => x.id === id)
            if (i && out.length < count) out.push(i)
        }
        return out
    }
    readonly property int perRow: Math.max(1, Math.ceil(count / 2))
    readonly property real gap: 8
    readonly property real keyW: (width - gap * (perRow + 1)) / (perRow + 2)
    readonly property real keyH: (height - gap) / 2
    readonly property string face: zone && zone.st ? (zone.st.font ?? "DejaVu Sans") : "DejaVu Sans"

    Row {
        anchors.fill: parent
        spacing: w.gap
        Grid {
            columns: w.perRow
            spacing: w.gap
            width: w.perRow * (w.keyW + w.gap) - w.gap
            Repeater {
                model: w.top
                delegate: Rectangle {
                    id: key
                    required property var modelData
                    objectName: "speedItem-" + modelData.id
                    width: w.keyW
                    height: w.keyH
                    radius: 10
                    color: !modelData.available ? "#3a3f48" : press.pressed ? "#4c8dff" : (modelData.buttonColor || "#343c49")
                    border.color: Qt.darker(color, 1.4)
                    border.width: 2
                    opacity: modelData.available ? 1 : 0.5
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 10
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: qsTranslate("Page", key.modelData.name)
                            color: key.modelData.buttonColor ? StoreColors.ink(key.modelData.buttonColor) : "white"
                            font.family: w.face
                            font.pixelSize: Math.max(12, Math.min(20, key.height * 0.28))
                            font.bold: true
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: key.modelData.available ? key.modelData.price : qsTr("SOLD OUT")
                            color: key.modelData.available ? "#fde047" : "#ff6b6b"
                            font.family: w.face
                            font.pixelSize: Math.max(11, Math.min(17, key.height * 0.22))
                        }
                    }
                    MouseArea {
                        id: press
                        anchors.fill: parent
                        enabled: key.modelData.available
                        onClicked: w.zone.controller.orderItem(key.modelData.id)
                    }
                }
            }
        }
        Grid {
            columns: 2
            spacing: w.gap
            WidgetKey {
                objectName: "speedRepeatRound"
                width: w.keyW
                height: w.keyH
                text: qsTr("REPEAT\nROUND")
                baseColor: "#1f6b40"
                fontScale: 0.2
                onClicked: w.zone.controller.command("repeatRound")
            }
            WidgetKey {
                objectName: "speedSameAgain"
                width: w.keyW
                height: w.keyH
                text: qsTr("SAME\nAGAIN")
                baseColor: "#2b62b0"
                fontScale: 0.2
                onClicked: w.zone.controller.command("sameAgain")
            }
            WidgetKey {
                objectName: "speedDraft"
                visible: w.draftKey
                width: w.keyW * 2 + w.gap
                height: w.keyH
                text: qsTr("🍺 DRAFT")
                baseColor: "#7a4a12"
                fontScale: 0.3
                onClicked: w.zone.controller.jumpToPage("draft")
            }
        }
    }
}
