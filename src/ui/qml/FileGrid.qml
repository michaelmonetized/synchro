import QtQuick
import Synchro.Theme

GridView {
    id: grid

    required property var fileModel
    property var keyMachine
    readonly property int thumbSizePx: 256
    readonly property int cellInner: 96

    signal viewToggleRequested()

    model: fileModel
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    highlightFollowsCurrentItem: true
    highlightMoveDuration: 0
    currentIndex: fileModel ? fileModel.currentIndex : -1
    focus: true
    activeFocusOnTab: true
    cellWidth: cellInner + Theme.space(16)
    cellHeight: cellInner + Theme.fontBody + Theme.space(20)
    cacheBuffer: cellHeight * 4

    highlight: Rectangle {
        color: Theme.selectedFill
        radius: Theme.radius
    }

    function syncThumbnails() {
        if (!grid.fileModel || !grid.visible)
            return
        if (grid.count <= 0) {
            grid.fileModel.requestVisibleThumbs(0, -1, grid.thumbSizePx)
            return
        }
        var first = grid.indexAt(1, grid.contentY + 1)
        var last = grid.indexAt(Math.max(1, grid.width - 2),
                                grid.contentY + grid.height - 2)
        if (first < 0 && last < 0) {
            thumbSync.interval = 50
            thumbSync.start()
            return
        }
        if (first < 0)
            first = 0
        if (last < 0)
            last = Math.min(grid.count - 1, first + 40)
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
    onVisibleChanged: if (visible) thumbSync.restart()
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

            Rectangle {
                anchors.fill: parent
                visible: cell.thumbnail.length === 0
                color: "transparent"
                border.color: Theme.normalBorder
                border.width: 1
                radius: Theme.radius

                Text {
                    anchors.centerIn: parent
                    text: cell.isDir ? "▸" : cell.isSymlink ? "↗" : "·"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
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
            acceptedButtons: Qt.LeftButton
            onClicked: grid.fileModel.currentIndex = cell.index
            onDoubleClicked: grid.fileModel.activateCurrent()
        }
    }

    Keys.onPressed: function (event) {
        var peekKeys = event.key === Qt.Key_Space || event.key === Qt.Key_L ||
                       event.key === Qt.Key_Escape ||
                       (grid.keyMachine && grid.keyMachine.peekOpen)
        if (grid.keyMachine && peekKeys &&
                grid.keyMachine.handleListKey(event.key, event.modifiers, event.text)) {
            event.accepted = true
            return
        }
        if (!grid.fileModel)
            return
        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
            grid.fileModel.moveCursor(1)
            event.accepted = true
        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            grid.fileModel.moveCursor(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            grid.fileModel.activateCurrent()
            event.accepted = true
        } else if (event.key === Qt.Key_V && event.modifiers === Qt.NoModifier) {
            grid.viewToggleRequested()
            event.accepted = true
        }
    }

    Connections {
        target: grid.fileModel
        function onCurrentIndexChanged() {
            if (grid.fileModel.currentIndex >= 0)
                grid.positionViewAtIndex(grid.fileModel.currentIndex, GridView.Contain)
        }
    }
}
