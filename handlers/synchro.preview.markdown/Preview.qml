import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    peekFlickable: flick
    property var preview: ({})

    implicitWidth: 720
    implicitHeight: 480

    function reload() {
        if (!root.host || !root.file)
            return
        root.preview = root.host.readPreview(root.file, 65536)
    }

    onFileChanged: root.reload()
    Component.onCompleted: root.reload()

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: Theme.space(16)
        contentWidth: flick.width
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Text {
            id: body
            width: flick.width
            text: {
                if (!root.preview || root.preview.ok === undefined)
                    return ""
                if (root.preview.ok)
                    return root.preview.text
                return root.preview.error === "binary"
                       ? "(binary file)"
                       : (root.preview.error || "unreadable")
            }
            textFormat: Text.MarkdownText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }

    Keys.onEscapePressed: if (root.host) root.host.close()
}
