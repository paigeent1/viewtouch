import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Menu Builder -> a retail item's card -> On hand (owner 2026-11-27): the retail on hand kept here
// (pos.retailStockCard, app/pos_retail_stock.cpp). A box / carton and its single (Parent Master UPC) are one
// family with two counts: sealed boxes / cartons and loose singles, shown "4 box 27 ea" on both items. Count:
// sealed + loose typed apart (nothing is turned into the other). Open boxes: sealed into loose, for stocking the
// humidor. A single sold opens a sealed one by itself only when none is loose. Managers.
Rectangle {
    id: card
    property Item menu   // the WidgetMenuBuilder
    readonly property var pos: menu ? menu.pos : null
    readonly property string itemId: menu && menu.draft ? String(menu.draft.id ?? "") : ""
    readonly property var info: pos ? (pos.retailStockCard ?? ({})) : ({})
    readonly property bool mine: info.id !== undefined && info.id === itemId
    readonly property bool family: mine && info.family === true

    objectName: "builderStock"
    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 20
    color: "#202630"
    radius: 6
    border.color: "#353e4d"

    // The state comes to a tablet only while a card shows it (PosServer: on request).
    Item {
        visible: false
        readonly property var p: card.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("retailStockCard", false)
            asked = p
            if (p) p.want("retailStockCard", true)
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) asked.want("retailStockCard", false)
    }
    onItemIdChanged: if (pos && itemId !== "") pos.retailStockShow(itemId)
    Component.onCompleted: if (pos && itemId !== "") pos.retailStockShow(itemId)
    // The count's fields follow the on hand (a sale, a count, Open), except while one is being typed in.
    onInfoChanged: if (mine && !sealedField.activeFocus && !looseField.activeFocus) {
        sealedField.text = numberText(info.sealed)
        looseField.text = numberText(info.loose)
    }

    function numberText(v) { return v === undefined ? "" : String(Math.round(Number(v) * 1000) / 1000) }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Label {
            objectName: "builderStockText"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 18
            font.bold: true
            text: !card.mine ? qsTr("Save the item first.")
                  : (card.info.text ?? "") !== "" ? qsTr("On hand: %1  ·  %2 at cost").arg(card.info.text).arg(card.info.value ?? "")
                  : qsTr("No on hand kept yet: count it, or receive it.")
        }
        Label {
            visible: card.family
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.75
            font.pixelSize: 13
            text: qsTr("%1 and %2 share one count: sealed %3 and loose %4 (%5 %4 in each %3). A %4 sold with none loose opens a %3 by itself.")
                  .arg(card.info.parentName ?? "").arg(card.info.singleName ?? "").arg(card.info.parentUnit ?? "")
                  .arg(card.info.singleUnit ?? "").arg(card.info.packSize ?? "")
        }
        RowLayout {
            visible: card.mine
            spacing: 8
            Label { text: card.family ? qsTr("Count: sealed") : qsTr("Count:") }
            TextField {
                id: sealedField
                objectName: "builderStockSealed"
                Layout.preferredWidth: 80
                implicitHeight: 52
                horizontalAlignment: Text.AlignHCenter
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onActiveFocusChanged: if (activeFocus) selectAll()
            }
            Label { visible: card.family; text: (card.info.parentUnit ?? "") + "   " + qsTr("loose") }
            TextField {
                id: looseField
                objectName: "builderStockLoose"
                visible: card.family
                Layout.preferredWidth: 80
                implicitHeight: 52
                horizontalAlignment: Text.AlignHCenter
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onActiveFocusChanged: if (activeFocus) selectAll()
            }
            Label { visible: card.family; text: card.info.singleUnit ?? "" }
            MenuTouchButton {
                objectName: "builderStockCount"
                implicitHeight: 52
                text: qsTr("Save count")
                onClicked: card.pos.retailSetCount(card.itemId, Number(sealedField.text) || 0, card.family ? (Number(looseField.text) || 0) : 0)
            }
        }
        RowLayout {
            visible: card.family
            spacing: 8
            MenuTouchButton {
                objectName: "builderStockOpenLess"
                implicitHeight: 52
                implicitWidth: 52
                text: "−"
                onClicked: openCount.value = Math.max(1, openCount.value - 1)
            }
            Label {
                id: openCount
                property int value: 1
                text: String(value)
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 30
            }
            MenuTouchButton {
                implicitHeight: 52
                implicitWidth: 52
                text: "+"
                onClicked: openCount.value = openCount.value + 1
            }
            MenuTouchButton {
                objectName: "builderStockOpen"
                implicitHeight: 52
                text: qsTr("Open %1 %2 (into loose)").arg(openCount.value).arg(card.info.parentUnit ?? "")
                onClicked: card.pos.retailOpenParents(card.itemId, openCount.value)
            }
        }
        Label {
            visible: card.mine && (card.info.message ?? "") !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#7ee2a8"
            text: card.info.message ?? ""
        }
    }
}
