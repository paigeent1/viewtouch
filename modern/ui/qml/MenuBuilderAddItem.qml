import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Menu Builder, Retail: + Add Item asks what it is first (owner 2026-11-24): a box or carton (the parent),
// a single that comes in a box (the child: its box is asked for next), or a single item with no box.
// Hospitality and Event modes don't ask. The card it opens has the cursor in its UPC field.
Popup {
    id: addItem
    property Item menu   // the WidgetMenuBuilder (not by its type: that would be a cycle)
    objectName: "builderAddKindDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 32 : 600, 600)
    height: stage === "box" ? Math.min(parent ? parent.height - 32 : 640, 640) : implicitHeight
    modal: true
    focus: true   // (the box's UPC field takes a scan)
    padding: 16
    property string stage: "kind"   // kind | box (a single's box)
    property string find: ""
    property bool pickWhenFound: false   // a scan or Enter: the box with that UPC is taken as soon as it's found
    property string boxProblem: ""

    readonly property var pos: menu ? menu.pos : null
    function ask() {
        stage = "kind"
        open()
    }
    function askBox() {
        stage = "box"
        find = ""
        boxProblem = ""
        pickWhenFound = false
        if (!opened) open()
        Qt.callLater(() => { if (boxFind.visible) boxFind.forceActiveFocus() })
    }
    // The main computer's item search (every item, not only the category open): by name, or a UPC.
    Item {
        visible: false
        readonly property var p: addItem.pos
        readonly property bool wanted: addItem.opened && addItem.stage === "box"
        property var asked: null
        function ask() {
            if (asked && (asked !== p || !wanted)) { asked.want("itemSearch", false); asked = null }
            if (p && wanted && asked !== p) { p.want("itemSearch", true); asked = p }
        }
        onPChanged: ask()
        onWantedChanged: ask()
        Component.onDestruction: if (asked) asked.want("itemSearch", false)
    }
    Timer {
        id: searchSoon
        interval: 250
        onTriggered: if (addItem.pos) addItem.pos.findItems(addItem.find.trim(), 20)
    }
    readonly property var found: {
        const s = pos && opened && stage === "box" ? pos.itemSearch : null
        if (!s || find.trim() === "" || s.query !== find.trim()) return []
        return (s.items ?? []).filter(i => i.id !== (menu.draft.id ?? ""))
    }
    function plain(code) { return String(code ?? "").replace(/^0+/, "") }
    onFoundChanged: {
        if (!pickWhenFound || find.trim() === "") return
        const exact = found.find(i => i.upc && plain(i.upc) === plain(find.trim()))
        if (exact) { pickWhenFound = false; Qt.callLater(() => addItem.use(exact)) }
    }
    function look(text, now) {
        find = text
        boxProblem = ""
        if (now) {
            pickWhenFound = /^[0-9]{4,}$/.test(text.trim())
            searchSoon.stop()
            if (pos) pos.findItems(text.trim(), 20)
            // Already on this screen: taken straight away.
            const here = pickWhenFound ? menu.findExistingItemByBarcode(text.trim()) : null
            if (here) { pickWhenFound = false; use(here) }
        } else {
            searchSoon.restart()
        }
    }
    // A scan while this is open (the Menu Builder passes it on).
    function scanned(code) {
        boxFind.text = code
        look(code, true)
    }
    function use(box) {
        const upc = String(box.upc ?? (String(box.barcodes ?? "").split(",")[0] ?? "")).trim()
        if (upc === "") {
            boxProblem = qsTr("%1 has no UPC yet. Open it, add its UPC, then try again.").arg(box.name)
            return
        }
        close()
        const b = menu
        Qt.callLater(() => b.startChild({ id: box.id, name: box.name, upc: upc, price: box.price }))
    }

    contentItem: ColumnLayout {
        spacing: 10
        // --- what is it? ---
        Label {
            visible: addItem.stage === "kind"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("What are you adding?")
            font.pixelSize: 20
            font.bold: true
        }
        Repeater {
            model: addItem.stage === "kind" ? [
                { kind: "parent", title: qsTr("Box or carton (parent)"),
                  example: qsTr("e.g. a box of 20 cigars, a carton of 10 packs") },
                { kind: "child", title: qsTr("Single in a box (child)"),
                  example: qsTr("e.g. one cigar from that box, one pack from the carton") },
                { kind: "single", title: qsTr("Single item (no box)"),
                  example: qsTr("e.g. a lighter, a cutter, a T-shirt") }] : []
            delegate: MenuTouchButton {
                required property var modelData
                objectName: "builderAddKind-" + modelData.kind
                Layout.fillWidth: true
                implicitHeight: 72
                contentItem: ColumnLayout {
                    spacing: 2
                    Label { text: modelData.title; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
                    Label { text: modelData.example; font.pixelSize: 14; opacity: 0.75; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                onClicked: {
                    const kind = modelData.kind, b = addItem.menu
                    if (kind === "child") { addItem.askBox(); return }
                    addItem.close()
                    Qt.callLater(() => b.startNew(kind, ""))
                }
            }
        }

        // --- a single: which box is it in? ---
        Label {
            visible: addItem.stage === "box"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Which box or carton does it come in? Scan or type the box's UPC, or type part of its name.")
            font.pixelSize: 17
            font.bold: true
        }
        TextField {
            id: boxFind
            objectName: "builderBoxFind"
            visible: addItem.stage === "box"
            Layout.fillWidth: true
            implicitHeight: 52
            font.pixelSize: 18
            placeholderText: qsTr("Box UPC or name")
            inputMethodHints: Qt.ImhNoPredictiveText
            text: addItem.find
            // A scan replaces what's there.
            onActiveFocusChanged: if (activeFocus) selectAll()
            onTextEdited: addItem.look(text, false)
            onAccepted: addItem.look(text, true)
        }
        Label {
            objectName: "builderBoxProblem"
            visible: addItem.stage === "box" && text !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: addItem.boxProblem !== "" ? addItem.boxProblem
                : addItem.find.trim() !== "" && addItem.found.length === 0 && addItem.pos && addItem.pos.itemSearch
                  && addItem.pos.itemSearch.query === addItem.find.trim()
                  ? qsTr("No item found. If the box isn't an item yet, add the box first.") : ""
            color: "#f5b940"
            font.pixelSize: 15
        }
        ListView {
            visible: addItem.stage === "box"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: addItem.stage === "box" ? addItem.found : []
            delegate: MenuTouchButton {
                required property var modelData
                objectName: "builderBox-" + modelData.id
                width: ListView.view.width
                text: modelData.name + "  ·  " + modelData.price + (modelData.upc ? "  ·  " + modelData.upc : "")
                onClicked: addItem.use(modelData)
            }
        }
        RowLayout {
            visible: addItem.stage === "box"
            Layout.fillWidth: true
            spacing: 8
            MenuTouchButton {
                objectName: "builderAddBoxFirst"
                Layout.fillWidth: true
                text: qsTr("Add the box first")
                onClicked: {
                    const typed = addItem.find.trim(), b = addItem.menu
                    addItem.close()
                    Qt.callLater(() => b.addBoxFirst(typed))
                }
            }
            MenuTouchButton {
                text: qsTr("‹ Back")
                onClicked: addItem.stage = "kind"
            }
        }
        MenuTouchButton {
            Layout.fillWidth: true
            text: qsTr("Cancel")
            onClicked: addItem.close()
        }
    }
}
