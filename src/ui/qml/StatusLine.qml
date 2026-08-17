import QtQuick
import Synchro.Theme

Item {
    id: root

    required property var fileModel
    property var keyMachine
    property var selection
    property var filterProxy
    property var host: null
    property string extra: ""

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
            if (root.keyMachine && root.keyMachine.mode === "field-search")
                bits.push("SEARCH")
            else if (root.fileModel && root.fileModel.isSearch)
                bits.push("RESULTS")
            if (root.extra)
                bits.push(root.extra)
            if (root.selection && root.selection.statusText)
                bits.push(root.selection.statusText)
            else if (root.fileModel && root.fileModel.count !== undefined)
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
            if (root.fileModel && root.fileModel.showHidden)
                bits.push("hidden")
            if (root.filterProxy && root.filterProxy.sortRoleName) {
                var role = root.filterProxy.sortRoleName
                var desc = root.filterProxy.sortOrder === "desc"
                if (role !== "name" || desc)
                    bits.push(role + (desc ? "↓" : "↑"))
            }
            if (root.fileModel && root.fileModel.path)
                bits.push(root.fileModel.path)
            if (root.keyMachine && root.keyMachine.statusMessage)
                bits.push(root.keyMachine.statusMessage)
            if (root.host && root.host.actionOpen && root.host.doHint)
                bits.push(root.host.doHint)
            else if (root.host && root.keyMachine && root.keyMachine.listFocused &&
                     !root.keyMachine.peekOpen && root.host.listHint)
                bits.push(root.host.listHint)
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
