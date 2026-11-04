import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Site Setup (Hardware Configuration > Site Setup): this Pi's hardware on one screen, in framed boxes
// like the old ILSA settings screen: the site, the scanner, the guest display, tablets, TVs, and the
// way to printers and receipts. Saved in this Pi's own files (~/.config/viewtouch, the same ones
// `vt-site` and `vt-directv` use); store-wide settings stay in Store Settings. Admins.
Item {
    id: w
    property ZoneItem zone
    readonly property LayoutController controller: zone ? zone.controller : null
    readonly property PosService pos: zone ? zone.pos : null
    readonly property bool admin: pos !== null && pos.can("admin")
    // Read again whenever a tool finishes, a scan comes in or the settings are saved.
    readonly property var site: controller && controller.siteRevision >= 0 ? controller.siteSettings() : ({})
    property var draft: ({})
    property var tablets: []
    property bool dirty: false
    function reload() {
        draft = { serverAddr: site.serverAddr ?? "", serverPort: site.serverPort ?? 7719,
                  scanner: site.scanner ?? "", scannerBaud: site.scannerBaud ?? 9600 }
        tablets = (site.tablets ?? []).map(t => ({ name: t.name, ip: t.ip }))
        // Typing replaces a field's binding: put the saved values back by hand.
        serverAddrField.text = draft.serverAddr
        serverPortBox.value = draft.serverPort
        dirty = false
    }
    function set(key, value) { const d = Object.assign({}, draft); d[key] = value; draft = d; dirty = true }
    function setTablet(i, key, value) { const t = tablets.slice(); t[i] = Object.assign({}, t[i]); t[i][key] = value; tablets = t; dirty = true }
    function save() {
        const v = Object.assign({}, draft)
        v.tablets = tablets
        v.siteName = storeName   // the name is Store Settings' (the scripts get a copy)
        if (controller.saveSiteSettings(v)) dirty = false
    }
    // The store's name: Store Settings > The store (one place for it); site.conf keeps a copy for vt-site.
    readonly property string storeName: pos ? pos.storeName : ""
    function syncName() {
        if (admin && controller && (site.configured ?? false) && storeName !== "" && (site.siteName ?? "") !== storeName)
            controller.saveSiteSettings({ siteName: storeName })
    }
    onStoreNameChanged: syncName()
    Component.onCompleted: { reload(); syncName() }
    onSiteChanged: if (!dirty) reload()
    function when(ms) { return ms ? Qt.locale().toString(new Date(ms), "h:mm:ss AP") : "" }
    function run(action) { if (controller) controller.siteAction(action) }

    // A titled frame, like the boxes on the old settings screen.
    component Box: GroupBox {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 1
        font.pixelSize: 17
        font.bold: true
    }
    component Caption: Label { font.pixelSize: 14; font.bold: false; opacity: 0.75; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    component Tool: Button { Layout.fillWidth: true; implicitHeight: 46; font.pixelSize: 15; font.bold: false; enabled: w.admin }

    Label {
        anchors.centerIn: parent
        visible: (w.site.android ?? false) || !w.admin
        text: (w.site.android ?? false) ? qsTr("Site Setup works on the Pi itself, not on a tablet.")
                                        : qsTr("Site Setup is for admins.")
        font.pixelSize: 20
        opacity: 0.7
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        visible: !(w.site.android ?? false) && w.admin

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: grid.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            GridLayout {
                id: grid
                width: flick.width - 14
                columns: width > 1500 ? 4 : width > 1000 ? 3 : width > 640 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                // --- the site ---
                Box {
                    title: qsTr("This site")
                    ColumnLayout {
                        anchors.fill: parent
                        Caption { text: qsTr("Store name (Store Settings > The store)") }
                        Label {
                            objectName: "siteName"
                            Layout.fillWidth: true
                            font.pixelSize: 18; font.bold: true
                            elide: Text.ElideRight
                            text: w.storeName || qsTr("(not set)")
                        }
                        Tool { text: qsTr("Change in Store Settings"); onClicked: w.controller.jumpTo("admin-store") }
                        Caption { text: qsTr("This Pi's address: %1").arg(w.site.thisAddress || qsTr("(no network)")) }
                        Caption { text: qsTr("Server address for pairing tablets (empty: this Pi)") }
                        TextField {
                            id: serverAddrField
                            Layout.fillWidth: true; font.pixelSize: 16; font.bold: false
                            placeholderText: w.site.thisAddress ?? ""
                            onTextEdited: w.set("serverAddr", text.trim())
                        }
                        Caption { text: qsTr("Server port") }
                        SpinBox {
                            id: serverPortBox
                            Layout.fillWidth: true; font.bold: false
                            from: 1; to: 65535; editable: true
                            textFromValue: (v, locale) => String(v)
                            onValueModified: w.set("serverPort", value)
                        }
                    }
                }

                // --- the scanner ---
                Box {
                    title: qsTr("Scanner")
                    ColumnLayout {
                        anchors.fill: parent
                        Caption { text: qsTr("Scanner port") }
                        ComboBox {
                            id: portBox
                            objectName: "scannerPort"
                            Layout.fillWidth: true; font.pixelSize: 15; font.bold: false
                            // Auto, Off, then the ports this Pi has now (the saved one kept if it's unplugged).
                            readonly property var choices: {
                                const out = [{ path: "", label: qsTr("Auto: the serial adapter's first port") },
                                             { path: "off", label: qsTr("Off (a USB scanner that types needs none)") }]
                                for (const p of (w.site.scannerPorts ?? [])) out.push(p)
                                const cur = w.draft.scanner ?? ""
                                if (cur !== "" && !out.some(p => p.path === cur)) out.push({ path: cur, label: cur + qsTr(" (not plugged in)") })
                                return out
                            }
                            model: choices.map(p => p.label)
                            currentIndex: Math.max(0, choices.findIndex(p => p.path === (w.draft.scanner ?? "")))
                            onActivated: i => w.set("scanner", choices[i].path)
                        }
                        Caption { text: qsTr("Speed (baud)") }
                        ComboBox {
                            Layout.fillWidth: true; font.pixelSize: 15; font.bold: false
                            readonly property var bauds: [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
                            model: bauds.map(String)
                            currentIndex: Math.max(0, bauds.indexOf(Number(w.draft.scannerBaud ?? 9600)))
                            onActivated: i => w.set("scannerBaud", bauds[i])
                        }
                        Caption { text: qsTr("Test: scan anything with this screen showing.") }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 52
                            radius: 6
                            color: w.site.lastScan ? "#1f3b2a" : "#2a313d"
                            Label {
                                objectName: "lastScan"
                                anchors.fill: parent; anchors.margins: 8
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                font.pixelSize: 16; font.bold: false
                                text: w.site.lastScan ? qsTr("%1  at %2").arg(w.site.lastScan).arg(w.when(w.site.lastScanAt))
                                                      : qsTr("Nothing scanned yet")
                            }
                        }
                        Tool { text: qsTr("Restart ViewTouch (new port or speed)"); onClicked: w.run("restart") }
                    }
                }

                // --- the guest display ---
                Box {
                    title: qsTr("Guest display (customer screen)")
                    ColumnLayout {
                        anchors.fill: parent
                        Caption {
                            text: (w.site.guestDisplay ?? false) ? qsTr("On: the second monitor shows the guest's order.")
                                                                 : qsTr("Off: the second monitor (if any) mirrors the POS.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Tool { text: qsTr("Turn on"); enabled: w.admin && !(w.site.guestDisplay ?? false); onClicked: w.run("guest-on") }
                            Tool { text: qsTr("Turn off"); enabled: w.admin && (w.site.guestDisplay ?? false); onClicked: w.run("guest-off") }
                        }
                        Caption { text: qsTr("Turning it on or off restarts ViewTouch.") }
                        Tool { objectName: "learnTouch"; text: qsTr("Learn guest touchscreen"); enabled: w.admin && (w.site.guestDisplay ?? false); onClicked: w.run("learn-touch") }
                        Caption { text: w.site.guestTouchPort ? qsTr("Guest touchscreen: USB port %1").arg(w.site.guestTouchPort)
                                                              : qsTr("Guest touchscreen not learned yet.") }
                        Tool { text: qsTr("Messages and pictures (Store Settings)"); onClicked: w.controller.jumpTo("admin-store") }
                    }
                }

                // --- printers and receipts: store-wide, in their own screens ---
                Box {
                    title: qsTr("Printers and receipts")
                    ColumnLayout {
                        anchors.fill: parent
                        Caption { text: qsTr("Set for the whole store in their own screens:") }
                        Tool { text: qsTr("Printers and cash drawer"); onClicked: w.controller.jumpTo("admin-printers") }
                        Tool { text: qsTr("Receipt header, footer, privacy notice"); onClicked: w.controller.jumpTo("admin-store") }
                        Tool { text: qsTr("Terminals (screens and tablets)"); onClicked: w.controller.jumpTo("admin-terminals") }
                        Caption { text: qsTr("Label printers: Manager > Label Utility.") }
                    }
                }

                // --- tablets ---
                Box {
                    title: qsTr("Tablets")
                    Layout.columnSpan: grid.columns > 1 ? 2 : 1
                    ColumnLayout {
                        anchors.fill: parent
                        Caption { text: qsTr("Each tablet's name and fixed address (reserve it in the router). Used for app updates and status.") }
                        Repeater {
                            model: w.tablets
                            delegate: RowLayout {
                                id: trow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                TextField {
                                    Layout.fillWidth: true; Layout.preferredWidth: 2; font.pixelSize: 15; font.bold: false
                                    text: trow.modelData.name; placeholderText: qsTr("Bar 1")
                                    onTextEdited: w.setTablet(trow.index, "name", text)
                                }
                                TextField {
                                    Layout.fillWidth: true; Layout.preferredWidth: 2; font.pixelSize: 15; font.bold: false
                                    text: trow.modelData.ip; placeholderText: qsTr("192.168.1.220")
                                    inputMethodHints: Qt.ImhPreferNumbers
                                    onTextEdited: w.setTablet(trow.index, "ip", text.trim())
                                }
                                Button {
                                    text: "✕"; implicitWidth: 46; implicitHeight: 40; font.bold: false
                                    onClicked: { const t = w.tablets.slice(); t.splice(trow.index, 1); w.tablets = t; w.dirty = true }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Tool { objectName: "addTablet"; text: qsTr("+ Tablet"); onClicked: { w.tablets = w.tablets.concat([{ name: qsTr("Bar %1").arg(w.tablets.length + 1), ip: "" }]); w.dirty = true } }
                            Tool { text: qsTr("Check tablets"); enabled: w.admin && !w.dirty; onClicked: w.run("check-tablets") }
                            Tool { text: qsTr("Send app update"); enabled: w.admin && !w.dirty; onClicked: w.run("push-tablets") }
                        }
                        Caption { visible: w.dirty; text: qsTr("Save first, then check or update the tablets.") }
                    }
                }

                // --- TVs ---
                Box {
                    title: qsTr("TVs and DIRECTV")
                    Layout.columnSpan: grid.columns > 2 ? 2 : 1
                    ColumnLayout {
                        anchors.fill: parent
                        Caption { text: (w.site.tvs ?? []).length ? qsTr("%n TV(s) and receiver(s):", "", (w.site.tvs ?? []).length)
                                                                  : qsTr("None yet. Scan finds DIRECTV receivers and smart TVs on this Pi's network.") }
                        Repeater {
                            model: w.site.tvs ?? []
                            delegate: Label {
                                required property var modelData
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 15; font.bold: false
                                text: modelData.name + "  ·  " + modelData.kind + "  ·  " + modelData.ip
                            }
                        }
                        Tool { text: qsTr("Scan the network for TVs"); onClicked: w.run("scan-tvs") }
                        Caption { text: qsTr("DIRECTV receivers need External Device access allowed (Menu > Settings > Whole-Home).") }
                    }
                }

                // --- coming ---
                Box {
                    title: qsTr("Coming later")
                    ColumnLayout {
                        anchors.fill: parent
                        Caption { text: qsTr("Driver's license reader (ID checks): uses the scanner above.") }
                        Caption { text: qsTr("Scale and scale scanner: not built yet.") }
                        Caption { text: qsTr("Card terminal (Global Payments): not built yet.") }
                    }
                }
            }
        }

        // --- Save / Cancel ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: w.dirty ? qsTr("Changes not saved yet.")
                              : (w.site.configured ?? false) ? qsTr("Saved on this Pi (~/.config/viewtouch).")
                                                             : qsTr("Not set up on this Pi yet: fill in and Save.")
                font.pixelSize: 15
                opacity: 0.75
            }
            Button {
                text: qsTr("Cancel"); implicitWidth: 160; implicitHeight: 54; font.pixelSize: 17
                enabled: w.dirty
                onClicked: w.reload()
            }
            Button {
                objectName: "saveSite"
                text: qsTr("Save"); implicitWidth: 200; implicitHeight: 54; font.pixelSize: 18; font.bold: true
                enabled: w.dirty && w.admin
                highlighted: true
                onClicked: w.save()
            }
        }
    }
}
