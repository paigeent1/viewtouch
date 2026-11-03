import QtQuick
import QtQuick.Layouts

// PRICE CHECK mode: "*PRICE CHECK*" flashing across the top while it's on, and
// the item scanned in a floating window (its box or its singles too). Touch the
// window to close it; the mode stays on until PRICE CHECK is touched again.
// `guest`: the customer display's copy (bigger, no buttons).
Item {
    id: o
    property PosService pos
    property bool guest: false
    readonly property var info: pos ? pos.priceCheck : ({})
    readonly property bool on: info.on === true
    readonly property var item: info.item ?? null
    readonly property real unit: Math.max(14, Math.min(width, height * 1.6) / (guest ? 32 : 44))
    visible: on && (!guest || item !== null)

    Rectangle {
        id: banner
        visible: !o.guest
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 6
        width: label.implicitWidth + o.unit * 3
        height: o.unit * 2.6
        radius: height / 2
        color: "#facc15"
        border.color: "#111827"
        border.width: 3
        Text {
            id: label
            anchors.centerIn: parent
            text: qsTr("*PRICE CHECK*")
            color: "#111827"
            font.pixelSize: o.unit * 1.5
            font.bold: true
        }
        SequentialAnimation on opacity {
            running: banner.visible && o.on
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 450 }
            NumberAnimation { to: 1; duration: 450 }
        }
    }

    // A dim behind the window: touching anywhere closes it.
    Rectangle {
        anchors.fill: parent
        visible: o.item !== null
        color: "#99000000"
        MouseArea { anchors.fill: parent; enabled: !o.guest; onClicked: o.pos.closePriceCheck() }
    }
    Rectangle {
        visible: o.item !== null
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.86, o.unit * 34)
        height: col.implicitHeight + o.unit * 2.4
        radius: o.unit
        color: "#ffffff"
        border.color: "#facc15"
        border.width: 6
        MouseArea { anchors.fill: parent; enabled: !o.guest; onClicked: o.pos.closePriceCheck() }
        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: o.unit * 1.2
            spacing: o.unit * 0.5
            Text {
                text: qsTr("PRICE CHECK")
                color: "#b45309"
                font.pixelSize: o.unit * 0.9
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: o.item ? o.item.name : ""
                color: "#111827"
                wrapMode: Text.Wrap
                font.pixelSize: o.unit * 1.6
                font.bold: true
            }
            Text {
                text: o.item ? o.item.price : ""
                color: "#15803d"
                font.pixelSize: o.unit * 3
                font.bold: true
            }
            Text {
                visible: !o.guest && o.item && (o.item.upc ?? "") !== ""
                text: o.item ? qsTr("UPC %1").arg(o.item.upc) : ""
                color: "#4b5563"
                font.pixelSize: o.unit * 0.9
            }
            Text {
                visible: !o.guest && o.item && (o.item.noDiscount === true || o.item.soldOut === true)
                text: o.item ? [o.item.noDiscount ? qsTr("No discounts") : "", o.item.soldOut ? qsTr("Sold out") : ""]
                                   .filter(x => x).join("  ·  ") : ""
                color: "#b91c1c"
                font.pixelSize: o.unit * 0.9
                font.bold: true
            }
            Rectangle {
                visible: o.item && (o.item.related ?? []).length > 0
                Layout.fillWidth: true
                height: 2
                color: "#e5e7eb"
            }
            Repeater {
                model: o.item ? (o.item.related ?? []) : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: o.unit
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: modelData.relation
                            color: "#6b7280"
                            font.pixelSize: o.unit * 0.8
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: "#111827"
                            elide: Text.ElideRight
                            font.pixelSize: o.unit * 1.1
                        }
                    }
                    Text {
                        text: modelData.price
                        color: "#15803d"
                        font.pixelSize: o.unit * 1.6
                        font.bold: true
                    }
                }
            }
            Text {
                visible: !o.guest
                Layout.topMargin: o.unit * 0.4
                text: qsTr("Touch to close · PRICE CHECK again to turn it off")
                color: "#6b7280"
                font.pixelSize: o.unit * 0.75
            }
        }
    }
}
