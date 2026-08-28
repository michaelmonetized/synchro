import QtQuick
import Synchro.Theme 1.0

// Reusable table/schema browser for peek handlers.
// Host: host.requestDatabase(file, engine, table, offset, limit)
// Keys (when the peek pane is file-focused): W/S tables or rows,
// Tab/E hop tables ↔ rows, Shift+W/S page rows.
Item {
    id: root

    property var host: null
    property url file
    property string engine: "sqlite"
    property int pageSize: 40

    property var info: ({})
    property double requestId: 0
    property string tableName: ""
    property int offset: 0
    property string inner: "tables"

    readonly property bool ready: root.info && root.info.ok === true
    readonly property var tables: root.ready && root.info.tables ? root.info.tables : []
    readonly property var columns: root.ready && root.info.columns ? root.info.columns : []
    readonly property var sample: root.ready && root.info.sample ? root.info.sample : []
    readonly property var names: {
        var out = []
        for (var i = 0; i < root.columns.length; ++i)
            out.push(root.columns[i].name)
        return out
    }
    readonly property int rowH: Math.max(Theme.fontBody + Theme.space(8), 22)
    readonly property var colWidths: {
        var out = []
        for (var i = 0; i < root.names.length; ++i)
            out.push(root.widthForName(root.names[i]))
        return out
    }
    readonly property int tableW: {
        var n = 0
        for (var i = 0; i < root.colWidths.length; ++i)
            n += root.colWidths[i]
        return n
    }
    readonly property int tableIndex: {
        for (var i = 0; i < root.tables.length; ++i) {
            if (root.tables[i].name === root.tableName)
                return i
        }
        return root.tables.length ? 0 : -1
    }

    function reload() {
        if (!root.host || !root.file || !root.host.requestDatabase)
            return
        root.info = ({ loading: true })
        root.requestId = root.host.requestDatabase(root.file, root.engine,
                                                   root.tableName, root.offset,
                                                   root.pageSize)
    }

    function selectTableAt(i) {
        if (i < 0 || i >= root.tables.length)
            return
        var name = root.tables[i].name
        if (name === root.tableName)
            return
        root.tableName = name
        root.offset = 0
        root.reload()
        if (tableList.currentIndex !== i)
            tableList.currentIndex = i
        tableList.positionViewAtIndex(i, ListView.Contain)
    }

    function cellText(row, name) {
        if (!row || name === undefined)
            return ""
        var v = row[name]
        if (v === undefined || v === null)
            return "·"
        return "" + v
    }

    function widthForName(name) {
        var n = (name || "").length
        var lower = (name || "").toLowerCase()
        if (lower === "text" || lower === "body" || lower === "content" ||
                lower === "message" || lower === "description")
            return Theme.space(240)
        return Math.max(Theme.space(88),
                        Math.min(Theme.space(168),
                                 Math.round(n * Theme.fontBody * 0.65) + Theme.space(18)))
    }

    function colWAt(i) {
        if (i < 0 || i >= root.colWidths.length)
            return Theme.space(120)
        return root.colWidths[i]
    }

    function scrollRows(key, modifiers) {
        var f = gridFlick
        if (!f.visible)
            return false
        var page = Math.max(Theme.space(48), Math.round(f.height * 0.85))
        var line = Math.max(Theme.space(24), root.rowH)
        var step = (modifiers & Qt.ShiftModifier) ? page : line
        if (key === Qt.Key_PageUp || key === Qt.Key_PageDown)
            step = page
        if (key === Qt.Key_W || key === Qt.Key_Up || key === Qt.Key_PageUp) {
            if (f.contentY <= 0 && root.offset > 0 &&
                    (key === Qt.Key_PageUp || (modifiers & Qt.ShiftModifier))) {
                root.offset = Math.max(0, root.offset - root.pageSize)
                root.reload()
                return true
            }
            f.contentY = Math.max(0, f.contentY - step)
            return true
        }
        if (key === Qt.Key_S || key === Qt.Key_Down || key === Qt.Key_PageDown) {
            var maxY = Math.max(0, f.contentHeight - f.height)
            if (f.contentY >= maxY - 1 && root.info && root.info.truncated &&
                    (key === Qt.Key_PageDown || (modifiers & Qt.ShiftModifier))) {
                root.offset = root.offset + root.pageSize
                root.reload()
                return true
            }
            f.contentY = Math.min(maxY, f.contentY + step)
            return true
        }
        if (key === Qt.Key_H || key === Qt.Key_Left) {
            f.contentX = Math.max(0, f.contentX - step)
            return true
        }
        if (key === Qt.Key_L || key === Qt.Key_Right || key === Qt.Key_D) {
            var maxX = Math.max(0, f.contentWidth - f.width)
            f.contentX = Math.min(maxX, f.contentX + step)
            return true
        }
        return false
    }

    function peekKey(key, modifiers) {
        if (key === Qt.Key_Tab) {
            root.inner = root.inner === "tables" ? "rows" : "tables"
            return true
        }
        if (key === Qt.Key_E) {
            root.inner = root.inner === "tables" ? "rows" : "tables"
            return true
        }
        if (root.inner === "tables") {
            if (key === Qt.Key_W || key === Qt.Key_Up) {
                root.selectTableAt(Math.max(0, root.tableIndex - 1))
                return true
            }
            if (key === Qt.Key_S || key === Qt.Key_Down) {
                root.selectTableAt(Math.min(root.tables.length - 1,
                                            root.tableIndex + 1))
                return true
            }
            if (key === Qt.Key_Return || key === Qt.Key_Enter ||
                    key === Qt.Key_L) {
                root.inner = "rows"
                return true
            }
            return false
        }
        return root.scrollRows(key, modifiers)
    }

    onFileChanged: {
        root.tableName = ""
        root.offset = 0
        root.inner = "tables"
        root.reload()
    }
    onEngineChanged: root.reload()
    Component.onCompleted: root.reload()

    Connections {
        target: root.host
        function onDatabaseReady(requestId, file, preview) {
            if (requestId !== root.requestId ||
                    file.toString() !== root.file.toString())
                return
            root.info = preview
            if (root.info && root.info.table)
                root.tableName = root.info.table
        }
    }

    Text {
        visible: root.info && root.info.loading === true
        anchors.centerIn: parent
        text: "reading " + root.engine + "…"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Text {
        visible: root.info && root.info.ok === false
        anchors.centerIn: parent
        text: root.info && root.info.error ? root.info.error : "unreadable"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Column {
        id: chrome
        visible: root.ready
        anchors.fill: parent
        anchors.margins: Theme.space(12)
        spacing: Theme.space(10)

        Flow {
            width: parent.width
            spacing: Theme.space(6)

            Repeater {
                model: root.ready ? [
                    root.engine,
                    root.tables.length + " tables",
                    root.tableName,
                    root.sample.length
                        ? ((root.offset + 1) + "–" + (root.offset + root.sample.length))
                        : "",
                    root.info.truncated ? "more" : "",
                    root.info.sampleNote ? root.info.sampleNote : ""
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

        Item {
            width: parent.width
            height: parent.height - y

            Item {
                id: tablePane
                objectName: "dbTableList"
                width: Math.min(Theme.space(220),
                                Math.max(Theme.space(140), parent.width * 0.28))
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: root.inner === "tables" ? Theme.accent
                                                          : Theme.normalBorder
                    border.width: 1
                    radius: Theme.radius
                }

                Text {
                    id: tableHead
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(8)
                    text: "tables"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                ListView {
                    id: tableList
                    anchors.top: tableHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.topMargin: Theme.space(4)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.tables
                    currentIndex: root.tableIndex
                    keyNavigationEnabled: false
                    highlightFollowsCurrentItem: true
                    highlightMoveDuration: 0

                    highlight: Rectangle {
                        objectName: "databaseTableSelection"
                        color: Theme.selectedFill
                        border.color: Theme.accent
                        border.width: 2
                        radius: Theme.radius
                    }

                    delegate: Item {
                        id: trow
                        required property var modelData
                        required property int index
                        width: tableList.width
                        height: root.rowH

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space(8)
                            anchors.rightMargin: Theme.space(8)
                            text: (trow.modelData.type === "view" ? "view  " : "") +
                                  (trow.modelData.name || "")
                            color: Theme.foreground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.inner = "tables"
                                root.selectTableAt(trow.index)
                            }
                        }
                    }
                }
            }

            Item {
                id: dataPane
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: tablePane.right
                anchors.right: parent.right
                anchors.leftMargin: Theme.space(10)

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: root.inner === "rows" ? Theme.accent
                                                        : Theme.normalBorder
                    border.width: 1
                    radius: Theme.radius
                }

                Text {
                    id: schemaHead
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(8)
                    text: root.columns.length
                          ? ("schema  ·  " + root.columns.length)
                          : "schema"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Flow {
                    id: schemaChips
                    anchors.top: schemaHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(8)
                    anchors.topMargin: Theme.space(4)
                    spacing: Theme.space(6)

                    Repeater {
                        model: root.columns
                        delegate: Rectangle {
                            required property var modelData
                            implicitHeight: Theme.fontBody + Theme.space(6)
                            implicitWidth: schemaChip.implicitWidth + Theme.space(12)
                            color: "transparent"
                            border.color: Theme.normalBorder
                            border.width: 1
                            radius: Theme.radius

                            Text {
                                id: schemaChip
                                anchors.centerIn: parent
                                text: (modelData.name || "") +
                                      (modelData.type ? ("  " + modelData.type) : "") +
                                      (modelData.pk ? "  pk" : "")
                                color: Theme.foreground
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontBody
                            }
                        }
                    }
                }

                Text {
                    id: sampleHead
                    anchors.top: schemaChips.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(8)
                    anchors.topMargin: Theme.space(8)
                    text: root.sample.length
                          ? ("rows  ·  " + root.sample.length)
                          : (root.info.sampleNote || "rows")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Flickable {
                    id: headFlick
                    anchors.top: sampleHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(1)
                    anchors.topMargin: Theme.space(4)
                    height: root.sample.length > 0 ? root.rowH : 0
                    clip: true
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: Math.max(width, root.tableW)
                    contentHeight: root.rowH
                    contentX: gridFlick.contentX
                    visible: root.sample.length > 0

                    Row {
                        height: root.rowH
                        Repeater {
                            model: root.names
                            delegate: Rectangle {
                                required property string modelData
                                required property int index
                                width: root.colWAt(index)
                                height: root.rowH
                                clip: true
                                color: Theme.selectedFill
                                border.color: Theme.normalBorder
                                border.width: 1

                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space(6)
                                    anchors.rightMargin: Theme.space(6)
                                    text: modelData
                                    color: Theme.foreground
                                    font.family: Theme.monoFontFamily
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.NoWrap
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }
                }

                Flickable {
                    id: gridFlick
                    objectName: "dbGrid"
                    anchors.top: headFlick.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.space(1)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: Math.max(width, root.tableW)
                    contentHeight: Math.max(height, root.sample.length * root.rowH)
                    visible: root.sample.length > 0

                    Column {
                        width: gridFlick.contentWidth

                        Repeater {
                            model: root.sample
                            delegate: Row {
                                id: sampleRow
                                required property var modelData
                                required property int index
                                readonly property var rowData: modelData
                                readonly property int rowIndex: index
                                height: root.rowH

                                Repeater {
                                    model: root.names
                                    delegate: Rectangle {
                                        required property string modelData
                                        required property int index
                                        width: root.colWAt(index)
                                        height: root.rowH
                                        clip: true
                                        color: sampleRow.rowIndex % 2 === 1
                                               ? Theme.hoverFill : "transparent"
                                        border.color: Theme.normalBorder
                                        border.width: 1

                                        Text {
                                            anchors.fill: parent
                                            anchors.leftMargin: Theme.space(6)
                                            anchors.rightMargin: Theme.space(6)
                                            text: root.cellText(sampleRow.rowData, modelData)
                                            color: Theme.foreground
                                            font.family: Theme.monoFontFamily
                                            font.pixelSize: Theme.fontBody
                                            wrapMode: Text.NoWrap
                                            elide: Text.ElideRight
                                            maximumLineCount: 1
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
