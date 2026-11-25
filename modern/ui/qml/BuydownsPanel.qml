import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Dialogs
import QtQuick.Layouts

// Menu Builder > Mass Updates > Buydowns / Promotions (owner 2026-11-25, after ILSA's "Buydown and Promotional
// Sales Definition" screen; PosService::buydownsOpen, app/pos_buydowns.cpp). The definitions list (ID, type,
// name, $ amount, style, qty used, start, end, coming / running / ended), then Add, Edit, Copy to New (last
// period's, then the new name, dates and amount), Delete, Print (the chosen one, or the ticked ones; Print all
// only when asked), the Sold report, a file for another store. Editing: the fields, the items with their
// Part/Ctn, price, $ amount and sale price; items come from Mass Updates' ticks (Find, a category, the brand
// tree), "Add items that start with", or a UPC. Apply saves (an admin, or an admin's PIN); Abort drops it.
Rectangle {
    id: p
    objectName: "buydownsPanel"
    property var pos: null
    property real unit: 16
    property real touch: Math.max(44, unit * 2.6)
    readonly property var info: pos ? pos.buydowns : ({})
    readonly property var list: info.list ?? []
    readonly property var draft: info.draft ?? null
    readonly property var report: info.report ?? null
    property string selectedId: ""
    property var markedDefs: []            // definitions ticked for printing several
    property var markedItems: []       // items ticked in the editor (to take out)
    property string fileError: ""
    signal pickItems()                 // back to Mass Updates' grid to tick items
    signal closed()
    color: "#f8fafc"

    function selected() { return list.find(d => d.id === selectedId) ?? null }
    function toggle(arr, v) { return arr.indexOf(v) >= 0 ? arr.filter(x => x !== v) : arr.concat([v]) }
    function printIds() { return markedDefs.length > 0 ? markedDefs : (selectedId !== "" ? [selectedId] : []) }
    function set(k, v) { const o = {}; o[k] = v; if (pos) pos.buydownSet(o) }

    component Small: Button {
        property real u: 16
        implicitHeight: Math.max(44, u * 2.6)
        Layout.minimumWidth: 48
        font.pixelSize: u * 0.8
    }
    component Field: TextField {
        property real u: 16
        // The value as the main computer has it: shown again unless it's being typed in.
        property string serverValue: ""
        onServerValueChanged: if (!activeFocus) text = serverValue
        Component.onCompleted: text = serverValue
        implicitHeight: Math.max(44, u * 2.6)
        font.pixelSize: u * 0.8
    }
    component Head: Label {
        property real u: 16
        font.pixelSize: u * 0.7
        font.bold: true
        color: "#374151"
        elide: Text.ElideRight
    }
    component Cell: Label {
        property real u: 16
        font.pixelSize: u * 0.72
        color: "#111827"
        elide: Text.ElideRight
    }

    MouseArea { anchors.fill: parent }   // (touches don't reach the grid under it)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: p.unit * 0.6
        spacing: 6

        // --- the top: what this is, new ones, the store's tax setting ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Buydowns / Promotions")
                font.pixelSize: p.unit * 1.1
                font.bold: true
                color: "#111827"
            }
            Small { u: p.unit; objectName: "buydownAddBuydown"; text: qsTr("Add Buydown"); enabled: !p.draft; onClicked: p.pos.buydownNew("buydown") }
            Small { u: p.unit; objectName: "buydownAddPromo"; text: qsTr("Add Promo"); enabled: !p.draft; onClicked: p.pos.buydownNew("promo") }
            Small { u: p.unit; objectName: "buydownImport"; text: qsTr("Import…"); enabled: !p.draft; onClicked: importFile.open() }
            CheckBox {
                objectName: "buydownStoreTax"
                text: qsTr("Buydowns: tax on the price before the buydown")
                font.pixelSize: p.unit * 0.75
                checked: p.info.storeTaxBefore === true
                onClicked: p.pos.buydownSetStoreTax(checked)
            }
            Item { Layout.fillWidth: true }
            Small { u: p.unit; objectName: "buydownPickItems"; visible: !!p.draft; text: qsTr("Tick items in Mass Updates"); onClicked: p.pickItems() }
            Small { u: p.unit; objectName: "buydownClose"; text: qsTr("Close"); onClicked: p.closed() }
        }
        Label {
            objectName: "buydownMessage"
            visible: text !== ""
            Layout.fillWidth: true
            text: p.fileError !== "" ? p.fileError : (p.info.message ?? "")
            font.pixelSize: p.unit * 0.8
            color: "#b45309"
            wrapMode: Text.WordWrap
        }

        // --- the definitions ---
        ColumnLayout {
            visible: !p.draft
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Head { u: p.unit; text: ""; Layout.preferredWidth: p.unit * 2 }
                Head { u: p.unit; text: qsTr("ID"); Layout.preferredWidth: p.unit * 3 }
                Head { u: p.unit; text: qsTr("Type"); Layout.preferredWidth: p.unit * 4 }
                Head { u: p.unit; text: qsTr("Name"); Layout.fillWidth: true }
                Head { u: p.unit; text: qsTr("$ Amount"); Layout.preferredWidth: p.unit * 4.5 }
                Head { u: p.unit; text: qsTr("Style"); Layout.preferredWidth: p.unit * 8 }
                Head { u: p.unit; text: qsTr("Qty"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("Start"); Layout.preferredWidth: p.unit * 8 }
                Head { u: p.unit; text: qsTr("End"); Layout.preferredWidth: p.unit * 8 }
                Head { u: p.unit; text: qsTr("Now"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("Items"); Layout.preferredWidth: p.unit * 3 }
            }
            ListView {
                id: defs
                objectName: "buydownList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                model: p.list
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "buydownRow-" + modelData.id
                    width: ListView.view.width
                    height: p.touch
                    color: modelData.id === p.selectedId ? "#dbeafe" : (index % 2 ? "#ffffff" : "#f1f5f9")
                    TapHandler { onTapped: p.selectedId = row.modelData.id }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        spacing: 4
                        CheckBox {
                            objectName: "buydownTick-" + row.modelData.id
                            Layout.preferredWidth: p.unit * 2
                            checked: p.markedDefs.indexOf(row.modelData.id) >= 0
                            onClicked: p.markedDefs = p.toggle(p.markedDefs, row.modelData.id)
                        }
                        Cell { u: p.unit; text: row.modelData.number; Layout.preferredWidth: p.unit * 3 }
                        Cell { u: p.unit; text: row.modelData.typeLabel; Layout.preferredWidth: p.unit * 4 }
                        Cell {
                            u: p.unit
                            Layout.fillWidth: true
                            text: row.modelData.name + (row.modelData.overlap ? "  ⚠" : "")
                            font.bold: true
                        }
                        Cell { u: p.unit; text: row.modelData.amountText; Layout.preferredWidth: p.unit * 4.5 }
                        Cell { u: p.unit; text: row.modelData.styleLabel; Layout.preferredWidth: p.unit * 8 }
                        Cell { u: p.unit; text: row.modelData.capped ? row.modelData.usedText : String(row.modelData.qty); Layout.preferredWidth: p.unit * 5 }
                        Cell { u: p.unit; text: row.modelData.startText; Layout.preferredWidth: p.unit * 8 }
                        Cell { u: p.unit; text: row.modelData.style === 2 ? qsTr("(no end)") : row.modelData.endText; Layout.preferredWidth: p.unit * 8 }
                        Cell {
                            u: p.unit
                            text: row.modelData.stateLabel
                            Layout.preferredWidth: p.unit * 5
                            font.bold: row.modelData.state === "running"
                            color: row.modelData.state === "running" ? "#15803d" : row.modelData.state === "coming" ? "#1d4ed8" : "#6b7280"
                        }
                        Cell { u: p.unit; text: row.modelData.items; Layout.preferredWidth: p.unit * 3 }
                    }
                }
                Label {
                    visible: p.list.length === 0
                    anchors.centerIn: parent
                    text: qsTr("No buydowns yet: touch Add Buydown.")
                    font.pixelSize: p.unit * 0.9
                    color: "#6b7280"
                }
            }
            Label {
                visible: p.list.some(d => d.overlap)
                text: qsTr("⚠ Some of its items are also in another one at the same time: the larger amount applies.")
                font.pixelSize: p.unit * 0.7
                color: "#b45309"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Small { u: p.unit; objectName: "buydownEdit"; text: qsTr("Edit"); enabled: p.selectedId !== ""; onClicked: p.pos.buydownEdit(p.selectedId) }
                Small {
                    u: p.unit
                    objectName: "buydownCopy"
                    text: qsTr("Copy to New Buydown/Promo")
                    enabled: p.selectedId !== ""
                    palette.button: "#1d4ed8"; palette.buttonText: "white"
                    onClicked: p.pos.buydownCopy(p.selectedId)
                }
                Small { u: p.unit; objectName: "buydownDelete"; text: qsTr("Delete"); enabled: p.selectedId !== ""; onClicked: confirmDelete.open() }
                Item { Layout.preferredWidth: p.unit }
                Small {
                    u: p.unit
                    objectName: "buydownPrint"
                    // The chosen one (or the ticked ones), never everything by default.
                    text: p.markedDefs.length > 1 ? qsTr("Print %1 ticked").arg(p.markedDefs.length) : qsTr("Print")
                    enabled: p.printIds().length > 0
                    onClicked: p.pos.buydownShowReport("definition", p.printIds())
                }
                Small { u: p.unit; objectName: "buydownSold"; text: qsTr("Sold report"); enabled: p.selectedId !== ""; onClicked: p.pos.buydownShowReport("sold", [p.selectedId]) }
                Small {
                    u: p.unit
                    objectName: "buydownExport"
                    text: qsTr("Send to stores (file)…")
                    enabled: p.selectedId !== ""
                    onClicked: if (p.zoneController) p.zoneController.exportBuydown(p.selectedId)
                }
                Item { Layout.fillWidth: true }
                Small { u: p.unit; objectName: "buydownPrintAll"; text: qsTr("Print all…"); enabled: p.list.length > 0; onClicked: p.pos.buydownShowReport("definition", ["*"]) }
            }
        }

        // --- one being edited ---
        Flickable {
            visible: !!p.draft
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: editor.implicitHeight
            ColumnLayout {
                id: editor
                width: parent.width
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: !p.draft ? "" : (p.draft.isNew ? qsTr("New") : qsTr("Editing")) + " " + p.draft.typeLabel + " · " + qsTr("ID %1").arg(p.draft.number)
                          + (p.draft.from ? " · " + qsTr("from %1").arg(p.draft.from) : "") + " · " + p.draft.stateLabel
                          + (p.draft.capped ? " · " + p.draft.usedText : "")
                    font.pixelSize: p.unit * 0.9
                    font.bold: true
                    color: "#111827"
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 6
                    columnSpacing: 8
                    rowSpacing: 4
                    Label { text: qsTr("Type"); font.pixelSize: p.unit * 0.75 }
                    ComboBox {
                        objectName: "buydownType"
                        implicitHeight: p.touch
                        font.pixelSize: p.unit * 0.75
                        model: [qsTr("Buydown"), qsTr("Promotional Sale")]
                        currentIndex: p.draft && p.draft.type === "promo" ? 1 : 0
                        onActivated: i => p.set("type", i === 1 ? "promo" : "buydown")
                    }
                    Label { text: qsTr("Name"); font.pixelSize: p.unit * 0.75 }
                    Field {
                        u: p.unit
                        objectName: "buydownName"
                        Layout.fillWidth: true
                        serverValue: p.draft ? p.draft.name : ""
                        placeholderText: "SONOMA 2026Q4"
                        onEditingFinished: if (text !== serverValue) p.set("name", text)
                    }
                    Label { text: qsTr("Receipt Text"); font.pixelSize: p.unit * 0.75 }
                    Field {
                        u: p.unit
                        objectName: "buydownReceiptText"
                        Layout.fillWidth: true
                        serverValue: p.draft ? p.draft.receiptText : ""
                        placeholderText: "SONOMA SAVINGS!"
                        onEditingFinished: if (text !== serverValue) p.set("receiptText", text)
                    }
                    Label { text: qsTr("$ Amount (per carton)"); font.pixelSize: p.unit * 0.75 }
                    Field {
                        u: p.unit
                        objectName: "buydownAmount"
                        serverValue: p.draft ? p.draft.amount : ""
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        placeholderText: "4.10"
                        onEditingFinished: if (text !== serverValue) p.set("amount", text)
                    }
                    Label { text: qsTr("Style"); font.pixelSize: p.unit * 0.75 }
                    ComboBox {
                        objectName: "buydownStyle"
                        implicitHeight: p.touch
                        Layout.fillWidth: true
                        font.pixelSize: p.unit * 0.75
                        model: [qsTr("1 = Date Range Only"), qsTr("2 = Quantity Only"), qsTr("3 = Quantity with Date Range"), qsTr("4 = Old Style")]
                        currentIndex: p.draft ? p.draft.style - 1 : 0
                        onActivated: i => p.set("style", i + 1)
                    }
                    Label { text: qsTr("Qty (units that get it)"); font.pixelSize: p.unit * 0.75; opacity: p.draft && p.draft.capped ? 1 : 0.5 }
                    Field {
                        u: p.unit
                        objectName: "buydownQty"
                        enabled: !!p.draft && p.draft.capped
                        serverValue: p.draft ? String(p.draft.qty) : ""
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: if (text !== serverValue) p.set("qty", Number(text) || 0)
                    }
                    Label { text: qsTr("Start Date"); font.pixelSize: p.unit * 0.75 }
                    DateField {
                        objectName: "buydownStart"
                        mode: "datetime"
                        twentyFour: true
                        allowClear: true
                        Layout.preferredWidth: p.unit * 14
                        height: p.touch
                        Layout.preferredHeight: p.touch
                        textColor: "white"
                        title: qsTr("Start Date")
                        value: p.draft ? p.draft.start : ""
                        onPicked: v => p.set("start", v)
                    }
                    Label { text: qsTr("End Date"); font.pixelSize: p.unit * 0.75; opacity: p.draft && p.draft.style === 2 ? 0.5 : 1 }
                    DateField {
                        objectName: "buydownEnd"
                        mode: "datetime"
                        twentyFour: true
                        allowClear: true
                        readOnly: !!p.draft && p.draft.style === 2
                        Layout.preferredWidth: p.unit * 14
                        height: p.touch
                        Layout.preferredHeight: p.touch
                        title: qsTr("End Date")
                        value: p.draft ? p.draft.end : ""
                        onPicked: v => p.set("end", v)
                    }
                    CheckBox {
                        objectName: "buydownUsePartCtn"
                        Layout.columnSpan: 2
                        text: qsTr("Use \"Part of Carton\" field (off: every UPC gets the full amount)")
                        font.pixelSize: p.unit * 0.72
                        checked: !!p.draft && p.draft.usePartCtn
                        onClicked: p.set("usePartCtn", checked)
                    }
                    Label { text: qsTr("Manufacturer"); font.pixelSize: p.unit * 0.75 }
                    Field {
                        u: p.unit
                        objectName: "buydownManufacturer"
                        Layout.fillWidth: true
                        serverValue: p.draft ? p.draft.manufacturer : ""
                        onEditingFinished: if (text !== serverValue) p.set("manufacturer", text)
                    }
                    Label { text: qsTr("Rpt Code"); font.pixelSize: p.unit * 0.75 }
                    Field {
                        u: p.unit
                        objectName: "buydownRptCode"
                        serverValue: p.draft ? p.draft.rptCode : ""
                        onEditingFinished: if (text !== serverValue) p.set("rptCode", text)
                    }
                    Label { text: qsTr("Acct #"); font.pixelSize: p.unit * 0.75 }
                    Field {
                        u: p.unit
                        objectName: "buydownAccount"
                        serverValue: p.draft ? p.draft.account : ""
                        onEditingFinished: if (text !== serverValue) p.set("account", text)
                    }
                    CheckBox {
                        objectName: "buydownTaxBefore"
                        Layout.columnSpan: 2
                        visible: !!p.draft && p.draft.type === "promo"
                        text: qsTr("Apply taxes before discount (this promotion)")
                        font.pixelSize: p.unit * 0.72
                        checked: !!p.draft && p.draft.taxBefore
                        onClicked: p.set("taxBefore", checked)
                    }
                    Label {
                        Layout.columnSpan: 2
                        visible: !!p.draft && p.draft.type !== "promo"
                        text: p.info.storeTaxBefore ? qsTr("Taxes: on the price before the buydown (store setting)")
                                                    : qsTr("Taxes: on the reduced price (store setting)")
                        font.pixelSize: p.unit * 0.7
                        color: "#4b5563"
                    }
                }

                // --- its items ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Small {
                        u: p.unit
                        objectName: "buydownAddChosen"
                        text: qsTr("Add the items ticked in Mass Updates (%1)").arg(p.info.chosenInMass ?? 0)
                        enabled: (p.info.chosenInMass ?? 0) > 0
                        onClicked: p.pos.buydownAddChosen()
                    }
                    Label { text: qsTr("Add items that start with:"); font.pixelSize: p.unit * 0.75 }
                    Field { u: p.unit; id: startsWith; objectName: "buydownStartsWith"; Layout.preferredWidth: p.unit * 9; placeholderText: "WINSTON"; onAccepted: p.pos.buydownAddStartingWith(text) }
                    Small { u: p.unit; objectName: "buydownStartsWithAdd"; text: qsTr("Add"); onClicked: p.pos.buydownAddStartingWith(startsWith.text) }
                    Label { text: qsTr("UPC / item #:"); font.pixelSize: p.unit * 0.75 }
                    Field { u: p.unit; id: oneCode; objectName: "buydownCode"; Layout.preferredWidth: p.unit * 9; onAccepted: { p.pos.buydownAddItems([text]); text = "" } }
                    Small { u: p.unit; objectName: "buydownCodeAdd"; text: qsTr("Add"); onClicked: { p.pos.buydownAddItems([oneCode.text]); oneCode.text = "" } }
                    Item { Layout.fillWidth: true }
                    Small {
                        u: p.unit
                        objectName: "buydownRemoveItems"
                        text: qsTr("Take out ticked (%1)").arg(p.markedItems.length)
                        enabled: p.markedItems.length > 0
                        onClicked: { p.pos.buydownRemoveItems(p.markedItems); p.markedItems = [] }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Head { u: p.unit; text: ""; Layout.preferredWidth: p.unit * 2 }
                    Head { u: p.unit; text: qsTr("UPC"); Layout.preferredWidth: p.unit * 8 }
                    Head { u: p.unit; text: qsTr("Description"); Layout.fillWidth: true }
                    Head { u: p.unit; text: qsTr("Part/Ctn"); Layout.preferredWidth: p.unit * 4.5 }
                    Head { u: p.unit; text: qsTr("Price"); Layout.preferredWidth: p.unit * 5 }
                    Head { u: p.unit; text: qsTr("$ Amount"); Layout.preferredWidth: p.unit * 5 }
                    Head { u: p.unit; text: qsTr("Sale Price"); Layout.preferredWidth: p.unit * 5 }
                }
                Repeater {
                    objectName: "buydownItems"
                    model: p.draft ? p.draft.itemList : []
                    delegate: Rectangle {
                        id: it
                        required property var modelData
                        required property int index
                        objectName: "buydownItem-" + modelData.upc
                        Layout.fillWidth: true
                        Layout.preferredHeight: p.touch
                        color: index % 2 ? "#ffffff" : "#f1f5f9"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            spacing: 4
                            CheckBox {
                                Layout.preferredWidth: p.unit * 2
                                checked: p.markedItems.indexOf(it.modelData.key) >= 0
                                onClicked: p.markedItems = p.toggle(p.markedItems, it.modelData.key)
                            }
                            Cell { u: p.unit; text: it.modelData.upc; Layout.preferredWidth: p.unit * 8 }
                            Cell { u: p.unit; text: it.modelData.name + (it.modelData.found ? "" : "  " + qsTr("(not in this store)")); Layout.fillWidth: true }
                            Field {
                                u: p.unit
                                Layout.preferredWidth: p.unit * 4.5
                                serverValue: it.modelData.partCtn
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onEditingFinished: if (text !== serverValue) p.pos.buydownSetPartCtn(it.modelData.key, Number(text) || 0)
                            }
                            Cell { u: p.unit; text: it.modelData.price; Layout.preferredWidth: p.unit * 5 }
                            Cell { u: p.unit; text: "−" + it.modelData.amount; Layout.preferredWidth: p.unit * 5; color: "#15803d" }
                            Cell { u: p.unit; text: it.modelData.sale; Layout.preferredWidth: p.unit * 5; font.bold: true }
                        }
                    }
                }
                Label {
                    visible: !!p.draft && p.draft.itemList.length === 0
                    text: qsTr("No items yet. Tick them in Mass Updates (Find, a category, the brand tree), or add those that start with a brand name.")
                    font.pixelSize: p.unit * 0.75
                    color: "#6b7280"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "buydownNotFound"
                    visible: !!p.draft && (p.draft.notFound ?? []).length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: p.unit * 0.72
                    color: "#b91c1c"
                    text: !p.draft ? "" : qsTr("Not found here (left out): %1").arg((p.draft.notFound ?? []).map(x => x.upc + " " + x.name).join(", "))
                }
                Label {
                    objectName: "buydownOverlaps"
                    visible: !!p.draft && p.draft.overlaps.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: p.unit * 0.72
                    color: "#b45309"
                    text: !p.draft ? "" : qsTr("⚠ Also in another at the same time (the larger amount applies): %1")
                                            .arg(p.draft.overlaps.map(o => (o.item || o.upc) + " → " + o.other).join(", "))
                }
                Label {
                    visible: !!p.draft && p.draft.problems.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: p.unit * 0.75
                    color: "#b91c1c"
                    text: p.draft ? p.draft.problems.join("  ") : ""
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        visible: !p.info.canApply
                        text: qsTr("An admin's PIN will be asked for.")
                        font.pixelSize: p.unit * 0.75
                        color: "#4b5563"
                    }
                    Item { Layout.fillWidth: true }
                    Small {
                        u: p.unit
                        objectName: "buydownApply"
                        text: qsTr("Apply")
                        palette.button: "#15803d"; palette.buttonText: "white"
                        // (A field still being typed in goes first.)
                        onClicked: { forceActiveFocus(); p.pos.buydownApply() }
                    }
                    Small { u: p.unit; objectName: "buydownAbort"; text: qsTr("Abort"); onClicked: p.pos.buydownAbort() }
                }
            }
        }
    }

    // --- a report: the definition(s), or one buydown's sales ---
    Rectangle {
        objectName: "buydownReport"
        visible: !!p.report
        anchors.fill: parent
        z: 5
        color: "#f8fafc"
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: p.unit * 0.6
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: !p.report ? "" : p.report.loading ? qsTr("Reading the sales…") : (p.report.title + (p.report.subtitle ? " · " + p.report.subtitle : ""))
                    font.pixelSize: p.unit
                    font.bold: true
                    color: "#111827"
                    elide: Text.ElideRight
                }
                Small { u: p.unit; objectName: "buydownReportPrint"; text: qsTr("Print"); enabled: !!p.report && !p.report.loading; onClicked: p.pos.buydownPrintReport() }
                Small { u: p.unit; objectName: "buydownReportClose"; text: qsTr("Close"); onClicked: p.pos.buydownCloseReport() }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: p.report && !p.report.loading ? p.report.rows : []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 6
                    readonly property bool wide: modelData.kind !== "line" || modelData.cells.length <= 1
                    Label {
                        Layout.fillWidth: true
                        text: modelData.cells[0] ?? ""
                        font.pixelSize: p.unit * (modelData.kind === "section" ? 0.85 : 0.72)
                        font.bold: modelData.kind === "section" || modelData.kind === "total"
                        font.italic: modelData.kind === "note"
                        color: "#111827"
                        elide: Text.ElideRight
                    }
                    Repeater {
                        model: (modelData.cells ?? []).slice(1)
                        Label {
                            required property var modelData
                            Layout.preferredWidth: p.unit * 6
                            horizontalAlignment: Text.AlignRight
                            text: modelData
                            font.pixelSize: p.unit * 0.72
                            color: "#111827"
                            elide: Text.ElideLeft
                        }
                    }
                }
            }
        }
    }

    // The screen's controller (exports to a file), from the Mass Updates screen around it.
    property var zoneController: null

    FileDialog {
        id: importFile
        title: qsTr("A buydown from another store")
        nameFilters: [qsTr("ViewTouch buydowns (*.vtbuydown.json *.json)"), qsTr("All files (*)")]
        onAccepted: {
            const read = p.zoneController ? p.zoneController.readBuydownFile(selectedFile) : { error: qsTr("Can't read files here.") }
            p.fileError = read.error ?? ""
            if (!read.error) p.pos.buydownImport(read)
        }
    }
    Dialog {
        id: confirmDelete
        objectName: "buydownConfirmDelete"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete %1?").arg(p.selected() ? p.selected().name : "")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Sales already rung keep their buydown lines."); font.pixelSize: p.unit * 0.8 }
        onAccepted: p.pos.buydownDelete(p.selectedId)
    }
}
