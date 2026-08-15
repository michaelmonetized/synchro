import QtQuick
import Synchro.Theme

ListView {
    id: list

    required property var fileModel

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
        if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
            list.fileModel.moveCursor(1)
            event.accepted = true
        } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
            list.fileModel.moveCursor(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            list.fileModel.activateCurrent()
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
