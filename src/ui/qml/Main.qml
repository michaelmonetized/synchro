import QtQuick
import Synchro.Theme

Window {
    id: root

    // Context properties cannot be bound as `foo: foo` on a child — the AOT
    // lookup hits the child's own unset property. Alias here first.
    readonly property var files: directoryModel
    readonly property var listing: filterProxy
    readonly property var history: navStack
    readonly property var keys: keyMachine
    property bool gridMode: false

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: root.files && root.files.path.length ? root.files.path : "Synchro"
    color: Theme.background

    PathBar {
        id: pathBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        fileModel: root.files
        navStack: root.history
    }

    CommandField {
        id: commandField
        objectName: "commandField"
        anchors.top: pathBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        keyMachine: root.keys
    }

    FileList {
        id: fileList
        objectName: "fileList"
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !root.gridMode
        enabled: visible
        fileModel: root.files
        filterProxy: root.listing
        navStack: root.history
        keyMachine: root.keys
        onViewToggleRequested: root.gridMode = true
    }

    FileGrid {
        id: fileGrid
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.gridMode
        enabled: visible
        fileModel: root.files
        keyMachine: root.keys
        onViewToggleRequested: root.gridMode = false
    }

    PeekOverlay {
        anchors.fill: parent
        host: typeof hostApi !== "undefined" ? hostApi : null
    }

    Shortcut {
        sequence: "Ctrl+K"
        enabled: root.keys && !root.keys.peekOpen
        onActivated: root.keys.focusFilter()
    }

    Shortcut {
        sequence: "Ctrl+L"
        enabled: root.keys && !root.keys.peekOpen
        onActivated: root.keys.focusJump()
    }

    Connections {
        target: root.files
        function onPathChanged() {
            if (root.gridMode)
                fileGrid.forceActiveFocus()
            else
                fileList.forceActiveFocus()
        }
    }

    Connections {
        target: root.keys
        function onModeChanged() {
            if (root.keys.listFocused) {
                if (root.gridMode)
                    fileGrid.forceActiveFocus()
                else
                    fileList.forceActiveFocus()
            } else {
                commandField.focusInput()
            }
        }
    }

    onGridModeChanged: {
        if (root.gridMode)
            fileGrid.forceActiveFocus()
        else
            fileList.forceActiveFocus()
    }

    Component.onCompleted: fileList.forceActiveFocus()
}
