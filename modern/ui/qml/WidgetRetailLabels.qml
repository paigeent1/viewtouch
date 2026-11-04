import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Labels from any screen (a tablet in the stock room or kitchen), one step at a time:
//   1. the label styles across the top;
//   2. the tree, one screen per level (group, category, brand; the ILSA price book also has product
//      group and sub group), with Show Items on every level;
//   3. the items, touched to add to the list;
//   4. the list: which UPC, how many, print order, preview, print.
// The widget's own ‹ Back steps back one screen; a scan or a typed name finds items from any step.
// The main computer's label utility draws and prints the labels, exactly as on its own screen.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.retailLabels : ({})
    readonly property var templates: info.templates ?? []
    property string chosen: ""
    readonly property var chosenOne: templates.find(t => t.name === chosen)
    readonly property bool product: chosenOne ? chosenOne.product : true
    readonly property var path: info.path ?? []

    // "label" | "tree" | "items" | "find" | "list"
    property string step: "label"
    property string cameFrom: "tree"   // where Find and the list go back to

    // [{name, upcs: [{label, code}], pick (index into upcs), qty, seq (order added)}]
    property var queue: []
    property int added: 0
    property int aloneQty: 1
    readonly property int total: product ? queue.reduce((n, q) => n + q.qty, 0) : aloneQty
    // The order labels print in (and the list shows): as added, by description, or by the UPC on the label.
    property string sortBy: "added"
    onSortByChanged: queue = sorted(queue)
    property bool previewOpen: false

    // The items list: A to Z by description, or by UPC; a letter row jumps to a letter (as on the main
    // computer's label utility).
    property string itemSort: "name"
    readonly property var shownItems: {
        const first = x => (x.upcs && x.upcs.length ? String(x.upcs[0].code) : "")
        const list = (info.matches ?? []).slice()
        return list.sort(itemSort === "upc"
                         ? (a, b) => (first(a).length - first(b).length) || first(a).localeCompare(first(b))
                         : (a, b) => String(a.name).localeCompare(String(b.name)))
    }
    function letterOf(text) {
        const c = String(text ?? "").trim().charAt(0).toUpperCase()
        return c >= "A" && c <= "Z" ? c : "#"
    }
    // A row of letters; those nothing in `names` starts with are greyed.
    // (Self-contained: an inline component can't see this file's ids.)
    component LetterRow: RowLayout {
        id: lr
        property var names: []
        signal picked(string letter)
        function letterOf(text) {
            const c = String(text ?? "").trim().charAt(0).toUpperCase()
            return c >= "A" && c <= "Z" ? c : "#"
        }
        readonly property var present: {
            const seen = {}
            for (const n of names)
                seen[lr.letterOf(n)] = true
            return seen
        }
        spacing: 2
        Repeater {
            model: "#ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("")
            delegate: Button {
                required property string modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                leftPadding: 0
                rightPadding: 0
                implicitHeight: 50
                font.pixelSize: 16
                font.bold: true
                enabled: lr.present[modelData] === true
                text: modelData
                onClicked: lr.picked(modelData)
            }
        }
    }

    function sorted(list) {
        const code = q => q.upcs[q.pick].code
        const by = {
            added: (a, b) => a.seq - b.seq,
            name: (a, b) => a.name.localeCompare(b.name) || a.seq - b.seq,
            upc: (a, b) => (code(a).length - code(b).length) || code(a).localeCompare(code(b)) || a.seq - b.seq
        }[sortBy]
        return list.slice().sort(by)
    }
    function queued(name) {
        const q = queue.find(x => x.name === name)
        return q ? q.qty : 0
    }
    function add(m) {
        const i = queue.findIndex(q => q.name === m.name)
        const next = queue.slice()
        if (i >= 0) next[i] = Object.assign({}, next[i], { qty: Math.min(500, next[i].qty + 1) })
        else next.push({ name: m.name, upcs: m.upcs, pick: 0, qty: 1, seq: ++added })
        queue = sorted(next)
    }
    function change(i, fields) {
        const next = queue.slice()
        next[i] = Object.assign({}, next[i], fields)
        queue = sorted(next)
    }
    function remove(i) {
        const next = queue.slice()
        next.splice(i, 1)
        queue = next
    }
    function printLabels() {
        const items = product ? queue.map(q => ({ upc: q.upcs[q.pick].code, qty: q.qty })) : []
        pos.printRetailLabels({ template: chosen, items: items, qty: aloneQty })
        queue = []
        aloneQty = 1
        if (previewOpen) closePreview()
        step = product ? "tree" : "label"
        if (product) browseTo(wantPath)
    }
    function preview(q) {
        previewOpen = true
        if (q) pos.previewRetailLabel({ template: chosen, upc: q.upcs[q.pick].code, name: q.name })
        else pos.previewRetailLabel({ template: chosen })
    }
    function closePreview() {
        previewOpen = false
        pos.closeRetailLabelPreview()
    }

    // Moving between the steps. wantPath is the level last asked of the main computer, so a reply
    // still on its way (tablets) isn't taken for the new one.
    property var wantPath: []
    function samePath(a, b) { return JSON.stringify(a ?? []) === JSON.stringify(b ?? []) }
    function browseTo(p) {
        wantPath = p
        pos.browseRetailLabels(p)
    }
    function itemsAt(p) {
        wantPath = p
        pos.showRetailLabelItems(p)
    }
    // A level with nothing below it shows its items by itself.
    function syncStep() {
        if (step === "tree" && (info.showingItems ?? false) && !(info.showItemsAsked ?? false) && samePath(info.path, wantPath))
            step = "items"
        checkScan()
    }
    onInfoChanged: syncStep()
    // A scan (the code and Enter): when the answer comes back with exactly one item, it goes on the list.
    property string scanCode: ""
    function checkScan() {
        if (scanCode === "" || (info.query ?? "") !== scanCode)
            return
        const m = info.matches ?? []
        scanCode = ""
        if (m.length === 1) {
            add(m[0])
            findField.text = ""
            back()
        }
    }
    function chooseLabel(name) {
        chosen = name
        const t = templates.find(x => x.name === name)
        if (t && !t.product) {          // a label without item fields: just how many
            cameFrom = "label"
            step = "list"
            return
        }
        step = "tree"
        browseTo([])
    }
    function openLevel(name) {
        step = "tree"
        browseTo(path.concat([name]))
    }
    function showItems() {
        step = "items"
        itemsAt(path)
    }
    function openList() {
        if (step !== "list") cameFrom = step
        step = "list"
    }
    function findItems(text) {
        if (step !== "find") cameFrom = step
        step = "find"
        pos.findRetailLabels(text)
    }
    function back() {
        if (previewOpen) { closePreview(); return }
        if (step === "list" || step === "find") {
            const to = step === "list" && cameFrom === "find" ? "tree" : cameFrom
            step = to
            findField.text = ""
            if (to === "items") itemsAt(wantPath)
            else if (to === "tree") browseTo(wantPath)
            return
        }
        if (step === "items") {
            // Back to the level whose items these are; a level with nothing below it: the one above.
            const leaf = (info.children ?? []).length === 0
            step = "tree"
            browseTo(leaf && path.length > 0 ? path.slice(0, path.length - 1) : path)
            return
        }
        if (step === "tree" && path.length > 0) {
            browseTo(path.slice(0, path.length - 1))
            return
        }
        step = "label"
    }

    Component.onCompleted: {
        if (pos) browseTo([])
    }

    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 460 : 1000) : 1.2
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Label {
                visible: !(w.info.ready ?? true)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#fca5a5"
                text: qsTr("Labels print from the main computer, and its label utility isn't installed there.")
            }

            // ---- 1. The label styles across the top ------------------------------------------------
            ColumnLayout {
                visible: w.step === "label"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                RowLayout {
                    spacing: 12
                    // The first screen's Back leaves the Labels screen (later screens' Back steps back here).
                    Button {
                        implicitHeight: 56
                        implicitWidth: 120
                        font.pixelSize: 18
                        text: qsTr("‹ Back")
                        onClicked: if (w.zone && w.zone.controller) w.zone.controller.goBack()
                    }
                    Label { text: qsTr("Choose a label"); font.pixelSize: 22; font.bold: true }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: w.templates
                        delegate: Button {
                            required property var modelData
                            width: w.narrow ? (parent.width - 10) / 2 : Math.max(200, (parent.width - 30) / 4)
                            implicitHeight: 96
                            font.pixelSize: 17
                            highlighted: w.chosen === modelData.name
                            text: modelData.name + "\n" + modelData.size
                            onClicked: w.chooseLabel(modelData.name)
                        }
                    }
                }
                Label {
                    visible: w.templates.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("No labels yet: open the Label Utility on the main computer once.")
                }
                Item { Layout.fillHeight: true }
                Button {
                    visible: w.queue.length > 0
                    Layout.alignment: Qt.AlignRight
                    implicitHeight: 60
                    font.pixelSize: 17
                    text: qsTr("To Print: %n label(s) ›", "", w.total)
                    onClicked: w.openList()
                }
            }

            // ---- The bar above every later step: Back, where we are, find/scan, the list ------------
            RowLayout {
                visible: w.step !== "label"
                Layout.fillWidth: true
                spacing: 8
                Button {
                    implicitHeight: 56
                    implicitWidth: 120
                    font.pixelSize: 18
                    text: qsTr("‹ Back")
                    onClicked: w.back()
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pixelSize: 16
                    text: {
                        const where = w.step === "find" ? [qsTr("Find")]
                                    : w.step === "list" ? [qsTr("To print")]
                                    : w.path.length ? w.path : [qsTr("All items")]
                        return [w.chosen].concat(where).join("  ›  ")
                    }
                }
                TextField {
                    id: findField
                    visible: w.product && w.step !== "list"
                    objectName: "retailLabelSearch"
                    Layout.preferredWidth: w.narrow ? 150 : 300
                    implicitHeight: 52
                    font.pixelSize: 17
                    placeholderText: qsTr("Find or scan…")
                    onTextEdited: {
                        if (text.trim() === "") w.back()
                        else w.findItems(text)
                    }
                    // A scanner types the code and Enter: the one item it matches goes on the list.
                    onAccepted: {
                        w.scanCode = text.trim()
                        w.findItems(text)
                        w.checkScan()
                    }
                }
                // The tablet's camera reads a barcode into the search (the Pi says it has no camera).
                Rectangle {
                    objectName: "retailLabelCamera"
                    visible: w.product && w.step !== "list"
                    implicitWidth: w.narrow ? 64 : 150
                    implicitHeight: 56
                    radius: 8
                    color: camTap.pressed ? "#d4a017" : "#facc15"
                    border.color: "#a16207"
                    Text {
                        anchors.centerIn: parent
                        text: w.narrow ? "📷" : qsTr("📷 Scan")
                        color: "#1f2937"
                        font.pixelSize: 19
                        font.bold: true
                    }
                    TapHandler {
                        id: camTap
                        onTapped: {
                            // The search box lets go first, so the tablet's keyboard doesn't cover the camera.
                            findField.focus = false
                            Qt.inputMethod.hide()
                            if (w.zone && w.zone.controller) w.zone.controller.command("scanCameraField")
                        }
                    }
                }
                Connections {
                    target: w.zone ? w.zone.controller : null
                    function onCameraCode(code) {
                        if (!findField.visible) return
                        findField.text = code
                        w.scanCode = code.trim()
                        w.findItems(code)
                        w.checkScan()
                    }
                }
                Button {
                    visible: w.product && w.step !== "list"
                    implicitHeight: 56
                    font.pixelSize: 17
                    highlighted: w.queue.length > 0
                    text: qsTr("To Print (%1) ›").arg(w.total)
                    onClicked: w.openList()
                }
            }

            // ---- 2. The tree, one level per screen ----------------------------------------------------
            ColumnLayout {
                visible: w.step === "tree"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                Label {
                    visible: (w.info.children ?? []).length > 0
                    text: w.info.levelTitle ?? ""
                    font.pixelSize: 20
                    font.bold: true
                }
                Button {
                    Layout.fillWidth: true
                    implicitHeight: 64
                    font.pixelSize: 19
                    highlighted: true
                    enabled: (w.info.itemCount ?? 0) > 0
                    text: qsTr("Show Items (%1)").arg(w.info.itemCount ?? 0)
                    onClicked: w.showItems()
                }
                LetterRow {
                    visible: (w.info.children ?? []).length > 8
                    Layout.fillWidth: true
                    names: (w.info.children ?? []).map(c => c.name)
                    onPicked: (letter) => {
                        const i = (w.info.children ?? []).findIndex(c => w.letterOf(c.name) === letter)
                        if (i >= 0)
                            levels.positionViewAtIndex(i, GridView.Beginning)
                    }
                }
                GridView {
                    id: levels
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    readonly property int columns: w.narrow ? 2 : 4
                    cellWidth: width / columns
                    cellHeight: 110
                    model: w.info.children ?? []
                    delegate: Item {
                        required property var modelData
                        width: levels.cellWidth
                        height: levels.cellHeight
                        Button {
                            anchors.fill: parent
                            anchors.margins: 5
                            font.pixelSize: 17
                            text: modelData.name + "\n" + qsTr("%n item(s)", "", modelData.count)
                            onClicked: w.openLevel(modelData.name)
                        }
                    }
                    ScrollBar.vertical: ScrollBar { width: 18 }
                }
            }

            // ---- 3. Items (under a level, or found) ------------------------------------------------------
            ColumnLayout {
                visible: w.step === "items" || w.step === "find"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        opacity: 0.8
                        wrapMode: Text.WordWrap
                        text: {
                            const n = (w.info.matches ?? []).length
                            if (w.step === "find") return n ? qsTr("Touch an item to add a label.") : qsTr("Nothing found.")
                            const all = w.info.itemCount ?? n
                            return all > n ? qsTr("The first %1 of %2 items: choose a level below to narrow it.").arg(n).arg(all)
                                           : qsTr("Touch an item to add a label.")
                        }
                    }
                    Label { visible: (w.info.matches ?? []).length > 1; text: qsTr("Sort"); opacity: 0.8 }
                    Button {
                        visible: (w.info.matches ?? []).length > 1
                        implicitHeight: 52
                        font.pixelSize: 16
                        checkable: true
                        checked: w.itemSort === "name"
                        text: qsTr("A–Z")
                        onClicked: w.itemSort = "name"
                    }
                    Button {
                        visible: (w.info.matches ?? []).length > 1
                        implicitHeight: 52
                        font.pixelSize: 16
                        checkable: true
                        checked: w.itemSort === "upc"
                        text: qsTr("UPC")
                        onClicked: w.itemSort = "upc"
                    }
                    Button {
                        visible: (w.info.matches ?? []).length > 1
                        implicitHeight: 52
                        font.pixelSize: 16
                        text: qsTr("Add All %1").arg((w.info.matches ?? []).length)
                        onClicked: { for (const m of (w.info.matches ?? [])) w.add(m) }
                    }
                }
                LetterRow {
                    visible: (w.info.matches ?? []).length > 8
                    Layout.fillWidth: true
                    names: w.shownItems.map(m => m.name)
                    onPicked: (letter) => {
                        w.itemSort = "name"   // letters go with A to Z
                        const i = w.shownItems.findIndex(m => w.letterOf(m.name) === letter)
                        if (i >= 0)
                            itemList.positionViewAtIndex(i, ListView.Beginning)
                    }
                }
                ListView {
                    id: itemList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.shownItems
                    delegate: Button {
                        required property var modelData
                        width: ListView.view.width - 20
                        implicitHeight: 64
                        font.pixelSize: 17
                        highlighted: w.queued(modelData.name) > 0
                        text: modelData.name + "   " + modelData.price
                              + (w.queued(modelData.name) > 0 ? "   ✓ " + w.queued(modelData.name) : "   +")
                        onClicked: w.add(modelData)
                    }
                    ScrollBar.vertical: ScrollBar { width: 18 }
                }
            }

            // ---- 4. The list: which UPC, how many, order, preview, print ------------------------------
            ColumnLayout {
                visible: w.step === "list"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                RowLayout {
                    visible: w.product
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: qsTr("Print order"); opacity: 0.8 }
                    Repeater {
                        model: [{ key: "added", text: qsTr("As added") }, { key: "name", text: qsTr("Description") },
                                { key: "upc", text: qsTr("UPC") }]
                        delegate: Button {
                            required property var modelData
                            implicitHeight: 48
                            font.pixelSize: 15
                            checkable: true
                            checked: w.sortBy === modelData.key
                            text: modelData.text
                            onClicked: w.sortBy = modelData.key
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        visible: w.queue.length > 0
                        implicitHeight: 48
                        font.pixelSize: 15
                        text: qsTr("Clear List")
                        onClicked: w.queue = []
                    }
                }
                ListView {
                    visible: w.product
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: w.queue
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        width: ListView.view.width - 20
                        spacing: 6
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: modelData.name; font.bold: true; font.pixelSize: 16; elide: Text.ElideRight; Layout.fillWidth: true }
                            // Which of the item's codes this label prints: touch for the next one.
                            Button {
                                implicitHeight: 44
                                font.pixelSize: 14
                                enabled: modelData.upcs.length > 1
                                text: modelData.upcs[modelData.pick].label + ": " + modelData.upcs[modelData.pick].code
                                      + (modelData.upcs.length > 1 ? "  ›" : "")
                                onClicked: w.change(index, { pick: (modelData.pick + 1) % modelData.upcs.length })
                            }
                        }
                        Button { implicitWidth: 52; implicitHeight: 52; font.pixelSize: 20; text: "−"
                            onClicked: if (modelData.qty > 1) w.change(index, { qty: modelData.qty - 1 }) }
                        Label { text: modelData.qty; font.pixelSize: 20; font.bold: true; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignHCenter }
                        Button { implicitWidth: 52; implicitHeight: 52; font.pixelSize: 20; text: "+"
                            onClicked: w.change(index, { qty: Math.min(500, modelData.qty + 1) }) }
                        Button { implicitHeight: 52; font.pixelSize: 15; text: qsTr("Preview")
                            enabled: w.chosen !== "" && (w.info.ready ?? true)
                            onClicked: w.preview(modelData) }
                        Button { implicitWidth: 52; implicitHeight: 52; font.pixelSize: 18; text: "✕"
                            onClicked: w.remove(index) }
                    }
                    ScrollBar.vertical: ScrollBar { width: 18 }
                }
                Label {
                    visible: w.product && w.queue.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: qsTr("Nothing to print yet: go back and touch items to add them.")
                }
                ColumnLayout {
                    visible: !w.product
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10
                    Label { text: qsTr("How many"); opacity: 0.8; font.pixelSize: 18 }
                    RowLayout {
                        spacing: 8
                        Button { implicitWidth: 64; implicitHeight: 64; font.pixelSize: 24; text: "−"; onClicked: if (w.aloneQty > 1) w.aloneQty-- }
                        Label { text: w.aloneQty; font.pixelSize: 28; font.bold: true; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignHCenter }
                        Button { implicitWidth: 64; implicitHeight: 64; font.pixelSize: 24; text: "+"; onClicked: w.aloneQty = Math.min(500, w.aloneQty + 1) }
                    }
                    Item { Layout.fillHeight: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        objectName: "retailLabelPreview"
                        implicitHeight: 68
                        implicitWidth: 180
                        font.pixelSize: 18
                        enabled: w.chosen !== "" && (w.info.ready ?? true) && (!w.product || w.queue.length > 0)
                        text: qsTr("Preview")
                        onClicked: w.preview(w.product ? w.queue[0] : null)
                    }
                    Button {
                        objectName: "retailLabelPrint"
                        Layout.fillWidth: true
                        implicitHeight: 68
                        font.pixelSize: 19
                        highlighted: true
                        enabled: w.chosen !== "" && w.total > 0 && (w.info.ready ?? true)
                        text: qsTr("Print %n Label(s)", "", w.total)
                        onClicked: w.printLabels()
                    }
                }
            }
        }

        // The label as it will print, drawn by the label utility on the main computer.
        Rectangle {
            anchors.fill: parent
            visible: w.previewOpen
            color: "#e6000000"
            z: 10
            MouseArea { anchors.fill: parent }   // nothing underneath takes the touch
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 760)
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: 18
                    font.bold: true
                    text: w.info.previewName ?? ""
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(w.height / w.zoom * 0.55, 420)
                    color: "white"
                    radius: 6
                    Image {
                        anchors.fill: parent
                        anchors.margins: 10
                        fillMode: Image.PreserveAspectFit
                        smooth: false
                        source: w.info.preview ?? ""
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: w.info.previewBusy ?? false
                        visible: running
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: "#b91c1c"
                        font.pixelSize: 16
                        visible: text !== ""
                        text: w.info.previewError ?? ""
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.7
                    visible: w.info.previewBusy ?? false
                    text: qsTr("Drawing the label…")
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    Button { implicitHeight: 60; implicitWidth: 180; font.pixelSize: 17; text: qsTr("Close")
                        onClicked: w.closePreview() }
                    Button { implicitHeight: 60; implicitWidth: 260; font.pixelSize: 17; highlighted: true
                        enabled: w.chosen !== "" && w.total > 0 && (w.info.ready ?? true)
                        text: qsTr("Print %n Label(s)", "", w.total)
                        onClicked: w.printLabels() }
                }
            }
        }
    }
}
