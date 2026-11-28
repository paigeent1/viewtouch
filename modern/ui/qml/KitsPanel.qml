import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Menu Builder > Mass Updates > Sampler Kits (owner 2026-11-27, after ILSA's KIT table; PosService::kitsOpen,
// app/pos_kits.cpp). The kits (name, UPC, kit price, how many things in it, what they'd sell for alone, its cost).
// Make a kit: find the kit item (made first in the Menu Builder with its UPC and kit price) and choose it; then
// its contents: Find and Add, a UPC, or the Mass Updates ticks; each with how many; "Print contents on the
// receipt". Apply saves (an admin, or an admin's PIN) and sets the kit's cost to its contents'. When a kit sells,
// its contents leave stock. "Not a kit" takes the contents off; the item stays.
Rectangle {
    id: p
    objectName: "kitsPanel"
    property var pos: null
    property real unit: 16
    property real touch: Math.max(44, unit * 2.6)
    readonly property var info: pos ? pos.kits : ({})
    readonly property var list: info.list ?? []
    readonly property var draft: info.draft ?? null
    readonly property var found: info.found ?? []
    property string selectedId: ""
    property bool choosing: false     // finding the item that becomes a kit
    signal pickItems()
    signal closed()
    color: "#f8fafc"

    component Small: Button {
        property real u: 16
        implicitHeight: Math.max(44, u * 2.6)
        Layout.minimumWidth: 48
        font.pixelSize: u * 0.8
    }
    component Field: TextField {
        property real u: 16
        property string serverValue: ""
        onServerValueChanged: if (!activeFocus) text = serverValue
        Component.onCompleted: text = serverValue
        implicitHeight: Math.max(44, u * 2.6)
        font.pixelSize: u * 0.8
    }
    component Cell: Label {
        property real u: 16
        font.pixelSize: u * 0.72
        color: "#111827"
        elide: Text.ElideRight
    }
    component Head: Label {
        property real u: 16
        font.pixelSize: u * 0.7
        font.bold: true
        color: "#374151"
        elide: Text.ElideRight
    }

    MouseArea { anchors.fill: parent }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: p.unit * 0.6
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Sampler Kits"); font.pixelSize: p.unit * 1.1; font.bold: true; color: "#111827" }
            Small { u: p.unit; objectName: "kitMake"; text: qsTr("Make a kit…"); enabled: !p.draft; onClicked: { p.choosing = true; p.pos.kitFind("") } }
            Item { Layout.fillWidth: true }
            Small { u: p.unit; objectName: "kitPickItems"; visible: !!p.draft; text: qsTr("Tick items in Mass Updates"); onClicked: p.pickItems() }
            Small { u: p.unit; objectName: "kitClose"; text: qsTr("Close"); onClicked: p.closed() }
        }
        Label {
            visible: text !== ""
            Layout.fillWidth: true
            text: p.info.message ?? ""
            font.pixelSize: p.unit * 0.8
            color: "#b45309"
            wrapMode: Text.WordWrap
        }

        // --- Find: the kit item (choosing) or its contents (editing) ---
        RowLayout {
            visible: p.choosing || !!p.draft
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: p.draft ? qsTr("Add to the kit:") : qsTr("The kit item (make it first in the Menu Builder, with its UPC and kit price):")
                font.pixelSize: p.unit * 0.8
                color: "#111827"
            }
            Field { id: find; u: p.unit; objectName: "kitFind"; Layout.fillWidth: true; placeholderText: qsTr("Find description or UPC")
                    onAccepted: p.pos.kitFind(text) }
            Small { u: p.unit; text: qsTr("Find"); onClicked: p.pos.kitFind(find.text) }
            Small { u: p.unit; visible: p.choosing && !p.draft; text: qsTr("Cancel"); onClicked: { p.choosing = false; p.pos.kitFind("") } }
        }
        ListView {
            id: foundList
            objectName: "kitFound"
            visible: (p.choosing || !!p.draft) && p.found.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, p.touch * 4)
            clip: true
            spacing: 1
            model: p.found
            ScrollBar.vertical: TouchScrollBar { id: foundBar; needed: foundList.contentHeight > foundList.height + 1 }
            delegate: Rectangle {
                id: fr
                required property var modelData
                required property int index
                width: ListView.view.width - foundBar.room
                height: p.touch * 0.9
                color: fr.index % 2 ? "#ffffff" : "#f1f5f9"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    spacing: 6
                    Cell { u: p.unit; text: fr.modelData.upc; Layout.preferredWidth: p.unit * 8 }
                    Cell { u: p.unit; text: fr.modelData.name + (fr.modelData.isKit ? "  " + qsTr("(a kit)") : ""); Layout.fillWidth: true }
                    Cell { u: p.unit; text: fr.modelData.onHand !== "" ? qsTr("%1 on hand").arg(fr.modelData.onHand) : ""; Layout.preferredWidth: p.unit * 6 }
                    Cell { u: p.unit; text: fr.modelData.price; Layout.preferredWidth: p.unit * 4.5; horizontalAlignment: Text.AlignRight }
                    Small {
                        u: p.unit
                        objectName: "kitFoundPick-" + fr.index
                        text: p.draft ? qsTr("Add") : qsTr("Choose")
                        onClicked: {
                            if (p.draft) p.pos.kitAddParts([fr.modelData.id])
                            else { p.choosing = false; p.pos.kitEdit(fr.modelData.id) }
                        }
                    }
                }
            }
        }

        // --- the kits ---
        ColumnLayout {
            visible: !p.draft && !p.choosing
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Head { u: p.unit; text: qsTr("Kit"); Layout.fillWidth: true }
                Head { u: p.unit; text: qsTr("UPC"); Layout.preferredWidth: p.unit * 8 }
                Head { u: p.unit; text: qsTr("Kit price"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("Things in it"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("Alone"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("Cost"); Layout.preferredWidth: p.unit * 5 }
            }
            ListView {
                id: kitList
                objectName: "kitList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                model: p.list
                ScrollBar.vertical: TouchScrollBar { id: kitBar; needed: kitList.contentHeight > kitList.height + 1 }
                delegate: Rectangle {
                    id: kr
                    required property var modelData
                    required property int index
                    objectName: "kitRow-" + modelData.id
                    width: ListView.view.width - kitBar.room
                    height: p.touch
                    color: kr.modelData.id === p.selectedId ? "#dbeafe" : (kr.index % 2 ? "#ffffff" : "#f1f5f9")
                    TapHandler { onTapped: p.selectedId = kr.modelData.id }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        spacing: 4
                        Cell { u: p.unit; text: kr.modelData.name; Layout.fillWidth: true; font.bold: true }
                        Cell { u: p.unit; text: kr.modelData.upc; Layout.preferredWidth: p.unit * 8 }
                        Cell { u: p.unit; text: kr.modelData.price; Layout.preferredWidth: p.unit * 5 }
                        Cell { u: p.unit; text: kr.modelData.parts; Layout.preferredWidth: p.unit * 5 }
                        Cell { u: p.unit; text: kr.modelData.value; Layout.preferredWidth: p.unit * 5 }
                        Cell { u: p.unit; text: kr.modelData.cost || "—"; Layout.preferredWidth: p.unit * 5 }
                    }
                }
                Label {
                    visible: p.list.length === 0
                    anchors.centerIn: parent
                    text: qsTr("No kits yet: make the kit item in the Menu Builder, then touch Make a kit.")
                    font.pixelSize: p.unit * 0.9
                    color: "#6b7280"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Small { u: p.unit; objectName: "kitEdit"; text: qsTr("Edit"); enabled: p.selectedId !== ""; onClicked: p.pos.kitEdit(p.selectedId) }
                Small { u: p.unit; objectName: "kitUnmake"; text: qsTr("Not a kit…"); enabled: p.selectedId !== ""; onClicked: confirmUnmake.open() }
            }
        }

        // --- one kit ---
        ColumnLayout {
            visible: !!p.draft
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: !p.draft ? "" : p.draft.name + "  ·  " + p.draft.upc + "  ·  " + qsTr("kit price %1").arg(p.draft.price)
                font.pixelSize: p.unit * 0.95
                font.bold: true
                color: "#111827"
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Head { u: p.unit; text: qsTr("How many"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("UPC"); Layout.preferredWidth: p.unit * 8 }
                Head { u: p.unit; text: qsTr("In the kit"); Layout.fillWidth: true }
                Head { u: p.unit; text: qsTr("On hand"); Layout.preferredWidth: p.unit * 5 }
                Head { u: p.unit; text: qsTr("Price alone"); Layout.preferredWidth: p.unit * 5 }
                Item { Layout.preferredWidth: p.touch }
            }
            ListView {
                id: partList
                objectName: "kitParts"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 1
                model: p.draft ? p.draft.parts : []
                ScrollBar.vertical: TouchScrollBar { id: partBar; needed: partList.contentHeight > partList.height + 1 }
                delegate: Rectangle {
                    id: pr
                    required property var modelData
                    required property int index
                    width: ListView.view.width - partBar.room
                    height: p.touch
                    color: pr.index % 2 ? "#ffffff" : "#f1f5f9"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        spacing: 4
                        Field { u: p.unit; objectName: "kitQty-" + pr.index; Layout.preferredWidth: p.unit * 5; serverValue: pr.modelData.qty
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onEditingFinished: if (text !== serverValue) p.pos.kitSetQty(pr.modelData.upc, text) }
                        Cell { u: p.unit; text: pr.modelData.upc; Layout.preferredWidth: p.unit * 8 }
                        Cell { u: p.unit; text: pr.modelData.name; Layout.fillWidth: true; color: pr.modelData.found ? "#111827" : "#b91c1c" }
                        Cell { u: p.unit; text: pr.modelData.onHand; Layout.preferredWidth: p.unit * 5 }
                        Cell { u: p.unit; text: pr.modelData.price; Layout.preferredWidth: p.unit * 5; horizontalAlignment: Text.AlignRight }
                        Small { u: p.unit; text: "✕"; Layout.preferredWidth: p.touch; onClicked: p.pos.kitSetQty(pr.modelData.upc, "0") }
                    }
                }
                Label {
                    visible: partList.count === 0
                    anchors.centerIn: parent
                    text: qsTr("Nothing in it yet: Find above and Add, or scan / type a UPC below.")
                    font.pixelSize: p.unit * 0.8
                    color: "#6b7280"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Field { id: upc; u: p.unit; objectName: "kitUpc"; Layout.preferredWidth: p.unit * 12; placeholderText: qsTr("Scan or type a UPC")
                        inputMethodHints: Qt.ImhDigitsOnly; onAccepted: { p.pos.kitAddParts([text]); text = "" } }
                Small { u: p.unit; text: qsTr("Add UPC"); onClicked: { p.pos.kitAddParts([upc.text]); upc.text = "" } }
                CheckBox {
                    objectName: "kitPrint"
                    text: qsTr("Print contents on the receipt")
                    font.pixelSize: p.unit * 0.75
                    checked: !!p.draft && p.draft.print
                    onClicked: p.pos.kitSetPrint(checked)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: !p.draft ? "" : qsTr("Alone: %1").arg(p.draft.value) + (p.draft.cost ? "   " + qsTr("Cost: %1").arg(p.draft.cost) : "")
                    font.pixelSize: p.unit * 0.8
                    color: "#111827"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { visible: !p.info.canApply; text: qsTr("An admin's PIN will be asked for."); font.pixelSize: p.unit * 0.75; color: "#4b5563" }
                Item { Layout.fillWidth: true }
                Small { u: p.unit; objectName: "kitApply"; text: qsTr("Apply"); palette.button: "#15803d"; palette.buttonText: "white"
                        onClicked: { forceActiveFocus(); p.pos.kitApply() } }
                Small { u: p.unit; objectName: "kitAbort"; text: qsTr("Abort"); onClicked: p.pos.kitAbort() }
            }
        }
    }

    Dialog {
        id: confirmUnmake
        objectName: "kitConfirmUnmake"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Make %1 a plain item again?").arg((p.list.find(k => k.id === p.selectedId) ?? {}).name ?? "")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Its contents come off; the item and its sales stay."); font.pixelSize: p.unit * 0.8 }
        onAccepted: { p.pos.kitUnmake(p.selectedId); p.selectedId = "" }
    }
}
