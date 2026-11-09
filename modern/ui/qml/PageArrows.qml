import QtQuick
import QtQuick.Layouts

// The big page buttons beside a list (Find, Item Look Up): ▲ PG UP over ▼ PG DN, a fixed-width
// column on the RIGHT of the list (the list takes the rest of the width). Each touch moves the
// list one page. Put it in a RowLayout after the list.
ColumnLayout {
    id: arrows
    property string upName: ""
    property string downName: ""
    property real keyWidth: 130
    signal up()
    signal down()

    Layout.fillWidth: false            // a layout's default is to fill: then it took half the row
    Layout.fillHeight: true
    Layout.preferredWidth: keyWidth
    Layout.maximumWidth: keyWidth
    spacing: 12
    WidgetKey {
        objectName: arrows.upName
        Layout.fillWidth: true
        Layout.fillHeight: true
        text: "▲\n" + qsTr("PG UP")
        fontScale: 0.26
        onClicked: arrows.up()
    }
    WidgetKey {
        objectName: arrows.downName
        Layout.fillWidth: true
        Layout.fillHeight: true
        text: "▼\n" + qsTr("PG DN")
        fontScale: 0.26
        onClicked: arrows.down()
    }
}
