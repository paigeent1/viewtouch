import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// ITEM RETURN (the Retail screen's button, or any button with the command "Item return"):
//  1. "Find the old sale" (Find a Check, Refund… there) or "Scan or look up the item".
//  2. Scan it (a scanner types into the box and presses Enter), or type its UPC or part of its name.
//  3. The item at its price-book price, already filled in: change the price if it sold for another
//     amount, how many, and why. "Add return" puts it on the sale as a RETURN line (a manager
//     approves); Cancel adds nothing.
// The main computer keeps the window's state (pos.itemReturn), so a tablet works the same.
Rectangle {
    id: sheet
    property PosService pos
    property var controller
    readonly property var info: pos ? pos.itemReturn : ({})
    readonly property string step: info.step ?? ""
    readonly property var item: info.item ?? null
    readonly property var matches: info.matches ?? []
    readonly property real u: Math.max(14, Math.min(width, height) / 30)
    property string reason: ""
    visible: info.on === true
    color: Qt.rgba(0.04, 0.05, 0.07, 0.92)

    // Cents from what was typed: "12.5" -> 1250, "12" -> 1200; -1: not a price.
    function cents(text) {
        const t = String(text).trim().replace("$", "")
        if (!/^\d{0,6}(\.\d{0,2})?$/.test(t) || t === "" || t === ".")
            return -1
        return Math.round(parseFloat(t) * 100)
    }
    function add() {
        const c = cents(priceField.text)
        const q = parseInt(qtyField.text, 10)
        if (!sheet.item || c <= 0 || !(q >= 1 && q <= 99) || sheet.reason === "")
            return
        sheet.pos.addItemReturn(sheet.item.id, c, q, sheet.reason, sheet.item.upc ?? "")
    }
    function lookUp() {
        const code = codeField.text.trim()
        if (code === "")
            return
        // As a scan: a receipt's barcode is the old sale (Find a Check), anything else the item.
        if (sheet.controller)
            sheet.controller.scanned(code)
        else
            sheet.pos.itemReturnCode(code)
        codeField.selectAll()   // not found: the next scan replaces it
    }

    onStepChanged: {
        reason = ""
        if (step === "choose" || step === "scan") {
            // A scanner that types (keyboard mode) types here, then Enter.
            codeField.text = info.typed ?? ""
            Qt.callLater(() => codeField.forceActiveFocus())
        } else if (step === "price") {
            Qt.callLater(() => { priceField.forceActiveFocus(); priceField.selectAll() })
        }
    }
    onItemChanged: if (item) {
        priceField.text = (item.priceCents / 100).toFixed(2)
        qtyField.text = "1"
    }
    onVisibleChanged: if (!visible) { reason = ""; codeField.text = "" }

    // Touches stop here, not on the page behind.
    MouseArea { anchors.fill: parent; hoverEnabled: true }
    Keys.onEscapePressed: pos.cancelItemReturn()

    Rectangle {
        // Near the top: the on-screen keyboard comes up from the bottom.
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(sheet.u * 0.6, Math.min(parent.height * 0.06, parent.height - height - sheet.u))
        width: Math.min(parent.width - 2 * sheet.u, 34 * sheet.u)
        height: Math.min(parent.height - 2 * sheet.u, column.implicitHeight + 2 * sheet.u)
        radius: sheet.u * 0.8
        color: "#232933"
        border.color: "#ef4444"
        border.width: 3

        Flickable {
            anchors.fill: parent
            anchors.margins: sheet.u
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: column
                width: parent.width
                spacing: sheet.u * 0.6

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Item return")
                    color: "#ff9a9e"
                    font.pixelSize: 1.6 * sheet.u
                    font.bold: true
                }

                // 1. The old sale, or the item?
                ColumnLayout {
                    visible: sheet.step === "choose"
                    Layout.fillWidth: true
                    spacing: sheet.u * 0.6
                    WidgetKey {
                        objectName: "returnOldSale"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4 * sheet.u
                        text: qsTr("Find the old sale")
                        fontScale: 0.3
                        baseColor: "#2f6fd6"
                        takesTouch: true
                        onClicked: {
                            sheet.pos.cancelItemReturn()
                            if (sheet.controller)
                                sheet.controller.command("findOldSale")
                        }
                    }
                    WidgetKey {
                        objectName: "returnScanItem"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4 * sheet.u
                        text: qsTr("Scan or look up the item")
                        fontScale: 0.3
                        baseColor: "#1f6b40"
                        takesTouch: true
                        onClicked: sheet.pos.itemReturnStep("scan")
                    }
                }

                // 2. Scan, or type a UPC or part of the name (a receipt's barcode: the old sale).
                ColumnLayout {
                    visible: sheet.step === "scan" || sheet.step === "choose"
                    Layout.fillWidth: true
                    spacing: sheet.u * 0.6
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: sheet.step === "choose" ? qsTr("Or scan the item, or the receipt's barcode, now.")
                                                      : qsTr("Scan the item, or type its UPC or part of its name")
                        color: sheet.step === "choose" ? "#b8c0cc" : "white"
                        font.pixelSize: (sheet.step === "choose" ? 1.0 : 1.2) * sheet.u
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: sheet.u / 2
                        TextField {
                            id: codeField
                            objectName: "returnCode"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3.4 * sheet.u
                            font.pixelSize: 1.4 * sheet.u
                            inputMethodHints: Qt.ImhNoPredictiveText
                            placeholderText: qsTr("UPC or name")
                            onAccepted: sheet.lookUp()
                        }
                        WidgetKey {
                            objectName: "returnFind"
                            visible: sheet.step === "scan"
                            Layout.preferredWidth: 8 * sheet.u
                            Layout.preferredHeight: 3.4 * sheet.u
                            text: qsTr("Find")
                            fontScale: 0.3
                            baseColor: "#2f6fd6"
                            takesTouch: true
                            onClicked: sheet.lookUp()
                        }
                    }
                    WidgetKey {
                        objectName: "returnCamera"
                        visible: sheet.step === "scan" && sheet.controller !== null && sheet.controller !== undefined
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3 * sheet.u
                        text: qsTr("Scan with the camera")
                        fontScale: 0.28
                        takesTouch: true
                        onClicked: sheet.controller.command("scanCamera")
                    }
                    Repeater {
                        model: sheet.matches
                        delegate: WidgetKey {
                            required property var modelData
                            required property int index
                            objectName: "returnMatch-" + index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3 * sheet.u
                            text: modelData.name + "   " + modelData.price
                            fontScale: 0.26
                            takesTouch: true
                            onClicked: sheet.pos.itemReturnPick(modelData.id)
                        }
                    }
                }

                // 3. The item at its price, to change; how many; why.
                ColumnLayout {
                    visible: sheet.step === "price" && sheet.item !== null
                    Layout.fillWidth: true
                    spacing: sheet.u * 0.5
                    Text {
                        objectName: "returnItemName"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: sheet.item ? sheet.item.name : ""
                        color: "white"
                        font.pixelSize: 1.5 * sheet.u
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: sheet.item ? qsTr("Price book: %1").arg(sheet.item.price)
                                           + (sheet.item.upc ? "   ·   " + sheet.item.upc : "") : ""
                        color: "#b8c0cc"
                        font.pixelSize: 1.0 * sheet.u
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: sheet.u / 2
                        rowSpacing: sheet.u / 2
                        Text {
                            text: sheet.item && sheet.item.taxIncluded ? qsTr("Price back (tax in it)") : qsTr("Price back")
                            color: "white"
                            font.pixelSize: 1.2 * sheet.u
                        }
                        TextField {
                            id: priceField
                            objectName: "returnPrice"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3.4 * sheet.u
                            font.pixelSize: 1.5 * sheet.u
                            horizontalAlignment: TextInput.AlignRight
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            validator: RegularExpressionValidator { regularExpression: /\$?\d{0,6}(\.\d{0,2})?/ }
                            color: sheet.cents(text) > 0 ? "black" : "#b91c1c"
                            onAccepted: qtyField.forceActiveFocus()
                        }
                        Text {
                            text: qsTr("How many")
                            color: "white"
                            font.pixelSize: 1.2 * sheet.u
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: sheet.u / 3
                            WidgetKey {
                                objectName: "returnQtyLess"
                                Layout.preferredWidth: 3.4 * sheet.u
                                Layout.preferredHeight: 3.4 * sheet.u
                                text: "−"
                                fontScale: 0.45
                                takesTouch: true
                                onClicked: qtyField.text = String(Math.max(1, (parseInt(qtyField.text, 10) || 1) - 1))
                            }
                            TextField {
                                id: qtyField
                                objectName: "returnQty"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 3.4 * sheet.u
                                font.pixelSize: 1.5 * sheet.u
                                horizontalAlignment: TextInput.AlignHCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: 99 }
                                onAccepted: sheet.add()
                            }
                            WidgetKey {
                                objectName: "returnQtyMore"
                                Layout.preferredWidth: 3.4 * sheet.u
                                Layout.preferredHeight: 3.4 * sheet.u
                                text: "+"
                                fontScale: 0.45
                                takesTouch: true
                                onClicked: qtyField.text = String(Math.min(99, (parseInt(qtyField.text, 10) || 0) + 1))
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Why?")
                        color: "white"
                        font.pixelSize: 1.2 * sheet.u
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: sheet.u / 2
                        rowSpacing: sheet.u / 2
                        Repeater {
                            model: sheet.info.reasons ?? []
                            delegate: WidgetKey {
                                required property string modelData
                                required property int index
                                objectName: "returnReason-" + index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 3 * sheet.u
                                text: modelData
                                fontScale: 0.28
                                accent: sheet.reason === modelData
                                takesTouch: true
                                onClicked: sheet.reason = modelData
                            }
                        }
                    }
                    WidgetKey {
                        objectName: "returnAdd"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4 * sheet.u
                        readonly property int backCents: sheet.cents(priceField.text)
                        readonly property int backQty: parseInt(qtyField.text, 10) || 0
                        text: sheet.reason === "" ? qsTr("Touch a reason")
                              : backCents <= 0 ? qsTr("Type the price")
                              : qsTr("Add return  −%1").arg((backCents * Math.max(1, backQty) / 100).toFixed(2))
                        fontScale: 0.3
                        baseColor: sheet.reason !== "" && backCents > 0 && backQty >= 1 ? "#b91c1c" : "#4b5563"
                        takesTouch: true
                        onClicked: sheet.add()
                    }
                }

                WidgetKey {
                    objectName: "returnCancel"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 3.2 * sheet.u
                    text: sheet.step === "choose" ? qsTr("Cancel") : qsTr("Cancel (nothing added)")
                    fontScale: 0.28
                    baseColor: "#6b2a2a"
                    takesTouch: true
                    onClicked: sheet.pos.cancelItemReturn()
                }
            }
        }
    }
}
