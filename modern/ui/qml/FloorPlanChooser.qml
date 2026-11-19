import QtQuick
import QtQuick.Layouts

// Choose Floor Plan (a manager; the chooseFloorPlan command): tonight's setup for every screen.
// "Normal (as built)" first, then this store's floor plans by name; the one in use is marked.
// A touch applies it at once; screens show it the next time they go to the floor plan.
Rectangle {
    id: chooser
    objectName: "floorPlanChooser"
    property LayoutController controller
    readonly property var choices: controller && controller.floorChooserOpen ? controller.floorPlanChoices() : []
    visible: controller ? controller.floorChooserOpen : false
    color: "#cc0f1318"

    // Touches stop here, not on the page behind.
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 520)
        height: Math.min(parent.height - 24, column.implicitHeight + 40)
        radius: 14
        color: "#232933"
        border.color: "#2f6fd6"
        border.width: 2

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Floor plan for every screen")
                color: "white"
                font.pixelSize: 24
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Screens change when they next go to the floor plan. Open checks stay with their tables.")
                color: "#b8c0cc"
                font.pixelSize: 16
                wrapMode: Text.WordWrap
            }
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: list.implicitHeight
                contentHeight: list.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: list
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: chooser.choices
                        delegate: WidgetKey {
                            required property var modelData
                            objectName: "floorPlanKey-" + (modelData.value === "" ? "normal" : modelData.value)
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            text: (modelData.current ? "✓ " : "") + modelData.text
                            fontScale: 0.32
                            baseColor: modelData.current ? "#1f8a4c" : "#2f6fd6"
                            onClicked: chooser.controller.chooseFloorPlan(modelData.value)
                        }
                    }
                }
            }
            WidgetKey {
                objectName: "floorPlanCancel"
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: qsTr("Cancel")
                fontScale: 0.3
                baseColor: "#6b2a2a"
                onClicked: chooser.controller.closeFloorChooser()
            }
        }
    }
}
