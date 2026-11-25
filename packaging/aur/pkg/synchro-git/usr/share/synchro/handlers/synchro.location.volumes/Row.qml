import QtQuick
import Synchro.Theme 1.0

// List-row decorate for volumes://. Host keeps keys.
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

    Row {
        anchors.fill: parent
        spacing: Theme.space(8)
        layoutDirection: Qt.RightToLeft

        Text {
            visible: root.detail.length > 0
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.width * 0.55)
            text: root.detail
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideLeft
            horizontalAlignment: Text.AlignRight
        }

        Item {
            visible: root.percent >= 0 && root.total > 0
            width: Theme.space(80)
            height: Theme.space(8)
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                anchors.fill: parent
                color: Theme.normalBorder
                opacity: 0.45
                radius: 1
            }

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
}
