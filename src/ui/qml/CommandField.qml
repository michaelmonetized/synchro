import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var keyMachine
    property var fileModel

    readonly property bool fieldActive: keyMachine && keyMachine.fieldFocused
    readonly property bool typingSearch: keyMachine && keyMachine.mode === "field-search"
    readonly property bool viewingSearch: fileModel && fileModel.isSearch
    readonly property bool contentSearch: {
        var t = keyMachine ? keyMachine.fieldText : ""
        return t.length >= 2 && t.charAt(0) === "?" && t.charAt(1) === "?"
    }
    readonly property bool viewToggle: keyMachine &&
                                       !keyMachine.fieldFocused &&
                                       !keyMachine.peekOpen &&
                                       !keyMachine.statusMessage.length &&
                                       !typingSearch && !viewingSearch &&
                                       keyMachine.mode !== "field-filter" &&
                                       keyMachine.mode !== "field-command" &&
                                       keyMachine.mode !== "field-jump"

    implicitHeight: Math.max(Theme.fontBody + Theme.space(10), 28)

    function focusInput() {
        input.forceActiveFocus()
        if (input.text.length > 0)
            input.cursorPosition = input.text.length
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    Text {
        id: prompt
        anchors.left: parent.left
        anchors.leftMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        text: {
            if (root.keyMachine && root.keyMachine.mode === "field-command")
                return ":"
            if (root.keyMachine && root.keyMachine.mode === "field-search")
                return root.contentSearch ? "??" : "?"
            return "/"
        }
        color: (root.fieldActive || root.viewingSearch) ? Theme.accent : Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    TextInput {
        id: input
        objectName: "commandInput"
        anchors.left: prompt.right
        anchors.leftMargin: Theme.space(8)
        anchors.right: hint.left
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        clip: true
        color: Theme.foreground
        selectedTextColor: Theme.background
        selectionColor: Theme.accent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        selectByMouse: true
        activeFocusOnPress: true
        activeFocusOnTab: true
        focus: false
        text: {
            var t = root.keyMachine ? root.keyMachine.fieldText : ""
            if (root.keyMachine && root.keyMachine.mode === "field-command" &&
                    t.length > 0 && t.charAt(0) === ":")
                return t.substring(1)
            if (root.keyMachine && root.keyMachine.mode === "field-search") {
                if (t.length >= 2 && t.charAt(0) === "?" && t.charAt(1) === "?")
                    return t.substring(2)
                if (t.length > 0 && t.charAt(0) === "?")
                    return t.substring(1)
            }
            return t
        }

        onTextEdited: {
            if (!root.keyMachine)
                return
            // Keep the ':' / '?' / '??' sigil on the C++ side so parse
            // order stays K7. The prompt already shows the sigil — do
            // not prepend another '?' onto a pasted "??query".
            if (root.keyMachine.mode === "field-command")
                root.keyMachine.fieldText = ":" + text
            else if (root.keyMachine.mode === "field-search") {
                if (text.indexOf("??") === 0)
                    root.keyMachine.fieldText = text
                else if (text.length > 0 && text.charAt(0) === "?")
                    root.keyMachine.fieldText = "?" + text
                else if (root.contentSearch)
                    root.keyMachine.fieldText = "??" + text
                else
                    root.keyMachine.fieldText = "?" + text
            } else
                root.keyMachine.fieldText = text
        }

        onActiveFocusChanged: {
            if (activeFocus && root.keyMachine && root.keyMachine.listFocused &&
                    !root.keyMachine.peekOpen)
                root.keyMachine.focusFilter()
        }

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: function (event) {
            if (!root.keyMachine)
                return
            if (root.keyMachine.handleFieldKey(event.key, event.modifiers))
                event.accepted = true
        }
    }

    Text {
        visible: input.text.length === 0
        enabled: false
        anchors.left: input.left
        anchors.right: input.right
        anchors.verticalCenter: parent.verticalCenter
        text: {
            if (root.keyMachine && root.keyMachine.mode === "field-command")
                return "command…"
            if (root.keyMachine && root.keyMachine.mode === "field-search")
                return root.contentSearch
                       ? "search content…  Tab listing"
                       : "search names…  Tab listing"
            return "filter or command…"
        }
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }

    Text {
        id: hint
        objectName: "commandModeHint"
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        text: {
            if (root.keyMachine && root.keyMachine.statusMessage.length)
                return root.keyMachine.statusMessage
            if (root.typingSearch)
                return root.contentSearch ? "CONTENT" : "SEARCH"
            if (root.viewingSearch)
                return root.fileModel.isContentSearch ? "CONTENT" : "RESULTS"
            if (root.keyMachine && root.keyMachine.mode === "field-filter")
                return "FILTER"
            if (root.keyMachine && root.keyMachine.mode === "field-command")
                return "COMMAND"
            if (root.keyMachine && root.keyMachine.mode === "field-jump")
                return "JUMP"
            if (root.viewToggle && root.keyMachine.fsnMode)
                return "FSN"
            if (root.viewToggle)
                return root.keyMachine.gridMode ? "GRID" : "LIST"
            return ""
        }
        color: (root.fieldActive || root.viewingSearch ||
                (root.keyMachine && root.keyMachine.statusMessage.length))
               ? Theme.accent : Theme.muted
        elide: Text.ElideLeft
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody

        MouseArea {
            anchors.fill: parent
            enabled: root.viewToggle
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (!root.keyMachine)
                    return
                if (root.keyMachine.fsnMode)
                    root.keyMachine.fsnMode = false
                else
                    root.keyMachine.gridMode = !root.keyMachine.gridMode
            }
        }
    }

    Connections {
        target: root.keyMachine
        function onJumpEpochChanged() {
            if (root.keyMachine && root.keyMachine.fieldFocused) {
                input.forceActiveFocus()
                input.selectAll()
            }
        }
    }
}
