import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Menu Builder -> an item's card -> Price check: the price on the card with the tax of the item's tax
// class (Store Settings -> Taxes), and the way back. Prices include tax (a bar): what's in the price
// before tax, the price that nets an amount, the price rounded. Tax on top (retail): what the guest
// pays, and the price that makes it a round amount. Retail with a cash discount: card and cash
// prices, and a carton at 10 cash packs. Only a calculator: worked out by pos.priceFigures (whole
// cents, on this screen or a tablet); checks are priced and paid as always.
Rectangle {
    id: check
    property Item menu   // the WidgetMenuBuilder (not by its type: that would be a cycle)
    readonly property var pos: menu ? menu.pos : null
    readonly property var draft: menu ? menu.draft : ({})
    readonly property bool retail: draft.isRetail === true
    // 0 (none), 25, 50 or 100 cents.
    property int roundCents: 0
    property string target: ""
    readonly property var pricing: pos ? pos.pricing : ({})
    readonly property string storeCash: pricing.cashDiscountPercent !== undefined && Number(pricing.cashDiscountPercent) > 0
                                        ? String(Number(pricing.cashDiscountPercent)) : ""
    readonly property var taxNames: ({ food: qsTr("Food"), alcohol: qsTr("Alcohol"), merchandise: qsTr("Merchandise"),
                                       room: qsTr("Room") })
    readonly property var f: {
        if (!pos || !pricing)   // read again when Store Settings -> Taxes changes
            return ({})
        return pos ? pos.priceFigures(String(draft.price ?? ""), draft.taxClass || "food", String(draft.cost ?? ""),
                                      target, roundCents, retail, cashField.text) : ({})
    }
    function use(price) { if (menu && price) menu.set("price", price) }

    objectName: "builderPriceCheck"
    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 20
    color: "#202630"
    radius: 6
    border.color: "#353e4d"

    component Line: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: 15
    }
    component Small: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: 12
        opacity: 0.7
    }
    component UseButton: MenuTouchButton {
        implicitHeight: 38
        font.pixelSize: 13
        text: qsTr("Use")
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8

        Line {
            objectName: "priceCheckRate"
            font.bold: true
            text: !check.f.hasTax ? qsTr("No tax on this item.")
                : (check.f.included ? qsTr("%1 tax %2 · prices include tax") : qsTr("%1 tax %2 · added on top"))
                  .arg(check.taxNames[check.draft.taxClass || "food"] ?? "").arg(check.f.rate ?? "")
        }
        Small {
            visible: check.f.valid !== true
            text: qsTr("Type a price to see it with tax.")
        }

        // --- Prices include tax (a bar): the price is what the guest pays ---
        ColumnLayout {
            visible: check.f.valid === true && check.f.included === true
            Layout.fillWidth: true
            spacing: 6
            Line {
                objectName: "priceCheckIncluded"
                text: qsTr("Guest pays %1 = %2 before tax + %3 tax").arg(check.f.guestPays ?? "")
                      .arg(check.f.preTax ?? "").arg(check.f.tax ?? "")
            }
            Small {
                text: qsTr("One item on its own. A check backs the tax out of its whole total, so several items together can differ by a cent.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("To net") }
                TextField {
                    objectName: "priceCheckNet"
                    Layout.preferredWidth: 90
                    placeholderText: "0.00"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    text: check.target
                    onTextEdited: check.target = text
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: check.f.chargeForNet ? qsTr("before tax, charge %1").arg(check.f.chargeForNet) : qsTr("before tax, charge…")
                }
                UseButton {
                    visible: !!check.f.chargeForNetPlain
                    onClicked: check.use(check.f.chargeForNetPlain)
                }
            }
        }

        // --- Tax on top (retail): the guest pays the price and the tax ---
        ColumnLayout {
            visible: check.f.valid === true && check.f.included !== true
            Layout.fillWidth: true
            spacing: 6
            Line {
                objectName: "priceCheckAdded"
                text: qsTr("%1 + %2 tax = guest pays %3").arg(check.f.preTax ?? "").arg(check.f.tax ?? "")
                      .arg(check.f.guestPays ?? "")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Guest pays") }
                TextField {
                    objectName: "priceCheckGuest"
                    Layout.preferredWidth: 90
                    placeholderText: "14.00"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    text: check.target
                    onTextEdited: check.target = text
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: !check.f.priceForGuest ? qsTr("→ price it at…")
                        : check.f.guestExact ? qsTr("%1 → price it at %2").arg(check.f.guestTarget).arg(check.f.priceForGuest)
                        : qsTr("%1 → price it at %2 (comes to %3: no price gives it exactly)")
                              .arg(check.f.guestTarget).arg(check.f.priceForGuest).arg(check.f.guestGets)
                }
                UseButton {
                    visible: !!check.f.priceForGuestPlain
                    onClicked: check.use(check.f.priceForGuestPlain)
                }
            }
        }

        // Round to: the guest price (prices include tax), or the guest total to aim for (tax on top).
        RowLayout {
            visible: check.f.valid === true
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Round to") }
            Repeater {
                model: [{ c: 0, t: qsTr("None") }, { c: 25, t: "$0.25" }, { c: 50, t: "$0.50" }, { c: 100, t: "$1" }]
                MenuTouchButton {
                    required property var modelData
                    objectName: "priceCheckRound-" + modelData.c
                    implicitHeight: 38
                    font.pixelSize: 13
                    text: modelData.t
                    highlighted: check.roundCents === modelData.c
                    onClicked: check.roundCents = modelData.c
                }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            visible: check.f.valid === true && !!check.f.rounded
            Layout.fillWidth: true
            Line {
                objectName: "priceCheckRounded"
                text: qsTr("Rounded: guest pays %1 (%2 before tax + %3 tax)").arg(check.f.rounded ?? "")
                      .arg(check.f.roundedPreTax ?? "").arg(check.f.roundedTax ?? "")
            }
            UseButton {
                visible: !!check.f.roundedPlain
                onClicked: check.use(check.f.roundedPlain)
            }
        }

        Line {
            visible: check.f.valid === true && !!check.f.margin && check.f.showCash !== true
            objectName: "priceCheckMargin"
            text: qsTr("Margin before tax: %1 (profit %2)").arg(check.f.margin ?? "").arg(check.f.profit ?? "")
        }

        // --- Retail: cash vs card (cigarettes) ---
        RowLayout {
            visible: check.retail
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Cash discount %") }
            TextField {
                id: cashField
                objectName: "priceCheckCash"
                Layout.preferredWidth: 70
                placeholderText: "0"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                text: check.storeCash
                // The store's setting (every screen): saved when the field is left.
                onEditingFinished: {
                    const v = Number(text.trim() === "" ? 0 : text.replace("%", ""))
                    if (check.pos && isFinite(v) && v >= 0 && v < 100 && v !== Number(check.pricing.cashDiscountPercent ?? 0))
                        check.pos.saveCashDiscount(v)
                }
            }
            Small {
                text: qsTr("Off the card price, on packs. The store's setting (also in Store Settings → Taxes). 0: off.")
            }
        }
        GridLayout {
            visible: check.f.valid === true && check.f.showCash === true
            objectName: "priceCheckCashTable"
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 14
            rowSpacing: 4
            Label { text: "" }
            Label { text: qsTr("Price"); font.bold: true }
            Label { text: check.f.included ? qsTr("Guest pays") : qsTr("With tax"); font.bold: true }
            Label { text: qsTr("Margin"); font.bold: true; visible: !!check.f.marginCard }
            Label { visible: !check.f.marginCard; text: "" }

            Label { text: qsTr("Card (pack)") }
            Label { text: check.f.card ?? "" }
            Label { text: check.f.cardWithTax ?? "" }
            Label { text: check.f.marginCard ?? "" }

            Label { text: qsTr("Cash (pack, %1 off)").arg(check.f.cashPercent ?? "") }
            Label { text: check.f.cash ?? ""; color: "#7ee2a8" }
            Label { text: check.f.cashWithTax ?? ""; color: "#7ee2a8" }
            Label { text: check.f.marginCash ?? "" }

            Label { text: qsTr("Carton (10 cash packs, cash or card)") }
            Label { text: check.f.carton ?? "" }
            Label { text: check.f.cartonWithTax ?? "" }
            Label { text: "" }
        }
        Small {
            visible: check.f.valid === true && check.f.showCash === true
            text: qsTr("The price on the card is the card price. Margins are before tax. Only a calculator: checks are rung up as always.")
        }
    }
}
