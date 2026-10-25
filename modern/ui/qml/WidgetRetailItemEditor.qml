import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

Rectangle {
    id: root
    anchors.fill: parent
    color: "#12161c"
    readonly property real s: Math.min(Math.max(root.width / 1920.0, 0.65), 1.0)
    readonly property int fSize: Math.round(14 * s)
    readonly property int hBtn: Math.round(42 * s)
    property var draft: ({})
    property string sortMode: "alpha"
    signal backRequested()
    signal saveRequested(var item)

    function margin(p, c) {
        p = parseFloat(p) || 0; c = parseFloat(c) || 0
        return p > 0 ? (((p - c) / p) * 100).toFixed(2) + "%" : "0.00%"
    }
    function subCost(c, n) {
        c = parseFloat(c) || 0; n = parseInt(n) || 1
        return (c / Math.max(1, n)).toFixed(4)
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Math.round(10 * root.s); spacing: Math.round(8 * root.s)
        Rectangle {
            Layout.fillWidth: true; implicitHeight: root.hBtn + 10; color: "#1a212b"; radius: 6
            RowLayout {
                anchors.fill: parent; anchors.margins: 6; spacing: 8
                Button { text: "‹ Back"; implicitHeight: root.hBtn; font.pixelSize: root.fSize; onClicked: root.backRequested() }
                Label { text: "RETAIL WORKSTATION: " + (root.draft.name || "PAC TWYST CAFROY TRP 5X38 BX25"); color: "#fff"; font.bold: true; font.pixelSize: root.fSize + 2; Layout.fillWidth: true; elide: Text.ElideRight }
                Button {
                    text: root.sortMode === "alpha" ? "🔤 Sort: Alpha" : "🔢 Sort: UPC"
                    implicitHeight: root.hBtn; font.pixelSize: root.fSize
                    palette.button: root.sortMode === "upc" ? "#2d4a6f" : "#343d4d"
                    onClicked: root.sortMode = (root.sortMode === "alpha" ? "upc" : "alpha")
                }
                Button { text: "Save & Apply (F10)"; implicitHeight: root.hBtn; font.pixelSize: root.fSize; font.bold: true; palette.button: "#1f5f3a"; palette.buttonText: "#fff"; onClicked: root.saveRequested(root.draft) }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: Math.round(8 * root.s)
            Rectangle {
                Layout.preferredWidth: parent.width * 0.28; Layout.fillHeight: true; color: "#181e26"; radius: 6
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 4
                    Label { text: "5-LEVEL MERCHANDISE TREE"; color: "#9aa3b0"; font.bold: true; font.pixelSize: root.fSize }
                    TextField { Layout.fillWidth: true; implicitHeight: root.hBtn - 6; placeholderText: "Filter tree..."; font.pixelSize: root.fSize }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        ColumnLayout {
                            width: parent.width; spacing: 3
                            Text { text: "▼ [L1] CIGARS"; color: "#6cc58a"; font.bold: true; font.pixelSize: root.fSize }
                            Text { text: "  ▼ [L2] PREM CIGARS"; color: "#6c9ce0"; font.bold: true; font.pixelSize: root.fSize }
                            Text { text: "    ▼ [L3] VENTURA"; color: "#f2d16b"; font.bold: true; font.pixelSize: root.fSize }
                            Text { text: "      ▼ [L4] PACIFIC TWYST"; color: "#ec8fc0"; font.pixelSize: root.fSize }
                            Text { text: "        ▼ [L5] CAFROY TORPEDO"; color: "#fff"; font.bold: true; font.pixelSize: root.fSize }
                            Text { text: "          • Box 25 (Parent)"; color: "#6cc58a"; font.pixelSize: root.fSize }
                            Text { text: "          • Single (Subunit)"; color: "#9aa3b0"; font.pixelSize: root.fSize }
                            Text { text: "  ► [L2] MASS MARKET"; color: "#777"; font.pixelSize: root.fSize }
                            Text { text: "► [L1] PACKAGED BEVERAGES"; color: "#777"; font.pixelSize: root.fSize }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: "#181e26"; radius: 6
                ScrollView {
                    anchors.fill: parent; anchors.margins: 10; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: Math.round(6 * root.s)
                        Label { text: "ITEM PROPERTIES & BARCODES"; color: "#9aa3b0"; font.bold: true; font.pixelSize: root.fSize }
                                                // 3-Way Item Classification: F&B vs Retail vs Event
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Button {
                                text: qsTr("🍽️ F&B")
                                Layout.fillWidth: true
                                implicitHeight: root.hBtn
                                font.pixelSize: root.fSize - 1
                                font.bold: !Boolean(root.draft.isRetail) && !Boolean(root.draft.isEvent)
                                palette.button: (!Boolean(root.draft.isRetail) && !Boolean(root.draft.isEvent)) ? "#5c3b1d" : "#222933"
                                onClicked: {
                                    root.draft.isRetail = false
                                    root.draft.isEvent = false
                                    root.draft.taxClass = "food"
                                    root.draft.printer = "kitchen"
                                }
                            }

                            Button {
                                text: qsTr("🏷️ Retail")
                                Layout.fillWidth: true
                                implicitHeight: root.hBtn
                                font.pixelSize: root.fSize - 1
                                font.bold: Boolean(root.draft.isRetail)
                                palette.button: Boolean(root.draft.isRetail) ? "#1f5f3a" : "#222933"
                                onClicked: {
                                    root.draft.isRetail = true
                                    root.draft.isEvent = false
                                    root.draft.taxClass = "merchandise"
                                    root.draft.printer = ""
                                }
                            }

                            Button {
                                text: qsTr("🎟️ Event")
                                Layout.fillWidth: true
                                implicitHeight: root.hBtn
                                font.pixelSize: root.fSize - 1
                                font.bold: Boolean(root.draft.isEvent)
                                palette.button: Boolean(root.draft.isEvent) ? "#5e2b6b" : "#222933"
                                onClicked: {
                                    root.draft.isRetail = false
                                    root.draft.isEvent = true
                                    root.draft.taxClass = "room"
                                    root.draft.printer = ""
                                }
                            }
                        }
                        Label { text: "Description:"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                        TextField { Layout.fillWidth: true; implicitHeight: root.hBtn; text: root.draft.name || "PAC TWYST CAFROY TRP 5X38 BX25"; font.pixelSize: root.fSize; onTextChanged: root.draft.name = text }
                        Label { text: "Primary UPC:"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { Layout.fillWidth: true; implicitHeight: root.hBtn; text: root.draft.upc || "751667292306"; font.pixelSize: root.fSize; onTextChanged: root.draft.upc = text }
                            Rectangle { width: 65 * root.s; height: root.hBtn; color: "#1f5f3a"; radius: 4; Text { anchors.centerIn: parent; text: "✓ GS1"; color: "#fff"; font.pixelSize: root.fSize - 2 } }
                        }
                        Label { text: "Alternate PLU / UPC-E:"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                        TextField { Layout.fillWidth: true; implicitHeight: root.hBtn; text: root.draft.altUpc || ""; font.pixelSize: root.fSize; onTextChanged: root.draft.altUpc = text }
                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox { text: "Print Shelf Barcode"; font.pixelSize: root.fSize; checked: true }
                            CheckBox { text: "POS Active Flag"; font.pixelSize: root.fSize; checked: true }
                        }
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: parent.width * 0.34; Layout.fillHeight: true; color: "#181e26"; radius: 6
                ScrollView {
                    anchors.fill: parent; anchors.margins: 10; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: Math.round(6 * root.s)
                        Label { text: "PRICING & SUBUNIT BREAKOUT"; color: "#9aa3b0"; font.bold: true; font.pixelSize: root.fSize }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: "Retail Price ($):"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                                TextField { id: pF; Layout.fillWidth: true; implicitHeight: root.hBtn; text: root.draft.price || "119.99"; font.pixelSize: root.fSize; onTextChanged: root.draft.price = text }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: "Unit Cost ($):"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                                TextField { id: cF; Layout.fillWidth: true; implicitHeight: root.hBtn; text: root.draft.cost || "59.2900"; font.pixelSize: root.fSize; onTextChanged: root.draft.cost = text }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 38 * root.s; color: "#202833"; radius: 4
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 6
                                Text { text: "Gross Margin:"; color: "#9aa3b0"; font.pixelSize: root.fSize }
                                Text { text: root.margin(pF.text, cF.text); color: "#6cc58a"; font.bold: true; font.pixelSize: root.fSize + 2 }
                            }
                        }
                        Label { text: "PARENT ↔ SUBUNIT BREAKOUT"; color: "#9aa3b0"; font.bold: true; font.pixelSize: root.fSize }
                        RadioButton { text: "Parent Master (Box / Carton)"; checked: true; font.pixelSize: root.fSize }
                        RadioButton { text: "Child Subunit (Single Stick / Pack)"; font.pixelSize: root.fSize }
                        Label { text: "Linked Subunit UPC:"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                        TextField { id: sUpc; Layout.fillWidth: true; implicitHeight: root.hBtn; text: "751667292313"; font.pixelSize: root.fSize }
                        Label { text: "Subunits per Box / Carton:"; color: "#fff"; font.pixelSize: root.fSize - 1 }
                        TextField { id: pSz; Layout.fillWidth: true; implicitHeight: root.hBtn; text: "25"; font.pixelSize: root.fSize }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 36 * root.s; color: "#1a2430"; radius: 4
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 6
                                Text { text: "Subunit Cost:"; color: "#9aa3b0"; font.pixelSize: root.fSize - 1 }
                                Text { text: "$" + root.subCost(cF.text, pSz.text); color: "#6cc58a"; font.bold: true; font.pixelSize: root.fSize }
                            }
                        }
                        Button { text: "🖨️ Print Label (Bixolon/Dymo)"; Layout.fillWidth: true; implicitHeight: root.hBtn; font.pixelSize: root.fSize }
                    }
                }
            }
        }
    }
}
