import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Look Up Checks (Cashier Duty), laid out like the old ILSA receipt lookup: the receipts on the right,
// newest first (receipt #, when, total); the one touched shown on the left as it printed; Print Receipt,
// Print Gift Receipt (no prices), Cancel. props.days: how far back it starts (default 1: today).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.checkSearch : ({})
    readonly property var found: (info.results ?? []).filter(r => r.status !== "open")   // receipts: closed checks
    readonly property var check: info.selected ?? null
    property int days: zone && zone.props && zone.props.days !== undefined ? zone.props.days : 1
    property var offsets: [0]
    property int index: -1
    function load(offset) { w.pos.searchChecks("", days, offset) }
    function choose(i) {
        if (i < 0 || i >= found.length) return
        index = i
        list.positionViewAtIndex(i, ListView.Contain)
        w.pos.selectFoundCheck(found[i].id)
    }
    onFoundChanged: if (found.length && (index < 0 || index >= found.length)) choose(0)
    Component.onCompleted: if (pos) { offsets = [0]; load(0) }
    readonly property real unit: Math.max(14, Math.min(width / 50, height / 26))

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        // --- the receipt, as it printed ---
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: w.width * 0.42
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#f7f9fc"
                border.color: "#9aa5b5"
                radius: 4
                Flickable {
                    id: paper
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentHeight: receiptText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    Text {
                        id: receiptText
                        objectName: "lookupReceipt"
                        width: paper.width
                        text: w.check ? (w.check.receiptText || "") : (w.found.length ? "" : qsTr("No receipts in this range."))
                        font.family: "DejaVu Sans Mono"
                        font.pixelSize: Math.max(12, w.unit * 0.62)
                        color: "#3b2a6b"
                        wrapMode: Text.NoWrap
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Button { text: "▲"; implicitWidth: w.unit * 5; implicitHeight: w.unit * 3; font.pixelSize: w.unit * 1.4
                         onClicked: paper.contentY = Math.max(0, paper.contentY - paper.height * 0.8) }
                Button { text: "▼"; implicitWidth: w.unit * 5; implicitHeight: w.unit * 3; font.pixelSize: w.unit * 1.4
                         onClicked: paper.contentY = Math.min(Math.max(0, paper.contentHeight - paper.height), paper.contentY + paper.height * 0.8) }
            }
        }

        // --- the receipts ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [[1, qsTr("Today")], [7, qsTr("Week")], [31, qsTr("Month")], [365, qsTr("Year")], [0, qsTr("All")]]
                    delegate: Button {
                        required property var modelData
                        text: modelData[1]
                        checkable: true
                        checked: w.days === modelData[0]
                        implicitHeight: w.unit * 2.4
                        font.pixelSize: w.unit * 0.8
                        onClicked: { w.days = modelData[0]; w.offsets = [0]; w.index = -1; w.load(0) }
                    }
                }
                Button {
                    // The receipts imported from ILSA (read-only), over this screen.
                    objectName: "lookupIlsa"
                    text: qsTr("ILSA history")
                    implicitHeight: w.unit * 2.4
                    font.pixelSize: w.unit * 0.8
                    palette.button: "#6b4e16"; palette.buttonText: "white"
                    onClicked: w.ilsa = true
                }
                Item { Layout.fillWidth: true }
                Button { text: qsTr("‹ Newer"); implicitHeight: w.unit * 2.4; font.pixelSize: w.unit * 0.8; enabled: w.offsets.length > 1
                         onClicked: { const o = w.offsets.slice(0, -1); w.offsets = o; w.index = -1; w.load(o[o.length - 1]) } }
                Button { text: qsTr("Older ›"); implicitHeight: w.unit * 2.4; font.pixelSize: w.unit * 0.8; enabled: w.info.more === true
                         onClicked: { const o = w.offsets.concat([w.info.nextOffset ?? 0]); w.offsets = o; w.index = -1; w.load(o[o.length - 1]) } }
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: w.found
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: w.unit * 3.4
                    radius: 3
                    color: row.index === w.index ? "#1d3fb8" : "#8f8a1c"
                    border.color: "#3a3a10"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: w.unit
                        anchors.rightMargin: w.unit
                        ColumnLayout {
                            spacing: 0
                            Label { text: qsTr("Rcpt #  %1").arg(row.modelData.id); color: row.index === w.index ? "#ffe066" : "white"; font.pixelSize: w.unit * 1.05; font.bold: true }
                            Label { text: qsTr("Total  %1").arg(row.modelData.total) + (row.modelData.refunded ? qsTr("  (refund)") : ""); color: "white"; font.pixelSize: w.unit * 0.95 }
                        }
                        Item { Layout.fillWidth: true }
                        Label { text: row.modelData.when; color: row.index === w.index ? "#ffe066" : "white"; font.pixelSize: w.unit * 0.95; font.bold: true }
                    }
                    TapHandler { onTapped: w.choose(row.index) }
                }
                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: w.info.loading ? qsTr("Looking…") : qsTr("No receipts in this range.")
                    font.pixelSize: w.unit
                    opacity: 0.7
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    text: qsTr("Cancel")
                    implicitWidth: w.unit * 6; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                    palette.button: "#b42318"; palette.buttonText: "white"
                    onClicked: w.zone.controller.goBack()
                }
                Button { text: "▲"; implicitWidth: w.unit * 5; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit * 1.4; onClicked: w.choose(w.index - 1) }
                Button { text: "▼"; implicitWidth: w.unit * 5; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit * 1.4; onClicked: w.choose(w.index + 1) }
                Item { Layout.fillWidth: true }
                Button {
                    // Reprint: which copy (the customer's, a gift receipt, or the store's with the date of birth).
                    objectName: "lookupPrint"
                    text: qsTr("Reprint…")
                    implicitWidth: w.unit * 9; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit * 1.05
                    palette.button: "#1f8a4c"; palette.buttonText: "white"
                    enabled: w.check !== null
                    onClicked: whichCopy.open()
                }
            }
        }
    }

    // ILSA history: the imported ILSA receipts, read-only (WidgetLegacyReceipts).
    property bool ilsa: false
    WidgetLegacyReceipts {
        anchors.fill: parent
        z: 5
        visible: w.ilsa
        zone: w.zone
        onClosed: w.ilsa = false
    }

    // Which copy to reprint.
    Popup {
        id: whichCopy
        objectName: "lookupWhichCopy"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: w.unit
        width: Math.min(w.width * 0.8, w.unit * 26)
        contentItem: ColumnLayout {
            spacing: w.unit * 0.6
            Label {
                text: w.check ? qsTr("Reprint receipt #%1").arg(w.check.id) : ""
                font.pixelSize: w.unit * 1.1; font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Button {
                objectName: "lookupCustomerCopy"
                Layout.fillWidth: true; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                text: qsTr("Customer copy  (no date of birth)")
                palette.button: "#1f8a4c"; palette.buttonText: "white"
                onClicked: { whichCopy.close(); w.pos.reprintCheck(w.check.id, false, false) }
            }
            Button {
                objectName: "lookupGift"
                Layout.fillWidth: true; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                text: qsTr("Gift receipt  (no prices)")
                palette.button: "#2b62b0"; palette.buttonText: "white"
                onClicked: { whichCopy.close(); w.pos.reprintCheck(w.check.id, true, false) }
            }
            Button {
                objectName: "lookupDob"
                visible: w.check !== null && w.check.hasDob === true
                Layout.fillWidth: true; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                text: qsTr("With date of birth  (store's copy)")
                palette.button: "#7c2d12"; palette.buttonText: "white"
                onClicked: { whichCopy.close(); w.pos.reprintCheck(w.check.id, false, true) }
            }
            Button {
                Layout.fillWidth: true; implicitHeight: w.unit * 2.6; font.pixelSize: w.unit * 0.9
                text: qsTr("Cancel")
                onClicked: whichCopy.close()
            }
        }
    }
}
