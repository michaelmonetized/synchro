import QtQuick
import Synchro.Handler 1.0

// Zip / tar / gzip peek. Lists members from the archive directory
// (zip central dir, ustar headers). Does not extract. A/D hop; W/S scroll.
HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480
    peekFlickable: browser.list

    ArchivePeek {
        id: browser
        objectName: "archivePeek"
        anchors.fill: parent
        host: root.host
        file: root.file
    }
}
