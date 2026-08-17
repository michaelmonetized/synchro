import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    readonly property var rows: root.host ? root.host.peekProxy : null
    readonly property var files: root.host ? root.host.peekModel : null
    readonly property bool grid: root.host && root.host.gridMode
    readonly property bool ready: root.rows && root.rows.count > 0

    function syncStride() {
        if (!root.host)
            return
        if (!root.grid) {
            root.host.peekGridStride = 1
            return
        }
        var w = grid.width > 1 ? grid.width : root.width
        root.host.peekGridStride = Math.max(1, Math.floor(w / Math.max(1, grid.cellWidth)))
    }

    onGridChanged: root.syncStride()
    onWidthChanged: root.syncStride()
    Component.onCompleted: root.syncStride()

    function syncThumbs(view, sizePx) {
        if (!root.files || !root.ready || view.count <= 0)
            return
        var first = 0
        var last = view.count - 1
        if (view.cellHeight && view.cellWidth) {
            var cols = Math.max(1, Math.floor(view.width / Math.max(1, view.cellWidth)))
            var rowH = Math.max(1, view.cellHeight)
            var over = 2
            var firstRow = Math.max(0, Math.floor(view.contentY / rowH) - over)
            var lastRow = Math.floor((view.contentY + view.height - 1) / rowH) + over
            first = firstRow * cols
            last = Math.min(view.count - 1, (lastRow + 1) * cols - 1)
        } else {
            var h = 24
            if (view.contentHeight > 0)
                h = Math.max(8, view.contentHeight / view.count)
            var over = 16
            first = Math.max(0, Math.floor(view.contentY / h) - over)
            last = Math.min(view.count - 1,
                            Math.ceil((view.contentY + view.height) / h) + over)
        }
        if (last < first)
            last = first
        if (root.rows && root.rows.requestVisibleThumbs)
            root.rows.requestVisibleThumbs(first, last, sizePx)
        else
            root.files.requestVisibleThumbs(first, last, sizePx)
    }

    ListView {
        id: list
        objectName: "peekFolderList"
        anchors.fill: parent
        visible: root.ready && !root.grid
        model: root.rows
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 0
        currentIndex: root.rows ? root.rows.currentIndex : -1
        focus: false

        highlight: Rectangle { color: Theme.selectedFill }

        onContentYChanged: listThumbs.restart()
        onHeightChanged: listThumbs.restart()
        onCountChanged: listThumbs.restart()
        onVisibleChanged: if (visible) listThumbs.restart()

        Timer {
            id: listThumbs
            interval: 16
            repeat: false
            onTriggered: root.syncThumbs(list, 128)
        }

        delegate: Item {
            id: row
            required property int index
            required property string name
            required property bool isDir
            required property string thumbnail
            width: ListView.view.width
            height: Math.max(Theme.fontBody + Theme.space(8), 20)

            Image {
                id: icon
                width: Theme.fontBody
                height: Theme.fontBody
                anchors.left: parent.left
                anchors.leftMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                visible: row.thumbnail.length > 0
                source: row.thumbnail
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
            }

            FolderMark {
                anchors.fill: icon
                visible: row.thumbnail.length === 0 && row.isDir
            }

            FileMark {
                anchors.fill: icon
                visible: row.thumbnail.length === 0 && !row.isDir
            }

            Text {
                anchors.left: icon.right
                anchors.right: parent.right
                anchors.leftMargin: Theme.space(8)
                anchors.verticalCenter: parent.verticalCenter
                text: row.name
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideMiddle
            }

            MouseArea {
                anchors.fill: parent
                onClicked: if (root.rows) root.rows.currentIndex = row.index
                onDoubleClicked: if (root.host) root.host.commitPeek()
            }
        }
    }

    GridView {
        id: grid
        objectName: "peekFolderGrid"
        anchors.fill: parent
        visible: root.ready && root.grid
        model: root.rows
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 0
        currentIndex: root.rows ? root.rows.currentIndex : -1
        focus: false
        cellWidth: 96 + Theme.space(16)
        cellHeight: 96 + Theme.fontBody + Theme.space(20)
        readonly property int columns: Math.max(1, Math.floor(width / Math.max(1, cellWidth)))
        onColumnsChanged: root.syncStride()
        onCellWidthChanged: root.syncStride()

        highlight: Rectangle {
            color: Theme.selectedFill
            radius: Theme.radius
        }

        onContentYChanged: gridThumbs.restart()
        onWidthChanged: {
            root.syncStride()
            gridThumbs.restart()
        }
        onHeightChanged: gridThumbs.restart()
        onCountChanged: gridThumbs.restart()
        onVisibleChanged: {
            if (visible) {
                root.syncStride()
                gridThumbs.restart()
            }
        }

        Timer {
            id: gridThumbs
            interval: 16
            repeat: false
            onTriggered: root.syncThumbs(grid, 256)
        }

        delegate: Item {
            id: cell
            required property int index
            required property string name
            required property bool isDir
            required property string thumbnail
            width: grid.cellWidth
            height: grid.cellHeight

            Item {
                id: preview
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.space(8)
                height: 80

                Image {
                    anchors.fill: parent
                    visible: cell.thumbnail.length > 0
                    source: cell.thumbnail
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
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
                onClicked: if (root.rows) root.rows.currentIndex = cell.index
                onDoubleClicked: if (root.host) root.host.commitPeek()
            }
        }
    }

    EmptyListing {
        objectName: "peekEmptyListing"
        anchors.centerIn: parent
        fileModel: root.files
        filterProxy: root.rows
    }

    Keys.onEscapePressed: if (root.host) root.host.close()
}
