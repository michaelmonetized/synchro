import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

// Data-browser dock panel. Its target exists only while one compatible file
// is explicitly selected; Main closes the contextual surface when relevance
// disappears, while this reset prevents a kept-alive instance going stale.
Item {
    id: panel

    property var host: null
    property var fileModel: null
    property var selectionModel: null
    property var navStack: null

    property string targetPath: ""
    property string engine: "duckdb"

    function focusContent() {
        panel.forceActiveFocus()
    }

    function engineFor(path) {
        var p = path.toLowerCase()
        if (/\.(duckdb|ddb)$/.test(p))
            return "duckdb"
        if (/\.(parquet|pq|csv|tsv)$/.test(p))
            return "duckfile"
        return ""
    }

    function maybeRetarget() {
        if (!panel.fileModel || (panel.selectionModel &&
                                 panel.selectionModel.selectedCount !== 1)) {
            panel.targetPath = ""
            return
        }
        var st = panel.fileModel.currentStat
        if (!st || !st.path) {
            panel.targetPath = ""
            return
        }
        var e = panel.engineFor(st.path)
        if (!e) {
            panel.targetPath = ""
            return
        }
        if (st.path === panel.targetPath)
            return
        panel.engine = e
        panel.targetPath = st.path
    }

    Connections {
        target: panel.fileModel
        function onCurrentStatChanged() { panel.maybeRetarget() }
    }

    Connections {
        target: panel.selectionModel
        function onSelectionChanged() { panel.maybeRetarget() }
    }

    onFileModelChanged: maybeRetarget()
    onSelectionModelChanged: maybeRetarget()
    Component.onCompleted: maybeRetarget()

    Rectangle {
        anchors.fill: parent
        color: Theme.opaqueBackground
    }

    // Target chip: what the workbench is looking at right now.
    Item {
        id: chipRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.fontBody + Theme.space(12)

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space(10)
            anchors.verticalCenter: parent.verticalCenter
            text: panel.targetPath.length
                  ? "▸ " + panel.targetPath.split("/").pop()
                  : "select a .duckdb / .parquet / .csv file"
            color: panel.targetPath.length ? Theme.accent : Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
            width: Math.min(implicitWidth, parent.width * 0.7)
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space(10)
            anchors.verticalCenter: parent.verticalCenter
            visible: panel.targetPath.length > 0
            text: panel.engine === "duckfile" ? "duckdb · file scan"
                                              : "duckdb"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.normalBorder
        }
    }

    DatabasePeek {
        objectName: "dataBrowserPeek"
        anchors.top: chipRow.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: panel.targetPath.length > 0
        host: panel.host
        file: panel.targetPath.length ? "file://" + panel.targetPath : ""
        engine: panel.engine
    }
}
