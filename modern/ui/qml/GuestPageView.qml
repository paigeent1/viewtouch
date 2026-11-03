import QtQuick

// The Customer Display page (Edit Pages -> Customer Display) on the guest's screen, scaled to
// fit like any page. Its widgets (guestOrder, guestMessages, guestMedia, guestHeader, labels,
// pictures) show the cashier's check; its buttons don't run (the guest's screen must not move
// the cashier's).
Item {
    id: view
    required property LayoutController controller
    readonly property size canvas: controller.guestCanvas
    readonly property real scaleFactor: Math.min(width / Math.max(1, canvas.width), height / Math.max(1, canvas.height))
    readonly property var bg: controller.guestBackground

    Rectangle { anchors.fill: parent; color: view.bg.fill ?? "#0f1318" }

    Item {
        id: surface
        width: view.canvas.width
        height: view.canvas.height
        scale: view.scaleFactor
        transformOrigin: Item.TopLeft
        x: (view.width - width * scale) / 2
        y: (view.height - height * scale) / 2
        clip: true

        Rectangle { anchors.fill: parent; color: view.bg.fill ?? "#0f1318" }
        Image {
            anchors.fill: parent
            visible: !!view.bg.texture
            source: view.bg.texture ? "qrc:/textures/" + view.bg.texture + ".xpm" : ""
            fillMode: Image.Tile
        }
        Image {
            anchors.fill: parent
            readonly property var pos: view.controller.pos
            visible: !!view.bg.image && status === Image.Ready
            source: !view.bg.image ? "" : pos ? (pos.imageRevision < 0 ? undefined : pos.imageUrl(view.bg.image)) : view.bg.image
            asynchronous: true
            fillMode: ({ fit: Image.PreserveAspectFit, stretch: Image.Stretch, tile: Image.Tile,
                         center: Image.Pad })[view.bg.imageFit] ?? Image.PreserveAspectCrop
        }

        Repeater {
            model: view.controller.guestZones
            delegate: ZoneItem {
                controller: view.controller
                pos: view.controller.pos
                screenScale: view.scaleFactor
                selectedZoneId: ""
                editing: false
                editSelected: false
                dragDX: 0
                dragDY: 0
                previewRect: null
                previewRotation: null
            }
        }
    }
}
