import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    peekFlickable: findBody.findOpen ? findBody.flick : flick
    implicitWidth: 720
    implicitHeight: 480
    objectName: "markdownPeekPreview"

    function peekKey(key, modifiers) {
        var shift = modifiers & Qt.ShiftModifier
        if (key === Qt.Key_Slash && !shift)
            return findBody.openFind()
        if (findBody.findOpen)
            return findBody.peekKey(key, modifiers)
        return root.scrollFlickable(flick, key, modifiers)
    }

    function openFind() { return findBody.openFind() }
    function closeFind() { return findBody.closeFind() }
    function runFind() { findBody.runFind() }
    function jumpTo(i) { findBody.jumpTo(i) }

    readonly property bool findOpen: findBody.findOpen
    property alias findQuery: findBody.findQuery
    readonly property int findCount: findBody.findCount
    readonly property int findIndex: findBody.findIndex
    readonly property int viewStart: findBody.viewStart
    readonly property var preview: findBody.preview

    PeekFindBody {
        id: findBody
        anchors.fill: parent
        host: root.host
        file: root.file
        visible: findOpen
        onFileChanged: if (!findOpen)
            findBody.reload()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: Theme.space(16)
        visible: !findBody.findOpen
        contentWidth: flick.width
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Text {
            id: body
            width: flick.width
            text: {
                if (!findBody.preview || findBody.preview.ok === undefined)
                    return ""
                if (findBody.preview.ok)
                    return findBody.preview.text
                return findBody.preview.error === "binary"
                       ? "(binary file)"
                       : (findBody.preview.error || "unreadable")
            }
            textFormat: Text.MarkdownText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }

    Keys.onEscapePressed: if (!root.closeFind() && root.host)
        root.host.close()
}
