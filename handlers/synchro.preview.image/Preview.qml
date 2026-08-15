import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    Image {
        anchors.fill: parent
        source: root.file
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
    }

    Keys.onEscapePressed: if (root.host) root.host.close()
}
