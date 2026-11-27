import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Manager > Inventory > Transfers (owner 2026-11-26): retail items sent to another store and received from one,
// matched by UPC (PosService::transferOpen..., app/pos_transfers.cpp; the state is pos.transfers). Transfer out:
// scan with the counter scanner (or type a UPC, or part of a name and Find), a quantity a line, To, a note, Send:
// off on hand here, a slip on the receipt printer, the file to the other store (it waits here while the link is
// down). Receive: the transfers waiting from the other stores; open one, tick lines off (or scan them), correct
// a count with why, add an item not on file from the transfer or leave it out, Receive. List: both ways with
// where each is (in transit, received, with differences), Void (not received yet) and Reprint.
Item {
    id: w
    objectName: "transfers"
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var info: pos ? pos.transfers : ({})
    readonly property string tab: info.tab ?? "out"
    readonly property var receiving: info.receiving ?? ({})
    readonly property bool isReceiving: receiving.id !== undefined
    readonly property real unit: Math.max(14, Math.min(width / 70, height / 34))
    readonly property real touch: Math.max(46, unit * 2.6)

    // The state comes to a tablet only while a screen shows it (PosServer: on request).
    Item {
        visible: false
        readonly property var p: w.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("transfers", false)
            asked = p
            if (p) { p.want("transfers", true); p.transferOpen("out") }
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) { asked.want("transfers", false); asked.transferClose() }
    }

    component Big: Button {
        implicitHeight: w.touch
        font.pixelSize: w.unit
    }
    component Words: Label {
        font.pixelSize: w.unit * 0.9
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            spacing: 8
            Repeater {
                model: [["out", qsTr("Transfer out")], ["in", qsTr("Receive (%1)").arg(w.info.waitingIn ?? 0)], ["list", qsTr("Transfers")]]
                delegate: Big {
                    required property var modelData
                    objectName: "xferTab-" + modelData[0]
                    text: modelData[1]
                    checkable: true
                    checked: w.tab === modelData[0]
                    highlighted: checked
                    onClicked: w.pos.transferOpen(modelData[0])
                }
            }
            Item { Layout.fillWidth: true }
            Words {
                objectName: "xferLink"
                visible: text !== ""
                text: w.info.link ?? ""
                color: "#b35c00"
                Layout.maximumWidth: w.width * 0.45
            }
        }
        Words {
            objectName: "xferMessage"
            visible: text !== ""
            text: w.info.message ?? ""
            Layout.fillWidth: true
        }

        // --- Transfer out ---
        ColumnLayout {
            visible: w.tab === "out"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Words {
                visible: (w.info.storeCode ?? "") === "" || (w.info.stores ?? []).length === 0
                text: qsTr("Set this store's short name and the other stores first: Store Settings > Other stores.")
                color: "#b00020"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: findField
                    objectName: "xferFind"
                    Layout.fillWidth: true
                    implicitHeight: w.touch
                    font.pixelSize: w.unit
                    placeholderText: qsTr("Scan, or type a UPC or part of a name")
                    onAccepted: { w.pos.transferFind(text); text = "" }
                }
                Big { objectName: "xferFindGo"; text: qsTr("Find"); onClicked: { w.pos.transferFind(findField.text); findField.text = "" } }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: (w.info.choices ?? []).length > 0
                Repeater {
                    model: w.info.choices ?? []
                    delegate: Big {
                        required property var modelData
                        objectName: "xferChoice-" + modelData.id
                        text: modelData.name + "  " + modelData.price
                        onClicked: w.pos.transferAddItem(modelData.id, 1)
                    }
                }
            }
            ListView {
                id: lineList
                objectName: "xferLines"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: w.info.lines ?? []
                ScrollBar.vertical: TouchScrollBar { needed: lineList.contentHeight > lineList.height + 1 }
                delegate: RowLayout {
                    required property var modelData
                    width: lineList.width - 12
                    spacing: 8
                    objectName: "xferLine-" + modelData.index
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label { text: modelData.name; font.pixelSize: w.unit; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            text: modelData.upc + (modelData.pack ? "  ·  " + modelData.pack : "") + "  ·  " + modelData.cost
                                  + (modelData.onHand !== "" ? "  ·  " + qsTr("on hand %1").arg(modelData.onHand) : "")
                            font.pixelSize: w.unit * 0.75
                            opacity: 0.75
                        }
                    }
                    // A carton and its packs (a box and its cigars) share a line: cartons, then loose packs.
                    Big { text: "−"; implicitWidth: w.touch; onClicked: w.pos.transferSetQty(modelData.index, Math.max(0, modelData.qty - 1), -1) }
                    TextField {
                        objectName: "xferQty-" + modelData.index
                        text: String(modelData.qty)
                        implicitWidth: w.unit * 5
                        implicitHeight: w.touch
                        font.pixelSize: w.unit
                        horizontalAlignment: Text.AlignHCenter
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: if (Number(text) !== modelData.qty) w.pos.transferSetQty(modelData.index, Number(text) || 0, -1)
                    }
                    Big { text: "+"; implicitWidth: w.touch; onClicked: w.pos.transferSetQty(modelData.index, modelData.qty + 1, -1) }
                    Label { visible: modelData.family === true; text: modelData.parentUnit ?? ""; font.pixelSize: w.unit }
                    Big {
                        visible: modelData.family === true
                        text: "−"; implicitWidth: w.touch
                        onClicked: w.pos.transferSetQty(modelData.index, modelData.qty, Math.max(0, (modelData.loose ?? 0) - 1))
                    }
                    TextField {
                        objectName: "xferLoose-" + modelData.index
                        visible: modelData.family === true
                        text: String(modelData.loose ?? 0)
                        implicitWidth: w.unit * 4
                        implicitHeight: w.touch
                        font.pixelSize: w.unit
                        horizontalAlignment: Text.AlignHCenter
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: if (Number(text) !== modelData.loose) w.pos.transferSetQty(modelData.index, modelData.qty, Number(text) || 0)
                    }
                    Big {
                        visible: modelData.family === true
                        text: "+"; implicitWidth: w.touch
                        onClicked: w.pos.transferSetQty(modelData.index, modelData.qty, (modelData.loose ?? 0) + 1)
                    }
                    Label { visible: modelData.family === true; text: modelData.singleUnit ?? ""; font.pixelSize: w.unit }
                    Label { text: modelData.lineCost; font.pixelSize: w.unit; Layout.preferredWidth: w.unit * 6; horizontalAlignment: Text.AlignRight }
                    Big { objectName: "xferRemove-" + modelData.index; text: "✕"; implicitWidth: w.touch; onClicked: w.pos.transferSetQty(modelData.index, 0, 0) }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("To:"); font.pixelSize: w.unit }
                Repeater {
                    model: w.info.stores ?? []
                    delegate: Big {
                        required property var modelData
                        objectName: "xferTo-" + modelData.name
                        text: modelData.name
                        checkable: true
                        checked: w.info.to === modelData.name
                        highlighted: checked
                        onClicked: w.pos.transferSetTo(modelData.name)
                    }
                }
                TextField {
                    id: noteField
                    objectName: "xferNote"
                    Layout.fillWidth: true
                    implicitHeight: w.touch
                    font.pixelSize: w.unit
                    placeholderText: qsTr("Note (optional)")
                    text: w.info.note ?? ""
                    onEditingFinished: w.pos.transferSetNote(text)
                }
                Label { text: qsTr("Total at cost: %1").arg(w.info.total ?? ""); font.pixelSize: w.unit; font.bold: true }
                Big { objectName: "xferClear"; text: qsTr("Clear"); enabled: (w.info.lines ?? []).length > 0; onClicked: w.pos.transferClear() }
                Big {
                    objectName: "xferSend"
                    text: qsTr("Send")
                    highlighted: true
                    enabled: (w.info.lines ?? []).length > 0 && (w.info.to ?? "") !== ""
                    onClicked: { w.pos.transferSetNote(noteField.text); w.pos.transferSend() }
                }
            }
        }

        // --- Receive ---
        ColumnLayout {
            visible: w.tab === "in"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            ListView {
                id: waitingList
                objectName: "xferWaiting"
                visible: !w.isReceiving
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: (w.info.incoming ?? []).filter(t => t.canReceive)
                ScrollBar.vertical: TouchScrollBar { needed: waitingList.contentHeight > waitingList.height + 1 }
                delegate: RowLayout {
                    required property var modelData
                    width: waitingList.width - 12
                    spacing: 8
                    Label { text: "#" + modelData.number + "  " + modelData.text + "  ·  " + modelData.when; font.pixelSize: w.unit; Layout.fillWidth: true; elide: Text.ElideRight }
                    Big { objectName: "xferOpen-" + modelData.id; text: qsTr("Open"); onClicked: w.pos.transferStartReceive(modelData.id) }
                }
                Words {
                    anchors.centerIn: parent
                    visible: waitingList.count === 0
                    text: qsTr("Nothing waiting from the other stores.")
                }
            }
            ColumnLayout {
                visible: w.isReceiving
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Words {
                    Layout.fillWidth: true
                    font.bold: true
                    text: qsTr("Transfer #%1 from %2, sent %3 by %4: %5 at cost").arg(w.receiving.number ?? "").arg(w.receiving.from ?? "")
                              .arg(w.receiving.when ?? "").arg(w.receiving.who ?? "").arg(w.receiving.cost ?? "")
                              + ((w.receiving.note ?? "") !== "" ? "\n" + qsTr("Note: %1").arg(w.receiving.note) : "")
                }
                Words { text: qsTr("Tick each line off (or scan it). A count that's different needs why. Counts are in the units sent.") }
                ListView {
                    id: recvList
                    objectName: "xferReceiveLines"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.receiving.lines ?? []
                    ScrollBar.vertical: TouchScrollBar { needed: recvList.contentHeight > recvList.height + 1 }
                    delegate: RowLayout {
                        required property var modelData
                        width: recvList.width - 12
                        spacing: 8
                        CheckBox {
                            objectName: "xferTick-" + modelData.index
                            checked: modelData.checked
                            onClicked: w.pos.transferCheck(modelData.index, checked)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.name + "  (" + qsTr("%1 sent").arg(String(modelData.qty) + ((modelData.unit ?? "") !== "" ? " " + modelData.unit : "")) + ")"; font.pixelSize: w.unit; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label {
                                text: modelData.upc + "  ·  " + (modelData.found ? qsTr("here: %1").arg(modelData.here) : qsTr("not on file here"))
                                color: modelData.found ? palette.windowText : "#b00020"
                                font.pixelSize: w.unit * 0.75
                            }
                        }
                        Big {
                            visible: !modelData.found
                            objectName: "xferAdd-" + modelData.index
                            text: qsTr("Add it")
                            checkable: true
                            checked: modelData.add === 1
                            highlighted: checked
                            onClicked: w.pos.transferAddMissing(modelData.index, true)
                        }
                        Big {
                            visible: !modelData.found
                            objectName: "xferLeave-" + modelData.index
                            text: qsTr("Leave out")
                            checkable: true
                            checked: modelData.add === 0
                            highlighted: checked
                            onClicked: w.pos.transferAddMissing(modelData.index, false)
                        }
                        TextField {
                            id: countField
                            objectName: "xferCount-" + modelData.index
                            text: String(modelData.counted)
                            implicitWidth: w.unit * 5
                            implicitHeight: w.touch
                            font.pixelSize: w.unit
                            horizontalAlignment: Text.AlignHCenter
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onEditingFinished: if (Number(text) !== modelData.counted) w.pos.transferCount(modelData.index, Number(text) || 0, reasonField.text)
                        }
                        TextField {
                            id: reasonField
                            objectName: "xferReason-" + modelData.index
                            visible: modelData.different
                            text: modelData.reason
                            implicitWidth: w.unit * 12
                            implicitHeight: w.touch
                            font.pixelSize: w.unit * 0.85
                            placeholderText: qsTr("Why? short, damaged…")
                            onEditingFinished: w.pos.transferCount(modelData.index, Number(countField.text) || 0, text)
                        }
                    }
                }
                RowLayout {
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    Big { objectName: "xferReceiveCancel"; text: qsTr("Back"); onClicked: w.pos.transferCancelReceive() }
                    Big { objectName: "xferReceive"; text: qsTr("Receive"); highlighted: true; onClicked: w.pos.transferReceive() }
                }
            }
        }

        // --- the list, both ways ---
        ColumnLayout {
            visible: w.tab === "list"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                spacing: 8
                Label { text: qsTr("From"); font.pixelSize: w.unit }
                TextField { id: fromField; objectName: "xferFrom"; implicitHeight: w.touch; implicitWidth: w.unit * 8; font.pixelSize: w.unit; placeholderText: "2026-11-01"; text: w.info.from ?? "" }
                Label { text: qsTr("to"); font.pixelSize: w.unit }
                TextField { id: untilField; objectName: "xferUntil"; implicitHeight: w.touch; implicitWidth: w.unit * 8; font.pixelSize: w.unit; placeholderText: "2026-11-30"; text: w.info.until ?? "" }
                Big { text: qsTr("Show"); onClicked: w.pos.transferRange(fromField.text, untilField.text) }
                Words { text: qsTr("Empty: the last 30 days. Open ones always show. Reports > Transfers prints them."); Layout.fillWidth: true }
            }
            ListView {
                id: allList
                objectName: "xferList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: (w.info.outgoing ?? []).concat(w.info.incoming ?? []).sort((a, b) => b.id - a.id)
                ScrollBar.vertical: TouchScrollBar { needed: allList.contentHeight > allList.height + 1 }
                delegate: RowLayout {
                    required property var modelData
                    width: allList.width - 12
                    spacing: 8
                    Label {
                        text: "#" + modelData.number + "  " + modelData.text + "  ·  " + modelData.when
                        font.pixelSize: w.unit * 0.9
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "xferStatus-" + modelData.id
                        text: modelData.statusText
                        font.pixelSize: w.unit * 0.9
                        font.bold: true
                        color: modelData.status === "differences" ? "#b35c00" : modelData.status === "void" ? "#777777" : palette.windowText
                    }
                    Big { objectName: "xferReprint-" + modelData.id; text: qsTr("Reprint"); onClicked: w.pos.transferReprint(modelData.id) }
                    Big { visible: modelData.canVoid; objectName: "xferVoid-" + modelData.id; text: qsTr("Void"); onClicked: w.pos.transferVoid(modelData.id) }
                }
            }
        }
    }
}
