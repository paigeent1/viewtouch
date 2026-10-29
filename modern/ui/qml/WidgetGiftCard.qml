import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Gift cards: type or swipe the number to see the balance and history,
// sell or reload one on the check (live once the check is paid), or pay
// the check from it.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var card: pos ? pos.giftCard : ({})
    readonly property bool found: card.found ?? false
    property bool askingCustomer: false
    property int amountCents: 0

    function lookUp() {
        if (number.text.length > 0)
            pos.lookupGiftCard(number.text)
    }

    // Upright (a phone): the card's controls, then its history below.
    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 380 : 1100) : 1
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        GridLayout {
            anchors.fill: parent
            anchors.margins: 12
            columns: w.narrow ? 1 : 3   // the controls | a separator | the history
            rowSpacing: 18
            columnSpacing: 18

            ColumnLayout {
                Layout.preferredWidth: w.narrow ? parent.width : parent.width * 0.5
                Layout.fillWidth: w.narrow
                Layout.fillHeight: !w.narrow
                spacing: 10

                Label { text: qsTr("Search Card #, Purchased By, or Purchased For…"); opacity: 0.8 }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        implicitHeight: 48
                        id: number
                        Layout.fillWidth: true
                        font.pixelSize: 22
                        placeholderText: qsTr("Type or swipe…")
                        onTextEdited: w.pos.lookupGiftCard(text.trim())
                        focus: true
                        // A card reader types the number and Enter.
                        onAccepted: w.lookUp()
                    }
                    Button { text: qsTr("Look Up")
                        font.pixelSize: 17; implicitHeight: 48; onClicked: w.lookUp() }
                }

                Label { text: qsTr("Amount"); opacity: 0.8; Layout.topMargin: 8 }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    Repeater {
                        model: [2500, 5000, 7500, 10000]
                        delegate: Button {
                            required property int modelData
                            Layout.fillWidth: true
                            implicitHeight: 56
                            font.pixelSize: 20
                            text: "$" + modelData / 100
                            checkable: true
                            checked: w.amountCents === modelData && other.text === ""
                            onClicked: { other.text = ""; w.amountCents = modelData }
                        }
                    }
                }
                RowLayout {
                    Label { text: qsTr("Other $") }
                    TextField {
                        implicitHeight: 48
                        id: other
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onTextEdited: {
                            const v = Number(text.replace(/[^0-9.]/g, ""))
                            w.amountCents = isNaN(v) ? 0 : Math.round(v * 100)
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Button {
                    Layout.fillWidth: true
                    implicitHeight: 64
                    font.pixelSize: 18
                    enabled: w.amountCents > 0
                    text: w.amountCents <= 0 ? qsTr("Choose an amount to sell or reload")
                          : (w.found ? qsTr("Reload %1 on the Check") : qsTr("Sell %1 Card on the Check"))
                            .arg("$" + (w.amountCents / 100).toFixed(2))
                    onClicked: {
                        w.pos.sellGiftCard(number.text, w.amountCents)
                        w.amountCents = 0
                        other.text = ""
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    text: qsTr("No number? A new one is made and printed on the receipt. A card works once its check is paid.")
                }
                Button {
                    visible: !w.askingCustomer; Layout.fillWidth: true; implicitHeight: 64; font.pixelSize: 18; highlighted: true
                    enabled: w.found && (w.card.balanceCents ?? 0) > 0 && w.pos !== null && w.pos.hasCheck
                    text: qsTr("Pay the Check from This Card")
                    onClicked: { if ((w.pos.check.customerId ?? "") !== "" || (w.pos.check.customerName ?? "") !== "") w.pos.payWithGiftCard(w.card.number, 0); else w.askingCustomer = true }
                }
                RowLayout {
                    visible: w.askingCustomer; Layout.fillWidth: true; spacing: 8
                    Button { Layout.fillWidth: true; implicitHeight: 64; font.pixelSize: 17; highlighted: true; text: qsTr("Add Customer…"); onClicked: { w.askingCustomer = false; if (w.zone && w.zone.controller) w.zone.controller.jumpTo("customers") } }
                    Button { Layout.fillWidth: true; implicitHeight: 64; font.pixelSize: 17; text: qsTr("Bypass & Pay Now"); onClicked: { w.askingCustomer = false; w.pos.payWithGiftCard(w.card.number, 0) } }
                }
                // State law (California: under $10): a small balance in cash, when the guest asks.
                Button {
                    objectName: "giftCashOut"
                    visible: w.found && (w.card.cashOut ?? false)
                    Layout.fillWidth: true
                    implicitHeight: 56
                    font.pixelSize: 17
                    text: qsTr("Cash Out %1 (under %2)").arg(w.card.balance ?? "").arg(w.card.cashOutBelow ?? "")
                    onClicked: w.pos.cashOutGiftCard()
                }
            }

            ToolSeparator { Layout.fillHeight: true; visible: !w.narrow }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Label {
                    text: !w.card.number ? qsTr("Look up a card to see its balance.")
                         : w.found ? qsTr("Card %1").arg(w.card.display) : qsTr("Card %1 isn't active yet").arg(w.card.display)
                    font.pixelSize: 20
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: w.found
                    text: w.card.balance ?? ""
                    font.pixelSize: 44
                    font.bold: true
                    color: "#7ee2a8"
                }
                Label { visible: w.found; text: qsTr("Since %1  |  PIN: %2  |  For: %3  |  By: %4").arg(w.card.issued ?? "").arg(w.card.pin ?? "----").arg(w.card.issuedTo || "—").arg(w.card.purchasedBy || "—"); opacity: 0.6 }
                Label { visible: (w.card.matches ?? []).length > 1 || (!w.found && (w.card.matches ?? []).length > 0); text: qsTr("Matching Gift Cards (%1) — Tap a card:").arg((w.card.matches ?? []).length); font.bold: true; color: "#90cdf4" }
                ListView { visible: (w.card.matches ?? []).length > 1 || (!w.found && (w.card.matches ?? []).length > 0); Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6; model: w.card.matches ?? []
                    delegate: WidgetKey { required property var modelData; width: ListView.view.width - 12; height: 58; fontScale: 0.32; baseColor: "#2b3340"; text: modelData.number + "   |   PIN: " + (modelData.pin || "----") + "   |   " + modelData.balance + "\n    For: " + (modelData.issuedTo || "—") + "   |   By: " + (modelData.purchasedBy || "—"); onClicked: { number.text = modelData.number; w.pos.lookupGiftCard(modelData.number) } }
                }
                ListView { visible: w.found && (w.card.matches ?? []).length <= 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: w.card.history ?? []
                    ScrollBar.vertical: TouchScrollBar { id: historyBar }
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width - historyBar.room
                        height: 34
                        Label { text: modelData.at; opacity: 0.6; Layout.preferredWidth: 150 }
                        Label { text: modelData.what; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: modelData.amount; font.bold: true }
                    }
                }
            }
        }
    }
}
