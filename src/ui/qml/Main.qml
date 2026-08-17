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
    readonly property var chips: typeof locationChips !== "undefined" ? locationChips : null
    readonly property var selection: typeof selectionModel !== "undefined" ? selectionModel : null
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
        locationChips: root.chips
    }

    CommandField {
        id: commandField
        objectName: "commandField"
        anchors.top: pathBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        keyMachine: root.keys
        fileModel: root.files
    }

    FileList {
        id: fileList
        objectName: "fileList"
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        visible: !root.gridMode
        enabled: visible
        fileModel: root.files
        filterProxy: root.listing
        navStack: root.history
        keyMachine: root.keys
        selection: root.selection
        onViewToggleRequested: if (root.keys) root.keys.gridMode = true
        onDoRequested: if (typeof hostApi !== "undefined" && hostApi)
            hostApi.openDoLayer()
    }

    FileGrid {
        id: fileGrid
        objectName: "fileGrid"
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        visible: root.gridMode
        enabled: visible
        fileModel: root.files
        filterProxy: root.listing
        keyMachine: root.keys
        selection: root.selection
        onViewToggleRequested: if (root.keys) root.keys.gridMode = false
        onDoRequested: if (typeof hostApi !== "undefined" && hostApi)
            hostApi.openDoLayer()
    }

    StatusLine {
        id: statusLine
        objectName: "statusLine"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        fileModel: root.files
        keyMachine: root.keys
        selection: root.selection
        filterProxy: root.listing
        host: typeof hostApi !== "undefined" ? hostApi : null
    }

    Confirm {
        anchors.fill: parent
        z: 105
        keyMachine: root.keys
    }

    PeekOverlay {
        anchors.fill: parent
        host: typeof hostApi !== "undefined" ? hostApi : null
        keys: root.keys
    }

    DoOverlay {
        id: actionOverlay
        anchors.fill: parent
        host: typeof hostApi !== "undefined" ? hostApi : null
        onClosed: {
            if (root.keys && root.keys.fieldFocused)
                commandField.focusInput()
            else
                root.focusListing()
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
            root.focusListing()
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
            if (root.gridMode) {
                root.keys.gridStride = fileGrid.columns
                fileGrid.forceActiveFocus()
            } else {
                root.keys.gridStride = 1
                fileList.forceActiveFocus()
            }
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

    function focusListing() {
        if (root.keys && !root.keys.listFocused)
            return
        if (root.gridMode)
            fileGrid.forceActiveFocus()
        else
            fileList.forceActiveFocus()
    }

    onActiveChanged: if (active)
        Qt.callLater(root.focusListing)

    onActiveFocusItemChanged: {
        if (!active || (root.keys && !root.keys.listFocused))
            return
        if (!activeFocusItem || activeFocusItem === root)
            Qt.callLater(root.focusListing)
    }

    Component.onCompleted: Qt.callLater(root.focusListing)
}
