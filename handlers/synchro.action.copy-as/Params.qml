import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    property int format: 0
    readonly property var formats: [
        {
            "id": "path",
            "name": "Absolute path",
            "hint": "Filesystem path, one per line."
        },
        {
            "id": "uri",
            "name": "file:// URI",
            "hint": "URI form of each path."
        },
        {
            "id": "name",
            "name": "Name only",
            "hint": "Basename of each file."
        }
    ]

    function itemPath(item) {
        if (!item)
            return ""
        if (item.path)
            return item.path
        if (item.uri)
            return String(item.uri).replace(/^file:\/\//, "")
        return ""
    }

    function formatItem(item) {
        var path = root.itemPath(item)
        if (!path)
            return ""
        var kind = root.formats[root.format].id
        if (kind === "uri") {
            if (item.uri)
                return String(item.uri)
            return "file://" + path
        }
        if (kind === "name") {
            var parts = path.split("/")
            return parts[parts.length - 1]
        }
        return path
    }

    function previewText() {
        var items = root.selection && root.selection.length ? root.selection : []
        if (items.length === 0 && root.file)
            items = [{ "uri": root.file, "path": String(root.file).replace(/^file:\/\//, "") }]
        var lines = []
        for (var i = 0; i < items.length; ++i) {
            var line = root.formatItem(items[i])
            if (line)
                lines.push(line)
        }
        return lines.join("\n")
    }

    function commit() {
        if (!root.host)
            return false
        var text = root.previewText()
        if (!text)
            return false
        return root.host.copyText(text)
    }

    function actionKey(key, modifiers) {
        if (key === Qt.Key_J || key === Qt.Key_Down || key === Qt.Key_S) {
            root.format = Math.min(root.format + 1, root.formats.length - 1)
            return true
        }
        if (key === Qt.Key_K || key === Qt.Key_Up || key === Qt.Key_W) {
            root.format = Math.max(root.format - 1, 0)
            return true
        }
        if (key === Qt.Key_Return || key === Qt.Key_Enter)
            return root.commit()
        return false
    }

    Column {
        id: col
        anchors.fill: parent
        spacing: Theme.space(6)

        Repeater {
            model: root.formats
            delegate: Item {
                required property int index
                required property var modelData
                width: col.width
                height: Math.max(Theme.fontBody + Theme.space(8), 22)

                Rectangle {
                    anchors.fill: parent
                    visible: root.format === index
                    color: Theme.selectedFill
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space(8)
                    anchors.rightMargin: Theme.space(8)
                    verticalAlignment: Text.AlignVCenter
                    text: (root.format === index ? "●  " : "○  ") + modelData.name
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.format = index
                        if (root.host)
                            root.host.doParamsFocused = true
                    }
                    onDoubleClicked: {
                        root.format = index
                        root.commit()
                    }
                }
            }
        }

        Text {
            width: parent.width
            text: root.formats[root.format].hint
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        Text {
            objectName: "copyAsPreview"
            width: parent.width
            text: root.previewText()
            color: Theme.foreground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WrapAnywhere
            elide: Text.ElideMiddle
            maximumLineCount: 6
        }
    }
}
