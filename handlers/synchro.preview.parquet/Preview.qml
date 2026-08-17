import QtQuick
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    peekFlickable: gridFlick
    property var info: ({})

    implicitWidth: 720
    implicitHeight: 480

    readonly property bool ready: root.info && root.info.ok === true
    readonly property var columns: root.ready && root.info.columns ? root.info.columns : []
    readonly property var names: root.ready && root.info.columnNames ? root.info.columnNames : []
    readonly property var sample: root.ready && root.info.sample ? root.info.sample : []
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

    function reload() {
        if (!root.host || !root.file)
            return
        root.info = root.host.readParquet(root.file, 12)
    }

    function cellText(row, name) {
        if (!row || name === undefined)
            return ""
        var v = row[name]
        if (v === undefined || v === null)
            return "·"
        return "" + v
    }

    function fmtCount(n) {
        var x = Number(n)
        if (!isFinite(x))
            return "0"
        return x.toLocaleString()
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

    onFileChanged: root.reload()
    Component.onCompleted: root.reload()

    Text {
        visible: root.info && root.info.ok === false
        anchors.centerIn: parent
        text: root.info && root.info.error ? root.info.error : "not parquet"
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
            id: chips
            width: parent.width
            spacing: Theme.space(6)

            Repeater {
                model: root.ready ? [
                    root.fmtCount(root.columns.length) + " cols",
                    root.fmtCount(root.info.numRows) + " rows",
                    root.info.rowGroups ? (root.fmtCount(root.info.rowGroups) + " groups") : "",
                    root.info.codec ? root.info.codec : ""
                ] : []

                delegate: Rectangle {
                    required property string modelData
                    visible: modelData.length > 0
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
            visible: root.info && root.info.createdBy
            width: parent.width
            text: root.info && root.info.createdBy ? root.info.createdBy : ""
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            elide: Text.ElideMiddle
        }

        Item {
            width: parent.width
            height: parent.height - y

            Item {
                id: schemaPane
                width: Math.min(Theme.space(260), Math.max(Theme.space(168), parent.width * 0.32))
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: Theme.normalBorder
                    border.width: 1
                    radius: Theme.radius
                }

                Text {
                    id: schemaHead
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(8)
                    text: "schema"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                ListView {
                    id: schemaList
                    anchors.top: schemaHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.topMargin: Theme.space(4)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.columns
                    spacing: 0

                    delegate: Item {
                        required property var modelData
                        width: schemaList.width
                        height: root.rowH

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.hoverFill
                            visible: schemaHover.hovered
                        }

                        HoverHandler { id: schemaHover }

                        Text {
                            id: colName
                            anchors.left: parent.left
                            anchors.right: colMeta.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: Theme.space(8)
                            anchors.rightMargin: Theme.space(8)
                            text: modelData.name || ""
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        Text {
                            id: colMeta
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: Theme.space(8)
                            text: (modelData.type || "") +
                                  (modelData.repetition ? "  " + modelData.repetition : "")
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
            }

            Item {
                id: samplePane
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: schemaPane.right
                anchors.right: parent.right
                anchors.leftMargin: Theme.space(10)

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: Theme.normalBorder
                    border.width: 1
                    radius: Theme.radius
                }

                Text {
                    id: sampleHead
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Theme.space(8)
                    text: root.sample.length
                          ? ("sample  ·  " + root.sample.length)
                          : (root.info.sampleNote || "sample")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideMiddle
                }

                Flickable {
                    id: gridFlick
                    anchors.top: sampleHead.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.space(1)
                    anchors.topMargin: Theme.space(4)
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: Math.max(width, root.tableW)
                    contentHeight: Math.max(height, (root.sample.length + 1) * root.rowH)
                    visible: root.sample.length > 0

                    Column {
                        width: gridFlick.contentWidth

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
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        wrapMode: Text.NoWrap
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }

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
                                            font.family: Theme.fontFamily
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

    Keys.onEscapePressed: if (root.host) root.host.close()
}
