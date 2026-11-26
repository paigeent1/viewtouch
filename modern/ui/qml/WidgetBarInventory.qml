import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Dialogs
import QtQuick.Layouts

// The bar (HCA): the shelf count in bottles and tenths (3.4), by category (a liquor bought in more than one
// size: a line a size, added up); the 18 draft lines and Change keg; Link pours to bottles (each drink item to
// its bottle, with a preview, undoable); Import costs (a price sheet saved as CSV: liquor, wine, beer; sizes,
// costs, cases, vendors, drink prices; a preview, then Apply, then the offer to link the pours). The zone's props.tab opens a tab ("count", "draft", "link", "import"); a page of Change keg alone
// sets "draft".
// The Bar Variance and Kegs reports are in Reports.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property bool manager: pos ? pos.permissions.includes("manager") : false
    readonly property var info: pos && manager ? pos.barInventory : ({})
    readonly property var draft: pos ? pos.draftLines : ({})
    property string tab: zone && zone.props && zone.props.tab ? zone.props.tab : (manager ? "count" : "draft")
    readonly property bool draftOnly: zone && zone.props && zone.props.tab === "draft"

    // --- the shelf count
    property string category: ""
    property var counts: ({})   // line key (good id, or "<id>@<size>" for another size) -> bottles typed
    property int typed: 0
    readonly property var goods: (info.goods ?? []).filter(g => category === "" || g.category === category)
    readonly property int howMany: typed >= 0 ? Object.keys(counts).filter(k => String(counts[k]).trim() !== "").length : 0
    readonly property var countId: info.countId ?? 0
    onCountIdChanged: { counts = ({}); ++typed }   // saved: start over
    function saveCount() {
        const lines = []
        for (const g of (info.goods ?? [])) {
            const c = counts[g.key]
            if (c !== undefined && String(c).trim() !== "")
                lines.push(g.sizeOz > 0 ? { ingredient: g.id, size: g.sizeOz, count: String(c) }
                                        : { ingredient: g.id, count: String(c) })
        }
        pos.saveBarCount({ lines: lines })
    }

    // --- Change keg
    property string pickLine: ""
    property string newKeg: ""
    property bool kicked: true
    readonly property var picked: (draft.lines ?? []).find(l => l.id === pickLine) ?? null
    function startChange(id) {
        pickLine = id
        newKeg = ""
        kicked = true
        leftField.text = ""
    }
    property string kegSize: "half"

    // --- Import costs
    readonly property var costImport: info.costImport ?? ({})
    readonly property bool importing: (costImport.rows ?? []).length > 0 || (costImport.problems ?? []).length > 0
    property string fileError: ""
    // Applied: then the offer to link the drink items to the goods (the Link pours to bottles preview).
    property bool applied: false
    readonly property var links: info.links ?? ({})

    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 460 : 900) : 1.4
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {   // tabs
                visible: !w.draftOnly && w.manager
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [{ id: "count", text: qsTr("Shelf count") }, { id: "draft", text: qsTr("Draft lines") },
                            { id: "link", text: qsTr("Link pours to bottles") }, { id: "import", text: qsTr("Import costs") }]
                    delegate: Button {
                        required property var modelData
                        objectName: "barTab-" + modelData.id
                        text: modelData.text
                        checkable: true
                        checked: w.tab === modelData.id
                        highlighted: checked
                        implicitHeight: 46
                        font.pixelSize: 16
                        onClicked: w.tab = modelData.id
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // ---------------- Shelf count ----------------
            ColumnLayout {
                visible: w.tab === "count" && w.manager
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [""].concat(w.info.categories ?? [])
                        delegate: Button {
                            required property var modelData
                            objectName: "barCategory-" + modelData
                            text: modelData === "" ? qsTr("All") : modelData
                            checkable: true
                            checked: w.category === modelData
                            highlighted: checked
                            implicitHeight: 40
                            font.pixelSize: 14
                            onClicked: w.category = modelData
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Item"); opacity: 0.6; Layout.fillWidth: true }
                    Label { visible: !w.narrow; text: qsTr("Size"); opacity: 0.6; Layout.preferredWidth: 80 }
                    Label { text: qsTr("Last count"); opacity: 0.6; Layout.preferredWidth: 100 }
                    Label { text: qsTr("Count now"); opacity: 0.6; Layout.preferredWidth: 120 }
                }
                ListView {
                    id: countList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.tab === "count" ? w.goods : []
                    ScrollBar.vertical: TouchScrollBar { id: countBar; needed: countList.contentHeight > countList.height + 1 }
                    delegate: RowLayout {
                        id: good
                        required property var modelData
                        width: ListView.view.width - countBar.room
                        height: 48
                        Label {
                            Layout.fillWidth: true
                            text: good.modelData.name
                            font.pixelSize: 16
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label { visible: !w.narrow; text: good.modelData.size; Layout.preferredWidth: 80; opacity: 0.8 }
                        Label {
                            Layout.preferredWidth: 100
                            // A liquor in more than one size: the last count of all its sizes, in oz, on its first line.
                            text: good.modelData.lastAll ? (good.modelData.lastCount !== "" ? qsTr("%1 oz, all sizes").arg(good.modelData.lastCount) : "")
                                  : good.modelData.lastCount !== "" ? good.modelData.lastCount + " " + good.modelData.counts : qsTr("never")
                            opacity: 0.8
                        }
                        TextField {
                            objectName: "barCount-" + good.modelData.key
                            Layout.preferredWidth: 120
                            implicitHeight: 44
                            font.pixelSize: 16
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            placeholderText: good.modelData.counts
                            text: w.typed >= 0 ? (w.counts[good.modelData.key] ?? "") : ""
                            onTextEdited: { w.counts[good.modelData.key] = text; ++w.typed }
                        }
                    }
                }
                Label {
                    visible: (w.info.goods ?? []).length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: qsTr("No bottles yet: Link pours to bottles makes them from the drink items.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 12
                    text: qsTr("Bottles and tenths: 3.4 is three full bottles and one with 4 tenths left. Kept in ounces; "
                               + "the last count is this count's opening. Reports -> Bar Variance shows what's short.")
                          + " " + qsTr("A liquor bought in more than one size has a line a size: count each; they add up "
                                       + "(a size left empty counts as none).")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%n counted", "", w.howMany)
                        font.pixelSize: 16
                    }
                    Button {
                        text: qsTr("Clear")
                        implicitHeight: 52
                        font.pixelSize: 16
                        onClicked: { w.counts = ({}); ++w.typed }
                    }
                    Button {
                        objectName: "saveBarCount"
                        text: qsTr("Save Count")
                        highlighted: true
                        enabled: w.howMany > 0
                        implicitHeight: 52
                        implicitWidth: 200
                        font.pixelSize: 17
                        onClicked: w.saveCount()
                    }
                }
            }

            // ---------------- Draft lines ----------------
            ColumnLayout {
                visible: w.tab === "draft"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Label {
                    text: qsTr("Touch a line to change its keg.")
                    opacity: 0.7
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: w.narrow ? 2 : 3
                    flow: GridLayout.TopToBottom
                    rows: w.narrow ? 9 : 6
                    columnSpacing: 6
                    rowSpacing: 6
                    Repeater {
                        model: w.draft.lines ?? []
                        delegate: Button {
                            required property var modelData
                            objectName: "draftLine-" + modelData.id
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 52
                            highlighted: w.pickLine === modelData.id
                            font.pixelSize: 14
                            text: modelData.id + "  " + (modelData.keg
                                  ? modelData.keg + "\n" + qsTr("%1 oz left (%2%)").arg(modelData.left).arg(modelData.percentLeft)
                                  : qsTr("(empty)"))
                            onClicked: w.startChange(modelData.id)
                        }
                    }
                }
                // Put draft beers on empty lines (managers): the plan first, then Apply; Undo after.
                RowLayout {
                    objectName: "draftFill"
                    visible: w.manager && !w.draftOnly && ((w.draft.fill ?? []).length > 0 || w.draft.fillUndo === true)
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        objectName: "draftFillPlan"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: (w.draft.fill ?? []).length > 0
                              ? qsTr("Put draft beers on empty lines: %1").arg((w.draft.fill ?? []).map(f => f.line + " " + f.beer + (f.newKeg ? " (" + qsTr("new keg: %1").arg(f.keg) + ")" : "")).join(", "))
                              : qsTr("Draft beers were put on empty lines.")
                    }
                    Button {
                        objectName: "draftFillApply"
                        visible: (w.draft.fill ?? []).length > 0
                        text: qsTr("Apply")
                        highlighted: true
                        implicitHeight: 44
                        onClicked: w.pos.fillDraftLines()
                    }
                    Button {
                        objectName: "draftFillUndo"
                        visible: w.draft.fillUndo === true
                        text: qsTr("Undo")
                        implicitHeight: 44
                        onClicked: w.pos.undoFillDraftLines()
                    }
                }
                RowLayout {   // keg allowance and a new keg good (managers)
                    visible: w.manager && !w.draftOnly
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: qsTr("Add a keg:") }
                    TextField {
                        id: kegBeer
                        objectName: "kegBeer"
                        Layout.fillWidth: true
                        implicitHeight: 44
                        placeholderText: qsTr("Beer")
                    }
                    Repeater {
                        model: [{ id: "half", text: "1/2" }, { id: "quarter", text: "1/4" }, { id: "sixth", text: "1/6" }]
                        delegate: Button {
                            required property var modelData
                            objectName: "kegSize-" + modelData.id
                            text: modelData.text
                            checkable: true
                            checked: w.kegSize === modelData.id
                            highlighted: checked
                            implicitHeight: 44
                            onClicked: w.kegSize = modelData.id
                        }
                    }
                    TextField {
                        id: kegCost
                        objectName: "kegCost"
                        Layout.preferredWidth: 100
                        implicitHeight: 44
                        placeholderText: qsTr("Cost")
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    Button {
                        objectName: "addKeg"
                        text: qsTr("Add")
                        enabled: kegBeer.text.trim() !== ""
                        implicitHeight: 44
                        onClicked: { w.pos.addKeg({ beer: kegBeer.text, size: w.kegSize, cost: kegCost.text }); kegBeer.text = ""; kegCost.text = "" }
                    }
                    Label { text: qsTr("Allowance %") }
                    TextField {
                        id: allowance
                        objectName: "kegAllowance"
                        Layout.preferredWidth: 60
                        implicitHeight: 44
                        text: String(w.draft.allowance ?? 7)
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: if (Number(text) !== (w.draft.allowance ?? 7)) w.pos.setKegAllowance(Number(text))
                    }
                }
            }

            // ---------------- Link pours to bottles ----------------
            ColumnLayout {
                visible: w.tab === "link" && w.manager
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                readonly property var links: w.info.links ?? ({})
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Each drink takes its pour from its bottle, found by its name without the size "
                               + "(1.25oz, 2oz, Glass, Bottle, 375ml, 187ml). Draft pours (12oz, 16oz) take theirs "
                               + "from the keg on their beer's line.")
                    opacity: 0.7
                }
                Label {
                    objectName: "linkSummary"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.bold: true
                    text: qsTr("To link: %1 (new bottle goods: %2) · linked already: %3 · no size in the name: %4")
                          .arg(parent.links.toLink ?? 0).arg(parent.links.newGoods ?? 0).arg(parent.links.same ?? 0)
                          .arg(parent.links.skipped ?? 0)
                }
                ListView {
                    id: linkList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: w.tab === "link" ? (parent.links.rows ?? []) : []
                    ScrollBar.vertical: TouchScrollBar { id: linkBar; needed: linkList.contentHeight > linkList.height + 1 }
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        width: ListView.view.width - linkBar.room
                        height: 34
                        Label { text: row.modelData.item; Layout.preferredWidth: w.narrow ? 150 : 300; elide: Text.ElideRight; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: row.modelData.status === "skip" ? row.modelData.note
                                  : "→ " + row.modelData.good + (row.modelData.isNew ? "  " + qsTr("(new)") : "")
                        }
                        Label { visible: !w.narrow; text: row.modelData.amount; Layout.preferredWidth: 80 }
                        Label {
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignRight
                            color: row.modelData.status === "link" ? "#f5b940" : row.modelData.status === "same" ? "#7ee2a8" : "#9aa4b2"
                            text: row.modelData.status === "link" ? qsTr("to link") : row.modelData.status === "same" ? qsTr("linked") : qsTr("skipped")
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "undoLinkPours"
                        visible: (w.info.undo ?? 0) > 0
                        text: qsTr("Undo linking")
                        implicitHeight: 52
                        font.pixelSize: 16
                        onClicked: w.pos.undoLinkPours()
                    }
                    Button {
                        objectName: "linkPours"
                        text: qsTr("Link %n pour(s)", "", parent.parent.links.toLink ?? 0)
                        highlighted: true
                        enabled: (parent.parent.links.toLink ?? 0) > 0
                        implicitHeight: 52
                        implicitWidth: 220
                        font.pixelSize: 17
                        onClicked: w.pos.linkPours()
                    }
                }
            }

            // ---------------- Import costs ----------------
            ColumnLayout {
                objectName: "barImport"
                visible: w.tab === "import" && w.manager
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("The price sheet saved as CSV (each sheet on its own: Liquor, Wine & Port, Beer), a row a "
                               + "name and size: Category (Type for beer), the name, Size (750ml, 1L, 1.75L, 3L box; 12oz "
                               + "bottle; 1/2, 1/4, 1/6 bbl), Cost per bottle (or keg), Bottles per case, Vendor and the "
                               + "prices (1.25oz and 2oz price; Glass and Bottle price; Price and Pour). A note above the "
                               + "header is fine. Each row finds its good by name; a size, a good or a vendor not here yet "
                               + "is made; drink prices go on the drink items of that name. Nothing changes until Apply.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        objectName: "barImportChoose"
                        text: qsTr("Choose a CSV file…")
                        implicitHeight: 48
                        font.pixelSize: 16
                        onClicked: importFile.open()
                    }
                    Label {
                        objectName: "barImportSummary"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.bold: true
                        color: w.fileError !== "" ? "#f5b940" : "white"
                        text: w.fileError !== "" ? w.fileError
                              : !w.importing ? ""
                              : qsTr("New bottles: %1 · changed: %2 · no change: %3 · problems: %4 · new vendors: %5")
                                    .arg(w.costImport.newGoods ?? 0).arg(w.costImport.changed ?? 0).arg(w.costImport.same ?? 0)
                                    .arg(w.costImport.problemCount ?? 0).arg(w.costImport.vendorsMade ?? 0)
                    }
                }
                Label {
                    objectName: "barImportPrices"
                    visible: w.importing && ((w.costImport.prices ?? []).length > 0 || (w.costImport.pricesMissing ?? []).length > 0
                                             || (w.costImport.pricesSame ?? 0) > 0)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Drink prices: %1 to change · %2 already that price · %3 with no drink item of that name")
                              .arg((w.costImport.prices ?? []).length).arg(w.costImport.pricesSame ?? 0)
                              .arg((w.costImport.pricesMissing ?? []).length)
                }
                // Applied: link the drink items to the goods now (the Link tab shows the list first).
                RowLayout {
                    objectName: "barImportLinkOffer"
                    visible: w.applied && !w.importing && (w.links.toLink ?? 0) > 0
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.bold: true
                        text: qsTr("Imported. Next: link the drink items to these goods, so pours take from them: %n to link", "", w.links.toLink ?? 0)
                              + ((w.links.newGoods ?? 0) > 0 ? " " + qsTr("(%n new good(s) for drinks not in the file)", "", w.links.newGoods ?? 0) : "")
                    }
                    Button {
                        objectName: "barImportSeeLinks"
                        text: qsTr("See the list")
                        implicitHeight: 52
                        font.pixelSize: 16
                        onClicked: { w.applied = false; w.tab = "link" }
                    }
                    Button {
                        objectName: "barImportLink"
                        text: qsTr("Link %n pour(s)", "", w.links.toLink ?? 0)
                        highlighted: true
                        implicitHeight: 52
                        font.pixelSize: 16
                        onClicked: { w.applied = false; w.pos.linkPours() }
                    }
                }
                ListView {
                    id: importList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: w.tab === "import" ? (w.costImport.problems ?? []).map(p => ({ problem: p }))
                                                    .concat(w.costImport.rows ?? [])
                                                    .concat((w.costImport.prices ?? []).map(p => ({ good: p.item, size: "", status: "price",
                                                                                                   what: qsTr("price %1 → %2").arg(p.before).arg(p.after) })))
                                                    .concat((w.costImport.pricesMissing ?? []).map(p => ({ problem: p }))) : []
                    ScrollBar.vertical: TouchScrollBar { id: importBar; needed: importList.contentHeight > importList.height + 1 }
                    delegate: RowLayout {
                        id: imp
                        required property var modelData
                        width: ListView.view.width - importBar.room
                        height: 34
                        Label {
                            visible: !imp.modelData.problem
                            text: imp.modelData.good ?? ""
                            Layout.preferredWidth: w.narrow ? 140 : 260
                            elide: Text.ElideRight
                            font.bold: true
                        }
                        Label { visible: !imp.modelData.problem; text: imp.modelData.size ?? ""; Layout.preferredWidth: 70 }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: imp.modelData.problem ?? imp.modelData.what
                            color: imp.modelData.problem || imp.modelData.status === "problem" ? "#f5b940"
                                   : imp.modelData.status === "same" ? "#9aa4b2" : "white"
                        }
                        Label {
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignRight
                            color: imp.modelData.status === "new" ? "#7ee2a8" : imp.modelData.status === "change" || imp.modelData.status === "price" ? "#f5b940" : "#9aa4b2"
                            text: imp.modelData.problem ? qsTr("left out")
                                  : imp.modelData.status === "new" ? qsTr("new") : imp.modelData.status === "change" ? qsTr("change")
                                  : imp.modelData.status === "price" ? qsTr("price")
                                  : imp.modelData.status === "same" ? qsTr("no change") : qsTr("left out")
                        }
                    }
                }
                Label {
                    visible: !w.importing && !w.applied
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: qsTr("No file chosen. From a USB stick: plug it in, then find it under its name in the file list.")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        visible: w.importing && w.costImport.canApply !== true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTr("Applying changes system costs and drink prices: it needs the price book permission.")
                    }
                    Item { Layout.fillWidth: true; visible: !(w.importing && w.costImport.canApply !== true) }
                    Button {
                        objectName: "barImportCancel"
                        visible: w.importing
                        text: qsTr("Cancel")
                        implicitHeight: 52
                        font.pixelSize: 16
                        onClicked: { w.fileError = ""; w.pos.barCostImportCancel() }
                    }
                    Button {
                        objectName: "barImportApply"
                        text: qsTr("Apply %n change(s)", "", w.costImport.toApply ?? 0)
                        highlighted: true
                        enabled: (w.costImport.toApply ?? 0) > 0
                        implicitHeight: 52
                        implicitWidth: 220
                        font.pixelSize: 17
                        onClicked: { w.applied = true; w.pos.barCostImportApply() }
                    }
                }
            }
        }
    }

    FileDialog {
        id: importFile
        title: qsTr("Bar costs: a spreadsheet saved as CSV")
        nameFilters: [qsTr("Spreadsheets saved as CSV (*.csv *.txt *.tsv)"), qsTr("All files (*)")]
        onAccepted: {
            const read = w.zone && w.zone.controller ? w.zone.controller.readCsvFile(selectedFile) : { error: qsTr("Can't read files here.") }
            w.fileError = read.error ?? ""
            w.applied = false
            if (!read.error) w.pos.barCostImport(read.text)
        }
    }

    // Change keg on the line touched: the new keg, and what was left in the old one.
    Rectangle {
        objectName: "changeKegView"
        anchors.fill: parent
        visible: w.pickLine !== ""
        color: "#14181e"
        MouseArea { anchors.fill: parent }
        Item {
            width: parent.width / w.zoom
            height: parent.height / w.zoom
            scale: w.zoom
            transformOrigin: Item.TopLeft
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Label {
                    text: qsTr("Change keg on %1").arg(w.pickLine)
                    font.bold: true
                    font.pixelSize: 20
                }
                Label {
                    visible: !!(w.picked && w.picked.keg)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: w.picked && w.picked.keg
                          ? qsTr("On it now: %1, since %2 (%3). Rung up: %4 oz.").arg(w.picked.keg).arg(w.picked.since).arg(w.picked.by).arg(w.picked.poured)
                          : ""
                }
                RowLayout {
                    visible: !!(w.picked && w.picked.keg)
                    spacing: 8
                    Label { text: qsTr("The old keg:") }
                    Button {
                        objectName: "kegKicked"
                        text: qsTr("Kicked (empty)")
                        checkable: true
                        checked: w.kicked
                        highlighted: checked
                        implicitHeight: 46
                        onClicked: w.kicked = true
                    }
                    Label { text: qsTr("or oz left:") }
                    TextField {
                        id: leftField
                        objectName: "kegLeft"
                        Layout.preferredWidth: 110
                        implicitHeight: 46
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onTextEdited: w.kicked = text.trim() === ""
                    }
                }
                Label { text: qsTr("The new keg:"); font.bold: true }
                Flow {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    Repeater {
                        model: [{ id: "", name: qsTr("Nothing (leave it empty)"), onHand: "" }].concat(w.draft.kegs ?? [])
                        delegate: Button {
                            required property var modelData
                            objectName: "newKeg-" + modelData.id
                            text: modelData.name + (modelData.id ? "  ·  " + qsTr("%1 on hand").arg(modelData.onHand) : "")
                            checkable: true
                            checked: w.newKeg === modelData.id
                            highlighted: checked
                            implicitHeight: 48
                            font.pixelSize: 15
                            onClicked: w.newKeg = modelData.id
                        }
                    }
                }
                Label {
                    visible: (w.draft.kegs ?? []).length === 0
                    text: qsTr("No kegs yet: a manager adds them under Draft lines (Add a keg).")
                    opacity: 0.6
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Cancel")
                        implicitHeight: 52
                        onClicked: w.pickLine = ""
                    }
                    Button {
                        objectName: "changeKeg"
                        text: qsTr("Change keg")
                        highlighted: true
                        enabled: w.newKeg !== "" || !!(w.picked && w.picked.keg)
                        implicitHeight: 52
                        implicitWidth: 200
                        onClicked: {
                            w.pos.changeKeg({ line: w.pickLine, keg: w.newKeg, kicked: w.kicked, left: leftField.text })
                            w.pickLine = ""
                        }
                    }
                }
            }
        }
    }
}
