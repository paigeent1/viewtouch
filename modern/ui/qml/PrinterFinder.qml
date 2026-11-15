import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Find My Printers (Manager -> Printers): the printers on the store's network (no addresses to type), each
// that answers like a receipt printer with a page that prints its address (to tell which is which) and a
// button for what it's for: receipts, the kitchen or the bar. (Fork) The store's label printers and
// anything that doesn't answer get no buttons; a printer that isn't on the network (USB) is never changed;
// taking a printer's place from another address asks first.
ColumnLayout {
    id: f
    property PosService pos
    property bool small: false
    readonly property var search: pos ? pos.printerSearch : ({})
    readonly property bool busy: search.busy === true
    readonly property var found: search.found ?? []
    readonly property var places: search.slots ?? ({})
    spacing: 8

    // Asked before a found printer takes the place of one at another address.
    property string askKind: ""
    property string askHost: ""
    function use(kind, host) {
        const now = f.places[kind]
        if (now && now.network && now.where !== host) {
            f.askKind = kind
            f.askHost = host
            replaceBox.open()
            return
        }
        f.pos.useFoundPrinter(kind, host, false)
    }

    Button {
        objectName: "findPrinters"
        Layout.fillWidth: f.small
        Layout.preferredWidth: f.small ? -1 : 280
        implicitHeight: 56
        highlighted: !f.search.done
        enabled: !f.busy
        font.pixelSize: 17
        text: f.busy ? qsTr("Looking…") : f.search.done ? qsTr("Look Again") : qsTr("Find My Printers")
        onClicked: f.pos.findPrinters()
    }
    Label {
        textFormat: Text.PlainText
        objectName: "findPrintersNone"
        visible: f.search.done === true && f.found.length === 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: "#c98a00"
        font.pixelSize: 15
        text: qsTr("None found. Is the printer on, and plugged into the same network (router or switch) as this computer? A USB printer isn't found this way: Manager → Printers.")
    }
    Label {
        textFormat: Text.PlainText
        visible: f.found.length > 0
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        opacity: 0.8
        font.pixelSize: 15
        text: qsTr("Touch Print Its Address on each: the one that prints is that one. Then say what it's for.")
    }
    Repeater {
        model: f.found
        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool usable: modelData.answered === true && modelData.label !== true
            objectName: "foundPrinter-" + modelData.host
            Layout.fillWidth: true
            implicitHeight: rowGrid.implicitHeight + 16
            radius: 6
            color: Qt.rgba(0.5, 0.5, 0.5, 0.10)
            border.color: Qt.rgba(0.5, 0.5, 0.5, 0.35)
            GridLayout {
                id: rowGrid
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                columns: f.small ? 2 : 5
                columnSpacing: 8
                rowSpacing: 6
                ColumnLayout {
                    Layout.columnSpan: f.small || !row.usable ? (f.small ? 2 : 5) : 1
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        textFormat: Text.PlainText
                        font.pixelSize: 18
                        font.bold: true
                        text: row.modelData.host
                    }
                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: row.modelData.printer ? "#2e9e5b" : palette.windowText
                        opacity: row.modelData.printer ? 1 : 0.75
                        font.pixelSize: 14
                        text: row.modelData.printer ? qsTr("✓ Your %1 printer").arg(row.modelData.printer)
                            : row.modelData.answered ? qsTr("A receipt or kitchen printer")
                            : qsTr("Takes print jobs, but didn't answer like a receipt printer (a label printer?): add it by hand with + Add.")
                    }
                }
                Button {
                    visible: row.usable
                    objectName: "foundPrinterTest-" + row.modelData.host
                    Layout.columnSpan: f.small ? 2 : 1
                    Layout.fillWidth: f.small
                    implicitHeight: 52
                    font.pixelSize: 15
                    text: qsTr("Print Its Address")
                    onClicked: f.pos.printWhoAmI(row.modelData.host)
                }
                Repeater {
                    model: row.usable ? [{ kind: "receipt", text: qsTr("Receipts") }, { kind: "kitchen", text: qsTr("Kitchen") },
                                         { kind: "bar", text: qsTr("Bar") }] : []
                    delegate: Button {
                        required property var modelData
                        objectName: "foundPrinterUse-" + modelData.kind + "-" + row.modelData.host
                        Layout.fillWidth: f.small
                        implicitHeight: 52
                        font.pixelSize: 15
                        text: modelData.text
                        onClicked: f.use(modelData.kind, row.modelData.host)
                    }
                }
            }
        }
    }

    Popup {
        id: replaceBox
        objectName: "foundPrinterReplaceBox"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, (parent ? parent.width : 560) - 32)
        padding: 20
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 12
            Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 18
                text: {
                    const now = f.places[f.askKind]
                    return now ? qsTr("Your %1 printer is at %2 now. Print on %3 instead?").arg(now.name).arg(now.where).arg(f.askHost) : ""
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Button {
                    implicitHeight: 56
                    Layout.fillWidth: true
                    font.pixelSize: 17
                    text: qsTr("Cancel")
                    onClicked: replaceBox.close()
                }
                Button {
                    objectName: "foundPrinterReplaceOk"
                    implicitHeight: 56
                    Layout.fillWidth: true
                    font.pixelSize: 17
                    text: qsTr("Use %1").arg(f.askHost)
                    onClicked: {
                        f.pos.useFoundPrinter(f.askKind, f.askHost, true)
                        replaceBox.close()
                    }
                }
            }
        }
    }
}
