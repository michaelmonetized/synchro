import QtQuick
import QtQuick.Pdf
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    PdfDocument {
        id: doc
        source: root.file
    }

    PdfMultiPageView {
        anchors.fill: parent
        document: doc
    }

    Text {
        visible: doc.status === PdfDocument.Error
        anchors.centerIn: parent
        text: "Could not open PDF"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Keys.onEscapePressed: if (root.host) root.host.close()
}
