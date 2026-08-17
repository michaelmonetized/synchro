import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var keyMachine
    property var fileModel

    readonly property bool fieldActive: keyMachine && keyMachine.fieldFocused
    readonly property bool typingSearch: keyMachine && keyMachine.mode === "field-search"
    readonly property bool viewingSearch: fileModel && fileModel.isSearch

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
                return "?"
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
            if (root.keyMachine && root.keyMachine.mode === "field-search" &&
                    t.length > 0 && t.charAt(0) === "?")
                return t.substring(1)
            return t
        }

        onTextEdited: {
            if (!root.keyMachine)
                return
            // Keep the ':' / '?' sigil on the C++ side so parse order stays K7.
            if (root.keyMachine.mode === "field-command")
                root.keyMachine.fieldText = ":" + text
            else if (root.keyMachine.mode === "field-search")
                root.keyMachine.fieldText = "?" + text
            else
                root.keyMachine.fieldText = text
        }

        onActiveFocusChanged: {
            if (activeFocus && root.keyMachine && root.keyMachine.listFocused)
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
                return "search names…  Tab listing"
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
                return "SEARCH   Tab listing"
            if (root.viewingSearch)
                return "RESULTS   Tab search"
            if (root.keyMachine && root.keyMachine.mode === "field-filter")
                return "FILTER"
            if (root.keyMachine && root.keyMachine.mode === "field-command")
                return "COMMAND"
            if (root.keyMachine && root.keyMachine.mode === "field-jump")
                return "JUMP"
            return "LIST   Tab search"
        }
        color: (root.fieldActive || root.viewingSearch ||
                (root.keyMachine && root.keyMachine.statusMessage.length))
               ? Theme.accent : Theme.muted
        elide: Text.ElideLeft
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.fieldActive ? 2 : 1
        color: (root.fieldActive || root.viewingSearch) ? Theme.accent
                                                        : Theme.normalBorder
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
