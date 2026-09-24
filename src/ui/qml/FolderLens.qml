import QtQuick
import Synchro.Theme

// One child column, not a second file manager. It borrows the browser's
// thumbnail language and commits navigation back to the authoritative view.
FocusScope {
    id: root

    property var host: null
    property var folderModel: null
    property var folderProxy: null
    property bool loading: false
    property string errorText: ""
    property bool truncated: false
    property bool queryBusy: false
    property bool gridMode: true
    property int currentIndex: -1
    readonly property int count: folderProxy ? folderProxy.count : 0
    readonly property int columns: Math.max(2, Math.floor(width /
                                                          Theme.space(118)))
    readonly property real cellWidth: width / columns
    readonly property int cellHeight: Theme.space(112)
    readonly property int thumbSizePx: gridMode ? 256 : 128
    signal viewToggleRequested()

    objectName: "folderLens"
    activeFocusOnTab: true
    clip: true

    function activeView() {
        return viewLoader.item
    }

    function selectRow(index) {
        if (index < 0 || index >= count)
            return
        forceActiveFocus()
        currentIndex = index
        if (folderProxy)
            folderProxy.setCurrentIndex(index)
    }

    function commitRow(index) {
        selectRow(index)
        if (host)
            host.commitInlineFolderRow(index)
    }

    function moveCurrent(delta) {
        if (count <= 0)
            return
        var next = currentIndex < 0 ? 0
                                    : Math.max(0, Math.min(count - 1,
                                                          currentIndex + delta))
        selectRow(next)
        Qt.callLater(positionCurrent)
    }

    function positionCurrent() {
        var view = activeView()
        if (view && currentIndex >= 0 && view.positionViewAtIndex)
            view.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    function syncThumbs() {
        var view = activeView()
        if (!visible || !folderProxy || !view || count <= 0)
            return
        // FileList owns its own list-specific runway calculation.
        if (!gridMode)
            return
        var first = 0
        var last = 0
        // Match the main grid's two-row runway. Cached tiles decode while
        // the user approaches them instead of after they enter the view.
        var firstRow = Math.max(0, Math.floor(view.contentY / cellHeight) - 2)
        var lastRow = Math.floor((view.contentY + view.height) /
                                 cellHeight) + 2
        first = firstRow * columns
        last = Math.min(count - 1, (lastRow + 1) * columns - 1)
        folderProxy.requestVisibleThumbs(first, last, thumbSizePx)
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_V && event.modifiers === Qt.NoModifier) {
            root.viewToggleRequested()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space &&
                (event.modifiers === Qt.NoModifier ||
                 event.modifiers === Qt.ShiftModifier)) {
            if (root.host && root.currentIndex >= 0)
                root.host.promoteInlineFolderRow(root.currentIndex)
            event.accepted = true
            return
        }
        var stride = root.gridMode ? root.columns : 1
        if (event.key === Qt.Key_J) {
            root.moveCurrent(stride)
            event.accepted = true
        } else if (event.key === Qt.Key_K) {
            root.moveCurrent(-stride)
            event.accepted = true
        } else if (event.key === Qt.Key_L) {
            root.moveCurrent(1)
            event.accepted = true
        } else if (event.key === Qt.Key_H) {
            root.moveCurrent(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up ||
                   event.key === Qt.Key_Right || event.key === Qt.Key_Left) {
            event.accepted = true
        } else if (event.key === Qt.Key_Return ||
                   event.key === Qt.Key_Enter ||
                   event.key === Qt.Key_O) {
            root.commitRow(root.currentIndex)
            event.accepted = true
        }
    }

    onVisibleChanged: if (visible) thumbTimer.restart()
    onWidthChanged: thumbTimer.restart()
    onHeightChanged: thumbTimer.restart()
    onFolderModelChanged: thumbTimer.restart()
    onFolderProxyChanged: {
        currentIndex = folderProxy ? folderProxy.currentIndex : -1
        thumbTimer.restart()
    }
    onGridModeChanged: thumbTimer.restart()
    onLoadingChanged: if (!loading) thumbTimer.restart()
    onCountChanged: {
        if (currentIndex >= count)
            currentIndex = count - 1
        thumbTimer.restart()
    }

    Connections {
        target: root.folderProxy
        function onCurrentIndexChanged() {
            if (!root.folderProxy)
                return
            root.currentIndex = root.folderProxy.currentIndex
            Qt.callLater(root.positionCurrent)
        }
    }

    Timer {
        id: thumbTimer
        interval: 16
        repeat: false
        onTriggered: root.syncThumbs()
    }

    Item {
        id: viewport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top

        Loader {
            id: viewLoader
            anchors.fill: parent
            sourceComponent: root.gridMode ? gridComponent : listComponent
            onLoaded: {
                thumbTimer.restart()
                Qt.callLater(root.positionCurrent)
            }
        }

        Text {
            anchors.centerIn: parent
            visible: !root.queryBusy && !root.loading && !root.errorText.length &&
                     root.folderModel && !root.folderModel.listing &&
                     root.count === 0
            text: "Empty folder"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
        }

        Text {
            anchors.centerIn: parent
            visible: !root.queryBusy && (root.loading || (root.folderModel &&
                     root.folderModel.listing && root.count === 0)
                     )
            text: root.loading ? "Querying folder…" : "Reading folder…"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
        }

        Text {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - Theme.spaceXL * 2)
            visible: !root.queryBusy && root.errorText.length > 0
            text: root.errorText
            color: Theme.urgent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBodySmall
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        QueryBusy {
            objectName: "folderQueryBusy"
            anchors.fill: parent
            running: root.queryBusy
            label: "Querying this folder…"
            z: 4
        }
    }

    Component {
        id: gridComponent

        GridView {
            id: grid
            objectName: "millerGridView"
            model: root.folderProxy
            cellWidth: root.cellWidth
            cellHeight: root.cellHeight
            reuseItems: true
            cacheBuffer: cellHeight * 4
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            currentIndex: root.currentIndex
            highlight: null
            onContentYChanged: thumbTimer.restart()

            delegate: Item {
                id: tile
                required property int index
                required property string name
                required property string path
                required property bool isDir
                required property string thumbnail
                required property bool thumbnailPending

                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool selected: root.currentIndex === index

                Rectangle {
                    id: frame
                    anchors.fill: parent
                    anchors.margins: Theme.spaceSM
                    color: tile.selected ? Theme.selectedFill
                                         : (hover.hovered ? Theme.hoverFill
                                                          : "transparent")
                    border.color: tile.selected ? Theme.accent
                                                : (hover.hovered
                                                   ? Theme.hoverBorder
                                                   : "transparent")
                    border.width: tile.selected ? 2 : 1
                    radius: Theme.radius
                }

                Item {
                    id: art
                    anchors.top: frame.top
                    anchors.left: frame.left
                    anchors.right: frame.right
                    anchors.bottom: label.top
                    anchors.margins: Theme.spaceMD

                    Image {
                        id: thumb
                        anchors.fill: parent
                        source: tile.thumbnail
                        visible: tile.thumbnail.length > 0 &&
                                 status !== Image.Error
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        sourceSize.width: Math.max(1, art.width)
                        sourceSize.height: Math.max(1, art.height)
                    }

                    FolderMark {
                        anchors.centerIn: parent
                        width: Math.min(parent.width, parent.height) * 0.62
                        height: width * 0.72
                        visible: tile.thumbnail.length === 0 && tile.isDir
                        opacity: tile.thumbnailPending ? 0.2 : 0.72
                    }

                    FileMark {
                        anchors.centerIn: parent
                        width: Math.min(parent.width, parent.height) * 0.46
                        height: width * 1.2
                        suffix: {
                            var dot = tile.name.lastIndexOf(".")
                            return dot > 0 ? tile.name.substring(dot + 1) : ""
                        }
                        visible: tile.thumbnail.length === 0 && !tile.isDir
                        opacity: tile.thumbnailPending ? 0.2 : 0.72
                    }

                    ThumbLoadingGlyph {
                        anchors.fill: parent
                        running: tile.thumbnailPending ||
                                 (tile.thumbnail.length > 0 &&
                                  thumb.status === Image.Loading)
                    }
                }

                Text {
                    id: label
                    anchors.left: frame.left
                    anchors.right: frame.right
                    anchors.bottom: frame.bottom
                    anchors.leftMargin: Theme.spaceMD
                    anchors.rightMargin: Theme.spaceMD
                    anchors.bottomMargin: Theme.spaceMD
                    text: tile.name
                    color: tile.selected ? Theme.brightForeground
                                         : Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontCaption
                    font.bold: tile.selected
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                }

                HoverHandler { id: hover }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectRow(tile.index)
                    onDoubleClicked: root.commitRow(tile.index)
                }
            }

            ScrollChrome { flick: grid }
        }
    }

    Component {
        id: listComponent

        FileList {
            objectName: "millerListView"
            fileModel: root.folderModel
            filterProxy: root.folderProxy
            host: root.host
            dndEnabled: false
            externalActivation: true
            onRowActivated: function(row) { root.commitRow(row) }
            onViewToggleRequested: root.viewToggleRequested()
        }
    }

    Item {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.space(22)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: root.activeFocus ? Theme.accent : Theme.normalBorder
            opacity: root.activeFocus ? 0.68 : 0.45
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spaceMD
            anchors.verticalCenter: parent.verticalCenter
            text: root.count + (root.truncated ? "+" : "") +
                  (root.count === 1 && !root.truncated ? " item" : " items")
            color: root.activeFocus ? Theme.foreground : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceMD
            anchors.verticalCenter: parent.verticalCenter
            text: root.activeFocus ? "V view  ·  Enter open"
                                   : "click to browse"
            color: root.activeFocus ? Theme.accent : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
        }
    }
}
