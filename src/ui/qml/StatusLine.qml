import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var fileModel
    property var keyMachine

    implicitHeight: Math.max(Theme.fontBody + Theme.space(8), 22)

    function fmtSize(n) {
        if (n === undefined || n === null || n < 0)
            return "—"
        if (n < 1024)
            return n + "B"
        if (n < 1024 * 1024)
            return (n / 1024).toFixed(1) + "K"
        if (n < 1024 * 1024 * 1024)
            return (n / (1024 * 1024)).toFixed(1) + "M"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + "G"
    }

    function fmtTime(ms) {
        if (!ms)
            return "—"
        var d = new Date(ms)
        if (isNaN(d.getTime()))
            return "—"
        return d.toLocaleString()
    }

    readonly property var stat: fileModel && fileModel.currentStat ? fileModel.currentStat : ({})

    Text {
        objectName: "statusLineText"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.space(8)
        anchors.rightMargin: Theme.space(8)
        anchors.verticalCenter: parent.verticalCenter
        text: {
            var bits = []
            if (root.fileModel && root.fileModel.count !== undefined)
                bits.push(root.fileModel.count + " files")
            var st = root.stat
            if (st && st.name)
                bits.push(st.name)
            if (st && st.size !== undefined)
                bits.push(root.fmtSize(st.size))
            if (st && st.mtime)
                bits.push(root.fmtTime(st.mtime))
            if (st && st.perm)
                bits.push(st.perm)
            if (root.fileModel && root.fileModel.path)
                bits.push(root.fileModel.path)
            if (root.keyMachine && root.keyMachine.statusMessage)
                bits.push(root.keyMachine.statusMessage)
            return bits.join("   ")
        }
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        elide: Text.ElideMiddle
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.normalBorder
    }
}
