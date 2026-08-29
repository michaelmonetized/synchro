import QtQuick
import Synchro.Theme

Item {
    id: grid

    required property var fileModel
    property var filterProxy
    property var navStack
    property var keyMachine
    property var selection
    property var host: null
    property var fileOps: null
    property var config: null
    property bool centerInitialSelection: false
    property string initialSelectionPath: ""
    property bool initialSelectionRevealed: false
    property real filterRestoreContentY: 0
    property bool dndEnabled: true
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property bool dndLive: dndEnabled && fileOps && fileModel &&
                                    !fileModel.isTrash && !fileModel.isRecent &&
                                    !fileModel.isSearch && !fileModel.isVolumes &&
                                    !fileModel.isSql
    readonly property bool searching: fileModel && fileModel.isSearch
    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property bool showCursorChrome: !keyMachine || keyMachine.listFocused
    // Keys belong to a panel: keep the cursor visible (panel apps target
    // the selected file) but dimmed so focus stays legible.
    readonly property bool cursorDim: keyMachine && keyMachine.panelFocused
    readonly property int thumbSizePx: preferredInner >= 176 ? 512 : 256
    readonly property int preferredInner: config ? config.gridSize : Theme.space(132)
    readonly property int cellPad: Theme.spaceXXL * 2
    readonly property int preferredCell: preferredInner + cellPad
    readonly property real layoutWidth: Math.min(width, Theme.space(1600))
    readonly property int columns: Math.max(
                                       1, Math.floor(layoutWidth / Math.max(1, preferredCell)))
    readonly property int cellInner: Math.max(
                                         Theme.space(48),
                                         Math.round(cellWidth - cellPad))
    readonly property real cellWidth: layoutWidth > 0 ? layoutWidth / columns
                                                      : preferredCell
    readonly property real cellHeight: cellInner + Theme.space(58)
    readonly property var groupModel: visible && searching && fileModel
                                      ? fileModel.folderGroupModel : null
    readonly property int sectionH: Math.max(Theme.fontBody + Theme.space(10), 24)

    signal viewToggleRequested()
    signal doRequested(real sceneX, real sceneY)

    focus: true
    activeFocusOnTab: true
    clip: true

    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            if (!grid.config) return
            var delta = event.angleDelta.y !== 0 ? event.angleDelta.y
                                                 : event.pixelDelta.y
            if (delta === 0) return
            grid.config.setGridSize(grid.config.gridSize + (delta > 0 ? 12 : -12))
            event.accepted = true
        }
    }

    onColumnsChanged: if (visible && keyMachine)
        keyMachine.gridStride = columns
    onVisibleChanged: {
        if (visible && keyMachine)
            keyMachine.gridStride = columns
        if (visible)
            thumbSync.restart()
    }
    onSearchingChanged: if (visible && keyMachine)
        keyMachine.gridStride = columns

    function dragPathsFor(index, path) {
        if (grid.selection && grid.selection.isSelected(index)) {
            var picked = grid.selection.selectedPaths()
            if (picked && picked.length)
                return picked
        }
        return path ? [path] : []
    }

    function armDrag(item, index, path, name, thumbnail, isDir) {
        if (!grid.dndLive || !grid.fileOps)
            return
        var paths = grid.dragPathsFor(index, path)
        if (!paths.length)
            return
        item.Drag.mimeData = grid.fileOps.dragMime(paths)
        item.Drag.hotSpot.x = Math.round(dragGhost.width / 2)
        item.Drag.hotSpot.y = Math.round(dragGhost.height / 2)
        dragGhost.name = name
        dragGhost.thumbnail = thumbnail
        dragGhost.isDir = isDir
        dragGhost.count = paths.length
        Qt.callLater(function () {
            dragGhost.grabToImage(function (result) {
                if (!result || !result.url)
                    return
                item.Drag.imageSource = result.url
            }, Qt.size(dragGhost.width, dragGhost.height))
        })
    }

    // View-row lookup. GridView / clicks use filterProxy order (name sort);
    // DirectoryModel.rowMap is source order and must not paint the tiles.
    function rowAt(i) {
        if (grid.rows && grid.rows.rowMap)
            return grid.rows.rowMap(i)
        if (grid.fileModel && grid.fileModel.rowMap)
            return grid.fileModel.rowMap(i)
        return ({})
    }

    function groupsNow() {
        if (grid.fileModel && grid.fileModel.folderGroups)
            return grid.fileModel.folderGroups
        return []
    }

    function searchList() {
        return bodyLoader.item && bodyLoader.item.view ? bodyLoader.item.view : null
    }

    function tilesView() {
        return bodyLoader.item && bodyLoader.item.objectName === "fileGridTiles"
               ? bodyLoader.item : null
    }

    function activeScrollView() {
        return grid.searching ? grid.searchList() : grid.tilesView()
    }

    // GridView, grouped search results, and future grid surfaces do not share
    // a reliable row/column stride while the browser is being resized. Walk
    // the live delegates and navigate their actual painted centers instead.
    function renderedCells() {
        var found = []
        var seen = ({})
        function visit(item) {
            if (!item)
                return
            if (item.objectName === "gridCell" && item.visible &&
                    item.width > 0 && item.height > 0) {
                var row = Number(item.rowIndex)
                if (isFinite(row) && row >= 0 && !seen[row]) {
                    var center = item.mapToItem(grid, item.width / 2,
                                                item.height / 2)
                    found.push({ row: row, x: center.x, y: center.y,
                                 width: item.width, height: item.height })
                    seen[row] = true
                }
            }
            var children = item.children
            if (!children)
                return
            for (var i = 0; i < children.length; ++i)
                visit(children[i])
        }
        visit(bodyLoader.item)
        return found
    }

    function geometryTarget(row, dx, dy, steps) {
        var cells = grid.renderedCells()
        var current = null
        for (var i = 0; i < cells.length; ++i) {
            if (cells[i].row === row) {
                current = cells[i]
                break
            }
        }
        if (!current)
            return -1
        steps = Math.max(1, Number(steps) || 1)
        var epsilon = 1

        if (dx !== 0) {
            var sameRow = []
            var rowTolerance = Math.max(3, current.height * 0.30)
            for (i = 0; i < cells.length; ++i) {
                var h = cells[i]
                if (h.row === current.row ||
                        Math.abs(h.y - current.y) > rowTolerance)
                    continue
                if ((dx > 0 && h.x > current.x + epsilon) ||
                        (dx < 0 && h.x < current.x - epsilon))
                    sameRow.push(h)
            }
            sameRow.sort(function(a, b) {
                return dx > 0 ? a.x - b.x : b.x - a.x
            })
            if (!sameRow.length)
                return -1
            return sameRow[Math.min(steps, sameRow.length) - 1].row
        }

        if (dy !== 0) {
            var directional = []
            for (i = 0; i < cells.length; ++i) {
                var v = cells[i]
                if (v.row === current.row)
                    continue
                if ((dy > 0 && v.y > current.y + epsilon) ||
                        (dy < 0 && v.y < current.y - epsilon))
                    directional.push(v)
            }
            directional.sort(function(a, b) {
                return dy > 0 ? a.y - b.y : b.y - a.y
            })
            if (!directional.length)
                return -1

            // Bucket delegates by their rendered row, then pick the nearest
            // x-coordinate in the requested row. A short final row therefore
            // bends only when the requested visual column truly does not exist.
            var bands = []
            var bandTolerance = Math.max(3, current.height * 0.30)
            for (i = 0; i < directional.length; ++i) {
                var candidate = directional[i]
                var band = bands.length ? bands[bands.length - 1] : null
                if (!band || Math.abs(candidate.y - band.y) > bandTolerance) {
                    band = { y: candidate.y, cells: [] }
                    bands.push(band)
                }
                band.cells.push(candidate)
            }
            var targetBand = bands[Math.min(steps, bands.length) - 1]
            var best = targetBand.cells[0]
            var bestDx = Math.abs(best.x - current.x)
            for (i = 1; i < targetBand.cells.length; ++i) {
                var distance = Math.abs(targetBand.cells[i].x - current.x)
                if (distance < bestDx) {
                    best = targetBand.cells[i]
                    bestDx = distance
                }
            }
            return best.row
        }
        return -1
    }

    function navigateGeometry(dx, dy, leap, retry) {
        if (!grid.rows || !grid.keyMachine)
            return false
        var target = grid.geometryTarget(grid.rows.currentIndex, dx, dy,
                                         leap ? 5 : 1)
        if (target < 0) {
            // A restored panel and the initial GridView population can settle
            // one frame after browser focus arrives. Retry against that frame's
            // delegates instead of falling back to stale column arithmetic.
            if (retry !== false)
                Qt.callLater(function () {
                    grid.navigateGeometry(dx, dy, leap, false)
                })
            return false
        }
        grid.keyMachine.moveGridCursorTo(target)
        return true
    }

    function ensureSearchRowVisible(row) {
        if (!grid.searching || row < 0)
            return
        var lv = grid.searchList()
        if (!lv || !lv.positionViewAtIndex)
            return
        var groups = grid.groupsNow()
        for (var g = 0; g < groups.length; ++g) {
            var grp = groups[g]
            if (row >= grp.first && row < grp.first + grp.count) {
                lv.positionViewAtIndex(g, ListView.Contain)
                return
            }
        }
    }

    function syncThumbnails() {
        if (!grid.fileModel || !grid.visible)
            return
        var n = grid.rows ? grid.rows.count : 0
        if (n <= 0)
            return
        if (grid.searching) {
            var flick = grid.searchList()
            var y = flick ? flick.contentY : 0
            var h = flick ? flick.height : grid.height
            var rowH = Math.max(1, grid.cellHeight)
            var over = 8
            var first = Math.max(0, Math.floor(y / rowH) - over)
            var last = Math.min(n - 1, Math.ceil((y + h) / rowH) + over)
            if (last < first)
                last = first
            if (grid.filterProxy)
                grid.filterProxy.requestVisibleThumbs(first, last, grid.thumbSizePx)
            else
                grid.fileModel.requestVisibleThumbs(first, last, grid.thumbSizePx)
            return
        }
        var tiles = grid.tilesView()
        var y = tiles ? tiles.contentY : 0
        var h = tiles ? tiles.height : grid.height
        var cols = Math.max(1, grid.columns)
        var rowH = Math.max(1, grid.cellHeight)
        var over = 2
        var firstRow = Math.max(0, Math.floor(y / rowH) - over)
        var lastRow = Math.floor((y + h - 1) / rowH) + over
        var first = firstRow * cols
        var last = Math.min(n - 1, (lastRow + 1) * cols - 1)
        if (last < first)
            last = first
        if (grid.filterProxy)
            grid.filterProxy.requestVisibleThumbs(first, last, grid.thumbSizePx)
        else
            grid.fileModel.requestVisibleThumbs(first, last, grid.thumbSizePx)
    }

    function syncStartupSelectionThumbnails() {
        if (!grid.rows || grid.rows.currentIndex < 0 || grid.rows.count <= 0)
            return
        var cols = Math.max(1, grid.columns)
        var visibleRows = Math.max(1, Math.ceil(grid.height /
                                                Math.max(1, grid.cellHeight)))
        var runwayRows = Math.ceil(visibleRows / 2) + 3
        var currentRow = Math.floor(grid.rows.currentIndex / cols)
        var first = Math.max(0, (currentRow - runwayRows) * cols)
        var last = Math.min(grid.rows.count - 1,
                            (currentRow + runwayRows + 1) * cols - 1)
        if (grid.filterProxy)
            grid.filterProxy.requestVisibleThumbs(first, last,
                                                  grid.thumbSizePx)
        else
            grid.fileModel.requestVisibleThumbs(first, last,
                                                grid.thumbSizePx)
    }

    Rectangle {
        anchors.fill: parent
        z: -2
        color: "transparent"
    }

    FileDragGhost {
        id: dragGhost
        parent: grid.Window.window ? grid.Window.window.contentItem : grid
        x: -500
        y: -500
        z: -1
    }

    Loader {
        id: bodyLoader
        anchors.fill: parent
        asynchronous: false
        sourceComponent: grid.searching ? searchComp : tilesComp
        onLoaded: thumbSync.restart()
    }

    Component {
        id: tilesComp
        GridView {
            objectName: "fileGridTiles"
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: grid.layoutWidth
            model: grid.rows
            clip: true
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 0
            currentIndex: grid.rows ? grid.rows.currentIndex : -1
            cellWidth: grid.cellWidth
            cellHeight: grid.cellHeight
            cacheBuffer: cellHeight * 4
            focus: false

            // Selection chrome belongs to the delegate. A GridView highlight
            // behind it made every tile read as a persistent card.
            highlight: null

            delegate: FileGridCell {
                required property int index
                listing: grid
                rowIndex: index
            }

            onContentYChanged: thumbSync.restart()
            onContentXChanged: thumbSync.restart()

            ScrollChrome {
                flick: parent
                itemCount: grid.rows ? grid.rows.count : -1
                wheelStep: grid.cellHeight * 0.9
            }
        }
    }

    Component {
        id: searchComp
        Item {
            property alias view: groupsFlick

            ListView {
                id: groupsFlick
                objectName: "searchGridGroups"
                anchors.fill: parent
                model: grid.groupModel
                clip: true
                reuseItems: false
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: false
                spacing: 0
                cacheBuffer: grid.cellHeight * 4
                focus: false
                onContentYChanged: thumbSync.restart()

                delegate: Item {
                    id: group
                    width: groupsFlick.width
                    required property string path
                    required property string label
                    required property int first
                    required property int count
                    readonly property string folderPath: path
                    readonly property string folderLabel: label
                    readonly property int tileRows: Math.ceil(
                        Math.max(0, count) / Math.max(1, grid.columns))
                    height: grid.sectionH + tileRows * grid.cellHeight

                    Item {
                        width: parent.width
                        height: grid.sectionH

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.opaqueBackground
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.normalBorder
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.space(8)
                            anchors.rightMargin: Theme.space(8)
                            text: group.folderLabel
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideMiddle
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: group.folderPath.length > 0
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (grid.navStack)
                                grid.navStack.navigate(group.folderPath)
                        }
                    }

                    Item {
                        id: tilesHost
                        y: grid.sectionH
                        width: grid.layoutWidth
                        x: (parent.width - width) / 2
                        height: group.tileRows * grid.cellHeight
                        readonly property int visFirst: {
                            var cols = Math.max(1, grid.columns)
                            var y0 = group.y + grid.sectionH
                            var row = Math.floor(
                                        (groupsFlick.contentY - y0) /
                                        Math.max(1, grid.cellHeight))
                            return Math.max(0, (row - 1) * cols)
                        }
                        readonly property int visLast: {
                            var cols = Math.max(1, grid.columns)
                            var y0 = group.y + grid.sectionH
                            var row = Math.ceil(
                                        (groupsFlick.contentY +
                                         groupsFlick.height - y0) /
                                        Math.max(1, grid.cellHeight))
                            return Math.min(group.count - 1,
                                            (row + 1) * cols - 1)
                        }
                        readonly property int visCount: {
                            if (group.count <= 0)
                                return 0
                            var n = visLast - visFirst + 1
                            if (n !== n)
                                return 0
                            return Math.max(0, Math.min(group.count, n))
                        }

                        Repeater {
                            model: tilesHost.visCount
                            FileGridCell {
                                required property int index
                                readonly property int local: tilesHost.visFirst + index
                                x: (local % Math.max(1, grid.columns)) *
                                   grid.cellWidth
                                y: Math.floor(local /
                                              Math.max(1, grid.columns)) *
                                   grid.cellHeight
                                listing: grid
                                rowIndex: group.first + local
                                liveFromModel: true
                                name: ""
                                isDir: false
                                isSymlink: false
                                thumbnail: ""
                                thumbnailPending: false
                                path: ""
                                detail: ""
                                used: -1
                                total: -1
                                percent: -1
                            }
                        }
                    }
                }

                ScrollChrome {
                    flick: groupsFlick
                    itemCount: grid.rows ? grid.rows.count : -1
                    wheelStep: grid.cellHeight * 0.9
                }
            }
        }
    }

    FileDropSurface {
        id: listingDrop
        objectName: "listingDrop"
        anchors.fill: parent
        z: -1
        fileOps: grid.fileOps
        destPath: grid.fileModel ? grid.fileModel.path : ""
        dropEnabled: grid.dndLive
    }

    Rectangle {
        anchors.fill: parent
        visible: listingDrop.hot
        color: "transparent"
        border.color: Theme.accent
        border.width: 1
        radius: Theme.radius
        z: 1
    }

    EmptyListing {
        anchors.centerIn: parent
        fileModel: grid.fileModel
        filterProxy: grid.filterProxy
    }

    Timer {
        id: thumbSync
        interval: 16
        repeat: false
        onTriggered: {
            interval = 16
            grid.syncThumbnails()
        }
    }

    Timer {
        id: initialReveal
        interval: 32
        repeat: false
        onTriggered: {
            if (!grid.centerInitialSelection || grid.initialSelectionRevealed ||
                    !grid.rows || grid.rows.currentIndex < 0)
                return
            if (grid.initialSelectionPath.length && grid.selection &&
                    grid.selection.cursorPath() !== grid.initialSelectionPath)
                return
            var view = grid.tilesView()
            if (!view || !view.positionViewAtIndex)
                return
            view.positionViewAtIndex(grid.rows.currentIndex, GridView.Center)
            // positionViewAtIndex() can update the visual viewport during the
            // polish pass without producing a later contentY notification.
            // Re-sample after layout so the exclusive thumbnail runway tracks
            // the viewport we actually jumped to, not the launch-time page.
            postRevealThumbSync.restart()
            // A large directory arrives in batches. Sorting can move the
            // selected row after an early reveal, so keep re-centering until
            // the listing is complete and only then consume the one-shot.
            grid.initialSelectionRevealed = !grid.fileModel ||
                                            !grid.fileModel.listing
        }
    }

    Timer {
        id: postRevealThumbSync
        interval: 64
        repeat: false
        // GridView can retain its pre-jump contentY while it materializes a
        // distant current item. Anchor this one runway to the selected row;
        // ordinary wheel/drag scrolling returns to contentY-based scheduling.
        onTriggered: grid.syncStartupSelectionThumbnails()
    }

    onWidthChanged: thumbSync.restart()
    onHeightChanged: thumbSync.restart()
    Component.onCompleted: {
        thumbSync.restart()
        if (grid.centerInitialSelection)
            initialReveal.restart()
    }

    Connections {
        target: grid.rows
        function onCurrentIndexChanged() {
            if (!grid.rows || grid.rows.currentIndex < 0)
                return
            if (grid.centerInitialSelection && !grid.initialSelectionRevealed) {
                initialReveal.restart()
            } else if (grid.searching)
                grid.ensureSearchRowVisible(grid.rows.currentIndex)
            else {
                var tiles = grid.tilesView()
                if (tiles && tiles.positionViewAtIndex)
                    tiles.positionViewAtIndex(grid.rows.currentIndex,
                                              GridView.Contain)
            }
        }
        function onCountChanged() {
            thumbSync.restart()
            if (grid.centerInitialSelection && !grid.initialSelectionRevealed)
                initialReveal.restart()
        }
    }

    Connections {
        target: grid.fileModel
        function onListingChanged() {
            if (grid.centerInitialSelection && !grid.initialSelectionRevealed)
                initialReveal.restart()
        }
        function onFolderGroupsChanged() {
            thumbSync.restart()
        }
    }

    Connections {
        target: grid.selection
        function onEpochChanged() {
            if (grid.centerInitialSelection && !grid.initialSelectionRevealed)
                initialReveal.restart()
        }
    }

    onActiveFocusChanged: {
        if (activeFocus && grid.keyMachine && !grid.keyMachine.listFocused)
            grid.keyMachine.focusList()
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function (event) {
        var arrow = event.key === Qt.Key_Left || event.key === Qt.Key_Right ||
                    event.key === Qt.Key_Up || event.key === Qt.Key_Down
        if (arrow && event.modifiers === Qt.NoModifier) {
            var dx = event.key === Qt.Key_Left ? -1
                   : (event.key === Qt.Key_Right ? 1 : 0)
            var dy = event.key === Qt.Key_Up ? -1
                   : (event.key === Qt.Key_Down ? 1 : 0)
            grid.navigateGeometry(dx, dy, false, true)
            event.accepted = true
            return
        }
        if (grid.keyMachine &&
                grid.keyMachine.handleListKey(event.key, event.modifiers, event.text)) {
            event.accepted = true
        }
    }

    Connections {
        target: grid.keyMachine
        function onLocalFilterStarted() {
            var view = grid.activeScrollView()
            grid.filterRestoreContentY = view ? view.contentY : 0
        }
        function onLocalFilterCanceled() {
            Qt.callLater(function() {
                var view = grid.activeScrollView()
                if (!view)
                    return
                view.contentY = Math.max(view.originY,
                    Math.min(grid.filterRestoreContentY,
                             view.originY + Math.max(0, view.contentHeight - view.height)))
            })
        }
    }
}
