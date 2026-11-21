import QtQuick

// The menu's categories as buttons, laid out by themselves, in the Menu
// Builder's order and colors: touch one for its items (the menu page on that
// category). New categories show up with no page editing. props.period: the
// meal period's categories (empty: this page's, else what's on now);
// props.columns. props.bar: the bar menu instead - the categories with drinks (or that print to the bar),
// at every meal, each split into its product groups (Bourbon, Scotch...) when it has two or more.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property int columns: zone && zone.props && zone.props.columns > 0 ? zone.props.columns : 3
    readonly property string period: zone && zone.props && zone.props.period ? zone.props.period : ""
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    // With items, and on the menu at this meal (or all day).
    readonly property bool bar: zone && zone.props && zone.props.bar === true
    readonly property var cats: (pos ? pos.menuCategories : []).filter(c =>
        c.count > 0 && !c.retailOnly && (bar ? c.bar === true
                                         : period !== "" ? (c.periods.length === 0 || c.periods.includes(period)) : c.now))
    // One tile a category; the bar's: one a product group where a category has several ({group} set).
    readonly property var shown: {
        if (!bar)
            return cats
        const out = []
        for (const c of cats) {
            const groups = c.barGroups ?? []
            if (groups.length === 0) {
                out.push(c)
                continue
            }
            for (const g of groups)
                out.push({ id: c.id, name: g.name !== "" ? g.name : c.name, color: c.color, group: g.name })
        }
        return out
    }
    // The bar's tiles without a color of their own (Menu Builder): each its own, from these.
    readonly property var tileColors: ["#8a4b16", "#1f6b40", "#2b62b0", "#8b2f5a", "#6b46c1", "#a8761a", "#2f7f86", "#b83232"]
    readonly property real gap: 16
    readonly property int rows: Math.max(1, Math.ceil(shown.length / columns))
    // Dark text on light buttons.
    function inkOn(c) {
        const k = Qt.color(c)
        return 0.299 * k.r + 0.587 * k.g + 0.114 * k.b > 0.6 ? "#14171c" : "white"
    }

    Grid {
        anchors.fill: parent
        columns: w.columns
        spacing: w.gap
        Repeater {
            model: w.shown
            delegate: WidgetKey {
                required property var modelData
                required property int index
                objectName: "category-" + modelData.id + (modelData.group !== undefined ? "-" + modelData.group : "")
                width: (w.width - w.gap * (w.columns - 1)) / w.columns
                // As tall as fits, up to a comfortable size.
                height: Math.min(260, (w.height - w.gap * (w.rows - 1)) / w.rows)
                text: qsTranslate("Page", modelData.name)
                baseColor: modelData.color || (w.bar ? w.tileColors[index % w.tileColors.length] : (w.zone.st.keyFill ?? "#343c49"))
                textColor: modelData.color ? w.inkOn(modelData.color) : (w.bar ? "white" : (w.zone.st.keyTextColor ?? "white"))
                fontScale: 0.2
                onClicked: modelData.group !== undefined ? w.zone.controller.openCategoryGroup(modelData.id, modelData.group)
                                                         : w.zone.controller.openCategory(modelData.id)
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: w.shown.length === 0
        text: w.bar ? qsTr("No drinks on the menu yet: import the drink list (Menu Builder).")
                    : qsTr("Nothing on the menu yet: Manager → Menu Builder.")
        color: "#8a94a6"
        font.family: w.face
        font.pixelSize: 28
    }
}
