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

    PathBar {
        id: pathBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        fileModel: directoryModel
        navStack: navStack
    }

    FileList {
        id: fileList
        anchors.top: pathBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        fileModel: directoryModel
        navStack: navStack
        Component.onCompleted: forceActiveFocus()
    }

    Connections {
        target: directoryModel
        function onPathChanged() {
            fileList.forceActiveFocus()
        }
    }
}
