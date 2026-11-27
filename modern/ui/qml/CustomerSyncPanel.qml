import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// TEMPORARY: remove (with app/pos_customer_sync.cpp) when the corporate customer list exists.
// Manager → Customers, under the list: customers shared with the other stores (Store Settings → Other stores →
// Share customers). Send All Customers Now (the first share), and Check These Customers: the same phone at
// another store with a clearly different name, for a manager to merge or keep apart. Managers only.
ColumnLayout {
    id: panel
    property PosService pos
    readonly property var sync: pos ? pos.customerSync : ({})
    readonly property var conflicts: sync.conflicts ?? []
    visible: (sync.stores ?? "") !== "" || conflicts.length > 0
    spacing: 4
    onConflictsChanged: if (conflicts.length === 0) checkWindow.close()

    Label {
        Layout.fillWidth: true
        visible: text !== ""
        wrapMode: Text.WordWrap
        opacity: 0.7
        font.pixelSize: 13
        text: panel.sync.note ?? ""
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Button {
            objectName: "customerSyncSendAll"
            visible: panel.sync.on ?? false
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            implicitHeight: 44
            text: qsTr("Send All Customers Now")
            onClicked: panel.pos.customerSyncSendAll()
        }
        Button {
            objectName: "customerSyncCheck"
            visible: panel.conflicts.length > 0
            highlighted: true
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            implicitHeight: 44
            text: qsTr("Check These Customers (%1)").arg(panel.conflicts.length)
            onClicked: checkWindow.open()
        }
    }

    Popup {
        id: checkWindow
        objectName: "customerSyncWindow"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 720, 720)
        height: Math.min(parent ? parent.height - 32 : 640, 640)
        padding: 18
        background: Rectangle { color: "#1d232d"; radius: 10; border.color: "#f0b429"; border.width: 2 }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("Check These Customers")
                font.pixelSize: 24; font.bold: true; color: "#f0b429"
            }
            Label {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                wrapMode: Text.WordWrap; color: "white"
                text: qsTr("Another store has a customer with the same phone number but a different name. "
                           + "Nothing was changed. Same person: Merge. Different people: Keep Apart.")
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: panel.conflicts
                ScrollBar.vertical: TouchScrollBar { id: bar }
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    width: ListView.view.width - bar.room
                    implicitHeight: row.implicitHeight + 16
                    radius: 6; color: "#2b3340"
                    RowLayout {
                        id: row
                        anchors.fill: parent; anchors.margins: 8; spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; spacing: 2
                            Label { Layout.fillWidth: true; elide: Text.ElideRight; color: "white"; font.bold: true
                                    text: qsTr("Here: %1").arg(card.modelData.hereName || qsTr("(no name)")) }
                            Label { Layout.fillWidth: true; elide: Text.ElideRight; color: "#c3cad6"
                                    text: [card.modelData.herePhone, card.modelData.hereEmail,
                                           qsTr("%n visit(s)", "", card.modelData.hereVisits ?? 0)].filter(x => (x ?? "") !== "").join("  ·  ") }
                            Label { Layout.fillWidth: true; elide: Text.ElideRight; color: "white"; font.bold: true
                                    text: qsTr("At %1: %2").arg(card.modelData.from).arg(card.modelData.thereName || qsTr("(no name)")) }
                            Label { Layout.fillWidth: true; elide: Text.ElideRight; color: "#c3cad6"
                                    text: [card.modelData.phone, card.modelData.thereEmail, card.modelData.thereAddress]
                                          .filter(x => (x ?? "") !== "").join("  ·  ") }
                        }
                        Button { text: qsTr("Merge"); implicitHeight: 44
                                 onClicked: panel.pos.customerSyncResolve(card.modelData.key, "merge") }
                        Button { text: qsTr("Keep Apart"); implicitHeight: 44
                                 onClicked: panel.pos.customerSyncResolve(card.modelData.key, "apart") }
                    }
                }
            }
            Button {
                Layout.fillWidth: true; implicitHeight: 48
                text: qsTr("Close")
                onClicked: checkWindow.close()
            }
        }
    }
}
