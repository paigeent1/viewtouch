import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// Left-hand panel: every page, with create / duplicate / delete.
Rectangle {
    id: panel

    required property LayoutController controller
    required property EditorController editor

    color: EditorStyle.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 8
            Layout.topMargin: 10
            Label {
                text: qsTr("Pages")
                font.bold: true
                font.pixelSize: 15
                Layout.fillWidth: true
            }
            // A-Z, or grouped by type (each group folds).
            Button {
                objectName: "pageSort"
                flat: true
                text: list.byType ? qsTr("By type") : qsTr("A–Z")
                onClicked: list.byType = !list.byType
                ToolTip.visible: hovered
                ToolTip.text: qsTr("List the pages A–Z, or grouped by type")
            }
        }

        // Find a page: by its name, its id or its type.
        TextField {
            id: findPage
            objectName: "pageFind"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            placeholderText: qsTr("Find a page…")
            rightPadding: clearFind.visible ? clearFind.width + 6 : padding
            Keys.onEscapePressed: text = ""
            ToolButton {
                id: clearFind
                visible: findPage.text !== ""
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                onClicked: findPage.text = ""
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            property bool byType: false
            property var opened: ({})          // By type: the groups opened (they start shut)
            // A page's phone version is reached by the Standard | Phone switch.
            readonly property var listed: panel.editor.pages.filter(p => !p.variantOf)
            readonly property string shownPage: panel.editor.revision >= 0 ? (panel.editor.screens.page ?? panel.editor.pageId) : ""
            function title(p) { return p.name ? qsTranslate("Page", p.name) : p.id }
            // The rows: pages (sorted, filtered by the search), with a header for each type when grouped.
            readonly property var rows: {
                const q = findPage.text.trim().toLowerCase()
                const kindName = k => panel.editor.pageKindName(k)
                let pages = listed.filter(p => q === "" || title(p).toLowerCase().includes(q)
                                                || p.id.toLowerCase().includes(q) || kindName(p.kind).toLowerCase().includes(q))
                pages = pages.slice().sort((a, b) => title(a).localeCompare(title(b)))
                if (!byType)
                    return pages.map(p => ({ page: p }))
                pages.sort((a, b) => kindName(a.kind).localeCompare(kindName(b.kind)) || title(a).localeCompare(title(b)))
                const out = []
                let last = null
                for (const p of pages) {
                    if (p.kind !== last) {
                        last = p.kind
                        out.push({ header: p.kind, count: pages.filter(x => x.kind === p.kind).length })
                    }
                    // Searching, or the page on screen: its group stays open.
                    if (q !== "" || opened[p.kind] || p.id === shownPage)
                        out.push({ page: p })
                }
                return out
            }
            model: rows
            currentIndex: rows.findIndex(r => r.page && r.page.id === shownPage)
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                readonly property bool isHeader: modelData.header !== undefined
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: {
                    if (isHeader) {
                        const o = Object.assign({}, list.opened)
                        o[modelData.header] = !o[modelData.header]
                        list.opened = o
                    } else {
                        panel.controller.showPage(modelData.page.id)
                    }
                }
                background: Rectangle {
                    color: row.isHeader ? Qt.rgba(0.5, 0.5, 0.5, 0.15)
                         : row.highlighted ? palette.highlight
                         : row.hovered ? Qt.rgba(0.5, 0.5, 0.5, 0.1) : "transparent"
                }

                contentItem: ColumnLayout {
                    spacing: 1
                    Label {
                        visible: row.isHeader
                        text: row.isHeader ? (list.opened[row.modelData.header] || findPage.text !== "" ? "▾ " : "▸ ")
                                             + panel.editor.pageKindName(row.modelData.header)
                                             + "  (" + row.modelData.count + ")" : ""
                        font.bold: true
                        font.pixelSize: 12
                        color: EditorStyle.muted
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: !row.isHeader
                        text: row.isHeader ? "" : list.title(row.modelData.page)
                        font.bold: row.highlighted
                        color: row.highlighted ? "white" : palette.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: !row.isHeader
                        readonly property var p: row.modelData.page ?? ({})
                        text: row.isHeader ? "" : (list.byType ? "" : panel.editor.pageKindName(p.kind))
                              + (p.role ? (list.byType ? "" : "  ·  ") + qsTr("role: %1").arg(panel.editor.pageRoleName(p.role)) : "")
                              + (p.templateId ? "  ·  ⧉ " + p.templateId : "")
                              + (p.hasPhone ? "  ·  " + qsTr("+ phone") : "")
                        color: row.highlighted ? Qt.rgba(1, 1, 1, 0.8) : EditorStyle.muted
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("No page matches.")
                color: EditorStyle.muted
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: EditorStyle.border }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4
            Button {
                text: qsTr("New…")
                Layout.fillWidth: true
                onClicked: newPageDialog.open()
            }
            Button {
                text: qsTr("Copy")
                Layout.fillWidth: true
                onClicked: panel.editor.duplicatePage()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Duplicate this page")
            }
            Button {
                text: qsTr("Delete")
                Layout.fillWidth: true
                onClicked: deleteDialog.open()
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: EditorStyle.border
    }

    Dialog {
        id: newPageDialog
        title: qsTr("New page")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: DialogWords.apply(this)
        width: 380

        onOpened: {
            nameField.text = ""
            kindBox.currentIndex = 0
            templateBox.currentIndex = 0
            nameField.forceActiveFocus()
        }
        onAccepted: panel.editor.newPage(nameField.text, kindBox.currentText, templateBox.currentValue)

        ColumnLayout {
            anchors.fill: parent
            spacing: 6
            Label { text: qsTr("Name") }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. Happy Hour")
                onAccepted: newPageDialog.accept()
            }
            Label { text: qsTr("Type") }
            ComboBox {
                id: kindBox
                Layout.fillWidth: true
                model: panel.editor.pageKinds
                onActivated: {
                    // Menu pages normally share the order template.
                    const menuKinds = ["index", "items", "modifier"]
                    const idx = templateBox.model.findIndex(o => o.value === "order-template")
                    if (menuKinds.includes(currentText) && idx >= 0 && templateBox.currentIndex === 0)
                        templateBox.currentIndex = idx
                }
            }
            Label { text: qsTr("Template (zones shown behind this page)") }
            ComboBox {
                id: templateBox
                Layout.fillWidth: true
                model: panel.editor.revision >= 0 ? panel.editor.pageOptions() : []   // pages made since show too
                textRole: "text"
                valueRole: "value"
            }
        }
    }

    Dialog {
        id: deleteDialog
        title: qsTr("Delete page?")
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel
        onAboutToShow: DialogWords.apply(this)
        width: 420
        onAccepted: panel.editor.deletePage()

        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.editor.references.length === 0
                      ? qsTr("Delete this page? No buttons lead here. You can undo this.")
                      : qsTr("These buttons lead here and will stop working:\n• %1\n\nYou can undo this.")
                        .arg(panel.editor.references.join("\n• "))
            }
        }
    }
}
