import QtQuick
import QtQuick.Layouts

// On-screen keyboard for notes; types into the POS text entry.
// props.placeholder: hint shown while nothing is typed.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    property bool shifted: true   // first letter capitalized
    // These keys always type into the line above them: it's active while the keyboard shows.
    readonly property bool active: visible && !!pos && pos.loggedIn
    // On Find (props.search, or the Find page): what the search is doing.
    readonly property bool finding: !!zone && (!!(zone.props && zone.props.search === true)
                                    || (!!zone.controller && zone.controller.pageId === "find-item"))
    readonly property string findStatus: {
        if (!finding || !pos)
            return ""
        const typed = pos.textEntry.trim().toLowerCase()
        if (typed === "")
            return ""
        const found = pos.itemSearch
        if (String(found.query ?? "").toLowerCase() !== typed)
            return qsTr("Searching…")
        const n = found.count ?? 0
        return n === 0 ? qsTr("No match for '%1'").arg(pos.textEntry.trim()) : n === 1 ? qsTr("1 match") : qsTr("%1 matches").arg(n)
    }

    readonly property var rows: [
        ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
        ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
        ["a", "s", "d", "f", "g", "h", "j", "k", "l", "'"],
        ["shift", "z", "x", "c", "v", "b", "n", "m", ",", "back"],
        ["clear", "space", "."],
    ]

    function press(k) {
        if (k === "shift") { shifted = !shifted; return }
        if (k === "back" || k === "clear" || k === "space") {
            pos.textKey(k)
        } else {
            pos.textKey(shifted ? k.toUpperCase() : k)
            shifted = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.height * 0.03
        spacing: w.height * 0.02

        // The line typed into: always the one these keys (and a keyboard) type into, so it shows as
        // active from the start (a bright border and a thick blinking cursor), no touch on it needed.
        // On Find: what the search is doing ("Searching…", "12 matches", "No match for …").
        Rectangle {
            id: line
            objectName: "keyboardLine"
            Layout.fillWidth: true
            Layout.preferredHeight: w.height * 0.14
            radius: 8
            color: "#10141a"
            border.color: w.active ? "#facc15" : "#475569"
            border.width: w.active ? Math.max(3, line.height * 0.06) : 1
            readonly property string entry: w.pos ? w.pos.textEntry : ""
            readonly property real fontPx: height * 0.45
            Text {
                id: typedText
                objectName: "keyboardText"
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, line.width - 16 - status.width - 40)
                text: line.entry
                color: "white"
                font.family: w.zone.st.font ?? "DejaVu Sans"
                font.pixelSize: line.fontPx
                elide: Text.ElideLeft
            }
            Rectangle {
                id: cursor
                objectName: "keyboardCursor"
                x: typedText.x + (line.entry === "" ? 0 : typedText.width + 3)
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(4, line.fontPx * 0.12)
                height: line.fontPx * 1.15
                radius: width / 2
                color: "#facc15"
                visible: w.active
                SequentialAnimation on opacity {
                    running: cursor.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 1; duration: 0 }
                    PauseAnimation { duration: 530 }
                    NumberAnimation { to: 0; duration: 0 }
                    PauseAnimation { duration: 530 }
                }
            }
            Text {
                anchors.left: cursor.right
                anchors.leftMargin: 10
                anchors.right: status.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: line.entry === ""
                text: w.zone && w.zone.props && w.zone.props.placeholder ? qsTranslate("Page", w.zone.props.placeholder) : qsTr("Type a note…")
                color: "#8a94a6"
                font.family: w.zone.st.font ?? "DejaVu Sans"
                font.pixelSize: line.fontPx
                elide: Text.ElideRight
            }
            Text {
                id: status
                objectName: "findStatus"
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                width: text === "" ? 0 : Math.min(implicitWidth, line.width * 0.45)
                visible: text !== ""
                text: w.findStatus
                color: w.findStatus === qsTr("Searching…") ? "#93c5fd" : "#fde047"
                font.family: w.zone.st.font ?? "DejaVu Sans"
                font.pixelSize: line.fontPx * 0.7
                font.bold: true
                elide: Text.ElideRight
            }
        }

        Repeater {
            model: w.rows
            delegate: RowLayout {
                id: row
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: w.height * 0.015
                Repeater {
                    model: row.modelData
                    delegate: WidgetKey {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: modelData === "space" ? 6 : (modelData.length > 1 ? 1.5 : 1)
                        text: modelData === "space" ? qsTr("space") : modelData === "back" ? "⌫"
                            : modelData === "shift" ? "⇧" : modelData === "clear" ? qsTr("Clear")
                            : (w.shifted ? modelData.toUpperCase() : modelData)
                        accent: modelData === "shift" && w.shifted
                        fontScale: modelData.length > 1 ? 0.3 : 0.42
                        onClicked: w.press(modelData)
                    }
                }
            }
        }
    }
}
