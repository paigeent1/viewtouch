import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Dialogs
import QtQuick.Layouts

// Manager > Inventory > Receive retail (owner 2026-11-04 and 2026-11-27): a vendor's invoice counted into the
// retail items' on hand (PosService::recvOpen..., app/pos_receiving.cpp; the state is pos.retailReceiving).
// Receive: the vendor, invoice # and date, the total from the paper; scan with the counter scanner (a repeat
// scan is one more; a carton's UPC brings its packs), type a UPC or the vendor's item number, or part of a
// name and Find; per line the quantity in the vendor's unit, units in it, the invoice's price, OTP and prepaid
// tax; Store use lines; Delivery, Credit / return and Other amounts; the tie-out against the total. A code
// not on file: Link it to an item (its alt UPC), Add it, or Leave it out. Save never changes a system cost or
// a price. Import: an invoice file (CSV), its columns, its invoices and the items not on file, then Receive
// these invoices or History only. Invoices received: the list.
Item {
    id: w
    objectName: "retailReceiving"
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.retailReceiving : ({})
    readonly property string tab: info.tab ?? "receive"
    readonly property var imp: info.importFile ?? ({})
    readonly property real unit: Math.max(14, Math.min(width / 76, height / 34))
    // A finger's size on this screen (ZoneItem.touch: the zone's scale taken out), at the least.
    readonly property real touch: Math.max(zone ? zone.touch(48) : 48, unit * 2.6)
    property string fileError: ""
    // The quick-add card: {index} (a line) or {code} (an import's code), with what's known.
    property var adding: null
    // The import's Link: the code being linked.
    property string linkCode: ""

    // The state comes to a tablet only while a screen shows it (PosServer: on request).
    Item {
        visible: false
        readonly property var p: w.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("retailReceiving", false)
            asked = p
            if (p) { p.want("retailReceiving", true); p.recvOpen("receive") }
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) { asked.want("retailReceiving", false); asked.recvClose() }
    }

    component Big: Button {
        implicitHeight: w.touch
        font.pixelSize: w.unit
    }
    component Words: Label {
        font.pixelSize: w.unit * 0.9
        wrapMode: Text.WordWrap
    }
    component Field: TextField {
        implicitHeight: w.touch
        font.pixelSize: w.unit
        selectByMouse: true
    }
    component Amount: TextField {
        implicitHeight: w.touch
        implicitWidth: w.unit * 5.5
        font.pixelSize: w.unit
        horizontalAlignment: Text.AlignRight
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
    component Small: Label {
        font.pixelSize: w.unit * 0.7
        opacity: 0.75
    }

    function vendorIndex(id) {
        const list = w.info.vendors ?? []
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return i
        return -1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            spacing: 8
            Repeater {
                model: [["receive", qsTr("Receive invoice")], ["import", qsTr("Import invoice file…")], ["list", qsTr("Invoices received")]]
                delegate: Big {
                    required property var modelData
                    objectName: "recvTab-" + modelData[0]
                    text: modelData[1]
                    checkable: true
                    checked: w.tab === modelData[0]
                    highlighted: checked
                    onClicked: w.pos.recvOpen(modelData[0])
                }
            }
            Item { Layout.fillWidth: true }
        }
        Words {
            objectName: "recvMessage"
            visible: text !== ""
            text: w.fileError !== "" ? w.fileError : (w.info.message ?? "")
            Layout.fillWidth: true
            color: w.fileError !== "" ? "#b00020" : palette.windowText
        }

        // --- Receive an invoice ---
        ColumnLayout {
            visible: w.tab === "receive"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            Words {
                visible: (w.info.vendors ?? []).length === 0
                text: qsTr("Add the vendors first: Manager > Inventory > Vendors (name, account number).")
                color: "#b00020"
                Layout.fillWidth: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    objectName: "recvVendor"
                    implicitHeight: w.touch
                    implicitWidth: w.unit * 16
                    font.pixelSize: w.unit
                    model: w.info.vendors ?? []
                    textRole: "name"
                    displayText: currentIndex >= 0 ? currentText : qsTr("Vendor…")
                    currentIndex: w.vendorIndex(w.info.vendorId ?? "")
                    onActivated: index => w.pos.recvSetHeader({ vendor: (w.info.vendors ?? [])[index].id })
                }
                Field {
                    objectName: "recvInvoice"
                    implicitWidth: w.unit * 9
                    placeholderText: qsTr("Invoice #")
                    text: w.info.invoice ?? ""
                    onEditingFinished: if (text !== (w.info.invoice ?? "")) w.pos.recvSetHeader({ invoice: text })
                }
                Field {
                    objectName: "recvDate"
                    implicitWidth: w.unit * 8
                    placeholderText: qsTr("Date")
                    text: w.info.date ?? ""
                    onEditingFinished: if (text !== (w.info.date ?? "")) w.pos.recvSetHeader({ date: text })
                }
                Amount {
                    objectName: "recvTotal"
                    implicitWidth: w.unit * 8
                    placeholderText: qsTr("Invoice total")
                    text: w.info.total ?? ""
                    onEditingFinished: if (text !== (w.info.total ?? "")) w.pos.recvSetHeader({ total: text })
                }
                Label {
                    objectName: "recvTie"
                    height: w.touch
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: w.unit
                    font.bold: true
                    text: (w.info.tie ?? "") !== "" ? w.info.tie : qsTr("Type the total from the paper to tie out")
                    color: (w.info.tie ?? "") === "" ? palette.windowText : (w.info.ties ? "#1f8a4c" : "#b35c00")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Field {
                    id: findField
                    objectName: "recvFind"
                    Layout.fillWidth: true
                    placeholderText: (w.info.linking ?? -1) >= 0 ? qsTr("Part of the item's name, then touch it below")
                                                                  : qsTr("Scan, or type a UPC, the vendor's item # or part of a name")
                    onAccepted: { (w.info.linking ?? -1) >= 0 ? w.pos.recvSearch(text) : w.pos.recvFind(text); text = "" }
                }
                Big {
                    objectName: "recvFindGo"
                    text: qsTr("Find")
                    onClicked: { (w.info.linking ?? -1) >= 0 ? w.pos.recvSearch(findField.text) : w.pos.recvFind(findField.text); findField.text = "" }
                }
                Big { objectName: "recvAddStoreUse"; text: qsTr("+ Store use"); onClicked: w.pos.recvAddCharge("storeuse") }
                Big { objectName: "recvAddDelivery"; text: qsTr("+ Delivery"); onClicked: w.pos.recvAddCharge("delivery") }
                Big { objectName: "recvAddCredit"; text: qsTr("+ Credit / return"); onClicked: w.pos.recvAddCharge("credit") }
                Big { objectName: "recvAddOther"; text: qsTr("+ Other"); onClicked: w.pos.recvAddCharge("other") }
            }
            RowLayout {
                visible: (w.info.linking ?? -1) >= 0
                Layout.fillWidth: true
                Words { text: qsTr("Linking a code not on file: find its item and touch it."); color: "#b35c00"; Layout.fillWidth: true }
                Big { text: qsTr("Stop linking"); onClicked: w.pos.recvLinkStart(-1) }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: (w.info.choices ?? []).length > 0 && w.linkCode === ""
                Repeater {
                    model: w.linkCode === "" ? (w.info.choices ?? []) : []
                    delegate: Big {
                        required property var modelData
                        objectName: "recvChoice-" + modelData.id
                        text: modelData.name + "  " + modelData.price
                        onClicked: (w.info.linking ?? -1) >= 0 ? w.pos.recvLink(w.info.linking, modelData.id) : w.pos.recvAddItem(modelData.id, 1)
                    }
                }
            }
            ListView {
                id: lineList
                objectName: "recvLines"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: w.info.lines ?? []
                ScrollBar.vertical: TouchScrollBar { needed: lineList.contentHeight > lineList.height + 1 }
                readonly property int last: w.info.last ?? -1
                onLastChanged: if (last >= 0) Qt.callLater(() => lineList.positionViewAtIndex(last, ListView.Contain))
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    objectName: "recvLine-" + modelData.index
                    width: lineList.width - 14
                    height: lineCol.implicitHeight + 8
                    radius: 4
                    color: "transparent"
                    border.width: modelData.aside ? 2 : 1
                    border.color: modelData.aside ? (modelData.leftOut ? "#777777" : "#b35c00") : "#40808080"
                    ColumnLayout {
                        id: lineCol
                        x: 6
                        y: 4
                        width: row.width - 12
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: modelData.isItem ? modelData.name : modelData.kindText
                                font.pixelSize: w.unit
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                color: modelData.aside && !modelData.leftOut ? "#b35c00" : palette.windowText
                            }
                            Label { text: modelData.extended; font.pixelSize: w.unit; font.bold: true }
                            Big { objectName: "recvRemove-" + modelData.index; text: "✕"; implicitWidth: w.touch; onClicked: w.pos.recvRemoveLine(modelData.index) }
                        }
                        Small {
                            visible: modelData.isItem && text !== ""
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: [modelData.upc,
                                   modelData.onHand !== "" ? qsTr("on hand %1").arg(modelData.onHand) : "",
                                   modelData.systemCost !== "" ? qsTr("system cost %1").arg(modelData.systemCost) : "",
                                   modelData.unitCost !== "" ? qsTr("invoice %1 each").arg(modelData.unitCost) : "",
                                   modelData.last].filter(s => s !== "").join("  ·  ")
                            color: modelData.different ? "#b35c00" : palette.windowText
                        }
                        Flow {   // a code not on file
                            visible: modelData.aside
                            Layout.fillWidth: true
                            spacing: 6
                            Big { objectName: "recvLinkLine-" + modelData.index; text: qsTr("Link to an item…"); onClicked: w.pos.recvLinkStart(modelData.index) }
                            Big {
                                objectName: "recvAddLine-" + modelData.index
                                text: qsTr("Add it…")
                                onClicked: w.adding = { index: modelData.index, upc: modelData.upc, name: "", cost: modelData.cost, packSize: "" }
                            }
                            Big {
                                objectName: "recvLeaveOut-" + modelData.index
                                text: qsTr("Leave it out")
                                checkable: true
                                checked: modelData.leftOut
                                highlighted: checked
                                onClicked: w.pos.recvLeaveOut(modelData.index, checked)
                            }
                        }
                        Flow {   // an item's line (stock or store use)
                            visible: modelData.isItem
                            Layout.fillWidth: true
                            spacing: 6
                            Big {
                                objectName: "recvKind-" + modelData.index
                                text: modelData.kind === "storeuse" ? qsTr("Store use") : qsTr("Stock")
                                highlighted: modelData.kind === "storeuse"
                                onClicked: w.pos.recvSetLine(modelData.index, { kind: modelData.kind === "storeuse" ? "stock" : "storeuse" })
                            }
                            Field {
                                visible: modelData.kind === "storeuse" && modelData.itemId === ""
                                implicitWidth: w.unit * 12
                                placeholderText: qsTr("What it is")
                                text: modelData.name
                                onEditingFinished: if (text !== modelData.name) w.pos.recvSetLine(modelData.index, { name: text })
                            }
                            Big { text: "−"; implicitWidth: w.touch; onClicked: w.pos.recvSetLine(modelData.index, { qty: String(Math.max(0, modelData.qty - 1)) }) }
                            Amount {
                                objectName: "recvQty-" + modelData.index
                                implicitWidth: w.unit * 4
                                horizontalAlignment: Text.AlignHCenter
                                text: String(modelData.qty)
                                onEditingFinished: if (Number(text) !== modelData.qty) w.pos.recvSetLine(modelData.index, { qty: text })
                            }
                            Big { text: "+"; implicitWidth: w.touch; onClicked: w.pos.recvSetLine(modelData.index, { qty: String(modelData.qty + 1) }) }
                            Field {
                                objectName: "recvUom-" + modelData.index
                                implicitWidth: w.unit * 4.5
                                placeholderText: qsTr("Unit")
                                text: modelData.uom
                                onEditingFinished: if (text !== modelData.uom) w.pos.recvSetLine(modelData.index, { uom: text })
                            }
                            Label { text: "×"; height: w.touch; verticalAlignment: Text.AlignVCenter; font.pixelSize: w.unit }
                            Amount {
                                objectName: "recvPer-" + modelData.index
                                implicitWidth: w.unit * 3.5
                                horizontalAlignment: Text.AlignHCenter
                                text: String(modelData.perUom)
                                onEditingFinished: if (Number(text) !== modelData.perUom) w.pos.recvSetLine(modelData.index, { perUom: text })
                            }
                            Small { text: qsTr("= %1 units").arg(modelData.units); height: w.touch; verticalAlignment: Text.AlignVCenter }
                            Field {
                                objectName: "recvVendorItem-" + modelData.index
                                implicitWidth: w.unit * 6
                                placeholderText: qsTr("Vendor #")
                                text: modelData.vendorItem
                                onEditingFinished: if (text !== modelData.vendorItem) w.pos.recvSetLine(modelData.index, { vendorItem: text })
                            }
                            Amount {
                                objectName: "recvCost-" + modelData.index
                                placeholderText: qsTr("Price")
                                text: modelData.cost
                                onEditingFinished: if (text !== modelData.cost) w.pos.recvSetLine(modelData.index, { cost: text })
                            }
                            Amount {
                                objectName: "recvOtp-" + modelData.index
                                placeholderText: qsTr("OTP")
                                text: modelData.otp
                                onEditingFinished: if (text !== modelData.otp) w.pos.recvSetLine(modelData.index, { otp: text })
                            }
                            Amount {
                                objectName: "recvPrepaid-" + modelData.index
                                placeholderText: qsTr("Prepaid tax")
                                text: modelData.prepaid
                                onEditingFinished: if (text !== modelData.prepaid) w.pos.recvSetLine(modelData.index, { prepaid: text })
                            }
                            Amount {
                                visible: modelData.kind === "storeuse"
                                objectName: "recvTax-" + modelData.index
                                placeholderText: qsTr("Use tax")
                                text: modelData.tax
                                onEditingFinished: if (text !== modelData.tax) w.pos.recvSetLine(modelData.index, { tax: text })
                            }
                        }
                        RowLayout {   // an invoice amount: delivery, credit, other
                            visible: !modelData.isItem
                            Layout.fillWidth: true
                            spacing: 6
                            Field {
                                Layout.fillWidth: true
                                placeholderText: qsTr("What it is")
                                text: modelData.name
                                onEditingFinished: if (text !== modelData.name) w.pos.recvSetLine(modelData.index, { name: text })
                            }
                            Amount {
                                objectName: "recvAmount-" + modelData.index
                                placeholderText: qsTr("Amount")
                                text: modelData.amount
                                onEditingFinished: if (text !== modelData.amount) w.pos.recvSetLine(modelData.index, { amount: text })
                            }
                        }
                    }
                }
                Words {
                    anchors.centerIn: parent
                    visible: lineList.count === 0
                    text: qsTr("Scan what came in. A carton's UPC brings its packs; a repeat scan is one more.")
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 14
                readonly property var t: w.info.totals ?? ({})
                Small { text: qsTr("Stock %1 (OTP %2)").arg(parent.t.stock ?? "").arg(parent.t.otp ?? "") }
                Small { text: qsTr("Prepaid tax %1").arg(parent.t.prepaid ?? "") }
                Small { text: qsTr("Store use %1 + tax %2").arg(parent.t.storeUse ?? "").arg(parent.t.useTax ?? "") }
                Small { text: qsTr("Delivery %1").arg(parent.t.delivery ?? "") }
                Small { text: qsTr("Credits %1").arg(parent.t.credits ?? "") }
                Small { text: qsTr("Other %1").arg(parent.t.other ?? "") }
                Small { text: qsTr("%1 units to stock").arg(parent.t.units ?? "") }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Field {
                    objectName: "recvNote"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Note (optional)")
                    text: w.info.note ?? ""
                    onEditingFinished: if (text !== (w.info.note ?? "")) w.pos.recvSetHeader({ note: text })
                }
                Label { objectName: "recvLinesTotal"; text: qsTr("Lines: %1").arg((w.info.totals ?? {}).total ?? ""); font.pixelSize: w.unit; font.bold: true }
                Big { objectName: "recvClear"; text: qsTr("Clear"); enabled: (w.info.lines ?? []).length > 0; onClicked: w.pos.recvClear() }
                Big {
                    objectName: "recvSave"
                    text: qsTr("Save")
                    highlighted: true
                    enabled: (w.info.lines ?? []).length > 0
                    onClicked: w.pos.recvSave()
                }
            }
        }

        // --- Import an invoice file ---
        ColumnLayout {
            visible: w.tab === "import"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    objectName: "recvImportVendor"
                    implicitHeight: w.touch
                    implicitWidth: w.unit * 16
                    font.pixelSize: w.unit
                    model: w.info.vendors ?? []
                    textRole: "name"
                    displayText: currentIndex >= 0 ? currentText : qsTr("Vendor…")
                    currentIndex: w.vendorIndex(w.imp.loaded ? (w.imp.vendorId ?? "") : (w.info.vendorId ?? ""))
                    onActivated: index => {
                        const id = (w.info.vendors ?? [])[index].id
                        if (w.imp.loaded) w.pos.recvImportVendor(id)
                        else w.pos.recvSetHeader({ vendor: id })
                    }
                }
                Big { objectName: "recvChooseFile"; text: w.imp.loaded ? qsTr("Another file…") : qsTr("Choose invoice file…"); onClicked: fileDialog.open() }
                Words { visible: w.imp.loaded === true; text: (w.imp.fileName ?? "") + "  ·  " + qsTr("%1 rows").arg(w.imp.rows ?? 0); Layout.fillWidth: true }
                Item { visible: !w.imp.loaded; Layout.fillWidth: true }
                Big {
                    objectName: "recvModeReceive"
                    visible: w.imp.loaded === true
                    text: qsTr("Receive these invoices")
                    checkable: true
                    checked: w.imp.mode !== "history"
                    highlighted: checked
                    onClicked: w.pos.recvImportMode("receive")
                }
                Big {
                    objectName: "recvModeHistory"
                    visible: w.imp.loaded === true
                    text: qsTr("History only")
                    checkable: true
                    checked: w.imp.mode === "history"
                    highlighted: checked
                    onClicked: w.pos.recvImportMode("history")
                }
                Big { visible: w.imp.loaded === true; text: qsTr("Cancel"); onClicked: { w.fileError = ""; w.pos.recvImportCancel() } }
            }
            Words {
                visible: !w.imp.loaded
                Layout.fillWidth: true
                text: qsTr("A CSV file from the vendor (an Excel file: save it as CSV first): a header row, then one row per invoice line. "
                           + "Its columns are guessed from their names: invoice #, date, account or ship-to (routes each invoice to the vendor "
                           + "with that account number), item #, UPC, description, UOM, units per UOM, quantity, unit price, OTP / excise, "
                           + "prepaid tax, extended, line type (store use, credit, delivery), delivery / freight, use tax, invoice total. "
                           + "You can change them before importing; they're kept for the vendor's next file. "
                           + "History only loads past invoices to learn the vendor's item numbers and last costs, without changing on hand.")
            }
            // The columns.
            Flow {
                visible: w.imp.loaded === true
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: w.imp.fields ?? []
                    delegate: ColumnLayout {
                        required property var modelData
                        spacing: 0
                        Small { text: modelData.label }
                        ComboBox {
                            objectName: "recvMap-" + modelData.field
                            implicitHeight: w.touch
                            implicitWidth: w.unit * 10
                            font.pixelSize: w.unit * 0.85
                            model: [qsTr("(none)")].concat(w.imp.headers ?? [])
                            currentIndex: modelData.column + 1
                            onActivated: index => w.pos.recvImportMap(modelData.field, index - 1)
                        }
                    }
                }
            }
            Words {
                visible: w.imp.loaded === true
                Layout.fillWidth: true
                font.bold: true
                text: qsTr("%1 ready · %2 waiting for items not on file · %3 already received · %4 with a problem")
                          .arg(w.imp.ready ?? 0).arg(w.imp.waiting ?? 0).arg(w.imp.duplicates ?? 0).arg(w.imp.problems ?? 0)
            }
            RowLayout {
                visible: w.imp.loaded === true
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                ListView {
                    id: invList
                    objectName: "recvInvoices"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 3
                    clip: true
                    spacing: 3
                    model: w.imp.invoices ?? []
                    ScrollBar.vertical: TouchScrollBar { needed: invList.contentHeight > invList.height + 1 }
                    delegate: RowLayout {
                        required property var modelData
                        width: invList.width - 14
                        spacing: 6
                        Label {
                            text: modelData.date + "  " + (modelData.vendor !== "" ? modelData.vendor : modelData.account) + "  #" + modelData.invoice
                                  + "  ·  " + qsTr("%1 lines").arg(modelData.lines) + "  ·  " + modelData.total + "  ·  " + modelData.tie
                            font.pixelSize: w.unit * 0.85
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "recvInvStatus-" + modelData.index
                            text: modelData.status
                            font.pixelSize: w.unit * 0.85
                            font.bold: true
                            color: modelData.ready ? "#1f8a4c" : "#b35c00"
                        }
                        Big { text: qsTr("Lines"); implicitHeight: w.touch; onClicked: w.pos.recvImportShow(modelData.index) }
                    }
                }
                ListView {
                    id: codeList
                    objectName: "recvUnmatched"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 2
                    clip: true
                    spacing: 3
                    model: w.imp.unmatched ?? []
                    ScrollBar.vertical: TouchScrollBar { needed: codeList.contentHeight > codeList.height + 1 }
                    header: Words {
                        width: codeList.width - 14
                        font.bold: true
                        text: (w.imp.unmatchedCount ?? 0) > 0 ? qsTr("Not on file (%1): link, add, store use or skip").arg(w.imp.unmatchedCount)
                                                               : qsTr("Every item is on file.")
                    }
                    delegate: ColumnLayout {
                        required property var modelData
                        width: codeList.width - 14
                        spacing: 2
                        Label {
                            text: modelData.description + "  ·  " + [modelData.upc, modelData.itemNo !== "" ? "#" + modelData.itemNo : "", modelData.cost]
                                  .filter(s => s !== "").join("  ·  ") + "  ·  " + qsTr("%1 rows").arg(modelData.rows)
                            font.pixelSize: w.unit * 0.85
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Big { objectName: "recvCodeLink-" + modelData.code; implicitHeight: w.touch; text: qsTr("Link…"); onClicked: { w.linkCode = modelData.code; w.pos.recvSearch("") } }
                            Big {
                                objectName: "recvCodeAdd-" + modelData.code
                                implicitHeight: w.touch
                                text: qsTr("Add…")
                                onClicked: w.adding = { code: modelData.code, upc: modelData.upc, name: modelData.description, cost: modelData.cost.replace(/[^0-9.]/g, ""), packSize: "" }
                            }
                            Big { objectName: "recvCodeStoreUse-" + modelData.code; implicitHeight: w.touch; text: qsTr("Store use"); onClicked: w.pos.recvImportDecide(modelData.code, "storeuse", "") }
                            Big { objectName: "recvCodeSkip-" + modelData.code; implicitHeight: w.touch; text: qsTr("Skip"); onClicked: w.pos.recvImportDecide(modelData.code, "skip", "") }
                        }
                    }
                    footer: ColumnLayout {
                        width: codeList.width - 14
                        spacing: 2
                        Words { visible: (w.imp.decided ?? []).length > 0; text: qsTr("Decided"); font.bold: true }
                        Repeater {
                            model: w.imp.decided ?? []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Small {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.code + ": " + (modelData.action === "link" ? qsTr("is %1").arg(modelData.item)
                                                                   : modelData.action === "storeuse" ? qsTr("store use") : qsTr("skipped"))
                                }
                                Big { implicitHeight: w.touch; text: qsTr("Undo"); onClicked: w.pos.recvImportDecide(modelData.code, "", "") }
                            }
                        }
                    }
                }
            }
            RowLayout {
                visible: w.imp.loaded === true
                Layout.fillWidth: true
                spacing: 8
                Words {
                    Layout.fillWidth: true
                    text: w.imp.mode === "history" ? qsTr("History only: item numbers and last costs are kept; on hand doesn't change.")
                                                   : qsTr("Receive: each ready invoice's stock goes into on hand. System costs and prices never change.")
                }
                Big {
                    objectName: "recvImportCommit"
                    text: w.imp.mode === "history" ? qsTr("Load %1 invoices as history").arg(w.imp.ready ?? 0)
                                                   : qsTr("Receive %1 invoices").arg(w.imp.ready ?? 0)
                    highlighted: true
                    enabled: (w.imp.ready ?? 0) > 0
                    onClicked: w.pos.recvImportCommit()
                }
            }
        }

        // --- Invoices received ---
        ColumnLayout {
            visible: w.tab === "list"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                spacing: 8
                Label { text: qsTr("From"); font.pixelSize: w.unit }
                Field { id: fromField; objectName: "recvFrom"; implicitWidth: w.unit * 8; placeholderText: "2026-11-01"; text: w.info.from ?? "" }
                Label { text: qsTr("to"); font.pixelSize: w.unit }
                Field { id: untilField; objectName: "recvUntil"; implicitWidth: w.unit * 8; placeholderText: "2026-11-30"; text: w.info.until ?? "" }
                Big { text: qsTr("Show"); onClicked: w.pos.recvRange(fromField.text, untilField.text) }
                Words { text: qsTr("By invoice date; empty: the last 60 days. Reports > Retail Purchases and Retail Cost Diffs print them."); Layout.fillWidth: true }
            }
            ListView {
                id: recentList
                objectName: "recvRecent"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 3
                model: w.info.recent ?? []
                ScrollBar.vertical: TouchScrollBar { needed: recentList.contentHeight > recentList.height + 1 }
                delegate: Label {
                    required property var modelData
                    width: recentList.width - 14
                    font.pixelSize: w.unit * 0.9
                    elide: Text.ElideRight
                    text: modelData.date + "  " + modelData.vendor + "  #" + modelData.invoice + "  ·  " + qsTr("%1 lines").arg(modelData.lines)
                          + "  ·  " + modelData.total + (modelData.tie !== "" ? "  (" + modelData.tie + ")" : "")
                          + "  ·  " + (modelData.history ? qsTr("history only") : qsTr("%1 units").arg(modelData.units))
                          + "  ·  " + modelData.by
                }
                Words {
                    anchors.centerIn: parent
                    visible: recentList.count === 0
                    text: qsTr("No invoices received in these dates.")
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("An invoice file saved as CSV")
        nameFilters: [qsTr("Spreadsheets saved as CSV (*.csv *.txt *.tsv)"), qsTr("All files (*)")]
        onAccepted: {
            const read = w.zone && w.zone.controller ? w.zone.controller.readCsvFile(selectedFile) : { error: qsTr("Can't read files here.") }
            w.fileError = read.error ?? ""
            if (!read.error) w.pos.recvImportText(read.text, String(selectedFile).split("/").pop())
        }
    }

    // The import's Link: find the item a code is.
    Rectangle {
        objectName: "recvLinkView"
        anchors.fill: parent
        visible: w.linkCode !== ""
        color: "#14181e"
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Label { text: qsTr("Which item is %1?").arg(w.linkCode); font.pixelSize: w.unit * 1.2; font.bold: true; color: "white" }
            RowLayout {
                Layout.fillWidth: true
                Field {
                    id: linkField
                    objectName: "recvLinkFind"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Part of its name, or its UPC")
                    onAccepted: w.pos.recvSearch(text)
                }
                Big { text: qsTr("Find"); onClicked: w.pos.recvSearch(linkField.text) }
                Big { text: qsTr("Cancel"); onClicked: w.linkCode = "" }
            }
            Flow {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Repeater {
                    model: w.linkCode !== "" ? (w.info.choices ?? []) : []
                    delegate: Big {
                        required property var modelData
                        objectName: "recvLinkChoice-" + modelData.id
                        text: modelData.name + "  " + modelData.upc
                        onClicked: { w.pos.recvImportDecide(w.linkCode, "link", modelData.id); w.linkCode = "" }
                    }
                }
            }
        }
    }

    // An invoice's lines (the import).
    Rectangle {
        objectName: "recvShowView"
        anchors.fill: parent
        visible: w.tab === "import" && (w.imp.show ?? -1) >= 0
        color: "#14181e"
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    color: "white"
                    font.pixelSize: w.unit * 1.1
                    font.bold: true
                    text: {
                        const i = (w.imp.invoices ?? [])[w.imp.show ?? -1]
                        return i ? i.date + "  " + i.vendor + "  #" + i.invoice + "  ·  " + i.total + "  ·  " + i.tie : ""
                    }
                }
                Big { text: qsTr("Close"); onClicked: w.pos.recvImportShow(-1) }
            }
            ListView {
                id: shownList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: w.imp.shown ?? []
                ScrollBar.vertical: TouchScrollBar { needed: shownList.contentHeight > shownList.height + 1 }
                delegate: Label {
                    required property var modelData
                    width: shownList.width - 14
                    color: modelData.found || modelData.kind !== qsTr("Stock") ? "white" : "#ffb060"
                    font.pixelSize: w.unit * 0.85
                    elide: Text.ElideRight
                    text: modelData.kind + "  " + modelData.name + "  " + [modelData.upc, modelData.vendorItem].filter(s => s !== "").join(" / ")
                          + "  ·  " + modelData.qty + " " + modelData.uom + " × " + modelData.perUom + "  @ " + modelData.cost
                          + "  =  " + modelData.extended
                          + (modelData.unitCost !== "" ? "  ·  " + qsTr("invoice %1 each").arg(modelData.unitCost) : "")
                          + (modelData.systemCost !== "" ? "  ·  " + qsTr("system cost %1").arg(modelData.systemCost) : "")
                          + (modelData.leftOut ? "  ·  " + qsTr("skipped") : "")
                }
            }
        }
    }

    // Quick add: a new retail item for a code not on file.
    Rectangle {
        objectName: "recvAddView"
        anchors.fill: parent
        visible: w.adding !== null
        color: "#14181e"
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, w.unit * 40)
            spacing: 8
            Label { text: qsTr("Add a new retail item"); font.pixelSize: w.unit * 1.2; font.bold: true; color: "white" }
            Field { id: addUpc; objectName: "recvNewUpc"; Layout.fillWidth: true; placeholderText: qsTr("UPC"); text: w.adding ? (w.adding.upc ?? "") : "" }
            Field { id: addName; objectName: "recvNewName"; Layout.fillWidth: true; placeholderText: qsTr("Name"); text: w.adding ? (w.adding.name ?? "") : "" }
            ComboBox {
                id: addCategory
                objectName: "recvNewCategory"
                Layout.fillWidth: true
                implicitHeight: w.touch
                font.pixelSize: w.unit
                model: w.info.categories ?? []
                textRole: "name"
                displayText: currentIndex >= 0 ? currentText : qsTr("Category…")
            }
            RowLayout {
                spacing: 8
                Amount { id: addCost; objectName: "recvNewCost"; placeholderText: qsTr("Cost"); text: w.adding ? (w.adding.cost ?? "") : "" }
                Amount { id: addPrice; objectName: "recvNewPrice"; placeholderText: qsTr("Price") }
                Amount { id: addPack; objectName: "recvNewPack"; placeholderText: qsTr("Pack size"); text: w.adding ? (w.adding.packSize ?? "") : "" }
            }
            Words { color: "white"; Layout.fillWidth: true; text: qsTr("Pack size: how many of the item one unit on the invoice is (a box of 20: 20). Empty: 1.") }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Big { text: qsTr("Cancel"); onClicked: w.adding = null }
                Big {
                    objectName: "recvNewAdd"
                    text: qsTr("Add")
                    highlighted: true
                    onClicked: {
                        const cats = w.info.categories ?? []
                        const r = { upc: addUpc.text, name: addName.text, cost: addCost.text, price: addPrice.text,
                                    packSize: Number(addPack.text) || 0,
                                    category: addCategory.currentIndex >= 0 && cats[addCategory.currentIndex] ? cats[addCategory.currentIndex].id : "" }
                        if (w.adding.index !== undefined) r.index = w.adding.index
                        if (w.adding.code !== undefined) r.code = w.adding.code
                        w.pos.recvQuickAdd(r)
                        w.adding = null
                        addPrice.text = ""
                    }
                }
            }
        }
    }
}
