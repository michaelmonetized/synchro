import QtQuick
import Synchro.Theme

Window {
    id: root

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: "Synchro"
    color: Theme.background

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        radius: Theme.radius
        border.width: 1
        border.color: Theme.normalBorder

        Text {
            anchors.centerIn: parent
            text: "Synchro"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }
}
