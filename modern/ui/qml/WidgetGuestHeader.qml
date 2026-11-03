import QtQuick

// Customer Display page: the bar across the top: the store's logo (or name) on the left,
// the check ("Quick 45 · #45") on the right during a sale. Its color: the Customer display
// color (Store Settings), or the zone's own fill with props.accent false.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property var prompt: pos ? pos.customerPrompt : ({})
    readonly property string face: zone && zone.st.font ? zone.st.font : "DejaVu Sans"
    readonly property color ink: zone && zone.st.textColor ? zone.st.textColor : "white"
    readonly property real unit: Math.max(10, height / 3.4)
    function img(ref) { return pos ? (pos.imageRevision < 0 ? undefined : pos.imageUrl(ref)) : ref }

    Rectangle {
        anchors.fill: parent
        visible: !(w.zone && w.zone.props && w.zone.props.accent === false)
        color: w.prompt.accent || "#2f6fd6"
    }
    Image {
        id: logo
        visible: !!w.prompt.logo
        anchors { left: parent.left; leftMargin: w.unit; verticalCenter: parent.verticalCenter }
        height: parent.height * 0.75
        width: parent.width * 0.4
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
        source: w.prompt.logo ? w.img(w.prompt.logo) : ""
    }
    Text {
        visible: !w.prompt.logo
        anchors { left: parent.left; leftMargin: w.unit; verticalCenter: parent.verticalCenter }
        text: w.pos ? w.pos.storeName : ""
        color: w.ink
        font.family: w.face
        font.pixelSize: w.unit * 1.6
        font.bold: true
    }
    Text {
        anchors { right: parent.right; rightMargin: w.unit; verticalCenter: parent.verticalCenter }
        visible: !!w.pos && w.pos.hasCheck
        text: (w.prompt.label ?? "") + (w.prompt.checkId ? "  ·  #" + w.prompt.checkId : "")
        color: w.ink
        font.family: w.face
        font.pixelSize: w.unit * 1.2
    }
}
