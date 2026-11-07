import QtQuick
import QtQuick.Layouts

// Which stores (like ILSA Corp): a tick box per store (BHDAN, BHNM, later HCA), All / None, and with
// more than one ticked, By store (a section per store, its own totals) or Corp roll-up (one total).
// It shows what it is given (stores, selected, byStore: usually the server's answer) and asks through
// chosen(arg): "BHDAN,BHNM" ("-" none), with "|rollup" for one combined total. For the ILSA History
// screens now, the corporate setup later.
RowLayout {
    id: p
    property var stores: []
    property var selected: []
    property bool byStore: true
    property bool showMode: true   // By store / Corp roll-up (a list of receipts has no totals)
    property real unit: 14
    property real keyH: 40
    property color ink: "white"
    property string face: "DejaVu Sans"
    signal chosen(string arg)

    function arg(list, by) { return (list.length ? list.join(",") : "-") + (by ? "" : "|rollup") }
    function toggle(s) {
        const l = p.selected.slice()
        const i = l.indexOf(s)
        if (i >= 0) l.splice(i, 1)
        else l.push(s)
        p.chosen(p.arg(p.stores.filter(x => l.indexOf(x) >= 0), p.byStore))
    }

    visible: stores.length > 0
    spacing: unit * 0.3
    Text { text: qsTr("Stores"); color: p.ink; font.family: p.face; font.pixelSize: p.unit * 0.8 }
    Repeater {
        model: p.stores
        delegate: WidgetKey {
            required property string modelData
            readonly property bool on: p.selected.indexOf(modelData) >= 0
            objectName: "storePick-" + modelData
            Layout.preferredWidth: p.unit * 6.5
            Layout.preferredHeight: p.keyH
            text: (on ? "☑ " : "☐ ") + modelData
            fontScale: 0.34
            baseColor: on ? "#1f8a4c" : "#2b3340"
            onClicked: p.toggle(modelData)
        }
    }
    WidgetKey {
        objectName: "storePickAll"
        Layout.preferredWidth: p.unit * 4.5
        Layout.preferredHeight: p.keyH
        text: qsTr("All")
        fontScale: 0.34
        onClicked: p.chosen(p.arg(p.stores, p.byStore))
    }
    WidgetKey {
        objectName: "storePickNone"
        Layout.preferredWidth: p.unit * 4.5
        Layout.preferredHeight: p.keyH
        text: qsTr("None")
        fontScale: 0.34
        onClicked: p.chosen(p.arg([], p.byStore))
    }
    WidgetKey {
        objectName: "storePickByStore"
        visible: p.showMode && p.selected.length > 1
        Layout.preferredWidth: p.unit * 6
        Layout.preferredHeight: p.keyH
        text: qsTr("By store")
        fontScale: 0.32
        baseColor: p.byStore ? "#2f6fd6" : "#343c49"
        onClicked: p.chosen(p.arg(p.selected, true))
    }
    WidgetKey {
        objectName: "storePickRollup"
        visible: p.showMode && p.selected.length > 1
        Layout.preferredWidth: p.unit * 7
        Layout.preferredHeight: p.keyH
        text: qsTr("Corp roll-up")
        fontScale: 0.32
        baseColor: p.byStore ? "#343c49" : "#2f6fd6"
        onClicked: p.chosen(p.arg(p.selected, false))
    }
    Item { Layout.fillWidth: true }
}
