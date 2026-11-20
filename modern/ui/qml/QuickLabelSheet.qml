import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// QUICK LABEL (owner 2026-11-20; the quickLabel command: the Labels screen's Quick Label button, Cashier
// Duty's QUICK LABEL, or any button given it), as the label utility's Quick Label window on the Pi:
//  1. Type up to 3 lines (line 1 is ready to type in; Enter prints, Tab or Next goes to the next line).
//  2. The label as it will print, drawn by the label utility on the main computer a moment after typing.
//  3. The label roll (a label size and its printer; the last one used comes first), how many, Print.
// For anyone logged in. The main computer keeps the state (pos.quickLabel), so a tablet works the same.
Rectangle {
    id: sheet
    property PosService pos
    property var controller
    readonly property bool open: controller ? controller.quickLabelOpen === true : false
    readonly property var info: pos ? pos.quickLabel : ({})
    readonly property var rolls: info.rolls ?? []
    readonly property int maxChars: info.maxChars ?? 40
    readonly property int maxCopies: info.maxCopies ?? 50
    readonly property real u: Math.max(14, Math.min(width, height) / 30)
    readonly property bool narrow: width < height
    property string roll: ""
    property int copies: 1
    readonly property var chosenRoll: rolls.find(r => r.key === roll) ?? null
    visible: open && pos !== null && pos.loggedIn
    color: Qt.rgba(0.04, 0.05, 0.07, 0.94)

    // The lines as the main computer takes them (trimmed); the preview says which it shows.
    readonly property var lines: [line1.text.trim(), line2.text.trim(), line3.text.trim()]
    readonly property bool typed: lines.some(l => l !== "")
    readonly property string previewKey: JSON.stringify([lines, roll])
    readonly property bool previewCurrent: (info.previewFor ?? "") === previewKey && (info.preview ?? "") !== ""

    // Shown only while it's up: the tablet asks the main computer for it (PosServer: on request).
    property var asked: null
    function want(on) {
        if (asked && (!on || asked !== pos)) { asked.want("quickLabel", false); asked = null }
        if (on && pos && asked !== pos) { asked = pos; pos.want("quickLabel", true) }
    }
    onOpenChanged: {
        if (open) {
            if (!pos) return
            line1.text = ""; line2.text = ""; line3.text = ""
            copies = 1
            roll = ""
            want(true)
            pos.quickLabelRolls()
            Qt.callLater(() => line1.forceActiveFocus())
        } else {
            if (pos) pos.closeQuickLabel()
            want(false)
        }
    }
    onVisibleChanged: if (!visible && open && controller) controller.closeQuickLabel()   // logged out
    Component.onDestruction: want(false)
    // The roll last used (the label utility remembers it, the same on the Pi), once the rolls come.
    onRollsChanged: if (roll === "" || !chosenRoll) roll = info.roll && rolls.some(r => r.key === info.roll) ? info.roll
                                                                : (rolls.length ? rolls[0].key : "")

    // The picture a moment after typing stops (each one runs the label utility on the main computer).
    Timer {
        id: previewTimer
        interval: 400
        onTriggered: if (sheet.open && sheet.typed && sheet.roll !== "") sheet.pos.quickLabelPreview(sheet.lines, sheet.roll)
    }
    onPreviewKeyChanged: if (open) previewTimer.restart()

    function print() {
        if (!typed || roll === "" || (info.printing ?? false))
            return
        previewTimer.stop()
        pos.quickLabelPreview(lines, roll)
        pos.printQuickLabel(lines, roll, copies)
    }
    function clear() {
        line1.text = ""; line2.text = ""; line3.text = ""
        line1.forceActiveFocus()
    }
    function close() {
        Qt.inputMethod.hide()
        if (controller) controller.closeQuickLabel()
    }

    // Touches stop here, not on the page behind.
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    // One line of the label (self-contained: an inline component can't see this file's ids).
    component LineField: TextField {
        property real unit: 20
        signal escaped()
        Layout.fillWidth: true
        implicitHeight: 2.6 * unit
        font.pixelSize: 1.5 * unit
        font.bold: true
        horizontalAlignment: TextInput.AlignHCenter
        inputMethodHints: Qt.ImhNoPredictiveText
        Keys.onEscapePressed: escaped()
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: sheet.u * 0.6
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: parent.width
            spacing: sheet.u * 0.5

            RowLayout {
                Layout.fillWidth: true
                spacing: sheet.u * 0.5
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Quick Label")
                    color: "#fbbf24"
                    font.pixelSize: 1.5 * sheet.u
                    font.bold: true
                }
                Label {
                    visible: !sheet.narrow
                    text: qsTr("Type up to 3 lines, then Print (or press Enter)")
                    color: "#cbd5e1"
                    font.pixelSize: 0.9 * sheet.u
                }
                Button {
                    objectName: "quickLabelClose"
                    implicitHeight: 2.4 * sheet.u
                    implicitWidth: 6 * sheet.u
                    font.pixelSize: sheet.u
                    text: qsTr("Close")
                    onClicked: sheet.close()
                }
            }

            Label {
                visible: !(sheet.info.ready ?? true)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#fca5a5"
                font.pixelSize: sheet.u
                text: qsTr("Labels print from the main computer, and its label utility isn't installed there.")
            }

            // The lines (left) and the label as it prints (right; under them on a narrow screen).
            GridLayout {
                Layout.fillWidth: true
                columns: sheet.narrow ? 1 : 2
                columnSpacing: sheet.u
                rowSpacing: sheet.u * 0.5

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: sheet.u * 0.35
                    LineField {
                        id: line1
                        objectName: "quickLabelLine1"
                        unit: sheet.u
                        maximumLength: sheet.maxChars
                        placeholderText: qsTr("Line 1")
                        KeyNavigation.tab: line2
                        onAccepted: sheet.print()
                        onEscaped: sheet.close()
                    }
                    LineField {
                        id: line2
                        objectName: "quickLabelLine2"
                        unit: sheet.u
                        maximumLength: sheet.maxChars
                        placeholderText: qsTr("Line 2 (optional)")
                        KeyNavigation.tab: line3
                        onAccepted: sheet.print()
                        onEscaped: sheet.close()
                    }
                    LineField {
                        id: line3
                        objectName: "quickLabelLine3"
                        unit: sheet.u
                        maximumLength: sheet.maxChars
                        placeholderText: qsTr("Line 3 (optional)")
                        KeyNavigation.tab: line1
                        onAccepted: sheet.print()
                        onEscaped: sheet.close()
                    }
                }

                Rectangle {
                    objectName: "quickLabelPreview"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    // The label's own shape (width : height), as big as fits.
                    Layout.preferredHeight: {
                        const r = sheet.chosenRoll
                        const m = r ? String(r.size).match(/([\d.]+)\s*x\s*([\d.]+)/) : null
                        const ratio = m ? Math.max(0.2, Math.min(1.5, parseFloat(m[2]) / parseFloat(m[1]))) : 0.5
                        return Math.min(width * ratio, 8.4 * sheet.u + 10)
                    }
                    color: "white"
                    radius: 6
                    Image {
                        anchors.fill: parent
                        anchors.margins: 6
                        fillMode: Image.PreserveAspectFit
                        smooth: false      // the printer's own dots
                        visible: sheet.typed
                        opacity: sheet.previewCurrent ? 1 : 0.35
                        source: sheet.info.preview ?? ""
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: sheet.typed && (sheet.info.previewBusy ?? false)
                        visible: running
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: !sheet.typed || (sheet.info.previewError ?? "") !== ""
                        color: sheet.typed ? "#b91c1c" : "#64748b"
                        font.pixelSize: 0.9 * sheet.u
                        font.italic: !sheet.typed
                        text: sheet.typed ? (sheet.info.previewError ?? "") : qsTr("(type the label's text)")
                    }
                }
            }

            // The label roll: a size and the printer it's on.
            Label {
                text: (sheet.info.loading ?? false) ? qsTr("Label roll (finding them…)") : qsTr("Label roll")
                color: "#e2e8f0"
                font.pixelSize: sheet.u
                font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: sheet.u * 0.35
                Repeater {
                    model: sheet.rolls
                    delegate: Button {
                        required property var modelData
                        objectName: "quickLabelRoll-" + modelData.key
                        width: sheet.narrow ? parent.width : Math.max(10 * sheet.u, (parent.width - 3 * sheet.u * 0.35) / 4)
                        implicitHeight: 3.2 * sheet.u
                        font.pixelSize: 0.85 * sheet.u
                        checkable: true
                        checked: sheet.roll === modelData.key
                        highlighted: checked
                        text: modelData.size + "\n" + modelData.printer
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            color: parent.palette.buttonText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            fontSizeMode: Text.Fit
                            minimumPixelSize: 10
                            maximumLineCount: 3
                        }
                        onClicked: sheet.roll = modelData.key
                    }
                }
            }

            // How many, Clear, Print.
            RowLayout {
                Layout.fillWidth: true
                spacing: sheet.u * 0.4
                Label { text: qsTr("Copies"); color: "#e2e8f0"; font.pixelSize: sheet.u; font.bold: true }
                Button {
                    implicitWidth: 2.8 * sheet.u; implicitHeight: 2.8 * sheet.u
                    font.pixelSize: 1.3 * sheet.u
                    text: "−"
                    onClicked: sheet.copies = Math.max(1, sheet.copies - 1)
                }
                Label {
                    objectName: "quickLabelCopies"
                    Layout.preferredWidth: 2.6 * sheet.u
                    horizontalAlignment: Text.AlignHCenter
                    text: sheet.copies
                    color: "white"
                    font.pixelSize: 1.5 * sheet.u
                    font.bold: true
                }
                Button {
                    implicitWidth: 2.8 * sheet.u; implicitHeight: 2.8 * sheet.u
                    font.pixelSize: 1.3 * sheet.u
                    text: "+"
                    onClicked: sheet.copies = Math.min(sheet.maxCopies, sheet.copies + 1)
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "quickLabelClear"
                    implicitHeight: 2.8 * sheet.u
                    implicitWidth: 6 * sheet.u
                    font.pixelSize: sheet.u
                    text: qsTr("Clear")
                    onClicked: sheet.clear()
                }
                Button {
                    objectName: "quickLabelPrint"
                    implicitHeight: 2.8 * sheet.u
                    implicitWidth: 10 * sheet.u
                    font.pixelSize: 1.2 * sheet.u
                    font.bold: true
                    highlighted: true
                    enabled: sheet.typed && sheet.roll !== "" && !(sheet.info.printing ?? false) && (sheet.info.ready ?? true)
                    text: (sheet.info.printing ?? false) ? qsTr("Printing…") : qsTr("Print %n", "", sheet.copies)
                    onClicked: sheet.print()
                }
            }

            Label {
                objectName: "quickLabelStatus"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text !== ""
                color: (sheet.info.statusOk ?? false) ? "#86efac" : "#fca5a5"
                font.pixelSize: sheet.u
                font.bold: true
                text: sheet.info.status ?? ""
            }
        }
    }
}
