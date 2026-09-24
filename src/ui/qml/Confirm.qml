import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var keyMachine

    readonly property bool open: keyMachine &&
                                 (keyMachine.mode === "rename-inline" ||
                                  keyMachine.mode === "confirm-dialog")
    readonly property bool yn: keyMachine && keyMachine.ynPrompt &&
                               keyMachine.mode === "confirm-dialog"

    visible: open
    z: 10
    focus: open && yn
    activeFocusOnTab: yn

    function syncInput() {
        if (root.yn || !root.keyMachine)
            return
        const next = root.keyMachine.promptText
        if (input.text !== next)
            input.text = next
    }

    function focusInput() {
        if (root.yn) {
            root.forceActiveFocus()
            return
        }
        // promptChanged can fire before mode is rename-inline (open
        // still false). Sync again when the overlay actually appears.
        root.syncInput()
        input.forceActiveFocus()
        input.selectAll()
    }

    Keys.onPressed: function (event) {
        if (!root.yn || !root.keyMachine)
            return
        if (root.keyMachine.handleListKey(event.key, event.modifiers, event.text))
            event.accepted = true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: {}
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.72
    }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.space(32), 420)
        implicitHeight: col.implicitHeight + Theme.space(24)
        color: Theme.background
        border.width: 1
        border.color: Theme.normalBorder
        radius: Theme.radius

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space(12)
            anchors.rightMargin: Theme.space(12)
            spacing: Theme.space(8)

            Text {
                text: {
                    if (!root.keyMachine)
                        return "Rename"
                    if (root.keyMachine.promptKind === "mkdir")
                        return "New folder"
                    if (root.keyMachine.promptKind === "unlink")
                        return "Delete permanently"
                    if (root.keyMachine.promptKind === "empty-trash")
                        return "Empty trash"
                    if (root.keyMachine.promptKind === "commit")
                        return "Commit message"
                    if (root.keyMachine.promptKind === "gitcp")
                        return "Commit and push"
                    if (root.keyMachine.promptKind === "ask")
                        return root.keyMachine.promptQuestion
                    return "Rename"
                }
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            Text {
                visible: root.yn
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.keyMachine ? root.keyMachine.promptText : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }

            Text {
                visible: root.yn
                text: "y / n / Esc"
                color: Theme.muted
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontBody
            }

            TextInput {
                id: input
                objectName: "confirmInput"
                visible: !root.yn
                width: parent.width
                color: Theme.foreground
                selectedTextColor: Theme.background
                selectionColor: Theme.accent
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontBody
                selectByMouse: true
                text: root.yn ? "" : (root.keyMachine ? root.keyMachine.promptText : "")

                onTextEdited: {
                    if (root.keyMachine && !root.yn)
                        root.keyMachine.promptText = text
                }

                Keys.onPressed: function (event) {
                    if (!root.keyMachine)
                        return
                    if (root.keyMachine.handleListKey(event.key, event.modifiers, event.text))
                        event.accepted = true
                }
            }
        }
    }

    Connections {
        target: root.keyMachine
        function onModeChanged() {
            if (root.open)
                root.focusInput()
        }
        function onPromptChanged() {
            if (root.open)
                root.syncInput()
        }
    }
}
