import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Manager editor for one kind of record (props.panel: menu | employees |
// tenders | printers | taxes | store | terminals | mealPeriods | billboards). List on the left, form
// on the right; the form's fields come from the service, like the page inspector.
// Billboards (Store Settings > Billboards; WidgetBillboards.qml): each TV's row says its address and
// whether it's online, on or dark, muted; its form adds Deploy / Update app, Restart app, Identify
// and Remove (a manager's PIN), and + Add is a step-by-step for a Fire TV (name and address, ADB
// debugging, then Deploy with its progress).
Item {
    id: w
    focus: true
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    // Set by a widget that is this panel (WidgetBillboards: "billboards"); else the zone's props.panel.
    property string forcedPanel: ""
    readonly property string panel: forcedPanel !== "" ? forcedPanel
                                  : zone && zone.props && zone.props.panel ? zone.props.panel : "menu"
    readonly property bool single: panel === "taxes" || panel === "store"

    readonly property var fields: {
        // Compared, not just read: an unused read is compiled away (no refresh).
        if (!pos || pos.adminRevision < 0 || pos.queryRevision < 0) return []
        return pos.adminFields(panel)
    }
    readonly property var records: {
        if (!pos || pos.adminRevision < 0 || pos.queryRevision < 0) return []
        return pos.adminRecords(panel)
    }

    property int index: -2       // -2 nothing chosen, -1 new record
    property var draft: ({})
    property bool dirty: false
    property bool armDelete: false

    function copy(o) { return o ? JSON.parse(JSON.stringify(o)) : ({}) }
    onRecordsChanged: {
        if (single && records.length > 0 && (!dirty || Object.keys(draft).length <= 2))
            choose(0)
    }
    function choose(i) {
        index = i
        draft = copy(i >= 0 ? records[i] : pos.adminNewRecord(panel))
        dirty = false
        armDelete = false
    }

    // Changed here, not saved yet.
    readonly property var saved: index >= 0 && index < records.length ? records[index] : null
    function differs(path) {
        return saved !== null && JSON.stringify(draft[path] ?? "") !== JSON.stringify(saved[path] ?? "")
    }
    function changedIn(key) {
        if (!dirty) return 0
        return fields.filter(f => (key === undefined || f.groupKey === key) && differs(f.path)).length
    }
    readonly property int changes: changedIn()

    // --- billboards ---------------------------------------------------------------------------
    readonly property bool boards: panel === "billboards"
    // Connected now, and its app (Manager -> Network's list, live).
    function liveOf(name) {
        const list = w.pos && w.pos.network ? (w.pos.network.terminals ?? []) : []
        return list.find(t => t.name === name) ?? null
    }
    function boardOnline(r) { return r ? (w.liveOf(r.name) !== null || (w.pos && !w.pos.network.terminals && r.online === true)) : false }
    function boardState(r) { return !r.enabled ? qsTr("off") : r.showing ? qsTr("on") : qsTr("dark") }
    function boardDetail(r) {
        if (!r._board) return r._detail ?? ""
        const out = [r.ip ? r.ip : qsTr("no IP yet"), w.boardOnline(r) ? qsTr("online") : qsTr("offline"), w.boardState(r)]
        if (r.muted) out.push(qsTr("muted"))
        return out.join(" · ")
    }
    function boardStatus(r) {
        if (!r) return ""
        const live = w.liveOf(r.name)
        const out = [w.boardOnline(r) ? qsTr("Online") : qsTr("Offline"), w.boardState(r),
                     r.muted ? qsTr("muted") : qsTr("sound %1%").arg(r.loudness)]
        const version = live ? (live.version ?? "") : (r.version ?? "")
        if (version !== "") out.push(qsTr("app %1").arg(version))
        if (r.now !== "" && r.nowUntil > 0)
            out.push((r.now === "on" ? qsTr("on now until %1") : qsTr("off now until %1"))
                     .arg(Qt.formatDateTime(new Date(r.nowUntil), "ddd h:mm AP")))
        return out.join(" · ")
    }
    // The job running (or last run) for this billboard: deploy / restart, with its lines.
    readonly property var boardJob: boards && saved ? (saved.job ?? null) : null
    // A billboard chosen (not the defaults row).
    readonly property bool boardOpen: boards && index >= 0 && draft._board === true
    // Preview: the arrangement as the TV would play it (the form's, saved or not); videos muted,
    // and only those on this computer (the store's); pictures the store's.
    readonly property var previewSlides: {
        if (!boardOpen) return []
        const text = draft.ownContent ? String(draft.slides ?? "") : String(draft._guestSlides ?? "")
        return text.split("\n").map(x => x.trim()).filter(x => x !== "").concat(draft._promotions ?? [])
    }
    function openPreview() { if (boardOpen) previewBox.open() }
    // Apply to...: the other billboards, and the guest screen, ticked.
    property var applyPicked: ({})
    function openApplyTo() { if (boardOpen) { applyPicked = ({}); applyBox.open() } }
    function applyTargets() { return Object.keys(applyPicked).filter(k => applyPicked[k]) }
    // + Add: the step-by-step (1 name and address, 2 ADB debugging on the Fire TV, 3 Deploy).
    property bool adding: false
    property int addStep: 1
    property string addName: ""
    property string addIp: ""
    property bool addStarted: false
    property bool notFireTv: false     // tapped "This TV isn't a Fire TV" (Google TV: a pairing code)
    readonly property var addRecord: adding ? (records.find(r => r.name === addName.trim()) ?? null) : null
    readonly property var addJob: addRecord && addStarted ? (addRecord.job ?? null) : null
    readonly property bool addAskPairing: notFireTv || (addRecord !== null && addRecord._askPairing === true)
    function startAdding() {
        adding = true
        addStep = 1
        addName = ""
        addIp = ""
        addStarted = false
        notFireTv = false
        index = -2
    }
    // Saved (new, or its name and address changed), then deployed, in that order on the store's computer.
    function addDeploy(pairPort, pairCode, adbPort) {
        const name = addName.trim()
        const i = records.findIndex(r => r.name === name)
        const rec = copy(i >= 0 ? records[i] : pos.adminNewRecord("billboards"))
        rec.name = name
        rec.ip = addIp.trim()
        if (adbPort !== undefined) rec.port = adbPort
        pos.adminSave("billboards", i, rec)
        pos.billboardAction(name, "deploy", { pairPort: pairPort ?? "", pairCode: pairCode ?? "" })
        addStarted = true
    }
    function finishAdding() {
        const i = records.findIndex(r => r.name === addName.trim())
        adding = false
        if (i >= 0) choose(i)
    }
    // A step's buttons and texts: a finger's height once the panel is scaled.
    component BoardButton: Button { implicitHeight: 44; font.pixelSize: 15 }
    component BoardText: Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 15 }

    // Store Settings: in sections (the fields' groups), with a search across
    // them all. A field with "when" shows while that other setting is on.
    readonly property var sections: {
        const out = []
        for (const f of fields)
            if (f.groupKey && !out.find(x => x.key === f.groupKey)) out.push({ key: f.groupKey, name: f.group })
        return out
    }
    readonly property bool sectioned: single && sections.length > 1
    property string section: ""
    readonly property string sectionShown: sections.find(x => x.key === section) ? section
                                         : (sections.length ? sections[0].key : "")
    property bool sectionOpen: false   // upright: a section's settings, not the list of them
    property string query: ""
    function plain(t) { return String(t ?? "").toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "") }
    function on(v) {
        if (typeof v === "string") return v !== "" && Number(v) !== 0
        return !!v
    }
    readonly property var words: plain(query).split(/\s+/).filter(x => x !== "")
    // Which "when" settings are on: the form is rebuilt only when one turns
    // on or off, not on every edit (that would drop the field being edited).
    readonly property var whenPaths: [...new Set(fields.filter(f => f.when).map(f => f.when))]
    readonly property string whenOn: whenPaths.map(p => on(draft[p]) ? "1" : "0").join("")
    function shows(f) { return !f.when || whenOn[whenPaths.indexOf(f.when)] === "1" }
    readonly property var shownFields: {
        if (!sectioned) return fields.filter(f => shows(f))
        if (words.length) {
            return fields.filter(f => {
                const hay = plain(f.label + " " + (f.hint ?? "") + " " + f.group)
                return words.every(x => hay.includes(x))
            })
        }
        return fields.filter(f => f.groupKey === sectionShown && shows(f))
    }
    // A page field's choices: field.pages "floor" lists only the floor plans
    // (start pages); a page chosen before stays listed while it's the choice.
    function pageChoices(f) {
        const list = zone.controller.pageChoices(f.pages ?? "")
        const now = draft[f.path] ?? ""
        if (now !== "" && !list.some(o => o.value === now)) {
            const page = zone.controller.pageChoices().find(o => o.value === now)
            list.push({ value: now, text: page ? page.text : now })
        }
        return list
    }
    function openSection(key) {
        section = key
        sectionOpen = true
        if (zone && zone.controller) zone.controller.setWidgetState("adminSection:" + panel, key)
    }
    // Saves may be answered by a server: react when the data changes.
    property string pending: ""   // "save-new" | "save" | "delete" while waiting
    function gs1Fix(s) {
        const c = String(s ?? "").trim()
        if (!/^[0-9]{11}$/.test(c)) return c
        let sum = 0
        for (let i = 0; i < 11; ++i) sum += Number(c[10 - i]) * (i % 2 === 0 ? 3 : 1)
        return c + String((10 - (sum % 10)) % 10)
    }
    // Unitech MS912+ Scanner: Jump to existing item if barcode exists, else fill empty UPC
    property string scanBuf: ""
    property double lastScanKeyMs: 0
    function findMenuIndexByBarcode(raw) {
        const c = String(raw ?? "").trim()
        if (c.length < 4 || w.panel !== "menu") return -1
        const c12 = /^[0-9]{11}$/.test(c) ? w.gs1Fix(c) : c
        for (let i = 0; i < w.records.length; ++i) {
            const r = w.records[i]
            const codes = String(r.barcodes ?? ((r.upc ?? "") + "," + (r.altupc ?? ""))).split(",").map(s => s.trim()).filter(Boolean)
            if (codes.includes(c) || codes.includes(c12)) return i
        }
        return -1
    }
    Keys.onPressed: (event) => {
        if (w.panel !== "menu") return
        const now = Date.now()
        if (now - w.lastScanKeyMs > 180) w.scanBuf = ""
        w.lastScanKeyMs = now
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (w.scanBuf.length >= 6) {
                const code = w.scanBuf.trim()
                w.scanBuf = ""
                const existingIdx = w.findMenuIndexByBarcode(code)
                if (existingIdx >= 0) {
                    w.choose(existingIdx)
                    event.accepted = true
                    return
                }
                if (w.index !== -2 && w.draft && String(w.draft.upc ?? "").trim() === "") {
                    const d = Object.assign({}, w.draft)
                    d.upc = code; w.draft = d; w.dirty = true
                    event.accepted = true
                }
            }
            w.scanBuf = ""
        } else if (event.text && /^[0-9A-Za-z-]$/.test(event.text)) {
            w.scanBuf += event.text
        }
    }

    Timer {
        interval: 80
        running: w.panel === "menu" && w.visible
        repeat: true
        onTriggered: {
            if (!w.zone || !w.zone.controller) return
            const scanned = w.zone.controller.take("adminScan")
            if (!scanned) return
            const code = String(scanned).trim()
            if (code === "") return
            const existingIdx = w.findMenuIndexByBarcode(code)
            if (existingIdx >= 0) {
                w.choose(existingIdx)
                return
            }
            if (w.index !== -2 && w.draft && String(w.draft.upc ?? "").trim() === "") {
                const d = w.copy(w.draft)
                d.upc = code
                w.dirty = true
                w.draft = d
            }
        }
    }
    function save() {
        if (panel === "menu" && draft) {
            const d = Object.assign({}, draft)
            const u = gs1Fix(d.upc)
            const a = String(d.altupc ?? "").split(",").map(x => gs1Fix(x)).filter(Boolean).join(", ")
            if (u !== String(d.upc ?? "").trim() || a !== String(d.altupc ?? "").trim()) {
                pos.adminSave(panel, index, draft)
                d.upc = u; d.altupc = a; draft = d
                return
            }
        }
        pending = index === -1 ? "save-new" : "save"
        pos.adminSave(panel, index, draft)
    }
    function remove() {
        pending = "delete"
        pos.adminDelete(panel, index)
    }
    Connections {
        target: w.pos
        function onAdminChanged() {
            const what = w.pending
            w.pending = ""
            if (what === "save-new") w.choose(w.records.length - 1)
            else if (what === "save") w.choose(w.index)
            else if (what === "delete") w.panel === "employees" ? w.choose(w.index) : (w.index = -2)
        }
        // A refused save only shows a notice; stop waiting.
        function onNotice() { if (w.pending !== "") Qt.callLater(() => w.pending = "") }
    }

    // This widget may be kept for the next manager page (another panel):
    // what's open is worked out from the panel itself, not from `single`,
    // which may not have caught up yet.
    property string openPanel: ""
    function keepUnsaved() {
        if (openPanel !== "" && zone && zone.controller)
            zone.controller.keep("admin:" + openPanel, dirty && index >= -1 ? { index: index, draft: draft } : undefined)
    }
    function open() {
        keepUnsaved()
        openPanel = panel
        adding = false
        query = ""
        sectionOpen = false
        dirty = false
        if (panel === "menu" && zone && zone.controller) {
            const pendingBc = String(zone.controller.widgetState("pendingNewBarcode") ?? "").trim()
            if (pendingBc !== "") {
                zone.controller.setWidgetState("pendingNewBarcode", "")
                choose(-1)
                const d = copy(draft)
                d.upc = pendingBc
                draft = d
                dirty = true
                return
            }
        }
        const one = panel === "taxes" || panel === "store"
        if (one) choose(0)
        else index = -2
        if (zone && zone.controller) {
            section = zone.controller.widgetState("adminSection:" + panel) ?? ""
            // Changes left unsaved when this page was left: back as they were.
            const k = zone.controller.take("admin:" + panel)
            if (k && k.index < records.length) {
                index = k.index
                draft = k.draft
                dirty = true
            }
        }
    }
    Component.onCompleted: open()
    Component.onDestruction: keepUnsaved()
    onPanelChanged: Qt.callLater(open)   // once the bindings have caught up

    // Controls are laid out at a comfortable size, then scaled with the page.
    // Upright (a phone): the list, then a record's form on its own, with ‹ List.
    readonly property bool narrow: zone ? zone.narrow : false
    // About: this screen, its name, size and pages.
    readonly property string screenText: !Window.window ? "" : qsTr("%1: %2 × %3, %4 pages")
        .arg(pos && pos.terminalName ? pos.terminalName : qsTr("This screen"))
        .arg(Window.window.width).arg(Window.window.height)
        .arg(zone && zone.controller && zone.controller.formFactor === "phone" ? qsTr("phone") : qsTr("standard"))
    readonly property real zoom: zone ? zone.formZoom(narrow ? 340 : 680) : 1.6
    Item {
        width: w.width / w.zoom
        height: w.height / w.zoom
        scale: w.zoom
        transformOrigin: Item.TopLeft

        // Terminals: pair a tablet or another screen. The code shows here
        // until the device uses it or 10 minutes pass.
        Rectangle {
            id: pairBar
            visible: w.panel === "terminals"
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            height: visible ? 64 : 0
            radius: 8
            color: w.pos && w.pos.pairing.active ? "#173a26" : "#232933"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12
                Label {
                    visible: !(w.pos && w.pos.pairing.active)
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Add a tablet or another screen: Pair a Device, then type the code on it (its Join screen).")
                    opacity: 0.8
                }
                Label {
                    visible: w.pos && w.pos.pairing.active
                    text: qsTr("Pairing code")
                    opacity: 0.8
                }
                Label {
                    visible: w.pos && w.pos.pairing.active
                    text: w.pos ? (w.pos.pairing.code ?? "") : ""
                    font.pixelSize: 30
                    font.bold: true
                    font.letterSpacing: 4
                    color: "#7ee2a8"
                }
                Label {
                    visible: w.pos && w.pos.pairing.active
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: (w.pos && w.pos.pairing.standby ? qsTr("For the standby computer. ") : "")
                          + qsTr("works once, until %1").arg(w.pos ? (w.pos.pairing.until ?? "") : "")
                    opacity: 0.8
                }
                Button {
                    Layout.minimumHeight: w.narrow ? 36 : 0
                    text: w.pos && w.pos.pairing.active ? qsTr("Stop") : qsTr("Pair a Device")
                    highlighted: !(w.pos && w.pos.pairing.active)
                    onClicked: w.pos.pairing.active ? w.pos.stopPairing() : w.pos.startPairing(false)
                }
                // A standby gets a copy of everything: its own kind of code.
                Button {
                    visible: !(w.pos && w.pos.pairing.active)
                    Layout.minimumHeight: w.narrow ? 36 : 0
                    text: qsTr("Pair a Standby Computer")
                    onClicked: w.pos.startPairing(true)
                }
                Button {
                    visible: !(w.pos && w.pos.pairing.active)
                    text: qsTr("+ USB Tablet")
                    onClicked: if (w.zone && w.zone.controller) w.zone.controller.command("vtSync", ({ mode: "add-tablet", name: String(w.draft.name ?? "") }))
                }
            }
        }

        RowLayout {
            id: searchBar
            visible: w.sectioned
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            height: visible ? 44 : 0
            spacing: 8
            TextField {
                id: search
                objectName: "settingsSearch"
                Layout.fillWidth: true
                Layout.fillHeight: true
                font.pixelSize: 16
                placeholderText: qsTr("Find a setting: tips, receipt, kiosk color…")
                text: w.query
                onTextEdited: w.query = text
            }
            Button {
                objectName: "settingsSearchClear"
                visible: w.query !== ""
                Layout.fillHeight: true
                text: qsTr("Clear")
                onClicked: { w.query = ""; search.focus = false }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            anchors.topMargin: pairBar.visible ? pairBar.height + 20 : searchBar.visible ? searchBar.height + 18 : 10
            spacing: 12

            // --- list ---
            ColumnLayout {
                visible: w.sectioned ? !w.narrow || (!w.sectionOpen && w.query === "")
                                     : !w.single && (!w.narrow || w.index === -2)
                Layout.fillWidth: w.narrow    // nested layouts fill by default
                Layout.preferredWidth: w.narrow ? parent.width : parent.width * (w.sectioned ? 0.3 : 0.36)
                Layout.fillHeight: true
                spacing: 6

                ListView {
                    id: list
                    objectName: "adminList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: w.sectioned ? w.sections : w.records
                    currentIndex: w.sectioned ? -1 : w.index
                    ScrollBar.vertical: TouchScrollBar { id: listBar }
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: w.sectioned ? "settingsSection-" + row.modelData.key : ""
                        width: ListView.view.width - listBar.room
                        // A phone: each row a finger's height.
                        implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, w.narrow ? 36 : 0)
                        highlighted: w.sectioned ? w.query === "" && row.modelData.key === w.sectionShown : row.index === w.index && !w.adding
                        onClicked: {
                            w.adding = false
                            if (!w.sectioned) return w.choose(row.index)
                            w.query = ""
                            w.openSection(row.modelData.key)
                        }
                        contentItem: ColumnLayout {
                            spacing: 0
                            Label {
                                text: (w.sectioned ? row.modelData.name : row.modelData._title) || qsTr("(unnamed)")
                                font.bold: true
                                // A section's name wraps (a narrow list, a longer language).
                                wrapMode: w.sectioned ? Text.WordWrap : Text.NoWrap
                                elide: w.sectioned ? Text.ElideNone : Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: text !== ""
                                // A section: how many of its settings are changed.
                                text: !w.sectioned ? (w.boards ? w.boardDetail(row.modelData) : row.modelData._detail ?? "")
                                    : w.changedIn(row.modelData.key) > 0 ? qsTr("%n changed", "", w.changedIn(row.modelData.key)) : ""
                                color: w.sectioned ? EditorStyle.warning : palette.windowText
                                opacity: w.sectioned ? 1 : 0.7
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                Button {
                    Layout.minimumHeight: w.narrow ? 36 : 0
                    visible: !w.sectioned
                    Layout.fillWidth: true
                    objectName: "adminAdd"
                    text: w.boards ? qsTr("+ Add billboard") : qsTr("+ Add")
                    onClicked: w.boards ? w.startAdding() : w.choose(-1)
                }
            }

            Rectangle {
                visible: (!w.single || w.sectioned) && !w.narrow
                Layout.fillHeight: true
                implicitWidth: 1
                color: "#3a4250"
            }

            // --- billboards: + Add, a step at a time ---
            ColumnLayout {
                objectName: "billboardAdd"
                visible: w.adding
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                Label {
                    text: qsTr("Add a billboard: step %1 of 3").arg(w.addStep)
                    font.bold: true
                    font.pixelSize: 18
                }
                ScrollView {
                    id: addScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical: TouchScrollBar {
                        id: addBar
                        parent: addScroll
                        x: addScroll.width - width
                        y: addScroll.topPadding
                        height: addScroll.availableHeight
                    }
                    ColumnLayout {
                        width: addScroll.availableWidth - addBar.room
                        spacing: 10
                        // 1: its name and address.
                        ColumnLayout {
                            visible: w.addStep === 1
                            Layout.fillWidth: true
                            spacing: 8
                            BoardText { text: qsTr("A TV with a Fire TV (or a Fire TV Stick) shows the guest screen's messages, pictures and videos. Give it a name, and its IP address.") }
                            Label { text: qsTr("Name"); font.bold: true }
                            TextField {
                                objectName: "billboardAddName"
                                Layout.fillWidth: true
                                implicitHeight: 44
                                placeholderText: qsTr("Bar TV")
                                text: w.addName
                                onTextEdited: w.addName = text
                            }
                            Label { text: qsTr("IP address"); font.bold: true }
                            TextField {
                                objectName: "billboardAddIp"
                                Layout.fillWidth: true
                                implicitHeight: 44
                                placeholderText: "192.168.1.60"
                                inputMethodHints: Qt.ImhPreferNumbers
                                text: w.addIp
                                onTextEdited: w.addIp = text
                            }
                            BoardText {
                                opacity: 0.8
                                text: qsTr("On the Fire TV: Settings > Network, highlight its network, press Play/Pause: its IP address shows. A TV at another store works too, over the VPN.")
                            }
                        }
                        // 2: ADB debugging on, on the Fire TV.
                        ColumnLayout {
                            visible: w.addStep === 2
                            Layout.fillWidth: true
                            spacing: 8
                            BoardText { text: qsTr("Turn on ADB debugging on the Fire TV (once):"); font.bold: true }
                            BoardText { text: qsTr("1. Settings > My Fire TV (or Device & Software) > Developer Options.") }
                            BoardText { text: qsTr("2. ADB debugging: ON.") }
                            BoardText { text: qsTr("No Developer Options? Settings > My Fire TV > About: click the device's name 7 times, then go back.") }
                            BoardText { opacity: 0.8; text: qsTr("Then Next. The TV may ask to allow this computer: choose Always allow.") }
                        }
                        // 3: Deploy, and how it goes.
                        ColumnLayout {
                            visible: w.addStep === 3
                            Layout.fillWidth: true
                            spacing: 8
                            BoardText {
                                visible: !w.addStarted
                                text: qsTr("Deploy puts the ViewTouch app on %1 (%2) and pairs it with the store. It takes a minute or two.")
                                      .arg(w.addName.trim()).arg(w.addIp.trim())
                            }
                            ProgressBar {
                                visible: w.addJob !== null
                                Layout.fillWidth: true
                                from: 0
                                to: w.addJob && w.addJob.steps > 0 ? w.addJob.steps : 1
                                value: !w.addJob ? 0 : w.addJob.ok ? to : w.addJob.step
                                indeterminate: w.addJob !== null && w.addJob.running && w.addJob.steps === 0
                            }
                            Repeater {
                                model: w.addJob ? w.addJob.lines.slice(-8) : []
                                delegate: BoardText {
                                    required property string modelData
                                    required property int index
                                    text: modelData
                                    opacity: !w.addJob || index === Math.min(7, w.addJob.lines.length - 1) ? 1 : 0.65
                                }
                            }
                            BoardText {
                                visible: w.addJob !== null && !w.addJob.running && w.addJob.ok
                                color: "#7ee0a1"
                                font.bold: true
                                text: qsTr("Done: %1 is a billboard. Its hours, sound and on / off are on its page.").arg(w.addName.trim())
                            }
                            // Not a Fire TV (a Google TV, Android TV 11 or newer): wireless debugging with a code.
                            Button {
                                flat: true
                                visible: w.addJob !== null && !w.addJob.running && !w.addJob.ok && !w.addAskPairing
                                         && (w.addRecord === null || (w.addRecord.maker ?? "") === "")
                                text: qsTr("This TV isn't a Fire TV")
                                font.underline: true
                                onClicked: w.notFireTv = true
                            }
                            ColumnLayout {
                                visible: w.addAskPairing && !(w.addJob && w.addJob.ok)
                                Layout.fillWidth: true
                                spacing: 6
                                BoardText { text: qsTr("Not a Fire TV: on the TV, Developer options > Wireless debugging ON, then Pair device with pairing code. Type what it shows:") }
                                GridLayout {
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 6
                                    Label { text: qsTr("Pairing port") }
                                    TextField { id: pairPort; implicitHeight: 44; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                                    Label { text: qsTr("Pairing code") }
                                    TextField { id: pairCode; implicitHeight: 44; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                                    Label { text: qsTr("ADB port") }
                                    TextField {
                                        id: adbPort
                                        implicitHeight: 44
                                        Layout.fillWidth: true
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        placeholderText: qsTr("the port on Wireless debugging, after the address")
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    BoardButton {
                        objectName: "billboardAddBack"
                        text: w.addStep === 1 ? qsTr("Cancel") : qsTr("‹ Back")
                        enabled: !(w.addJob && w.addJob.running)
                        onClicked: w.addStep === 1 ? (w.adding = false) : (w.addStep = w.addStep - 1)
                    }
                    Item { Layout.fillWidth: true }
                    BoardButton {
                        objectName: "billboardAddNext"
                        visible: w.addStep < 3
                        highlighted: true
                        enabled: w.addStep !== 1 || (w.addName.trim() !== "" && w.addIp.trim() !== "")
                        text: qsTr("Next ›")
                        onClicked: w.addStep = w.addStep + 1
                    }
                    BoardButton {
                        objectName: "billboardAddDeploy"
                        visible: w.addStep === 3 && !(w.addJob && w.addJob.ok)
                        highlighted: true
                        enabled: !(w.addJob && w.addJob.running)
                        text: w.addJob ? qsTr("Try again") : qsTr("Deploy")
                        onClicked: w.addAskPairing ? w.addDeploy(pairPort.text, pairCode.text, adbPort.text) : w.addDeploy()
                    }
                    BoardButton {
                        visible: w.addStep === 3 && w.addJob !== null && w.addJob.ok
                        highlighted: true
                        text: qsTr("Done")
                        onClicked: w.finishAdding()
                    }
                }
            }

            // --- form ---
            ColumnLayout {
                visible: !w.adding && (w.sectioned ? !w.narrow || w.sectionOpen || w.query !== ""
                                                   : !w.narrow || w.index !== -2 || w.single)
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Button {
                    objectName: "adminBackToList"
                    visible: w.narrow && !w.single && w.index !== -2
                    text: qsTr("‹ List")
                    onClicked: { w.index = -2; w.dirty = false }
                }
                Button {
                    objectName: "adminBackToSections"
                    visible: w.sectioned && w.narrow && w.query === ""
                    text: qsTr("‹ Settings")
                    onClicked: w.sectionOpen = false
                }
                Label {
                    visible: w.index === -2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: w.boards ? qsTr("Choose a billboard on the left, or + Add billboard.") : qsTr("Choose something on the left, or Add.")
                    opacity: 0.7
                }

                Label {
                    visible: w.index !== -2
                    text: w.sectioned ? (w.query !== "" ? qsTr("%n setting(s) found", "", w.shownFields.length)
                                                        : (w.sections.find(x => x.key === w.sectionShown) ?? {}).name ?? "")
                        : w.index === -1 ? qsTr("New") : (w.draft.name || w.draft.storeName || (w.single ? "" : qsTr("Edit")))
                    font.bold: true
                    font.pixelSize: 18
                }

                ScrollView {
                    id: scroll
                    visible: w.index !== -2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    // Finger-wide, shown when the form is longer than the panel.
                    ScrollBar.vertical: TouchScrollBar {
                        id: formBar
                        objectName: "adminFormScroll"
                        parent: scroll
                        x: scroll.width - width
                        y: scroll.topPadding
                        height: scroll.availableHeight
                    }

                    ColumnLayout {
                        width: scroll.availableWidth - formBar.room
                        spacing: 10
                        Label {
                            visible: w.sectioned && w.query !== "" && w.shownFields.length === 0
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("No setting has those words. Try fewer, or look through the sections.")
                            opacity: 0.7
                        }
                        Repeater {
                            model: w.shownFields
                            delegate: FieldEditor {
                                required property var modelData
                                minHeight: w.narrow ? 36 : 0
                                // A "look" field: an enum of this screen's looks.
                                // The billboard defaults row: its values are the defaults themselves.
                                field: w.boards && w.draft._defaults === true ? Object.assign({}, modelData, { inheritable: false })
                                     : modelData.type !== "look" || !w.zone ? modelData
                                     : Object.assign({}, modelData, {
                                           type: "enum",
                                           options: [{ value: "", text: qsTr("The store's look") }].concat(
                                               w.zone.controller.looks().map(l => ({ value: l.id, text: qsTranslate("Looks", l.name) })))
                                       })
                                pos: w.pos
                                controller: w.zone ? w.zone.controller : null
                                touch: true
                                pages: modelData.type === "page" && w.zone ? w.pageChoices(modelData) : []
                                // This screen's own lines (About): from this screen, not the store.
                                value: modelData.path === "screen.version" && w.zone ? w.zone.controller.screenBuild()
                                     : modelData.path === "screen.display" ? w.screenText
                                     : w.boards && modelData.path === "status" ? w.boardStatus(w.saved)
                                     : w.draft[modelData.path]
                                isSet: !(modelData.inheritable === true && w.draft._defaults !== true
                                         && (w.draft[modelData.path] === null || w.draft[modelData.path] === undefined))
                                resolved: modelData.inheritable === true ? (w.draft._inherit ?? {})[modelData.path] : undefined
                                inheritedText: qsTr("default")
                                changed: w.differs(modelData.path)
                                section: w.sectioned && w.query !== "" ? modelData.group : ""
                                readOnly: modelData.readonlyExisting === true && w.index >= 0
                                onCommit: v => {
                                    if (w.panel === "menu" && modelData.path === "upc") {
                                        const hit = w.findMenuIndexByBarcode(v)
                                        if (hit >= 0 && hit !== w.index) {
                                            Qt.callLater(() => w.choose(hit))
                                            return
                                        }
                                    }
                                    const d = w.copy(w.draft)
                                    d[modelData.path] = v
                                    w.dirty = true   // first: the new draft may rebuild this form
                                    w.draft = d
                                }
                                // Emptied (a color): back to the usual. A billboard's: back to the defaults.
                                onReset: commit(modelData.inheritable === true ? null : "")
                            }
                        }
                    }
                }

                // Billboards: Deploy / Restart as it goes (what vt-add-billboard says, plainly).
                Rectangle {
                    objectName: "billboardJob"
                    visible: w.boardOpen && w.boardJob !== null
                    Layout.fillWidth: true
                    implicitHeight: jobBody.implicitHeight + 16
                    radius: 6
                    color: !w.boardJob || w.boardJob.running ? "#232a35" : w.boardJob.ok ? "#173a26" : "#3a1f1f"
                    ColumnLayout {
                        id: jobBody
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                        spacing: 4
                        ProgressBar {
                            visible: w.boardJob !== null && w.boardJob.running
                            Layout.fillWidth: true
                            from: 0
                            to: w.boardJob && w.boardJob.steps > 0 ? w.boardJob.steps : 1
                            value: w.boardJob ? w.boardJob.step : 0
                            indeterminate: w.boardJob !== null && w.boardJob.steps === 0
                        }
                        Repeater {
                            model: w.boardJob ? w.boardJob.lines.slice(-4) : []
                            delegate: Label {
                                required property string modelData
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: modelData
                            }
                        }
                        Button {
                            flat: true
                            visible: w.boardJob !== null && !w.boardJob.running && !w.boardJob.ok && w.boardJob.refused
                                     && w.saved && (w.saved.maker ?? "") === "" && w.draft._askPairing !== true
                            text: qsTr("This TV isn't a Fire TV")
                            font.underline: true
                            onClicked: { const d = w.copy(w.draft); d._askPairing = true; w.draft = d }
                        }
                    }
                }

                Flow {
                    spacing: 6
                    // Not on About (nothing there to change), unless changes elsewhere wait.
                    visible: w.index !== -2 && !(w.sectioned && w.query === "" && w.sectionShown === "about" && !w.dirty)
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Button {
                        objectName: "adminSave"
                        text: w.changes > 0 ? qsTr("Save %n change(s)", "", w.changes) : qsTr("Save")
                        highlighted: true
                        enabled: w.dirty || w.index === -1
                        onClicked: w.save()
                    }
                    Button {
                        text: qsTr("Undo changes")
                        enabled: w.dirty
                        onClicked: w.choose(w.index)
                    }
                    // Printers: a test page (saved settings), and its drawer.
                    Button {
                        objectName: "testPrint"
                        visible: w.panel === "printers" && w.index >= 0
                        enabled: !w.dirty
                        text: w.dirty ? qsTr("Save, then Test Print") : qsTr("Test Print")
                        onClicked: w.pos.testPrinter(w.draft.id, false)
                    }
                    // Billboards: the TV itself (on the store's computer, in the background).
                    Button {
                        objectName: "billboardDeploy"
                        visible: w.boardOpen
                        enabled: !(w.boardJob && w.boardJob.running)
                        text: w.saved && w.saved.paired ? qsTr("Update app") : qsTr("Deploy")
                        onClicked: {
                            if (w.dirty) w.save()
                            w.pos.billboardAction(w.draft.name, "deploy", { pairPort: String(w.draft.pairPort ?? ""), pairCode: String(w.draft.pairCode ?? "") })
                        }
                    }
                    Button {
                        objectName: "billboardRestart"
                        visible: w.boardOpen
                        enabled: !w.dirty && !(w.boardJob && w.boardJob.running)
                        text: qsTr("Restart app")
                        onClicked: w.pos.billboardAction(w.saved.name, "restart")
                    }
                    Button {
                        objectName: "billboardIdentify"
                        visible: w.boardOpen
                        enabled: !w.dirty
                        text: qsTr("Identify")
                        onClicked: w.pos.billboardAction(w.saved.name, "identify")
                    }
                    Button {
                        objectName: "billboardPreview"
                        visible: w.boardOpen
                        text: qsTr("Preview")
                        onClicked: w.openPreview()
                    }
                    Button {
                        objectName: "billboardApplyTo"
                        visible: w.boardOpen
                        text: qsTr("Apply to…")
                        onClicked: w.openApplyTo()
                    }
                    Button {
                        visible: w.panel === "terminals" && w.index >= 0 && w.draft.screen === "billboard"
                        text: qsTr("Billboards page…")
                        onClicked: if (w.zone && w.zone.controller) w.zone.controller.jumpTo("billboards")
                    }
                    Button {
                        visible: w.panel === "printers" && w.index >= 0 && w.draft.drawerKick === true
                        enabled: !w.dirty
                        text: qsTr("Test Print + Open Drawer")
                        onClicked: w.pos.testPrinter(w.draft.id, true)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "billboardRemove"
                        visible: w.boardOpen
                        text: qsTr("Remove")
                        onClicked: { removePin.text = ""; removeBox.open() }
                    }
                    Button {
                        visible: !w.single && w.index >= 0 && !w.boards
                        text: w.armDelete ? qsTr("Tap again to confirm")
                                          : (w.panel === "employees" ? qsTr("Deactivate") : qsTr("Remove"))
                        onClicked: {
                            if (!w.armDelete) {
                                w.armDelete = true
                                return
                            }
                            w.remove()
                            w.armDelete = false
                        }
                    }
                }
            }
        }
    }

    // Billboards: Remove asks for a manager's PIN (it unpairs the TV for good).
    Popup {
        id: removeBox
        objectName: "billboardRemoveBox"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, (parent ? parent.width : 560) - 32)
        padding: 20
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 20
                font.bold: true
                text: qsTr("Remove %1?").arg(w.saved ? w.saved.name : "")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 16
                text: qsTr("It's unpaired: it can't connect to the store again until it's added and deployed anew. A manager's PIN:")
            }
            TextField {
                id: removePin
                objectName: "billboardRemovePin"
                Layout.fillWidth: true
                implicitHeight: 64
                font.pixelSize: 24
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Button {
                    implicitHeight: 64
                    Layout.fillWidth: true
                    font.pixelSize: 18
                    text: qsTr("Cancel")
                    onClicked: removeBox.close()
                }
                Button {
                    objectName: "billboardRemoveOk"
                    implicitHeight: 64
                    Layout.fillWidth: true
                    font.pixelSize: 18
                    enabled: removePin.text !== ""
                    text: qsTr("Remove")
                    onClicked: {
                        w.pos.removeBillboard(w.saved.name, removePin.text)
                        removePin.text = ""
                        removeBox.close()
                        w.index = -2
                    }
                }
            }
        }
    }

    // Billboards: Preview - the arrangement played as the TV would, before it's saved or sent.
    Popup {
        id: previewBox
        objectName: "billboardPreviewBox"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 12
        width: Math.min(1200, (parent ? parent.width : 1200) - 32)
        background: Rectangle { color: "#14171c"; radius: 10; border.color: "#3a4250" }
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 10
            Label {
                Layout.fillWidth: true
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.WordWrap
                text: qsTr("Preview: %1 (videos without sound here)").arg(w.draft.name ?? "")
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width * 9 / 16
                color: "black"
                clip: true
                Loader {
                    anchors.fill: parent
                    active: previewBox.visible
                    sourceComponent: BillboardShow {
                        slides: w.previewSlides
                        mute: true
                        accent: w.draft._accent || "#2f6fd6"
                        storeName: w.draft._storeName ?? ""
                        revision: w.pos ? w.pos.imageRevision : 0
                        logoUrl: w.pos && (w.draft._logo ?? "") !== "" ? w.pos.imageUrl(w.draft._logo) : ""
                        imageFor: ref => w.pos ? w.pos.imageUrl(ref) : ""
                        // A video plays here only when its file is on this computer (the store's).
                        videoFor: line => {
                            let path = String(line).slice(6)
                            if (path.endsWith("#sound")) path = path.slice(0, -6)
                            return path.startsWith("/") ? "file://" + path : ""
                        }
                    }
                }
            }
            Button {
                objectName: "billboardPreviewClose"
                Layout.alignment: Qt.AlignRight
                implicitHeight: 64
                implicitWidth: 200
                font.pixelSize: 18
                text: qsTr("Close")
                onClicked: previewBox.close()
            }
        }
    }

    // Billboards: Apply to... - this one's messages, pictures and videos copied to the ones ticked.
    Popup {
        id: applyBox
        objectName: "billboardApplyBox"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 20
        width: Math.min(640, (parent ? parent.width : 640) - 32)
        readonly property var targets: [{ key: "@guest", name: qsTr("Guest screen") }].concat(
            w.records.filter(r => r._board === true && r.name !== (w.saved ? w.saved.name : "")).map(r => ({ key: r.name, name: r.name })))
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 20
                font.bold: true
                text: qsTr("Copy %1's messages, pictures and videos to:").arg(w.saved ? w.saved.name : "")
            }
            Repeater {
                model: applyBox.targets
                delegate: ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 64
                    font.pixelSize: 18
                    text: (w.applyPicked[modelData.key] ? "✓  " : "     ") + modelData.name
                    highlighted: w.applyPicked[modelData.key] === true
                    onClicked: {
                        const p = Object.assign({}, w.applyPicked)
                        p[modelData.key] = !p[modelData.key]
                        w.applyPicked = p
                    }
                }
            }
            Label {
                visible: applyBox.targets.length === 1
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.75
                text: qsTr("No other billboards yet.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Button {
                    implicitHeight: 64
                    Layout.fillWidth: true
                    font.pixelSize: 18
                    text: qsTr("Cancel")
                    onClicked: applyBox.close()
                }
                Button {
                    objectName: "billboardApplyOk"
                    implicitHeight: 64
                    Layout.fillWidth: true
                    font.pixelSize: 18
                    highlighted: true
                    enabled: w.applyTargets().length > 0
                    text: qsTr("Copy to %n", "", w.applyTargets().length)
                    onClicked: {
                        if (w.dirty) w.save()   // what's on the form, first
                        w.pos.applyBillboardContent(w.saved.name, w.applyTargets())
                        applyBox.close()
                    }
                }
            }
        }
    }
}
