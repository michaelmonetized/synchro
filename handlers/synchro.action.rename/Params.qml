import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    function currentName() {
        if (root.host && root.host.doTargetName)
            return root.host.doTargetName
        if (!root.file)
            return ""
        var parts = String(root.file).split("/")
        return decodeURIComponent(parts[parts.length - 1])
    }

    function selectUsefulPart() {
        var name = renameInput.text
        var dot = name.lastIndexOf(".")
        renameInput.select(0, dot > 0 ? dot : name.length)
    }

    function beginEditing() {
        if (!root.visible)
            return
        renameInput.forceActiveFocus()
        root.selectUsefulPart()
    }

    function commit() {
        if (!root.host || renameInput.text.length === 0)
            return false
        return root.host.renameDoTarget(renameInput.text)
    }

    function actionKey(key, modifiers) {
        if (key === Qt.Key_Return || key === Qt.Key_Enter)
            return root.commit()
        return false
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.alpha(Theme.darkerBackground, 0.18)
        radius: Theme.radius
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.space(14)
        anchors.rightMargin: Theme.space(14)
        spacing: Theme.space(8)

        Text {
            text: "NEW NAME"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.bold: true
            font.letterSpacing: 1
        }

        Rectangle {
            width: parent.width
            height: Theme.space(42)
            color: Theme.alpha(Theme.background, 0.72)
            border.color: renameInput.activeFocus ? Theme.accent
                                                  : Theme.normalBorder
            border.width: 1
            radius: Theme.radius

            TextInput {
                id: renameInput
                objectName: "doRenameInput"
                anchors.fill: parent
                anchors.leftMargin: Theme.space(11)
                anchors.rightMargin: Theme.space(11)
                verticalAlignment: TextInput.AlignVCenter
                text: root.currentName()
                color: Theme.brightForeground
                selectionColor: Theme.selectedFill
                selectedTextColor: Theme.brightForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.bold: true
                selectByMouse: true
                clip: true

                Keys.onEscapePressed: if (root.host) root.host.closeAction()
                Keys.onReturnPressed: root.commit()
                Keys.onEnterPressed: root.commit()
            }
        }

        Text {
            width: parent.width
            text: "The extension is preserved in the field, but left unselected for quick edits."
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    Connections {
        target: root.host
        function onDoFocusChanged() {
            if (root.host && root.host.doParamsFocused)
                Qt.callLater(root.beginEditing)
        }
    }

    Component.onCompleted: Qt.callLater(root.beginEditing)
}
