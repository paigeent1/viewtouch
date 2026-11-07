import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Look Up Checks > ILSA history: the receipts imported from ILSA (vt-ilsa-history), read-only,
// marked ILSA. Find by day (2026-10-28), receipt # (#412345), UPC, total (12.34) or a word in an
// item's name; the one touched is shown as ILSA printed it; Reprint (with the buyer's date of birth
// as ILSA printed it: the store's copy, or without it). Shown over the Look Up Checks screen.
Rectangle {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    signal closed()
    color: "#1e232b"
    readonly property var info: pos && (pos.rangeReport.id ?? "") === "ilsa-receipts" ? pos.rangeReport : ({})
    readonly property var found: info.results ?? []
    readonly property var picked: info.selected ?? null
    property string query: ""
    property string store: ""   // "" this site's ILSA store, BHDAN / BHNM..., "*" every store's
    readonly property var range: info
    readonly property string face: "DejaVu Sans"
    readonly property color ink: "white"
    property var offsets: [0]
    property int index: -1
    readonly property real unit: Math.max(14, Math.min(width / 50, height / 26))
    readonly property real keyH: Math.max(w.unit * 2.4, w.zone ? w.zone.touch(48) : 48)
    function load(offset) { if (w.pos) w.pos.findLegacyReceipts(w.query, offset, w.store) }
    function reload() { w.offsets = [0]; w.index = -1; w.load(0) }
    function choose(i) {
        if (i < 0 || i >= found.length) return
        index = i
        list.positionViewAtIndex(i, ListView.Contain)
        w.pos.selectLegacyReceipt(found[i].id)
    }
    onFoundChanged: if (visible && found.length && (index < 0 || index >= found.length)) choose(0)
    onVisibleChanged: if (visible) { offsets = [0]; index = -1; Qt.callLater(load, 0) }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        // --- the receipt, as ILSA printed it ---
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: w.width * 0.42
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#fbf7ec"
                border.color: "#b59a5a"
                radius: 4
                Flickable {
                    id: paper
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentHeight: receiptText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    Text {
                        id: receiptText
                        objectName: "ilsaReceipt"
                        width: paper.width
                        text: w.picked ? (w.picked.text ?? "")
                                       : w.found.length ? "" : w.info.loading ? qsTr("Looking…") : (w.info.history ?? "")
                        font.family: "DejaVu Sans Mono"
                        font.pixelSize: Math.max(12, w.unit * 0.62)
                        color: "#5a3d0c"
                        wrapMode: w.picked ? Text.NoWrap : Text.WordWrap
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                Button { text: "▲"; implicitWidth: w.unit * 5; implicitHeight: Math.max(w.unit * 3, w.keyH); font.pixelSize: w.unit * 1.4
                         onClicked: paper.contentY = Math.max(0, paper.contentY - paper.height * 0.8) }
                Button { text: "▼"; implicitWidth: w.unit * 5; implicitHeight: Math.max(w.unit * 3, w.keyH); font.pixelSize: w.unit * 1.4
                         onClicked: paper.contentY = Math.min(Math.max(0, paper.contentHeight - paper.height), paper.contentY + paper.height * 0.8) }
            }
        }

        // --- the receipts found ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Label {
                text: qsTr("ILSA history (read-only)")
                color: "#f4c95d"
                font.pixelSize: w.unit * 1.1
                font.bold: true
            }
            // Which store's ILSA history (Store Settings' ILSA history store first), or every store's.
        RowLayout {
            visible: (w.range.stores ?? []).length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: w.keyH
            Layout.fillHeight: false
            spacing: w.unit * 0.3
            Text { text: qsTr("Store"); color: w.ink; font.family: w.face; font.pixelSize: w.unit * 0.8 }
            Repeater {
                model: (w.range.stores ?? []).concat(["*"])
                delegate: WidgetKey {
                    required property string modelData
                    objectName: "ilsaStore-" + modelData
                    Layout.preferredWidth: w.unit * 6
                    Layout.fillHeight: true
                    text: modelData === "*" ? qsTr("All stores") : modelData
                    fontScale: 0.34
                    baseColor: (w.range.store ?? "") === modelData ? "#1f8a4c" : "#2b3340"
                    onClicked: { w.store = modelData; Qt.callLater(w.reload) }
                }
            }
            Item { Layout.fillWidth: true }
        }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: search
                    objectName: "ilsaSearch"
                    Layout.fillWidth: true
                    implicitHeight: w.keyH
                    font.pixelSize: w.unit * 0.8
                    placeholderText: qsTr("Day (2026-10-28), BHDAN-836100, UPC, 12.34 or a name")
                    onAccepted: { w.query = text; w.offsets = [0]; w.index = -1; w.load(0) }
                }
                Button {
                    objectName: "ilsaFind"
                    text: qsTr("Find")
                    implicitHeight: w.keyH
                    font.pixelSize: w.unit * 0.8
                    onClicked: { w.query = search.text; w.offsets = [0]; w.index = -1; w.load(0) }
                }
                Button { text: qsTr("‹ Newer"); implicitHeight: w.keyH; font.pixelSize: w.unit * 0.8; enabled: w.offsets.length > 1
                         onClicked: { const o = w.offsets.slice(0, -1); w.offsets = o; w.index = -1; w.load(o[o.length - 1]) } }
                Button { text: qsTr("Older ›"); implicitHeight: w.keyH; font.pixelSize: w.unit * 0.8; enabled: w.info.more === true
                         onClicked: { const o = w.offsets.concat([w.info.nextOffset ?? 0]); w.offsets = o; w.index = -1; w.load(o[o.length - 1]) } }
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: w.found
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: w.unit * 3.4
                    radius: 3
                    color: row.index === w.index ? "#1d3fb8" : "#6b4e16"
                    border.color: "#3a2a08"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: w.unit
                        anchors.rightMargin: w.unit
                        ColumnLayout {
                            spacing: 0
                            Label { text: qsTr("ILSA  Rcpt  %1").arg(row.modelData.label ?? row.modelData.number ?? ""); color: row.index === w.index ? "#ffe066" : "white"; font.pixelSize: w.unit * 1.05; font.bold: true }
                            Label { text: qsTr("Total  %1").arg(row.modelData.total ?? "") + ((row.modelData.mode ?? 0) !== 0 ? qsTr("  (not a sale)") : "")
                                          + "   " + qsTr("Cashier %1").arg(((row.modelData.cashier ?? "") + " " + (row.modelData.cashierName ?? "")).trim())
                                    color: "white"; font.pixelSize: w.unit * 0.95 }
                        }
                        Item { Layout.fillWidth: true }
                        Label { text: row.modelData.when ?? ""; color: row.index === w.index ? "#ffe066" : "white"; font.pixelSize: w.unit * 0.95; font.bold: true }
                    }
                    TapHandler { onTapped: { if (TouchGuard.covered(point.scenePressPosition)) return; w.choose(row.index) } }
                }
                Label {
                    anchors.centerIn: parent
                    width: list.width * 0.9
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: list.count === 0
                    text: w.info.loading ? qsTr("Looking…") : (w.info.error ?? (w.info.history ?? ""))
                    font.pixelSize: w.unit
                    opacity: 0.7
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    objectName: "ilsaClose"
                    text: qsTr("‹ ViewTouch checks")
                    implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                    palette.button: "#b42318"; palette.buttonText: "white"
                    onClicked: w.closed()
                }
                Button { text: "▲"; implicitWidth: w.unit * 5; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit * 1.4; onClicked: w.choose(w.index - 1) }
                Button { text: "▼"; implicitWidth: w.unit * 5; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit * 1.4; onClicked: w.choose(w.index + 1) }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "ilsaReprint"
                    text: qsTr("Reprint…")
                    implicitWidth: w.unit * 9; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit * 1.05
                    palette.button: "#1f8a4c"; palette.buttonText: "white"
                    enabled: w.picked !== null
                    onClicked: whichCopy.open()
                }
            }
        }
    }

    // Which copy to reprint.
    Popup {
        id: whichCopy
        objectName: "ilsaWhichCopy"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: w.unit
        width: Math.min(w.width * 0.8, w.unit * 26)
        contentItem: ColumnLayout {
            spacing: w.unit * 0.6
            Label {
                text: w.picked ? qsTr("Reprint ILSA receipt %1").arg(w.picked.label ?? w.picked.number ?? "") : ""
                font.pixelSize: w.unit * 1.1; font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            Button {
                objectName: "ilsaDobCopy"
                visible: w.picked !== null && w.picked.hasDob === true
                Layout.fillWidth: true; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                text: qsTr("As ILSA printed it  (with date of birth)")
                palette.button: "#7c2d12"; palette.buttonText: "white"
                onClicked: { whichCopy.close(); w.pos.reprintLegacyReceipt(w.picked.id, true) }
            }
            Button {
                objectName: "ilsaPlainCopy"
                Layout.fillWidth: true; implicitHeight: w.unit * 3.4; font.pixelSize: w.unit
                text: qsTr("Without date of birth")
                palette.button: "#1f8a4c"; palette.buttonText: "white"
                onClicked: { whichCopy.close(); w.pos.reprintLegacyReceipt(w.picked.id, false) }
            }
            Button {
                Layout.fillWidth: true; implicitHeight: Math.max(w.unit * 2.6, w.keyH); font.pixelSize: w.unit * 0.9
                text: qsTr("Cancel")
                onClicked: whichCopy.close()
            }
        }
    }
}
