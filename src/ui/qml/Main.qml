import QtQuick
import Synchro.Theme

Window {
    id: root

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: directoryModel.path.length ? directoryModel.path : "Synchro"
    color: Theme.background

    PathBar {
        id: pathBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        fileModel: directoryModel
        navStack: navStack
    }

    CommandField {
        id: commandField
        anchors.top: pathBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        keyMachine: keyMachine
    }

    FileList {
        id: fileList
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        fileModel: directoryModel
        filterProxy: filterProxy
        navStack: navStack
        keyMachine: keyMachine
        Component.onCompleted: forceActiveFocus()
    }

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: keyMachine.focusFilter()
    }

    Shortcut {
        sequence: "Ctrl+L"
        onActivated: keyMachine.focusJump()
    }

    Connections {
        target: directoryModel
        function onPathChanged() {
            fileList.forceActiveFocus()
        }
    }

    Connections {
        target: keyMachine
        function onModeChanged() {
            if (keyMachine.listFocused)
                fileList.forceActiveFocus()
            else
                commandField.focusInput()
        }
    }
}
