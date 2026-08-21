import QtQuick
import Synchro.Theme

// Read-only folder visualization for the do-layer. No activate / peek / keys.
GridView {
    id: grid

    property var fileModel: null
    property var filterProxy: null
    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property int thumbSizePx: 128
    readonly property int preferredInner: 72
    readonly property int cellPad: Theme.space(12)
    readonly property int preferredCell: preferredInner + cellPad
    readonly property int columns: Math.max(
                                       1, Math.floor(width / Math.max(1, preferredCell)))
    readonly property int cellInner: Math.max(
                                         Theme.space(40),
                                         Math.round(cellWidth - cellPad))

    objectName: "doFolderGrid"
    model: grid.rows
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    highlightFollowsCurrentItem: false
    focus: false
    activeFocusOnTab: false
    interactive: contentHeight > height + 1
    cellWidth: width > 0 ? width / columns : preferredCell
    cellHeight: cellInner + Theme.fontBody + Theme.space(14)
    cacheBuffer: cellHeight * 3

    EmptyListing {
        anchors.centerIn: parent
        fileModel: grid.fileModel
        filterProxy: grid.filterProxy
    }

    ScrollChrome {
        flick: grid
    }

    function syncThumbnails() {
        if (!grid.visible || grid.count <= 0)
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
        if (grid.filterProxy && grid.filterProxy.requestVisibleThumbs)
            grid.filterProxy.requestVisibleThumbs(first, last, grid.thumbSizePx)
        else if (grid.fileModel && grid.fileModel.requestVisibleThumbs)
            grid.fileModel.requestVisibleThumbs(first, last, grid.thumbSizePx)
    }

    Timer {
        id: thumbSync
        interval: 16
        repeat: false
        onTriggered: grid.syncThumbnails()
    }

    onContentYChanged: thumbSync.restart()
    onWidthChanged: thumbSync.restart()
    onHeightChanged: thumbSync.restart()
    onCountChanged: thumbSync.restart()
    onVisibleChanged: if (visible)
        thumbSync.restart()
    Component.onCompleted: thumbSync.restart()

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
            anchors.margins: Theme.space(6)
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
    }
}
