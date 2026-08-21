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
    readonly property bool fsnMode: root.keys ? root.keys.fsnMode : false

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: root.files && root.files.path.length ? root.files.path : "Synchro"
    color: Theme.background

    PathBar {
        id: pathBar
        objectName: "pathBar"
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
        filterProxy: root.listing
    }

    Rectangle {
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        color: Theme.opaqueBackground
        z: 0
    }

    // ---- dock panel (synchro.panel.*), e.g. the terminal ----
    readonly property string panelId: root.keys ? root.keys.panelId : ""
    readonly property bool panelOpen: panelId.length > 0 &&
                                      panelDock.sourceUrl.toString().length > 0
    readonly property string panelSide: root.keys ? root.keys.panelSide
                                                  : "bottom"
    readonly property bool panelBottom: panelSide === "bottom"
    readonly property bool panelLeft: panelSide === "left"
    readonly property bool panelRight: panelSide === "right"

    function focusPanel() {
        if (panelDock.panelItem && panelDock.panelItem.focusContent)
            panelDock.panelItem.focusContent()
        else if (panelDock.panelItem)
            panelDock.panelItem.forceActiveFocus()
    }

    function toggleTerminalPanel() {
        if (!root.keys)
            return
        if (!root.panelOpen) {
            root.keys.panelId = "synchro.panel.terminal"
            Qt.callLater(root.focusPanel)
        } else if (panelDock.activeFocus) {
            root.focusListingForce()
        } else {
            root.focusPanel()
        }
    }

    Loader {
        id: listingLoader
        anchors.top: commandField.bottom
        anchors.left: root.panelOpen && root.panelLeft ? panelDock.right
                                                       : parent.left
        anchors.right: root.panelOpen && root.panelRight ? panelDock.left
                                                         : parent.right
        anchors.bottom: root.panelOpen && root.panelBottom ? panelDock.top
                                                           : statusLine.top
        z: 1
        sourceComponent: root.fsnMode ? fsnComp
                                      : (root.gridMode ? gridComp : listComp)
        onLoaded: {
            if (!root.fsnMode && root.gridMode && item && root.keys)
                root.keys.gridStride = item.columns
            if (root.keys && root.keys.listFocused && item)
                item.forceActiveFocus()
        }
    }

    Component {
        id: listComp
        FileList {
            objectName: "fileList"
            fileModel: root.files
            filterProxy: root.listing
            navStack: root.history
            keyMachine: root.keys
            selection: root.selection
            host: typeof hostApi !== "undefined" ? hostApi : null
            fileOps: typeof fileOpEngine !== "undefined" ? fileOpEngine : null
            onViewToggleRequested: if (root.keys) root.keys.gridMode = true
            onDoRequested: if (typeof hostApi !== "undefined" && hostApi)
                hostApi.openDoLayer()
        }
    }

    Component {
        id: fsnComp
        FileFsn {
            objectName: "fileFsn"
            fileModel: root.files
            filterProxy: root.listing
            navStack: root.history
            keyMachine: root.keys
            selection: root.selection
            host: typeof hostApi !== "undefined" ? hostApi : null
            fileOps: typeof fileOpEngine !== "undefined" ? fileOpEngine : null
            onViewToggleRequested: if (root.keys)
                                       root.keys.fsnTreeView = !root.keys.fsnTreeView
            onDoRequested: if (typeof hostApi !== "undefined" && hostApi)
                hostApi.openDoLayer()
        }
    }

    Component {
        id: gridComp
        FileGrid {
            objectName: "fileGrid"
            fileModel: root.files
            filterProxy: root.listing
            navStack: root.history
            keyMachine: root.keys
            selection: root.selection
            host: typeof hostApi !== "undefined" ? hostApi : null
            fileOps: typeof fileOpEngine !== "undefined" ? fileOpEngine : null
            onViewToggleRequested: if (root.keys) root.keys.gridMode = false
            onDoRequested: if (typeof hostApi !== "undefined" && hostApi)
                hostApi.openDoLayer()
        }
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
        fileOps: typeof fileOpEngine !== "undefined" ? fileOpEngine : null
    }

    Confirm {
        anchors.fill: parent
        z: 105
        keyMachine: root.keys
    }

    FocusScope {
        id: panelDock
        objectName: "panelDock"
        // Keep-alive: once a panel has loaded it stays loaded while hidden,
        // so the terminal's shell survives Ctrl+` toggles.
        property url loadedUrl: ""
        readonly property url sourceUrl: root.panelId.length &&
                                         typeof hostApi !== "undefined" && hostApi
                                         ? hostApi.panelSource(root.panelId)
                                         : ""
        function latchSource() {
            if (sourceUrl.toString().length > 0)
                loadedUrl = sourceUrl
        }
        onSourceUrlChanged: latchSource()
        Component.onCompleted: latchSource() // change handlers skip the initial value
        readonly property var panelItem: panelLoader.item
        readonly property int span: appConfig
                                    ? Math.min(appConfig.panelSize,
                                               (root.panelBottom ? root.height
                                                                 : root.width) * 0.7)
                                    : 260
        visible: root.panelOpen
        z: 2
        onActiveFocusChanged: if (root.keys)
                                  root.keys.panelFocused = activeFocus
        anchors.left: root.panelRight ? undefined : parent.left
        anchors.right: root.panelLeft ? undefined : parent.right
        anchors.top: root.panelBottom ? undefined : commandField.bottom
        anchors.bottom: statusLine.top
        width: span
        height: span

        Rectangle {
            anchors.fill: parent
            color: Theme.opaqueBackground
        }

        Loader {
            id: panelLoader
            anchors.fill: parent
            anchors.topMargin: root.panelBottom ? 6 : 0
            anchors.leftMargin: root.panelRight ? 6 : 0
            anchors.rightMargin: root.panelLeft ? 6 : 0
            source: panelDock.loadedUrl
            focus: true
            onLoaded: {
                if (!item)
                    return
                if (item.host !== undefined)
                    item.host = typeof hostApi !== "undefined" ? hostApi : null
                if (item.fileModel !== undefined)
                    item.fileModel = root.files
                if (item.navStack !== undefined)
                    item.navStack = root.history
            }
        }

        // Inner-edge separator + drag-resize handle, one pair per side.
        Rectangle {
            visible: root.panelBottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: panelDock.activeFocus ? 2 : 1
            color: panelDock.activeFocus ? Theme.accent : Theme.normalBorder
        }
        Rectangle {
            visible: root.panelRight
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: panelDock.activeFocus ? 2 : 1
            color: panelDock.activeFocus ? Theme.accent : Theme.normalBorder
        }
        Rectangle {
            visible: root.panelLeft
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: panelDock.activeFocus ? 2 : 1
            color: panelDock.activeFocus ? Theme.accent : Theme.normalBorder
        }

        MouseArea {
            id: panelResizeH
            visible: root.panelBottom
            z: 5
            cursorShape: Qt.SplitVCursor
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 6
            preventStealing: true
            property real startSize: 0
            property real startY: 0
            onPressed: function (mouse) {
                startSize = appConfig ? appConfig.panelSize : 260
                startY = mapToItem(null, mouse.x, mouse.y).y
            }
            onPositionChanged: function (mouse) {
                if (!pressed || !appConfig)
                    return
                var y = mapToItem(null, mouse.x, mouse.y).y
                appConfig.panelSize = Math.round(startSize + (startY - y))
            }
        }
        MouseArea {
            id: panelResizeV
            visible: !root.panelBottom
            z: 5
            cursorShape: Qt.SplitHCursor
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: root.panelRight ? parent.left : undefined
            anchors.right: root.panelLeft ? parent.right : undefined
            width: 6
            preventStealing: true
            property real startSize: 0
            property real startX: 0
            onPressed: function (mouse) {
                startSize = appConfig ? appConfig.panelSize : 300
                startX = mapToItem(null, mouse.x, mouse.y).x
            }
            onPositionChanged: function (mouse) {
                if (!pressed || !appConfig)
                    return
                var x = mapToItem(null, mouse.x, mouse.y).x
                var delta = root.panelRight ? (startX - x) : (x - startX)
                appConfig.panelSize = Math.round(startSize + delta)
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+`"
        enabled: root.keys && !root.keys.chooserMode
        onActivated: root.toggleTerminalPanel()
    }

    Connections {
        target: root.keys
        function onPanelFocusRequested() { Qt.callLater(root.focusPanel) }
    }

    PeekOverlay {
        anchors.fill: parent
        host: typeof hostApi !== "undefined" ? hostApi : null
        keys: root.keys
        indexModel: root.listing
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
        enabled: root.keys && !root.keys.peekOpen && !root.keys.actionOpen &&
                 !panelDock.activeFocus
        onActivated: root.keys.focusFilter()
    }

    Shortcut {
        sequence: "Ctrl+L"
        enabled: root.keys && !root.keys.peekOpen && !root.keys.actionOpen &&
                 !panelDock.activeFocus
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
            if (panelDock.activeFocus)
                return
            if (root.keys.listFocused)
                root.focusListing()
            else
                commandField.focusInput()
        }
        function onGridModeChanged() {
            if (!root.gridMode && root.keys)
                root.keys.gridStride = 1
            Qt.callLater(root.focusListing)
        }
        function onFsnModeChanged() {
            Qt.callLater(root.focusListing)
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
        // The terminal keeps the keyboard until the user flips back
        // (Ctrl+` or a click); navigation events must not steal it.
        if (panelDock.activeFocus)
            return
        if (listingLoader.item)
            listingLoader.item.forceActiveFocus()
    }
    function focusListingForce() {
        if (listingLoader.item)
            listingLoader.item.forceActiveFocus()
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
