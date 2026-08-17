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
        anchors.margins: Theme.space(12)
        contentWidth: body.implicitWidth
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Text {
            id: body
            width: Math.max(flick.width, implicitWidth)
            text: {
                if (!root.preview || root.preview.ok === undefined)
                    return ""
                if (!root.preview.ok)
                    return root.preview.error === "binary"
                           ? "(binary file)"
                           : (root.preview.error || "unreadable")
                if (root.preview.highlighted && root.preview.html)
                    return root.preview.html + (root.preview.truncated ? "\n…" : "")
                return root.preview.text + (root.preview.truncated ? "\n…" : "")
            }
            textFormat: (root.preview && root.preview.highlighted)
                        ? Text.RichText : Text.PlainText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.NoWrap
        }
    }

    Keys.onEscapePressed: if (root.host) root.host.close()
}
