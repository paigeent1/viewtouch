import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The menu, laid out by itself: a button for every item of a family (or of
// every family, with a chip for each across the top). New items, prices and
// sold-out items show up with no page editing. props.family: one family, or
// empty for all; props.columns; props.photos: show item photos;
// props.search: the items whose name has every word typed (the Find page; the main computer finds
// them, as Item Look Up does, and they show as bars, best first);
// props.popular: today's best sellers, most sold first (the Popular page).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    // Find's answer comes to a tablet only while a screen shows it (PosServer: on request); the
    // whole item list (a 30,000-item price book) no longer has to.
    Item {
        visible: false
        readonly property var p: w.search ? w.pos : null
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("itemSearch", false)
            asked = p
            if (p) p.want("itemSearch", true)
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) asked.want("itemSearch", false)
    }
    // Typing: found a moment after the last key; the best 50 (More… for the next 50).
    property int findLimit: 50
    readonly property bool foundMore: search && typed !== "" && (found.count ?? 0) > shown.length
    function findMore() {
        findLimit = shown.length + 50
        if (pos) pos.findItems(typed, findLimit)
    }
    Timer {
        id: findTyping
        interval: 150
        onTriggered: if (w.pos && w.search) w.pos.findItems(w.typed, w.findLimit)
    }
    onTypedChanged: {
        if (!search) return
        findLimit = 50
        findTyping.restart()
        if (typed === "") refreshFound()      // cleared: no bars right away
    }
    Component.onCompleted: {
        if (search && pos) pos.findItems(typed, findLimit)
        refreshFound()
    }
    readonly property var found: pos && search ? pos.itemSearch : ({})
    // The bars: made again only when the answer is a new one (found.rev), so paging isn't lost when
    // something else changes; none while nothing is typed.
    property var foundItems: []
    property var foundRev: undefined
    function refreshFound() {
        const r = found.rev
        if (typed !== "" && r !== undefined && r === foundRev) return
        foundRev = typed === "" ? undefined : r
        foundItems = typed === "" ? [] : (found.items ?? [])
    }
    onFoundChanged: refreshFound()
    readonly property string fixedFamily: zone && zone.props && zone.props.family ? zone.props.family : ""
    // Columns: set by the page (0: as many as fit; see MenuButtons).
    readonly property int setColumns: zone && zone.props && zone.props.columns > 0 ? zone.props.columns : 0
    readonly property bool pagePhotos: zone && zone.props && zone.props.photos === true
    readonly property bool search: zone && zone.props && zone.props.search === true
    readonly property bool popular: zone && zone.props && zone.props.popular === true
    readonly property string typed: pos && search ? pos.textEntry.trim().toLowerCase() : ""
    readonly property string face: zone.st.font ?? "DejaVu Sans"

    // The order screens: the menu less the retail-only categories (a price book stays on the main
    // computer); a search (Find Item) is done there, through every item (itemSearch).
    readonly property var allItems: pos && !search ? pos.orderItems : []
    readonly property var items: allItems.filter(i => !i.modifier)
    // The menu's categories with items, in their order (Menu Builder).
    readonly property var categories: pos ? pos.menuCategories.filter(c => c.count > 0 && (search || !c.retailOnly)) : []
    // The guest's allergies (the check's): items that contain one are marked.
    readonly property var allergies: pos && pos.hasCheck ? (pos.check.allergies ?? []) : []
    // ★ Favorites first: the items marked so, then today's best sellers (12 in all).
    readonly property var favorites: {
        const out = items.filter(i => i.favorite)
        for (const id of (pos ? pos.popularItems : []))
            if (out.length < 12 && !out.some(i => i.id === id)) {
                const i = items.find(x => x.id === id)
                if (i) out.push(i)
            }
        return out.slice(0, Math.max(12, items.filter(i => i.favorite).length))
    }
    readonly property var families: (favorites.length ? ["★"] : []).concat(categories.map(c => c.id))
    // The category being shown: the one touched on the menu screen.
    readonly property string chosen: zone && zone.controller ? zone.controller.menuCategory : ""
    readonly property string family: fixedFamily !== "" ? fixedFamily
                                    : families.includes(chosen) ? chosen
                                    : ((categories.find(c => c.now) ?? categories[0] ?? {}).id ?? "")
    function categoryOf(id) { return categories.find(c => c.id === id) ?? ({}) }
    // The category shown: its own button size, photos, prices (Menu Builder).
    readonly property var shownCategory: !search && !popular && family !== "★" ? categoryOf(family) : ({})
    // Searching: every family, best matches first (found by the main computer: foundItems).
    readonly property var shown: {
        if (search)
            return foundItems
        if (popular)
            return (pos ? pos.popularItems : []).map(id => items.find(i => i.id === id)).filter(i => i !== undefined)
        return family === "★" ? favorites : items.filter(i => i.family === family)
    }
    function title(f) {
        if (f === "★")
            return qsTr("★ Favorites")
        const name = categoryOf(f).name ?? ""
        return f === "" ? qsTr("Other") : qsTranslate("Page", name !== "" ? name : f.charAt(0).toUpperCase() + f.slice(1))
    }

    readonly property real gap: 12
    // Find's rows: about six show at once, never shorter than a finger (48 screen pixels) nor taller
    // than 96; the words sized from the row, so a name stays readable.
    readonly property real rowH: {
        const fit = (height - gap * 3) / 6
        const finger = zone ? zone.touch(48) : 48
        return Math.max(finger, Math.min(96, fit))
    }
    readonly property real nameSize: Math.max(14, Math.min(28, rowH * 0.4))
    readonly property real detailSize: Math.max(11, Math.min(19, rowH * 0.26))
    readonly property real priceSize: Math.max(14, Math.min(30, rowH * 0.44))
    // Arranging (managers): touch an item, then move it or color it.
    // Kept by the controller: a menu change rebuilds the page.
    property bool arranging: zone && zone.controller ? zone.controller.widgetState(zone.zoneId + ".arranging") === true : false
    property string picked: zone && zone.controller ? (zone.controller.widgetState(zone.zoneId + ".picked") ?? "") : ""
    function setArranging(on) {
        arranging = on
        picked = ""
        zone.controller.setWidgetState(zone.zoneId + ".arranging", on)
        zone.controller.setWidgetState(zone.zoneId + ".picked", "")
    }
    function pick(id) {
        picked = id
        zone.controller.setWidgetState(zone.zoneId + ".picked", id)
    }
    readonly property bool mayArrange: pos !== null && pos.loggedIn && pos.can("manager") && !search && !popular

    ColumnLayout {
        anchors.fill: parent
        spacing: w.gap

        // Families across the top (only when showing them all).
        Flow {
            Layout.fillWidth: true
            visible: !w.search && !w.popular && w.fixedFamily === "" && w.families.length > 1
            spacing: w.gap * 0.6
            Repeater {
                model: w.families
                delegate: WidgetKey {
                    required property string modelData
                    objectName: "menuFamily-" + modelData
                    width: Math.max(160, (w.width - w.gap * 0.6 * 5) / 6)
                    height: Math.max(56, w.zone ? w.zone.touch(52) : 56)
                    text: w.title(modelData)
                    accent: modelData === w.family
                    fontScale: 0.38
                    onClicked: w.zone.controller.menuCategory = modelData
                }
            }
            WidgetKey {
                objectName: "menuArrange"
                visible: w.mayArrange && !w.arranging
                width: Math.max(160, (w.width - w.gap * 0.6 * 5) / 6)
                height: Math.max(56, w.zone ? w.zone.touch(52) : 56)
                text: qsTr("Arrange…")
                fontScale: 0.38
                onClicked: w.setArranging(true)
            }
        }

        // Find: one row per item (name; its UPC and number under it; the price on the right), best first,
        // about six to a screen, with ▲ PG UP / ▼ PG DN on the right to page through them.
        RowLayout {
            objectName: "findBars"
            visible: w.search && w.shown.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: w.gap
            ListView {
                id: bars
                objectName: "findList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: w.gap * 0.5
                boundsBehavior: Flickable.StopAtBounds
                model: w.search ? w.shown : []
                readonly property var allergenNames: w.pos ? w.pos.allergenList() : []
                function allergenName(id) { return (allergenNames.find(a => a.id === id) ?? { name: id }).name }
                // One page: as many rows as show.
                function page(by) {
                    const step = Math.max(1, Math.floor((height + spacing) / (w.rowH + spacing)))
                    const top = indexAt(10, contentY + 10)
                    positionViewAtIndex(Math.max(0, Math.min(count - 1, (top < 0 ? 0 : top) + by * step)), ListView.Beginning)
                }
                // More than shown: the next 50.
                footer: Item {
                    width: bars.width
                    height: w.foundMore ? w.rowH + w.gap * 0.5 : 0
                    WidgetKey {
                        objectName: "findMore"
                        visible: w.foundMore
                        anchors.fill: parent
                        anchors.topMargin: w.gap * 0.5
                        text: qsTr("More… (%1 of %2 shown)").arg(w.shown.length).arg(w.found.count ?? 0)
                        fontScale: 0.3
                        onClicked: w.findMore()
                    }
                }
                delegate: Rectangle {
                    id: bar
                    required property var modelData
                    objectName: "menuItem-" + modelData.id
                    width: ListView.view.width
                    height: w.rowH
                    radius: 8
                    readonly property var hits: (modelData.allergens ?? []).filter(a => w.allergies.includes(a))
                    color: !modelData.available ? "#3a3f48" : "#1f2937"
                    border.color: hits.length > 0 ? "#f59e0b" : "#475569"
                    border.width: hits.length > 0 ? 3 : 1
                    opacity: modelData.available ? 1 : 0.7
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 14
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                objectName: "findName-" + bar.modelData.id
                                Layout.fillWidth: true
                                text: qsTranslate("Page", bar.modelData.name)
                                font.family: w.face
                                font.pixelSize: w.nameSize; font.bold: true
                                color: "white"
                                elide: Text.ElideRight
                            }
                            Text {
                                objectName: "allergyMark-" + bar.modelData.id
                                visible: bar.hits.length > 0
                                Layout.fillWidth: true
                                text: "⚠ " + bar.hits.map(a => bars.allergenName(a)).join(", ")
                                font.pixelSize: w.detailSize; font.bold: true
                                color: "#fbbf24"
                                elide: Text.ElideRight
                            }
                            // Its UPC and number (no allergy line there instead).
                            Text {
                                objectName: "findDetail-" + bar.modelData.id
                                Layout.fillWidth: true
                                visible: bar.hits.length === 0 && text !== ""
                                text: [bar.modelData.upc ? "UPC " + bar.modelData.upc : "",
                                       bar.modelData.number ? "#" + bar.modelData.number : ""].filter(x => x).join("    ")
                                font.family: w.face
                                font.pixelSize: w.detailSize
                                color: "#cbd5e1"
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            objectName: "menuPrice-" + bar.modelData.id
                            text: !bar.modelData.available ? qsTr("SOLD OUT")
                                : bar.modelData.price + (bar.modelData.byWeight ? " / " + bar.modelData.unit : "")
                            font.family: w.face
                            font.pixelSize: w.priceSize; font.bold: true
                            color: !bar.modelData.available ? "#ff6b6b" : "#fde047"
                        }
                    }
                    TapHandler {
                        enabled: bar.modelData.available
                        onTapped: w.zone.controller.orderItem(bar.modelData.id, true)
                    }
                }
            }
            PageArrows {
                upName: "findPageUp"
                downName: "findPageDown"
                keyWidth: Math.max(110, Math.min(w.zone ? w.zone.touch(90) : 130, w.width * 0.14))
                onUp: bars.page(-1)
                onDown: bars.page(1)
            }
        }

        MenuButtons {
            id: grid
            visible: !w.search
            Layout.fillWidth: true
            Layout.fillHeight: true
            items: w.search ? [] : w.shown   // Find shows bars instead
            category: w.shownCategory
            categories: w.categories
            pos: w.pos
            setColumns: w.setColumns
            pagePhotos: w.pagePhotos
            // Headings and spaces: one category, as arranged in the Menu Builder.
            sectioned: !w.search && !w.popular && w.family !== "★"
            st: w.zone ? w.zone.st : ({})
            face: w.face
            gap: w.gap
            arranging: w.arranging
            picked: w.picked
            managing: w.mayArrange
            allergies: w.allergies
            onItemTapped: item => {
                if (w.arranging) w.pick(item.id)
                else if (item.available) w.zone.controller.orderItem(item.id, w.search)
            }
            // A manager holds one: changed right here.
            onItemHeld: item => quickCard.edit(item)
        }
        // Arranging: move the picked item, color it, Done.
        RowLayout {
            objectName: "arrangeBar"
            visible: w.arranging
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: w.gap * 0.5
            readonly property real keyH: Math.max(56, w.zone ? w.zone.touch(52) : 56)
            WidgetKey {
                objectName: "arrangeEarlier"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 150; Layout.preferredHeight: parent.keyH
                text: qsTr("◀ Earlier")
                fontScale: 0.34
                onClicked: w.pos.moveMenuItem(w.picked, -1)
            }
            WidgetKey {
                objectName: "arrangeLater"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 150; Layout.preferredHeight: parent.keyH
                text: qsTr("Later ▶")
                fontScale: 0.34
                onClicked: w.pos.moveMenuItem(w.picked, 1)
            }
            // Its color: the store's colors, its own, or one mixed.
            WidgetKey {
                objectName: "arrangeColor"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 170; Layout.preferredHeight: parent.keyH
                readonly property var item: w.allItems.find(i => i.id === w.picked) ?? null
                text: "🎨 " + qsTr("Color…")
                baseColor: item && item.buttonColor ? item.buttonColor : "#343c49"
                fontScale: 0.32
                onClicked: colorPopup.open()
            }
            WidgetKey {
                objectName: "arrangeEdit"
                enabled: w.picked !== ""
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 140; Layout.preferredHeight: parent.keyH
                text: qsTr("Edit…")
                fontScale: 0.32
                onClicked: {
                    const i = w.items.find(x => x.id === w.picked)
                    if (i) quickCard.edit(i)
                }
            }
            // A new one, in the category shown.
            WidgetKey {
                objectName: "arrangeAdd"
                visible: w.family !== "" && w.family !== "★"
                Layout.preferredWidth: 170; Layout.preferredHeight: parent.keyH
                text: qsTr("+ New Item")
                fontScale: 0.32
                onClicked: quickCard.add(w.family)
            }
            Item { Layout.fillWidth: true }
            WidgetKey {
                objectName: "arrangeDone"
                Layout.preferredWidth: 150; Layout.preferredHeight: parent.keyH
                text: qsTr("Done")
                baseColor: "#1f6b40"
                fontScale: 0.34
                onClicked: w.setArranging(false)
            }
        }
        Text {
            visible: w.shown.length === 0
            Layout.alignment: Qt.AlignHCenter
            text: !w.pos || !w.pos.loggedIn ? ""
                : w.popular ? qsTr("Today's best sellers show up here as orders come in.")
                : w.search ? (w.typed === "" ? qsTr("Type part of a name: \"cob\" finds Cobb.")
                              : String(w.found.query ?? "").toLowerCase() !== w.typed ? ""   // still looking
                              : qsTr("Nothing on the menu has \"%1\".").arg(w.typed))
                : qsTr("Nothing on the menu here yet (Manager -> Menu).")
            color: "#8a94a6"
            font.pixelSize: 24
        }
    }

    Popup {
        id: colorPopup
        objectName: "arrangeColorPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 600, 600)
        modal: true
        padding: 16
        contentItem: ColorPicker {
            pos: w.pos
            noneText: qsTr("No color")
            color: {
                const i = w.allItems.find(x => x.id === w.picked) ?? null
                return i ? i.buttonColor : ""
            }
            onPicked: c => {
                w.pos.setMenuItemColor(w.picked, c)
                colorPopup.close()
            }
        }
    }

    QuickItemCard {
        id: quickCard
        pos: w.pos
        controller: w.zone ? w.zone.controller : null
    }
}
