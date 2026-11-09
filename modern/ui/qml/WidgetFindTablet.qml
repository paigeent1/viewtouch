import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Find a Tablet (End of shift, any staff): the store's tablets, connected or not and when each was
// last seen. Find makes that tablet beep loudly and show "Here I am" until someone taps it there,
// Stop is touched here, or about 2 minutes pass (PosService::findTablet).
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    // The list comes to a tablet only while a screen shows it (PosServer: on request).
    Item {
        visible: false
        readonly property var p: w.pos
        property var asked: null
        function ask() {
            if (asked === p) return
            if (asked) asked.want("tablets", false)
            asked = p
            if (p) p.want("tablets", true)
        }
        onPChanged: ask()
        Component.onCompleted: ask()
        Component.onDestruction: if (asked) asked.want("tablets", false)
    }
    readonly property var tablets: pos ? pos.tablets : []
    // Said plainly when the one touched can't hear the store.
    property string problem: ""

    readonly property bool narrow: zone ? zone.narrow : false
    readonly property real zoom: zone ? zone.formZoom(narrow ? 380 : 900) : 1
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 18
                text: qsTr("Touch Find on the tablet you're looking for: it beeps loudly and shows “Here I am” until someone taps it, you touch Stop, or about 2 minutes pass.")
            }
            Label {
                objectName: "findTabletProblem"
                Layout.fillWidth: true
                visible: w.problem !== ""
                wrapMode: Text.WordWrap
                font.pixelSize: 20
                font.bold: true
                color: "#ff8a80"
                text: w.problem
            }
            Label {
                Layout.fillWidth: true
                visible: w.tablets.length === 0
                wrapMode: Text.WordWrap
                font.pixelSize: 18
                opacity: 0.8
                text: qsTr("No tablets are paired with this store yet (Manager → Terminals).")
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: w.tablets
                ScrollBar.vertical: TouchScrollBar { id: listBar }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: ListView.view.width - listBar.room
                    height: 84
                    radius: 10
                    color: modelData.beeping ? "#5a1f1f" : "#2b3240"
                    border.color: modelData.beeping ? "#ff5252" : "#3a4354"
                    border.width: 2
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 14
                        Rectangle {
                            width: 18; height: 18; radius: 9
                            color: row.modelData.connected ? "#4ade80" : "#6b7280"
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: row.modelData.name
                                font.pixelSize: 24
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 16
                                opacity: 0.8
                                text: row.modelData.beeping ? qsTr("Beeping now…")
                                    : row.modelData.connected ? qsTr("Connected")
                                    : row.modelData.lastSeenText ? qsTr("Not connected · last seen %1").arg(row.modelData.lastSeenText)
                                    : qsTr("Not connected")
                            }
                        }
                        Button {
                            objectName: "find-" + row.modelData.id
                            implicitWidth: 150
                            implicitHeight: 64
                            font.pixelSize: 20
                            highlighted: !row.modelData.beeping
                            text: row.modelData.beeping ? qsTr("Stop") : qsTr("Find")
                            onClicked: {
                                if (row.modelData.beeping) {
                                    w.problem = ""
                                    w.pos.stopFindingTablet(row.modelData.id)
                                } else if (!row.modelData.connected) {
                                    w.problem = qsTr("%1 is not connected — it may be off, asleep with Wi-Fi off, or out of battery.").arg(row.modelData.name)
                                } else {
                                    w.problem = ""
                                    w.pos.findTablet(row.modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
