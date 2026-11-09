import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts
import "DateMath.js" as DateMath

// A date chosen by touch (owner 2026-11-09, like the old ILSA register's): the twelve months
// on the left, the month's days in the middle (weeks from Sunday, today ringed), the year with
// big - and + on the right; Today (or Now), Clear where the field may be empty, Cancel and OK.
// mode "datetime" adds the hour, minute and AM/PM; "time" shows only those. It gives back the
// text the screens already keep: "2026-09-01", "2026-09-01 7:00 PM", "7:00 PM" (twentyFour:
// "19:00"). Over the whole window, whatever scale the zone that opened it is drawn at.
Popup {
    id: picker
    objectName: "datePicker"
    property string mode: "date"            // "date", "datetime" or "time"
    property bool allowClear: false
    property bool twentyFour: false
    property string title: ""

    // What is being chosen.
    property int year: 2026
    property int month: 1                   // 1-12
    property int day: 1
    property int hour: 12                   // 0-23
    property int minute: 0

    // OK: the value; Clear: "".
    signal picked(string value)

    readonly property bool hasDate: mode !== "time"
    readonly property bool hasTime: mode !== "date"
    readonly property bool upright: width < height * 0.95
    // Ringed in the days; read again each time it opens (a screen left on overnight).
    property var today: new Date()

    // Starts on the value given, or today (now) when it's empty or not a date.
    function openWith(value) {
        const now = new Date()
        today = now
        const d = DateMath.parseDate(value)
        year = d ? d.year : now.getFullYear()
        month = d ? d.month : now.getMonth() + 1
        day = d ? d.day : now.getDate()
        const t = hasTime ? DateMath.parseTime(mode === "datetime" && d ? String(value).trim().slice(10) : value) : null
        hour = t ? t.hour : (d || !hasTime ? 12 : now.getHours())
        minute = t ? t.minute : (d || !hasTime ? 0 : now.getMinutes())
        open()
    }
    function setMonth(m) { month = m; day = DateMath.clampDay(year, month, day) }
    function stepMonth(by) {
        const n = DateMath.addMonths(year, month, by)
        year = n.year
        month = n.month
        day = DateMath.clampDay(year, month, day)
    }
    function stepYear(by) { year = Math.max(1900, Math.min(2200, year + by)); day = DateMath.clampDay(year, month, day) }
    function setHour12(h12) { hour = (h12 % 12) + (hour >= 12 ? 12 : 0) }
    function setPm(pm) { hour = (hour % 12) + (pm ? 12 : 0) }
    function stepMinute(by) { minute = (minute + by + 60) % 60 }
    function toNow() {
        const now = new Date()
        year = now.getFullYear(); month = now.getMonth() + 1; day = now.getDate()
        if (hasTime) { hour = now.getHours(); minute = now.getMinutes() }
    }
    readonly property string value: DateMath.valueText(mode, year, month, day, hour, minute, twentyFour)

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: parent ? Math.min(parent.width - 16, hasDate ? 1180 : 760) : 800
    height: parent ? Math.min(parent.height - 16, hasDate ? (hasTime ? 760 : 600) : 440) : 600
    modal: true
    focus: true
    dim: true
    padding: 14
    closePolicy: Popup.CloseOnEscape
    z: 900

    Overlay.modal: Rectangle { color: "#cc0f1318" }
    background: Rectangle { radius: 14; color: "#232933"; border.color: "#4a7bd8"; border.width: 2 }

    // A finger's size, from the room there is.
    readonly property real gap: 8
    readonly property real keyH: Math.max(44, Math.min(78, (height - 2 * padding) / (hasDate ? (hasTime ? 11 : 8.4) : 6.2)))

    contentItem: ColumnLayout {
        spacing: picker.gap

        // The heading: what it's for, and the choice so far.
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: picker.title !== "" ? picker.title
                    : picker.mode === "time" ? qsTr("Choose a time")
                    : picker.mode === "datetime" ? qsTr("Choose a date and time") : qsTr("Choose a date")
                color: "#b8c0cc"
                font.pixelSize: Math.max(16, picker.keyH * 0.32)
                elide: Text.ElideRight
            }
            Text {
                objectName: "datePickerShown"
                text: (picker.hasDate ? Qt.locale().toString(new Date(picker.year, picker.month - 1, picker.day), "dddd, MMMM d, yyyy") : "")
                    + (picker.hasDate && picker.hasTime ? "  ·  " : "")
                    + (picker.hasTime ? DateMath.timeText(picker.hour, picker.minute, picker.twentyFour) : "")
                color: "white"
                font.pixelSize: Math.max(18, picker.keyH * 0.4)
                font.bold: true
            }
        }

        // Months | days | year (a phone: one over the other).
        GridLayout {
            visible: picker.hasDate
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: picker.upright ? 1 : 3
            rowSpacing: picker.gap
            columnSpacing: picker.gap * 2

            // The twelve months.
            GridLayout {
                Layout.fillHeight: true
                Layout.fillWidth: picker.upright
                Layout.preferredWidth: picker.upright ? -1 : picker.keyH * 4.2
                Layout.preferredHeight: picker.upright ? picker.keyH * 2.2 : -1
                columns: picker.upright ? 6 : 3
                rowSpacing: picker.gap * 0.6
                columnSpacing: picker.gap * 0.6
                Repeater {
                    model: 12
                    delegate: WidgetKey {
                        required property int index
                        objectName: "dateMonth-" + (index + 1)
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: Qt.locale().standaloneMonthName(index, Locale.ShortFormat)
                        fontScale: 0.36
                        takesTouch: true
                        baseColor: picker.month === index + 1 ? "#2f6fd6" : "#343c49"
                        onClicked: picker.setMonth(index + 1)
                    }
                }
            }

            // The month's days, weeks from Sunday.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: picker.gap * 0.5
                RowLayout {
                    Layout.fillWidth: true
                    spacing: picker.gap * 0.5
                    WidgetKey {
                        objectName: "datePrevMonth"
                        Layout.preferredWidth: picker.keyH * 1.3
                        Layout.preferredHeight: picker.keyH * 0.8
                        text: "◀"
                        fontScale: 0.45
                        takesTouch: true
                        onClicked: picker.stepMonth(-1)
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: Qt.locale().standaloneMonthName(picker.month - 1, Locale.LongFormat) + " " + picker.year
                        color: "white"
                        font.pixelSize: Math.max(18, picker.keyH * 0.42)
                        font.bold: true
                    }
                    WidgetKey {
                        objectName: "dateNextMonth"
                        Layout.preferredWidth: picker.keyH * 1.3
                        Layout.preferredHeight: picker.keyH * 0.8
                        text: "▶"
                        fontScale: 0.45
                        takesTouch: true
                        onClicked: picker.stepMonth(1)
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 7
                    rowSpacing: picker.gap * 0.4
                    columnSpacing: picker.gap * 0.4
                    // Sunday first (2026-11-01 was a Sunday).
                    Repeater {
                        model: 7
                        delegate: Text {
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            horizontalAlignment: Text.AlignHCenter
                            text: Qt.locale().toString(new Date(2026, 10, 1 + index), "ddd")
                            color: index === 0 || index === 6 ? "#f6ad55" : "#b8c0cc"
                            font.pixelSize: Math.max(13, picker.keyH * 0.28)
                            font.bold: true
                        }
                    }
                    Repeater {
                        model: DateMath.monthCells(picker.year, picker.month)
                        delegate: Item {
                            id: cell
                            required property int modelData
                            required property int index
                            readonly property bool isToday: modelData > 0 && picker.year === picker.today.getFullYear()
                                                            && picker.month === picker.today.getMonth() + 1
                                                            && modelData === picker.today.getDate()
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 1
                            WidgetKey {
                                visible: cell.modelData > 0
                                objectName: "dateDay-" + cell.modelData
                                anchors.fill: parent
                                text: String(cell.modelData)
                                fontScale: 0.46
                                takesTouch: true
                                baseColor: picker.day === cell.modelData ? "#2f6fd6" : cell.isToday ? "#1f6b40" : "#2b3340"
                                onClicked: picker.day = cell.modelData
                            }
                            Rectangle {   // today, ringed even when another day is chosen
                                visible: cell.isToday
                                anchors.fill: parent
                                radius: Math.min(width, height) * 0.14
                                color: "transparent"
                                border.color: "#48bb78"
                                border.width: 4
                            }
                        }
                    }
                }
            }

            // The year.
            GridLayout {
                Layout.fillHeight: !picker.upright
                Layout.fillWidth: picker.upright
                Layout.preferredWidth: picker.upright ? -1 : picker.keyH * 2.2
                columns: picker.upright ? 3 : 1
                rowSpacing: picker.gap
                columnSpacing: picker.gap
                WidgetKey {
                    objectName: "dateYearUp"
                    Layout.fillWidth: true
                    Layout.preferredHeight: picker.keyH * 1.3
                    text: "+"
                    fontScale: 0.6
                    takesTouch: true
                    onClicked: picker.stepYear(1)
                }
                Text {
                    objectName: "dateYear"
                    Layout.fillWidth: true
                    Layout.fillHeight: !picker.upright
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: String(picker.year)
                    color: "white"
                    font.pixelSize: Math.max(22, picker.keyH * 0.6)
                    font.bold: true
                }
                WidgetKey {
                    objectName: "dateYearDown"
                    Layout.fillWidth: true
                    Layout.preferredHeight: picker.keyH * 1.3
                    text: "−"
                    fontScale: 0.6
                    takesTouch: true
                    onClicked: picker.stepYear(-1)
                }
            }
        }

        // The time: hour, minute, AM/PM.
        GridLayout {
            visible: picker.hasTime
            Layout.fillWidth: true
            Layout.fillHeight: !picker.hasDate
            Layout.preferredHeight: picker.keyH * 2 + picker.gap
            columns: picker.upright ? 1 : 3
            rowSpacing: picker.gap
            columnSpacing: picker.gap * 2

            GridLayout {   // hours 1-12
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 6
                columns: 6
                rowSpacing: picker.gap * 0.5
                columnSpacing: picker.gap * 0.5
                Repeater {
                    model: [12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
                    delegate: WidgetKey {
                        required property int modelData
                        objectName: "dateHour-" + modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: String(modelData)
                        fontScale: 0.42
                        takesTouch: true
                        baseColor: picker.hour % 12 === modelData % 12 ? "#2f6fd6" : "#343c49"
                        onClicked: picker.setHour12(modelData)
                    }
                }
            }
            GridLayout {   // minutes by five, and a minute either way
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 7
                columns: 7
                rowSpacing: picker.gap * 0.5
                columnSpacing: picker.gap * 0.5
                Repeater {
                    model: [0, 5, 10, 15, 20, 25, -1, 30, 35, 40, 45, 50, 55, 1]
                    delegate: WidgetKey {
                        required property int modelData
                        required property int index
                        objectName: modelData < 0 ? "dateMinuteDown" : index === 13 ? "dateMinuteUp" : "dateMinute-" + modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: modelData < 0 ? "−1" : index === 13 ? "+1" : ":" + DateMath.pad(modelData)
                        fontScale: 0.38
                        takesTouch: true
                        baseColor: index !== 6 && index !== 13 && picker.minute === modelData ? "#2f6fd6" : "#343c49"
                        onClicked: {
                            if (modelData < 0) picker.stepMinute(-1)
                            else if (index === 13) picker.stepMinute(1)
                            else picker.minute = modelData
                        }
                    }
                }
            }
            GridLayout {   // AM / PM
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                columns: picker.upright ? 2 : 1
                rowSpacing: picker.gap * 0.5
                columnSpacing: picker.gap * 0.5
                WidgetKey {
                    objectName: "dateAm"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: "AM"
                    fontScale: 0.4
                    takesTouch: true
                    baseColor: picker.hour < 12 ? "#2f6fd6" : "#343c49"
                    onClicked: picker.setPm(false)
                }
                WidgetKey {
                    objectName: "datePm"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: "PM"
                    fontScale: 0.4
                    takesTouch: true
                    baseColor: picker.hour >= 12 ? "#2f6fd6" : "#343c49"
                    onClicked: picker.setPm(true)
                }
            }
        }

        // Today / Now, Clear, Cancel, OK.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: picker.keyH
            Layout.fillHeight: false
            spacing: picker.gap
            WidgetKey {
                objectName: "dateToday"
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                Layout.fillHeight: true
                text: picker.mode === "date" ? qsTr("Today") : picker.mode === "time" ? qsTr("Now")
                    : qsTr("Current Date and Time")
                fontScale: 0.34
                takesTouch: true
                baseColor: "#2b6cb0"
                onClicked: picker.toNow()
            }
            WidgetKey {
                visible: picker.allowClear
                objectName: "dateClear"
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.fillHeight: true
                text: qsTr("Clear")
                fontScale: 0.36
                takesTouch: true
                baseColor: "#c0392b"
                onClicked: { picker.close(); picker.picked("") }
            }
            WidgetKey {
                objectName: "dateCancel"
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.fillHeight: true
                text: qsTr("Cancel")
                fontScale: 0.36
                takesTouch: true
                onClicked: picker.close()
            }
            WidgetKey {
                objectName: "dateOk"
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.fillHeight: true
                text: qsTr("OK")
                fontScale: 0.4
                takesTouch: true
                baseColor: "#1f8a4c"
                onClicked: { const v = picker.value; picker.close(); picker.picked(v) }
            }
        }
    }
}
