import QtQuick
import Synchro.Theme

Item {
    id: cell

    required property var listing
    required property int rowIndex
    required property string name
    required property bool isDir
    required property bool isSymlink
    required property string thumbnail
    required property bool thumbnailPending
    required property string path
    required property string detail
    required property var used
    required property var total
    required property int percent
    property bool liveFromModel: false

    objectName: "gridCell"
    width: listing.cellWidth
    height: listing.cellHeight

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        z: -1
    }

    function refreshFromModel() {
        if (!liveFromModel || !listing || !listing.rowAt)
            return
        var rec = listing.rowAt(rowIndex)
        name = rec && rec.name ? rec.name : ""
        isDir = !!(rec && rec.isDir)
        isSymlink = !!(rec && rec.isSymlink)
        thumbnail = rec && rec.thumbnail ? rec.thumbnail : ""
        thumbnailPending = !!(rec && rec.thumbnailPending)
        path = rec && rec.path ? rec.path : ""
        detail = rec && rec.detail ? rec.detail : ""
        used = rec && rec.used !== undefined ? rec.used : -1
        total = rec && rec.total !== undefined ? rec.total : -1
        percent = rec && rec.percent !== undefined ? rec.percent : -1
    }

    onRowIndexChanged: if (liveFromModel)
        refreshFromModel()
    Component.onCompleted: if (liveFromModel)
        refreshFromModel()

    Connections {
        target: liveFromModel && listing ? listing.rows : null
        function onRowsInserted(parent, first, last) {
            if (cell.rowIndex >= first)
                cell.refreshFromModel()
        }
        function onRowsRemoved(parent, first, last) {
            if (cell.rowIndex >= first)
                cell.refreshFromModel()
        }
        function onDataChanged(tl, br, roles) {
            if (!tl || !br)
                return
            if (cell.rowIndex >= tl.row && cell.rowIndex <= br.row)
                cell.refreshFromModel()
        }
        function onModelReset() {
            cell.refreshFromModel()
        }
    }

    Drag.dragType: Drag.Automatic
    Drag.active: dragArea.drag.active && listing.dndLive
    Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
    Drag.proposedAction: listing.outboundDragAction
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    readonly property bool isCurrent: listing.rows &&
                                      listing.rows.currentIndex === cell.rowIndex
    readonly property bool picked: listing.selection &&
                                   listing.selectionEpoch >= 0 &&
                                   listing.selection.isSelected(cell.rowIndex)
    Rectangle {
        id: card
        anchors.fill: parent
        anchors.margins: Theme.spaceMD
        color: "transparent"
        border.color: listing.showCursorChrome && cell.picked
                      ? Theme.accent
                      : (hover.hovered ? Theme.alpha(Theme.hoverBorder, 0.55)
                                       : "transparent")
        border.width: listing.showCursorChrome && cell.picked ? 2 : 1
        radius: Theme.radius
    }

    Rectangle {
        objectName: "gridSelectionFill"
        anchors.fill: card
        anchors.margins: card.border.width
        visible: listing.showCursorChrome && cell.picked
        color: Theme.selectedFill
        opacity: cell.isCurrent ? (listing.cursorDim ? 0.45 : 1) : 0.45
        radius: Theme.radius
    }

    Rectangle {
        anchors.fill: card
        visible: folderDrop.hot
        color: "transparent"
        border.color: Theme.accent
        border.width: 1
        radius: Theme.radius
    }

    FileDropSurface {
        id: folderDrop
        objectName: "folderDrop"
        anchors.fill: parent
        fileOps: listing.fileOps
        destPath: cell.isDir ? cell.path : ""
        dropEnabled: listing.dndLive && cell.isDir
    }

    HoverHandler {
        id: hover
    }

    Item {
        id: preview
        anchors.top: card.top
        anchors.left: card.left
        anchors.right: card.right
        anchors.margins: Theme.spaceLG
        height: Math.min(listing.cellInner, width)

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "transparent"
            radius: Theme.radius
        }

        Image {
            id: thumbImage
            anchors.fill: parent
            anchors.margins: Theme.spaceXS
            visible: cell.thumbnail.length > 0
            source: cell.thumbnail
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
            sourceSize.width: listing.cellInner
            sourceSize.height: listing.cellInner
            opacity: 1
        }

        Loader {
            anchors.fill: parent
            anchors.margins: Theme.spaceLG
            active: cell.thumbnail.length === 0
            opacity: cell.thumbnailPending ? 0.2 : 1
            sourceComponent: cell.isDir ? folderMarkComp : fileMarkComp
        }

        ThumbLoadingGlyph {
            anchors.fill: parent
            running: cell.thumbnailPending ||
                     (cell.thumbnail.length > 0 &&
                      thumbImage.status === Image.Loading)
        }

        Component {
            id: folderMarkComp
            FolderMark {}
        }

        Component {
            id: fileMarkComp
            Item {
                FileMark {
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 0.72)
                    height: Math.round(parent.height * 0.84)
                    suffix: {
                        var n = cell.name
                        var i = n.lastIndexOf(".")
                        if (i <= 0 || i === n.length - 1)
                            return ""
                        return n.slice(i + 1).toUpperCase()
                    }
                }
            }
        }

        ListingChrome {
            objectName: "listingThumbChrome"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.space(6)
            host: listing.host
            mode: "thumb"
            file: cell.path ? Qt.resolvedUrl("file://" + cell.path) : ""
            name: cell.name
            path: cell.path
            detail: cell.detail
            used: cell.used
            total: cell.total
            percent: cell.percent
        }

    }

    Text {
        id: nameLabel
        anchors.top: preview.bottom
        anchors.left: card.left
        anchors.right: card.right
        anchors.topMargin: Theme.spaceSM
        anchors.leftMargin: Theme.spaceLG
        anchors.rightMargin: Theme.spaceLG
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: cell.name
        color: cell.picked ? Theme.brightForeground : Theme.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.bold: cell.picked
        elide: Text.ElideMiddle
    }

    Text {
        anchors.top: nameLabel.bottom
        anchors.left: card.left
        anchors.right: card.right
        anchors.bottom: card.bottom
        anchors.leftMargin: Theme.spaceLG
        anchors.rightMargin: Theme.spaceLG
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readonly property string gitText: {
            var lane = (typeof gitLane !== "undefined") ? gitLane : null
            if (!cell.isDir || !lane)
                return ""
            var rev = lane.revision
            return rev >= 0 ? lane.mark(cell.path) : ""
        }
        text: gitText.length ? gitText : cell.detail
        color: gitText.length && gitText !== "?0 ~0" && gitText !== "?0 ~0 ↑0 ↓0"
               ? Theme.accent : Theme.darkForeground
        font.family: gitText.length ? Theme.monoFontFamily : Theme.fontFamily
        font.pixelSize: Theme.fontCaption
        elide: Text.ElideRight
        opacity: gitText.length || hover.hovered || cell.picked ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 90 }
        }
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
        drag.target: listing.dndLive ? dragProxy : null
        drag.threshold: Math.max(8, Qt.styleHints.startDragDistance)
        preventStealing: drag.active

        onPressed: function (mouse) {
            didDrag = false
            if (mouse.button !== Qt.LeftButton || !listing.dndLive)
                return
            listing.armDrag(cell, cell.rowIndex, cell.path, cell.name,
                            cell.thumbnail, cell.isDir)
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
            listing.forceActiveFocus()
            if (mouse.button === Qt.MiddleButton) {
                listing.viewToggleRequested()
                return
            }
            if (mouse.button === Qt.RightButton) {
                if (listing.keyMachine && listing.keyMachine.mode === "field-search")
                    listing.keyMachine.focusList()
                if (listing.selection) {
                    if (!listing.selection.isSelected(cell.rowIndex))
                        listing.selection.click(cell.rowIndex)
                } else if (listing.filterProxy) {
                    listing.filterProxy.selectRow(cell.rowIndex)
                } else {
                    listing.fileModel.currentIndex = cell.rowIndex
                }
                var point = dragArea.mapToItem(null, mouse.x, mouse.y)
                listing.doRequested(point.x, point.y)
                return
            }
            if (listing.keyMachine && listing.keyMachine.mode === "field-search")
                listing.keyMachine.focusList()
            if (listing.selection) {
                if (mouse.modifiers & Qt.ControlModifier)
                    listing.selection.ctrlClick(cell.rowIndex)
                else if (mouse.modifiers & Qt.ShiftModifier)
                    listing.selection.shiftClick(cell.rowIndex)
                else
                    listing.selection.leftClick(cell.rowIndex)
                return
            }
            if (listing.filterProxy)
                listing.filterProxy.selectRow(cell.rowIndex)
            else
                listing.fileModel.currentIndex = cell.rowIndex
        }
        onDoubleClicked: {
            if (listing.filterProxy)
                listing.filterProxy.activateCurrent()
            else
                listing.fileModel.activateCurrent()
        }
    }
}
