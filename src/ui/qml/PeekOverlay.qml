import QtQuick
import Synchro.Theme

Item {
    id: root

    property var host: null
    objectName: "peekOverlay"
    visible: host && host.open
    z: 100

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.86
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: Theme.space(24)
        color: Theme.background
        border.color: Theme.normalBorder
        border.width: 1

        Item {
            id: surface
            objectName: "peekSurface"
            anchors.fill: parent
            anchors.margins: Theme.space(8)
        }

        Text {
            visible: !root.host || !root.host.previewItem
            anchors.centerIn: parent
            text: root.host && root.host.file ? root.host.file.toString() : "No preview"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
            width: parent.width - Theme.space(24)
            horizontalAlignment: Text.AlignHCenter
        }
    }

    function reparentPreview() {
        if (!root.host)
            return
        var item = root.host.previewItem
        if (!item)
            return
        item.parent = surface
        item.anchors.fill = surface
        if (item.forceActiveFocus)
            item.forceActiveFocus()
    }

    Connections {
        target: root.host
        function onPreviewItemChanged() { root.reparentPreview() }
        function onOpenChanged() { if (root.host && root.host.open) root.reparentPreview() }
    }
}
