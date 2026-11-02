import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Labels from any screen (a tablet in the stock room or kitchen): choose a label, find items by name
// or scan their barcode, pick which of each item's UPCs to print and how many. The main computer's
// label utility prints them, exactly as it does on its own screen, on the label's printer.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.retailLabels : ({})
    readonly property var templates: info.templates ?? []
    property string chosen: ""
    readonly property var chosenOne: templates.find(t => t.name === chosen)
    readonly property bool product: chosenOne ? chosenOne.product : true
    // [{name, upcs: [{label, code}], pick (index into upcs), qty}]
    property var queue: []
    property int aloneQty: 1
    property bool browsing: true   // the tree (group > category > brand > items), or Find
    readonly property var path: info.path ?? []
    readonly property int total: product ? queue.reduce((n, q) => n + q.qty, 0) : aloneQty

    function add(m) {
        const i = queue.findIndex(q => q.name === m.name)
        const next = queue.slice()
        if (i >= 0) next[i] = Object.assign({}, next[i], { qty: next[i].qty + 1 })
        else next.push({ name: m.name, upcs: m.upcs, pick: 0, qty: 1 })
        queue = next
    }
    function change(i, fields) {
        const next = queue.slice()
        next[i] = Object.assign({}, next[i], fields)
        queue = next
    }
    function remove(i) {
        const next = queue.slice()
        next.splice(i, 1)
        queue = next
    }
    function print() {
        const items = product ? queue.map(q => ({ upc: q.upcs[q.pick].code, qty: q.qty })) : []
        pos.printRetailLabels({ template: chosen, items: items, qty: aloneQty })
        queue = []
        aloneQty = 1
    }
    Component.onCompleted: {
        if (templates.length > 0 && chosen === "") chosen = templates[0].name
        if (pos) pos.browseRetailLabels([])
    }
    onTemplatesChanged: if (chosen === "" && templates.length > 0) chosen = templates[0].name

    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 460 : 1000) : 1.2
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        GridLayout {
            anchors.fill: parent
            anchors.margins: 10
            columns: w.narrow ? 1 : 2
            columnSpacing: 16
            rowSpacing: 8

            // Left: the label, then finding items.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 8

                Label {
                    visible: !(w.info.ready ?? true)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#fca5a5"
                    text: qsTr("Labels print from the main computer, and its label utility isn't installed there.")
                }
                Label { text: qsTr("Label"); opacity: 0.8 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: w.templates
                        delegate: Button {
                            required property var modelData
                            implicitHeight: 52
                            font.pixelSize: 15
                            checkable: true
                            checked: w.chosen === modelData.name
                            text: modelData.name + "\n" + modelData.size
                            onClicked: w.chosen = modelData.name
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

                RowLayout {
                    visible: w.product
                    Layout.fillWidth: true
                    spacing: 6
                    Button { implicitHeight: 44; font.pixelSize: 15; text: qsTr("Browse"); checkable: true; checked: w.browsing
                        onClicked: { w.browsing = true; find.text = ""; w.pos.browseRetailLabels(w.path) } }
                    Button { implicitHeight: 44; font.pixelSize: 15; text: qsTr("Find"); checkable: true; checked: !w.browsing
                        onClicked: { w.browsing = false; w.pos.findRetailLabels(find.text) } }
                }
                // Where we are in the tree; touch a step to go back up to it.
                Flow {
                    visible: w.product && w.browsing
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: [qsTr("All")].concat(w.path)
                        delegate: Button {
                            required property var modelData
                            required property int index
                            implicitHeight: 40
                            font.pixelSize: 14
                            flat: index === w.path.length
                            text: modelData + (index < w.path.length ? "  ›" : "")
                            onClicked: w.pos.browseRetailLabels(w.path.slice(0, index))
                        }
                    }
                }
                ListView {
                    visible: w.product && w.browsing && w.path.length < 3
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.info.children ?? []
                    delegate: Button {
                        required property var modelData
                        width: ListView.view.width
                        implicitHeight: 50
                        font.pixelSize: 16
                        text: modelData.name + "   (" + modelData.count + ")   ›"
                        onClicked: w.pos.browseRetailLabels(w.path.concat([modelData.name]))
                    }
                }
                RowLayout {
                    visible: w.product && !w.browsing
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: find
                        objectName: "retailLabelSearch"
                        Layout.fillWidth: true
                        implicitHeight: 48
                        font.pixelSize: 17
                        placeholderText: qsTr("Find an item: name, or scan its barcode")
                        onTextEdited: { w.browsing = false; w.pos.findRetailLabels(text) }
                        // A scanner types the code and Enter: the one item it matches goes on the list.
                        onAccepted: {
                            w.pos.findRetailLabels(text)
                            const m = w.info.matches ?? []
                            if (m.length === 1) { w.add(m[0]); text = ""; w.pos.findRetailLabels("") }
                        }
                    }
                }
                Button {
                    visible: w.product && w.browsing && w.path.length === 3 && (w.info.matches ?? []).length > 1
                    Layout.fillWidth: true
                    implicitHeight: 44
                    font.pixelSize: 15
                    text: qsTr("Add All %1").arg((w.info.matches ?? []).length)
                    onClicked: for (const m of (w.info.matches ?? [])) w.add(m)
                }
                ListView {
                    visible: w.product && (!w.browsing || w.path.length === 3)
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.info.matches ?? []
                    delegate: Button {
                        required property var modelData
                        width: ListView.view.width
                        implicitHeight: 50
                        font.pixelSize: 15
                        text: modelData.name + "   " + modelData.price + "   +"
                        onClicked: w.add(modelData)
                    }
                }
                Item { visible: !w.product; Layout.fillHeight: true }
            }

            // Right: what will print.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                spacing: 8

                Label {
                    text: w.product ? qsTr("To print") : qsTr("How many")
                    opacity: 0.8
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
                        width: ListView.view.width
                        spacing: 6
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: modelData.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            // Which of the item's codes this label prints: touch for the next one.
                            Button {
                                implicitHeight: 40
                                font.pixelSize: 13
                                enabled: modelData.upcs.length > 1
                                text: modelData.upcs[modelData.pick].label + ": " + modelData.upcs[modelData.pick].code
                                      + (modelData.upcs.length > 1 ? "  ›" : "")
                                onClicked: w.change(index, { pick: (modelData.pick + 1) % modelData.upcs.length })
                            }
                        }
                        Button { implicitWidth: 48; implicitHeight: 48; font.pixelSize: 20; text: "−"
                            onClicked: if (modelData.qty > 1) w.change(index, { qty: modelData.qty - 1 }) }
                        Label { text: modelData.qty; font.pixelSize: 20; font.bold: true; Layout.preferredWidth: 36; horizontalAlignment: Text.AlignHCenter }
                        Button { implicitWidth: 48; implicitHeight: 48; font.pixelSize: 20; text: "+"
                            onClicked: w.change(index, { qty: Math.min(500, modelData.qty + 1) }) }
                        Button { implicitWidth: 48; implicitHeight: 48; font.pixelSize: 18; text: "✕"
                            onClicked: w.remove(index) }
                    }
                }
                Label {
                    visible: w.product && w.queue.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: qsTr("Find or scan items on the left; each touch adds one label.")
                }
                RowLayout {
                    visible: !w.product
                    spacing: 8
                    Button { implicitWidth: 56; implicitHeight: 56; font.pixelSize: 22; text: "−"; onClicked: if (w.aloneQty > 1) w.aloneQty-- }
                    Label { text: w.aloneQty; font.pixelSize: 26; font.bold: true; Layout.preferredWidth: 60; horizontalAlignment: Text.AlignHCenter }
                    Button { implicitWidth: 56; implicitHeight: 56; font.pixelSize: 22; text: "+"; onClicked: w.aloneQty = Math.min(500, w.aloneQty + 1) }
                }
                Item { visible: !w.product; Layout.fillHeight: true }

                Button {
                    objectName: "retailLabelPrint"
                    Layout.fillWidth: true
                    implicitHeight: 64
                    font.pixelSize: 18
                    highlighted: true
                    enabled: w.chosen !== "" && w.total > 0 && (w.info.ready ?? true)
                    text: qsTr("Print %n Label(s)", "", w.total)
                    onClicked: w.print()
                }
            }
        }
    }
}
