import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Item Look Up (the Retail screen's ALPHA LOOK UP and PRICE CHECK), laid out like the label utility:
// type part of a name or a UPC, or go down the tree (department group, department, brand, product
// group, sub group), and the items show with their prices and UPCs. Touching one rings it in on the
// Retail screen (with its X TIMES), or in Price Check just shows it. The main computer does the
// finding (the same as the Labels screen), so a 30,000-item price book is as quick as a small one.
// Big ▲ ▼ buttons page through the list.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var controller: zone ? zone.controller : null
    readonly property var info: pos ? pos.retailLabels : ({})
    readonly property var path: info.path ?? []
    readonly property string query: info.query ?? ""
    // "ring" (ALPHA LOOK UP) or "price" (PRICE CHECK): what the Retail screen asked for.
    property string mode: "ring"
    property string returnTo: "retail-entry"
    property var picked: null          // Price Check: the item touched

    // What this screen shows comes to a tablet only while it's shown (PosServer: on request).
    Item {
        visible: false
        readonly property var p: w.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("retailLabels", false)
            asked = p
            if (p) p.want("retailLabels", true)
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) asked.want("retailLabels", false)
    }

    // The list: the matches (a search, or a level's items), else the next level's names.
    readonly property bool showingItems: query !== "" || (info.showingItems ?? false)
    readonly property var rows: showingItems
        ? (info.matches ?? []).slice().sort((a, b) => String(a.name).localeCompare(String(b.name)))
        : (info.children ?? [])

    function browseTo(p) { picked = null; pos.browseRetailLabels(p) }
    function showItems() { picked = null; pos.showRetailLabelItems(path) }
    function find(text) {
        picked = null
        const t = String(text ?? "").trim()
        if (t === "") browseTo(path)
        else pos.findRetailLabels(t)
    }
    function leave() {
        if (controller) controller.jumpToPage(returnTo)
    }
    function touch(row) {
        if (!showingItems) {                     // a level: one down
            browseTo(path.concat([row.name]))
            return
        }
        if (mode === "price") {                  // Price Check: shown, nothing rung
            picked = row
            return
        }
        const code = row.upcs && row.upcs.length ? String(row.upcs[0].code) : ""
        if (code === "" || !controller) return
        controller.keep("lookupRing", code)      // the Retail screen rings it (with its X TIMES)
        leave()
    }
    function back() {
        if (searchField.text !== "") { searchField.text = ""; find(""); return }
        if ((info.showItemsAsked ?? false) && (info.children ?? []).length > 0) { browseTo(path); return }
        if (path.length > 0) { browseTo(path.slice(0, path.length - 1)); return }
        leave()
    }
    function page(by) {
        const step = Math.max(1, Math.floor(list.height / rowHeight) - 1)
        const top = list.indexAt(10, list.contentY + 10)
        list.positionViewAtIndex(Math.max(0, Math.min(rows.length - 1, (top < 0 ? 0 : top) + by * step)), ListView.Beginning)
    }

    Component.onCompleted: {
        if (controller) {
            const m = controller.take("lookupMode")
            if (m) mode = String(m)
            const r = controller.take("lookupReturn")
            if (r) returnTo = String(r)
        }
        if (pos) browseTo([])
    }
    // A scan here (the main computer's scanner): found like a typed UPC.
    Timer {
        interval: 80
        running: w.visible
        repeat: true
        onTriggered: {
            if (!w.controller) return
            const code = w.controller.take("lookupScan")
            if (!code) return
            searchField.text = String(code)
            w.find(code)
        }
    }
    // Typing: found as it's typed (a moment after the last key).
    Timer {
        id: typing
        interval: 300
        onTriggered: w.find(searchField.text)
    }

    readonly property real zoom: zone ? zone.formZoom(1000) : 1.2
    readonly property real rowHeight: 76
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            // ---- Back, the search, the mode -------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Button {
                    objectName: "lookupBack"
                    implicitHeight: 64; implicitWidth: 130
                    font.pixelSize: 22; font.bold: true
                    text: qsTr("‹ Back")
                    onClicked: w.back()
                }
                Label {
                    text: w.mode === "price" ? qsTr("PRICE CHECK") : qsTr("ITEM LOOK UP")
                    font.pixelSize: 24; font.bold: true
                    color: w.mode === "price" ? "#fde047" : "#93c5fd"
                }
                TextField {
                    id: searchField
                    objectName: "lookupSearch"
                    Layout.fillWidth: true
                    implicitHeight: 64
                    font.pixelSize: 24
                    placeholderText: qsTr("Type part of a name, or scan / type a UPC")
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onTextEdited: typing.restart()
                    onAccepted: { typing.stop(); w.find(text) }
                }
                Button {
                    implicitHeight: 64; implicitWidth: 110
                    font.pixelSize: 20
                    text: qsTr("Clear")
                    enabled: searchField.text !== ""
                    onClicked: { searchField.text = ""; w.find("") }
                }
                Button {
                    implicitHeight: 64; implicitWidth: 140
                    font.pixelSize: 20; font.bold: true
                    text: qsTr("✕ Close")
                    palette.button: "#b42318"; palette.buttonText: "white"
                    onClicked: w.leave()
                }
            }

            // ---- Where we are in the tree (touch one to go back up), Show Items -------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: w.query === ""
                Button {
                    implicitHeight: 52
                    font.pixelSize: 18
                    text: qsTr("All")
                    flat: w.path.length > 0
                    onClicked: w.browseTo([])
                }
                Repeater {
                    model: w.path
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        spacing: 6
                        Label { text: "›"; font.pixelSize: 22; opacity: 0.6 }
                        Button {
                            implicitHeight: 52
                            font.pixelSize: 18
                            text: modelData
                            flat: index < w.path.length - 1
                            onClicked: w.browseTo(w.path.slice(0, index + 1))
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "lookupShowItems"
                    visible: !w.showingItems
                    implicitHeight: 52
                    font.pixelSize: 18
                    text: qsTr("Show all %n item(s) here", "", w.info.itemCount ?? 0)
                    onClicked: w.showItems()
                }
            }

            // ---- The list, and the big ▲ ▼ ------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                ListView {
                    id: list
                    objectName: "lookupList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    boundsBehavior: Flickable.StopAtBounds
                    model: w.rows
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: w.rowHeight
                        radius: 8
                        readonly property bool isItem: w.showingItems
                        readonly property bool chosen: w.picked !== null && w.picked.id === modelData.id && isItem
                        color: chosen ? "#1d4ed8" : (isItem ? "#1f2937" : "#334155")
                        border.color: chosen ? "#facc15" : "#475569"
                        border.width: chosen ? 3 : 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 14
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.name
                                    font.pixelSize: 24; font.bold: true
                                    color: "white"
                                    elide: Text.ElideRight
                                }
                                Label {
                                    visible: row.isItem
                                    text: row.modelData.upcs && row.modelData.upcs.length ? "UPC " + row.modelData.upcs[0].code : ""
                                    font.pixelSize: 16
                                    color: "#cbd5e1"
                                }
                            }
                            Label {
                                visible: row.isItem
                                text: row.modelData.price ?? ""
                                font.pixelSize: 30; font.bold: true
                                color: "#fde047"
                            }
                            Label {
                                visible: !row.isItem
                                text: qsTr("%n item(s)  ›", "", row.modelData.count ?? 0)
                                font.pixelSize: 18
                                color: "#cbd5e1"
                            }
                        }
                        TapHandler { onTapped: w.touch(row.modelData) }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: list.count === 0
                        font.pixelSize: 22
                        opacity: 0.7
                        text: w.query !== "" ? qsTr("Nothing matches \"%1\".").arg(w.query) : qsTr("Nothing here.")
                    }
                }

                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 150
                    spacing: 10
                    Button {
                        objectName: "lookupPageUp"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        font.pixelSize: 64
                        text: "▲"
                        onClicked: w.page(-1)
                    }
                    Button {
                        objectName: "lookupPageDown"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        font.pixelSize: 64
                        text: "▼"
                        onClicked: w.page(1)
                    }
                }
            }

            // ---- What a touch does; Price Check's answer; how many ---------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 70
                radius: 8
                color: w.picked ? "#713f12" : "#111827"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: w.picked ? 26 : 18
                        font.bold: w.picked !== null
                        color: "white"
                        text: w.picked ? w.picked.name
                              : w.showingItems
                                ? (w.mode === "price" ? qsTr("Touch an item to see its price.")
                                                      : qsTr("Touch an item to ring it in."))
                                  + (w.query !== "" && (w.info.matches ?? []).length >= 40
                                     ? qsTr("  The first 40: type more to narrow it down.") : "")
                                : (w.info.levelTitle ?? "")
                    }
                    Label {
                        visible: w.picked !== null
                        text: w.picked ? w.picked.price : ""
                        font.pixelSize: 40; font.bold: true
                        color: "#fde047"
                    }
                }
            }
        }
    }
}
