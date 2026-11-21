import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The Draft page: the bar's draft lines laid out as the bar has them, a tower to a column (T1, T2,
// T3 side by side, its lines top to bottom), from the draft-line data (pos.draftLines). Each line is
// a big key with the beer on it now and how full its keg is; touch one: its one pour is rung at once,
// or (two or more) choose the size (12 oz, 16 oz, with prices): rung as any item (the keg on that line pours it). A line with nothing on it, or
// whose beer has no menu item, is grey and can't be touched; a kicked keg (all of it rung up) says
// "Kicked — change keg" and opens Change keg.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var draft: pos ? pos.draftLines : ({})
    readonly property var lines: draft.lines ?? []
    // The towers, in the order their lines come ("T1", "T2", ...).
    readonly property var towers: {
        const out = []
        for (const l of lines) {
            const t = l.tower ?? String(l.id).split("-")[0]
            if (!out.includes(t)) out.push(t)
        }
        return out
    }
    readonly property int rows: {
        let n = 1
        for (const t of towers) n = Math.max(n, lines.filter(l => (l.tower ?? String(l.id).split("-")[0]) === t).length)
        return n
    }
    readonly property var items: pos ? pos.orderItems : []
    function itemOf(id) { return items.find(i => i.id === id) ?? null }
    // The sizes that pour from a line: its items on the order screens (price, sold out).
    function sizesOf(line) {
        return (line.items ?? []).map(s => {
            const i = w.itemOf(s.id)
            return i ? { id: s.id, oz: s.oz, name: i.name, price: i.price, available: i.available } : null
        }).filter(s => s !== null)
    }
    function stateOf(line) {
        if (!line.keg) return "empty"
        if (line.kicked) return "kicked"
        const sizes = sizesOf(line)
        if (sizes.length === 0) return "noItem"
        return sizes.some(x => x.available) ? "ready" : "soldOut"
    }
    readonly property real gap: 10
    readonly property string face: zone && zone.st ? (zone.st.font ?? "DejaVu Sans") : "DejaVu Sans"
    property var picked: null   // the line touched: choose its size
    onPickedChanged: picked !== null ? sizes.open() : sizes.close()

    RowLayout {
        anchors.fill: parent
        spacing: w.gap
        visible: w.lines.length > 0
        Repeater {
            model: w.towers
            delegate: ColumnLayout {
                id: tower
                required property string modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: w.gap * 0.6
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Tower %1").arg(tower.modelData.replace(/^T/, ""))
                    color: "#c8d0dc"
                    font.family: w.face
                    font.pixelSize: 22
                    font.bold: true
                }
                Repeater {
                    model: w.lines.filter(l => (l.tower ?? String(l.id).split("-")[0]) === tower.modelData)
                    delegate: Rectangle {
                        id: tap
                        required property var modelData
                        readonly property string state_: w.stateOf(modelData)
                        readonly property var sizes: w.sizesOf(modelData)
                        readonly property bool usable: state_ === "ready" || state_ === "kicked"
                        objectName: "draftTap-" + modelData.id
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.maximumHeight: (w.height - 40) / w.rows
                        radius: 12
                        color: state_ === "ready" ? (press.pressed ? "#4c8dff" : "#7a4a12")
                             : state_ === "kicked" ? "#5a1d1d" : "#2a2f37"
                        border.color: state_ === "kicked" ? "#ff6b6b" : Qt.darker(color, 1.4)
                        border.width: 2
                        opacity: usable ? 1 : 0.5
                        // How full: a bar along the bottom (percent left).
                        Rectangle {
                            visible: tap.state_ === "ready" && (tap.modelData.kegOz ?? 0) > 0
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 4
                            height: 8
                            radius: 4
                            width: Math.max(0, (parent.width - 8) * Math.min(100, tap.modelData.percentLeft ?? 0) / 100)
                            color: (tap.modelData.percentLeft ?? 0) <= 15 ? "#ff6b6b" : (tap.modelData.percentLeft ?? 0) <= 35 ? "#f5b940" : "#4ade80"
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 6
                            text: tap.modelData.id
                            color: "#cbd5e1"
                            font.family: w.face
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Text {
                            visible: tap.state_ === "ready"
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 6
                            text: qsTr("%1% left").arg(tap.modelData.percentLeft ?? 0)
                            color: "#fde68a"
                            font.family: w.face
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Text {
                            objectName: "draftTapName-" + tap.modelData.id
                            anchors.centerIn: parent
                            width: parent.width - 16
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            text: tap.state_ === "empty" ? qsTr("(empty)")
                                : tap.state_ === "kicked" ? tap.modelData.beer + "\n" + qsTr("Kicked — change keg")
                                : tap.state_ === "noItem" ? tap.modelData.beer + "\n" + qsTr("(not on the menu)")
                                : tap.state_ === "soldOut" ? tap.modelData.beer + "\n" + qsTr("SOLD OUT")
                                : tap.modelData.beer + (tap.sizes.length === 1 ? "\n" + (tap.sizes[0].oz > 0 ? qsTr("%1 oz").arg(tap.sizes[0].oz) + "  " : "")
                                                                                 + (tap.sizes[0].available ? tap.sizes[0].price : qsTr("SOLD OUT")) : "")
                            color: "white"
                            font.family: w.face
                            font.pixelSize: Math.max(14, Math.min(26, tap.height * 0.24))
                            font.bold: true
                        }
                        MouseArea {
                            id: press
                            anchors.fill: parent
                            enabled: tap.usable
                            onClicked: {
                                if (tap.state_ === "kicked") {
                                    w.zone.controller.jumpToPage("change-keg")
                                    return
                                }
                                // One pour for this beer: rung at once; two or more: choose the size.
                                const sizes = w.sizesOf(tap.modelData)
                                if (sizes.length === 1) {
                                    if (sizes[0].available)
                                        w.zone.controller.orderDraft(tap.modelData.id, sizes[0].id)
                                } else {
                                    w.picked = tap.modelData
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Text {
        visible: w.lines.length === 0
        anchors.centerIn: parent
        text: qsTr("No draft lines yet (Manager → Bar & Kegs → Draft lines).")
        color: "#8a94a6"
        font.pixelSize: 24
    }

    // The sizes of the line touched, with their prices.
    Popup {
        id: sizes
        objectName: "draftSizes"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 16
        onClosed: w.picked = null
        readonly property var list: w.picked ? w.sizesOf(w.picked) : []
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: w.picked ? w.picked.beer + "  ·  " + w.picked.id : ""
                color: "white"
                font.pixelSize: 26
                font.bold: true
            }
            RowLayout {
                spacing: 12
                Repeater {
                    model: sizes.list
                    delegate: WidgetKey {
                        required property var modelData
                        objectName: "draftSize-" + modelData.id
                        implicitWidth: 230
                        implicitHeight: 130
                        opacity: modelData.available ? 1 : 0.45
                        baseColor: "#7a4a12"
                        fontScale: 0.2
                        text: (modelData.oz > 0 ? qsTr("%1 oz").arg(modelData.oz) : modelData.name) + "\n"
                              + (modelData.available ? modelData.price : qsTr("SOLD OUT"))
                        onClicked: {
                            if (!modelData.available) return
                            const line = w.picked.id
                            w.picked = null
                            w.zone.controller.orderDraft(line, modelData.id)
                        }
                    }
                }
            }
            WidgetKey {
                Layout.fillWidth: true
                implicitHeight: 64
                text: qsTr("Cancel")
                fontScale: 0.34
                onClicked: w.picked = null
            }
        }
    }
}
