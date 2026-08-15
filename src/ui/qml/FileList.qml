import QtQuick
import Synchro.Theme

ListView {
    id: list

    required property var fileModel
    property var navStack

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
    cacheBuffer: Math.max(0, Theme.fontBody + Theme.space(8)) * 8

    highlight: Rectangle {
        color: Theme.selectedFill
    }

    delegate: Item {
        id: row

        required property int index
        required property string name
        required property bool isDir
        required property bool isSymlink

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

        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.space(8)
            anchors.rightMargin: Theme.space(8)
            verticalAlignment: Text.AlignVCenter
            text: (row.isDir ? "▸ " : row.isSymlink ? "↗ " : "  ") + row.name
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: list.fileModel.currentIndex = row.index
            onDoubleClicked: list.fileModel.activateCurrent()
        }
    }

    Keys.onPressed: function (event) {
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
            list.fileModel.moveCursor(1)
            event.accepted = true
        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            if (alt || chord)
                return
            list.fileModel.moveCursor(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (alt || chord)
                return
            list.fileModel.activateCurrent()
            event.accepted = true
        } else if (event.key === Qt.Key_L && !alt && !chord && !shift) {
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
        }
    }

    Connections {
        target: list.fileModel
        function onCurrentIndexChanged() {
            if (list.fileModel.currentIndex >= 0)
                list.positionViewAtIndex(list.fileModel.currentIndex, ListView.Contain)
        }
    }
}
