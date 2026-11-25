import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    peekFlickable: findBody.flick
    implicitWidth: 720
    implicitHeight: 480
    objectName: "textPeekPreview"

    function peekKey(key, modifiers) {
        return findBody.peekKey(key, modifiers)
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
        asyncLoad: root.inlinePreview
    }

    Keys.onEscapePressed: if (!root.closeFind() && root.host)
        root.host.close()
}
