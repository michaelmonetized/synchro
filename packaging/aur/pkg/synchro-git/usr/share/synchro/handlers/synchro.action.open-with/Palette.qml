import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    ListView {
        id: choices
        objectName: "openWithList"
        anchors.fill: parent
        clip: true
        focus: false
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        highlightFollowsCurrentItem: true
        highlightMoveDuration: 0
        model: root.host ? root.host.openCandidates : []

        highlight: Rectangle {
            color: Theme.selectedFill
        }

        delegate: Item {
            id: row
            required property int index
            required property var modelData
            width: ListView.view.width
            height: Math.max(Theme.fontBody + Theme.space(10), 24)

            Text {
                anchors.fill: parent
                anchors.leftMargin: Theme.space(8)
                anchors.rightMargin: Theme.space(8)
                verticalAlignment: Text.AlignVCenter
                text: row.modelData && row.modelData.name ? row.modelData.name : ""
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    choices.currentIndex = row.index
                    if (root.host)
                        root.host.doParamsFocused = true
                }
                onDoubleClicked: {
                    choices.currentIndex = row.index
                    root.commit()
                }
            }
        }
    }

    Text {
        visible: choices.count === 0
        anchors.centerIn: parent
        text: "No open handlers"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    function commit() {
        if (!root.host || choices.currentIndex < 0 ||
                choices.currentIndex >= choices.count)
            return false
        var row = choices.model[choices.currentIndex]
        if (!row || !row.id)
            return false
        return root.host.runOpen(row.id)
    }

    function actionKey(key, modifiers) {
        if (key === Qt.Key_J || key === Qt.Key_Down || key === Qt.Key_S) {
            if (choices.count > 0)
                choices.currentIndex = Math.min(choices.currentIndex + 1,
                                                choices.count - 1)
            return true
        }
        if (key === Qt.Key_K || key === Qt.Key_Up || key === Qt.Key_W) {
            if (choices.count > 0)
                choices.currentIndex = Math.max(choices.currentIndex - 1, 0)
            return true
        }
        if (key === Qt.Key_Return || key === Qt.Key_Enter)
            return root.commit()
        return false
    }
}
