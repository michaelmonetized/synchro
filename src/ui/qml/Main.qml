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
    readonly property bool gridMode: root.keys ? root.keys.gridMode : false

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
        onViewToggleRequested: if (root.keys) root.keys.gridMode = true
    }

    FileGrid {
        id: fileGrid
        objectName: "fileGrid"
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.gridMode
        enabled: visible
        fileModel: root.files
        keyMachine: root.keys
        onViewToggleRequested: if (root.keys) root.keys.gridMode = false
    }

    PeekOverlay {
        anchors.fill: parent
        host: typeof hostApi !== "undefined" ? hostApi : null
    }

    Item {
        id: actionOverlay
        objectName: "actionOverlay"
        anchors.fill: parent
        z: 110
        visible: typeof hostApi !== "undefined" && hostApi && hostApi.actionOpen
        focus: false

        Rectangle {
            anchors.fill: parent
            color: Theme.background
            opacity: 0.86
            MouseArea {
                anchors.fill: parent
                onClicked: if (typeof hostApi !== "undefined" && hostApi)
                    hostApi.closeAction()
            }
        }

        Rectangle {
            id: actionFrame
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.space(48), 480)
            height: Math.min(parent.height - Theme.space(48), 360)
            color: Theme.background
            border.color: Theme.normalBorder
            border.width: 1

            Item {
                id: actionSurface
                objectName: "actionSurface"
                anchors.fill: parent
                anchors.margins: Theme.space(8)
            }
        }

        function reparentAction() {
            if (typeof hostApi === "undefined" || !hostApi || !hostApi.actionItem)
                return
            hostApi.actionItem.parent = actionSurface
            hostApi.actionItem.anchors.fill = actionSurface
            hostApi.actionItem.forceActiveFocus()
        }

        Connections {
            target: typeof hostApi !== "undefined" ? hostApi : null
            function onActionItemChanged() { actionOverlay.reparentAction() }
            function onActionOpenChanged() {
                if (typeof hostApi !== "undefined" && hostApi && hostApi.actionOpen)
                    actionOverlay.reparentAction()
                if (typeof hostApi !== "undefined" && hostApi && !hostApi.actionOpen) {
                    if (root.keys && root.keys.fieldFocused)
                        commandField.focusInput()
                    else if (root.gridMode)
                        fileGrid.forceActiveFocus()
                    else
                        fileList.forceActiveFocus()
                }
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+K"
        enabled: root.keys && !root.keys.peekOpen && !root.keys.actionOpen
        onActivated: root.keys.focusFilter()
    }

    Shortcut {
        sequence: "Ctrl+L"
        enabled: root.keys && !root.keys.peekOpen && !root.keys.actionOpen
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
        function onGridModeChanged() {
            if (root.gridMode)
                fileGrid.forceActiveFocus()
            else
                fileList.forceActiveFocus()
        }
    }

    Item {
        id: helpOverlay
        objectName: "helpOverlay"
        anchors.fill: parent
        z: 120
        visible: root.keys && root.keys.helpOpen
        focus: false

        Rectangle {
            anchors.fill: parent
            color: Theme.background
            opacity: 0.9
            MouseArea {
                anchors.fill: parent
                onClicked: if (root.keys) root.keys.escape()
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - Theme.space(48), 560)
            height: Math.min(parent.height - Theme.space(48), 280)
            color: Theme.background
            border.color: Theme.normalBorder
            border.width: 1

            Text {
                anchors.fill: parent
                anchors.margins: Theme.space(16)
                text: root.keys ? root.keys.helpText : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }
        }
    }

    Component.onCompleted: fileList.forceActiveFocus()
}
