import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// ILSA History (Reports > ILSA History): the sales history imported from ILSA (vt-ilsa-history),
// read-only. Daily Z (an ILSA day, or ViewTouch's own checks that day), sales by department, item
// movement (by item, UPC, brand, department), GP by month and by year (as ILSA counted it, and
// without the lines that have no cost), ILSA's own Z beside the imported receipts and ViewTouch's
// days, and the days ILSA's Z and its receipts disagree. props.report picks the first report.
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
        { id: "differences", label: qsTr("ILSA Differences") }
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
            w.pos.requestLegacyReport(w.kind, w.fromDate, w.toDate, w.kind === "depts" ? w.deptBy : w.by)
    }
    // Asked a moment later, not while the page is being built (the answer changes rangeReport,
    // which bindings here read: asking from inside a binding update is a binding loop).
    onKindChanged: Qt.callLater(refresh)
    onByChanged: Qt.callLater(refresh)
    onDeptByChanged: Qt.callLater(refresh)
    Component.onCompleted: Qt.callLater(refresh)

    readonly property var range: pos ? pos.rangeReport : ({})
    readonly property bool mine: (range.id ?? "") === "ilsa-" + kind
    readonly property var report: mine && range.report ? range.report
                                 : ({ title: mine && range.loading ? qsTr("Reading the ILSA history…") : "", rows: [] })
    readonly property int valueColumns: Math.max(1, (report.columns ?? []).length - 1)
    readonly property real valueWidth: Math.min(w.unit * 6.5, (w.width - w.unit * 12) / valueColumns)

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

        ListView {
            id: rows
            objectName: "ilsaRows"
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
}
