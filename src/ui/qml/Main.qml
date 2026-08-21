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
    readonly property bool panelTopSide: panelSide === "top"
    readonly property bool panelLeft: panelSide === "left"
    readonly property bool panelRight: panelSide === "right"
    readonly property bool panelHorizontal: panelBottom || panelTopSide

    // Grip-drag re-docking state (the gesture only starts from the grip,
    // so panel content keeps its own drag and drop untouched).
    property bool panelDragging: false
    property real panelDragX: 0
    property real panelDragY: 0
    property string panelDragTargetId: ""
    property string panelDragLabel: ""

    // Panel apps: which are relevant to the current selection (parked
    // pills) and which have been opened this session (kept alive).
    property var relevantPanels: []
    property var openedPanels: []

    function refreshRelevantPanels() {
        if (typeof hostApi === "undefined" || !hostApi)
            return
        var list = hostApi.relevantPanels()
        for (var i = 0; i < list.length; ++i)
            root.panelMeta[list[i].id] = { name: list[i].name,
                                           glyph: list[i].glyph }
        root.relevantPanels = list
    }

    // Remember name/glyph for every app we have seen, so pills for
    // opened-but-no-longer-relevant apps stay labeled.
    property var panelMeta: ({})

    function panelNameFor(id) {
        var m = root.panelMeta[id]
        if (m && m.name)
            return m.name
        var dot = id.lastIndexOf(".")
        return dot >= 0 ? id.substring(dot + 1) : id
    }

    function panelGlyphFor(id) {
        var m = root.panelMeta[id]
        if (m && m.glyph && m.glyph.length)
            return m.glyph
        var n = root.panelNameFor(id)
        return n.length ? n.charAt(0).toUpperCase() : "?"
    }

    // Parked pills: relevant + opened apps, minus the one on the dock.
    readonly property var parkedPanels: {
        var out = []
        var seen = {}
        var cur = root.panelOpen ? root.panelId : ""
        var i
        for (i = 0; i < root.relevantPanels.length; ++i) {
            var rp = root.relevantPanels[i]
            if (rp.id !== cur && !seen[rp.id]) {
                seen[rp.id] = true
                out.push({ id: rp.id, name: rp.name })
            }
        }
        for (i = 0; i < root.openedPanels.length; ++i) {
            var oid = root.openedPanels[i]
            if (oid !== cur && !seen[oid]) {
                seen[oid] = true
                out.push({ id: oid, name: root.panelNameFor(oid) })
            }
        }
        return out
    }


    function focusPanel() {
        var it = panelDock.panelItems[root.panelId]
        if (it && it.focusContent)
            it.focusContent()
        else if (it)
            it.forceActiveFocus()
    }

    // Shared drop rule for both grip gestures: an edge zone docks (and
    // opens) the panel there; center/outside closes or cancels.
    function applyPanelDrop() {
        var z = panelDropZones.zone
        var target = root.panelDragTargetId
        root.panelDragging = false
        root.panelDragTargetId = ""
        if (!root.keys || !target.length)
            return
        if (z === "close") {
            // dropping the docked app closes it; a parked pill just cancels
            if (target === root.keys.panelId)
                root.keys.panelId = ""
        } else if (z.length) {
            root.keys.panelSide = z
            root.keys.panelId = target
            Qt.callLater(root.focusPanel)
        }
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

        HoverHandler {
            enabled: root.hoverFocusAllowed
            onHoveredChanged: {
                if (hovered && panelDock.activeFocus)
                    root.focusListingForce()
            }
        }
        anchors.top: root.panelOpen && root.panelTopSide ? panelDock.bottom
                                                          : commandField.bottom
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
        readonly property url sourceUrl: root.panelId.length &&
                                         typeof hostApi !== "undefined" && hostApi
                                         ? hostApi.panelSource(root.panelId)
                                         : ""
        property var panelItems: ({})
        readonly property int span: appConfig
                                    ? Math.min(appConfig.panelSize,
                                               (root.panelHorizontal
                                                ? root.height
                                                : root.width) * 0.7)
                                    : 260
        visible: root.panelOpen
        z: 2
        onActiveFocusChanged: if (root.keys)
                                  root.keys.panelFocused = activeFocus

        HoverHandler {
            enabled: root.hoverFocusAllowed
            onHoveredChanged: {
                if (hovered && !panelDock.activeFocus)
                    root.focusPanel()
            }
        }
        // Plain positional bindings: conditional anchors that flip to
        // `undefined` at runtime do not reliably un-anchor, which left the
        // dock stretched across stale edges after a grip re-dock.
        readonly property real areaTop: commandField.y + commandField.height
        readonly property real areaBottom: statusLine.y
        x: root.panelRight ? root.width - span : 0
        y: root.panelBottom ? areaBottom - span : areaTop
        width: root.panelHorizontal ? root.width : span
        height: root.panelHorizontal ? span : areaBottom - areaTop

        Rectangle {
            anchors.fill: parent
            color: Theme.opaqueBackground
        }

        // One keep-alive Loader per opened panel app: hidden panels stay
        // running (the shell survives; the workbench keeps its table).
        Repeater {
            model: root.openedPanels

            Loader {
                required property string modelData
                anchors.fill: parent
                anchors.topMargin: root.panelBottom ? 6 : 0
                anchors.bottomMargin: root.panelTopSide ? 6 : 0
                anchors.leftMargin: root.panelRight ? 6 : 0
                anchors.rightMargin: root.panelLeft ? 6 : 0
                source: typeof hostApi !== "undefined" && hostApi
                        ? hostApi.panelSource(modelData) : ""
                visible: modelData === root.panelId
                focus: visible
                onLoaded: {
                    if (!item)
                        return
                    if (item.host !== undefined)
                        item.host = typeof hostApi !== "undefined" ? hostApi
                                                                   : null
                    if (item.fileModel !== undefined)
                        item.fileModel = root.files
                    if (item.navStack !== undefined)
                        item.navStack = root.history
                    panelDock.panelItems[modelData] = item
                }
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
        Rectangle {
            visible: root.panelTopSide
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: panelDock.activeFocus ? 2 : 1
            color: panelDock.activeFocus ? Theme.accent : Theme.normalBorder
        }

        MouseArea {
            id: panelResizeH
            visible: root.panelHorizontal
            z: 5
            cursorShape: Qt.SplitVCursor
            x: 0
            width: parent.width
            y: root.panelBottom ? 0 : parent.height - 6
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
                var delta = root.panelBottom ? (startY - y) : (y - startY)
                appConfig.panelSize = Math.round(startSize + delta)
            }
        }
        Item {
            id: panelGrip
            z: 7
            width: root.panelHorizontal ? 72 : 16
            height: root.panelHorizontal ? 16 : 72
            // Inset past the 6px resize strip so the two gestures never
            // overlap: the edge resizes, the pill moves.
            x: root.panelHorizontal ? (parent.width - width) / 2
                                    : (root.panelRight ? 7
                                                       : parent.width - width - 7)
            y: root.panelHorizontal ? (root.panelBottom ? 7
                                                        : parent.height - height - 7)
                                    : (parent.height - height) / 2

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: gripArea.pressed || gripArea.containsMouse
                       ? Theme.hoverFill : "transparent"
                border.color: gripArea.pressed || gripArea.containsMouse
                              ? Theme.accent : Theme.normalBorder
                border.width: 1
                opacity: 0.9
            }

            Text {
                anchors.centerIn: parent
                text: root.panelGlyphFor(root.panelId)
                color: gripArea.pressed || gripArea.containsMouse
                       ? Theme.accent : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody - 1
            }

            MouseArea {
                id: gripArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                onPressed: function (mouse) {
                    var g = mapToItem(null, mouse.x, mouse.y)
                    root.panelDragX = g.x
                    root.panelDragY = g.y
                    root.panelDragTargetId = root.panelId
                    root.panelDragLabel = root.panelNameFor(root.panelId)
                    root.panelDragging = true
                }
                onPositionChanged: function (mouse) {
                    if (!pressed)
                        return
                    var g = mapToItem(null, mouse.x, mouse.y)
                    root.panelDragX = g.x
                    root.panelDragY = g.y
                }
                onReleased: root.applyPanelDrop()
                onCanceled: root.panelDragging = false
            }
        }

        MouseArea {
            id: panelResizeV
            visible: !root.panelHorizontal
            z: 5
            cursorShape: Qt.SplitHCursor
            y: 0
            height: parent.height
            x: root.panelRight ? 0 : parent.width - 6
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

    // Parked pills: one per available panel app, side by side on the
    // panel's edge. Click opens; drag summons to a chosen edge.
    Repeater {
        model: root.parkedPanels

        Item {
            id: parkedPill
            required property var modelData
            required property int index
            readonly property int slot: index
            readonly property int count: root.parkedPanels.length
            visible: root.keys && !root.keys.chooserMode
            z: 4
            width: root.panelHorizontal ? 72 : 16
            height: root.panelHorizontal ? 16 : 72
            x: root.panelHorizontal
               ? (root.width - width) / 2 +
                 (slot - (count - 1) / 2) * (width + 10)
               : (root.panelRight ? root.width - width - 2 : 2)
            y: root.panelHorizontal
               ? (root.panelBottom ? statusLine.y - height - 2
                                   : commandField.y + commandField.height + 2)
               : (commandField.y + commandField.height + statusLine.y) / 2
                 - height / 2 + (slot - (count - 1) / 2) * (height + 10)

            Rectangle {
                anchors.fill: parent
                radius: height > width ? width / 2 : height / 2
                color: pillArea.pressed || pillArea.containsMouse
                       ? Theme.hoverFill : "transparent"
                border.color: pillArea.pressed || pillArea.containsMouse
                              ? Theme.accent : Theme.normalBorder
                border.width: 1
                opacity: 0.9
            }

            Text {
                anchors.centerIn: parent
                text: root.panelGlyphFor(parkedPill.modelData.id)
                color: pillArea.pressed || pillArea.containsMouse
                       ? Theme.accent : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody - 1
            }

            // name chip on hover
            Rectangle {
                visible: pillArea.containsMouse && !root.panelDragging
                x: root.panelHorizontal
                   ? (parkedPill.width - width) / 2
                   : (root.panelRight ? parkedPill.width + 6 : -width - 6)
                y: root.panelHorizontal
                   ? (root.panelBottom ? -height - 6 : parkedPill.height + 6)
                   : (parkedPill.height - height) / 2
                width: pillName.implicitWidth + Theme.space(12)
                height: pillName.implicitHeight + Theme.space(6)
                radius: Theme.radius
                color: Theme.background
                border.color: Theme.normalBorder
                border.width: 1

                Text {
                    id: pillName
                    anchors.centerIn: parent
                    text: parkedPill.modelData.name
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }
            }

            MouseArea {
                id: pillArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                property real pressX: 0
                property real pressY: 0
                onPressed: function (mouse) {
                    var g = mapToItem(null, mouse.x, mouse.y)
                    pressX = g.x
                    pressY = g.y
                    root.panelDragX = g.x
                    root.panelDragY = g.y
                    root.panelDragTargetId = parkedPill.modelData.id
                    root.panelDragLabel = parkedPill.modelData.name
                }
                onPositionChanged: function (mouse) {
                    if (!pressed)
                        return
                    var g = mapToItem(null, mouse.x, mouse.y)
                    root.panelDragX = g.x
                    root.panelDragY = g.y
                    if (!root.panelDragging &&
                            Math.hypot(g.x - pressX, g.y - pressY) > 8)
                        root.panelDragging = true
                }
                onReleased: {
                    if (root.panelDragging) {
                        root.applyPanelDrop()
                    } else if (root.keys) {
                        root.keys.panelId = parkedPill.modelData.id
                        Qt.callLater(root.focusPanel)
                    }
                    root.panelDragTargetId = ""
                }
                onCanceled: {
                    root.panelDragging = false
                    root.panelDragTargetId = ""
                }
            }
        }
    }

    Item {
        id: panelDropZones
        visible: root.panelDragging
        z: 150
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: commandField.bottom
        anchors.bottom: statusLine.top

        readonly property string zone: {
            if (!root.panelDragging)
                return ""
            var p = mapFromItem(null, root.panelDragX, root.panelDragY)
            if (p.x < 0 || p.y < 0 || p.x > width || p.y > height)
                return "close"
            if (p.y < height * 0.30)
                return "top"
            if (p.y > height * 0.70)
                return "bottom"
            if (p.x < width * 0.30)
                return "left"
            if (p.x > width * 0.70)
                return "right"
            return "close"
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.background
            opacity: 0.55
        }

        component DropZone: Rectangle {
            required property string side
            readonly property bool hot: panelDropZones.zone === side
            color: hot ? Theme.selectedFill : "transparent"
            border.color: hot ? Theme.accent : Theme.normalBorder
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: parent.side
                color: parent.hot ? Theme.accent : Theme.muted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
            }
        }

        DropZone {
            side: "top"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height * 0.30
        }
        DropZone {
            side: "bottom"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.30
        }
        DropZone {
            side: "left"
            anchors.left: parent.left
            y: parent.height * 0.30
            height: parent.height * 0.40
            width: parent.width * 0.30
        }
        DropZone {
            side: "right"
            anchors.right: parent.right
            y: parent.height * 0.30
            height: parent.height * 0.40
            width: parent.width * 0.30
        }

        Text {
            anchors.centerIn: parent
            text: panelDropZones.zone === "close"
                  ? (root.panelOpen ? "release to close" : "release to cancel")
                  : "drop on an edge to dock"
            color: panelDropZones.zone === "close" ? Theme.urgent : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }

    Rectangle {
        visible: root.panelDragging
        z: 151
        x: root.panelDragX + 14
        y: root.panelDragY + 12
        width: ghostLabel.implicitWidth + Theme.space(16)
        height: ghostLabel.implicitHeight + Theme.space(8)
        radius: Theme.radius
        color: Theme.background
        border.color: Theme.accent
        border.width: 1
        opacity: 0.92

        Text {
            id: ghostLabel
            anchors.centerIn: parent
            text: root.panelDragLabel.length ? root.panelDragLabel : "panel"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }

    // Focus follows the pointer between browser and terminal, Hyprland
    // style — but never steals from the command field or overlays.
    readonly property bool hoverFocusAllowed: panelOpen && keys &&
                                              !panelDragging &&
                                              !keys.fieldFocused &&
                                              !keys.peekOpen &&
                                              !keys.actionOpen &&
                                              !keys.helpOpen



    Connections {
        target: root.keys
        function onPanelFocusRequested() { Qt.callLater(root.focusPanel) }
        function onPanelChanged() {
            var id = root.keys.panelId
            if (id.length && root.openedPanels.indexOf(id) < 0)
                root.openedPanels = root.openedPanels.concat([id])
        }
    }

    Connections {
        target: root.files
        function onCurrentStatChanged() { root.refreshRelevantPanels() }
        function onPathChanged() { root.refreshRelevantPanels() }
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

        // Split sections into two balanced columns by row weight.
        function helpColumns() {
            var secs = root.keys ? root.keys.helpModel : []
            var total = 0
            var i
            for (i = 0; i < secs.length; ++i)
                total += secs[i].rows.length + 2
            var acc = 0
            var split = secs.length
            for (i = 0; i < secs.length; ++i) {
                acc += secs[i].rows.length + 2
                if (acc >= total / 2) {
                    split = i + 1
                    break
                }
            }
            return [secs.slice(0, split), secs.slice(split)]
        }

        Rectangle {
            id: helpPanel
            readonly property int keyColW: Math.round(Theme.fontBody * 12.5)
            readonly property int colW: keyColW + Math.round(Theme.fontBody * 19)
            readonly property bool twoCol: helpOverlay.width >=
                                           colW * 2 + Theme.space(96)
            anchors.centerIn: parent
            width: (twoCol ? colW * 2 + Theme.space(32) : colW) +
                   Theme.space(40)
            height: Math.min(parent.height - Theme.space(40),
                             helpFlick.contentHeight + Theme.space(36))
            color: Theme.background
            border.color: Theme.normalBorder
            border.width: 1

            Flickable {
                id: helpFlick
                anchors.fill: parent
                anchors.margins: Theme.space(18)
                contentHeight: helpBody.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: helpBody
                    spacing: Theme.space(32)

                    Repeater {
                        model: helpPanel.twoCol ? 2 : 1

                        Column {
                            id: helpCol
                            required property int index
                            spacing: Theme.space(14)

                            Repeater {
                                model: helpPanel.twoCol
                                       ? helpOverlay.helpColumns()[helpCol.index]
                                       : (root.keys ? root.keys.helpModel : [])

                                Column {
                                    id: helpSection
                                    required property var modelData
                                    spacing: Theme.space(3)

                                    Text {
                                        text: helpSection.modelData.title
                                        color: Theme.accent
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        font.bold: true
                                    }

                                    Repeater {
                                        model: helpSection.modelData.rows

                                        Item {
                                            id: helpRowItem
                                            required property var modelData
                                            width: helpPanel.colW
                                            height: Theme.fontBody +
                                                    Theme.space(5)

                                            Text {
                                                width: helpPanel.keyColW
                                                anchors.verticalCenter:
                                                    parent.verticalCenter
                                                text: helpRowItem.modelData.keys
                                                color: Theme.foreground
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontBody
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                x: helpPanel.keyColW +
                                                   Theme.space(10)
                                                width: parent.width - x
                                                anchors.verticalCenter:
                                                    parent.verticalCenter
                                                text: helpRowItem.modelData.what
                                                color: Theme.muted
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontBody
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
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

    Component.onCompleted: {
        // A panel restored from config was set before QML loaded, so the
        // panelChanged connection never saw it — seed the keep-alive list.
        if (root.panelId.length)
            root.openedPanels = [root.panelId]
        Qt.callLater(root.focusListing)
        Qt.callLater(root.refreshRelevantPanels)
    }
}
