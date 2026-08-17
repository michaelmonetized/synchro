import QtQuick
import Synchro.Theme

ListView {
    id: list

    required property var fileModel
    property var filterProxy
    property var navStack
    property var keyMachine
    property var selection
    readonly property int selectionEpoch: selection ? selection.epoch : 0
    readonly property int thumbSizePx: 128

    signal viewToggleRequested()
    signal doRequested()

    readonly property var rows: filterProxy ? filterProxy : fileModel
    readonly property bool showCursorChrome: !keyMachine || keyMachine.listFocused

    model: list.rows
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    highlightFollowsCurrentItem: true
    highlightMoveDuration: 0
    currentIndex: list.rows ? list.rows.currentIndex : -1
    focus: true
    activeFocusOnTab: true
    cacheBuffer: Math.max(0, Theme.fontBody + Theme.space(8)) * 8

    highlight: Rectangle {
        color: Theme.selectedFill
        visible: list.showCursorChrome
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
    Component.onCompleted: thumbSync.restart()

    delegate: Item {
        id: row

        required property int index
        required property string name
        required property bool isDir
        required property bool isSymlink
        required property string thumbnail

        width: ListView.view.width
        height: Math.max(Theme.fontBody + Theme.space(8), 20)

        readonly property bool picked: list.selection &&
                                       list.selectionEpoch >= 0 &&
                                       list.selection.isSelected(row.index)

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

        HoverHandler {
            id: hover
        }

        Item {
            id: iconBox
            width: Theme.fontBody
            height: Theme.fontBody
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter

            Image {
                anchors.fill: parent
                visible: row.thumbnail.length > 0
                source: row.thumbnail
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: iconBox.width
                sourceSize.height: iconBox.height
            }

            FolderMark {
                anchors.fill: parent
                visible: row.thumbnail.length === 0 && row.isDir
            }

            FileMark {
                anchors.fill: parent
                visible: row.thumbnail.length === 0 && !row.isDir
            }
        }

        Text {
            anchors.left: iconBox.right
            anchors.right: parent.right
            anchors.leftMargin: Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter
            text: row.name
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            onClicked: function (mouse) {
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
                    list.doRequested()
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
                if (list.filterProxy)
                    list.filterProxy.activateCurrent()
                else
                    list.fileModel.activateCurrent()
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
            if (list.filterProxy)
                list.filterProxy.activateCurrent()
            else
                list.fileModel.activateCurrent()
            event.accepted = true
        } else if ((event.key === Qt.Key_L || event.key === Qt.Key_Right) &&
                   !alt && !chord && !shift) {
            if (list.filterProxy)
                list.filterProxy.activateCurrent()
            else
                list.fileModel.activateCurrent()
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
            if (list.rows && list.rows.currentIndex >= 0)
                list.positionViewAtIndex(list.rows.currentIndex, ListView.Contain)
        }
    }
}
