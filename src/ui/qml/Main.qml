import QtQuick
import Synchro.Theme

Window {
    id: root

    width: 960
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: directoryModel.path.length ? directoryModel.path : "Synchro"
    color: Theme.background

    FileList {
        id: fileList
        anchors.fill: parent
        fileModel: directoryModel
        Component.onCompleted: forceActiveFocus()
    }
}
