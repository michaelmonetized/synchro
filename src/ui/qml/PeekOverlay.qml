import QtQuick
import Synchro.Theme

Item {
    id: root

    property var host: null
    property var keys: null
    objectName: "peekOverlay"
    visible: host && host.open
    z: 100
    // List keeps focus so KeyMachine owns Space / j / k / Esc.
    focus: false
    activeFocusOnTab: false

    readonly property bool folderPeek: root.host && root.host.folderPeek
    readonly property bool filePeekVisible: root.host && root.host.open &&
                                            (!root.host.folderPeek ||
                                             !root.host.folderListing)

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: root.folderPeek ? 0.62 : 0.86
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: Theme.space(root.folderPeek ? 36 : 24)
        color: Theme.background
        border.color: Theme.normalBorder
        border.width: 1

        Text {
            id: caption
            visible: root.host && root.host.open
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: fileNameCaption.visible ? fileNameCaption.left
                                                   : parent.right
            anchors.margins: Theme.space(8)
            anchors.rightMargin: fileNameCaption.visible ? Theme.space(12)
                                                         : Theme.space(8)
            text: {
                if (root.host && root.host.folderPath.length)
                    return "peek  " + root.host.folderPath
                return "peek"
            }
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        Text {
            id: fileNameCaption
            objectName: "peekFileName"
            visible: root.filePeekVisible && root.host &&
                     root.host.peekFileName.length > 0
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.space(8)
            text: root.host ? root.host.peekFileName : ""
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, parent.width * 0.42)
            horizontalAlignment: Text.AlignRight
        }

        // Stay mounted (and laid out) while a file peek is on top so the
        // folder ListView/GridView keep contentY and cached thumbs.
        Item {
            id: folderSurface
            objectName: "peekFolderSurface"
            z: 0
            anchors.top: caption.visible ? caption.bottom : parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space(8)
            visible: root.host && root.host.folderPeek
            opacity: root.host && root.host.folderListing ? 1 : 0
            enabled: root.host && root.host.folderListing
        }

        Item {
            id: fileIndex
            objectName: "peekFileIndex"
            z: 1
            visible: root.filePeekVisible && root.host && root.host.peekProxy
            readonly property bool grid: root.host && root.host.gridMode
            readonly property var rows: root.host ? root.host.peekProxy : null
            readonly property int thumbPx: 96
            anchors.top: caption.visible ? caption.bottom : parent.top
            anchors.left: parent.left
            anchors.bottom: root.filePeekVisible ? focusHint.top : parent.bottom
            anchors.margins: Theme.space(8)
            anchors.rightMargin: 0
            width: visible
                   ? Math.min(Theme.space(fileIndex.grid ? 200 : 168),
                              Math.max(Theme.space(fileIndex.grid ? 128 : 112),
                                       parent.width * (fileIndex.grid ? 0.26 : 0.22)))
                   : 0

            function activateAt(i) {
                if (fileIndex.grid && root.host && root.host.peekFileProxy)
                    root.host.peekFileProxy.currentIndex = i
                else if (fileIndex.rows)
                    fileIndex.rows.currentIndex = i
                if (root.host)
                    root.host.peekActivate()
            }

            function commitAt(i) {
                if (fileIndex.grid && root.host && root.host.peekFileProxy)
                    root.host.peekFileProxy.currentIndex = i
                else if (fileIndex.rows)
                    fileIndex.rows.currentIndex = i
                if (root.keys)
                    root.keys.handleListKey(Qt.Key_Return, Qt.NoModifier, "")
                else if (root.host)
                    root.host.commitPeek()
            }

            function syncThumbs() {
                if (!fileIndex.grid || !fileIndex.rows || !fileIndex.visible)
                    return
                var files = root.host ? root.host.peekFileProxy : null
                if (!files || !files.requestVisibleThumbs)
                    return
                var h = Math.max(1, indexGrid.itemH)
                var over = 2
                var first = Math.max(0, Math.floor(indexGrid.contentY / h) - over)
                var last = Math.min(indexGrid.count - 1,
                                    Math.ceil((indexGrid.contentY + indexGrid.height) / h)
                                    + over)
                files.requestVisibleThumbs(first, last, fileIndex.thumbPx)
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: fileIndex.visible && root.host &&
                              !root.host.peekPreviewFocused ? 1 : 0
                border.color: Theme.accent
                radius: Theme.radius
            }

            ListView {
                id: indexList
                objectName: "peekFileIndexList"
                anchors.fill: parent
                visible: !fileIndex.grid
                clip: true
                model: fileIndex.rows
                currentIndex: model ? model.currentIndex : -1
                keyNavigationEnabled: false
                highlightFollowsCurrentItem: true
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                focus: false
                spacing: 0

                onVisibleChanged: {
                    if (visible && currentIndex >= 0)
                        positionViewAtIndex(currentIndex, ListView.Center)
                }
                onCurrentIndexChanged: {
                    if (visible && currentIndex >= 0)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                }

                highlight: Rectangle { color: Theme.selectedFill }

                delegate: Item {
                    id: idxRow
                    required property int index
                    required property string name
                    required property bool isDir
                    width: indexList.width
                    height: Math.max(Theme.fontBody + Theme.space(6), 18)

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space(6)
                        anchors.rightMargin: Theme.space(6)
                        text: idxRow.name
                        color: idxRow.isDir ? Theme.muted : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: fileIndex.activateAt(idxRow.index)
                        onDoubleClicked: fileIndex.commitAt(idxRow.index)
                    }
                }
            }

            ListView {
                id: indexGrid
                objectName: "peekFileIndexGrid"
                anchors.fill: parent
                visible: fileIndex.grid
                clip: true
                model: root.host ? root.host.peekFileProxy : null
                currentIndex: model ? model.currentIndex : -1
                keyNavigationEnabled: false
                highlightFollowsCurrentItem: true
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                focus: false
                spacing: Theme.space(4)
                readonly property int itemH: Math.max(
                    Theme.space(72),
                    Math.round(width - Theme.space(12)) + Theme.fontBody
                    + Theme.space(10))

                onVisibleChanged: {
                    if (visible && currentIndex >= 0)
                        positionViewAtIndex(currentIndex, ListView.Center)
                    if (visible)
                        fileIndex.syncThumbs()
                }
                onCurrentIndexChanged: {
                    if (visible && currentIndex >= 0)
                        positionViewAtIndex(currentIndex, ListView.Contain)
                }
                onContentYChanged: indexThumbSync.restart()
                onHeightChanged: indexThumbSync.restart()
                onCountChanged: indexThumbSync.restart()
                onWidthChanged: indexThumbSync.restart()

                Timer {
                    id: indexThumbSync
                    interval: 16
                    repeat: false
                    onTriggered: fileIndex.syncThumbs()
                }

                highlight: Rectangle {
                    color: Theme.selectedFill
                    radius: Theme.radius
                }

                delegate: Item {
                    id: gRow
                    required property int index
                    required property string name
                    required property bool isDir
                    required property string thumbnail
                    width: indexGrid.width
                    height: indexGrid.itemH

                    Item {
                        id: gThumb
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.space(6)
                        height: width

                        Image {
                            anchors.fill: parent
                            visible: gRow.thumbnail.length > 0
                            source: gRow.thumbnail
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: fileIndex.thumbPx
                            sourceSize.height: fileIndex.thumbPx
                        }

                        FolderMark {
                            anchors.fill: parent
                            visible: gRow.thumbnail.length === 0 && gRow.isDir
                        }

                        FileMark {
                            anchors.centerIn: parent
                            width: Math.round(parent.width * 0.72)
                            height: Math.round(parent.height * 0.84)
                            visible: gRow.thumbnail.length === 0 && !gRow.isDir
                            suffix: {
                                var n = gRow.name
                                var i = n.lastIndexOf(".")
                                if (i <= 0 || i === n.length - 1)
                                    return ""
                                return n.slice(i + 1).toUpperCase()
                            }
                        }
                    }

                    Text {
                        anchors.top: gThumb.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.space(4)
                        anchors.rightMargin: Theme.space(4)
                        text: gRow.name
                        color: gRow.isDir ? Theme.muted : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: fileIndex.activateAt(gRow.index)
                        onDoubleClicked: fileIndex.commitAt(gRow.index)
                    }
                }
            }
        }

        Item {
            id: fileSurface
            objectName: "peekSurface"
            z: 1
            anchors.top: caption.visible ? caption.bottom : parent.top
            anchors.left: fileIndex.visible ? fileIndex.right : parent.left
            anchors.right: parent.right
            anchors.bottom: root.filePeekVisible ? focusHint.top : parent.bottom
            anchors.margins: Theme.space(8)
            anchors.leftMargin: fileIndex.visible ? Theme.space(6) : Theme.space(8)
            visible: root.filePeekVisible

            Rectangle {
                anchors.fill: parent
                z: 20
                visible: root.host && root.host.peekPreviewFocused
                color: "transparent"
                border.width: 1
                border.color: Theme.accent
                radius: Theme.radius
            }
        }

        Text {
            id: focusHint
            objectName: "peekFocusHint"
            visible: root.filePeekVisible
            anchors.left: fileSurface.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: fileIndex.visible ? Theme.space(6) : Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            anchors.bottomMargin: Theme.space(6)
            text: {
                if (!root.host)
                    return ""
                if (root.host.peekPreviewFocused)
                    return "file  ·  A index   W/S scroll   j/k next file"
                return "index  ·  D file   W/S next file"
            }
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
        }

        Text {
            z: 2
            visible: fileSurface.visible && root.host && !root.host.previewItem
            anchors.centerIn: fileSurface
            text: {
                if (root.host && root.host.peekFileName.length)
                    return "No preview  ·  " + root.host.peekFileName
                if (root.host && root.host.file)
                    return root.host.file.toString()
                return "No preview"
            }
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
            width: parent.width - Theme.space(24)
            horizontalAlignment: Text.AlignHCenter
        }
    }

    function reparentPreview() {
        if (!root.host)
            return
        var folder = root.host.folderListingItem
        if (folder) {
            folder.parent = folderSurface
            folder.anchors.fill = folderSurface
            folder.focus = false
            if (folder.syncStride)
                folder.syncStride()
        }
        var item = root.host.previewItem
        if (item && item !== folder) {
            item.parent = fileSurface
            item.anchors.fill = fileSurface
            item.focus = false
        }
    }

    Connections {
        target: root.host
        function onPreviewItemChanged() { root.reparentPreview() }
        function onFolderListingItemChanged() { root.reparentPreview() }
        function onOpenChanged() { if (root.host && root.host.open) root.reparentPreview() }
    }
}
