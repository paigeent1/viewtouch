import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// The guest screen's pictures, chosen by touch (Store Settings > Customer display > + Add a picture):
// the pictures in Dropbox > POS project > Guest Screen, copied to the Pi when this opens
// (LayoutController.refreshGuestPictures, vt-guest-pictures), and any in ~/Pictures/Guest Screen.
// Touching one picks it.
Popup {
    id: picker
    objectName: "guestPicturePicker"

    property LayoutController controller: null
    signal picked(string path)

    readonly property var found: controller && controller.siteRevision >= 0 ? controller.guestPictures() : ({})
    readonly property var files: found.pictures ?? []
    readonly property bool busy: found.busy === true

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 32 : 900, 1100)
    height: Math.min(parent ? parent.height - 32 : 700, 760)
    modal: true
    padding: 20

    onOpened: if (controller) controller.refreshGuestPictures()

    background: Rectangle { color: "#1c2128"; radius: 12; border.color: "#3a4250" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            spacing: 10
            Label {
                text: qsTr("Pictures in Dropbox > POS project > Guest Screen")
                font.pixelSize: 22
                font.bold: true
                color: "white"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Button {
                objectName: "guestPicturesAgain"
                text: qsTr("Look again")
                implicitHeight: 52
                font.pixelSize: 16
                enabled: !picker.busy
                onClicked: if (picker.controller) picker.controller.refreshGuestPictures()
            }
            Button {
                text: qsTr("Cancel")
                implicitHeight: 52
                font.pixelSize: 16
                onClicked: picker.close()
            }
        }
        Label {
            visible: text !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: picker.busy ? "#aab3c2" : "#f5b940"
            font.pixelSize: 16
            text: picker.busy ? qsTr("Copying the pictures from Dropbox…")
                : (picker.found.error ?? "") !== "" ? picker.found.error
                : picker.files.length === 0 ? qsTr("No pictures yet. Put JPEG pictures in Dropbox > POS project > Guest Screen (from a computer or the iPhone), then touch Look again.")
                : ""
        }
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / Math.max(2, Math.floor(width / 220)))
            cellHeight: Math.round(cellWidth * 0.75) + 34
            model: picker.files
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 14 }
            delegate: Item {
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                Rectangle {
                    anchors { fill: parent; margins: 6 }
                    radius: 8
                    color: tap.pressed ? "#2f6fd6" : "#262c35"
                    Image {
                        id: thumb
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                        height: Math.round(width * 0.5625)   // as the guest screen: 16:9
                        source: "file://" + modelData.path
                        sourceSize.width: 320
                        sourceSize.height: 180
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    Label {
                        anchors { left: parent.left; right: parent.right; top: thumb.bottom; margins: 6 }
                        text: modelData.name
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignHCenter
                        color: "white"
                        font.pixelSize: 13
                    }
                    TapHandler {
                        id: tap
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            picker.close()
                            picker.picked(modelData.path)
                        }
                    }
                }
            }
        }
    }
}
