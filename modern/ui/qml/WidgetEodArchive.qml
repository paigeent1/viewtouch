import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Manager > Reports > End of Day archive: each closed day's summary PDF (the reports, the Exceptions
// report and a picture of each exception check), kept write-once in the store's data folder and
// fingerprinted in a chain (PosService::eodArchive). The days newest first on the left (or pick one
// on the calendar); the day's PDF on the right, page by page, drawn on the store's computer so a
// tablet needs no PDF viewer; Print. At the top: the integrity check and the long-term folder.
Item {
    id: w
    property ZoneItem zone
    readonly property PosService pos: zone ? zone.pos : null
    readonly property real unit: Math.max(14, Math.min(width / 54, height / 28))

    property var info: ({})
    readonly property var days: info.days ?? []
    readonly property var check: info.check ?? ({})
    readonly property var longTerm: info.longTerm ?? ({})
    readonly property var news: info.news ?? ({})
    property int index: -1
    readonly property var day: index >= 0 && index < days.length ? days[index] : null
    property int page: 1
    property int pages: 0
    property string pageUrl: ""
    property string pageError: ""
    property bool loadingPage: false
    property real zoom: 1

    function reload() { if (pos) pos.ask("eodArchive", "eodArchive", []) }
    function choose(i) {
        if (i < 0 || i >= days.length) return
        index = i
        list.positionViewAtIndex(i, ListView.Contain)
        page = 1
        pages = 0
        zoom = 1
        loadPage()
    }
    function loadPage() {
        if (!day) return
        pageUrl = ""
        pageError = ""
        loadingPage = true
        pos.loadArchivePage(day.seq, page)
    }
    function goToDate(iso) {
        for (let i = 0; i < days.length; ++i)
            if (days[i].date === iso) { choose(i); return }
        // None that day: the nearest before it.
        for (let i = 0; i < days.length; ++i)
            if (days[i].date < iso) { choose(i); return }
    }
    function sizeText(bytes) {
        return bytes >= 1048576 ? qsTr("%1 MB").arg((bytes / 1048576).toFixed(1)) : qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)))
    }
    function stateText(d) {
        if (d.place === "longTerm" && !d.reachable) return qsTr("In the long-term archive: not reachable now")
        switch (d.state) {
        case "ok": return d.place === "longTerm" ? qsTr("In the long-term archive · unchanged") : qsTr("Unchanged since archived")
        case "changed": return qsTr("CHANGED since it was archived")
        case "missing": return qsTr("MISSING")
        case "unreachable": return qsTr("In the long-term archive: not reachable now")
        default: return d.place === "longTerm" ? qsTr("In the long-term archive") : qsTr("Not checked yet")
        }
    }
    function stateColor(d) {
        return d.state === "changed" || d.state === "missing" ? "#b42318"
             : d.place === "longTerm" && !d.reachable ? "#8a6d00" : "#1f6f43"
    }

    Connections {
        target: w.pos
        function onAnswered(tag, result) {
            if (tag !== "eodArchive") return
            const keep = w.day ? w.day.seq : -1
            w.info = result ?? ({})
            if (w.info.error && (w.info.days ?? []).length === 0) return
            const list = w.info.days ?? []
            let at = list.findIndex(d => d.seq === keep)
            if (at < 0 && list.length > 0 && w.index < 0) w.choose(0)
            else if (at >= 0) w.index = at
            // Checked at End of Day; checked here too when it hasn't been since ViewTouch started.
            if (!w.info.checking && !(w.info.check ?? {}).at && !w.askedCheck) {
                w.askedCheck = true
                w.pos.checkEodArchive()
                w.reload()
            }
        }
        function onArchivePageLoaded(seq, page, pages, url, error) {
            if (!w.day || seq !== w.day.seq) return
            w.loadingPage = false
            if (pages > 0) w.pages = pages
            w.page = page
            w.pageUrl = url
            w.pageError = error
        }
    }
    property bool askedCheck: false
    // While a check runs (it reads every PDF), look again now and then.
    Timer { interval: 1500; repeat: true; running: w.info.checking === true; onTriggered: w.reload() }
    Component.onCompleted: reload()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // --- the integrity check and the long-term folder ---
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: banner.implicitHeight + w.unit
            radius: 4
            color: w.info.checking ? "#44506a" : w.check.ok === false ? "#7a1c14" : w.check.at ? "#1f5135" : "#44506a"
            ColumnLayout {
                id: banner
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: w.unit * 0.6
                spacing: 2
                Label {
                    objectName: "archiveCheck"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "white"
                    font.pixelSize: w.unit * 0.95
                    font.bold: true
                    text: w.info.error && w.days.length === 0 ? w.info.error
                        : w.info.checking ? qsTr("Checking every archived day's fingerprint…")
                        : w.check.ok === false ? qsTr("Problem: %1").arg(w.check.problem)
                        : w.check.at ? qsTr("All %1 archived days check out: each PDF and record is as it was archived (any change would show).").arg(w.check.days)
                        : qsTr("Not checked yet.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#e6e9ef"
                    font.pixelSize: w.unit * 0.75
                    text: (w.check.at ? qsTr("Checked %1.").arg(new Date(w.check.at).toLocaleString(Qt.locale(), Locale.ShortFormat)) + "  " : "")
                        + (w.check.unreachable > 0 ? qsTr("%1 in the long-term archive couldn't be reached to check.").arg(w.check.unreachable) + "  " : "")
                        + (!w.longTerm.folder ? qsTr("No long-term archive location is set (Store Settings > Backups): every PDF stays on this computer.")
                           : w.longTerm.reachable ? qsTr("Days older than %1 move to the long-term archive: %2").arg(w.longTerm.days).arg(w.longTerm.folder)
                           : qsTr("The long-term archive (%1) is not reachable now: PDFs stay here until it is.").arg(w.longTerm.folder))
                }
                Label {
                    visible: !!w.news.warning || !!w.news.error
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#ffd166"
                    font.pixelSize: w.unit * 0.75
                    text: qsTr("Last End of Day: %1").arg(w.news.error || w.news.warning || "")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // --- the days, newest first ---
            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: w.width * 0.34
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    DateField {
                        objectName: "archiveDate"
                        Layout.fillWidth: true
                        implicitHeight: w.unit * 2.4
                        fontSize: w.unit * 0.8
                        value: w.day ? w.day.date : ""
                        placeholder: qsTr("Go to a day…")
                        title: qsTr("The archived day")
                        onPicked: v => { if (v !== "") w.goToDate(v) }
                    }
                    Button {
                        text: qsTr("Check now")
                        implicitHeight: w.unit * 2.4
                        font.pixelSize: w.unit * 0.8
                        enabled: w.info.checking !== true
                        onClicked: { w.pos.checkEodArchive(); w.reload() }
                    }
                }
                ListView {
                    id: list
                    objectName: "archiveDays"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: w.days
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: w.unit * 3.3
                        radius: 3
                        color: row.index === w.index ? "#1d3fb8" : "#2b3445"
                        border.color: "#111827"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: w.unit * 0.6
                            anchors.rightMargin: w.unit * 0.6
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: Qt.locale().toString(new Date(row.modelData.date + "T12:00:00"), "ddd MMM d, yyyy")
                                    color: "white"; font.pixelSize: w.unit * 0.95; font.bold: true
                                }
                                Label { text: qsTr("day %1").arg(row.modelData.dayId); color: "#cbd5e1"; font.pixelSize: w.unit * 0.75 }
                                Item { Layout.fillWidth: true }
                                Label {
                                    visible: row.modelData.kind === "re-archived"
                                    text: qsTr("re-archived"); color: "#ffd166"; font.pixelSize: w.unit * 0.7; font.bold: true
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: w.stateText(row.modelData) + " · " + w.sizeText(row.modelData.size)
                                color: row.index === w.index ? "#ffe066" : Qt.lighter(w.stateColor(row.modelData), 1.9)
                                font.pixelSize: w.unit * 0.72
                            }
                        }
                        TapHandler { onTapped: w.choose(row.index) }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width * 0.9
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: list.count === 0
                        text: qsTr("No days archived yet. Each End of Day adds one.")
                        font.pixelSize: w.unit * 0.9
                        opacity: 0.7
                    }
                }
            }

            // --- the day's PDF ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    visible: w.day !== null
                    wrapMode: Text.WordWrap
                    font.pixelSize: w.unit * 0.75
                    color: "#cbd5e1"
                    text: w.day ? qsTr("%1 · fingerprint (SHA-256) %2…").arg(w.day.file).arg(String(w.day.sha).slice(0, 16)) : ""
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#dfe3ea"
                    radius: 4
                    clip: true
                    Flickable {
                        id: paper
                        anchors.fill: parent
                        anchors.margins: 6
                        contentWidth: Math.max(width, pageImage.width)
                        contentHeight: pageImage.height
                        boundsBehavior: Flickable.StopAtBounds
                        Image {
                            id: pageImage
                            objectName: "archivePage"
                            source: w.pageUrl
                            width: paper.width * w.zoom
                            height: implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width * 0.8
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: w.pageUrl === ""
                        color: "#374151"
                        font.pixelSize: w.unit * 0.9
                        text: !w.day ? qsTr("Choose a day.")
                            : w.loadingPage ? qsTr("Getting the page…")
                            : w.pageError !== "" ? w.pageError
                            : ""
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        text: "‹"; implicitWidth: w.unit * 3.4; implicitHeight: w.unit * 2.8; font.pixelSize: w.unit * 1.3
                        enabled: w.day !== null && w.page > 1 && !w.loadingPage
                        onClicked: { w.page -= 1; w.loadPage() }
                    }
                    Label {
                        text: w.pages > 0 ? qsTr("Page %1 of %2").arg(w.page).arg(w.pages) : ""
                        font.pixelSize: w.unit * 0.85
                    }
                    Button {
                        text: "›"; implicitWidth: w.unit * 3.4; implicitHeight: w.unit * 2.8; font.pixelSize: w.unit * 1.3
                        enabled: w.day !== null && w.page < w.pages && !w.loadingPage
                        onClicked: { w.page += 1; w.loadPage() }
                    }
                    Button {
                        text: "−"; implicitWidth: w.unit * 2.8; implicitHeight: w.unit * 2.8; font.pixelSize: w.unit * 1.3
                        enabled: w.zoom > 1
                        onClicked: w.zoom = Math.max(1, w.zoom - 0.5)
                    }
                    Button {
                        text: "+"; implicitWidth: w.unit * 2.8; implicitHeight: w.unit * 2.8; font.pixelSize: w.unit * 1.3
                        enabled: w.zoom < 3
                        onClicked: w.zoom = Math.min(3, w.zoom + 0.5)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "archivePrint"
                        text: qsTr("Print")
                        implicitWidth: w.unit * 6; implicitHeight: w.unit * 2.8; font.pixelSize: w.unit * 0.95
                        palette.button: "#1f8a4c"; palette.buttonText: "white"
                        enabled: w.day !== null && (w.day.place !== "longTerm" || w.day.reachable)
                        onClicked: w.pos.printEodArchive(w.day.seq)
                    }
                }
            }
        }
    }
}
