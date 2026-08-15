import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    // Modal picker: this overlay owns j/k/Enter/Esc while open.
    focus: true

    ListView {
        id: choices
        objectName: "openWithList"
        anchors.fill: parent
        clip: true
        focus: true
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
                    if (root.host && row.modelData && row.modelData.id)
                        root.host.runOpen(row.modelData.id)
                }
            }
        }

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_J || event.key === Qt.Key_Down) {
                if (count > 0)
                    currentIndex = Math.min(currentIndex + 1, count - 1)
                event.accepted = true
            } else if (event.key === Qt.Key_K || event.key === Qt.Key_Up) {
                if (count > 0)
                    currentIndex = Math.max(currentIndex - 1, 0)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (currentIndex >= 0 && currentIndex < model.length &&
                        root.host && model[currentIndex] && model[currentIndex].id)
                    root.host.runOpen(model[currentIndex].id)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                if (root.host)
                    root.host.closeAction()
                event.accepted = true
            }
        }

        Component.onCompleted: forceActiveFocus()
    }

    Text {
        visible: choices.count === 0
        anchors.centerIn: parent
        text: "No open handlers"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape && root.host) {
            root.host.closeAction()
            event.accepted = true
        }
    }
}
