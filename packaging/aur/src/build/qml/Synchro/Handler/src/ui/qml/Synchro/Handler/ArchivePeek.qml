import QtQuick
import Synchro.Theme 1.0

// Member list for zip / tar / gzip peeks.
// Host: host.readArchive(file, maxEntries)
Item {
    id: root

    property var host: null
    property url file
    property int maxEntries: 200
    property var info: ({})

    readonly property bool ready: root.info && root.info.ok === true
    readonly property var entries: root.ready && root.info.entries ? root.info.entries : []
    readonly property int rowH: Math.max(Theme.fontBody + Theme.space(8), 22)
    property alias list: entryList

    function reload() {
        if (!root.host || !root.file || !root.host.readArchive)
            return
        root.info = root.host.readArchive(root.file, root.maxEntries)
    }

    function fmtSize(n) {
        var x = Number(n)
        if (!isFinite(x) || x < 0)
            return "—"
        if (x < 1024)
            return x + "B"
        if (x < 1024 * 1024)
            return (x / 1024).toFixed(1) + "K"
        if (x < 1024 * 1024 * 1024)
            return (x / (1024 * 1024)).toFixed(1) + "M"
        return (x / (1024 * 1024 * 1024)).toFixed(1) + "G"
    }

    onFileChanged: root.reload()
    Component.onCompleted: root.reload()

    Text {
        visible: root.info && root.info.ok === false
        anchors.centerIn: parent
        text: root.info && root.info.error ? root.info.error : "not an archive"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Column {
        visible: root.ready
        anchors.fill: parent
        anchors.margins: Theme.space(12)
        spacing: Theme.space(10)

        Flow {
            width: parent.width
            spacing: Theme.space(6)

            Repeater {
                model: root.ready ? [
                    root.info.format || "",
                    root.info.files ? (root.info.files + " files") : "",
                    root.info.dirs ? (root.info.dirs + " dirs") : "",
                    root.info.uncompressed ? root.fmtSize(root.info.uncompressed) : "",
                    root.info.method || "",
                    root.info.origName || "",
                    root.info.mtime || "",
                    root.info.truncated ? "more" : "",
                    root.info.sampleNote || ""
                ] : []

                delegate: Rectangle {
                    required property string modelData
                    visible: modelData && modelData.length > 0
                    implicitHeight: Theme.fontBody + Theme.space(8)
                    implicitWidth: chipLabel.implicitWidth + Theme.space(14)
                    color: "transparent"
                    border.color: Theme.normalBorder
                    border.width: 1
                    radius: Theme.radius

                    Text {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: modelData
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                    }
                }
            }
        }

        Text {
            visible: root.ready && root.entries.length === 0
            width: parent.width
            text: root.info.format === "gzip"
                  ? "single stream  ·  no member list"
                  : "no members"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }

        ListView {
            id: entryList
            objectName: "archiveList"
            width: parent.width
            height: parent.height - y
            clip: true
            visible: root.entries.length > 0
            boundsBehavior: Flickable.StopAtBounds
            model: root.entries
            spacing: 0

            delegate: Item {
                required property var modelData
                width: entryList.width
                height: root.rowH

                Rectangle {
                    anchors.fill: parent
                    color: Theme.hoverFill
                    visible: rowHover.hovered
                }
                HoverHandler { id: rowHover }

                Text {
                    id: kindMark
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.space(18)
                    text: modelData.kind === "link" ? "↗" : ""
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    anchors.left: kindMark.right
                    anchors.right: sizeLabel.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: Theme.space(8)
                    text: modelData.name || ""
                    color: modelData.kind === "dir" ? Theme.muted : Theme.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideMiddle
                    wrapMode: Text.NoWrap
                    maximumLineCount: 1
                }

                Text {
                    id: sizeLabel
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.kind === "dir" ? "" : root.fmtSize(modelData.size)
                    color: Theme.muted
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontBody
                }
            }
        }
    }
}
