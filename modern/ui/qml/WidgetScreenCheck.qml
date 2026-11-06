import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Screen check (Hardware Configuration > Screen check): which monitor is the register and which the
// guest's screen, which HDMI port each is on, which touchscreen touches which monitor, and plain
// warnings when something's off, with the fixes as buttons. So anyone at the store can check and
// fix the screens without a call. What it shows comes from vt-screens (xrandr / xinput on the Pi)
// and from ViewTouch itself (which of its windows is on which monitor). Looking is for anyone on
// this page; the fixes are for admins.
Item {
    id: w
    property ZoneItem zone
    readonly property LayoutController controller: zone ? zone.controller : null
    readonly property PosService pos: zone ? zone.pos : null
    readonly property bool admin: pos !== null && pos.can("admin")
    readonly property var info: controller && controller.siteRevision >= 0 ? controller.screenCheck() : ({})
    readonly property var outputs: info.outputs ?? []
    readonly property var touches: info.touch ?? []
    readonly property var problems: info.problems ?? []
    readonly property var qtScreens: info.qtScreens ?? []
    property bool identifying: false

    Component.onCompleted: if (controller && !(info.android ?? false)) controller.refreshScreens()
    function run(action) { if (controller) controller.siteAction(action) }
    function when(ms) { return ms ? Qt.locale().toString(new Date(ms), "h:mm:ss AP") : "" }
    function inches(o) {
        return o.mmW ? Math.round(Math.sqrt(o.mmW * o.mmW + o.mmH * o.mmH) / 25.4) + "″" : ""
    }
    function role(name) {
        return name === info.register ? qsTr("REGISTER") : name === info.guest ? qsTr("GUEST SCREEN") : ""
    }
    // ViewTouch's own windows on that monitor (Qt names the monitors like xrandr does).
    function windowsOn(name) {
        for (const s of qtScreens) if (s.name === name) return s.windows ?? []
        return []
    }
    // A port's plain place on the Pi 5: HDMI-1 (HDMI0 on the case) is the one next to the USB-C power.
    function where(name) {
        if (/^HDMI-?A?-?1$/.test(name)) return qsTr("the HDMI port next to the USB-C power plug")
        if (/^HDMI-?A?-?2$/.test(name)) return qsTr("the other HDMI port (farther from the power plug)")
        return ""
    }

    component Box: GroupBox {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        font.pixelSize: 17
        font.bold: true
    }
    component Caption: Label { font.pixelSize: 14; font.bold: false; opacity: 0.75; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    component Tool: Button { implicitHeight: 64; Layout.minimumHeight: 64; Layout.minimumWidth: 200; font.pixelSize: 16; font.bold: false }

    Label {
        anchors.centerIn: parent
        visible: w.info.android ?? false
        text: qsTr("Screen check works on the Pi itself, not on a tablet.")
        font.pixelSize: 20
        opacity: 0.7
    }

    TouchScrollColumn {
        anchors.fill: parent
        visible: !(w.info.android ?? false)
        spacing: 12

            // --- the verdict, then look again ---
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: verdict.implicitHeight + 24
                radius: 6
                color: w.problems.length ? "#4a2323" : (w.outputs.length ? "#1f3b2a" : "#2a313d")
                ColumnLayout {
                    id: verdict
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                    spacing: 6
                    Label {
                        font.pixelSize: 20
                        font.bold: true
                        color: "white"
                        text: w.info.busy && !w.outputs.length ? qsTr("Looking at the screens...")
                              : w.problems.length ? qsTr("%n thing(s) to fix:", "", w.problems.length)
                              : w.outputs.length ? qsTr("All good: register on %1%2").arg(w.info.register ?? "")
                                                   .arg(w.info.guest ? qsTr(", guest screen on %1").arg(w.info.guest) : "")
                              : qsTr("No screens found yet.")
                    }
                    Repeater {
                        model: w.problems
                        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 16; color: "#ffd0d0"; text: "• " + modelData }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Tool { text: (w.info.busy ?? false) ? qsTr("Looking...") : qsTr("Look again"); enabled: !(w.info.busy ?? false); onClicked: w.controller.refreshScreens() }
                Tool { objectName: "identify"; text: qsTr("Show each monitor's name on it"); onClicked: { w.identifying = true; identifyTimer.restart() } }
                Caption { text: w.info.checkedAt ? qsTr("Checked at %1").arg(w.when(w.info.checkedAt)) : "" }
            }

            // --- each monitor ---
            Box {
                title: qsTr("Monitors (%1)").arg(w.outputs.length)
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    Caption { visible: !w.outputs.length; text: qsTr("No monitor found. Check that each is plugged in and turned on, then Look again.") }
                    Repeater {
                        model: w.outputs
                        Rectangle {
                            id: card
                            required property var modelData
                            readonly property string r: w.role(modelData.name)
                            Layout.fillWidth: true
                            implicitHeight: cardRow.implicitHeight + 20
                            radius: 6
                            color: r === qsTr("REGISTER") ? "#1e3355" : r ? "#3a2a4f" : "#2a313d"
                            border.color: "#55607a"
                            RowLayout {
                                id: cardRow
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                                spacing: 16
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        color: "white"; font.pixelSize: 22; font.bold: true
                                        text: card.modelData.name + (card.r ? "  —  " + card.r : "")
                                    }
                                    Label {
                                        color: "#d8dde8"; font.pixelSize: 15
                                        text: [card.modelData.w + " × " + card.modelData.h, card.modelData.rate, w.inches(card.modelData),
                                               card.modelData.model, card.modelData.primary ? qsTr("main (primary) monitor") : ""]
                                              .filter(x => x).join("  ·  ")
                                    }
                                    Label { color: "#aab3c5"; font.pixelSize: 14; visible: text !== ""; text: w.where(card.modelData.name) }
                                    Label {
                                        color: "#aab3c5"; font.pixelSize: 14
                                        text: w.windowsOn(card.modelData.name).length
                                              ? qsTr("Showing: %1").arg(w.windowsOn(card.modelData.name).join(", "))
                                              : qsTr("No ViewTouch window on it.")
                                    }
                                }
                                ColumnLayout {
                                    spacing: 6
                                    Tool {
                                        text: card.r === qsTr("REGISTER") ? qsTr("Make this the register again") : qsTr("Make this the register")
                                        enabled: w.admin && w.outputs.length > 1
                                        Layout.preferredWidth: 300
                                        onClicked: w.run("register:" + card.modelData.name)
                                    }
                                    RowLayout {
                                        spacing: 6
                                        Tool {
                                            text: qsTr("Best mode")
                                            enabled: w.admin
                                            Layout.preferredWidth: 147
                                            onClicked: w.run("best:" + card.modelData.name)
                                        }
                                        Tool {
                                            text: qsTr("Change…")
                                            enabled: w.admin && (card.modelData.modes ?? []).length > 0
                                            Layout.preferredWidth: 147
                                            onClicked: { modePicker.output = card.modelData; modePicker.open() }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Caption {
                        text: qsTr("'Make this the register' puts the register on that monitor and the guest's screen on the other, "
                                   + "remembers it after a reboot, and restarts ViewTouch. Admins.")
                    }
                }
            }

            // --- each touchscreen ---
            Box {
                title: qsTr("Touchscreens (%1)").arg(w.touches.length)
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    Caption {
                        visible: !w.touches.length
                        text: w.info.session === "wayland" ? qsTr("Touchscreens can't be checked on the Wayland desktop.")
                                                           : qsTr("No touchscreen found. Check its USB cable.")
                    }
                    Repeater {
                        model: w.touches
                        Label {
                            required property var modelData
                            readonly property bool wrong: modelData.landsOn && modelData.shouldLandOn && modelData.landsOn !== modelData.shouldLandOn
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 16
                            font.bold: false
                            color: wrong ? "#ff9a9a" : palette.windowText
                            text: qsTr("%1%2 (USB %3): touches land on %4%5")
                                  .arg(modelData.name).arg(modelData.guest ? qsTr(" — guest's") : "")
                                  .arg(modelData.port || "?").arg(modelData.landsOn || "?")
                                  .arg(wrong ? qsTr(", should be %1").arg(modelData.shouldLandOn) : " ✓")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Tool { text: qsTr("Tie touchscreens again"); enabled: w.admin && w.touches.length > 0; onClicked: w.run("touch-map") }
                        Tool { text: qsTr("Learn guest touchscreen"); enabled: w.admin && (w.info.guestOn ?? false); onClicked: w.run("learn-touch") }
                    }
                    Caption {
                        text: qsTr("Both touchscreens report the same name, so the Pi tells them apart by USB port. 'Learn guest touchscreen': "
                                   + "touch the guest's screen within 20 seconds when asked. Keep each touch cable in the same USB port afterwards.")
                    }
                }
            }

            // --- what the words mean ---
            Box {
                title: qsTr("If the screens are swapped")
                ColumnLayout {
                    anchors.fill: parent
                    Caption {
                        text: qsTr("1. Tap 'Show each monitor's name on it' to see which port is which.\n"
                                   + "2. On the monitor that should be the register, tap 'Make this the register'. ViewTouch restarts.\n"
                                   + "3. If a touch lands on the wrong screen, tap 'Tie touchscreens again'; if it's still wrong, 'Learn guest touchscreen'.\n"
                                   + "4. If a monitor flickers, looks blurry or doesn't fill the screen, tap its 'Best mode' (or 'Change\u2026' to pick one).\n"
                                   + "The Pi checks the screens by itself every 10 minutes and says so on the status line when something needs fixing.\n"
                                   + "From the Pi's terminal the same is: vt-screens (report), vt-guest-display on HDMI-1, vt-screen-mode HDMI-1 best, vt-touch-map.")
                    }
                }
            }
    }

    // "Change...": the sizes and rates that monitor offers; the one picked is kept after reboots.
    Popup {
        id: modePicker
        property var output: ({})
        readonly property var choices: {
            const out = []
            for (const m of (output.modes ?? []))
                for (const r of (m.rates ?? []))
                    out.push({ size: m.size, rate: Number(r).toFixed(2), preferred: m.preferred, current: m.current,
                               label: m.size.replace("x", " × ") + "  at " + Math.round(r) + " Hz"
                                      + (m.preferred && r === m.rates[0] ? qsTr("  (its best)") : "")
                                      + (m.current && Math.round(r) === parseInt(output.rate) ? qsTr("  ← now") : "") })
            return out
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 40 : 700, 700)
        height: Math.min(parent ? parent.height - 40 : 700, 760)
        modal: true
        padding: 16
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 20
                font.bold: true
                text: qsTr("%1: pick a size and refresh rate").arg(modePicker.output.name ?? "")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 14
                opacity: 0.75
                text: qsTr("60 Hz is steady; under 55 Hz can flicker. Kept after reboots. 'Best mode' goes back to the monitor's own choice.")
            }
            ListView {
                id: modeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: modePicker.choices
                ScrollBar.vertical: TouchScrollBar { needed: modeList.contentHeight > modeList.height + 1 }
                delegate: Tool {
                    required property var modelData
                    width: modeList.width - 40
                    text: modelData.label
                    font.bold: modelData.current
                    onClicked: { w.run("mode:" + modePicker.output.name + ":" + modelData.size + ":" + modelData.rate); modePicker.close() }
                }
            }
            Tool { text: qsTr("Cancel"); Layout.alignment: Qt.AlignRight; onClicked: modePicker.close() }
        }
    }

    // Each monitor's name in big letters on that monitor, for a few seconds.
    Timer { id: identifyTimer; interval: 6000; onTriggered: w.identifying = false }
    Instantiator {
        model: w.identifying ? Qt.application.screens : []
        delegate: Window {
            required property var modelData
            screen: modelData
            flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
            width: 900
            height: 320
            x: modelData.virtualX + (modelData.width - width) / 2
            y: modelData.virtualY + (modelData.height - height) / 2
            color: "#101828"
            visible: true
            Column {
                anchors.centerIn: parent
                spacing: 10
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: "white"; font.pixelSize: 96; font.bold: true; text: modelData.name }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: "#9ec5ff"; font.pixelSize: 44
                    text: w.role(modelData.name) || qsTr("(not used)")
                }
            }
            MouseArea { anchors.fill: parent; onClicked: w.identifying = false }
        }
    }
}
