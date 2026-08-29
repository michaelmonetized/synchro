import QtQuick
import Synchro.Theme

ListView {
    id: list

    required property var fileModel
    property var filterProxy
    property var navStack
    property var keyMachine
    property var selection
    property var host: null
    property var fileOps: null
    property bool dndEnabled: true
    // Companion surfaces can reuse the complete table without letting its
    // private DirectoryModel navigate independently of the main browser.
    property bool externalActivation: false
    property bool centerInitialSelection: false
    property string initialSelectionPath: ""
    property bool initialSelectionRevealed: false
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property int thumbSizePx: 128
    readonly property bool dndLive: dndEnabled && fileOps && fileModel &&
                                    !fileModel.isTrash && !fileModel.isRecent &&
                                    !fileModel.isSearch && !fileModel.isVolumes &&
                                    !fileModel.isSql
    readonly property bool searching: fileModel && fileModel.isSearch

    signal viewToggleRequested()
    signal doRequested(real sceneX, real sceneY)
    signal rowActivated(int row)

    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property bool showCursorChrome: !keyMachine || keyMachine.listFocused
    // Keys belong to a panel: keep the cursor visible (panel apps target
    // the selected file) but dimmed so focus stays legible.
    readonly property bool cursorDim: !!(keyMachine && keyMachine.panelFocused)
    readonly property real contentInset: Math.max(0, (width - Theme.space(1600)) / 2)

    readonly property Component folderMarkComp: Component { FolderMark {} }
    readonly property Component fileMarkComp: Component { FileMark {} }

    // ---- detail columns (Name | Size | Type | Modified) ----
    readonly property real colScale: Math.max(0.6, Theme.fontBody / 12)
    readonly property int sizeColW: Math.round(76 * colScale)
    readonly property int typeColW: Math.round(150 * colScale)
    readonly property int mtimeColW: Math.round(124 * colScale)
    readonly property bool showSizeCol: width >= 360 * colScale
    readonly property bool showTypeCol: width >= 640 * colScale
    readonly property bool showMtimeCol: width >= 480 * colScale
    readonly property bool showColumns: !searching
    readonly property bool headerVisible: showColumns && filterProxy &&
                                          fileModel && count > 0
    // Recents and search keep their own order; headers stay informative
    // but stop offering sort there.
    readonly property bool headerSortable: fileModel && !fileModel.isRecent &&
                                           !fileModel.isSearch && !fileModel.isSql

    function fmtSize(n) {
        if (n === undefined || n === null || n < 0)
            return "—"
        if (n < 1024)
            return n + " B"
        var units = ["KB", "MB", "GB", "TB", "PB"]
        var v = n
        for (var i = 0; i < units.length; ++i) {
            v /= 1024
            if (v < 1024 || i === units.length - 1)
                return (v < 10 ? v.toFixed(1) : Math.round(v)) + " " + units[i]
        }
        return ""
    }

    function fmtMtime(ms) {
        if (!ms || ms <= 0)
            return ""
        return Qt.formatDateTime(new Date(ms), "yyyy-MM-dd hh:mm")
    }

    function headerClicked(role) {
        if (!list.filterProxy || !list.headerSortable)
            return
        if (list.filterProxy.sortRoleName === role)
            list.filterProxy.sortOrder =
                    list.filterProxy.sortOrder === "desc" ? "asc" : "desc"
        else {
            list.filterProxy.sortRoleName = role
            list.filterProxy.sortOrder = "asc"
        }
    }

    function activateRow(row) {
        if (list.externalActivation) {
            if (row < 0)
                return
            if (list.filterProxy)
                list.filterProxy.selectRow(row)
            else if (list.fileModel)
                list.fileModel.currentIndex = row
            list.rowActivated(row)
            return
        }
        if (list.filterProxy)
            list.filterProxy.activateCurrent()
        else if (list.fileModel)
            list.fileModel.activateCurrent()
    }

    component SortHeader: Item {
        id: head
        property string role
        property string label
        property int align: Text.AlignLeft
        readonly property bool active: list.filterProxy &&
                                       list.filterProxy.sortRoleName === role

        Rectangle {
            anchors.fill: parent
            color: Theme.hoverFill
            visible: headHover.hovered && list.headerSortable
        }

        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.space(4)
            anchors.rightMargin: Theme.space(4)
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: head.align
            text: head.active
                  ? head.label + (list.filterProxy.sortOrder === "desc"
                                  ? " ▾" : " ▴")
                  : head.label
            color: head.active ? Theme.accent : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            font.bold: head.active
            elide: Text.ElideRight
        }

        HoverHandler { id: headHover }

        MouseArea {
            anchors.fill: parent
            enabled: list.headerSortable
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: list.headerClicked(head.role)
        }
    }

    model: list.visible ? list.rows : null
    clip: true
    reuseItems: !list.searching
    readonly property int sectionH: Math.max(Theme.controlHeight, 28)
    readonly property int rowInner: Math.max(Theme.controlHeight + Theme.spaceMD, 34)
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    highlightFollowsCurrentItem: true
    highlightMoveDuration: 0
    currentIndex: list.rows ? list.rows.currentIndex : -1
    focus: true
    activeFocusOnTab: true
    cacheBuffer: Math.max(0, Theme.fontBody + Theme.space(8)) * 8
    topMargin: headerVisible ? sectionH : 0

    Rectangle {
        anchors.fill: parent
        z: -2
        color: "transparent"
    }

    Item {
        id: colHeader
        objectName: "listColumnHeader"
        visible: list.headerVisible
        z: 3
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: list.sectionH

        Rectangle {
            anchors.fill: parent
            color: Theme.darkBackground
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.normalBorder
        }

        SortHeader {
            role: "name"
            label: "Name"
            anchors.left: parent.left
            anchors.leftMargin: list.contentInset + Theme.space(12) +
                                Theme.space(24) + Theme.spaceLG
            anchors.right: headerCols.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
        }

        Row {
            id: headerCols
            anchors.right: parent.right
            anchors.rightMargin: list.contentInset + Theme.space(12)
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: Theme.space(6)

            SortHeader {
                role: "size"
                label: "Size"
                align: Text.AlignRight
                width: list.sizeColW
                height: parent.height
                visible: list.showSizeCol
            }
            SortHeader {
                role: "type"
                label: "Type"
                width: list.typeColW
                height: parent.height
                visible: list.showTypeCol
            }
            SortHeader {
                role: "mtime"
                label: "Modified"
                width: list.mtimeColW
                height: parent.height
                visible: list.showMtimeCol
            }
        }
    }

    highlight: Item {
        visible: list.showCursorChrome
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: list.rowInner
            color: Theme.selectedFill
        }
    }

    function folderLabel(parentPath) {
        var root = list.fileModel && list.fileModel.searchRoot
                   ? list.fileModel.searchRoot : ""
        if (!parentPath)
            return ""
        if (root && parentPath === root)
            return "this folder"
        if (root && parentPath.indexOf(root + "/") === 0)
            return parentPath.slice(root.length + 1)
        return parentPath
    }

    function parentPathAt(i) {
        if (list.fileModel && list.fileModel.rowMap)
            return list.fileModel.rowMap(i).parentPath || ""
        return ""
    }

    function dragPathsFor(index, path) {
        if (list.selection && list.selection.isSelected(index)) {
            var picked = list.selection.selectedPaths()
            if (picked && picked.length)
                return picked
        }
        return path ? [path] : []
    }

    function armDrag(item, index, path, name, thumbnail, isDir) {
        if (!list.dndLive || !list.fileOps)
            return
        var paths = list.dragPathsFor(index, path)
        if (!paths.length)
            return
        item.Drag.mimeData = list.fileOps.dragMime(paths)
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

    FileDragGhost {
        id: dragGhost
        parent: list.Window.window ? list.Window.window.contentItem : list
        x: -500
        y: -500
        z: -1
    }

    FileDropSurface {
        id: listingDrop
        objectName: "listingDrop"
        anchors.fill: parent
        z: -1
        fileOps: list.fileOps
        destPath: list.fileModel ? list.fileModel.path : ""
        dropEnabled: list.dndLive
    }

    Rectangle {
        anchors.fill: parent
        visible: listingDrop.hot
        color: "transparent"
        border.color: Theme.accent
        border.width: 1
        z: 1
    }

    EmptyListing {
        anchors.centerIn: parent
        fileModel: list.fileModel
        filterProxy: list.filterProxy
    }

    ScrollChrome {
        flick: list
    }

    function syncThumbnails() {
        if (!list.fileModel || !list.visible)
            return
        if (list.count <= 0)
            return
        var h = 24
        if (list.count > 0 && list.contentHeight > 0)
            h = Math.max(8, list.contentHeight / list.count)
        var over = 16
        var first = Math.max(0, Math.floor(list.contentY / h) - over)
        var last = Math.min(list.count - 1,
                            Math.ceil((list.contentY + list.height) / h) + over)
        if (list.filterProxy)
            list.filterProxy.requestVisibleThumbs(first, last, list.thumbSizePx)
        else
            list.fileModel.requestVisibleThumbs(first, last, list.thumbSizePx)
    }

    function syncStartupSelectionThumbnails() {
        if (!list.rows || list.rows.currentIndex < 0 || list.count <= 0)
            return
        var rowHeight = 24
        if (list.count > 0 && list.contentHeight > 0)
            rowHeight = Math.max(8, list.contentHeight / list.count)
        var visibleRows = Math.max(1, Math.ceil(list.height / rowHeight))
        var runway = Math.ceil(visibleRows / 2) + 16
        var first = Math.max(0, list.rows.currentIndex - runway)
        var last = Math.min(list.count - 1,
                            list.rows.currentIndex + runway)
        if (list.filterProxy)
            list.filterProxy.requestVisibleThumbs(first, last,
                                                  list.thumbSizePx)
        else
            list.fileModel.requestVisibleThumbs(first, last,
                                                list.thumbSizePx)
    }

    Timer {
        id: thumbSync
        interval: 16
        repeat: false
        onTriggered: {
            interval = 16
            list.syncThumbnails()
        }
    }

    onContentYChanged: thumbSync.restart()
    onHeightChanged: thumbSync.restart()
    onCountChanged: thumbSync.restart()
    onVisibleChanged: if (visible) thumbSync.restart()
    Component.onCompleted: {
        thumbSync.restart()
        if (list.centerInitialSelection)
            initialReveal.restart()
    }

    delegate: Item {
        id: row

        required property int index
        required property string name
        required property bool isDir
        required property bool isSymlink
        required property string thumbnail
        required property bool thumbnailPending
        // Must be required: Qt only maps model roles onto required
        // properties once a delegate uses that style.
        required property string path
        required property string detail
        required property double size
        required property double mtime
        required property string typeLabel
        required property var used
        required property var total
        required property int percent
        required property string parentPath
        required property string parentLabel

        readonly property bool showFolderHead: {
            if (!list.searching)
                return false
            if (index <= 0)
                return true
            return list.parentPathAt(index - 1) !== row.parentPath
        }

        width: ListView.view.width
        height: list.rowInner + (showFolderHead ? list.sectionH : 0)

        Drag.dragType: Drag.Automatic
        Drag.active: dragArea.drag.active && list.dndLive
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.proposedAction: Qt.MoveAction
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2

        readonly property bool picked: !!(list.selection &&
                                          list.selectionEpoch >= 0 &&
                                          list.selection.isSelected(row.index))

        Rectangle {
            anchors.fill: parent
            color: "transparent"
        }

        Loader {
            id: folderHead
            width: parent.width
            height: row.showFolderHead ? list.sectionH : 0
            active: list.searching
            visible: row.showFolderHead
            sourceComponent: Item {
                clip: true

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
                    anchors.leftMargin: list.contentInset + Theme.space(12)
                    anchors.rightMargin: list.contentInset + Theme.space(12)
                    text: list.folderLabel(row.parentPath)
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideMiddle
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: row.parentPath.length > 0 && list.navStack
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: list.navStack.navigate(row.parentPath)
                }
            }
        }

        Item {
            id: fileRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: list.rowInner

        Rectangle {
            anchors.fill: parent
            visible: list.showCursorChrome && row.ListView.isCurrentItem &&
                     row.picked
            color: Theme.selectedFill
            opacity: list.cursorDim ? 0.45 : 1
        }

        Rectangle {
            anchors.fill: parent
            visible: list.showCursorChrome && row.picked && !row.ListView.isCurrentItem
            color: Theme.selectedFill
            opacity: 0.45
        }

        Rectangle {
            anchors.fill: parent
            visible: hover.hovered && !row.ListView.isCurrentItem
            color: Theme.hoverFill
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: list.contentInset
            anchors.rightMargin: list.contentInset
            height: 1
            color: Theme.alpha(Theme.foreground, 0.045)
        }

        Rectangle {
            anchors.fill: parent
            visible: folderDrop.hot
            color: "transparent"
            border.color: Theme.accent
            border.width: 1
        }

        FileDropSurface {
            id: folderDrop
            objectName: "folderDrop"
            anchors.fill: parent
            fileOps: list.fileOps
            destPath: row.isDir ? row.path : ""
            dropEnabled: list.dndLive && row.isDir
        }

        HoverHandler {
            id: hover
        }

        Item {
            id: iconBox
            width: Theme.space(24)
            height: Theme.space(24)
            anchors.left: parent.left
            anchors.leftMargin: list.contentInset + Theme.space(12)
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                color: Theme.darkBackground
                border.color: Theme.alpha(Theme.foreground, 0.10)
                border.width: 1
                radius: Theme.radius
            }

            Image {
                id: rowThumbImage
                anchors.fill: parent
                anchors.margins: 1
                visible: row.thumbnail.length > 0
                source: row.thumbnail
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: iconBox.width
                sourceSize.height: iconBox.height
            }

            Loader {
                anchors.fill: parent
                active: row.thumbnail.length === 0
                opacity: row.thumbnailPending ? 0.18 : 1
                sourceComponent: row.isDir ? list.folderMarkComp
                                           : list.fileMarkComp
            }

            ThumbLoadingGlyph {
                anchors.fill: parent
                anchors.margins: Theme.spaceXS
                running: row.thumbnailPending ||
                         (row.thumbnail.length > 0 &&
                          rowThumbImage.status === Image.Loading)
            }
        }

        Text {
            anchors.left: iconBox.right
            anchors.right: cols.visible ? cols.left
                                        : (chrome.visible ? chrome.left
                                                          : parent.right)
            anchors.leftMargin: Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            text: row.name
            color: row.ListView.isCurrentItem || row.picked
                   ? Theme.brightForeground : Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: row.ListView.isCurrentItem || row.picked
                         ? Font.DemiBold : Font.Normal
            elide: Text.ElideMiddle
        }

        Row {
            id: cols
            anchors.right: chrome.visible ? chrome.left : parent.right
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space(6)
            visible: list.showColumns &&
                     (list.showSizeCol || list.showTypeCol || list.showMtimeCol)

            Text {
                width: list.sizeColW
                visible: list.showSizeCol
                horizontalAlignment: Text.AlignRight
                text: row.isDir ? "—" : list.fmtSize(row.size)
                color: Theme.darkForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontBodySmall
                elide: Text.ElideRight
            }
            Text {
                width: list.typeColW
                visible: list.showTypeCol
                text: row.typeLabel
                color: Theme.darkForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBodySmall
                elide: Text.ElideRight
            }
            Text {
                width: list.mtimeColW
                visible: list.showMtimeCol
                text: list.fmtMtime(row.mtime)
                color: Theme.darkForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontBodySmall
                elide: Text.ElideRight
            }
        }

        ListingChrome {
            id: chrome
            objectName: "listingRowChrome"
            anchors.right: parent.right
            anchors.rightMargin: list.contentInset + Theme.space(12)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.55)
            height: parent.height
            host: list.host
            mode: "row"
            file: row.path ? Qt.resolvedUrl("file://" + row.path) : ""
            name: row.name
            path: row.path
            detail: row.detail
            used: row.used
            total: row.total
            percent: row.percent
        }

        Item {
            id: dragProxy
            width: 1
            height: 1
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            property bool didDrag: false
            drag.target: list.dndLive ? dragProxy : null
            drag.threshold: Math.max(8, Qt.styleHints.startDragDistance)
            preventStealing: drag.active

            onPressed: function (mouse) {
                didDrag = false
                if (mouse.button !== Qt.LeftButton || !list.dndLive)
                    return
                list.armDrag(row, row.index, row.path, row.name,
                             row.thumbnail, row.isDir)
            }
            onPositionChanged: if (drag.active)
                didDrag = true
            onReleased: {
                dragProxy.x = 0
                dragProxy.y = 0
            }
            onClicked: function (mouse) {
                if (didDrag)
                    return
                list.forceActiveFocus()
                if (mouse.button === Qt.MiddleButton) {
                    list.viewToggleRequested()
                    return
                }
                if (mouse.button === Qt.RightButton) {
                    if (list.keyMachine && list.keyMachine.mode === "field-search")
                        list.keyMachine.focusList()
                    if (list.selection) {
                        if (!list.selection.isSelected(row.index))
                            list.selection.click(row.index)
                    } else if (list.filterProxy) {
                        list.filterProxy.selectRow(row.index)
                    } else {
                        list.fileModel.currentIndex = row.index
                    }
                    var point = dragArea.mapToItem(null, mouse.x, mouse.y)
                    list.doRequested(point.x, point.y)
                    return
                }
                if (list.keyMachine && list.keyMachine.mode === "field-search")
                    list.keyMachine.focusList()
                if (list.selection) {
                    if (mouse.modifiers & Qt.ControlModifier)
                        list.selection.ctrlClick(row.index)
                    else if (mouse.modifiers & Qt.ShiftModifier)
                        list.selection.shiftClick(row.index)
                    else
                        list.selection.click(row.index)
                    return
                }
                if (list.filterProxy)
                    list.filterProxy.selectRow(row.index)
                else
                    list.fileModel.currentIndex = row.index
            }
            onDoubleClicked: {
                list.activateRow(row.index)
            }
        }
        }
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function (event) {
        if (list.keyMachine &&
                list.keyMachine.handleListKey(event.key, event.modifiers, event.text)) {
            event.accepted = true
            return
        }
        if (!list.fileModel)
            return
        var alt = event.modifiers & Qt.AltModifier
        var ctrl = event.modifiers & Qt.ControlModifier
        var meta = event.modifiers & Qt.MetaModifier
        var shift = event.modifiers & Qt.ShiftModifier
        var chord = ctrl || meta
        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
            if (alt || chord)
                return
            if (list.filterProxy)
                list.filterProxy.moveCursor(1)
            else
                list.fileModel.moveCursor(1)
            event.accepted = true
        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            if (alt || chord)
                return
            if (list.filterProxy)
                list.filterProxy.moveCursor(-1)
            else
                list.fileModel.moveCursor(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (alt || chord)
                return
            list.activateRow(list.currentIndex)
            event.accepted = true
        } else if ((event.key === Qt.Key_L || event.key === Qt.Key_Right) &&
                   !alt && !chord && !shift) {
            list.activateRow(list.currentIndex)
            event.accepted = true
        } else if ((event.key === Qt.Key_H || event.key === Qt.Key_Backspace ||
                    event.key === Qt.Key_Left) &&
                   !alt && !chord && !shift) {
            if (list.navStack)
                list.navStack.goUp()
            event.accepted = true
        } else if (event.key === Qt.Key_Left && alt && !chord) {
            if (list.navStack)
                list.navStack.goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Right && alt && !chord) {
            if (list.navStack)
                list.navStack.goForward()
            event.accepted = true
        } else if (event.key === Qt.Key_Period && !alt && !chord && !shift) {
            list.fileModel.showHidden = !list.fileModel.showHidden
            event.accepted = true
        } else if (event.key === Qt.Key_V && event.modifiers === Qt.NoModifier) {
            list.viewToggleRequested()
            event.accepted = true
        }
    }

    onActiveFocusChanged: {
        if (activeFocus && list.keyMachine && list.keyMachine.fieldFocused &&
                list.keyMachine.mode !== "field-search")
            list.keyMachine.focusList()
    }

    Connections {
        target: list.rows
        function onCurrentIndexChanged() {
            if (!list.rows || list.rows.currentIndex < 0)
                return
            if (list.centerInitialSelection && !list.initialSelectionRevealed) {
                initialReveal.restart()
            } else {
                list.positionViewAtIndex(list.rows.currentIndex, ListView.Contain)
            }
        }
        function onCountChanged() {
            if (list.centerInitialSelection && !list.initialSelectionRevealed)
                initialReveal.restart()
        }
    }

    Timer {
        id: initialReveal
        interval: 32
        repeat: false
        onTriggered: {
            if (!list.centerInitialSelection || list.initialSelectionRevealed ||
                    !list.rows || list.rows.currentIndex < 0)
                return
            if (list.initialSelectionPath.length && list.selection &&
                    list.selection.cursorPath() !== list.initialSelectionPath)
                return
            list.positionViewAtIndex(list.rows.currentIndex, ListView.Center)
            // Programmatic positioning may settle after contentYChanged; make
            // the thumbnail runway follow the final painted rows explicitly.
            postRevealThumbSync.restart()
            // Rows can continue arriving and re-sort around the selected file.
            // Keep the startup target centered until the listing settles.
            list.initialSelectionRevealed = !list.fileModel ||
                                            !list.fileModel.listing
        }
    }

    Timer {
        id: postRevealThumbSync
        interval: 64
        repeat: false
        onTriggered: list.syncStartupSelectionThumbnails()
    }

    Connections {
        target: list.fileModel
        function onListingChanged() {
            if (list.centerInitialSelection && !list.initialSelectionRevealed)
                initialReveal.restart()
        }
    }

    Connections {
        target: list.selection
        function onEpochChanged() {
            if (list.centerInitialSelection && !list.initialSelectionRevealed)
                initialReveal.restart()
        }
    }

}
