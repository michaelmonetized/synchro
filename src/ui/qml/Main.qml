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
    readonly property var config: typeof appConfig !== "undefined" ? appConfig : null
    readonly property var finder: typeof agentSearch !== "undefined" ? agentSearch : null
    readonly property var semanticFinder: typeof semanticSearch !== "undefined"
                                          ? semanticSearch : null
    readonly property string startupRevealPath: startupSelectPath
    readonly property bool startupReveal: startupRevealPath.length > 0
    readonly property bool gridMode: root.keys ? root.keys.gridMode : false
    readonly property bool fsnMode: root.keys ? root.keys.fsnMode : false
    readonly property int themeEpoch: Theme.epoch
    // The listing's vertical edge belongs to its scrollbar. Parked panel
    // grips stay close without sitting on top of its pointer target.
    readonly property int browserScrollClearance: Theme.space(18)

    onThemeEpochChanged: {
        if (typeof hostApi !== "undefined" && hostApi)
            hostApi.refreshThemedPreviews()
    }

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: root.files && root.files.isSql
           ? (root.files.isSemantic ? "SEE " : "SQL ") +
             root.files.sqlLabel + " — " + root.files.sqlContext +
             " — Synchro"
           : (root.files && root.files.path.length
              ? root.files.path + " — Synchro" : "Synchro")
    color: "transparent"

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
        lookAvailable: true
        lookHasRoom: root.lookStandaloneHasRoom || root.lookInPanel
        lookHasSelection: root.selectionCount > 0
        lookOpen: root.lookEnabled
        onLookToggleRequested: root.setLookOpen(!root.lookEnabled, true)
        agentAvailable: typeof agentSearch !== "undefined" && !!agentSearch
        onAgentRequested: root.openAgentSearch()
        onSettingsRequested: indexerSettings.open()
    }

    Rectangle {
        id: browserCanvas
        anchors.top: commandField.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        color: "transparent"
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.canvasGlassTop }
            GradientStop { position: 0.42; color: Theme.canvasGlassMiddle }
            GradientStop { position: 1.0; color: Theme.canvasGlassBottom }
        }
        z: 0

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.canvasGlassEdge
        }
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
    readonly property int selectionCount: root.selection
                                          ? root.selection.selectedCount : 0
    property bool lookSessionOpen: root.config
                                   ? root.config.panelLookOpen : true
    readonly property bool lookEnabled: root.lookSessionOpen
    property string lookSideOverride: ""
    readonly property string explicitLookSide: root.config
                                                ? root.config.lookSide
                                                : root.lookSideOverride
    readonly property bool lookSideExplicit: explicitLookSide.length > 0
    readonly property string lookDockSide: lookSideExplicit
                                            ? explicitLookSide : "right"
    readonly property bool lookDockLeft: lookDockSide === "left"
    readonly property bool lookDockRight: lookDockSide === "right"
    readonly property bool lookDockTop: lookDockSide === "top"
    readonly property bool lookDockBottom: lookDockSide === "bottom"
    readonly property bool lookDockVertical: lookDockLeft || lookDockRight
    // Action Deck is a true overlay: keep the browser composition and its
    // loaded Look companion intact underneath it.
    // A local quick-filter owns the browser canvas. The proxy cursor advances
    // through candidates as text changes, but that must not make LOOK resemble
    // a recursive search inside whichever candidate happens to sort first.
    readonly property bool lookWanted: lookEnabled && selectionCount > 0 &&
                                       root.keys && !root.keys.peekOpen &&
                                       !root.keys.fieldFocused
    readonly property bool lookInPanel: lookWanted && root.panelOpen &&
                                       panelDock.lookSupported &&
                                       panelDock.lookHasRoom &&
                                       (!lookSideExplicit ||
                                        lookDockSide === panelSide)
    readonly property real browserAreaLeft: root.panelOpen && root.panelLeft
                                             ? panelDock.x + panelDock.width : 0
    readonly property real browserAreaRight: root.panelOpen && root.panelRight
                                              ? panelDock.x : root.width
    readonly property real browserAreaTop: root.panelOpen && root.panelTopSide
                                            ? panelDock.y + panelDock.height
                                            : commandField.y + commandField.height
    readonly property real browserAreaBottom: root.panelOpen && root.panelBottom
                                               ? panelDock.y : statusLine.y
    readonly property real browserAreaWidth: Math.max(
                                                  0, browserAreaRight -
                                                     browserAreaLeft)
    readonly property real browserAreaHeight: Math.max(
                                                   0, browserAreaBottom -
                                                      browserAreaTop)
    readonly property int lookTargetSize: root.config
                                          ? Math.min(root.config.lookSize,
                                                     Math.round(
                                                         (lookDockVertical
                                                          ? browserAreaWidth
                                                          : browserAreaHeight) *
                                                         0.46))
                                          : Math.round((lookDockVertical
                                                        ? browserAreaWidth
                                                        : browserAreaHeight) *
                                                       0.46)
    readonly property int lookMinimumSpan: lookDockVertical
                                            ? Theme.space(240)
                                            : Theme.space(145)
    readonly property bool lookStandaloneHasRoom:
        root.lookTargetSize >= root.lookMinimumSpan &&
        (root.lookDockVertical
         ? root.browserAreaWidth - root.lookTargetSize >= Theme.space(300) &&
           root.browserAreaHeight >= Theme.space(260)
         : root.browserAreaHeight - root.lookTargetSize >= Theme.space(180) &&
           root.browserAreaWidth >= Theme.space(300))
    readonly property bool lookStandalone: lookWanted && !lookInPanel &&
                                           lookStandaloneHasRoom

    // Grip-drag re-docking state (the gesture only starts from the grip,
    // so panel content keeps its own drag and drop untouched).
    property bool panelDragging: false
    property real panelDragX: 0
    property real panelDragY: 0
    property string panelDragTargetId: ""
    property string panelDragLabel: ""
    readonly property string lookDragTarget: "__synchro_look__"
    readonly property bool draggingLook: panelDragTargetId === lookDragTarget

    // Panel apps: which are relevant to the current selection (parked
    // pills) and which have been opened this session (kept alive).
    property var relevantPanels: []
    property var openedPanels: []
    property var pendingSqlBookmark: null
    property bool pendingSqlScan: false
    // Published by the keep-alive SQL panel. This is deliberately distinct
    // from HostApi.inlineFolderLoading: a workbench query updates the browser
    // and its Miller companion, while a preview-only drill marks Miller alone.
    property bool mainSqlBusy: false
    readonly property bool browserQueryBusy: root.mainSqlBusy ||
                                             !!(root.semanticFinder &&
                                                root.semanticFinder.running)

    function setLookOpen(open, persist) {
        root.lookSessionOpen = !!open
        if (persist && root.config)
            root.config.panelLookOpen = root.lookSessionOpen
    }

    function setLookDockSide(side) {
        root.lookSideOverride = side
        if (root.config)
            root.config.lookSide = side
    }

    function openAgentSearch() {
        var scope = root.files && root.files.isSql
                    ? root.files.sqlContext
                    : (root.files && root.files.path ? root.files.path : "")
        agentOverlay.openFor(scope)
    }

    function startSemanticSearch(query) {
        if (!root.semanticFinder || !root.keys)
            return
        var scope = root.files && root.files.isSql
                    ? root.files.sqlContext
                    : (root.files && root.files.path ? root.files.path : "")
        if (root.semanticFinder.start(query, scope, 60))
            root.keys.setStatusMessage("Looking through indexed images…")
        else if (root.semanticFinder.error.length)
            root.keys.setStatusMessage(root.semanticFinder.error)
    }

    function toggleLookFromBrowser() {
        if (root.selectionCount <= 0) {
            if (root.keys)
                root.keys.setStatusMessage("Select an item to preview")
            return
        }
        if (root.lookInPanel || root.lookStandalone) {
            root.setLookOpen(false, false)
            return
        }
        var canRender = root.panelOpen
                        ? (panelDock.lookSupported && panelDock.lookHasRoom)
                        : root.lookStandaloneHasRoom
        if (canRender) {
            root.setLookOpen(true, false)
            return
        }
        if (typeof hostApi !== "undefined" && hostApi)
            hostApi.toggle()
    }

    function refreshRelevantPanels() {
        if (typeof hostApi === "undefined" || !hostApi)
            return
        var list = hostApi.relevantPanels()
        for (var i = 0; i < list.length; ++i)
            root.panelMeta[list[i].id] = { name: list[i].name,
                                           glyph: list[i].glyph }
        root.relevantPanels = list
        // Match-scoped apps cease to be a valid surface when their target
        // selection disappears. "always" panels (the terminal) remain in
        // the list, so they are never closed by this rule.
        if (root.keys && root.keys.panelId.length) {
            var activeStillRelevant = false
            for (var j = 0; j < list.length; ++j) {
                if (list[j].id === root.keys.panelId) {
                    activeStillRelevant = true
                    break
                }
            }
            if (!activeStillRelevant &&
                    hostApi.panelRelevance(root.keys.panelId) === "match")
                root.keys.panelId = ""
        }
    }

    // Remember name/glyph for keep-alive panel instances and drag labels.
    property var panelMeta: ({})
    readonly property var activePanelPeers: {
        if (!root.panelId.length || typeof hostApi === "undefined" || !hostApi)
            return []
        return hostApi.panelPeers(root.panelId)
    }

    function activePeer(id) {
        for (var i = 0; i < root.activePanelPeers.length; ++i) {
            if (root.activePanelPeers[i].id === id)
                return true
        }
        return false
    }

    function panelNameFor(id) {
        var m = root.panelMeta[id]
        if ((!m || !m.name) && typeof hostApi !== "undefined" && hostApi) {
            var info = hostApi.panelInfo(id)
            if (info && info.name) {
                root.panelMeta[id] = info
                m = info
            }
        }
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

    // Parked pills represent availability now, not panel history. Opened
    // contextual apps stay alive internally but do not leave stale launchers.
    readonly property var parkedPanels: {
        var out = []
        var seen = {}
        var cur = root.panelOpen ? root.panelId : ""
        var i
        for (i = 0; i < root.relevantPanels.length; ++i) {
            var rp = root.relevantPanels[i]
            if (rp.id !== cur && !root.activePeer(rp.id) && !seen[rp.id]) {
                seen[rp.id] = true
                out.push({ id: rp.id, name: rp.name })
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

    function deliverPendingSqlAction() {
        var panel = panelDock.panelItems["synchro.panel.sql"]
        if (!panel)
            return
        if (root.pendingSqlBookmark && panel.openBookmark) {
            var saved = root.pendingSqlBookmark
            root.pendingSqlBookmark = null
            panel.openBookmark(saved.name, saved.sql, saved.cwd)
        }
        if (root.pendingSqlScan && panel.forceTreeScan) {
            root.pendingSqlScan = false
            panel.forceTreeScan()
        }
    }

    function openSqlBookmark(name, sql, cwd, bookmarkId) {
        if (!root.keys)
            return
        root.pendingSqlBookmark = { name: name, sql: sql, cwd: cwd,
                                    id: bookmarkId }
        root.keys.panelId = "synchro.panel.sql"
        Qt.callLater(root.deliverPendingSqlAction)
        Qt.callLater(root.focusPanel)
    }

    // Shared drop rule for both grip gestures: an edge zone docks (and
    // opens) the panel there; center/outside closes or cancels.
    function applyPanelDrop() {
        var z = panelDropZones.zone
        var target = root.panelDragTargetId
        root.panelDragging = false
        root.panelDragTargetId = ""
        if (!target.length)
            return
        if (target === root.lookDragTarget) {
            // Center/outside is a harmless cancel for Look. It is a persistent
            // companion, not a panel app that should disappear by accident.
            if (z !== "close" && z.length) {
                root.setLookDockSide(z)
                root.setLookOpen(true, true)
            }
            return
        }
        if (!root.keys)
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
        } else if (root.panelId !== "synchro.panel.terminal") {
            root.keys.panelId = "synchro.panel.terminal"
            Qt.callLater(root.focusPanel)
        } else {
            root.focusPanel()
        }
    }

    // Standalone Look reserves browser space only while an explicit selection
    // exists. It can occupy any edge of the browser area left by an app panel.
    Item {
        id: standaloneLookDock
        objectName: "standaloneLookDock"
        property real span: root.lookStandalone ? root.lookTargetSize : 0
        x: root.lookDockRight ? root.browserAreaRight - width
                              : root.browserAreaLeft
        y: root.lookDockBottom ? root.browserAreaBottom - height
                               : root.browserAreaTop
        width: root.lookDockVertical ? span : root.browserAreaWidth
        height: root.lookDockVertical ? root.browserAreaHeight : span
        z: 2
        clip: true

        Behavior on span {
            NumberAnimation {
                duration: 140
                easing.type: Easing.OutCubic
            }
        }
    }

    Loader {
        id: listingLoader
        objectName: "listingLoader"

        HoverHandler {
            enabled: root.hoverFocusAllowed
            onHoveredChanged: {
                if (hovered && panelDock.activeFocus)
                    root.focusListingForce()
            }
        }
        anchors.top: standaloneLookDock.span > 0.5 && root.lookDockTop
                     ? standaloneLookDock.bottom
                     : (root.panelOpen && root.panelTopSide
                        ? panelDock.bottom : commandField.bottom)
        anchors.left: standaloneLookDock.span > 0.5 && root.lookDockLeft
                      ? standaloneLookDock.right
                      : (root.panelOpen && root.panelLeft
                         ? panelDock.right : parent.left)
        anchors.right: standaloneLookDock.span > 0.5 && root.lookDockRight
                       ? standaloneLookDock.left
                       : (root.panelOpen && root.panelRight
                          ? panelDock.left : parent.right)
        anchors.bottom: standaloneLookDock.span > 0.5 && root.lookDockBottom
                        ? standaloneLookDock.top
                        : (root.panelOpen && root.panelBottom
                           ? panelDock.top : statusLine.top)
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

    QueryBusy {
        objectName: "mainQueryBusy"
        anchors.fill: listingLoader
        running: root.browserQueryBusy
        label: root.semanticFinder && root.semanticFinder.running
               ? "Finding images by visual meaning…"
               : "Updating browser results…"
        z: 3
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
            centerInitialSelection: root.startupReveal
            initialSelectionPath: root.startupRevealPath
            onViewToggleRequested: if (root.keys) root.keys.gridMode = true
            onDoRequested: function(sceneX, sceneY) {
                if (typeof hostApi !== "undefined" && hostApi)
                    hostApi.openDoContext(sceneX, sceneY)
            }
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
            catalog: typeof fileCatalog !== "undefined" ? fileCatalog : null
            host: typeof hostApi !== "undefined" ? hostApi : null
            fileOps: typeof fileOpEngine !== "undefined" ? fileOpEngine : null
            onViewToggleRequested: if (root.keys)
                                       root.keys.fsnTreeView = !root.keys.fsnTreeView
            onDoRequested: function(sceneX, sceneY) {
                if (typeof hostApi !== "undefined" && hostApi)
                    hostApi.openDoContext(sceneX, sceneY)
            }
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
            config: typeof appConfig !== "undefined" ? appConfig : null
            centerInitialSelection: root.startupReveal
            initialSelectionPath: root.startupRevealPath
            onViewToggleRequested: if (root.keys) root.keys.gridMode = false
            onDoRequested: function(sceneX, sceneY) {
                if (typeof hostApi !== "undefined" && hostApi)
                    hostApi.openDoContext(sceneX, sceneY)
            }
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
        catalog: typeof fileCatalog !== "undefined" ? fileCatalog : null
    }

    Confirm {
        anchors.fill: parent
        z: 105
        keyMachine: root.keys
    }

    AgentSearchOverlay {
        id: agentOverlay
        anchors.fill: parent
        z: 130
        bridge: root.finder
        onDismissed: Qt.callLater(root.focusListingForce)
    }

    IndexerSettingsOverlay {
        id: indexerSettings
        anchors.fill: parent
        z: 132
        config: root.config
        onDismissed: Qt.callLater(root.focusListingForce)
    }

    FocusScope {
        id: panelDock
        objectName: "panelDock"
        readonly property url sourceUrl: root.panelId.length &&
                                         typeof hostApi !== "undefined" && hostApi
                                         ? hostApi.panelSource(root.panelId)
                                         : ""
        property var panelItems: ({})
        readonly property int headerH: Theme.controlHeight + Theme.spaceSM
        readonly property bool lookSupported:
            root.panelId.length && typeof hostApi !== "undefined" && hostApi
            ? hostApi.panelSupportsCompanion(root.panelId, "preview") : false
        readonly property int contentLeft: root.panelRight ? 6 : 0
        readonly property int contentRight: width - (root.panelLeft ? 6 : 0)
        readonly property int contentTop: headerH + (root.panelBottom ? 6 : 0)
        readonly property int contentBottom: height - (root.panelTopSide ? 6 : 0)
        readonly property int contentWidth: Math.max(0, contentRight - contentLeft)
        readonly property int contentHeight: Math.max(0, contentBottom - contentTop)
        readonly property bool lookHasRoom: root.panelHorizontal
                                                   ? contentWidth >= 720 &&
                                                     contentHeight >= 145
                                                   : contentWidth >= 240 &&
                                                     contentHeight >= 360
        readonly property bool lookVisible: root.lookInPanel
        readonly property real lookRatio: root.config
                                          ? root.config.panelLookRatio : 0.34
        readonly property int lookSpan: lookVisible
                                        ? Math.round((root.panelHorizontal
                                                      ? contentWidth
                                                      : contentHeight) *
                                                     lookRatio) : 0
        readonly property int span: root.config
                                    ? Math.min(root.config.panelSize,
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
            color: Theme.darkerBackground
        }

        Rectangle {
            id: panelHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: panelDock.headerH
            color: Theme.darkBackground

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spaceLG
                anchors.right: panelHeaderActions.left
                anchors.rightMargin: Theme.spaceLG
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spaceLG

                Repeater {
                    model: root.activePanelPeers

                    delegate: Rectangle {
                        id: modeTab
                        required property var modelData
                        readonly property bool current: modelData.id === root.panelId
                        visible: panelDock.width >= Theme.space(420) || current
                        width: visible
                               ? modeLabel.implicitWidth +
                                 Theme.controlPaddingX * 2 : 0
                        height: Theme.space(22)
                        color: current ? Theme.accent
                                       : (modeHover.hovered ? Theme.hoverFill
                                                            : "transparent")
                        border.color: current ? Theme.accent : Theme.normalBorder
                        border.width: 1
                        radius: Theme.radius

                        Text {
                            id: modeLabel
                            anchors.centerIn: parent
                            text: modeTab.modelData.glyph + "  " +
                                  modeTab.modelData.name
                            color: modeTab.current ? Theme.background
                                                   : Theme.darkForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            font.bold: modeTab.current
                        }

                        HoverHandler { id: modeHover }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!root.keys || modeTab.current)
                                    return
                                root.keys.panelId = modeTab.modelData.id
                                Qt.callLater(root.focusPanel)
                            }
                        }
                    }
                }

            }

            Row {
                id: panelHeaderActions
                anchors.right: parent.right
                anchors.rightMargin: Theme.spaceLG
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spaceSM

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: panelDock.activeFocus &&
                             panelDock.width >= Theme.space(420)
                    text: "KEYBOARD"
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: true
                }

                ChromeButton {
                    visible: panelDock.lookSupported
                    height: Theme.space(24)
                    compact: false
                    label: "LOOK"
                    checked: root.lookEnabled
                    toolTip: !panelDock.lookHasRoom
                             ? (checked
                                ? "Look is enabled · enlarge the panel to show it"
                                : "Enlarge the panel to show Look")
                             : (checked ? "Hide ambient preview"
                                        : "Show ambient preview")
                    onTriggered: root.setLookOpen(!root.lookEnabled, true)
                }

                ChromeButton {
                    height: Theme.space(24)
                    compact: true
                    iconName: "view-restore-symbolic"
                    fallbackGlyph: "↻"
                    toolTip: "Move panel to next edge"
                    onTriggered: {
                        if (!root.keys) return
                        var sides = ["bottom", "right", "top", "left"]
                        var i = sides.indexOf(root.panelSide)
                        root.keys.panelSide = sides[(i + 1) % sides.length]
                    }
                }

                ChromeButton {
                    height: Theme.space(24)
                    compact: true
                    iconName: "window-close-symbolic"
                    fallbackGlyph: "×"
                    toolTip: "Hide panel"
                    onTriggered: if (root.keys) root.keys.panelId = ""
                }
            }
        }

        // One keep-alive Loader per opened panel app: hidden panels stay
        // running (the shell survives; the workbench keeps its table).
        Repeater {
            model: root.openedPanels

            Loader {
                required property string modelData
                anchors.fill: parent
                anchors.topMargin: panelDock.headerH + (root.panelBottom ? 6 : 0)
                anchors.bottomMargin: (root.panelTopSide ? 6 : 0) +
                                      (panelDock.lookVisible &&
                                       !root.panelHorizontal
                                       ? panelDock.lookSpan + 6 : 0)
                anchors.leftMargin: root.panelRight ? 6 : 0
                anchors.rightMargin: (root.panelLeft ? 6 : 0) +
                                     (panelDock.lookVisible &&
                                      root.panelHorizontal
                                      ? panelDock.lookSpan + 6 : 0)
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
                    if (item.selectionModel !== undefined)
                        item.selectionModel = root.selection
                    if (item.navStack !== undefined)
                        item.navStack = root.history
                    if (item.catalog !== undefined)
                        item.catalog = typeof fileCatalog !== "undefined"
                                     ? fileCatalog : null
                    if (item.config !== undefined)
                        item.config = root.config
                    if (item.shell !== undefined)
                        item.shell = root
                    panelDock.panelItems[modelData] = item
                    Qt.callLater(root.deliverPendingSqlAction)
                }
            }
        }

        MouseArea {
            id: lookResize
            visible: panelDock.lookVisible
            z: 4
            x: root.panelHorizontal
               ? panelDock.contentRight - panelDock.lookSpan - 3
               : panelDock.contentLeft
            y: root.panelHorizontal
               ? panelDock.contentTop
               : panelDock.contentBottom - panelDock.lookSpan - 3
            width: root.panelHorizontal ? 6 : panelDock.contentWidth
            height: root.panelHorizontal ? panelDock.contentHeight : 6
            cursorShape: root.panelHorizontal ? Qt.SplitHCursor
                                              : Qt.SplitVCursor
            preventStealing: true
            property real startRatio: 0
            property real startCoord: 0
            onPressed: function(mouse) {
                startRatio = root.config ? root.config.panelLookRatio : 0.34
                var p = mapToItem(null, mouse.x, mouse.y)
                startCoord = root.panelHorizontal ? p.x : p.y
            }
            onPositionChanged: function(mouse) {
                if (!pressed || !root.config)
                    return
                var p = mapToItem(null, mouse.x, mouse.y)
                var now = root.panelHorizontal ? p.x : p.y
                var total = root.panelHorizontal ? panelDock.contentWidth
                                                 : panelDock.contentHeight
                root.config.panelLookRatio = startRatio -
                                             (now - startCoord) / total
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
                startSize = root.config ? root.config.panelSize : 260
                startY = mapToItem(null, mouse.x, mouse.y).y
            }
            onPositionChanged: function (mouse) {
                if (!pressed || !root.config)
                    return
                var y = mapToItem(null, mouse.x, mouse.y).y
                var delta = root.panelBottom ? (startY - y) : (y - startY)
                root.config.panelSize = Math.round(startSize + delta)
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
                startSize = root.config ? root.config.panelSize : 300
                startX = mapToItem(null, mouse.x, mouse.y).x
            }
            onPositionChanged: function (mouse) {
                if (!pressed || !root.config)
                    return
                var x = mapToItem(null, mouse.x, mouse.y).x
                var delta = root.panelRight ? (startX - x) : (x - startX)
                root.config.panelSize = Math.round(startSize + delta)
            }
        }
    }

    // One preview companion, two homes. Keeping this as a single instance
    // preserves loaded rich handlers and folder state as it moves between the
    // browser and an app panel.
    PanelLook {
        id: panelLook
        parent: root.lookInPanel ? panelDock : root.contentItem
        visible: root.lookInPanel || standaloneLookDock.span > 0.5
        opacity: root.lookInPanel || root.lookStandalone ? 1 : 0
        z: 2
        x: root.lookInPanel
           ? (root.panelHorizontal
              ? panelDock.contentRight - panelDock.lookSpan
              : panelDock.contentLeft)
           : standaloneLookDock.x
        y: root.lookInPanel
           ? (root.panelHorizontal
              ? panelDock.contentTop
              : panelDock.contentBottom - panelDock.lookSpan)
           : standaloneLookDock.y
        width: root.lookInPanel
               ? (root.panelHorizontal ? panelDock.lookSpan
                                       : panelDock.contentWidth)
               : standaloneLookDock.width
        height: root.lookInPanel
                ? (root.panelHorizontal ? panelDock.contentHeight
                                        : panelDock.lookSpan)
                : standaloneLookDock.height
        host: typeof hostApi !== "undefined" ? hostApi : null
        fileModel: root.files
        filterProxy: root.listing
        selectionModel: root.selection
        horizontalSplit: root.lookInPanel ? root.panelHorizontal
                                           : !root.lookDockVertical
        mainQueryBusy: root.browserQueryBusy
        onCollapseRequested: root.setLookOpen(false, true)
        onDockDragStarted: function(sceneX, sceneY) {
            root.panelDragX = sceneX
            root.panelDragY = sceneY
            root.panelDragTargetId = root.lookDragTarget
            root.panelDragLabel = "LOOK"
            root.panelDragging = true
        }
        onDockDragMoved: function(sceneX, sceneY) {
            if (!root.panelDragging || !root.draggingLook)
                return
            root.panelDragX = sceneX
            root.panelDragY = sceneY
        }
        onDockDragFinished: function(sceneX, sceneY) {
            if (!root.panelDragging || !root.draggingLook)
                return
            root.panelDragX = sceneX
            root.panelDragY = sceneY
            root.applyPanelDrop()
        }
        onDockDragCanceled: {
            if (root.draggingLook) {
                root.panelDragging = false
                root.panelDragTargetId = ""
            }
        }

        Behavior on opacity {
            NumberAnimation { duration: 90 }
        }
    }

    MouseArea {
        id: standaloneLookResize
        visible: standaloneLookDock.span > 0.5 && !root.lookInPanel
        z: 5
        x: root.lookDockRight ? standaloneLookDock.x - 3
                              : (root.lookDockLeft
                                 ? standaloneLookDock.x +
                                   standaloneLookDock.width - 3
                                 : standaloneLookDock.x)
        y: root.lookDockBottom ? standaloneLookDock.y - 3
                               : (root.lookDockTop
                                  ? standaloneLookDock.y +
                                    standaloneLookDock.height - 3
                                  : standaloneLookDock.y)
        width: root.lookDockVertical ? 6 : standaloneLookDock.width
        height: root.lookDockVertical ? standaloneLookDock.height : 6
        cursorShape: root.lookDockVertical ? Qt.SplitHCursor
                                           : Qt.SplitVCursor
        preventStealing: true
        property real startSize: 0
        property real startCoord: 0
        onPressed: function(mouse) {
            startSize = root.config ? root.config.lookSize : 360
            var p = mapToItem(null, mouse.x, mouse.y)
            startCoord = root.lookDockVertical ? p.x : p.y
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root.config)
                return
            var p = mapToItem(null, mouse.x, mouse.y)
            var now = root.lookDockVertical ? p.x : p.y
            var delta = root.lookDockRight || root.lookDockBottom
                        ? startCoord - now : now - startCoord
            root.config.lookSize = Math.round(startSize + delta)
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
               : (root.panelRight
                  ? root.width - width - 2 - root.browserScrollClearance : 2)
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
        objectName: "panelDropZones"
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
                  ? (root.draggingLook
                     ? "release to keep current position"
                     : (root.panelOpen ? "release to close"
                                       : "release to cancel"))
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
                                              !agentOverlay.visible &&
                                              !indexerSettings.visible &&
                                              !keys.fieldFocused &&
                                              !keys.peekOpen &&
                                              !keys.actionOpen &&
                                              !keys.helpOpen



    Connections {
        target: root.keys
        function onLookToggleRequested() { root.toggleLookFromBrowser() }
        function onAgentSearchRequested() { root.openAgentSearch() }
        function onSemanticSearchRequested(query) {
            root.startSemanticSearch(query)
        }
        function onSettingsRequested() { indexerSettings.open() }
        function onPanelFocusRequested() { Qt.callLater(root.focusPanel) }
        function onSqlScanRequested() {
            root.pendingSqlScan = true
            Qt.callLater(root.deliverPendingSqlAction)
        }
        function onPanelChanged() {
            var id = root.keys.panelId
            if (id.length && root.openedPanels.indexOf(id) < 0)
                root.openedPanels = root.openedPanels.concat([id])
        }
    }

    Connections {
        target: root.chips
        function onSqlBookmarkActivated(name, sql, cwd, bookmarkId) {
            root.openSqlBookmark(name, sql, cwd, bookmarkId)
        }
    }

    Connections {
        target: root.files
        function onCurrentStatChanged() { root.refreshRelevantPanels() }
        function onPathChanged() { root.refreshRelevantPanels() }
    }

    Connections {
        target: root.selection
        function onSelectionChanged() { root.refreshRelevantPanels() }
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
        sequence: "Ctrl+,"
        enabled: root.config && !indexerSettings.visible &&
                 root.keys && !root.keys.peekOpen && !root.keys.actionOpen
        onActivated: indexerSettings.open()
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
        target: root.finder
        function onResultReady(label, sql, cwd) {
            root.openSqlBookmark(label, sql, cwd, "agent")
            agentOverlay.complete()
        }
    }

    Connections {
        target: root.semanticFinder
        function onResultReady(label, result) {
            if (root.files)
                root.files.showSqlResult(result, label)
            var count = Number(result.count || 0)
            var searched = Number(result.searched || 0)
            root.keys.setStatusMessage(count + " semantic matches · " +
                                       searched + " indexed images compared")
            Qt.callLater(root.focusListingForce)
        }
        function onChanged() {
            if (!root.semanticFinder.running && root.semanticFinder.error.length)
                root.keys.setStatusMessage(root.semanticFinder.error)
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
        function onGridMoveRequested(dx, dy) {
            if (root.gridMode && !root.fsnMode && listingLoader.item &&
                    listingLoader.item.navigateGeometry)
                listingLoader.item.navigateGeometry(dx, dy, false, true)
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
            color: Theme.alpha(Theme.darkerBackground, 0.88)
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
                             helpFlick.contentHeight + Theme.space(96))
            color: Theme.lighterBackground
            border.color: Theme.focusBorder
            border.width: 1

            Item {
                id: helpHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.space(18)
                anchors.rightMargin: Theme.space(12)
                height: Theme.space(48)

                Column {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceXXS

                    Text {
                        text: "Synchro controls"
                        color: Theme.brightForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                    }
                    Text {
                        text: "Keyboard-first, mouse-friendly"
                        color: Theme.darkForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                    }
                }

                ChromeButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true
                    iconName: "window-close-symbolic"
                    fallbackGlyph: "×"
                    toolTip: "Close help  ·  Esc"
                    onTriggered: if (root.keys) root.keys.escape()
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.normalBorder
                }
            }

            Flickable {
                id: helpFlick
                anchors.top: helpHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.space(18)
                anchors.rightMargin: Theme.space(18)
                anchors.topMargin: Theme.space(12)
                anchors.bottomMargin: Theme.space(18)
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
                                        font.pixelSize: Theme.fontSubtitle
                                        font.bold: true
                                    }

                                    Repeater {
                                        model: helpSection.modelData.rows

                                        Item {
                                            id: helpRowItem
                                            required property var modelData
                                            width: helpPanel.colW
                                            height: Math.max(Theme.controlHeight,
                                                             helpKey.implicitHeight)

                                            Keycap {
                                                id: helpKey
                                                width: helpPanel.keyColW
                                                anchors.verticalCenter:
                                                    parent.verticalCenter
                                                label: helpRowItem.modelData.keys
                                            }
                                            Text {
                                                x: helpPanel.keyColW +
                                                   Theme.space(10)
                                                width: parent.width - x
                                                anchors.verticalCenter:
                                                    parent.verticalCenter
                                                text: helpRowItem.modelData.what
                                                color: Theme.lightForeground
                                                opacity: 0.74
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
        if (root.keys)
            root.keys.lookKeyMode = true
        // A panel restored from config was set before QML loaded, so the
        // panelChanged connection never saw it — seed the keep-alive list.
        if (root.panelId.length)
            root.openedPanels = [root.panelId]
        Qt.callLater(root.focusListing)
        Qt.callLater(root.refreshRelevantPanels)
    }

    Component.onDestruction: if (root.keys)
                                 root.keys.lookKeyMode = false
}
