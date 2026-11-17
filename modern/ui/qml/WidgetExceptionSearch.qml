import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Manager > Reports > Exceptions search: every void, error correct, comp or discount, payment taken back,
// reopened check, Cancel Sale, No Sale, refund, date of birth typed by hand and ID refused, over any days,
// from the database (PosService::searchExceptions). Filters: the days (today to start), who, which kinds,
// at least an amount, and a check number (with or without the store's prefix) or words of the item or
// the reason. Totals by kind at the top (they match the Exceptions report); 200 rows at a time, then
// More. Touch a row: its check in Find a Check (its history and receipt). Print or PDF the list.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property real unit: Math.max(14, Math.min(width / 56, height / 30))
    readonly property var info: pos ? pos.exceptionSearch : ({})
    readonly property var rows: info.results ?? []
    readonly property var totals: info.totals ?? []
    readonly property bool narrow: width < unit * 50

    function isoOf(d) { return Qt.formatDate(d, "yyyy-MM-dd") }
    property string from: isoOf(new Date())
    property string to: isoOf(new Date())
    property string employee: ""
    property var types: []
    readonly property var kinds: [
        ["void", qsTr("Void (item already sent)")], ["errorCorrect", qsTr("Error correct")],
        ["discount", qsTr("Comp / discount")], ["takenBack", qsTr("Payment taken back")],
        ["reopen", qsTr("Reopened check")], ["cancel", qsTr("Cancel Sale")], ["noSale", qsTr("No Sale")],
        ["refund", qsTr("Refund")], ["ageHand", qsTr("Date of birth typed by hand")], ["ageRefused", qsTr("ID refused")]]

    function run() {
        w.pos.searchExceptions({ from: from, to: to, employee: employee, types: types,
                                 minAmount: amountField.text, text: textField.text })
    }
    function since(days) {
        const d = new Date()
        d.setDate(d.getDate() - days)
        from = isoOf(d)
        to = isoOf(new Date())
        run()
    }
    function toggle(type) {
        const t = types.slice()
        const at = t.indexOf(type)
        if (at >= 0) t.splice(at, 1); else t.push(type)
        types = t
        run()
    }
    function openCheck(r) {
        if (!r.checkId) return
        zone.controller.keep("findCheck", { query: "#" + r.checkId, id: r.checkId })
        zone.controller.showPage("find-check")
    }

    // The results come to a tablet only while a screen shows them (PosServer: on request).
    Item {
        visible: false
        readonly property var p: w.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("exceptionSearch", false)
            asked = p
            if (p) p.want("exceptionSearch", true)
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) asked.want("exceptionSearch", false)
    }
    Component.onCompleted: if (pos) run()

    // The list as a PDF: the whole of it, as a report (the Reports screen's PDF).
    Connections {
        target: w.pos
        function onAnswered(tag, result) {
            if (tag === "exceptionsPdf" && result && result.title)
                w.zone.controller.exportReport(result, "pdf")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // --- the days ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [[0, qsTr("Today")], [6, qsTr("Week")], [30, qsTr("Month")], [365, qsTr("Year")]]
                delegate: Button {
                    required property var modelData
                    text: modelData[1]
                    implicitHeight: w.unit * 2.4
                    font.pixelSize: w.unit * 0.8
                    onClicked: w.since(modelData[0])
                }
            }
            DateField {
                objectName: "exceptionsFrom"
                Layout.preferredWidth: w.unit * 10
                implicitHeight: w.unit * 2.4
                fontSize: w.unit * 0.75
                value: w.from
                title: qsTr("From")
                onPicked: v => { if (v !== "") { w.from = v; if (w.to < v) w.to = v; w.run() } }
            }
            Label { text: "–"; font.pixelSize: w.unit }
            DateField {
                objectName: "exceptionsTo"
                Layout.preferredWidth: w.unit * 10
                implicitHeight: w.unit * 2.4
                fontSize: w.unit * 0.75
                value: w.to
                title: qsTr("To")
                onPicked: v => { if (v !== "") { w.to = v; if (w.from > v) w.from = v; w.run() } }
            }
            ComboBox {
                id: who
                objectName: "exceptionsEmployee"
                Layout.preferredWidth: w.unit * 10
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.75
                model: [qsTr("Everyone")].concat(w.info.people ?? [])
                onActivated: i => { w.employee = i === 0 ? "" : model[i]; w.run() }
            }
            Item { Layout.fillWidth: true }
        }
        // --- which kinds, how much, what words ---
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: w.kinds
                delegate: Button {
                    required property var modelData
                    text: modelData[1]
                    checkable: true
                    checked: w.types.indexOf(modelData[0]) >= 0
                    implicitHeight: w.unit * 2.1
                    font.pixelSize: w.unit * 0.7
                    onClicked: w.toggle(modelData[0])
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: amountField
                objectName: "exceptionsAmount"
                Layout.preferredWidth: w.unit * 8
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.8
                placeholderText: qsTr("At least $")
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onAccepted: w.run()
            }
            TextField {
                id: textField
                objectName: "exceptionsText"
                Layout.fillWidth: true
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.8
                placeholderText: qsTr("Check # (with or without the store's prefix), item or reason")
                onAccepted: w.run()
            }
            Button {
                text: qsTr("Search")
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.85
                palette.button: "#1d3fb8"; palette.buttonText: "white"
                onClicked: w.run()
            }
        }

        // --- totals by kind ---
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: w.totals
                delegate: Rectangle {
                    required property var modelData
                    width: tot.implicitWidth + w.unit
                    height: w.unit * 1.8
                    radius: 3
                    color: "#2b3445"
                    Label {
                        id: tot
                        anchors.centerIn: parent
                        color: "white"
                        font.pixelSize: w.unit * 0.72
                        text: modelData.label + ": " + modelData.count + (modelData.cents ? "  ·  " + modelData.amount : "")
                    }
                }
            }
            Label {
                visible: w.totals.length === 0
                text: w.info.loading ? qsTr("Looking…") : (w.info.label ? qsTr("No exceptions %1.").arg(w.info.label) : "")
                font.pixelSize: w.unit * 0.8
                opacity: 0.8
            }
        }

        // --- the list ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#f4f6fa"
            radius: 4
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 2
                RowLayout {   // headings
                    visible: !w.narrow
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: [[qsTr("When"), 7], [qsTr("Employee"), 7], [qsTr("Type"), 7], [qsTr("Check #"), 4.5],
                                [qsTr("Item / what"), 0], [qsTr("Amount"), 4], [qsTr("Reason"), 7], [qsTr("Approved by"), 6]]
                        delegate: Label {
                            required property var modelData
                            text: modelData[0]
                            Layout.preferredWidth: modelData[1] > 0 ? w.unit * modelData[1] : -1
                            Layout.fillWidth: modelData[1] === 0
                            font.pixelSize: w.unit * 0.7
                            font.bold: true
                            color: "#374151"
                        }
                    }
                }
                ListView {
                    id: list
                    objectName: "exceptionsList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: w.rows
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: w.narrow ? w.unit * 3 : w.unit * 1.9
                        color: tap.pressed ? "#c7d2fe" : row.index % 2 ? "#ffffff" : "#e9edf5"
                        GridLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            columns: w.narrow ? 4 : 8
                            columnSpacing: 4
                            rowSpacing: 0
                            Repeater {
                                model: [["when", 7], ["who", 7], ["typeLabel", 7], ["check", 4.5], ["what", 0],
                                        ["amount", 4], ["reason", 7], ["approvedBy", 6]]
                                delegate: Label {
                                    required property var modelData
                                    readonly property string key: modelData[0]
                                    Layout.preferredWidth: modelData[1] > 0 ? w.unit * modelData[1] : -1
                                    Layout.fillWidth: modelData[1] === 0
                                    text: row.modelData[key] ?? ""
                                    elide: Text.ElideRight
                                    horizontalAlignment: key === "amount" ? Text.AlignRight : Text.AlignLeft
                                    font.pixelSize: w.unit * 0.68
                                    font.bold: key === "typeLabel"
                                    color: key === "check" && row.modelData.checkId ? "#1d3fb8" : "#111827"
                                }
                            }
                        }
                        TapHandler { id: tap; onTapped: w.openCheck(row.modelData) }
                    }
                }
            }
        }

        // --- more, print, PDF ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: w.info.count > 0 ? qsTr("Showing %1 of %2 · %3").arg(w.info.shown).arg(w.info.count).arg(w.info.label ?? "") : ""
                font.pixelSize: w.unit * 0.75
            }
            Button {
                visible: w.info.more === true
                text: qsTr("More")
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.8
                onClicked: w.pos.moreExceptions()
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "exceptionsPrint"
                text: qsTr("Print")
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.8
                enabled: w.info.loading === false
                onClicked: w.pos.printExceptionSearch()
            }
            Button {
                objectName: "exceptionsPdf"
                text: "PDF"
                implicitHeight: w.unit * 2.4
                font.pixelSize: w.unit * 0.8
                enabled: w.info.loading === false
                onClicked: w.pos.ask("exceptionsPdf", "exceptionSearchReport", [])
            }
        }
    }
}
