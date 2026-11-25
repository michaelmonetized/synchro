import QtQuick
import Synchro.Theme 1.0

// Grid-thumb decorate for volumes://.
Item {
    id: root

    property var host
    property url file
    property string name: ""
    property string path: ""
    property string detail: ""
    property real used: -1
    property real total: -1
    property int percent: -1

    visible: root.percent >= 0 && root.total > 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.space(5)
        color: Theme.normalBorder
        opacity: 0.55
        radius: 1

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.round(parent.width *
                              Math.max(0, Math.min(100, root.percent)) / 100)
            color: root.percent >= 90 ? Theme.urgent : Theme.accent
            radius: 1
        }
    }
}
