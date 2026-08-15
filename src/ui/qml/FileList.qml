import QtQuick
import Synchro.Theme

ListView {
    id: list

    required property var fileModel
    property var filterProxy
    property var navStack
    property var keyMachine
    readonly property int thumbSizePx: 128

    signal viewToggleRequested()

    readonly property var rows: filterProxy ? filterProxy : fileModel

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
    }

    function syncThumbnails() {
        if (!list.fileModel || !list.visible)
            return
        if (list.count <= 0) {
            list.fileModel.requestVisibleThumbs(0, -1, list.thumbSizePx)
            return
        }
        var first = list.indexAt(1, list.contentY + 1)
        var last = list.indexAt(1, list.contentY + list.height - 2)
        if (first < 0 && last < 0) {
            thumbSync.interval = 50
            thumbSync.start()
            return
        }
        if (first < 0)
            first = 0
        if (last < 0)
            last = Math.min(list.count - 1, first + 40)
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

            Text {
                anchors.fill: parent
                visible: row.thumbnail.length === 0
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: row.isDir ? "▸" : row.isSymlink ? "↗" : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
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
            acceptedButtons: Qt.LeftButton
            onClicked: {
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
        } else if (event.key === Qt.Key_L && !alt && !chord && !shift) {
            if (list.filterProxy)
                list.filterProxy.activateCurrent()
            else
                list.fileModel.activateCurrent()
            event.accepted = true
        } else if ((event.key === Qt.Key_H || event.key === Qt.Key_Backspace) &&
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
        if (activeFocus && list.keyMachine && list.keyMachine.fieldFocused)
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
