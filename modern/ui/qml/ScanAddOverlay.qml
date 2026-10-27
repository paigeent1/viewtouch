import QtQuick
import QtQuick.Layouts

Rectangle {
    id: ov
    anchors.fill: parent; z: 92; visible: barcode !== ""; color: "#aa0b0f17"
    property var controller: null
    property string barcode: ""
    property string step: "ask"
    property string pin: ""
    property string pinVerified: ""
    property string errorText: ""
    readonly property string permMode: (controller && controller.pos && controller.pos.scanAddPermission) ? String(controller.pos.scanAddPermission) : "manager"
    function promptFor(c) { barcode = String(c ?? "").trim(); step = "ask"; pin = ""; pinVerified = ""; errorText = "" }
    function closeOverlay() { barcode = ""; step = "ask"; pin = ""; pinVerified = ""; errorText = "" }
    function chooseYes() { if (permMode === "none") { step = "type"; errorText = "" } else { step = "pin"; pin = ""; errorText = "" } }
    function submitPin() { if (pin.length === 0) return; pinVerified = pin; step = "type"; errorText = "" }
    function selectType(mode) {
        if (!controller) return
        const c = barcode; const p = pinVerified; const pm = permMode
        closeOverlay()
        if (pm === "none") controller.openNewItemWithBarcode(c, mode)
        else controller.submitScanAddPin(p, c, mode)
    }
    MouseArea { anchors.fill: parent; hoverEnabled: true }
    Rectangle {
        width: Math.min(parent.width * 0.90, 580); height: col.implicitHeight + 48
        anchors.centerIn: parent; radius: 16; color: "#1e2430"; border.color: "#3b82f6"; border.width: 2
        ColumnLayout {
            id: col; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 24; spacing: 16
            Text { Layout.fillWidth: true; color: "#f8fafc"; font.pixelSize: 24; font.bold: true; horizontalAlignment: Text.AlignHCenter; text: ov.step === "ask" ? qsTr("Item Not on File") : (ov.step === "pin" ? qsTr("Enter PIN to Add Item") : qsTr("Select Entry Mode for New Item")) }
            Text { Layout.fillWidth: true; color: "#cbd5e1"; font.pixelSize: 18; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; text: ov.step === "ask" ? qsTr("Barcode \"%1\" is not on file.\nDo you want to add this item?").arg(ov.barcode) : qsTr("Barcode: %1").arg(ov.barcode) }
            Text { Layout.fillWidth: true; visible: ov.errorText !== ""; text: ov.errorText; color: "#f87171"; font.pixelSize: 15; font.bold: true; horizontalAlignment: Text.AlignHCenter }
            RowLayout {
                Layout.fillWidth: true; spacing: 20; visible: ov.step === "ask"
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 64; radius: 12; color: "#16a34a"; Text { anchors.centerIn: parent; text: qsTr("Yes (Y)"); color: "#fff"; font.pixelSize: 22; font.bold: true } MouseArea { anchors.fill: parent; onClicked: ov.chooseYes() } }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 64; radius: 12; color: "#dc2626"; Text { anchors.centerIn: parent; text: qsTr("No (N)"); color: "#fff"; font.pixelSize: 22; font.bold: true } MouseArea { anchors.fill: parent; onClicked: ov.closeOverlay() } }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 12; visible: ov.step === "type"
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 84; radius: 12; color: "#5c3b1d"; border.color: "#d97706"; border.width: 2; ColumnLayout { anchors.centerIn: parent; spacing: 4; Text { Layout.alignment: Qt.AlignHCenter; text: "🍽️ F&B"; color: "#fff"; font.pixelSize: 20; font.bold: true } Text { Layout.alignment: Qt.AlignHCenter; text: "Hospitality"; color: "#fde68a"; font.pixelSize: 13 } } MouseArea { anchors.fill: parent; onClicked: ov.selectType("hospitality") } }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 84; radius: 12; color: "#1f5f3a"; border.color: "#10b981"; border.width: 2; ColumnLayout { anchors.centerIn: parent; spacing: 4; Text { Layout.alignment: Qt.AlignHCenter; text: "🏷️ Retail"; color: "#fff"; font.pixelSize: 20; font.bold: true } Text { Layout.alignment: Qt.AlignHCenter; text: "Merchandise"; color: "#a7f3d0"; font.pixelSize: 13 } } MouseArea { anchors.fill: parent; onClicked: ov.selectType("retail") } }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 84; radius: 12; color: "#5e2b6b"; border.color: "#c084fc"; border.width: 2; ColumnLayout { anchors.centerIn: parent; spacing: 4; Text { Layout.alignment: Qt.AlignHCenter; text: "🎟️ Event"; color: "#fff"; font.pixelSize: 20; font.bold: true } Text { Layout.alignment: Qt.AlignHCenter; text: "Ticket / Adm"; color: "#e9d5ff"; font.pixelSize: 13 } } MouseArea { anchors.fill: parent; onClicked: ov.selectType("event") } }
                }
                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 220; Layout.preferredHeight: 44; radius: 10; color: "#dc2626"; Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: "#fff"; font.pixelSize: 17; font.bold: true } MouseArea { anchors.fill: parent; onClicked: ov.closeOverlay() } }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 12; visible: ov.step === "pin"
                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 260; Layout.preferredHeight: 48; radius: 8; color: "#0f172a"; border.color: "#475569"; Text { anchors.centerIn: parent; text: "•".repeat(ov.pin.length); color: "#f8fafc"; font.pixelSize: 28; font.letterSpacing: 6 } }
                GridLayout {
                    Layout.alignment: Qt.AlignHCenter; columns: 3; rowSpacing: 10; columnSpacing: 10
                    Repeater { model: ["1","2","3","4","5","6","7","8","9","Clear","0","OK"]
                        Rectangle { required property string modelData; Layout.preferredWidth: 88; Layout.preferredHeight: 54; radius: 10; color: modelData === "OK" ? "#16a34a" : (modelData === "Clear" ? "#475569" : "#334155")
                            Text { anchors.centerIn: parent; text: parent.modelData; color: "#fff"; font.pixelSize: 20; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: { if (parent.modelData === "Clear") ov.pin = ""; else if (parent.modelData === "OK") ov.submitPin(); else if (ov.pin.length < 8) ov.pin += parent.modelData } } } }
                }
                Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 284; Layout.preferredHeight: 46; radius: 10; color: "#dc2626"; Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: "#fff"; font.pixelSize: 18; font.bold: true } MouseArea { anchors.fill: parent; onClicked: ov.closeOverlay() } }
            }
        }
    }
}
