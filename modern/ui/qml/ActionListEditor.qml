import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

// "When touched" list for the selected zone: ordered action cards whose
// fields come from the action schema. Every change rewrites the whole list
// (one undo step).
ColumnLayout {
    id: ale

    required property EditorController editor

    readonly property var actions: {
        if (editor.revision < 0) return []   // (compared, not just read: an unused read is compiled away and nothing refreshes)
        return editor.actions()
    }
    readonly property var types: editor.actionTypes()

    spacing: 8
    Layout.fillWidth: true

    function typeDef(type) {
        return types.find(t => t.type === type) ?? { type: type, label: type, fields: [] }
    }

    function copyActions() {
        return JSON.parse(JSON.stringify(actions))
    }

    function setKey(i, key, value) {
        const list = copyActions()
        if (value === undefined || value === "" || (value !== null && typeof value === "object" && value.length === 0))
            delete list[i][key]
        else
            list[i][key] = value
        editor.setActions(list)
    }

    // A command's own settings (the schema's commandArgs), kept in the action's args.
    readonly property var commandArgs: (types.find(t => t.type === "command") ?? {}).commandArgs ?? ({})
    // TV buttons: a changed address or type goes to every button for the same TV (owner 2026-11-21).
    property bool tvAll: true

    function argFieldsOf(action) {
        return action && action.type === "command" ? (commandArgs[action.name] ?? []) : []
    }

    function argShown(f, args) {
        return !f.when || ((args[f.when.path] ?? f.when.unset ?? "") === f.when.is)
    }

    // A private (store network) IPv4 address: 10.x, 172.16-31.x, 192.168.x (as the main computer checks).
    function privateIp(text) {
        const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(text)
        if (!m) return false
        const n = m.slice(1).map(Number)
        if (n.some(x => x > 255) || m.slice(1).some(x => x.length > 1 && x[0] === "0")) return false
        return n[0] === 10 || (n[0] === 172 && n[1] >= 16 && n[1] <= 31) || (n[0] === 192 && n[1] === 168)
    }

    function hasTvFields(action) {
        return argFieldsOf(action).some(f => f.tvAddress === true)
    }

    function setArg(i, f, value) {
        const list = copyActions()
        const a = list[i]
        const fields = argFieldsOf(a)
        const args = Object.assign({}, a.args ?? {})
        if (f.digits && typeof value === "string")
            value = value.replace(/[^0-9]/g, "")
        if (value === undefined || value === null || value === "")
            delete args[f.path]
        else
            args[f.path] = value
        // Fields that show by this one (Key or Channel by What it does): start again, an enum at its first choice.
        for (const g of fields)
            if (g.when && g.when.path === f.path && g.path !== f.path)
                delete args[g.path]
        for (const g of fields)
            if (g.when && g.when.path === f.path && argShown(g, args) && g.type === "enum" && (g.options ?? []).length)
                args[g.path] = g.options[0].value
        if (Object.keys(args).length) a.args = args
        else delete a.args
        if (tvAll && (f.tvAddress === true || f.path === "kind") && String(args.box ?? "").trim() !== "")
            editor.setActionsForTv(list, args.box)
        else
            editor.setActions(list)
    }

    function defaultsFor(type) {
        switch (type) {
        case "jump": return { type: type, mode: "push" }
        case "qualifier": return { type: type, qualifier: "no" }
        case "command": return { type: type, name: "sendOrder" }
        default: return { type: type }
        }
    }

    function setType(i, type) {
        const list = copyActions()
        list[i] = defaultsFor(type)
        editor.setActions(list)
    }

    function move(i, delta) {
        const list = copyActions()
        const j = i + delta
        if (j < 0 || j >= list.length) return
        const t = list[i]; list[i] = list[j]; list[j] = t
        editor.setActions(list)
    }

    function remove(i) {
        const list = copyActions()
        list.splice(i, 1)
        editor.setActions(list)
    }

    function add(type) {
        const list = copyActions()
        list.push(defaultsFor(type))
        editor.setActions(list)
    }

    Label {
        visible: ale.actions.length === 0
        text: qsTr("Nothing happens when this is touched.")
        color: EditorStyle.muted
        font.italic: true
    }

    Repeater {
        model: ale.actions.length

        delegate: Frame {
            id: card
            required property int index
            readonly property var action: ale.actions[index] ?? ({})
            readonly property var def: ale.typeDef(action.type)

            Layout.fillWidth: true
            padding: 8
            background: Rectangle {
                color: EditorStyle.panelRaised
                border.color: EditorStyle.border
                radius: 6
            }

            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: (card.index + 1) + "."
                        color: EditorStyle.muted
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ale.types
                        textRole: "label"
                        valueRole: "type"
                        currentIndex: Math.max(0, ale.types.findIndex(t => t.type === card.action.type))
                        onActivated: index => ale.setType(card.index, ale.types[index].type)
                    }
                    ToolButton { text: "▲"; enabled: card.index > 0; onClicked: ale.move(card.index, -1) }
                    ToolButton { text: "▼"; enabled: card.index < ale.actions.length - 1; onClicked: ale.move(card.index, 1) }
                    ToolButton {
                        text: "✕"
                        onClicked: ale.remove(card.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove this action")
                    }
                }

                Repeater {
                    model: card.def.fields
                    delegate: FieldEditor {
                        required property var modelData
                        field: modelData
                        editor: ale.editor
                        value: card.action[modelData.path]
                        onCommit: v => ale.setKey(card.index, modelData.path, v)
                        onReset: ale.setKey(card.index, modelData.path, undefined)
                    }
                }

                // The command's own settings (TV / DIRECTV remote: the TV, its address, the key or channel).
                Repeater {
                    model: ale.argFieldsOf(card.action)
                    delegate: ColumnLayout {
                        id: argRow
                        required property var modelData
                        readonly property var args: card.action.args ?? ({})
                        readonly property string text: String(args[modelData.path] ?? "")
                        visible: ale.argShown(modelData, args)
                        Layout.fillWidth: true
                        spacing: 2
                        FieldEditor {
                            field: argRow.modelData
                            editor: ale.editor
                            value: argRow.args[argRow.modelData.path]
                            onCommit: v => ale.setArg(card.index, argRow.modelData, v)
                            onReset: ale.setArg(card.index, argRow.modelData, undefined)
                        }
                        // The TV names already on the pages.
                        ComboBox {
                            readonly property var names: argRow.modelData.suggest === "tvNames" && ale.editor.revision >= 0
                                                         ? ale.editor.tvNames() : []
                            visible: names.length > 0
                            Layout.fillWidth: true
                            model: [qsTr("TV names on the pages…")].concat(names)
                            currentIndex: 0
                            onActivated: index => {
                                if (index > 0)
                                    ale.setArg(card.index, argRow.modelData, names[index - 1])
                                currentIndex = 0
                            }
                        }
                        Label {
                            visible: argRow.modelData.tvAddress === true && argRow.text.trim() !== ""
                                     && !ale.privateIp(argRow.text.trim())
                            text: qsTr("Not a store network address (10.x.x.x, 172.16-31.x.x or 192.168.x.x): the button won't work.")
                            color: EditorStyle.warning
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
                CheckBox {
                    readonly property string box: String((card.action.args ?? {}).box ?? "").trim()
                    readonly property int count: box !== "" && ale.editor.revision >= 0 ? ale.editor.tvButtonCount(box) : 0
                    visible: ale.hasTvFields(card.action) && box !== ""
                    Layout.fillWidth: true
                    checked: ale.tvAll
                    text: qsTr("Same address and type on all %n button(s) for this TV (every page)", "", count)
                    onToggled: {
                        ale.tvAll = checked
                        if (checked)
                            ale.editor.setActionsForTv(ale.copyActions(), box)
                    }
                }
            }
        }
    }

    Button {
        text: qsTr("+ Add action")
        onClicked: addMenu.popup(0, height)   // under the button (a touch has no pointer)
        Menu {
            id: addMenu
            Repeater {
                model: ale.types
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.label
                    onTriggered: ale.add(modelData.type)
                }
            }
        }
    }
}
