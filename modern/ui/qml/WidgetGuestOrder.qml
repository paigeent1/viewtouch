import QtQuick
import QtQuick.Layouts

// Customer Display page: the guest's order: their rewards (or "touch to add your phone"), each
// item with its choices and voids, then Subtotal, Discounts, each tax on its own line, Gratuity,
// Tip, Total, Paid, Change. Text sizes follow the zone's width; colors the zone's style.
// props.rewards false: no rewards box.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    // The check, while the cashier works it (zone.checkShown on the guest's screen).
    readonly property bool hasCheck: zone && zone.checkShown !== undefined ? zone.checkShown : !!pos && pos.hasCheck
    readonly property var lines: hasCheck ? pos.lines : []
    readonly property var totals: hasCheck ? pos.totals : ({})
    readonly property var prompt: pos ? pos.customerPrompt : ({})
    readonly property var loyalty: prompt.loyalty ?? ({})
    readonly property var props: zone && zone.props ? zone.props : ({})
    readonly property color accent: prompt.accent || "#2f6fd6"
    readonly property string face: zone && zone.st.font ? zone.st.font : "DejaVu Sans"
    readonly property color ink: zone && zone.st.textColor ? zone.st.textColor : "white"
    readonly property color soft: Qt.rgba(ink.r, ink.g, ink.b, 0.72)
    readonly property real unit: Math.max(11, Math.min(width / 18, height / 22))
    // The customer display around this page (the phone keypad); none in the editor.
    function host() {
        for (let p = w.parent; p; p = p.parent)
            if (p.guestHost === true) return p
        return null
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: w.unit * 0.6
        spacing: w.unit * 0.3

        // Rewards: their points, or join.
        Rectangle {
            visible: (w.loyalty.enabled ?? false) && w.props.rewards !== false
            Layout.fillWidth: true
            Layout.preferredHeight: w.unit * 4.2
            radius: w.unit * 0.5
            color: Qt.rgba(0, 0, 0, 0.18)
            border.color: (w.loyalty.member ?? "") !== "" ? "#f5b940" : w.accent
            border.width: 2
            Column {
                anchors.centerIn: parent
                width: parent.width - w.unit
                visible: (w.loyalty.member ?? "") !== ""
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: (w.loyalty.named ?? false) ? qsTr("Hi %1!").arg(w.loyalty.member ?? "")
                                                     : qsTr("Welcome! (%1)").arg(w.loyalty.member ?? "")
                    color: w.ink
                    font.family: w.face
                    font.pixelSize: w.unit
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: Math.round(w.unit * 0.6)
                    font.bold: true
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: !(w.loyalty.hasEmail ?? true) && w.props.email !== false
                    text: qsTr("✉ Touch to add your email for offers")
                    color: w.ink
                    font.family: w.face
                    font.pixelSize: w.unit * 0.75
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: Math.round(w.unit * 0.45)
                    font.underline: true
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("%1 points · +%2 today").arg(w.loyalty.points ?? 0).arg(w.loyalty.earning ?? 0)
                    color: "#f5b940"
                    font.family: w.face
                    font.pixelSize: w.unit * 0.85
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: Math.round(w.unit * 0.5)
                }
            }
            Text {
                anchors.fill: parent
                anchors.margins: w.unit * 0.4
                visible: (w.loyalty.member ?? "") === ""
                text: qsTr("Earn rewards: touch to add your phone")
                color: w.ink
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
                fontSizeMode: Text.Fit
                minimumPixelSize: Math.round(w.unit * 0.5)
                font.family: w.face
                font.pixelSize: w.unit
            }
            TapHandler {
                enabled: w.hasCheck
                onTapped: {
                    if (TouchGuard.covered(point.scenePressPosition)) return
                    const h = w.host()
                    if (!h || h.overlayOpen) return
                    if ((w.loyalty.member ?? "") === "")
                        h.askPhone(qsTr("Your phone number"), phone => w.pos.customerJoin(phone))
                    else if (!(w.loyalty.hasEmail ?? true) && w.props.email !== false)
                        h.askEmail(qsTr("Your email (for offers)"), email => w.pos.customerEmail(email))
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: w.lines
            spacing: w.unit * 0.3
            onCountChanged: positionViewAtEnd()   // the newest item stays in view
            delegate: Column {
                id: row
                required property var modelData
                width: ListView.view.width
                visible: !modelData.comment
                RowLayout {
                    width: parent.width
                    Text {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1   // wrapped text: its width from the layout, not its own (no recursive rearrange)
                        text: (row.modelData.quantity > 1 ? row.modelData.quantity + " × " : "") + row.modelData.name
                        color: row.modelData.voided ? "#e06c75" : w.ink
                        font.strikeout: row.modelData.voided ?? false
                        font.family: w.face
                        font.pixelSize: w.unit
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: row.modelData.voided ? qsTr("void") : row.modelData.price
                        color: row.modelData.voided ? "#e06c75" : w.ink
                        font.family: w.face
                        font.pixelSize: w.unit
                    }
                }
                Repeater {
                    model: row.modelData.modifiers ?? []
                    delegate: RowLayout {
                        required property var modelData
                        width: row.width
                        Text {
                            Layout.fillWidth: true
                            leftPadding: w.unit
                            text: "+ " + (modelData.name ?? modelData)
                            color: w.soft
                            font.family: w.face
                            font.pixelSize: w.unit * 0.8
                            elide: Text.ElideRight
                        }
                        Text { text: modelData.price ? "+" + modelData.price : ""; color: w.soft; font.family: w.face; font.pixelSize: w.unit * 0.8 }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 2; color: w.soft; opacity: 0.4 }
        component Line: RowLayout {
            property alias label: l.text
            property alias value: v.text
            property real size: 1.0
            property color valueInk: w.ink
            Layout.fillWidth: true
            Text { id: l; Layout.fillWidth: true; color: w.soft; font.family: w.face; font.pixelSize: w.unit * parent.size; elide: Text.ElideRight }
            Text { id: v; color: parent.valueInk; font.family: w.face; font.pixelSize: w.unit * parent.size }
        }
        Line { label: qsTr("Subtotal"); value: w.totals.items ?? "" }
        Line {
            label: qsTr("Discounts & promotions")
            value: w.totals.discounts ?? ""
            visible: w.totals.hasDiscount ?? false
            valueInk: "#7ee2a8"
        }
        // Each tax on its own (food and beverage, merchandise...), as the Order summary panel shows them.
        Line { label: qsTr("Tax"); value: w.totals.tax ?? ""; visible: (w.totals.taxLines ?? []).length === 0 }
        Repeater {
            model: w.totals.taxLines ?? []
            delegate: Line {
                required property var modelData
                label: modelData.name
                value: modelData.amount
            }
        }
        Line { label: qsTr("Gratuity"); value: w.totals.gratuity ?? ""; visible: w.totals.hasGratuity ?? false }
        Line { label: qsTr("Tip"); value: w.prompt.tip || w.totals.tips || ""; visible: (w.prompt.tipChosen ?? false) || (w.totals.hasTips ?? false) }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 2; color: w.soft; opacity: 0.4 }
        Line { label: qsTr("Total"); value: w.totals.total ?? ""; size: 1.9 }
        Line { label: qsTr("Paid"); value: w.totals.paid ?? ""; visible: w.hasCheck && w.pos.payments.length > 0 }
        Line { label: qsTr("Change"); value: w.totals.change ?? ""; visible: w.totals.hasChange ?? false; size: 1.4 }
    }
}
