import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: ilsaRoot
    color: "#141619"
    clip: true
    property var controller: null
    property var pos: controller ? controller.pos : null
    signal exitRetailMode()
    property string numBuffer: ""
    property int pendingQty: 1
    property bool itemReturnMode: false
    property bool editButtonsMode: false
    property string statusMsg: ""
    property int editingGroup: 0
    property int editingIdx: -1
    property var row1Model: [
        { lbl: "LOG\nON/Off", col: "#d4af37", rad: 32, w: 86, cmd: "logout", pg: "", code: "" },
        { lbl: "CASHIER\nDUTY", col: "#a855f7", rad: 32, w: 86, cmd: "", pg: "drawer-count", code: "" },
        { lbl: "Reprint\nReceipt", col: "#a855f7", rad: 32, w: 86, cmd: "printReceipt", pg: "", code: "" },
        { lbl: "CANCEL SALE", col: "#ef4444", rad: 32, w: 175, cmd: "cancelCheck", pg: "", code: "" },
        { lbl: "ITEM\nRETURN", col: "#ef4444", rad: 32, w: 155, cmd: "toggleReturn", pg: "", code: "" },
        { lbl: "VOID\nPREV ITEM", col: "#ef4444", rad: 32, w: 165, cmd: "removeLine", pg: "", code: "" },
        { lbl: "TIME\nCLOCK", col: "#67e8f9", rad: 14, w: 165, cmd: "", pg: "timeclock", code: "" }
    ]
    property var row2Model: [
        { lbl: "Lighter\nRefill", col: "#60a5fa", rad: 10, w: 95, cmd: "", pg: "", code: "LIGHTER-REFILL" },
        { lbl: "EMPTY\nBOX", col: "#4ade80", rad: 10, w: 95, cmd: "", pg: "", code: "EMPTY-BOX" },
        { lbl: "Manager\nLog On/Off", col: "#e2e8f0", rad: 10, w: 105, cmd: "", pg: "manager", code: "" },
        { lbl: "PRICE\nCHECK", col: "#fde047", rad: 30, w: 105, cmd: "", pg: "find-item", code: "" },
        { lbl: "ALPHA LOOK\nUP", col: "#fde047", rad: 30, w: 150, cmd: "", pg: "menu-builder", code: "" },
        { lbl: "X\nTIMES", col: "#f1f5f9", rad: 30, w: 135, cmd: "X_TIMES", pg: "", code: "" },
        { lbl: "MISC\nTXBL", col: "#38bdf8", rad: 30, w: 105, cmd: "", pg: "", code: "MISC-TXBL" },
        { lbl: "SUSPEND", col: "#c084fc", rad: 30, w: 165, cmd: "releaseCheck", pg: "", code: "" }
    ]
    property var bottomLeftModel: [
        { lbl: "DISCOUNTS", col: "#ef4444", rad: 10, cmd: "", pg: "discounts", code: "" },
        { lbl: "ADD EMAIL", col: "#fde047", rad: 22, cmd: "", pg: "check-customer", code: "" },
        { lbl: "NO SALE", col: "#60a5fa", rad: 22, cmd: "openDrawer", pg: "", code: "" },
        { lbl: "SCALE ITEMS", col: "#2563eb", rad: 10, cmd: "", pg: "find-item", code: "" },
        { lbl: "Gift Cards", col: "#fb923c", rad: 10, cmd: "", pg: "gift-cards", code: "" },
        { lbl: "BOX ↔ STICK", col: "#0284c7", rad: 10, cmd: "", pg: "menu-builder", code: "" },
        { lbl: "Exit POS", col: "#2563eb", rad: 10, cmd: "logout", pg: "", code: "" },
        { lbl: "Cust LookUp", col: "#2dd4bf", rad: 10, cmd: "", pg: "check-customer", code: "" }
    ]
    function ringInput(codeStr) {
        var clean = codeStr.trim(); if (clean === "") return
        if (controller && (!pos || !pos.checkNumber || pos.checkNumber <= 0)) controller.command("startQuick");
        if (pos) { for (var i = 0; i < pendingQty; ++i) { if (typeof pos.scanBarcode === "function") pos.scanBarcode(clean); else if (controller) controller.command("scanBarcode", { "barcode": clean, "code": clean }) } }
        statusMsg = (itemReturnMode ? "RETURN: " : "SCANNED: ") + clean + (pendingQty > 1 ? (" (x" + pendingQty + ")") : "")
        pendingQty = 1; itemReturnMode = false; numBuffer = ""
    }
    function pressPad(key) {
        if (key === "CLEAR") { numBuffer = ""; pendingQty = 1; itemReturnMode = false; statusMsg = "" }
        else if (key === "X_TIMES") { var q = parseInt(numBuffer); if (!isNaN(q) && q > 0) { pendingQty = q; statusMsg = "X TIMES: " + q + " @" } numBuffer = "" }
        else if (key === "ENTER") ringInput(numBuffer)
        else numBuffer += key
    }
    function fireBtn(grp, idx, b) {
        if (editButtonsMode) { editingGroup = grp; editingIdx = idx; editLbl.text = b.lbl.replace("\n", " "); editCol.text = b.col; editCode.text = b.code; editPg.text = b.pg; editCmd.text = b.cmd; btnEditPopup.open(); return }
        if (b.cmd === "X_TIMES") pressPad("X_TIMES")
        else if (b.cmd === "toggleReturn") { itemReturnMode = !itemReturnMode; statusMsg = itemReturnMode ? "RETURN MODE" : "" }
        else if (b.code !== "") ringInput(b.code)
        else if (b.pg !== "" && controller) controller.jumpToPage(b.pg)
        else if (b.cmd !== "" && controller) controller.command(b.cmd)
    }
    RowLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 14
        // LEFT COLUMN (35% Width): Top 2/3 Receipt Window + Bottom 1/3 Buttons
        ColumnLayout {
            Layout.preferredWidth: Math.round(ilsaRoot.width * 0.35); Layout.fillHeight: true; spacing: 6
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: Math.round(ilsaRoot.height * 0.58)
                color: "#f4f6fa"; border.color: "#94a3b8"; border.width: 2; clip: true
                Loader { anchors.fill: parent; anchors.margins: 4; source: "WidgetOrderPanel.qml"; onLoaded: { if (item) { item.controller = ilsaRoot.controller; item.pos = ilsaRoot.pos } } }
            }
            Text { text: "Cartons: 0          Items: " + (pos && pos.checkItemCount ? pos.checkItemCount : "0"); color: "#fff"; font.pixelSize: 18; font.bold: true }
            GridLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; columns: 2; rowSpacing: 6; columnSpacing: 8
                Repeater {
                    model: ilsaRoot.bottomLeftModel
                    delegate: Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; radius: modelData.rad; color: modelData.col
                        border.color: ilsaRoot.editButtonsMode ? "#facc15" : "#cbd5e1"; border.width: ilsaRoot.editButtonsMode ? 3 : 1
                        Text { anchors.centerIn: parent; text: modelData.lbl; horizontalAlignment: Text.AlignHCenter; color: modelData.col === "#2563eb" ? "#fff" : "#111827"; font.pixelSize: 15; font.bold: true }
                        MouseArea { anchors.fill: parent; onClicked: ilsaRoot.fireBtn(3, index, modelData) }
                    }
                }
            }
        }
        // RIGHT COLUMN (65% Width): Top Mode/Price Header + Function Rows + Big Center Keypad
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 110
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 10
                        Rectangle {
                            width: 145; height: 38; radius: 6; color: ilsaRoot.editButtonsMode ? "#ca8a04" : "#334155"; border.color: "#facc15"
                            Text { anchors.centerIn: parent; text: ilsaRoot.editButtonsMode ? "✓ Done Editing" : "✏️ Edit Screen"; color: "#fff"; font.pixelSize: 13; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: ilsaRoot.editButtonsMode = !ilsaRoot.editButtonsMode }
                        }
                        Rectangle {
                            width: 165; height: 38; radius: 6; color: "#1e3a8a"; border.color: "#3b82f6"
                            Text { anchors.centerIn: parent; text: "🍽️ Hospitality Mode"; color: "#fff"; font.pixelSize: 13; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: {
                                ilsaRoot.exitRetailMode()
                                if (controller) {
                                    if (pos && pos.checkNumber > 0) controller.jumpToPage("order")
                                    else if (typeof controller.back === "function" && controller.canBack) controller.back()
                                    else controller.jumpToPage("tables")
                                }
                            } }
                        }
                    }
                    Text { text: ilsaRoot.statusMsg; color: "#facc15"; font.pixelSize: 18; font.bold: true }
                }
                Text { text: ilsaRoot.numBuffer !== "" ? ilsaRoot.numBuffer : (pos && pos.checkTotal ? pos.checkTotal : "$0.00"); color: "#fff"; font.pixelSize: 68; font.bold: true }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 62; spacing: 8
                Repeater {
                    model: ilsaRoot.row1Model
                    delegate: Rectangle {
                        Layout.fillWidth: true; Layout.preferredWidth: modelData.w; Layout.fillHeight: true; radius: modelData.rad; color: modelData.col; border.color: ilsaRoot.editButtonsMode ? "#facc15" : "#fff"; border.width: ilsaRoot.editButtonsMode ? 3 : 1
                        Text { anchors.centerIn: parent; text: modelData.lbl; horizontalAlignment: Text.AlignHCenter; color: "#111827"; font.pixelSize: 14; font.bold: true }
                        MouseArea { anchors.fill: parent; onClicked: ilsaRoot.fireBtn(1, index, modelData) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 60; spacing: 8
                Repeater {
                    model: ilsaRoot.row2Model
                    delegate: Rectangle {
                        Layout.fillWidth: true; Layout.preferredWidth: modelData.w; Layout.fillHeight: true; radius: modelData.rad; color: modelData.col; border.color: ilsaRoot.editButtonsMode ? "#facc15" : "#fff"; border.width: ilsaRoot.editButtonsMode ? 3 : 1
                        Text { anchors.centerIn: parent; text: modelData.lbl; horizontalAlignment: Text.AlignHCenter; color: "#111827"; font.pixelSize: 13; font.bold: true }
                        MouseArea { anchors.fill: parent; onClicked: ilsaRoot.fireBtn(2, index, modelData) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
                // BIG 3x4 WHITE PILL KEYPAD (75% of Right Area Width)
                GridLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; columns: 3; rowSpacing: 10; columnSpacing: 14
                    Repeater {
                        model: ["7","8","9","4","5","6","1","2","3","0","00","."]
                        delegate: Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; radius: 30; color: "#e2e8f0"; border.color: "#94a3b8"; border.width: 2
                            Text { anchors.centerIn: parent; text: modelData; color: "#0f172a"; font.pixelSize: 38; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: ilsaRoot.pressPad(modelData) }
                        }
                    }
                }
                // RIGHT ACTION COLUMN (Fixed 210px Width)
                ColumnLayout {
                    Layout.preferredWidth: 210; Layout.fillHeight: true; spacing: 10
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 60; radius: 30; color: "#c084fc"; border.color: "#fff"; Text { anchors.centerIn: parent; text: "RESUME"; color: "#111827"; font.pixelSize: 22; font.bold: true } MouseArea { anchors.fill: parent; onClicked: if (controller) controller.jumpToPage("find-check") } }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 60; radius: 30; color: "#e2e8f0"; border.color: "#94a3b8"; Text { anchors.centerIn: parent; text: "CLEAR"; color: "#111827"; font.pixelSize: 24; font.bold: true } MouseArea { anchors.fill: parent; onClicked: ilsaRoot.pressPad("CLEAR") } }
                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: 65; color: "#4ade80"; border.color: "#bbf7d0"; border.width: 3; Text { anchors.centerIn: parent; text: "SUB\nTOTAL"; horizontalAlignment: Text.AlignHCenter; color: "#111827"; font.pixelSize: 26; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { if (ilsaRoot.numBuffer !== "") ilsaRoot.ringInput(ilsaRoot.numBuffer); else if (controller) controller.jumpToPage("settle") } } }
                }
            }
        }
    }
    Popup {
        id: btnEditPopup; anchors.centerIn: parent; width: 460; height: 360; modal: true
        background: Rectangle { color: "#1e293b"; radius: 10; border.color: "#facc15"; border.width: 2 }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 8
            Text { text: "✏️ Edit Retail Button"; color: "#facc15"; font.pixelSize: 18; font.bold: true }
            TextField { id: editLbl; Layout.fillWidth: true; placeholderText: "Button Label" }
            TextField { id: editCol; Layout.fillWidth: true; placeholderText: "Hex Color (e.g. #ef4444)" }
            TextField { id: editCode; Layout.fillWidth: true; placeholderText: "Ring UPC / PLU Code (optional)" }
            TextField { id: editPg; Layout.fillWidth: true; placeholderText: "Jump to Page (e.g. discounts, manager)" }
            TextField { id: editCmd; Layout.fillWidth: true; placeholderText: "POS Command (e.g. openDrawer, logout)" }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Button { text: "Cancel"; Layout.fillWidth: true; onClicked: btnEditPopup.close() }
                Button {
                    text: "Save Button"; Layout.fillWidth: true
                    onClicked: {
                        var arr = ilsaRoot.editingGroup === 1 ? ilsaRoot.row1Model.slice() : (ilsaRoot.editingGroup === 2 ? ilsaRoot.row2Model.slice() : ilsaRoot.bottomLeftModel.slice())
                        arr[ilsaRoot.editingIdx] = Object.assign({}, arr[ilsaRoot.editingIdx], { lbl: editLbl.text, col: editCol.text, code: editCode.text, pg: editPg.text, cmd: editCmd.text })
                        if (ilsaRoot.editingGroup === 1) ilsaRoot.row1Model = arr; else if (ilsaRoot.editingGroup === 2) ilsaRoot.row2Model = arr; else ilsaRoot.bottomLeftModel = arr
                        btnEditPopup.close()
                    }
                }
            }
        }
    }
}
