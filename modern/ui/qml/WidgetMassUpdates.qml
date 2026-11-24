import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Dialogs
import QtQuick.Layouts

// Menu Builder > Mass Updates (owner 2026-11-24, after ILSA's Mass Updates screen): the retail items as a
// grid, narrowed, sorted and paged on the main computer (PosService::massView: a page of 200 comes here,
// never the whole price book). Find, a range on a field, a category and its tree, No category, New items.
// Tick items (All: every item found, on every page), then a change below: prices (by margin on cost, or
// add $ / add % / set, rounded like ILSA), copy a price, costs, other fields. Each is a Preview first
// (old -> new for every item); Apply makes it one change set (Undo puts it back) that Store Updating can
// save as a file for another store, which matches it by UPC. Built for the Pi's wide screen; a narrow
// tablet scrolls sideways. Applying is an admin's (the price book permission), or an admin's PIN.
Item {
    id: w
    objectName: "massUpdates"
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property real unit: Math.max(14, Math.min(width / 80, height / 36))
    readonly property real touch: Math.max(44, unit * 2.6)   // a finger's height for every control
    readonly property real wide: unit * 84          // the grid's columns: narrower than that scrolls sideways
    readonly property var info: pos ? pos.massUpdate : ({})
    readonly property var rows: info.rows ?? []
    readonly property var preview: info.preview ?? null
    readonly property bool canApply: info.canApply === true
    readonly property string money: pos ? pos.currencySymbol : "$"
    property var view: ({ scope: "retail", find: "", category: "", path: [], noCategory: false, newDays: 0,
                          field: "", from: "", to: "", chosenOnly: false, sort: "name", desc: false, page: 0 })
    property string roundTo: "none"
    property string fileError: ""   // a change set file that couldn't be read

    function setView(changes) {
        const v = Object.assign({}, view, changes)
        if (changes.page === undefined) v.page = 0
        view = v
        if (pos) pos.massView(v)
    }
    function sortBy(key) { setView({ sort: key, desc: view.sort === key ? !view.desc : false }) }
    function why() { return whyField.text.trim() }
    function previewAction(a) { if (pos) pos.massPreview(Object.assign({ round: roundTo, why: why() }, a)) }

    // The grid comes to a tablet only while a screen shows it (PosServer: on request).
    Item {
        visible: false
        readonly property var p: w.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("massUpdate", false)
            asked = p
            if (p) p.want("massUpdate", true)
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) { asked.want("massUpdate", false); asked.massClose() }
    }
    Component.onCompleted: {
        if (!pos || !pos.loggedIn) return
        // From Menu Builder's Select: those items, ticked and shown alone.
        const from = zone && zone.controller ? zone.controller.take("massUpdates") : undefined
        if (from && from.scope) view = Object.assign({}, view, { scope: from.scope })
        if (from && from.ids && from.ids.length > 0) {
            view = Object.assign({}, view, { chosenOnly: true })
            pos.massView(view)
            pos.massChooseOnly(from.ids)
        } else {
            pos.massView(view)
        }
    }
    Timer { id: findLater; interval: 350; onTriggered: w.setView({ find: findField.text }) }

    // (Inline components don't see the ids around them: each is given the size, u.)
    component Small: Button {
        property real u: 16
        implicitHeight: Math.max(44, u * 2.6)
        Layout.minimumWidth: 48   // a finger's width, however short its text
        font.pixelSize: u * 0.8
    }
    component Pick: Button {
        property real u: 16
        implicitHeight: Math.max(44, u * 2.6)
        Layout.minimumWidth: 48   // a finger's width, however short its text
        font.pixelSize: u * 0.75
        checkable: true
        highlighted: checked
    }
    component Field: TextField {
        property real u: 16
        implicitHeight: Math.max(44, u * 2.6)
        font.pixelSize: u * 0.8
    }
    component Choice: ComboBox {
        property real u: 16
        implicitHeight: Math.max(44, u * 2.6)
        font.pixelSize: u * 0.75
    }

    Flickable {
        id: sideways
        anchors.fill: parent
        contentWidth: Math.max(width, w.wide)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: contentWidth > width

        ColumnLayout {
            width: sideways.contentWidth
            height: sideways.height
            spacing: 6

            // --- narrowing: find, category, its tree, the shortcuts ---
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Field { u: w.unit;
                    id: findField
                    objectName: "massFind"
                    Layout.preferredWidth: w.unit * 16
                    placeholderText: qsTr("Find: name, UPC or item #")
                    onTextEdited: findLater.restart()
                    onAccepted: { findLater.stop(); w.setView({ find: text }) }
                }
                Choice { u: w.unit;
                    objectName: "massCategory"
                    Layout.preferredWidth: w.unit * 11
                    readonly property var cats: w.info.categories ?? []
                    model: [qsTr("All categories")].concat(cats.map(c => c.name))
                    currentIndex: Math.max(0, cats.findIndex(c => c.id === w.view.category) + 1)
                    onActivated: i => w.setView({ category: i === 0 ? "" : cats[i - 1].id, path: [] })
                }
                Choice { u: w.unit;
                    objectName: "massBrand"
                    Layout.preferredWidth: w.unit * 10
                    readonly property var list: w.info.brands ?? []
                    model: [qsTr("All brands")].concat(list)
                    currentIndex: Math.max(0, list.indexOf(w.view.path[0] ?? "") + 1)
                    onActivated: i => w.setView({ path: i === 0 ? [] : [list[i - 1]] })
                }
                Choice { u: w.unit;
                    objectName: "massGroup"
                    visible: (w.info.groups ?? []).length > 0
                    Layout.preferredWidth: w.unit * 9
                    readonly property var list: w.info.groups ?? []
                    model: [qsTr("All product groups")].concat(list)
                    currentIndex: Math.max(0, list.indexOf(w.view.path[1] ?? "") + 1)
                    onActivated: i => w.setView({ path: i === 0 ? [w.view.path[0]] : [w.view.path[0], list[i - 1]] })
                }
                Choice { u: w.unit;
                    objectName: "massSub"
                    visible: (w.info.subs ?? []).length > 0
                    Layout.preferredWidth: w.unit * 9
                    readonly property var list: w.info.subs ?? []
                    model: [qsTr("All sub groups")].concat(list)
                    currentIndex: Math.max(0, list.indexOf(w.view.path[2] ?? "") + 1)
                    onActivated: i => w.setView({ path: i === 0 ? w.view.path.slice(0, 2) : [w.view.path[0], w.view.path[1], list[i - 1]] })
                }
                Pick { u: w.unit;
                    objectName: "massNoCategory"
                    text: qsTr("No category (%1)").arg(w.info.noCategoryCount ?? 0)
                    checked: w.view.noCategory
                    onClicked: w.setView({ noCategory: checked })
                }
                Pick { u: w.unit;
                    objectName: "massNewItems"
                    text: qsTr("New items")
                    checked: w.view.newDays > 0
                    onClicked: w.setView({ newDays: checked ? Math.max(1, Number(newDays.text) || 30) : 0 })
                }
                Field { u: w.unit;
                    id: newDays
                    Layout.preferredWidth: w.unit * 3.5
                    text: "30"
                    inputMethodHints: Qt.ImhDigitsOnly
                    onAccepted: if (w.view.newDays > 0) w.setView({ newDays: Math.max(1, Number(text) || 30) })
                }
                Label { text: qsTr("days"); font.pixelSize: w.unit * 0.8 }
                Item { Layout.fillWidth: true }
            }
            // --- a range on a field (ILSA's Sort by Field, Start Value, End Value, Apply Range) ---
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("Range on"); font.pixelSize: w.unit * 0.8 }
                Choice { u: w.unit;
                    id: rangeField
                    objectName: "massRangeField"
                    Layout.preferredWidth: w.unit * 10
                    readonly property var keys: ["name", "upc", "category", "brand", "cost", "price", "margin"]
                    model: [qsTr("Description"), qsTr("UPC"), qsTr("Category"), qsTr("Brand"), qsTr("Cost"), qsTr("Price"), qsTr("Margin %")]
                }
                Field { u: w.unit; id: rangeFrom; objectName: "massRangeFrom"; Layout.preferredWidth: w.unit * 10; placeholderText: qsTr("Start value") }
                Label { text: qsTr("to"); font.pixelSize: w.unit * 0.8 }
                Field { u: w.unit; id: rangeTo; objectName: "massRangeTo"; Layout.preferredWidth: w.unit * 10; placeholderText: qsTr("End value") }
                Small { u: w.unit;
                    objectName: "massRangeApply"
                    text: qsTr("Apply range")
                    onClicked: w.setView({ field: rangeField.keys[rangeField.currentIndex], from: rangeFrom.text, to: rangeTo.text })
                }
                Small { u: w.unit;
                    visible: w.view.field !== ""
                    text: qsTr("Clear range")
                    onClicked: { rangeFrom.text = ""; rangeTo.text = ""; w.setView({ field: "", from: "", to: "" }) }
                }
                Item { Layout.fillWidth: true }
            }
            // --- how many, choosing, pages ---
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    objectName: "massCount"
                    text: qsTr("%1 of %2 items · %3 chosen").arg(Number(w.info.count ?? 0).toLocaleString(Qt.locale(), "f", 0))
                                                            .arg(Number(w.info.total ?? 0).toLocaleString(Qt.locale(), "f", 0))
                                                            .arg(Number(w.info.chosen ?? 0).toLocaleString(Qt.locale(), "f", 0))
                    font.pixelSize: w.unit * 0.85
                    font.bold: true
                }
                Small { u: w.unit; objectName: "massAll"; text: qsTr("All"); onClicked: w.pos.massChooseAll() }
                Small { u: w.unit; objectName: "massNone"; text: qsTr("None"); onClicked: { w.view = Object.assign({}, w.view, { chosenOnly: false }); w.pos.massChooseNone() } }
                Pick { u: w.unit;
                    objectName: "massChosenOnly"
                    text: qsTr("Show chosen only")
                    checked: w.view.chosenOnly
                    onClicked: w.setView({ chosenOnly: checked })
                }
                Item { Layout.fillWidth: true }
                Small { u: w.unit; text: "‹"; enabled: (w.info.page ?? 0) > 0; onClicked: w.setView({ page: (w.info.page ?? 0) - 1 }) }
                Label {
                    text: qsTr("Page %1 of %2").arg((w.info.page ?? 0) + 1).arg(w.info.pages ?? 1)
                    font.pixelSize: w.unit * 0.8
                }
                Small { u: w.unit; objectName: "massNextPage"; text: "›"; enabled: (w.info.page ?? 0) + 1 < (w.info.pages ?? 1); onClicked: w.setView({ page: (w.info.page ?? 0) + 1 }) }
            }

            // --- the grid ---
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#f4f6fa"
                radius: 4
                readonly property var columns: [["", qsTr("✓"), 2.8], ["upc", qsTr("UPC"), 8], ["name", qsTr("Description"), 0],
                                                ["category", qsTr("Category"), 9], ["brand", qsTr("Brand"), 9],
                                                ["group", qsTr("Product group"), 7], ["cost", qsTr("Cost"), 5],
                                                ["price", qsTr("Price"), 5], ["margin", qsTr("Margin %"), 5],
                                                ["soldOut", qsTr("Sold out"), 4.5]]
                id: grid
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2
                    RowLayout {   // headings: touch to sort, again to reverse
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: grid.columns
                            delegate: Label {
                                required property var modelData
                                objectName: "massSort-" + modelData[0]
                                text: modelData[1] + (modelData[0] !== "" && w.view.sort === modelData[0] ? (w.view.desc ? " ▼" : " ▲") : "")
                                Layout.preferredWidth: modelData[2] > 0 ? w.unit * modelData[2] : -1
                                Layout.fillWidth: modelData[2] === 0
                                Layout.preferredHeight: w.touch
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: w.unit * 0.72
                                font.bold: true
                                color: "#1d3fb8"
                                padding: w.unit * 0.3
                                TapHandler { enabled: modelData[0] !== ""; onTapped: { if (TouchGuard.covered(point.scenePressPosition, parent)) return; w.sortBy(modelData[0]) } }
                            }
                        }
                    }
                    ListView {
                        id: rowList
                        objectName: "massList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 1
                        model: w.rows
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            required property int index
                            objectName: "massRow-" + modelData.id
                            width: ListView.view.width
                            height: w.touch
                            color: modelData.chosen ? "#c7d2fe" : row.index % 2 ? "#ffffff" : "#e9edf5"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                spacing: 4
                                CheckBox {
                                    objectName: "massTick-" + row.modelData.id
                                    Layout.preferredWidth: w.unit * 2.8
                                    Layout.minimumWidth: 44
                                    Layout.fillHeight: true
                                    checked: row.modelData.chosen
                                    onClicked: w.pos.massChoose([row.modelData.id], checked)
                                }
                                Repeater {
                                    model: grid.columns.slice(1)
                                    delegate: Label {
                                        required property var modelData
                                        readonly property string key: modelData[0]
                                        readonly property var value: row.modelData[key]
                                        Layout.preferredWidth: modelData[2] > 0 ? w.unit * modelData[2] : -1
                                        Layout.fillWidth: modelData[2] === 0
                                        text: key === "soldOut" ? (value ? qsTr("yes") : "")
                                            : (key === "cost" || key === "price") && value !== "" ? w.money + value
                                            : key === "margin" && value !== "" ? value + "%" : (value ?? "")
                                        elide: Text.ElideRight
                                        horizontalAlignment: key === "cost" || key === "price" || key === "margin" ? Text.AlignRight : Text.AlignLeft
                                        font.pixelSize: w.unit * 0.68
                                        color: key === "soldOut" ? "#b91c1c" : "#111827"
                                    }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: w.rows.length === 0
                            text: qsTr("No items found.")
                            font.pixelSize: w.unit
                            color: "#374151"
                        }
                    }
                }
            }

            Label {
                objectName: "massMessage"
                visible: text !== ""
                Layout.fillWidth: true
                text: w.fileError !== "" ? w.fileError : (w.info.message ?? "")
                font.pixelSize: w.unit * 0.85
                color: "#f5b940"
                wrapMode: Text.WordWrap
            }

            // --- what to do with the chosen ones ---
            TabBar {
                id: tabs
                Layout.fillWidth: true
                font.pixelSize: w.unit * 0.8
                TabButton { implicitHeight: w.touch; objectName: "massTabPrices"; text: qsTr("Standard Prices") }
                TabButton { implicitHeight: w.touch; objectName: "massTabCopy"; text: qsTr("Copy Prices") }
                TabButton { implicitHeight: w.touch; objectName: "massTabCost"; text: qsTr("Cost Updates") }
                TabButton { implicitHeight: w.touch; objectName: "massTabFields"; text: qsTr("Field Updates") }
                TabButton { implicitHeight: w.touch; objectName: "massTabStores"; text: qsTr("Store Updating") }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(w.unit * 6.2, w.touch * 2 + 12)
                currentIndex: tabs.currentIndex
                // Prices: by margin, or by hand.
                ColumnLayout {
                    spacing: 4
                    RowLayout {
                        spacing: 6
                        Label { text: qsTr("Gross margin price:"); font.pixelSize: w.unit * 0.8; font.bold: true }
                        Field { u: w.unit; id: marginPct; objectName: "massMargin"; Layout.preferredWidth: w.unit * 5; placeholderText: "40"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                        Label { text: qsTr("% on cost"); font.pixelSize: w.unit * 0.8 }
                        Small { u: w.unit; objectName: "massMarginPreview"; text: qsTr("Preview"); onClicked: w.previewAction({ kind: "margin", amount: marginPct.text }) }
                        Label { text: qsTr("Round to:"); font.pixelSize: w.unit * 0.8 }
                        Repeater {   // ILSA's Round to (the manual price uses it too)
                            model: [["none", qsTr("None")], ["5up", qsTr("5 (up)")], ["9up", qsTr("9 (up)")],
                                    ["9near", qsTr("9 (up/down)")], ["5or9up", qsTr("5 or 9 (up)")]]
                            delegate: Pick {
                                required property var modelData
                                u: w.unit
                                objectName: "massRound-" + modelData[0]
                                text: modelData[1]
                                checked: w.roundTo === modelData[0]
                                onClicked: w.roundTo = modelData[0]
                            }
                        }
                    }
                    RowLayout {
                        spacing: 6
                        Label { text: qsTr("Manual price:"); font.pixelSize: w.unit * 0.8; font.bold: true }
                        Choice { u: w.unit;
                            id: priceHow
                            objectName: "massPriceHow"
                            Layout.preferredWidth: w.unit * 8
                            readonly property var keys: ["add", "percent", "set"]
                            model: [qsTr("Add $ to price"), qsTr("Add % to price"), qsTr("Set price")]
                        }
                        Field { u: w.unit; id: priceAmount; objectName: "massPriceAmount"; Layout.preferredWidth: w.unit * 5; placeholderText: "0.00"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                        Small { u: w.unit;
                            objectName: "massPricePreview"
                            text: qsTr("Preview")
                            onClicked: w.previewAction({ kind: "price", how: priceHow.keys[priceHow.currentIndex], amount: priceAmount.text })
                        }
                        Label { text: qsTr("(same Round to)"); font.pixelSize: w.unit * 0.7; opacity: 0.7 }
                    }
                }
                // Copy one item's price to the others.
                RowLayout {
                    spacing: 6
                    Label { text: qsTr("Copy the price of (UPC or item #):"); font.pixelSize: w.unit * 0.8 }
                    Field { u: w.unit; id: copyFrom; objectName: "massCopyFrom"; Layout.preferredWidth: w.unit * 12 }
                    Label { text: qsTr("to the chosen items"); font.pixelSize: w.unit * 0.8 }
                    Small { u: w.unit; objectName: "massCopyPreview"; text: qsTr("Preview"); onClicked: w.previewAction({ kind: "copy", from: copyFrom.text, round: "none" }) }
                    Item { Layout.fillWidth: true }
                }
                // Costs.
                RowLayout {
                    spacing: 6
                    Choice { u: w.unit;
                        id: costHow
                        objectName: "massCostHow"
                        Layout.preferredWidth: w.unit * 8
                        readonly property var keys: ["set", "add", "percent"]
                        model: [qsTr("Set cost"), qsTr("Add $ to cost"), qsTr("Add % to cost")]
                    }
                    Field { u: w.unit; id: costAmount; objectName: "massCostAmount"; Layout.preferredWidth: w.unit * 5; placeholderText: "0.00"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                    Small { u: w.unit;
                        objectName: "massCostPreview"
                        text: qsTr("Preview")
                        onClicked: w.previewAction({ kind: "cost", how: costHow.keys[costHow.currentIndex], amount: costAmount.text, round: "none" })
                    }
                    Label { text: qsTr("A box's singles get their cost from it, as on the card."); font.pixelSize: w.unit * 0.7; opacity: 0.7 }
                    Item { Layout.fillWidth: true }
                }
                // Other fields.
                RowLayout {
                    spacing: 6
                    Choice { u: w.unit;
                        id: fieldPick
                        objectName: "massFieldPick"
                        Layout.preferredWidth: w.unit * 9
                        readonly property var keys: ["category", "taxClass", "soldOut", "noDiscount", "buttonColor"]
                        model: [qsTr("Move to category"), qsTr("Tax"), qsTr("Sold out / for sale"), qsTr("No discounts"), qsTr("Button color")]
                    }
                    Choice { u: w.unit;
                        id: fieldValue
                        objectName: "massFieldValue"
                        visible: fieldPick.currentIndex !== 4
                        Layout.preferredWidth: w.unit * 11
                        readonly property var choices: {
                            switch (fieldPick.keys[fieldPick.currentIndex]) {
                            case "category": return (w.info.categories ?? []).map(c => [c.id, c.name])
                            case "taxClass": return [["food", qsTr("Food")], ["alcohol", qsTr("Alcohol")], ["merchandise", qsTr("Merchandise")], ["room", qsTr("Room")]]
                            case "soldOut": return [["yes", qsTr("Sold out")], ["no", qsTr("For sale")]]
                            default: return [["yes", qsTr("No discounts")], ["no", qsTr("Discounts allowed")]]
                            }
                        }
                        model: choices.map(c => c[1])
                    }
                    Field { u: w.unit;
                        id: colorValue
                        visible: fieldPick.currentIndex === 4
                        Layout.preferredWidth: w.unit * 8
                        placeholderText: qsTr("#2b62b0 (empty: none)")
                    }
                    Small { u: w.unit;
                        objectName: "massFieldPreview"
                        text: qsTr("Preview")
                        onClicked: w.previewAction({ kind: "field", field: fieldPick.keys[fieldPick.currentIndex], round: "none",
                                                     value: fieldPick.currentIndex === 4 ? colorValue.text
                                                          : (fieldValue.choices[fieldValue.currentIndex] ?? [""])[0] })
                    }
                    Item { Layout.fillWidth: true }
                }
                // Other stores: a change set as a file, one from another store, the ones applied here.
                RowLayout {
                    spacing: 6
                    ColumnLayout {
                        Layout.preferredWidth: w.unit * 16
                        spacing: 4
                        Small { u: w.unit;
                            objectName: "massImport"
                            Layout.fillWidth: true
                            text: qsTr("Import a change set…")
                            onClicked: importFile.open()
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Another store's file: its items are found here by UPC and shown first.")
                            font.pixelSize: w.unit * 0.65
                            opacity: 0.7
                        }
                    }
                    ListView {
                        objectName: "massSets"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: w.info.sets ?? []
                        delegate: RowLayout {
                            required property var modelData
                            width: ListView.view.width
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: w.unit * 0.7
                                text: modelData.when + " · " + modelData.label + " · " + qsTr("%n change(s)", "", modelData.count)
                                      + " · " + modelData.who + (modelData.from && modelData.from !== w.pos.storeName ? " · " + qsTr("from %1").arg(modelData.from) : "")
                                      + (modelData.undone ? " · " + qsTr("undone") : "")
                            }
                            Small { u: w.unit; text: qsTr("Export"); onClicked: w.zone.controller.exportChangeSet(modelData.id) }
                        }
                        Label {
                            visible: (w.info.sets ?? []).length === 0
                            text: qsTr("No changes applied here yet.")
                            font.pixelSize: w.unit * 0.75
                            opacity: 0.7
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Field { u: w.unit; id: whyField; objectName: "massWhy"; Layout.preferredWidth: w.unit * 20; placeholderText: qsTr("Why (optional): kept with the change") }
                Label {
                    visible: !w.canApply
                    text: qsTr("Applying needs an admin (or an admin's PIN).")
                    font.pixelSize: w.unit * 0.75
                    opacity: 0.8
                }
                Item { Layout.fillWidth: true }
                Small { u: w.unit;
                    objectName: "massUndo"
                    visible: !!w.info.last && !w.info.last.undone
                    text: w.info.last ? qsTr("Undo: %1").arg(w.info.last.label) : ""
                    onClicked: w.pos.massUndo()
                }
            }
        }
    }

    FileDialog {
        id: importFile
        title: qsTr("A change set from another store")
        nameFilters: [qsTr("ViewTouch change sets (*.vtchanges.json *.json)"), qsTr("All files (*)")]
        onAccepted: {
            const read = w.zone.controller.readChangeSetFile(selectedFile)
            w.fileError = read.error ?? ""
            if (!read.error) w.pos.massImport(read)
        }
    }

    // --- the preview: old -> new for every item, then Apply or Cancel ---
    Rectangle {
        objectName: "massPreview"
        visible: w.preview !== null
        anchors.fill: parent
        z: 10
        color: "#e6111827"
        MouseArea { anchors.fill: parent }   // (touches don't reach the grid under it)
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: w.unit
            spacing: 6
            Label {
                Layout.fillWidth: true
                color: "white"
                font.pixelSize: w.unit * 1.1
                font.bold: true
                wrapMode: Text.WordWrap
                text: !w.preview ? ""
                    : (w.preview.kind === "import" ? qsTr("From %1: ").arg(w.preview.from) : "") + w.preview.label
            }
            Label {
                objectName: "massPreviewCount"
                Layout.fillWidth: true
                color: "white"
                font.pixelSize: w.unit * 0.9
                wrapMode: Text.WordWrap
                text: !w.preview ? ""
                    : qsTr("%n change(s) to %1 item(s). Nothing is changed until Apply.", "", w.preview.count).arg(w.preview.items)
                      + (w.preview.skippedCount > 0 ? "  " + qsTr("%n left out (below).", "", w.preview.skippedCount) : "")
            }
            ListView {
                objectName: "massPreviewList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                model: w.preview ? w.preview.lines : []
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: w.unit * 1.8
                    color: index % 2 ? "#1f2937" : "#273244"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 6
                        Label { Layout.preferredWidth: w.unit * 8; text: modelData.upc; color: "#cbd5e1"; font.pixelSize: w.unit * 0.7 }
                        Label { Layout.fillWidth: true; text: modelData.item; elide: Text.ElideRight; color: "white"; font.pixelSize: w.unit * 0.7 }
                        Label { Layout.preferredWidth: w.unit * 6; text: modelData.fieldLabel; color: "#cbd5e1"; font.pixelSize: w.unit * 0.7 }
                        Label {
                            Layout.preferredWidth: w.unit * 16
                            readonly property bool cash: modelData.field === "price" || modelData.field === "cost"
                            text: (cash && modelData.before !== "" ? w.money : "") + (modelData.before === "" ? "—" : modelData.before)
                                  + "  →  " + (cash && modelData.after !== "" ? w.money : "") + (modelData.after === "" ? "—" : modelData.after)
                            color: "#fde68a"
                            font.pixelSize: w.unit * 0.75
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
            Label {
                objectName: "massPreviewSkipped"
                visible: !!w.preview && w.preview.skippedCount > 0
                Layout.fillWidth: true
                Layout.maximumHeight: w.unit * 4
                color: "#f5b940"
                font.pixelSize: w.unit * 0.7
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                text: !w.preview ? "" : qsTr("Left out: %1").arg(w.preview.skipped.map(s => (s.item || s.upc) + " (" + s.why + ")").join(", "))
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Small { u: w.unit; text: "‹"; visible: !!w.preview && w.preview.pages > 1; enabled: !!w.preview && w.preview.page > 0; onClicked: w.pos.massPreviewPage(w.preview.page - 1) }
                Label {
                    visible: !!w.preview && w.preview.pages > 1
                    color: "white"
                    text: w.preview ? qsTr("Page %1 of %2").arg(w.preview.page + 1).arg(w.preview.pages) : ""
                    font.pixelSize: w.unit * 0.8
                }
                Small { u: w.unit; text: "›"; visible: !!w.preview && w.preview.pages > 1; enabled: !!w.preview && w.preview.page + 1 < w.preview.pages; onClicked: w.pos.massPreviewPage(w.preview.page + 1) }
                Item { Layout.fillWidth: true }
                Label {
                    visible: !w.canApply
                    color: "#cbd5e1"
                    text: qsTr("An admin's PIN will be asked for.")
                    font.pixelSize: w.unit * 0.75
                }
                Small { u: w.unit;
                    objectName: "massApply"
                    text: qsTr("Apply")
                    enabled: !!w.preview && w.preview.count > 0
                    palette.button: "#15803d"; palette.buttonText: "white"
                    onClicked: w.pos.massApply()
                }
                Small { u: w.unit; objectName: "massCancel"; text: qsTr("Cancel"); onClicked: w.pos.massCancel() }
            }
        }
    }
}
