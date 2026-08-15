import QtQuick
import Synchro.Theme

Window {
    id: root

    property bool gridMode: false

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
        visible: !root.gridMode
        enabled: visible
        fileModel: directoryModel
        onViewToggleRequested: root.gridMode = true
    }

    FileGrid {
        id: fileGrid
        anchors.fill: parent
        visible: root.gridMode
        enabled: visible
        fileModel: directoryModel
        onViewToggleRequested: root.gridMode = false
    }

    onGridModeChanged: {
        if (root.gridMode)
            fileGrid.forceActiveFocus()
        else
            fileList.forceActiveFocus()
    }

    Component.onCompleted: fileList.forceActiveFocus()
}
