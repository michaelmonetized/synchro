import QtQuick
import Synchro.Theme

// Terminal dock panel. The actual terminal lives in TermSurface.qml behind a
// Loader so a missing qmltermwidget degrades to an install hint instead of
// breaking the dock.
Item {
    id: panel

    property var host: null
    property var fileModel: null
    property var navStack: null

    function focusContent() {
        if (surface.status === Loader.Ready && surface.item)
            surface.item.focusContent()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.opaqueBackground
    }

    Loader {
        id: surface
        anchors.fill: parent
        source: "TermSurface.qml"
        onLoaded: {
            item.host = Qt.binding(function () { return panel.host })
            item.fileModel = Qt.binding(function () { return panel.fileModel })
            item.navStack = Qt.binding(function () { return panel.navStack })
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.space(6)
        visible: surface.status === Loader.Error

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "terminal needs qmltermwidget"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "sudo pacman -S qmltermwidget   then reopen with :term"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }
    }
}
