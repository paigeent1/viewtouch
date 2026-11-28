import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Dialogs
import QtQuick.Layouts

// Menu Builder > Mass Updates > System Discounts (owner 2026-11-27, after ILSA's "System Discount Setup Form";
// PosService::sysDiscOpen, app/pos_system_discounts.cpp). The list (ID, name, start, end, tax before, level 1,
// receipt, items, running / coming / ended): tick several for Copy to New (D26 -> D27 in one go), Delete or
// Print; Edit one. Editing: the name (30 characters), the type ($ or % off each item, one for every level),
// what counts (any mix of its items, or the same item only), the dates, tax before the discount, five levels
// (Qty, Or More, amount, receipt text), and its items: on the left the ones in it (Remove, Remove all), on the
// right Find's matches to add (Add ticked, Add all shown), "Add items that start with", boxes and their sticks,
// a UPC, or the Mass Updates ticks. Apply saves (an admin, or an admin's PIN); Abort drops it. Import reads
// ILSA's running discounts (a file from vt-ilsa-sysdisc), shown before they're added.
Rectangle {
    id: p
    objectName: "systemDiscountsPanel"
    property var pos: null
    property real unit: 16
    property real touch: Math.max(44, unit * 2.6)
    // "" System Discounts (quantity levels); "mixmatch": Mix-Match (Mass Updates' own button), its kind only.
    property string mode: ""
    readonly property bool mixMode: mode === "mixmatch"
    readonly property var info: pos ? pos.systemDiscounts : ({})
    readonly property var list: (info.list ?? []).filter(d => (d.kind ?? "") === mode)
    readonly property bool mixDraft: !!draft && draft.kind === "mixmatch"
    readonly property var draft: info.draft ?? null
    readonly property var importing: info.importing ?? null
    readonly property var report: info.report ?? null
    readonly property var found: info.found ?? []
    property string selectedId: ""
    property var markedDefs: []      // definitions ticked (Copy to New, Delete, Print)
    property var markedItems: []     // items ticked in the editor (to take out)
    property var markedFound: []     // Find's matches ticked (to add)
    property string fileError: ""
    signal pickItems()               // back to Mass Updates' grid to tick items
    signal closed()
    color: "#f8fafc"

    function selected() { return list.find(d => d.id === selectedId) ?? null }
    function toggle(arr, v) { return arr.indexOf(v) >= 0 ? arr.filter(x => x !== v) : arr.concat([v]) }
    function chosenIds() { return markedDefs.length > 0 ? markedDefs : (selectedId !== "" ? [selectedId] : []) }
    function set(k, v) { const o = {}; o[k] = v; if (pos) pos.sysDiscSet(o) }
    function setLevel(i, k, v) { const o = {}; o[k] = v; if (pos) pos.sysDiscSetLevel(i, o) }
    // "D26 EST/CAR HABMDR" -> "D26": the start the ticked ones share, for Copy to New.
    function sharedPrefix(ids) {
        const names = list.filter(d => ids.indexOf(d.id) >= 0).map(d => d.name.split(" ")[0])
        return names.length > 0 && names.every(n => n === names[0]) ? names[0] : ""
    }
    function nextPrefix(s) {
        const m = /^(.*?)(\d+)$/.exec(s)
        return m ? m[1] + String(Number(m[2]) + 1).padStart(m[2].length, "0") : s
    }

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

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: p.mixMode ? qsTr("Mix-Match") : qsTr("System Discounts")
                font.pixelSize: p.unit * 1.1
                font.bold: true
                color: "#111827"
            }
            Small { u: p.unit; objectName: "sysDiscAdd"; text: qsTr("Add"); enabled: !p.draft && !p.importing; onClicked: p.pos.sysDiscNew(p.mode) }
            Small { u: p.unit; objectName: "sysDiscImport"; visible: !p.mixMode; text: qsTr("Import from ILSA…"); enabled: !p.draft && !p.importing; onClicked: importFile.open() }
            Item { Layout.fillWidth: true }
            Small { u: p.unit; objectName: "sysDiscPickItems"; visible: !!p.draft; text: qsTr("Tick items in Mass Updates"); onClicked: p.pickItems() }
            Small { u: p.unit; objectName: "sysDiscClose"; text: qsTr("Close"); onClicked: p.closed() }
        }
        Label {
            objectName: "sysDiscMessage"
            visible: text !== ""
            Layout.fillWidth: true
            text: p.fileError !== "" ? p.fileError : (p.info.message ?? "")
            font.pixelSize: p.unit * 0.8
            color: "#b45309"
            wrapMode: Text.WordWrap
        }

        // --- the list ---
        ColumnLayout {
            visible: !p.draft && !p.importing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                CheckBox {
                    objectName: "sysDiscTickAll"
                    Layout.preferredWidth: p.unit * 2
                    checked: p.list.length > 0 && p.markedDefs.length === p.list.length
                    onClicked: p.markedDefs = checked ? p.list.map(d => d.id) : []
                }
                Head { u: p.unit; text: qsTr("ID"); Layout.preferredWidth: p.unit * 3 }
                Head { u: p.unit; text: qsTr("Discount Name"); Layout.fillWidth: true }
                Head { u: p.unit; text: qsTr("Start"); Layout.preferredWidth: p.unit * 7 }
                Head { u: p.unit; text: qsTr("End"); Layout.preferredWidth: p.unit * 7 }
                Head { u: p.unit; text: qsTr("Tax before"); Layout.preferredWidth: p.unit * 4.5 }
                Head { u: p.unit; text: p.mixMode ? qsTr("Mix-Match") : qsTr("Level 1"); Layout.preferredWidth: p.unit * 5.5 }
                Head { u: p.unit; text: qsTr("Receipt"); Layout.preferredWidth: p.unit * 10 }
                Head { u: p.unit; text: qsTr("Items"); Layout.preferredWidth: p.unit * 3 }
                Head { u: p.unit; text: qsTr("Now"); Layout.preferredWidth: p.unit * 4.5 }
            }
            ListView {
                id: defs
                objectName: "sysDiscList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                model: p.list
                ScrollBar.vertical: TouchScrollBar { id: defsBar; needed: defs.contentHeight > defs.height + 1 }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "sysDiscRow-" + modelData.id
                    width: ListView.view.width - defsBar.room
                    height: p.touch
                    color: modelData.id === p.selectedId ? "#dbeafe" : (index % 2 ? "#ffffff" : "#f1f5f9")
                    TapHandler { onTapped: p.selectedId = row.modelData.id }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        spacing: 4
                        CheckBox {
                            objectName: "sysDiscTick-" + row.modelData.id
                            Layout.preferredWidth: p.unit * 2
                            checked: p.markedDefs.indexOf(row.modelData.id) >= 0
                            onClicked: p.markedDefs = p.toggle(p.markedDefs, row.modelData.id)
                        }
                        Cell { u: p.unit; text: row.modelData.number; Layout.preferredWidth: p.unit * 3 }
                        Cell { u: p.unit; text: row.modelData.name; Layout.fillWidth: true; font.bold: true }
                        Cell { u: p.unit; text: row.modelData.startText.split(" ")[0]; Layout.preferredWidth: p.unit * 7 }
                        Cell { u: p.unit; text: row.modelData.endText.split(" ")[0]; Layout.preferredWidth: p.unit * 7 }
                        Cell { u: p.unit; text: row.modelData.taxBefore ? qsTr("Yes") : qsTr("No"); Layout.preferredWidth: p.unit * 4.5 }
                        Cell { u: p.unit; text: row.modelData.level1; Layout.preferredWidth: p.unit * 5.5 }
                        Cell { u: p.unit; text: row.modelData.receipt; Layout.preferredWidth: p.unit * 10 }
                        Cell { u: p.unit; text: row.modelData.items; Layout.preferredWidth: p.unit * 3 }
                        Cell {
                            u: p.unit
                            text: row.modelData.stateLabel
                            Layout.preferredWidth: p.unit * 4.5
                            font.bold: row.modelData.state === "running"
                            color: row.modelData.state === "running" ? "#15803d" : row.modelData.state === "coming" ? "#1d4ed8" : "#6b7280"
                        }
                    }
                }
                Label {
                    visible: p.list.length === 0
                    anchors.centerIn: parent
                    text: p.mixMode ? qsTr("No Mix-Match yet: touch Add (2 for $1.00, buy one get one, 50% off the third).")
                                    : qsTr("No system discounts yet: touch Add, or Import from ILSA.")
                    font.pixelSize: p.unit * 0.9
                    color: "#6b7280"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Small { u: p.unit; objectName: "sysDiscEdit"; text: qsTr("Edit"); enabled: p.selectedId !== ""; onClicked: p.pos.sysDiscEdit(p.selectedId) }
                Small {
                    u: p.unit
                    objectName: "sysDiscCopy"
                    text: p.markedDefs.length > 1 ? qsTr("Copy %1 to New…").arg(p.markedDefs.length) : qsTr("Copy to New…")
                    enabled: p.chosenIds().length > 0
                    palette.button: "#1d4ed8"; palette.buttonText: "white"
                    onClicked: {
                        const from = p.sharedPrefix(p.chosenIds())
                        copyFrom.text = from
                        copyTo.text = p.nextPrefix(from)
                        copyStart.value = ""
                        copyEnd.value = ""
                        copyPop.open()
                    }
                }
                Small {
                    u: p.unit
                    objectName: "sysDiscDelete"
                    text: p.markedDefs.length > 1 ? qsTr("Delete %1…").arg(p.markedDefs.length) : qsTr("Delete…")
                    enabled: p.chosenIds().length > 0
                    onClicked: confirmDelete.open()
                }
                Item { Layout.preferredWidth: p.unit }
                Small {
                    u: p.unit
                    objectName: "sysDiscPrint"
                    text: p.markedDefs.length > 1 ? qsTr("Print %1 ticked").arg(p.markedDefs.length) : qsTr("Print")
                    enabled: p.chosenIds().length > 0
                    onClicked: p.pos.sysDiscShowReport(p.chosenIds())
                }
                Item { Layout.fillWidth: true }
                Small { u: p.unit; objectName: "sysDiscPrintAll"; text: qsTr("Print all…"); enabled: p.list.length > 0; onClicked: p.pos.sysDiscShowReport(p.list.map(d => d.id)) }
            }
        }

        // --- ILSA's discounts read from a file, before they're added ---
        ColumnLayout {
            visible: !!p.importing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: !p.importing ? "" : qsTr("%1 running discount(s) from %2 · %3 over already, left out")
                                             .arg(p.importing.count).arg(p.importing.from || qsTr("the file")).arg(p.importing.skipped)
                font.pixelSize: p.unit * 0.9
                font.bold: true
                color: "#111827"
            }
            ListView {
                id: importList
                objectName: "sysDiscImportList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                model: p.importing ? p.importing.rows : []
                ScrollBar.vertical: TouchScrollBar { id: importBar; needed: importList.contentHeight > importList.height + 1 }
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width - importBar.room
                    height: p.touch * 0.8
                    color: index % 2 ? "#ffffff" : "#f1f5f9"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        spacing: 4
                        Cell { u: p.unit; text: modelData.name; Layout.fillWidth: true; font.bold: true }
                        Cell { u: p.unit; text: modelData.startText.split(" ")[0] + " – " + modelData.endText.split(" ")[0]; Layout.preferredWidth: p.unit * 12 }
                        Cell { u: p.unit; text: modelData.level1; Layout.preferredWidth: p.unit * 5.5 }
                        Cell { u: p.unit; text: modelData.receipt; Layout.preferredWidth: p.unit * 10 }
                        Cell { u: p.unit; text: qsTr("%1 items").arg(modelData.items); Layout.preferredWidth: p.unit * 5; color: modelData.items > 0 ? "#111827" : "#b91c1c" }
                        Cell { u: p.unit; text: modelData.replaces ? qsTr("replaces") : qsTr("new"); Layout.preferredWidth: p.unit * 5 }
                    }
                }
            }
            Label {
                objectName: "sysDiscImportNotFound"
                visible: !!p.importing && p.importing.notFound.length > 0
                Layout.fillWidth: true
                Layout.maximumHeight: p.unit * 5
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                font.pixelSize: p.unit * 0.72
                color: "#b91c1c"
                text: !p.importing ? "" : qsTr("%1 UPC(s) not in this store, left out: %2").arg(p.importing.notFound.length)
                                             .arg(p.importing.notFound.map(x => x.upc + " " + x.name).join(", "))
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { visible: !p.info.canApply; text: qsTr("An admin's PIN will be asked for."); font.pixelSize: p.unit * 0.75; color: "#4b5563" }
                Item { Layout.fillWidth: true }
                Small { u: p.unit; objectName: "sysDiscImportApply"; text: qsTr("Add them"); palette.button: "#15803d"; palette.buttonText: "white"; onClicked: p.pos.sysDiscImportApply() }
                Small { u: p.unit; objectName: "sysDiscImportCancel"; text: qsTr("Cancel"); onClicked: p.pos.sysDiscImportCancel() }
            }
        }

        // --- one being edited ---
        ColumnLayout {
            visible: !!p.draft
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: !p.draft ? "" : (p.draft.isNew ? qsTr("New") : qsTr("Editing")) + " · " + qsTr("ID %1").arg(p.draft.number) + " · " + p.draft.stateLabel
                      + (p.draft.ilsaId ? " · " + qsTr("from ILSA (%1)").arg(p.draft.ilsaId) : "")
                font.pixelSize: p.unit * 0.9
                font.bold: true
                color: "#111827"
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 6
                columnSpacing: 8
                rowSpacing: 4
                Label { text: qsTr("Discount Name"); font.pixelSize: p.unit * 0.75 }
                Field {
                    u: p.unit
                    objectName: "sysDiscName"
                    Layout.fillWidth: true
                    Layout.minimumWidth: p.unit * 16
                    maximumLength: p.info.nameLength ?? 30
                    serverValue: p.draft ? p.draft.name : ""
                    placeholderText: "D27 LA AURORA CTGTOR"
                    onEditingFinished: if (text !== serverValue) p.set("name", text)
                }
                Label { visible: !p.mixDraft; text: qsTr("Discount Type"); font.pixelSize: p.unit * 0.75 }
                ComboBox {
                    visible: !p.mixDraft
                    objectName: "sysDiscType"
                    implicitHeight: p.touch
                    Layout.preferredWidth: p.unit * 11
                    font.pixelSize: p.unit * 0.75
                    model: [qsTr("$ Off of Each Item"), qsTr("% Off of Each Item")]
                    currentIndex: p.draft && p.draft.percent ? 1 : 0
                    onActivated: i => p.set("percent", i === 1)
                }
                Label { text: qsTr("Counts"); font.pixelSize: p.unit * 0.75 }
                ComboBox {
                    objectName: "sysDiscCounts"
                    implicitHeight: p.touch
                    Layout.preferredWidth: p.unit * 13
                    font.pixelSize: p.unit * 0.75
                    model: [qsTr("Any mix of these items"), qsTr("Same item only")]
                    currentIndex: p.draft && p.draft.sameItemOnly ? 1 : 0
                    onActivated: i => p.set("sameItemOnly", i === 1)
                }
                Label { text: qsTr("Start Date"); font.pixelSize: p.unit * 0.75 }
                DateField {
                    objectName: "sysDiscStart"
                    mode: "datetime"
                    allowClear: true
                    Layout.preferredWidth: p.unit * 14
                    height: p.touch
                    Layout.preferredHeight: p.touch
                    title: qsTr("Start Date")
                    value: p.draft ? p.draft.start : ""
                    onPicked: v => p.set("start", v)
                }
                Label { text: qsTr("End Date"); font.pixelSize: p.unit * 0.75 }
                DateField {
                    objectName: "sysDiscEnd"
                    mode: "datetime"
                    allowClear: true
                    Layout.preferredWidth: p.unit * 14
                    height: p.touch
                    Layout.preferredHeight: p.touch
                    title: qsTr("End Date")
                    value: p.draft ? p.draft.end : ""
                    onPicked: v => p.set("end", v)
                }
                CheckBox {
                    objectName: "sysDiscTaxBefore"
                    Layout.columnSpan: 2
                    text: qsTr("Tax Before Discount")
                    font.pixelSize: p.unit * 0.75
                    checked: !!p.draft && p.draft.taxBefore
                    onClicked: p.set("taxBefore", checked)
                }
            }
            // Mix-Match (ILSA's price record): starting with the sale of item #, change the price of how many, how.
            Flow {
                objectName: "sysDiscMix"
                visible: p.mixDraft
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Starting with the sale of item #"); font.pixelSize: p.unit * 0.8; height: p.touch; verticalAlignment: Text.AlignVCenter }
                Field { u: p.unit; objectName: "sysDiscMixStart"; width: p.unit * 4; serverValue: p.draft ? (p.draft.mixStart ?? "") : ""
                        inputMethodHints: Qt.ImhDigitsOnly; onEditingFinished: if (text !== serverValue) p.set("mixStart", text) }
                Label { text: qsTr("change the price for"); font.pixelSize: p.unit * 0.8; height: p.touch; verticalAlignment: Text.AlignVCenter }
                Field { u: p.unit; objectName: "sysDiscMixCount"; width: p.unit * 4; serverValue: p.draft ? (p.draft.mixCount ?? "") : ""
                        inputMethodHints: Qt.ImhDigitsOnly; onEditingFinished: if (text !== serverValue) p.set("mixCount", text) }
                Label { text: qsTr("item(s):"); font.pixelSize: p.unit * 0.8; height: p.touch; verticalAlignment: Text.AlignVCenter }
                ComboBox {
                    objectName: "sysDiscMixHow"
                    implicitHeight: p.touch
                    width: p.unit * 11
                    font.pixelSize: p.unit * 0.75
                    readonly property var keys: ["off", "percent", "price"]
                    model: [qsTr("Discount Price ($ off)"), qsTr("Percent Discount"), qsTr("Replace Price With")]
                    currentIndex: Math.max(0, keys.indexOf(p.draft ? p.draft.mixHow : "off"))
                    onActivated: i => p.set("mixHow", keys[i])
                }
                Field { u: p.unit; objectName: "sysDiscMixAmount"; width: p.unit * 6; serverValue: p.draft ? (p.draft.mixAmount ?? "") : ""
                        placeholderText: p.draft && p.draft.mixHow === "percent" ? "50" : "0.41"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly; onEditingFinished: if (text !== serverValue) p.set("mixAmount", text) }
                Label { text: qsTr("Receipt"); font.pixelSize: p.unit * 0.8; height: p.touch; verticalAlignment: Text.AlignVCenter }
                Field { u: p.unit; objectName: "sysDiscMixText"; width: p.unit * 14; maximumLength: 40; serverValue: p.draft ? (p.draft.mixText ?? "") : ""
                        placeholderText: "2 FOR $1.00"; onEditingFinished: if (text !== serverValue) p.set("mixText", text) }
            }
            // The five levels.
            RowLayout {
                visible: !p.mixDraft
                objectName: "sysDiscLevels"
                Layout.fillWidth: true
                spacing: 8
                Head { u: p.unit; text: qsTr("Level"); Layout.preferredWidth: p.unit * 2.5 }
                Head { u: p.unit; text: qsTr("Quantity"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: ""; Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: p.draft && p.draft.percent ? qsTr("% off each") : qsTr("$ off each"); Layout.preferredWidth: p.unit * 6 }
                Head { u: p.unit; text: qsTr("Receipt Text"); Layout.fillWidth: true }
            }
            Repeater {
                model: p.draft && !p.mixDraft ? p.draft.levels : []
                RowLayout {
                    id: lvl
                    required property var modelData
                    required property int index
                    objectName: "sysDiscLevel-" + index
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: String(lvl.index + 1); font.pixelSize: p.unit * 0.8; Layout.preferredWidth: p.unit * 2.5 }
                    Field {
                        u: p.unit
                        objectName: "sysDiscQty-" + lvl.index
                        Layout.preferredWidth: p.unit * 5
                        serverValue: lvl.modelData.qty
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: if (text !== serverValue) p.setLevel(lvl.index, "qty", text)
                    }
                    CheckBox {
                        objectName: "sysDiscOrMore-" + lvl.index
                        Layout.preferredWidth: p.unit * 5
                        text: qsTr("Or More")
                        font.pixelSize: p.unit * 0.72
                        checked: lvl.modelData.orMore
                        onClicked: p.setLevel(lvl.index, "orMore", checked)
                    }
                    Field {
                        u: p.unit
                        objectName: "sysDiscAmount-" + lvl.index
                        Layout.preferredWidth: p.unit * 6
                        serverValue: lvl.modelData.amount
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        placeholderText: p.draft && p.draft.percent ? "10" : "25.00"
                        onEditingFinished: if (text !== serverValue) p.setLevel(lvl.index, "amount", text)
                    }
                    Field {
                        u: p.unit
                        objectName: "sysDiscReceipt-" + lvl.index
                        Layout.fillWidth: true
                        maximumLength: 40
                        serverValue: lvl.modelData.receiptText
                        placeholderText: lvl.index === 0 ? "DIRTY DOZEN SAVINGS!" : ""
                        onEditingFinished: if (text !== serverValue) p.setLevel(lvl.index, "receiptText", text)
                    }
                }
            }
            // The items: in it (left), to add (right).
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    spacing: 4
                    Label {
                        text: qsTr("Items in this discount (%1)").arg(p.draft ? p.draft.itemList.length : 0)
                        font.pixelSize: p.unit * 0.8
                        font.bold: true
                    }
                    ListView {
                        id: inList
                        objectName: "sysDiscItems"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: p.touch * 2
                        clip: true
                        spacing: 1
                        model: p.draft ? p.draft.itemList : []
                        ScrollBar.vertical: TouchScrollBar { id: inBar; needed: inList.contentHeight > inList.height + 1 }
                        delegate: Rectangle {
                            id: it
                            required property var modelData
                            required property int index
                            width: ListView.view.width - inBar.room
                            height: p.touch * 0.85
                            color: it.index % 2 ? "#ffffff" : "#f1f5f9"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                spacing: 4
                                CheckBox {
                                    Layout.preferredWidth: p.unit * 2
                                    checked: p.markedItems.indexOf(it.modelData.key) >= 0
                                    onClicked: p.markedItems = p.toggle(p.markedItems, it.modelData.key)
                                }
                                Cell { u: p.unit; text: it.modelData.upc; Layout.preferredWidth: p.unit * 7.5 }
                                Cell { u: p.unit; text: it.modelData.name + (it.modelData.found ? "" : "  " + qsTr("(not in this store)")); Layout.fillWidth: true }
                                Cell { u: p.unit; text: it.modelData.price; Layout.preferredWidth: p.unit * 4.5; horizontalAlignment: Text.AlignRight }
                            }
                        }
                        Label {
                            visible: inList.count === 0
                            anchors.centerIn: parent
                            width: parent.width - p.unit
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("No items yet: find them on the right, or tick them in Mass Updates.")
                            font.pixelSize: p.unit * 0.75
                            color: "#6b7280"
                        }
                    }
                    RowLayout {
                        spacing: 6
                        Small {
                            u: p.unit
                            objectName: "sysDiscRemove"
                            text: qsTr("Remove item")
                            enabled: p.markedItems.length > 0
                            onClicked: { p.pos.sysDiscRemoveItems(p.markedItems); p.markedItems = [] }
                        }
                        Small {
                            u: p.unit
                            objectName: "sysDiscRemoveAll"
                            text: qsTr("Remove all items")
                            enabled: !!p.draft && p.draft.itemList.length > 0
                            onClicked: { p.pos.sysDiscRemoveItems(["*"]); p.markedItems = [] }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Field {
                            id: findField
                            u: p.unit
                            objectName: "sysDiscFind"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Find description or UPC")
                            onAccepted: { p.markedFound = []; p.pos.sysDiscFind(text) }
                        }
                        Small { u: p.unit; text: qsTr("Find"); onClicked: { p.markedFound = []; p.pos.sysDiscFind(findField.text) } }
                    }
                    ListView {
                        id: foundList
                        objectName: "sysDiscFound"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: p.touch * 2
                        clip: true
                        spacing: 1
                        model: p.found
                        ScrollBar.vertical: TouchScrollBar { id: foundBar; needed: foundList.contentHeight > foundList.height + 1 }
                        delegate: Rectangle {
                            id: fr
                            required property var modelData
                            required property int index
                            width: ListView.view.width - foundBar.room
                            height: p.touch * 0.85
                            color: fr.index % 2 ? "#ffffff" : "#f1f5f9"
                            opacity: fr.modelData.inIt ? 0.5 : 1
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                spacing: 4
                                CheckBox {
                                    Layout.preferredWidth: p.unit * 2
                                    enabled: !fr.modelData.inIt
                                    checked: p.markedFound.indexOf(fr.modelData.id) >= 0
                                    onClicked: p.markedFound = p.toggle(p.markedFound, fr.modelData.id)
                                }
                                Cell { u: p.unit; text: fr.modelData.name + (fr.modelData.inIt ? "  ✓" : ""); Layout.fillWidth: true }
                                Cell { u: p.unit; text: fr.modelData.price; Layout.preferredWidth: p.unit * 4.5; horizontalAlignment: Text.AlignRight }
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Small {
                            u: p.unit
                            objectName: "sysDiscAddTicked"
                            text: qsTr("Add ticked")
                            enabled: p.markedFound.length > 0
                            onClicked: { p.pos.sysDiscAddItems(p.markedFound); p.markedFound = [] }
                        }
                        Small {
                            u: p.unit
                            objectName: "sysDiscAddShown"
                            text: qsTr("Add all shown")
                            enabled: p.found.some(f => !f.inIt)
                            onClicked: p.pos.sysDiscAddItems(p.found.filter(f => !f.inIt).map(f => f.id))
                        }
                        Small { u: p.unit; objectName: "sysDiscAddStart"; text: qsTr("Add items that start with…"); onClicked: startPop.open() }
                        Small { u: p.unit; objectName: "sysDiscAddLinked"; text: qsTr("Add box / sticks too"); onClicked: p.pos.sysDiscAddLinked() }
                        Small { u: p.unit; objectName: "sysDiscAddUpc"; text: qsTr("Add UPC…"); onClicked: upcPop.open() }
                    }
                }
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
                    objectName: "sysDiscApply"
                    text: qsTr("Apply")
                    palette.button: "#15803d"; palette.buttonText: "white"
                    // (A field still being typed in goes first.)
                    onClicked: { forceActiveFocus(); p.pos.sysDiscApply() }
                }
                Small { u: p.unit; objectName: "sysDiscAbort"; text: qsTr("Abort"); onClicked: p.pos.sysDiscAbort() }
            }
        }
    }

    // --- the Definition report ---
    Rectangle {
        objectName: "sysDiscReport"
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
                    text: !p.report ? "" : p.report.title + (p.report.subtitle ? " · " + p.report.subtitle : "")
                    font.pixelSize: p.unit
                    font.bold: true
                    color: "#111827"
                    elide: Text.ElideRight
                }
                Small { u: p.unit; objectName: "sysDiscReportPrint"; text: qsTr("Print"); onClicked: p.pos.sysDiscPrintReport() }
                Small { u: p.unit; objectName: "sysDiscReportClose"; text: qsTr("Close"); onClicked: p.pos.sysDiscCloseReport() }
            }
            ListView {
                id: reportList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: p.report ? p.report.rows : []
                ScrollBar.vertical: TouchScrollBar { id: reportBar; needed: reportList.contentHeight > reportList.height + 1 }
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width - reportBar.room
                    spacing: 6
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
                            Layout.preferredWidth: p.unit * 14
                            text: modelData
                            font.pixelSize: p.unit * 0.72
                            color: "#111827"
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // The screen's controller (reads files), from the Mass Updates screen around it.
    property var zoneController: null

    FileDialog {
        id: importFile
        title: qsTr("ILSA's system discounts (from vt-ilsa-sysdisc)")
        nameFilters: [qsTr("ViewTouch system discounts (*.vtsysdisc.json *.json)"), qsTr("All files (*)")]
        onAccepted: {
            const read = p.zoneController ? p.zoneController.readSystemDiscountFile(selectedFile) : { error: qsTr("Can't read files here.") }
            p.fileError = read.error ?? ""
            if (!read.error) p.pos.sysDiscImport(read)
        }
    }
    Dialog {
        id: confirmDelete
        objectName: "sysDiscConfirmDelete"
        anchors.centerIn: parent
        modal: true
        title: p.chosenIds().length > 1 ? qsTr("Delete %1 system discounts?").arg(p.chosenIds().length)
                                         : qsTr("Delete %1?").arg(p.selected() ? p.selected().name : "")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Sales already rung keep their discount lines."); font.pixelSize: p.unit * 0.8 }
        onAccepted: { p.pos.sysDiscDelete(p.chosenIds()); p.markedDefs = []; p.selectedId = "" }
    }
    // Copy to New for the ticked ones: D26 -> D27, new dates.
    Popup {
        id: copyPop
        objectName: "sysDiscCopyPopup"
        anchors.centerIn: parent
        modal: true
        padding: p.unit
        width: Math.min(p.width * 0.9, p.unit * 34)
        background: Rectangle { color: "white"; radius: 8; border.color: "#94a3b8" }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("Copy %1 to new discounts").arg(p.chosenIds().length)
                font.pixelSize: p.unit
                font.bold: true
                color: "#111827"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Each copy keeps its items, levels and amounts. Names that start with the first word get the second instead.")
                font.pixelSize: p.unit * 0.72
                color: "#4b5563"
            }
            RowLayout {
                spacing: 6
                Label { text: qsTr("Change"); font.pixelSize: p.unit * 0.8; color: "#111827" }
                Field { id: copyFrom; u: p.unit; objectName: "sysDiscCopyFrom"; Layout.preferredWidth: p.unit * 6; placeholderText: "D26" }
                Label { text: qsTr("to"); font.pixelSize: p.unit * 0.8; color: "#111827" }
                Field { id: copyTo; u: p.unit; objectName: "sysDiscCopyTo"; Layout.preferredWidth: p.unit * 6; placeholderText: "D27" }
            }
            GridLayout {
                columns: 2
                columnSpacing: 8
                Label { text: qsTr("New start"); font.pixelSize: p.unit * 0.8; color: "#111827" }
                DateField { id: copyStart; objectName: "sysDiscCopyStart"; mode: "datetime"; allowClear: true
                            Layout.preferredWidth: p.unit * 14; height: p.touch; Layout.preferredHeight: p.touch; title: qsTr("New start")
                            onPicked: v => value = v }
                Label { text: qsTr("New end"); font.pixelSize: p.unit * 0.8; color: "#111827" }
                DateField { id: copyEnd; objectName: "sysDiscCopyEnd"; mode: "datetime"; allowClear: true
                            Layout.preferredWidth: p.unit * 14; height: p.touch; Layout.preferredHeight: p.touch; title: qsTr("New end")
                            onPicked: v => value = v }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Dates left empty stay as they were.")
                font.pixelSize: p.unit * 0.7
                color: "#6b7280"
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Small {
                    u: p.unit
                    objectName: "sysDiscCopyGo"
                    text: qsTr("Copy")
                    palette.button: "#1d4ed8"; palette.buttonText: "white"
                    onClicked: {
                        p.pos.sysDiscCopy(p.chosenIds(), copyFrom.text, copyTo.text, copyStart.value, copyEnd.value)
                        p.markedDefs = []
                        copyPop.close()
                    }
                }
                Small { u: p.unit; text: qsTr("Cancel"); onClicked: copyPop.close() }
            }
        }
    }
    // "Add Items that start with:" (ILSA): every retail item whose name starts with it.
    Popup {
        id: startPop
        anchors.centerIn: parent
        modal: true
        padding: p.unit
        background: Rectangle { color: "white"; radius: 8; border.color: "#94a3b8" }
        onOpened: { startField.text = ""; startField.forceActiveFocus() }
        RowLayout {
            spacing: 6
            Label { text: qsTr("Add items that start with"); font.pixelSize: p.unit * 0.8; color: "#111827" }
            Field { id: startField; u: p.unit; objectName: "sysDiscStartWith"; Layout.preferredWidth: p.unit * 12; placeholderText: "1875"
                    onAccepted: { p.pos.sysDiscAddStartingWith(text); startPop.close() } }
            Small { u: p.unit; text: qsTr("Add"); onClicked: { p.pos.sysDiscAddStartingWith(startField.text); startPop.close() } }
        }
    }
    Popup {
        id: upcPop
        anchors.centerIn: parent
        modal: true
        padding: p.unit
        background: Rectangle { color: "white"; radius: 8; border.color: "#94a3b8" }
        onOpened: { upcField.text = ""; upcField.forceActiveFocus() }
        RowLayout {
            spacing: 6
            Label { text: qsTr("Scan or type a UPC"); font.pixelSize: p.unit * 0.8; color: "#111827" }
            Field { id: upcField; u: p.unit; objectName: "sysDiscUpc"; Layout.preferredWidth: p.unit * 12; inputMethodHints: Qt.ImhDigitsOnly
                    onAccepted: { p.pos.sysDiscAddItems([text]); text = "" } }
            Small { u: p.unit; text: qsTr("Add"); onClicked: { p.pos.sysDiscAddItems([upcField.text]); upcField.text = "" } }
            Small { u: p.unit; text: qsTr("Done"); onClicked: upcPop.close() }
        }
    }
}
