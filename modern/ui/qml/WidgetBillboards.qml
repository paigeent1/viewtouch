import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Hardware Configuration > Billboards: each billboard (a TV around the store showing the guest
// screen's pictures and videos; Billboard.qml) - on or off, its video sound muted or not, and its
// hours each day of the week, by the store's clock (the Pi's). Kept on the store's computer: a
// change reaches the TV at once, nobody touches the TV. Outside its hours a billboard goes dark.
// Managers only (pos.setBillboard; the list is adminRecords("billboards")).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var boards: !pos || pos.adminRevision < 0 || pos.queryRevision < 0 ? [] : pos.adminRecords("billboards")
    readonly property var dayNames: [qsTr("Monday"), qsTr("Tuesday"), qsTr("Wednesday"), qsTr("Thursday"),
                                     qsTr("Friday"), qsTr("Saturday"), qsTr("Sunday")]
    // Every half hour of the day, kept as "HH:mm", shown as the store's clock shows it.
    readonly property var times: {
        const out = []
        for (let m = 0; m < 24 * 60; m += 30)
            out.push(String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"))
        return out
    }
    function timeText(t) { return Qt.locale().toString(new Date(2000, 0, 1, Number(t.slice(0, 2)), Number(t.slice(3))), "h:mm AP") }
    function hours(b) { const h = b.hours ?? []; return [0, 1, 2, 3, 4, 5, 6].map(d => h[d] ?? "") }
    function setDay(b, day, value) {
        const h = hours(b)
        h[day] = value
        pos.setBillboard(b.name, { hours: h })
    }

    component Big: Label { font.pixelSize: 22; font.bold: true; color: "white" }
    component Caption: Label { font.pixelSize: 16; opacity: 0.75; color: "white"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    component Choice: ComboBox { implicitHeight: 64; Layout.minimumHeight: 64; font.pixelSize: 18 }
    component Toggle: Switch { implicitHeight: 64; Layout.minimumHeight: 64; font.pixelSize: 18 }
    component Tool: Button { implicitHeight: 64; Layout.minimumHeight: 64; Layout.minimumWidth: 200; font.pixelSize: 17 }

    Label {
        anchors.centerIn: parent
        width: parent.width * 0.8
        visible: w.boards.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "white"
        font.pixelSize: 20
        opacity: 0.8
        text: qsTr("No billboards yet. A TV with Fire TV becomes one with vt-add-billboard on the Pi, or set a paired screen's Screen layout to Billboard in Terminals.")
    }

    TouchScrollColumn {
        anchors.fill: parent
        visible: w.boards.length > 0
        spacing: 16

        Caption {
            text: qsTr("A billboard shows the guest screen's messages, pictures and videos (Store Settings > Customer display), never a sale or a customer. Outside its hours the TV goes dark.")
        }

        Repeater {
            model: w.boards
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property var b: modelData
                Layout.fillWidth: true
                implicitHeight: body.implicitHeight + 32
                radius: 8
                color: "#232a35"
                ColumnLayout {
                    id: body
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                    spacing: 10
                    RowLayout {
                        spacing: 24
                        Big { text: card.b.name; Layout.fillWidth: true }
                        Label {
                            font.pixelSize: 18
                            color: card.b.showing ? "#7ee0a1" : "#ffb37a"
                            text: card.b.showing ? qsTr("Showing now") : card.b.enabled ? qsTr("Dark now (outside its hours)") : qsTr("Off")
                        }
                        Toggle {
                            objectName: "billboardOn"
                            text: qsTr("On")
                            checked: card.b.enabled
                            onToggled: w.pos.setBillboard(card.b.name, { enabled: checked })
                        }
                        Toggle {
                            objectName: "billboardMute"
                            text: qsTr("Mute all videos")
                            checked: card.b.mute
                            onToggled: w.pos.setBillboard(card.b.name, { mute: checked })
                        }
                    }
                    Caption { text: qsTr("Hours by the store's clock. All day every day: always on.") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: 7
                            delegate: RowLayout {
                                id: day
                                required property int index
                                readonly property string value: w.hours(card.b)[index]
                                readonly property string kind: value === "" ? "all" : value === "closed" ? "closed" : "hours"
                                readonly property string from: kind === "hours" ? value.slice(0, 5) : "09:00"
                                readonly property string to: kind === "hours" ? value.slice(6, 11) : "21:00"
                                Layout.fillWidth: true
                                spacing: 12
                                Label { text: w.dayNames[day.index]; color: "white"; font.pixelSize: 18; Layout.preferredWidth: 150 }
                                Choice {
                                    Layout.preferredWidth: 220
                                    model: [qsTr("All day"), qsTr("These hours"), qsTr("Closed")]
                                    currentIndex: day.kind === "all" ? 0 : day.kind === "hours" ? 1 : 2
                                    onActivated: i => w.setDay(card.b, day.index, i === 0 ? "" : i === 2 ? "closed" : day.from + "-" + day.to)
                                }
                                Choice {
                                    visible: day.kind === "hours"
                                    Layout.preferredWidth: 180
                                    model: w.times.map(w.timeText)
                                    currentIndex: Math.max(0, w.times.indexOf(day.from))
                                    onActivated: i => w.setDay(card.b, day.index, w.times[i] + "-" + day.to)
                                }
                                Label { visible: day.kind === "hours"; text: qsTr("to"); color: "white"; font.pixelSize: 18 }
                                Choice {
                                    visible: day.kind === "hours"
                                    Layout.preferredWidth: 180
                                    model: w.times.map(w.timeText)
                                    currentIndex: Math.max(0, w.times.indexOf(day.to))
                                    onActivated: i => w.setDay(card.b, day.index, day.from + "-" + w.times[i])
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 12
                        Tool {
                            text: qsTr("Monday's hours every day")
                            onClicked: { const h = w.hours(card.b); w.pos.setBillboard(card.b.name, { hours: [0, 1, 2, 3, 4, 5, 6].map(() => h[0]) }) }
                        }
                        Tool {
                            text: qsTr("Always on")
                            onClicked: w.pos.setBillboard(card.b.name, { hours: [] })
                        }
                    }
                }
            }
        }
    }
}
