import QtQuick
import Synchro.Theme

GridView {
    id: grid

    required property var fileModel
    property var filterProxy
    property var keyMachine
    property var selection
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property bool showCursorChrome: !keyMachine || keyMachine.listFocused
    readonly property int thumbSizePx: 256
    readonly property int cellInner: 96

    signal viewToggleRequested()
    signal doRequested()

    model: grid.rows
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    highlightFollowsCurrentItem: true
    highlightMoveDuration: 0
    currentIndex: grid.rows ? grid.rows.currentIndex : -1
    focus: true
    activeFocusOnTab: true
    cellWidth: cellInner + Theme.space(16)
    cellHeight: cellInner + Theme.fontBody + Theme.space(20)
    cacheBuffer: cellHeight * 4
    readonly property int columns: Math.max(1, Math.floor(width / Math.max(1, cellWidth)))

    onColumnsChanged: if (visible && keyMachine)
        keyMachine.gridStride = columns

    highlight: Rectangle {
        color: Theme.selectedFill
        radius: Theme.radius
        visible: grid.showCursorChrome
    }

    EmptyListing {
        anchors.centerIn: parent
        fileModel: grid.fileModel
        filterProxy: grid.filterProxy
    }

    ScrollChrome {
        flick: grid
    }

    function syncThumbnails() {
        if (!grid.fileModel || !grid.visible)
            return
        if (grid.count <= 0)
            return
        var cols = Math.max(1, grid.columns)
        var rowH = Math.max(1, grid.cellHeight)
        var over = 2
        var firstRow = Math.max(0, Math.floor(grid.contentY / rowH) - over)
        var lastRow = Math.floor((grid.contentY + grid.height - 1) / rowH) + over
        var first = firstRow * cols
        var last = Math.min(grid.count - 1, (lastRow + 1) * cols - 1)
        if (last < first)
            last = first
        if (grid.filterProxy)
            grid.filterProxy.requestVisibleThumbs(first, last, grid.thumbSizePx)
        else
            grid.fileModel.requestVisibleThumbs(first, last, grid.thumbSizePx)
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

    onContentYChanged: thumbSync.restart()
    onContentXChanged: thumbSync.restart()
    onWidthChanged: thumbSync.restart()
    onHeightChanged: thumbSync.restart()
    onCountChanged: thumbSync.restart()
    onVisibleChanged: {
        if (visible && keyMachine)
            keyMachine.gridStride = columns
        if (visible)
            thumbSync.restart()
    }
    Component.onCompleted: thumbSync.restart()

    delegate: Item {
        id: cell

        required property int index
        required property string name
        required property bool isDir
        required property bool isSymlink
        required property string thumbnail

        width: grid.cellWidth
        height: grid.cellHeight

        readonly property bool picked: grid.selection &&
                                       grid.selectionEpoch >= 0 &&
                                       grid.selection.isSelected(cell.index)

        Rectangle {
            anchors.fill: parent
            visible: grid.showCursorChrome && cell.picked && !cell.GridView.isCurrentItem
            color: Theme.selectedFill
            opacity: 0.45
            radius: Theme.radius
        }

        Rectangle {
            anchors.fill: parent
            visible: hover.hovered && !cell.GridView.isCurrentItem
            color: Theme.hoverFill
            radius: Theme.radius
        }

        HoverHandler {
            id: hover
        }

        Item {
            id: preview
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.space(8)
            height: grid.cellInner

            Image {
                anchors.fill: parent
                visible: cell.thumbnail.length > 0
                source: cell.thumbnail
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: grid.cellInner
                sourceSize.height: grid.cellInner
            }

            FolderMark {
                anchors.fill: parent
                visible: cell.thumbnail.length === 0 && cell.isDir
            }

            FileMark {
                anchors.centerIn: parent
                width: Math.round(parent.width * 0.72)
                height: Math.round(parent.height * 0.84)
                visible: cell.thumbnail.length === 0 && !cell.isDir
                suffix: {
                    var n = cell.name
                    var i = n.lastIndexOf(".")
                    if (i <= 0 || i === n.length - 1)
                        return ""
                    return n.slice(i + 1).toUpperCase()
                }
            }
        }

        Text {
            anchors.top: preview.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.space(4)
            anchors.rightMargin: Theme.space(4)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: cell.name
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            onClicked: function (mouse) {
                grid.forceActiveFocus()
                if (mouse.button === Qt.MiddleButton) {
                    grid.viewToggleRequested()
                    return
                }
                if (mouse.button === Qt.RightButton) {
                    if (grid.keyMachine && grid.keyMachine.mode === "field-search")
                        grid.keyMachine.focusList()
                    if (grid.selection) {
                        if (!grid.selection.isSelected(cell.index))
                            grid.selection.click(cell.index)
                    } else if (grid.filterProxy) {
                        grid.filterProxy.selectRow(cell.index)
                    } else {
                        grid.fileModel.currentIndex = cell.index
                    }
                    grid.doRequested()
                    return
                }
                if (grid.keyMachine && grid.keyMachine.mode === "field-search")
                    grid.keyMachine.focusList()
                if (grid.selection) {
                    if (mouse.modifiers & Qt.ControlModifier)
                        grid.selection.ctrlClick(cell.index)
                    else if (mouse.modifiers & Qt.ShiftModifier)
                        grid.selection.shiftClick(cell.index)
                    else
                        grid.selection.click(cell.index)
                    return
                }
                if (grid.filterProxy)
                    grid.filterProxy.selectRow(cell.index)
                else
                    grid.fileModel.currentIndex = cell.index
            }
            onDoubleClicked: {
                if (grid.filterProxy)
                    grid.filterProxy.activateCurrent()
                else
                    grid.fileModel.activateCurrent()
            }
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

    Connections {
        target: grid.rows
        function onCurrentIndexChanged() {
            if (grid.rows && grid.rows.currentIndex >= 0)
                grid.positionViewAtIndex(grid.rows.currentIndex, GridView.Contain)
        }
    }
}
