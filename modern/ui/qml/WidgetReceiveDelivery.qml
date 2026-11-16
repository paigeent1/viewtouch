import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Receiving a delivery: pick the vendor, type how much came of each item
// (and the invoice's cost if it isn't the usual), Receive. Stock goes up and
// the delivery is kept for the Purchases report. Receiving never changes an
// item's system cost: an invoice cost that differs goes on Cost differences
// for review (the system cost comes from the item editor or the price book).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.receiving : ({})
    property string vendor: ""
    property var qty: ({})    // ingredient id -> quantity typed
    property var cost: ({})   // ingredient id -> cost typed
    property int typed: 0     // bumps when either changes (for the total)
    property bool showDiffs: false   // the Cost differences review, over the screen
    readonly property var diffs: info.costDifferences ?? []
    readonly property var shown: (info.ingredients ?? []).filter(i => vendor === "" || i.vendor === vendor)
    readonly property int recentCount: (info.recent ?? []).length
    onRecentCountChanged: clear()   // received: start over

    function clear() {
        qty = ({})
        cost = ({})
        invoice.text = ""
        ++typed
    }
    function unitCost(item) {
        const c = cost[item.id]
        return c !== undefined && c !== "" ? Number(c) : item.cost
    }
    readonly property real total: {
        if (typed < 0) return 0   // compared, not just read: so it refreshes
        let t = 0
        for (const item of info.ingredients ?? [])
            if (Number(qty[item.id] ?? 0) > 0)
                t += Number(qty[item.id]) * unitCost(item)
        return t
    }
    function receive() {
        const lines = []
        for (const item of info.ingredients ?? []) {
            const q = Number(qty[item.id] ?? 0)
            if (q > 0)
                lines.push({ ingredient: item.id, qty: q, cost: unitCost(item) })
        }
        pos.receiveDelivery({ vendor: vendor, invoice: invoice.text, lines: lines })
    }

    // Upright (a phone): the delivery only (no "On hand", no recent deliveries).
    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 460 : 900) : 1.4
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Flow {   // which vendor
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [{ id: "", name: qsTr("All items") }].concat(w.info.vendors ?? [])
                        delegate: Button {
                            required property var modelData
                            objectName: "vendor-" + modelData.id
                            text: modelData.name
                            checkable: true
                            checked: w.vendor === modelData.id
                            highlighted: checked
                            implicitHeight: 44
                            font.pixelSize: 15
                            onClicked: w.vendor = modelData.id
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Label { text: qsTr("Invoice #"); font.pixelSize: 15 }
                    TextField {
                        id: invoice
                        objectName: "deliveryInvoice"
                        Layout.preferredWidth: 220
                        implicitHeight: 44
                        font.pixelSize: 15
                    }
                    Item { Layout.fillWidth: true }
                    // Invoice costs that differed from the system cost, for review.
                    Button {
                        objectName: "costDifferences"
                        text: w.diffs.length ? qsTr("Cost differences (%1)").arg(w.diffs.length) : qsTr("Cost differences")
                        implicitHeight: 44
                        font.pixelSize: 15
                        onClicked: w.showDiffs = true
                    }
                }
                RowLayout {   // headings
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Label { text: qsTr("Item"); opacity: 0.6; Layout.fillWidth: true }
                    Label { visible: !w.narrow; text: qsTr("On hand"); opacity: 0.6; Layout.preferredWidth: 110 }
                    Label { text: qsTr("Received"); opacity: 0.6; Layout.preferredWidth: 110 }
                    Label { text: qsTr("Invoice cost"); opacity: 0.6; Layout.preferredWidth: 110 }
                    Label { text: qsTr("Total"); opacity: 0.6; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                }
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.shown
                    ScrollBar.vertical: TouchScrollBar { id: listBar; needed: list.contentHeight > list.height + 1 }
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        width: ListView.view.width - listBar.room
                        height: 48
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            font.pixelSize: 16
                            font.bold: true
                            color: row.modelData.low ? "#f5b940" : "white"
                            // Two lines before it's cut: "Veggie Patties" whole.
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            lineHeight: 0.9
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: !w.narrow
                            Layout.preferredWidth: 110
                            text: Number(row.modelData.onHand).toLocaleString(Qt.locale(), "f", 1).replace(/\.0$/, "") + " " + row.modelData.unit
                            opacity: 0.8
                        }
                        TextField {
                            objectName: "qty-" + row.modelData.id
                            Layout.preferredWidth: 110
                            implicitHeight: 44
                            font.pixelSize: 16
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            text: w.typed >= 0 ? (w.qty[row.modelData.id] ?? "") : ""
                            placeholderText: row.modelData.unit
                            onTextEdited: { w.qty[row.modelData.id] = text; ++w.typed }
                        }
                        TextField {
                            objectName: "cost-" + row.modelData.id
                            Layout.preferredWidth: 110
                            implicitHeight: 44
                            font.pixelSize: 16
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            text: w.typed >= 0 ? (w.cost[row.modelData.id] ?? "") : ""
                            placeholderText: Number(row.modelData.cost).toFixed(2)
                            onTextEdited: { w.cost[row.modelData.id] = text; ++w.typed }
                        }
                        Label {
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignRight
                            text: { if (w.typed < 0) return ""; const q = Number(w.qty[row.modelData.id] ?? 0)
                                    return q > 0 ? "$" + (q * w.unitCost(row.modelData)).toFixed(2) : "" }
                        }
                    }
                }
                Label {   // what the cost typed here is (and isn't)
                    objectName: "invoiceCostNote"
                    Layout.fillWidth: true
                    text: qsTr("Invoice cost: what this invoice charges, for review. It never changes the system cost; "
                               + "a difference goes on Cost differences.")
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 12
                }
                Label {
                    visible: w.shown.length === 0
                    text: (w.info.ingredients ?? []).length === 0 ? qsTr("Add ingredients in Manager → Inventory first.")
                                                                  : qsTr("Nothing comes from this vendor yet (set it on each item in Inventory).")
                    opacity: 0.6
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    spacing: 10
                    Label { text: qsTr("Total %1").arg("$" + w.total.toFixed(2)); font.pixelSize: 20; font.bold: true; Layout.fillWidth: true }
                    Button {
                        text: qsTr("Clear")
                        implicitHeight: 52
                        font.pixelSize: 16
                        onClicked: w.clear()
                    }
                    Button {
                        objectName: "receiveDelivery"
                        text: qsTr("Receive Delivery")
                        highlighted: true
                        enabled: w.total > 0 || Object.keys(w.qty).some(k => Number(w.qty[k]) > 0)
                        implicitHeight: 52
                        implicitWidth: 220
                        font.pixelSize: 17
                        onClicked: w.receive()
                    }
                }
            }

            ToolSeparator { Layout.fillHeight: true; visible: !w.narrow }

            ColumnLayout {
                visible: !w.narrow
                Layout.preferredWidth: parent.width * 0.3
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: 6
                Label { text: qsTr("Received lately"); font.bold: true; font.pixelSize: 17 }
                ListView {
                    id: recent
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: w.info.recent ?? []
                    ScrollBar.vertical: TouchScrollBar { id: recentBar; needed: recent.contentHeight > recent.height + 1 }
                    delegate: ColumnLayout {
                        required property var modelData
                        width: ListView.view.width - recentBar.room
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: (modelData.vendor || qsTr("Delivery")) + "  ·  " + modelData.total
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.when + "  ·  " + qsTr("%n item(s)", "", modelData.items)
                                  + (modelData.invoice ? "  ·  #" + modelData.invoice : "") + "  ·  " + modelData.by
                            opacity: 0.6
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
                Label {
                    visible: (w.info.recent ?? []).length === 0
                    text: qsTr("Nothing received yet.")
                    opacity: 0.6
                }
            }
        }
    }

    // Cost differences: lines received at an invoice cost other than the system cost, newest first.
    Rectangle {
        objectName: "costDifferencesView"
        anchors.fill: parent
        visible: w.showDiffs
        color: "#14181e"
        MouseArea { anchors.fill: parent }   // nothing under it is touched
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Cost differences"); font.bold: true; font.pixelSize: 20; Layout.fillWidth: true }
                Button {
                    objectName: "closeCostDifferences"
                    text: qsTr("Close")
                    implicitHeight: 48
                    font.pixelSize: 16
                    onClicked: w.showDiffs = false
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Invoice costs that differ from the system cost. Receiving never changes the system cost: "
                           + "change it in the item editor, or let the price book sync do it.")
                wrapMode: Text.WordWrap
                opacity: 0.6
            }
            RowLayout {   // headings
                Layout.fillWidth: true
                visible: !w.narrow
                Label { text: qsTr("Date"); opacity: 0.6; Layout.preferredWidth: 130 }
                Label { text: qsTr("Vendor"); opacity: 0.6; Layout.preferredWidth: 150 }
                Label { text: qsTr("Item"); opacity: 0.6; Layout.fillWidth: true }
                Label { text: qsTr("System cost"); opacity: 0.6; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                Label { text: qsTr("Invoice cost"); opacity: 0.6; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                Label { text: qsTr("Difference"); opacity: 0.6; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
            }
            ListView {
                id: diffList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: w.showDiffs ? w.diffs : []
                ScrollBar.vertical: TouchScrollBar { id: diffBar; needed: diffList.contentHeight > diffList.height + 1 }
                delegate: RowLayout {
                    id: diff
                    required property var modelData
                    width: ListView.view.width - diffBar.room
                    Label { visible: !w.narrow; text: diff.modelData.when; Layout.preferredWidth: 130; elide: Text.ElideRight }
                    Label { visible: !w.narrow; text: diff.modelData.vendor || "-"; Layout.preferredWidth: 150; elide: Text.ElideRight }
                    Label {
                        Layout.fillWidth: true
                        text: w.narrow ? diff.modelData.item + "  ·  " + (diff.modelData.vendor || "-") + "  ·  " + diff.modelData.when
                                       : diff.modelData.item
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label { text: diff.modelData.systemCost; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                    Label { text: diff.modelData.invoiceCost; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight }
                    Label {
                        text: diff.modelData.difference
                        color: diff.modelData.differenceCents > 0 ? "#f5b940" : "#7ee2a8"
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
            Label {
                visible: w.diffs.length === 0
                text: qsTr("No differences: every invoice cost matched the system cost.")
                opacity: 0.6
            }
        }
    }
}
