import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var keyMachine

    readonly property bool fieldActive: keyMachine && keyMachine.fieldFocused

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
        text: (root.keyMachine && root.keyMachine.mode === "field-command") ? ":" : "/"
        color: root.fieldActive ? Theme.accent : Theme.muted
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
            return t
        }

        onTextEdited: {
            if (!root.keyMachine)
                return
            // Keep the ':' sigil on the C++ side so parse order stays K7.
            if (root.keyMachine.mode === "field-command")
                root.keyMachine.fieldText = ":" + text
            else
                root.keyMachine.fieldText = text
        }

        onActiveFocusChanged: {
            if (activeFocus && root.keyMachine && root.keyMachine.listFocused)
                root.keyMachine.focusFilter()
        }

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
        text: (root.keyMachine && root.keyMachine.mode === "field-command")
              ? "command…"
              : "filter or command…"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }

    Text {
        id: hint
        anchors.right: parent.right
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        text: (root.keyMachine && root.keyMachine.statusMessage.length)
              ? root.keyMachine.statusMessage
              : "Ctrl+K"
        color: (root.keyMachine && root.keyMachine.statusMessage.length)
               ? Theme.accent
               : Theme.muted
        elide: Text.ElideLeft
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.normalBorder
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
