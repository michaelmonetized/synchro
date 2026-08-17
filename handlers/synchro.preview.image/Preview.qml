import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    readonly property url raster: root.host && root.file.toString().length
                                  ? root.host.rasterUrl(root.file) : root.file

    Image {
        id: img
        anchors.fill: parent
        source: root.raster
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: false
    }

    Text {
        visible: img.status === Image.Error
        anchors.centerIn: parent
        text: "Could not open image"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Keys.onEscapePressed: if (root.host) root.host.close()
}
