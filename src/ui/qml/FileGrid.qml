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
    property bool dndEnabled: true
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property bool dndLive: dndEnabled && fileOps && fileModel &&
                                    !fileModel.isTrash && !fileModel.isRecent &&
                                    !fileModel.isSearch && !fileModel.isVolumes
    readonly property bool searching: fileModel && fileModel.isSearch
    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property bool showCursorChrome: !keyMachine ||
                                             (keyMachine.listFocused &&
                                              !keyMachine.panelFocused)
    readonly property int thumbSizePx: 256
    readonly property int preferredInner: 96
    readonly property int cellPad: Theme.space(16)
    readonly property int preferredCell: preferredInner + cellPad
    readonly property int columns: Math.max(
                                       1, Math.floor(width / Math.max(1, preferredCell)))
    readonly property int cellInner: Math.max(
                                         Theme.space(48),
                                         Math.round(cellWidth - cellPad))
    readonly property real cellWidth: width > 0 ? width / columns : preferredCell
    readonly property real cellHeight: cellInner + Theme.fontBody + Theme.space(20)
    readonly property var groupModel: visible && searching && fileModel
                                      ? fileModel.folderGroupModel : null
    readonly property int sectionH: Math.max(Theme.fontBody + Theme.space(10), 24)

    signal viewToggleRequested()
    signal doRequested()

    focus: true
    activeFocusOnTab: true
    clip: true

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

    Rectangle {
        anchors.fill: parent
        z: -2
        color: Theme.opaqueBackground
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
            anchors.fill: parent
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

            highlight: Rectangle {
                color: Theme.selectedFill
                radius: Theme.radius
                visible: grid.showCursorChrome
            }

            delegate: FileGridCell {
                required property int index
                listing: grid
                rowIndex: index
            }

            onContentYChanged: thumbSync.restart()
            onContentXChanged: thumbSync.restart()

            ScrollChrome {
                flick: parent
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
                        width: parent.width
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

    onWidthChanged: thumbSync.restart()
    onHeightChanged: thumbSync.restart()
    Component.onCompleted: thumbSync.restart()

    Connections {
        target: grid.rows
        function onCurrentIndexChanged() {
            if (!grid.rows || grid.rows.currentIndex < 0)
                return
            if (grid.searching)
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
        }
    }

    Connections {
        target: grid.fileModel
        function onFolderGroupsChanged() {
            thumbSync.restart()
        }
    }

    onActiveFocusChanged: {
        if (activeFocus && grid.keyMachine && grid.keyMachine.fieldFocused &&
                grid.keyMachine.mode !== "field-search")
            grid.keyMachine.focusList()
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function (event) {
        if (grid.keyMachine &&
                grid.keyMachine.handleListKey(event.key, event.modifiers, event.text)) {
            event.accepted = true
            return
        }
        if (!grid.fileModel)
            return
        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
            if (grid.filterProxy)
                grid.filterProxy.moveCursor(1)
            else
                grid.fileModel.moveCursor(1)
            event.accepted = true
        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            if (grid.filterProxy)
                grid.filterProxy.moveCursor(-1)
            else
                grid.fileModel.moveCursor(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (grid.filterProxy)
                grid.filterProxy.activateCurrent()
            else
                grid.fileModel.activateCurrent()
            event.accepted = true
        } else if (event.key === Qt.Key_V && event.modifiers === Qt.NoModifier) {
            grid.viewToggleRequested()
            event.accepted = true
        }
    }
}
