import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// ILSA History (Reports > ILSA History): the sales history imported from ILSA (vt-ilsa-history),
// read-only. Daily Z (an ILSA day, or ViewTouch's own checks that day), sales by department, item
// movement (by item, UPC, brand, department), GP by month and by year (as ILSA counted it, and
// without the lines that have no cost), ILSA's own Z beside the imported receipts and ViewTouch's
// days, the days ILSA's Z and its receipts disagree, sales by employee and item (ILSA cashiers tied
// to ViewTouch employees on Cashiers). props.report picks the first report.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property string face: zone.st.font ?? "DejaVu Sans"
    readonly property color ink: zone.st.textColor ?? "white"
    readonly property real unit: Math.max(12, Math.min(24, w.width * 0.02))
    readonly property real keyH: Math.max(w.unit * 2.2, w.zone ? w.zone.touch(46) : 0)

    readonly property var kinds: [
        { id: "z", label: qsTr("Daily Z") }, { id: "depts", label: qsTr("Departments") },
        { id: "items", label: qsTr("Item Movement") }, { id: "gpMonth", label: qsTr("GP by Month") },
        { id: "gpYear", label: qsTr("GP by Year") }, { id: "compare", label: qsTr("ILSA vs ViewTouch") },
        { id: "differences", label: qsTr("ILSA Differences") }, { id: "employees", label: qsTr("By Employee") },
        { id: "cashiers", label: qsTr("Cashiers") }
    ]
    readonly property var groupings: [
        { id: "item", label: qsTr("Item") }, { id: "upc", label: qsTr("UPC") },
        { id: "brand", label: qsTr("Brand") }, { id: "dept", label: qsTr("Department") }
    ]
    readonly property var periods: [
        { id: "yesterday", label: qsTr("Yesterday") }, { id: "month", label: qsTr("This Month") },
        { id: "lastMonth", label: qsTr("Last Month") }, { id: "year", label: qsTr("This Year") },
        { id: "lastYear", label: qsTr("Last Year") }, { id: "all", label: qsTr("All") }
    ]
    property string kind: zone && zone.props && zone.props.report ? zone.props.report : "z"
    property string by: "item"
    // By Employee: an item, UPC, brand or department; with ViewTouch's own sales too.
    property string employeeFilter: ""
    // The ILSA store: "" this site's (Store Settings), BHDAN / BHNM..., "*" every store's.
    property string store: ""
    property bool withViewTouch: true
    // Departments: the one each line was sold in (owner 2026-11-07), or as ILSA's printouts (the
    // items' departments now).
    property string deptBy: "dept"
    readonly property var deptChoices: [
        { id: "dept", label: qsTr("Department when sold") }, { id: "current", label: qsTr("As ILSA: item's current department") }
    ]
    function iso(d) { return Qt.formatDate(d, "yyyy-MM-dd") }
    property string fromDate: iso(new Date(Date.now() - 86400000))
    property string toDate: fromDate
    function setDates(a, b) {
        fromDate = a
        toDate = b
        fromField.text = a
        toField.text = b
        Qt.callLater(refresh)
    }
    function setPeriod(id) {
        const now = new Date()
        const y = now.getFullYear(), m = now.getMonth()
        if (id === "yesterday") { const d = iso(new Date(Date.now() - 86400000)); setDates(d, d) }
        else if (id === "month") setDates(iso(new Date(y, m, 1)), iso(now))
        else if (id === "lastMonth") setDates(iso(new Date(y, m - 1, 1)), iso(new Date(y, m, 0)))
        else if (id === "year") setDates(iso(new Date(y, 0, 1)), iso(now))
        else if (id === "lastYear") setDates(iso(new Date(y - 1, 0, 1)), iso(new Date(y - 1, 11, 31)))
        else setDates("2000-01-01", iso(now))
    }
    function refresh() {
        if (w.pos && w.fromDate !== "")
            w.pos.requestLegacyReport(w.kind, w.fromDate, w.toDate,
                                      w.kind === "depts" ? w.deptBy
                                      : w.kind === "employees" ? (w.withViewTouch ? "vt|" : "|") + w.employeeFilter : w.by,
                                      w.store)
    }
    // Asked a moment later, not while the page is being built (the answer changes rangeReport,
    // which bindings here read: asking from inside a binding update is a binding loop).
    onKindChanged: Qt.callLater(refresh)
    onByChanged: Qt.callLater(refresh)
    onDeptByChanged: Qt.callLater(refresh)
    onWithViewTouchChanged: Qt.callLater(refresh)
    Component.onCompleted: Qt.callLater(refresh)

    readonly property var range: pos ? pos.rangeReport : ({})
    readonly property bool mine: (range.id ?? "") === "ilsa-" + kind
    readonly property var report: mine && range.report ? range.report
                                 : ({ title: mine && range.loading ? qsTr("Reading the ILSA history…") : "", rows: [] })
    readonly property int valueColumns: Math.max(1, (report.columns ?? []).length - 1)
    readonly property real valueWidth: Math.min(w.unit * 6.5, (w.width - w.unit * 12) / valueColumns)
    readonly property var cashiers: mine && w.kind === "cashiers" ? (range.cashiers ?? []) : []
    readonly property var staff: mine && w.kind === "cashiers" ? (range.employees ?? []) : []
    property var picking: null   // the cashier whose employee is being chosen
    // Ties are per ILSA copy; the same cashier (number and name) in the other stores' copies too when on.
    property bool tieAllStores: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.6
        spacing: w.unit * 0.4

        // Which report.
        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            columns: w.kinds.length
            columnSpacing: w.unit * 0.3
            Repeater {
                model: w.kinds
                delegate: WidgetKey {
                    required property var modelData
                    objectName: "ilsaReport-" + modelData.id
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData.label
                    fontScale: 0.34
                    baseColor: w.kind === modelData.id ? "#2f6fd6" : "#343c49"
                    onClicked: w.kind = modelData.id
                }
            }
        }

        // Which stores: one, several or all (Store Settings' ILSA history store first); by store or rolled up.
        StorePicker {
            objectName: "ilsaStores"
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            stores: w.range.stores ?? []
            ticked: w.range.ticked ?? []
            byStore: w.range.byStore ?? true
            showMode: true
            unit: w.unit
            keyH: w.keyH
            ink: w.ink
            face: w.face
            onChosen: function(arg) { w.store = arg; Qt.callLater(w.refresh) }
        }

        // Item movement: by what.
        GridLayout {
            visible: w.kind === "items"
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            columns: w.groupings.length
            columnSpacing: w.unit * 0.3
            Repeater {
                model: w.groupings
                delegate: WidgetKey {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData.label
                    fontScale: 0.34
                    baseColor: w.by === modelData.id ? "#1f8a4c" : "#2b3340"
                    onClicked: w.by = modelData.id
                }
            }
        }

        // By Employee: which items, and ViewTouch's own sales beside ILSA's.
        RowLayout {
            visible: w.kind === "employees"
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            spacing: w.unit * 0.3
            Text { text: qsTr("Item, UPC, brand or department"); color: w.ink; font.family: w.face; font.pixelSize: w.unit * 0.8 }
            TextField {
                id: employeeFilterField
                objectName: "ilsaEmployeeFilter"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: w.zone ? w.zone.touch(48) : 48
                font.pixelSize: w.unit * 0.8
                placeholderText: qsTr("all items")
                onAccepted: { w.employeeFilter = text; Qt.callLater(w.refresh) }
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 4.5
                Layout.fillHeight: true
                text: qsTr("Show")
                fontScale: 0.34
                onClicked: { w.employeeFilter = employeeFilterField.text; Qt.callLater(w.refresh) }
            }
            WidgetKey {
                objectName: "ilsaWithViewTouch"
                Layout.preferredWidth: w.unit * 9
                Layout.fillHeight: true
                text: qsTr("+ ViewTouch sales")
                fontScale: 0.32
                baseColor: w.withViewTouch ? "#1f8a4c" : "#2b3340"
                onClicked: w.withViewTouch = !w.withViewTouch
            }
        }

        // Departments: when sold, or as ILSA.
        GridLayout {
            visible: w.kind === "depts"
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            columns: w.deptChoices.length
            columnSpacing: w.unit * 0.3
            Repeater {
                model: w.deptChoices
                delegate: WidgetKey {
                    required property var modelData
                    objectName: "ilsaDepts-" + modelData.id
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData.label
                    fontScale: 0.32
                    baseColor: w.deptBy === modelData.id ? "#1f8a4c" : "#2b3340"
                    onClicked: w.deptBy = modelData.id
                }
            }
        }

        // Which days: a quick choice, or from / to (business days; Daily Z: one day, or several added up).
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            spacing: w.unit * 0.3
            Repeater {
                model: w.periods
                delegate: WidgetKey {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: modelData.label
                    fontScale: 0.32
                    onClicked: w.setPeriod(modelData.id)
                }
            }
            Text { text: qsTr("From"); color: w.ink; font.family: w.face; font.pixelSize: w.unit * 0.8 }
            TextField {
                id: fromField
                objectName: "ilsaFrom"
                implicitWidth: w.unit * 7
                Layout.fillHeight: true
                Layout.minimumHeight: w.zone ? w.zone.touch(48) : 48
                font.pixelSize: w.unit * 0.8
                text: w.fromDate
                placeholderText: "2026-10-28"
                inputMethodHints: Qt.ImhPreferNumbers
                onTextEdited: { w.fromDate = text; w.toDate = text; toField.text = text }
                onAccepted: w.refresh()
            }
            Text { text: qsTr("to"); color: w.ink; font.family: w.face; font.pixelSize: w.unit * 0.8 }
            TextField {
                id: toField
                objectName: "ilsaTo"
                implicitWidth: w.unit * 7
                Layout.fillHeight: true
                Layout.minimumHeight: w.zone ? w.zone.touch(48) : 48
                font.pixelSize: w.unit * 0.8
                text: w.toDate
                placeholderText: "2026-10-28"
                inputMethodHints: Qt.ImhPreferNumbers
                onTextEdited: w.toDate = text
                onAccepted: w.refresh()
            }
            WidgetKey {
                objectName: "ilsaShow"
                Layout.preferredWidth: w.unit * 4.5
                Layout.fillHeight: true
                text: qsTr("Show")
                fontScale: 0.34
                onClicked: w.refresh()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            spacing: w.unit * 0.3
            Text {
                Layout.fillWidth: true
                text: (w.report.title ?? "") + ((w.range.label ?? "") !== "" && w.mine ? "  ·  " + w.range.label : "")
                color: w.ink
                font.family: w.face
                font.pixelSize: w.unit * 1.2
                font.bold: true
                elide: Text.ElideRight
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 4.5
                Layout.fillHeight: true
                text: qsTr("Print")
                fontScale: 0.36
                onClicked: w.pos.printLegacyReport()
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 4
                Layout.fillHeight: true
                text: "CSV"
                fontScale: 0.36
                onClicked: w.zone.controller.exportReport(w.report, "csv")
            }
            WidgetKey {
                Layout.preferredWidth: w.unit * 4
                Layout.fillHeight: true
                text: "PDF"
                fontScale: 0.36
                onClicked: w.zone.controller.exportReport(w.report, "pdf")
            }
        }

        // Column headings
        RowLayout {
            visible: (w.report.columns ?? []).length > 2
            Layout.fillWidth: true
            Layout.rightMargin: 28
            Layout.fillHeight: false
            spacing: 6
            Repeater {
                model: w.report.columns ?? []
                delegate: Text {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: index === 0
                    Layout.preferredWidth: index === 0 ? -1 : w.valueWidth
                    horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                    text: modelData
                    color: "#90cdf4"
                    font.family: w.face
                    font.bold: true
                    font.pixelSize: w.unit * 0.72
                    elide: Text.ElideRight
                }
            }
        }

        // Cashiers: each ILSA cashier, and the ViewTouch employee they are (touch to choose).
        ListView {
            id: cashierList
            objectName: "ilsaCashiers"
            visible: w.kind === "cashiers"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            ScrollBar.vertical: TouchScrollBar { id: cashierBar }
            model: w.cashiers
            delegate: Rectangle {
                id: crow
                required property var modelData
                width: ListView.view.width - Math.max(28, cashierBar.room + 14)
                height: w.keyH * 1.3
                radius: 4
                color: "#2b3340"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: w.unit * 0.6
                    anchors.rightMargin: w.unit * 0.4
                    spacing: w.unit * 0.5
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: crow.modelData.label ?? ""; color: w.ink; font.family: w.face; font.bold: true; font.pixelSize: w.unit * 0.9; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text {
                            text: qsTr("%1 receipts, %2, %3 to %4 (%5)").arg(crow.modelData.receipts ?? 0).arg(crow.modelData.sales ?? "")
                                      .arg(crow.modelData.first ?? "").arg(crow.modelData.last ?? "").arg(crow.modelData.source ?? "")
                            color: "#a9b4c6"; font.family: w.face; font.pixelSize: w.unit * 0.7; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                    WidgetKey {
                        objectName: "ilsaCashierPick-" + (crow.modelData.sourceId ?? 0) + "-" + (crow.modelData.number ?? 0)
                        Layout.preferredWidth: w.unit * 14
                        Layout.preferredHeight: w.keyH
                        text: (crow.modelData.employeeId ?? "") !== "" ? crow.modelData.employeeName : qsTr("Former employee")
                        fontScale: 0.32
                        baseColor: (crow.modelData.employeeId ?? "") !== "" ? "#1f8a4c" : "#5b4a2a"
                        onClicked: { w.picking = crow.modelData; pickEmployee.open() }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: cashierList.count === 0
                text: w.mine && w.range.loading ? qsTr("Reading the ILSA history…") : qsTr("No ILSA cashiers yet (import the history first).")
                font.pixelSize: w.unit
                opacity: 0.7
            }
        }

        ListView {
            id: rows
            objectName: "ilsaRows"
            visible: w.kind !== "cashiers"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: TouchScrollBar { id: rowsBar }
            model: w.report.rows ?? []
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool section: modelData.kind === "section"
                readonly property bool total: modelData.kind === "total"
                readonly property bool note: modelData.kind === "note"
                width: ListView.view.width - Math.max(28, rowsBar.room + 14)
                height: row.note ? noteText.implicitHeight + w.unit * 0.3 : (row.section ? w.unit * 2.2 : w.unit * 1.5)

                Rectangle {
                    visible: row.total
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: "#3a4250"
                }
                Text {
                    id: noteText
                    visible: row.note
                    width: parent.width
                    text: row.note ? ((row.modelData.cells ?? [])[0] ?? "") : ""
                    wrapMode: Text.WordWrap
                    color: "#8a94a6"
                    font.family: w.face
                    font.italic: true
                    font.pixelSize: w.unit * 0.75
                }
                RowLayout {
                    visible: !row.note
                    anchors.fill: parent
                    anchors.topMargin: row.section ? w.unit * 0.7 : 0
                    spacing: 6
                    Repeater {
                        model: row.note ? [] : (row.modelData.cells ?? [])
                        delegate: Text {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: index === 0
                            Layout.preferredWidth: index === 0 ? -1 : w.valueWidth
                            horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight
                            text: modelData
                            color: w.ink
                            font.family: w.face
                            font.pixelSize: row.section ? w.unit * 0.95 : w.unit * 0.78
                            font.bold: row.section || row.total
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: rows.count === 0
                text: w.mine && w.range.loading ? qsTr("Reading the ILSA history…") : ""
                font.pixelSize: w.unit
                opacity: 0.7
            }
        }
    }

    // Which ViewTouch employee an ILSA cashier is.
    Popup {
        id: pickEmployee
        objectName: "ilsaPickEmployee"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: w.unit
        width: Math.min(w.width * 0.8, w.unit * 30)
        height: Math.min(w.height * 0.9, w.unit * 34)
        contentItem: ColumnLayout {
            spacing: w.unit * 0.5
            Label {
                Layout.fillWidth: true
                text: w.picking ? qsTr("Who is %1 in ViewTouch?").arg(w.picking.label) : ""
                font.pixelSize: w.unit * 1.0; font.bold: true
                wrapMode: Text.WordWrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: w.staff
                delegate: WidgetKey {
                    required property var modelData
                    width: ListView.view.width
                    height: w.keyH
                    text: (modelData.name ?? "") + ((modelData.active ?? true) ? "" : qsTr(" (inactive)"))
                    fontScale: 0.34
                    baseColor: w.picking && w.picking.employeeId === modelData.id ? "#1f8a4c" : "#343c49"
                    onClicked: {
                        const c = w.picking
                        pickEmployee.close()
                        if (c) w.pos.setLegacyCashier(c.sourceId, c.number, modelData.id, w.tieAllStores)
                    }
                }
            }
            WidgetKey {
                objectName: "ilsaTieAllStores"
                Layout.fillWidth: true
                Layout.preferredHeight: w.keyH
                text: (w.tieAllStores ? "☑ " : "☐ ") + qsTr("Tie the same cashier in all stores")
                fontScale: 0.32
                baseColor: w.tieAllStores ? "#1f8a4c" : "#2b3340"
                onClicked: w.tieAllStores = !w.tieAllStores
            }
            WidgetKey {
                objectName: "ilsaFormerEmployee"
                Layout.fillWidth: true
                Layout.preferredHeight: w.keyH
                text: qsTr("Former employee (not in ViewTouch)")
                fontScale: 0.32
                baseColor: "#5b4a2a"
                onClicked: {
                    const c = w.picking
                    pickEmployee.close()
                    if (c) w.pos.setLegacyCashier(c.sourceId, c.number, "", w.tieAllStores)
                }
            }
            WidgetKey {
                Layout.fillWidth: true
                Layout.preferredHeight: w.keyH
                text: qsTr("Cancel")
                fontScale: 0.32
                onClicked: pickEmployee.close()
            }
        }
    }
}
