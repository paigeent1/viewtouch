import QtQuick
import "DateMath.js" as DateMath

// A date (or a date and time, or a time) to touch: it shows the day in words ("Tue, Sep 1, 2026")
// and opens the calendar (DatePickerPopup), not the keyboard. `value` stays as the screens keep
// it ("2026-09-01", "2026-09-01 7:00 PM", "7:00 PM"); picked() gives the new one ("" when cleared).
// As tall as the keys beside it: give it their height.
Rectangle {
    id: f
    property string value: ""
    property string mode: "date"           // "date", "datetime", "time"
    property bool allowClear: false
    property bool twentyFour: false
    property string title: ""
    property string placeholder: mode === "time" ? qsTr("Choose a time") : qsTr("Choose a date")
    property bool readOnly: false
    property real fontSize: Math.max(14, Math.min(height * 0.42, 26))
    property color textColor: "white"
    signal picked(string value)
    readonly property alias picker: popup   // the calendar it opens (tests)

    // The words shown: a day like "Tue, Sep 1, 2026", "7:00 PM", or both.
    function friendly(v) {
        const d = DateMath.parseDate(v)
        const t = f.mode === "date" ? null : DateMath.parseTime(f.mode === "datetime" && d ? String(v).trim().slice(10) : v)
        let out = ""
        if (f.mode !== "time" && d)
            out = Qt.locale().toString(new Date(d.year, d.month - 1, d.day), "ddd, MMM d, yyyy")
        if (t)
            out += (out !== "" ? "  " : "") + DateMath.timeText(t.hour, t.minute, f.twentyFour)
        return out !== "" ? out : String(v ?? "")
    }
    function choose() {
        if (f.readOnly)
            return
        f.forceActiveFocus()   // off any field being typed in: the keyboard goes away
        popup.openWith(f.value)
    }

    implicitWidth: Math.max(160, shown.implicitWidth + 56)
    implicitHeight: 48
    radius: 8
    color: tap.pressed ? "#3a4659" : "#1d2430"
    border.color: popup.opened ? "#4a7bd8" : "#5a6678"
    border.width: 2
    activeFocusOnTab: true
    Keys.onReturnPressed: f.choose()
    Keys.onSpacePressed: f.choose()

    Text {
        id: shown
        anchors.left: parent.left
        anchors.right: glyph.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: f.value !== "" ? f.friendly(f.value) : f.placeholder
        color: f.value !== "" ? f.textColor : "#8f99a8"
        font.pixelSize: f.fontSize
        font.bold: f.value !== ""
        elide: Text.ElideRight
    }
    // A little calendar page (a clock for a time), drawn: no font is needed for it.
    Item {
        id: glyph
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(28, f.height * 0.5)
        height: width
        visible: !f.readOnly
        Rectangle {
            visible: f.mode !== "time"
            anchors.fill: parent
            radius: 3
            color: "transparent"
            border.color: "#90cdf4"
            border.width: 2
            Rectangle { width: parent.width; height: parent.height * 0.28; color: "#90cdf4"; radius: 2 }
            Grid {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: parent.height * 0.12
                columns: 3
                spacing: parent.width * 0.08
                Repeater {
                    model: 6
                    delegate: Rectangle { width: glyph.width * 0.14; height: width; color: "#90cdf4" }
                }
            }
        }
        Rectangle {
            visible: f.mode === "time"
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: "#90cdf4"
            border.width: 2
            Rectangle { x: parent.width / 2 - 1; y: parent.height * 0.18; width: 2; height: parent.height * 0.34; color: "#90cdf4" }
            Rectangle { x: parent.width / 2 - 1; y: parent.height / 2 - 1; width: parent.width * 0.28; height: 2; color: "#90cdf4" }
        }
    }

    TapHandler {
        id: tap
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: { if (TouchGuard.covered(point.scenePressPosition)) return; f.choose() }
    }

    DatePickerPopup {
        id: popup
        mode: f.mode
        allowClear: f.allowClear
        twentyFour: f.twentyFour
        title: f.title
        onPicked: v => f.picked(v)
    }
}
